// rstb_totk.h: TotK's resource size table (System/Resource/ResourceSizeTable.Product.100.rsizetable.zs, unpacked), merged
// with the tables mods bring and grown for their files (fs.c rstb_totk_patch). The table: "RESTBL", u32 version 1, u32
// name field length (160), u32 crc entry count, u32 name entry count, then the crc entries {u32 crc32 of the path, u32
// size} sorted by crc, then the name entries {char path[name field length], u32 size} for paths whose crc collides (all
// little-endian). Header-only, no game data: tools/test_mods.c checks it on tables it makes.
// After weeping-of-the-empire v0.1.8.2 engine/runtime/rstb_merge.h (Copyright (c) 2026 daniel-madrid-07, MIT License);
// its entry sizes for mod files (restbl_estimate, restbl_grow, the factors) are from totk-pc-recomp runtime/restbl.c
// (Copyright (c) 2026 Valiantsin Dzerakh, MIT License), measured there.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct { uint32_t crc, size; } RestblCrc;
typedef struct { char *name; uint32_t size; } RestblName;
typedef struct {
    uint32_t name_len;
    RestblCrc *crc;
    size_t ncrc, cap;
    bool unsorted;  // (entries added since the last sort)
    RestblName *names;
    size_t nnames;
} Restbl;

static inline void restbl_free(Restbl *t) {
    for (size_t i = 0; i < t->nnames; i++) free(t->names[i].name);
    free(t->crc), free(t->names);
    memset(t, 0, sizeof *t);
}

static inline uint32_t restbl_rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static inline bool restbl_parse(const uint8_t *b, size_t n, Restbl *t) {
    memset(t, 0, sizeof *t);
    if (n < 22 || memcmp(b, "RESTBL", 6)) return false;
    uint32_t ver = restbl_rd32(b + 6), nl = restbl_rd32(b + 10), nc = restbl_rd32(b + 14), nn = restbl_rd32(b + 18);
    if (ver != 1 || !nl || nl > 4096 || 22 + (uint64_t)nc * 8 + (uint64_t)nn * (nl + 4) > n) return false;
    t->name_len = nl;
    t->crc = malloc(sizeof *t->crc * (nc ? nc : 1));
    t->ncrc = t->cap = nc;
    for (uint32_t i = 0; i < nc; i++) t->crc[i] = (RestblCrc){restbl_rd32(b + 22 + i * 8), restbl_rd32(b + 26 + i * 8)};
    t->unsorted = true;  // (sorted on the console; checked, not trusted)
    t->names = calloc(nn ? nn : 1, sizeof *t->names);
    const uint8_t *q = b + 22 + (uint64_t)nc * 8;
    for (uint32_t i = 0; i < nn; i++, q += nl + 4) {
        size_t len = strnlen((const char *)q, nl);
        t->names[i].name = malloc(len + 1);
        memcpy(t->names[i].name, q, len);
        t->names[i].name[len] = 0;
        t->names[i].size = restbl_rd32(q + nl);
    }
    t->nnames = nn;
    return true;
}

static inline int restbl_crc_cmp(const void *a, const void *b) {
    uint32_t x = ((const RestblCrc *)a)->crc, y = ((const RestblCrc *)b)->crc;
    return x < y ? -1 : x > y;
}

static inline RestblCrc *restbl_crc_find(Restbl *t, uint32_t crc) {
    if (t->unsorted) qsort(t->crc, t->ncrc, sizeof *t->crc, restbl_crc_cmp), t->unsorted = false;
    RestblCrc k = {crc, 0};
    return bsearch(&k, t->crc, t->ncrc, sizeof *t->crc, restbl_crc_cmp);
}

static inline uint32_t restbl_crc32(const char *k) { return (uint32_t)crc32(0, (const Bytef *)k, (uInt)strlen(k)); }

// The entry's size field of key k (the name entries hold the keys whose crc collides); NULL none
static inline uint32_t *restbl_get(Restbl *t, const char *k) {
    for (size_t i = 0; i < t->nnames; i++)
        if (!strcmp(t->names[i].name, k)) return &t->names[i].size;
    RestblCrc *c = restbl_crc_find(t, restbl_crc32(k));
    return c ? &c->size : NULL;
}

static inline void restbl_add_crc(Restbl *t, uint32_t crc, uint32_t size) {
    if (t->ncrc == t->cap) t->cap = t->cap ? t->cap * 2 : 64, t->crc = realloc(t->crc, sizeof *t->crc * t->cap);
    t->crc[t->ncrc++] = (RestblCrc){crc, size};
    t->unsorted = true;
}

// Raises key k's entry to `want`, adding it when missing; true when it changed
static inline bool restbl_raise(Restbl *t, const char *k, uint32_t want) {
    uint32_t *v = restbl_get(t, k);
    if (!v) { restbl_add_crc(t, restbl_crc32(k), want); return true; }
    if (*v >= want) return false;
    *v = want;
    return true;
}

