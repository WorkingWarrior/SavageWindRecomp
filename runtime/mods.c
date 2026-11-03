// The player's mods folder: the list, its order and switches (runtime/mods.h)
#include "mods.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cheat_vm.h"
#include "kernel.h"
#include "game_profile.h"
#include "exefs_patch.h"
#include "hle.h"
#include "mods_scan.h"
#include "paths.h"
#include "platform.h"
#include "rt.h"

#define MODS_MAX 256

typedef struct { Mod m; ModFound f; } Entry;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Entry *list;  // top first
static int n;
static bool scanned;
static char folder[1100];
static char list_file[1200];  // <folder>/mods.ini
// the run's: the mods that were on and usable when fs.c first asked, and what it counted for them
static Entry *start;
static int nstart = -1;

static const char *title(void) { return game_is_botw() ? "01007EF00011E000" : "0100F2C0115B6000"; }
static const char *aoc_title(void) { return game_is_botw() ? "01007EF00011F001" : NULL; }

static void cheats_sync_locked(void);
static void cheats_get_locked(Mod *out);

static void save_locked(void) {
    char tmp[1300];
    snprintf(tmp, sizeof tmp, "%s.tmp", list_file);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "# Savage Wind mods, top first (the top mod wins a file two mods change): +name on, -name off\n");
    for (int i = 0; i < n; i++) fprintf(f, "%c%s\n", list[i].m.on ? '+' : '-', list[i].m.name);
    if (fclose(f) == 0) rename(tmp, list_file);
}

static int by_name(const void *a, const void *b) { return strcmp(((const Entry *)a)->m.name, ((const Entry *)b)->m.name); }

static void scan_locked(void) {
    snprintf(folder, sizeof folder, "%s/mods", rt_data_root());
    mkdir(folder, 0755);
    snprintf(folder, sizeof folder, "%s/mods/%s", rt_data_root(), game_profile_name());  // (each game its own)
    mkdir(folder, 0755);
    snprintf(list_file, sizeof list_file, "%s/mods.ini", folder);
    // the saved order and switches
    char *saved[MODS_MAX];
    bool saved_off[MODS_MAX];
    int nsaved = 0;
    FILE *f = fopen(list_file, "r");
    char line[512];
    while (f && fgets(line, sizeof line, f) && nsaved < MODS_MAX) {
        line[strcspn(line, "\r\n")] = 0;
        if ((line[0] != '+' && line[0] != '-') || !line[1]) continue;
        saved_off[nsaved] = line[0] == '-';
        saved[nsaved++] = strdup(line + 1);
    }
    if (f) fclose(f);
    // the folders
    Entry *found = calloc(MODS_MAX, sizeof *found);
    int nfound = 0;
    DIR *d = opendir(folder);
    struct dirent *e;
    while (d && (e = readdir(d)) && nfound < MODS_MAX) {
        if (e->d_name[0] == '.' || e->d_name[0] == '_') continue;
        char p[1400];
        snprintf(p, sizeof p, "%s/%s", folder, e->d_name);
        if (!ms_is_dir(p)) continue;
        ModFound f;
        mods_scan_one(p, title(), aoc_title(), &f);
        static char coll[MODS_MAX][1100];  // (a copied emulator collection: one mod per folder, named <folder>/<mod>)
        int nc = f.romfs[0] || f.aoc[0] || f.code ? 0 : mods_scan_collection(p, title(), aoc_title(), coll, MODS_MAX);
        for (int c = 0; c < (nc ? nc : 1) && nfound < MODS_MAX; c++) {
            Entry *x = &found[nfound++];
            if (nc) {
                snprintf(x->m.name, sizeof x->m.name, "%s/%s", e->d_name, strrchr(coll[c], '/') + 1);
                mods_scan_one(coll[c], title(), aoc_title(), &x->f);
            } else {
                snprintf(x->m.name, sizeof x->m.name, "%s", e->d_name);
                x->f = f;
            }
            snprintf(x->m.layout, sizeof x->m.layout, "%s", x->f.layout);
            snprintf(x->m.other_title, sizeof x->m.other_title, "%s", x->f.other_title);
            x->m.aoc = x->f.aoc[0] != 0;
            x->m.exefs = x->f.nexefs > 0;
            x->m.cheats = x->f.cheats[0] != 0;
            x->m.problem = x->f.romfs[0] || x->f.aoc[0] || x->f.nexefs || x->f.cheats[0] ? MOD_OK
                         : x->f.other_title[0] ? MOD_OTHER_GAME : MOD_EMPTY;
            x->m.on = true;
        }
    }
    if (d) closedir(d);
    qsort(found, nfound, sizeof *found, by_name);
    const char *names[MODS_MAX];
    int order[MODS_MAX];
    for (int i = 0; i < nfound; i++) names[i] = found[i].m.name;
    int k = mods_scan_order(names, nfound, (const char *const *)saved, nsaved, order);
    free(list);
    list = calloc(k ? k : 1, sizeof *list);
    n = k;
    for (int i = 0; i < k; i++) {
        list[i] = found[order[i]];
        for (int s = 0; s < nsaved; s++)
            if (!strcmp(saved[s], list[i].m.name)) { list[i].m.on = !saved_off[s]; break; }
    }
    free(found);
    for (int s = 0; s < nsaved; s++) free(saved[s]);
    if (!scanned || k != nsaved) save_locked();  // (new mods written in, gone ones dropped)
    scanned = true;
    cheats_sync_locked();
}

