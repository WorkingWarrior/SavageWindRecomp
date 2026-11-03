// mods_scan.h: where a mod folder keeps its RomFS (runtime/mods.c). Pure functions on host folders, no game data, no
// settings; tools/test_mods.c checks them on folders it makes.
// The layouts are the ones emulators and the Switch take: <mod>/romfs, <mod>/atmosphere/contents/<title>/romfs,
// <mod>/contents/<title>/romfs, <mod>/<title>/romfs (names in any case); else the folder itself when it is a RomFS tree
// (one of the game's top folders, Actor/ or Pack/...); else the one folder it holds when that is all it holds (a zip
// unpacked into a folder of its own name). The add-on content's RomFS is the same with its own title id (BCML's
// 01007EF00011F001/romfs), or <romfs>/aoc/0010 (a mod laid out as on the Wii U, as the overlay folder is).
// Layout list after weeping-of-the-empire v0.1.8.2 engine/runtime/mods_scan.h (Copyright (c) 2026 daniel-madrid-07, MIT
// License), itself after totk-pc-recomp runtime/mods.c (Copyright (c) 2026 Valiantsin Dzerakh, MIT License).
#pragma once
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct {
    char romfs[1100];        // its RomFS root ("" none)
    char aoc[1100];          // its add-on content RomFS root ("" none)
    char layout[200];        // where the RomFS was found ("romfs", "atmosphere/contents/<title>/romfs", "folder", ...)
    char other_title[17];    // contents/<id>/ of another game ("" none)
    bool code;               // exefs / cheats folders (code patches: not loaded by Savage Wind)
} ModFound;

static inline bool ms_is_dir(const char *p) {
    struct stat sb;
    return !stat(p, &sb) && S_ISDIR(sb.st_mode);
}

// The child of `dir` named `name` in any case, into out; false: none
static inline bool ms_child_ci(const char *dir, const char *name, char *out, size_t cap) {
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)))
        if (!strcasecmp(e->d_name, name)) snprintf(out, cap, "%s/%s", dir, e->d_name), found = true;
    closedir(d);
    return found;
}

// Does the folder hold a file anywhere below it?
static inline bool ms_has_file(const char *dir, int depth) {
    DIR *d = depth < 24 ? opendir(dir) : NULL;
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    char p[1400];
    while (!found && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat sb;
        if (stat(p, &sb)) continue;
        found = S_ISREG(sb.st_mode) || (S_ISDIR(sb.st_mode) && ms_has_file(p, depth + 1));
    }
    closedir(d);
    return found;
}

// The top folders of the game's RomFS a bare tree is recognised by (both games')
static inline bool ms_romfs_top(const char *n) {
    static const char *const tops[] = {"Actor", "AI", "AnimationEvent", "AS", "Bake", "Banc", "Cave", "Effect", "Event",
                                       "Font", "Game", "GameParameter", "Layout", "LevelSensor", "Lib", "Logic", "Mals",
                                       "Map", "Model", "Movie", "NavMesh", "Pack", "Phive", "Physics", "Preset", "RSDB",
                                       "Sequence", "Shader", "Sound", "System", "Terrain", "TexToGo", "UI", "Voice"};
    for (size_t i = 0; i < sizeof tops / sizeof *tops; i++)
        if (!strcasecmp(n, tops[i])) return true;
    return false;
}

static inline bool ms_title_id(const char *n) {
    if (strlen(n) != 16) return false;
    for (int i = 0; i < 16; i++)
        if (!isxdigit((unsigned char)n[i])) return false;
    return true;
}

// The `part` folder (romfs, exefs, cheats) of title `title` in a mod, in the places emulators and Atmosphere keep it,
// into out (layout: the place's name); false: none. A contents/<id>/ of a title that is neither `title` nor `also`
// (the game's other id: its add-on content's, or the game's own) goes in other.
static inline bool ms_find_part(const char *mod, const char *part, const char *title, const char *also, char *out,
                                size_t cap, char *layout, size_t lcap, char *other) {
    static const char *const pres[] = {"atmosphere/contents", "contents", ""};
    for (int k = 0; k < 3; k++) {
        char base[1100], next[1100];
        snprintf(base, sizeof base, "%s", mod);
        bool ok = true;
        for (const char *s = pres[k]; ok && *s;) {
            const char *slash = strchr(s, '/');
            char name[64];
            snprintf(name, sizeof name, "%.*s", slash ? (int)(slash - s) : (int)strlen(s), s);
            ok = ms_child_ci(base, name, next, sizeof next);
            if (ok) memcpy(base, next, sizeof base);
            s = slash ? slash + 1 : "";
        }
        if (!ok || !ms_is_dir(base)) continue;
        char t[1100];
        if (ms_child_ci(base, title, t, sizeof t)) {
            if (ms_child_ci(t, part, out, cap) && ms_is_dir(out)) {
                snprintf(layout, lcap, "%s%s%s/%s", pres[k], *pres[k] ? "/" : "", title, part);
                return true;
            }
            continue;
        }
        if (!*pres[k] || other[0]) continue;
        DIR *d = opendir(base);  // contents/<another 16-digit id>/: a mod for another game
        struct dirent *e;
        while (d && (e = readdir(d)))
            if (ms_title_id(e->d_name) && strcasecmp(e->d_name, title) && (!also || strcasecmp(e->d_name, also))) {
                snprintf(other, 17, "%s", e->d_name);
                break;
            }
        if (d) closedir(d);
    }
    return false;
}

