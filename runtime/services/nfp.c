// nfp:user: amiibo from the player's own dumps (runtime/amiibo.h), over one virtual NFC reader. ListDevices finds it;
// StartDetection makes it search and brings up the list of dumps (runtime/ui/menu.mm); a dump picked goes on it (tag
// found: the activate event); Mount, GetTagInfo and GetModelInfo read it (runtime/amiibo_dump.h); StopDetection takes it
// off (the deactivate event when a tag was on). Only the dump's plaintext is read: the figure's application area (the
// game's own data on it, encrypted on the tag) starts empty for each dump and lives only while Savage Wind runs.
// Commands and layouts from switchbrew's NFC services page; after weeping-of-the-empire v0.1.8.2
// engine/runtime/pc_amiibo.cpp (Copyright (c) 2026 daniel-madrid-07, MIT License), which does this for TotK by
// replacing nn::nfp's functions.
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../amiibo.h"
#include "../amiibo_dump.h"
#include "../hle.h"
#include "../ipc.h"
#include "../paths.h"

#define NFP_DEVICE_NOT_FOUND MAKE_RESULT(115, 64)
#define NFP_TAG_NOT_FOUND MAKE_RESULT(115, 97)
#define NFP_AREA_NOT_CREATED MAKE_RESULT(115, 128)  // (hypothesis: the code emulators gave for "no application area")
#define DUMPS_MAX 64
#define AREA_SIZE 0xD8
enum { DEV_INITIALIZED = 0, DEV_SEARCHING = 1, DEV_TAG_FOUND = 2, DEV_TAG_REMOVED = 3, DEV_TAG_MOUNTED = 4 };
static const uint64_t DEVICE = 0x4F4249494D41;  // our reader's DeviceHandle (opaque to the game)

typedef struct { char name[128]; int problem; AmiiboTag tag; uint8_t area[AREA_SIZE]; bool has_area; } Dump;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Dump dumps[DUMPS_MAX];
static int ndumps = -1;  // (-1: not read yet)
static char folder[1100];
static int dev = DEV_INITIALIZED, on_reader = -1;
static KEvent *activate, *deactivate;

extern void menu_amiibo_open(void);   // runtime/ui/menu.mm: the list of dumps comes up / goes
extern void menu_amiibo_close(void);

RT_ON_RESET(nfp_reset) {
    pthread_mutex_lock(&lock);
    dev = DEV_INITIALIZED, on_reader = -1;
    activate = deactivate = NULL;  // (the kernel's objects go with the boot)
    pthread_mutex_unlock(&lock);
}

static void read_dumps_locked(void) {
    snprintf(folder, sizeof folder, "%s/amiibo", rt_data_root());
    mkdir(folder, 0755);
    Dump old[DUMPS_MAX];  // (an application area made this run stays with its dump)
    int nold = ndumps > 0 ? ndumps : 0;
    memcpy(old, dumps, sizeof *old * nold);
    ndumps = 0;
    DIR *d = opendir(folder);
    struct dirent *e;
    while (d && (e = readdir(d)) && ndumps < DUMPS_MAX) {
        const char *dot = strrchr(e->d_name, '.');
        if (e->d_name[0] == '.' || !dot || (strcasecmp(dot, ".bin") && strcasecmp(dot, ".nfc"))) continue;
        Dump *x = &dumps[ndumps];
        memset(x, 0, sizeof *x);
        snprintf(x->name, sizeof x->name, "%.*s", (int)(dot - e->d_name), e->d_name);
        x->problem = AMIIBO_UNREADABLE;
        char p[1400];
        snprintf(p, sizeof p, "%s/%s", folder, e->d_name);
        FILE *f = fopen(p, "rb");
        uint8_t file[64 * 1024], img[1024];
        size_t n = f ? fread(file, 1, sizeof file, f) : 0;
        if (f) fclose(f);
        if (n) x->problem = amiibo_parse(img, amiibo_image(file, n, img, sizeof img), &x->tag);
        for (int i = 0; i < nold; i++)
            if (!strcmp(old[i].name, x->name) && old[i].has_area)
                memcpy(x->area, old[i].area, AREA_SIZE), x->has_area = true;
        ndumps++;
    }
    if (d) closedir(d);
    for (int i = 1; i < ndumps; i++)  // (by name)
        for (int j = i; j > 0 && strcmp(dumps[j - 1].name, dumps[j].name) > 0; j--) {
            Dump t = dumps[j];
            dumps[j] = dumps[j - 1], dumps[j - 1] = t;
        }
    on_reader = -1;
}

