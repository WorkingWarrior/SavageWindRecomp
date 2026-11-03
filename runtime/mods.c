// The player's mods folder: the list, its order and switches (runtime/mods.h)
#include "mods.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "game_profile.h"
#include "mods_scan.h"
#include "paths.h"
#include "rt.h"

#define MODS_MAX 256
#define LIST_FILE "mods.ini"

typedef struct { Mod m; ModFound f; } Entry;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Entry *list;  // top first
static int n;
static bool scanned;
static char folder[1100];
// the run's: the mods that were on and usable when fs.c first asked, and what it counted for them
static Entry *start;
static int nstart = -1;

static const char *title(void) { return game_is_botw() ? "01007EF00011E000" : "0100F2C0115B6000"; }
static const char *aoc_title(void) { return game_is_botw() ? "01007EF00011F001" : NULL; }

static void save_locked(void) {
    FILE *f = fopen(LIST_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "# Savage Wind mods, top first (the top mod wins a file two mods change): +name on, -name off\n");
    for (int i = 0; i < n; i++) fprintf(f, "%c%s\n", list[i].m.on ? '+' : '-', list[i].m.name);
    if (fclose(f) == 0) rename(LIST_FILE ".tmp", LIST_FILE);
}

static int by_name(const void *a, const void *b) { return strcmp(((const Entry *)a)->m.name, ((const Entry *)b)->m.name); }

static void scan_locked(void) {
    snprintf(folder, sizeof folder, "%s/mods", rt_data_root());
    mkdir(folder, 0755);
    // the saved order and switches
    char *saved[MODS_MAX];
    bool saved_off[MODS_MAX];
    int nsaved = 0;
    FILE *f = fopen(LIST_FILE, "r");
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
        Entry *x = &found[nfound++];
        snprintf(x->m.name, sizeof x->m.name, "%s", e->d_name);
        mods_scan_one(p, title(), aoc_title(), &x->f);
        snprintf(x->m.layout, sizeof x->m.layout, "%s", x->f.layout);
        snprintf(x->m.other_title, sizeof x->m.other_title, "%s", x->f.other_title);
        x->m.aoc = x->f.aoc[0] != 0;
        x->m.code = x->f.code;
        x->m.problem = x->f.romfs[0] || x->f.aoc[0] ? MOD_OK : x->f.other_title[0] ? MOD_OTHER_GAME
                     : x->f.code ? MOD_CODE_ONLY : MOD_EMPTY;
        x->m.on = true;
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
                out->counted = start[s].m.counted;
                out->replaced = start[s].m.replaced, out->added = start[s].m.added, out->shadowed = start[s].m.shadowed;
            }
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
    if (i >= 0 && i < n && list[i].m.on != on) list[i].m.on = on, save_locked();
    pthread_mutex_unlock(&lock);
}

void mods_move(int i, int delta) {
    pthread_mutex_lock(&lock);
    int j = i + delta;
    if (i >= 0 && i < n && j >= 0 && j < n) {
        Entry t = list[i];
        list[i] = list[j], list[j] = t;
        save_locked();
    }
    pthread_mutex_unlock(&lock);
}

bool mods_changed(void) {
    pthread_mutex_lock(&lock);
    bool changed = false;
    if (nstart >= 0) {
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (!list[i].m.on || list[i].m.problem != MOD_OK) continue;
            changed |= k >= nstart || strcmp(start[k].m.name, list[i].m.name);
            k++;
        }
        changed |= k != nstart;
    }
    pthread_mutex_unlock(&lock);
    return changed;
}

int mods_roots(int image, const char **roots, int *mod, int max) {
    pthread_mutex_lock(&lock);
    if (nstart < 0) {
        ensure_locked();
        start = calloc(n ? n : 1, sizeof *start);
        nstart = 0;
        for (int i = 0; i < n; i++) {
            const Entry *x = &list[i];
            if (x->m.problem != MOD_OK) LOG("[mods] %s: not used (%s)\n", x->m.name,
                                            x->m.problem == MOD_OTHER_GAME ? "made for another game"
                                            : x->m.problem == MOD_CODE_ONLY ? "only code patches, which are not loaded"
                                            : "no RomFS files");
            else if (!x->m.on) LOG("[mods] %s: off\n", x->m.name);
            else {
                LOG("[mods] %s: on (%s%s%s)\n", x->m.name, x->m.layout, x->m.aoc ? ", add-on content too" : "",
                    x->m.code ? "; its code patches are not loaded" : "");
                start[nstart++] = *x;
            }
        }
    }
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
