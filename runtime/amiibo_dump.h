// amiibo_dump.h: an amiibo dump the player made of their own figure (an NTAG215 image: a 540-byte .bin from a phone or
// emulator tool, or a Flipper Zero .nfc text file) into what nfp:user hands the game (runtime/services/nfp.c): the tag's
// UID for GetTagInfo and the figure's identification block for GetModelInfo. Only the plaintext parts are read: the UID
// (pages 0-1) and the identification block (page 0x15, bytes 0x54-0x5B); the encrypted figure data is never touched,
// so no keys are needed. Header-only: tools/test_amiibo.c checks it.
// After weeping-of-the-empire v0.1.8.2 engine/runtime/amiibo_dump.h (Copyright (c) 2026 daniel-madrid-07, MIT License).
// ModelInfo differs from theirs: switchbrew's NFC services page (read 2026-11-03) gives Character ID as 3 bytes at 0
// (the tag's own order: character, variant), Series ID at 3, the Numbering ID byte-swapped at 4 and the NFP type at 6;
// they put the character byte-swapped and type / series the other way round.
#pragma once
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t uid[7];  // NTAG215 serial: bytes 0-2 and 4-7 (3 and 8 are its check bytes)
    uint8_t id[8];   // character (2, big-endian), variant, figure type, model number (2, big-endian), series, 0x02
} AmiiboTag;

// Why a file is no usable amiibo
enum { AMIIBO_OK = 0, AMIIBO_SIZE, AMIIBO_NO_CC, AMIIBO_FORMAT, AMIIBO_UNREADABLE };

// The tag image from a file's contents (n bytes) into img (cap, 1024 is plenty): binary as it is, or a Flipper .nfc
// ("Page N: AA BB CC DD" lines). Returns its size.
static inline size_t amiibo_image(const uint8_t *file, size_t n, uint8_t *img, size_t cap) {
    const char *t = (const char *)file;
    bool flipper = n >= 9 && !memcmp(t, "Filetype:", 9);
    for (size_t i = 0; !flipper && i + 8 <= n; i++) flipper = !memcmp(t + i, "\nPage 0:", 8);
    if (!flipper) {
        size_t k = n < cap ? n : cap;
        memcpy(img, file, k);
        return n;  // (the real size: amiibo_parse checks it)
    }
    size_t size = 0;
    for (size_t at = 0; at + 5 < n; at++) {
        if ((at && t[at - 1] != '\n') || memcmp(t + at, "Page ", 5)) continue;
        size_t p = at + 5;
        long page = 0;
        int digits = 0;
        while (p < n && isdigit((unsigned char)t[p]) && digits < 4) page = page * 10 + (t[p++] - '0'), digits++;
        if (!digits || p >= n || t[p] != ':' || page > 255 || (size_t)(page + 1) * 4 > cap) continue;
        p++;
        for (int i = 0; i < 4; i++) {
            while (p < n && (t[p] == ' ' || t[p] == '\t')) p++;
            if (p + 1 >= n || !isxdigit((unsigned char)t[p]) || !isxdigit((unsigned char)t[p + 1])) break;
            char hex[3] = {t[p], t[p + 1], 0};
            img[page * 4 + i] = (uint8_t)strtoul(hex, NULL, 16);
            p += 2;
        }
        if ((size_t)(page + 1) * 4 > size) size = (size_t)(page + 1) * 4;
    }
    return size;
}

// AMIIBO_OK: *tag filled
static inline int amiibo_parse(const uint8_t *img, size_t n, AmiiboTag *tag) {
    if (n < 0x5C || n > 1024) return AMIIBO_SIZE;  // (an NTAG215 image is 540 bytes; 532 / 572 too)
    static const uint8_t cc[4] = {0xF1, 0x10, 0xFF, 0xEE};  // the capability container every amiibo carries (page 3)
    if (memcmp(img + 0x0C, cc, 4)) return AMIIBO_NO_CC;
    if (img[0x5B] != 0x02) return AMIIBO_FORMAT;  // (the identification block's format byte)
    memcpy(tag->uid, img, 3);
    memcpy(tag->uid + 3, img + 4, 4);
    memcpy(tag->id, img + 0x54, 8);
    return AMIIBO_OK;
}

// TagInfo (0x58): UID (10, here 7 used), its length, protocol (NFC-A) and tag type (Type 2)
static inline void amiibo_tag_info(const AmiiboTag *t, uint8_t out[0x58]) {
    memset(out, 0, 0x58);
    memcpy(out, t->uid, 7);
    out[0x0A] = 7;
    const uint32_t protocol = 1, type = 2;
    memcpy(out + 0x20, &protocol, 4);
    memcpy(out + 0x24, &type, 4);
}

// ModelInfo (0x40): character id and variant as on the tag, series, model number in the console's byte order, type
static inline void amiibo_model_info(const AmiiboTag *t, uint8_t out[0x40]) {
    memset(out, 0, 0x40);
    memcpy(out, t->id, 3);
    out[3] = t->id[6];
    const uint16_t model = (uint16_t)(t->id[4] << 8 | t->id[5]);
    memcpy(out + 4, &model, 2);
    out[6] = t->id[3];
}