int amiibo_count(void) {
    pthread_mutex_lock(&lock);
    if (ndumps < 0) read_dumps_locked();
    int n = ndumps;
    pthread_mutex_unlock(&lock);
    return n;
}

bool amiibo_get(int i, char *name, int cap, const char **problem) {
    static const char *const why[] = {"", "Not an amiibo dump: an amiibo image is 540 bytes.",
                                      "Not an amiibo dump: the amiibo marker is missing.",
                                      "Not an amiibo dump: unknown figure data.", "The file cannot be read."};
    pthread_mutex_lock(&lock);
    bool ok = i >= 0 && i < ndumps;
    if (ok) snprintf(name, cap, "%s", dumps[i].name), *problem = why[dumps[i].problem];
    pthread_mutex_unlock(&lock);
    return ok;
}

const char *amiibo_folder(void) {
    amiibo_count();
    return folder;
}

bool amiibo_searching(void) {
    pthread_mutex_lock(&lock);
    bool s = dev == DEV_SEARCHING;
    pthread_mutex_unlock(&lock);
    return s;
}

static void signal_event(KEvent *ev) {
    if (!ev) return;
    klock();
    kernel_signal(ev);
    pthread_mutex_unlock(&g_klock);
}

bool amiibo_scan(int i) {
    pthread_mutex_lock(&lock);
    bool ok = dev == DEV_SEARCHING && i >= 0 && i < ndumps && dumps[i].problem == AMIIBO_OK;
    KEvent *ev = activate;
    if (ok) {
        on_reader = i, dev = DEV_TAG_FOUND;
        const uint8_t *id = dumps[i].tag.id;
        LOG("[nfp] amiibo %s on the reader (id %02x%02x%02x%02x%02x%02x%02x%02x)\n", dumps[i].name, id[0], id[1], id[2],
            id[3], id[4], id[5], id[6], id[7]);
    }
    pthread_mutex_unlock(&lock);
    if (ok) signal_event(ev);
    return ok;
}

// (test) SAVAGEWIND_AMIIBO_AUTO=<dump name>: that dump goes on the reader a second after the game starts searching
static void *auto_scan(void *arg) {
    (void)arg;
    sleep(1);
    const char *want = getenv("SAVAGEWIND_AMIIBO_AUTO");
    char name[128];
    const char *problem;
    for (int i = 0; amiibo_get(i, name, sizeof name, &problem); i++)
        if (!strcmp(name, want)) { amiibo_scan(i); menu_amiibo_close(); }
    return NULL;
}

static uint8_t *out(IpcReq *req, uint64_t size) {  // the out buffer, if it holds `size` bytes
    IpcBuf *b = out_buf(req, 0);
    return b && b->size >= size ? (uint8_t *)(uintptr_t)b->addr : NULL;
}