static void ensure_locked(void) { if (!scanned) scan_locked(); }

int mods_count(void) {
    pthread_mutex_lock(&lock);
    ensure_locked();
    int k = n;
    pthread_mutex_unlock(&lock);
    return k;
}

bool mods_get(int i, Mod *out) {
    pthread_mutex_lock(&lock);
    ensure_locked();
    bool ok = i >= 0 && i < n;
    if (ok) {
        *out = list[i].m;
        for (int s = 0; s < nstart; s++)  // (the counts are the run's: by name, the list may have moved since)
            if (!strcmp(start[s].m.name, out->name)) {
                out->in_run = true;
                out->counted = start[s].m.counted;
                out->replaced = start[s].m.replaced, out->added = start[s].m.added, out->shadowed = start[s].m.shadowed;
                out->patch_files = start[s].m.patch_files;
                out->patch_data = start[s].m.patch_data, out->patch_code = start[s].m.patch_code;
            }
        cheats_get_locked(out);
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

const char *mods_folder(void) {
    pthread_mutex_lock(&lock);
    ensure_locked();
    pthread_mutex_unlock(&lock);
    return folder;
}

void mods_rescan(void) {
    pthread_mutex_lock(&lock);
    scan_locked();  // (the edits are saved already: the scan reads them back)
    pthread_mutex_unlock(&lock);
}

void mods_set_on(int i, bool on) {
    pthread_mutex_lock(&lock);
    if (i >= 0 && i < n && list[i].m.on != on) list[i].m.on = on, save_locked(), cheats_sync_locked();
    pthread_mutex_unlock(&lock);
}

void mods_move(int i, int delta) {
    pthread_mutex_lock(&lock);
    int j = i + delta;
    if (i >= 0 && i < n && j >= 0 && j < n) {
        Entry t = list[i];
        list[i] = list[j], list[j] = t;
        save_locked(), cheats_sync_locked();
    }
    pthread_mutex_unlock(&lock);
}

// It has what the game takes only at its start (RomFS files, ExeFS patches; cheats switch at once)
static bool at_start(const Entry *x) { return x->f.romfs[0] || x->f.aoc[0] || x->f.nexefs; }

bool mods_changed(void) {
    pthread_mutex_lock(&lock);
    bool changed = false;
    if (nstart >= 0) {
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (!list[i].m.on || list[i].m.problem != MOD_OK || !at_start(&list[i])) continue;
            while (k < nstart && !at_start(&start[k])) k++;
            changed |= k >= nstart || strcmp(start[k].m.name, list[i].m.name);
            k++;
        }
        while (k < nstart && !at_start(&start[k])) k++;
        changed |= k < nstart;
    }
    pthread_mutex_unlock(&lock);
    return changed;
}

// The run's list (the mods on and usable), fixed at the first ask
static void start_locked(void) {
    if (nstart < 0) {
        ensure_locked();
        start = calloc(n ? n : 1, sizeof *start);
        nstart = 0;
        for (int i = 0; i < n; i++) {
            const Entry *x = &list[i];
            if (x->m.problem != MOD_OK) LOG("[mods] %s: not used (%s)\n", x->m.name,
                                            x->m.problem == MOD_OTHER_GAME ? "made for another game"
                                            : "no RomFS files");
            else if (!x->m.on) LOG("[mods] %s: off\n", x->m.name);
            else {
                LOG("[mods] %s: on (%s%s%s%s)\n", x->m.name, x->m.layout[0] ? x->m.layout : "no RomFS files",
                    x->m.aoc ? ", add-on content too" : "", x->m.exefs ? ", ExeFS patches" : "",
                    x->m.cheats ? ", cheats" : "");
                start[nstart++] = *x;
            }
        }
    }
}

int mods_roots(int image, const char **roots, int *mod, int max) {
    pthread_mutex_lock(&lock);
    start_locked();
    int k = 0;
    for (int i = 0; i < nstart && k < max; i++) {
        const char *r = image == 0 ? start[i].f.romfs : image == 1 ? start[i].f.aoc : "";
        if (!r[0]) continue;
        roots[k] = r, mod[k] = i, k++;
    }
    pthread_mutex_unlock(&lock);
    return k;
}

void mods_note(int mod, int replaced, int added, int shadowed) {
    pthread_mutex_lock(&lock);
    if (mod >= 0 && mod < nstart) {
        Mod *m = &start[mod].m;
        m->counted = true;
        m->replaced += replaced, m->added += added, m->shadowed += shadowed;
    }
    pthread_mutex_unlock(&lock);
}

// ---- ExeFS patches
RT_ON_RESET(mods_reset) {  // (the loader applies them again)
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nstart; i++) start[i].m.patch_files = start[i].m.patch_data = start[i].m.patch_code = 0;
    pthread_mutex_unlock(&lock);
}