// into + from, each entry at the larger size; false (into unchanged) when the name field lengths differ
static inline bool restbl_merge(Restbl *into, Restbl *from, int *raised, int *added) {
    if (from->name_len != into->name_len) return false;
    for (size_t i = 0; i < from->ncrc; i++) {
        RestblCrc *c = restbl_crc_find(into, from->crc[i].crc);
        if (!c) restbl_add_crc(into, from->crc[i].crc, from->crc[i].size), (*added)++;
        else if (from->crc[i].size > c->size) c->size = from->crc[i].size, (*raised)++;
    }
    for (size_t i = 0; i < from->nnames; i++) {
        if (strlen(from->names[i].name) >= into->name_len) continue;  // (would not fit the field with its terminator)
        size_t k = 0;
        while (k < into->nnames && strcmp(into->names[k].name, from->names[i].name)) k++;
        if (k == into->nnames) {
            into->names = realloc(into->names, sizeof *into->names * (into->nnames + 1));
            into->names[into->nnames++] = (RestblName){strdup(from->names[i].name), from->names[i].size};
            (*added)++;
        } else if (from->names[i].size > into->names[k].size) {
            into->names[k].size = from->names[i].size, (*raised)++;
        }
    }
    return true;
}

static inline uint8_t *restbl_serialize(Restbl *t, size_t *out_n) {
    if (t->unsorted) qsort(t->crc, t->ncrc, sizeof *t->crc, restbl_crc_cmp), t->unsorted = false;
    size_t n = 22 + t->ncrc * 8 + t->nnames * (t->name_len + 4);
    uint8_t *o = calloc(1, n), *q = o + 22;
    uint32_t ver = 1, nc = (uint32_t)t->ncrc, nn = (uint32_t)t->nnames;
    memcpy(o, "RESTBL", 6), memcpy(o + 6, &ver, 4), memcpy(o + 10, &t->name_len, 4), memcpy(o + 14, &nc, 4),
        memcpy(o + 18, &nn, 4);
    for (size_t i = 0; i < t->ncrc; i++, q += 8) memcpy(q, &t->crc[i].crc, 4), memcpy(q + 4, &t->crc[i].size, 4);
    for (size_t i = 0; i < t->nnames; i++, q += t->name_len + 4) {
        memcpy(q, t->names[i].name, strnlen(t->names[i].name, t->name_len - 1));
        memcpy(q + t->name_len, &t->names[i].size, 4);
    }
    *out_n = n;
    return o;
}

// A RomFS path's key: no .zs / .mc ("Pack/Actor/X.pack" for "Pack/Actor/X.pack.zs")
static inline void restbl_key(const char *rel, char *out, size_t cap) {
    snprintf(out, cap, "%s", rel[0] == '/' ? rel + 1 : rel);
    size_t n = strlen(out);
    if (n >= 3 && (!strcmp(out + n - 3, ".zs") || !strcmp(out + n - 3, ".mc"))) out[n - 3] = 0;
}

// The factor a type's entry needs over its unpacked size (1000 = 1.0); 0 for a type the table never lists
static inline unsigned restbl_factor(const char *k) {
    static const struct { const char *ext; unsigned f; } types[] = {
        {"bfres", 1500}, {"bkres", 2050}, {"bgyml", 3850}, {"asb", 1400}, {"bstar", 1200}, {"bphsc", 1100},
        {"byml", 1000}, {"txtg", 1000}, {"pack", 1000}, {"bntx", 1000}, {"bphsh", 1000}, {"vsts", 1000},
        {"ainb", 1000}, {"bphnm", 1000}, {"bars", 1000}, {"chunk", 1000}, {"bfevfl", 1000}, {"quad", 1000},
        {"baev", 1000}, {"bcul", 1000}, {"crbin", 1000}, {"wbr", 1000}, {"bhtmp", 1000}, {"dpi", 1000},
        {"cai", 1000}, {"png", 1000}, {"cutinfo", 1000}, {"blwp", 1000}, {"sarc", 1000}, {"tscb", 1000},
        {"bfsha", 1000}, {"jpg", 1000}, {"beco", 1000}, {"bfarc", 1000}, {"blarc", 1000}, {"belnk", 1000},
        {"genvb", 1000}, {"bnsh", 1000}, {"bslnk", 1000}, {"baatarc", 1000}, {"bagst", 1000},
        {"bwav", 0}, {"webm", 0},
    };
    const char *slash = strrchr(k, '/'), *dot = strrchr(k, '.');
    if (!dot || (slash && dot < slash)) return 1500;
    for (size_t i = 0; i < sizeof types / sizeof *types; i++)
        if (!strcmp(dot + 1, types[i].ext)) return types[i].f;
    return 1500;
}

static inline uint32_t restbl_clamp(uint64_t v) { return v > 0xFFFFFFF0u ? 0xFFFFFFF0u : (uint32_t)v; }

// An added file's entry from its unpacked size (0: a type the table never lists)
static inline uint32_t restbl_estimate(const char *k, uint64_t size) {
    unsigned f = restbl_factor(k);
    if (!f) return 0;
    uint64_t e = (size * f + 999) / 1000 + 8192;
    return restbl_clamp((e + 4095) & ~(uint64_t)4095);
}

// A replaced file's entry: the game's, grown by what the file grew (scaled: a .mc mesh codec file, in proportion)
static inline uint32_t restbl_grow(const char *k, uint32_t entry, uint64_t was, uint64_t now, bool scaled) {
    if (now <= was) return entry;
    if (scaled) return restbl_clamp(was ? ((uint64_t)entry * now + was - 1) / was : restbl_estimate(k, now * 8));
    unsigned f = restbl_factor(k);
    if (!f) return entry;
    uint64_t e = entry + ((now - was) * f + 999) / 1000;
    return restbl_clamp((e + 31) & ~(uint64_t)31);
}