static void nfp_user(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    bool close = false, open = false;
    KEvent *ev = NULL;
    pthread_mutex_lock(&lock);
    bool tag_on = dev == DEV_TAG_FOUND || dev == DEV_TAG_MOUNTED;
    Dump *t = on_reader >= 0 ? &dumps[on_reader] : NULL;
    switch (req->cmd) {
    case 0: dev = DEV_INITIALIZED; break;                                    // Initialize
    case 1: dev = DEV_INITIALIZED, activate = deactivate = NULL; close = true; break;  // Finalize
    case 2: {                                                                // ListDevices: our reader
        uint8_t *b = out(req, 8);
        if (b) memcpy(b, &DEVICE, 8);
        OUT(int32_t, 0) = b ? 1 : 0;
        resp_size(resp, 4);
        break;
    }
    case 3:                                                                  // StartDetection
        if (ndumps < 0 || dev != DEV_SEARCHING) read_dumps_locked();
        dev = DEV_SEARCHING, on_reader = -1, open = true;
        LOG("[nfp] the game searches for an amiibo (%d dumps in %s)\n", ndumps, folder);
        break;
    case 4:                                                                  // StopDetection
        if (tag_on) ev = deactivate;
        dev = DEV_INITIALIZED, close = true;
        break;
    case 5: if (!tag_on) resp->result = NFP_TAG_NOT_FOUND; else dev = DEV_TAG_MOUNTED; break;  // Mount
    case 6: if (dev == DEV_TAG_MOUNTED) dev = DEV_TAG_FOUND; break;          // Unmount
    case 7:                                                                  // OpenApplicationArea(access id)
        if (!tag_on || !t) resp->result = NFP_TAG_NOT_FOUND;
        else if (!t->has_area) resp->result = NFP_AREA_NOT_CREATED;
        break;
    case 8: {                                                                // GetApplicationArea -> size
        uint8_t *b = out(req, 1);
        uint32_t n = b ? (uint32_t)(out_buf(req, 0)->size < AREA_SIZE ? out_buf(req, 0)->size : AREA_SIZE) : 0;
        if (!t || !t->has_area) { resp->result = NFP_AREA_NOT_CREATED; break; }
        if (b) memcpy(b, t->area, n);
        OUT(uint32_t, 0) = n;
        resp_size(resp, 4);
        break;
    }
    case 9: case 12: case 24: {                                              // Set / Create / RecreateApplicationArea
        IpcBuf *b = in_buf(req, 0);
        if (!t) { resp->result = NFP_TAG_NOT_FOUND; break; }
        memset(t->area, 0, AREA_SIZE);
        if (b) memcpy(t->area, (const void *)(uintptr_t)b->addr, b->size < AREA_SIZE ? b->size : AREA_SIZE);
        t->has_area = true;
        LOG("[nfp] application area of %s %s (%llu bytes, kept while Savage Wind runs)\n", t->name,
            req->cmd == 9 ? "written" : "made", (unsigned long long)(b ? b->size : 0));
        break;
    }
    case 10: case 11: break;                                                 // Flush, Restore
    case 13: {                                                               // GetTagInfo
        uint8_t *b = out(req, 0x58);
        if (!tag_on || !t) resp->result = NFP_TAG_NOT_FOUND;
        else if (b) amiibo_tag_info(&t->tag, b);
        break;
    }
    case 14: {                                                               // GetRegisterInfo: the dump's name
        uint8_t *b = out(req, 0x100);
        if (dev != DEV_TAG_MOUNTED || !t) { resp->result = NFP_TAG_NOT_FOUND; break; }
        if (!b) break;
        memset(b, 0, 0x100);
        const uint8_t date[4] = {0xE1, 0x07, 3, 3};  // (2017-03-03: u16 year, month, day)
        memcpy(b + 0x58, date, 4);
        snprintf((char *)b + 0x5C, 0x29, "%.10s", t->name);
        break;
    }
    case 15: {                                                               // GetCommonInfo
        uint8_t *b = out(req, 0x40);
        if (dev != DEV_TAG_MOUNTED || !t) { resp->result = NFP_TAG_NOT_FOUND; break; }
        if (!b) break;
        memset(b, 0, 0x40);
        const uint8_t date[4] = {0xE1, 0x07, 3, 3};
        const uint32_t area = AREA_SIZE;
        memcpy(b, date, 4);
        memcpy(b + 8, &area, 4);
        break;
    }
    case 16: {                                                               // GetModelInfo
        uint8_t *b = out(req, 0x40);
        if (dev != DEV_TAG_MOUNTED || !t) resp->result = NFP_TAG_NOT_FOUND;
        else if (b) amiibo_model_info(&t->tag, b);
        break;
    }
    case 17: case 18: {                                                      // Attach{Activate,Deactivate}Event
        uint32_t w, r;
        KEvent *e = kernel_create_event_pair(&w, &r);
        if (req->cmd == 17) activate = e;
        else deactivate = e;
        resp->copy_handles[resp->ncopy++] = r;
        break;
    }
    case 23: {                                                               // AttachAvailabilityChangeEvent: never
        uint32_t w, r;
        kernel_create_event_pair(&w, &r);
        resp->copy_handles[resp->ncopy++] = r;
        break;
    }
    case 19: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;                // GetState: initialized
    case 20: OUT(uint32_t, 0) = (uint32_t)dev; resp_size(resp, 4); break;    // GetDeviceState
    case 21: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;                // GetNpadId: the first player's
    case 22: OUT(uint32_t, 0) = AREA_SIZE; resp_size(resp, 4); break;        // GetApplicationAreaSize
    default: LOG("[nfp] IUser cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
    if (resp->result)
        LOG("[nfp] IUser cmd %u: %#x\n", req->cmd, resp->result);
    pthread_mutex_unlock(&lock);
    signal_event(ev);
    if (open) {
        menu_amiibo_open();
        pthread_t th;
        if (getenv("SAVAGEWIND_AMIIBO_AUTO") && !pthread_create(&th, NULL, auto_scan, NULL)) pthread_detach(th);
    }
    if (close) menu_amiibo_close();
}

SERVICE("nfp:user", nfp_user_manager) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("IUser(nfp)", nfp_user, NULL));  // CreateUserInterface
    else LOG("[nfp] IUserManager cmd %u (ok)\n", req->cmd);
}