typedef struct { Module *m; int data, code, outside; } PatchCtx;

static void patch_record(void *ctx, int64_t off, const uint8_t *bytes, uint32_t n) {
    PatchCtx *c = ctx;
    Module *m = c->m;
    if (off < 0 || (uint64_t)off + n > m->size) { c->outside++; return; }
    uint64_t text_end = m->text_off + m->text_size;
    if ((uint64_t)off < text_end) {  // (code: recompiled ahead of time, so its bytes here are not what runs)
        c->code++;
        uint64_t skip = text_end - (uint64_t)off;
        if (skip >= n) return;
        off += (int64_t)skip, bytes += skip, n -= (uint32_t)skip;
    }
    memcpy((uint8_t *)(uintptr_t)(m->base + (uint64_t)off), bytes, n);
    c->data++;
}

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    uint8_t *d = NULL;
    *n = 0;
    if (f && !fseek(f, 0, SEEK_END)) {
        long len = ftell(f);
        if (len > 0 && len <= (16 << 20) && !fseek(f, 0, SEEK_SET) && (d = malloc((size_t)len + 1)) &&
            fread(d, 1, (size_t)len, f) == (size_t)len)
            *n = (size_t)len, d[len] = 0;
        else free(d), d = NULL;
    }
    if (f) fclose(f);
    return d;
}

void mods_exefs_apply(Module *m, const char *build_id) {
    pthread_mutex_lock(&lock);
    start_locked();
    for (int i = nstart - 1; i >= 0; i--) {
        Entry *x = &start[i];
        for (int e = 0; e < x->f.nexefs; e++) {
            DIR *d = opendir(x->f.exefs[e]);
            struct dirent *de;
            while (d && (de = readdir(d))) {
                const char *dot = strrchr(de->d_name, '.');
                bool ips = dot && !strcasecmp(dot, ".ips"), pch = dot && !strcasecmp(dot, ".pchtxt");
                if (!ips && !pch) continue;
                if (ips && !exefs_bid_match(de->d_name, build_id)) continue;
                char p[1400];
                snprintf(p, sizeof p, "%s/%s", x->f.exefs[e], de->d_name);
                size_t n;
                uint8_t *data = slurp(p, &n);
                char bid[80];
                if (!data || (pch && (!exefs_pchtxt_bid((const char *)data, n, bid, sizeof bid) ||
                                      !exefs_bid_match(bid, build_id)))) {
                    free(data);
                    continue;
                }
                PatchCtx c = {m, 0, 0, 0};
                int records = ips ? exefs_ips(data, n, patch_record, &c) : exefs_pchtxt((const char *)data, n, patch_record, &c);
                free(data);
                if (records < 0) { LOG("[mods] %s: not an IPS patch, or cut short\n", p); continue; }
                x->m.patch_files++, x->m.patch_data += c.data, x->m.patch_code += c.code;
                LOG("[mods] %s on %s: %d records on its data applied, %d on its code left out (recompiled)%s\n", p,
                    m->name, c.data, c.code, c.outside ? ", some outside the module" : "");
            }
            if (d) closedir(d);
        }
    }
    pthread_mutex_unlock(&lock);
}

