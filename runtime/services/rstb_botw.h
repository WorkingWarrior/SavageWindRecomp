// rstb_botw.h: BOTW's ResourceSizeTable (System/Resource/ResourceSizeTable.product.srsizetable, unpacked), merged with
// the tables mods bring (fs.c rstb_patch). The Switch table, little-endian: "RSTB", u32 crc entry count, u32 name
// entry count, the crc entries {u32 crc32 of the path, u32 size} sorted by crc, then the name entries {char path[128],
// u32 size} for paths whose crc collides. Header-only, no game data: tools/test_mods.c checks it on tables it makes.
// After weeping-of-the-empire v0.1.8.2 engine/runtime/rstb_merge.h (Copyright (c) 2026 daniel-madrid-07, MIT License),
// which does this for TotK's table: each entry at the larger of the sizes.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline bool rstb_valid(const uint8_t *d, size_t n, uint32_t *ncrc, uint32_t *nname) {
    if (n < 12 || memcmp(d, "RSTB", 4)) return false;
    memcpy(ncrc, d + 4, 4), memcpy(nname, d + 8, 4);
    return 12 + (uint64_t)*ncrc * 8 + (uint64_t)*nname * 132 <= n;
}

// a + b, each entry at the larger size, into a new buffer (*out_n); NULL when either isn't a table. *raised: entries of
// a that b made bigger, *added: entries only b has.
static inline uint8_t *rstb_merge(const uint8_t *a, size_t an, const uint8_t *b, size_t bn, size_t *out_n, int *raised,
                                  int *added) {
    uint32_t ac, anm, bc, bnm;
    if (!rstb_valid(a, an, &ac, &anm) || !rstb_valid(b, bn, &bc, &bnm)) return NULL;
    uint8_t *o = malloc(12 + ((uint64_t)ac + bc) * 8 + ((uint64_t)anm + bnm) * 132);
    if (!o) return NULL;
    const uint8_t *pa = a + 12, *pb = b + 12;
    uint8_t *q = o + 12;
    uint32_t i = 0, j = 0, nc = 0, nn = 0;
    while (i < ac || j < bc) {
        uint32_t ka = 0, kb = 0, va, vb;
        if (i < ac) memcpy(&ka, pa + i * 8, 4);
        if (j < bc) memcpy(&kb, pb + j * 8, 4);
        if (j == bc || (i < ac && ka < kb)) { memcpy(q, pa + i * 8, 8), i++; }
        else if (i == ac || kb < ka) { memcpy(q, pb + j * 8, 8), j++, (*added)++; }
        else {
            memcpy(&va, pa + i * 8 + 4, 4), memcpy(&vb, pb + j * 8 + 4, 4);
            memcpy(q, &ka, 4);
            memcpy(q + 4, vb > va ? &vb : &va, 4);
            *raised += vb > va;
            i++, j++;
        }
        q += 8, nc++;
    }
    const uint8_t *na = a + 12 + ac * 8, *nb = b + 12 + bc * 8;
    for (uint32_t k = 0; k < anm; k++, nn++, q += 132) {
        memcpy(q, na + k * 132, 132);
        for (uint32_t m = 0; m < bnm; m++) {
            if (strncmp((const char *)q, (const char *)nb + m * 132, 128)) continue;
            uint32_t va, vb;
            memcpy(&va, q + 128, 4), memcpy(&vb, nb + m * 132 + 128, 4);
            if (vb > va) memcpy(q + 128, &vb, 4), (*raised)++;
            break;
        }
    }
    for (uint32_t m = 0; m < bnm; m++) {
        bool known = false;
        for (uint32_t k = 0; k < anm && !known; k++) known = !strncmp((const char *)na + k * 132, (const char *)nb + m * 132, 128);
        if (known) continue;
        memcpy(q, nb + m * 132, 132), q += 132, nn++, (*added)++;
    }
    memcpy(o, "RSTB", 4), memcpy(o + 4, &nc, 4), memcpy(o + 8, &nn, 4);
    *out_n = (size_t)(q - o);
    return o;
}