// Files beside a bare RomFS tree that don't make it something else: a readme, a picture, a manifest
static inline bool ms_side_file(const char *n) {
    static const char *const ext[] = {".txt", ".md", ".png", ".jpg", ".jpeg", ".json", ".ini", ".yml", ".url"};
    const char *dot = strrchr(n, '.');
    for (size_t i = 0; dot && i < sizeof ext / sizeof *ext; i++)
        if (!strcasecmp(dot, ext[i])) return true;
    return false;
}

// What mod folder `mod` holds for the game `title` (its add-on content: `aoc_title`, NULL none) and where. One level
// of a lone wrapper folder is looked through.
static inline void mods_scan_one(const char *mod, const char *title, const char *aoc_title, ModFound *f) {
    memset(f, 0, sizeof *f);
    char root[1100], p[1100], lay[200];
    snprintf(root, sizeof root, "%s", mod);
    for (int depth = 0; depth < 2; depth++) {
        if (ms_child_ci(root, "romfs", f->romfs, sizeof f->romfs) && ms_is_dir(f->romfs))
            snprintf(f->layout, sizeof f->layout, "romfs");
        else if (!ms_find_part(root, "romfs", title, aoc_title, f->romfs, sizeof f->romfs, f->layout, sizeof f->layout,
                               f->other_title))
            f->romfs[0] = 0;
        if (aoc_title && !ms_find_part(root, "romfs", aoc_title, title, f->aoc, sizeof f->aoc, lay, sizeof lay, f->other_title))
            f->aoc[0] = 0;
        f->code |= (ms_child_ci(root, "exefs", p, sizeof p) && ms_is_dir(p)) ||
                   (ms_child_ci(root, "cheats", p, sizeof p) && ms_is_dir(p)) ||
                   ms_find_part(root, "exefs", title, aoc_title, p, sizeof p, lay, sizeof lay, f->other_title) ||
                   ms_find_part(root, "cheats", title, aoc_title, p, sizeof p, lay, sizeof lay, f->other_title);
        if (f->romfs[0] || f->aoc[0]) break;
        // the folder itself is a RomFS tree?
        DIR *d = opendir(root);
        if (!d) break;
        struct dirent *e;
        int dirs = 0;
        bool files = false, top = false;
        char only[1100] = "";
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "%s/%s", root, e->d_name);
            if (ms_is_dir(p)) {
                dirs++;
                top |= ms_romfs_top(e->d_name);
                memcpy(only, p, sizeof only);
            } else if (!ms_side_file(e->d_name)) {
                files = true;
            }
        }
        closedir(d);
        if (top) {
            snprintf(f->romfs, sizeof f->romfs, "%s", root);
            snprintf(f->layout, sizeof f->layout, "%s", depth ? "folder in a folder" : "folder");
            break;
        }
        if (dirs != 1 || files || f->code) break;
        memcpy(root, only, sizeof root);  // a zip unpacked into a folder of its own: look inside once
    }
    // a mod laid out as on the Wii U keeps the add-on content's files in <romfs>/aoc/0010
    if (aoc_title && !f->aoc[0] && f->romfs[0] && ms_child_ci(f->romfs, "aoc", p, sizeof p) &&
        ms_child_ci(p, "0010", f->aoc, sizeof f->aoc) && !ms_is_dir(f->aoc))
        f->aoc[0] = 0;
    if (f->romfs[0] && !ms_has_file(f->romfs, 0)) f->romfs[0] = 0;
    if (f->aoc[0] && !ms_has_file(f->aoc, 0)) f->aoc[0] = 0;
    if (!f->romfs[0] && !f->aoc[0]) f->layout[0] = 0;
}

// The names in list order: the saved order's names first (those still there), then the new ones by name. `saved` and
// `names` are arrays of n / nsaved strings; out gets indices into names. Returns how many.
static inline int mods_scan_order(const char *const *names, int n, const char *const *saved, int nsaved, int *out) {
    int k = 0;
    for (int s = 0; s < nsaved; s++)
        for (int i = 0; i < n; i++) {
            if (strcmp(names[i], saved[s])) continue;
            bool dup = false;
            for (int j = 0; j < k; j++) dup |= out[j] == i;
            if (!dup) out[k++] = i;
            break;
        }
    int first_new = k;
    for (int i = 0; i < n; i++) {
        bool known = false;
        for (int j = 0; j < first_new; j++) known |= out[j] == i;
        if (known) continue;
        int at = k++;  // (insertion by name among the new ones)
        while (at > first_new && strcmp(names[out[at - 1]], names[i]) > 0) out[at] = out[at - 1], at--;
        out[at] = i;
    }
    return k;
}