// ---- cheats: every mod's in the list, loaded once the game's build id is known (and files found later by a rescan);
// each file's run while its mod is on, switched at once
typedef struct {
    char mod[256];
    uint32_t *op, n;  // the file's codes: master, then the rest
    int count;
    CheatVm vm;
    bool on;     // its mod is on (the lock)
    int rank;    // its mod's place in the list, top 0 (the lock)
    _Atomic bool stopped, code;  // (the cheat thread)
} CheatProg;
static CheatProg **progs;
static int nprogs;
static char cheat_bid[80];      // the main module's build id ("" before the loader gave it)
static char (*seen)[1400];      // the cheat files read (loaded or not): a rescan reads only new ones
static int nseen;
#define PROGS_MAX 256

static bool cheat_rd(uint64_t a, void *b, size_t n) { return kernel_guest_copy(a, b, n, GUEST_READ); }

static bool in_code(uint64_t a, size_t n) {
    for (int i = 0; i < g_nmodules; i++) {
        uint64_t lo = g_modules[i].base + g_modules[i].text_off, hi = lo + ((g_modules[i].text_size + 0xFFF) & ~0xFFFull);
        if (a < hi && a + n > lo) return true;
    }
    return false;
}

static CheatProg *cur;  // (the program the cheat thread runs, for cheat_wr)
static bool cheat_wr(uint64_t a, const void *b, size_t n) {
    if (in_code(a, n)) {  // (recompiled ahead of time: its bytes here are not what runs)
        if (!cur->code) LOG("[mods] %s: a cheat writes to the game's code at %llx: left out (recompiled)\n", cur->mod,
                            (unsigned long long)a);
        cur->code = true;
        return false;
    }
    return kernel_guest_copy(a, (void *)b, n, GUEST_WRITE_RO);
}

static void cheat_log(void *ctx, unsigned id, uint64_t v) {
    static int left = 64;
    if (left > 0) left--, LOG("[mods] %s: cheat log %u: %llx\n", ((CheatProg *)ctx)->mod, id, (unsigned long long)v);
}

// FF0 / FF1: the game paused (kernel_guest_pause) and resumed; a run that leaves it paused resumes it at its end
static bool cheat_paused;
static void cheat_pause(void *ctx, bool on) {
    if (on == cheat_paused) return;
    cheat_paused = on;
    if (!on) { kernel_guest_resume(); return; }
    static bool logged;
    if (!kernel_guest_pause(50000000) && !logged)
        logged = true, LOG("[mods] %s: a cheat paused the game, which did not stop in 50 ms: its codes ran anyway\n",
                           ((CheatProg *)ctx)->mod);
}

uint64_t input_game_buttons(void);  // runtime/hle/input.c

static int by_rank(const void *a, const void *b) {  // (the lowest mod first: the top one's writes come last)
    return (*(CheatProg *const *)b)->rank - (*(CheatProg *const *)a)->rank;
}

static void *cheat_thread(void *arg) {
    (void)arg;
    Module *main = loader_module("main");
    static CheatProg *run[PROGS_MAX];
    uint64_t next = plat_now_ns();
    for (;;) {
        next += 1000000000ull / 12;  // (twelve times a second, as Atmosphere and the emulators run them)
        if (next < plat_now_ns()) next = plat_now_ns();  // (behind: start again, no catching up)
        plat_sleep_until_ns(next);
        int k = 0;
        pthread_mutex_lock(&lock);
        for (int i = 0; i < nprogs; i++)
            if (progs[i]->on && !progs[i]->stopped) run[k++] = progs[i];
        qsort(run, k, sizeof *run, by_rank);
        pthread_mutex_unlock(&lock);
        CheatEnv e = {.base = {main ? main->base : MAIN_BASE, HEAP_BASE, ALIAS_BASE, GUEST_LO, 0},
                      .keys = input_game_buttons(), .rd = cheat_rd, .wr = cheat_wr, .log = cheat_log,
                      .pause = cheat_pause};
        for (int i = 0; i < k; i++) {
            CheatProg *p = run[i];  // (never freed: a program stays while the program runs)
            cur = p, e.ctx = p;
            int r = cheat_vm_run(&p->vm, p->op, p->n, &e);
            cheat_pause(p, false);
            if (r == CHEAT_DONE) continue;
            p->stopped = true;
            LOG("[mods] %s: its cheats stopped (%s)\n", p->mod, r == CHEAT_BAD ? "a code that isn't one" : "a loop that never ends");
        }
    }
    return NULL;
}

static void cheats_load_locked(const Entry *x) {
    static Cheat c[256];
    DIR *d = opendir(x->f.cheats);
    struct dirent *de;
    while (d && (de = readdir(d)) && nprogs < PROGS_MAX) {
        const char *dot = strrchr(de->d_name, '.');
        if (!dot || strcasecmp(dot, ".txt") || !exefs_bid_match(de->d_name, cheat_bid)) continue;
        char p[1400];
        snprintf(p, sizeof p, "%s/%s", x->f.cheats, de->d_name);
        int s = 0;
        while (s < nseen && strcmp(seen[s], p)) s++;
        if (s < nseen) continue;
        seen = realloc(seen, sizeof *seen * (nseen + 1));
        snprintf(seen[nseen++], sizeof *seen, "%s", p);
        size_t n;
        uint8_t *data = slurp(p, &n);
        int k = data ? cheat_parse((const char *)data, n, c, 256) : -1;
        free(data);
        if (k <= 0) { LOG("[mods] %s: no cheats in it\n", p); continue; }
        CheatProg *g = calloc(1, sizeof *g);
        snprintf(g->mod, sizeof g->mod, "%s", x->m.name);
        g->op = malloc(sizeof *g->op * CHEAT_OPS_MAX * (k ? k : 1));
        // A cheat that patches the game's code is left out whole: what else it writes (code of its own put in a
        // free spot of the data) only makes sense with that patch, which a recompiled game can't take
        static bool code[256];
        Module *main = loader_module("main");
        int broken = 0, coded = 0;
        for (int j = 0; j < k; j++) {
            code[j] = !c[j].broken && main && cheat_writes_main(&c[j], main->text_off, main->text_off + ((main->text_size + 0xFFF) & ~0xFFFull));
            broken += c[j].broken, coded += code[j];
        }
        for (int pass = 0; pass < 2; pass++)  // (the master code first)
            for (int j = 0; j < k; j++)
                if (c[j].master == !pass && !c[j].broken && !code[j])
                    memcpy(g->op + g->n, c[j].op, sizeof *g->op * c[j].n), g->n += c[j].n;
        g->count = k - broken;
        g->code = coded > 0;
        cheat_vm_init(&g->vm, g->n);
        progs = realloc(progs, sizeof *progs * (nprogs + 1));
        progs[nprogs++] = g;
        LOG("[mods] %s: %d cheats%s, %d of them changing the game's code (left out: recompiled)\n", p, g->count,
            broken ? ", some left out (a code that isn't 8 hex digits)" : "", coded);
        for (int j = 0; j < k; j++)
            if (c[j].broken) LOG("[mods]   [%s] left out: a code that isn't 8 hex digits\n", c[j].name);
            else if (code[j]) LOG("[mods]   [%s] left out: it changes the game's code\n", c[j].name);
    }
    if (d) closedir(d);
}

// The list's mods' cheat files loaded (the ones not read yet: a file copied into a known mod comes in too), each
// file switched as its mod is
static void cheats_sync_locked(void) {
    if (!cheat_bid[0]) return;
    for (int i = 0; i < n; i++) {
        const Entry *x = &list[i];
        if (x->f.cheats[0] && x->m.problem == MOD_OK) cheats_load_locked(x);
    }
    for (int p = 0; p < nprogs; p++) {
        CheatProg *g = progs[p];
        bool was = g->on;
        g->on = false;
        for (int i = 0; i < n; i++)
            if (!strcmp(list[i].m.name, g->mod)) { g->on = list[i].m.on, g->rank = i; break; }
        if (g->on != was) LOG("[mods] %s: its cheats %s\n", g->mod, g->on ? "on" : "off");
    }
}

static void cheats_get_locked(Mod *out) {
    out->cheats_checked = cheat_bid[0] != 0;
    for (int p = 0; p < nprogs; p++) {
        CheatProg *g = progs[p];
        if (strcmp(g->mod, out->name)) continue;
        out->cheat_files++, out->cheat_count += g->count;
        out->cheat_bad |= g->stopped, out->cheat_code |= g->code;
    }
}

void mods_cheats_start(const char *main_build_id) {
    pthread_mutex_lock(&lock);
    bool first = !cheat_bid[0];  // (a reboot: the same game, its cheats keep running)
    if (first) {
        ensure_locked();
        snprintf(cheat_bid, sizeof cheat_bid, "%s", main_build_id);
        cheats_sync_locked();
    }
    pthread_mutex_unlock(&lock);
    pthread_t th;
    if (first && pthread_create(&th, NULL, cheat_thread, NULL)) LOG("[mods] the cheats' thread did not start\n");
}
