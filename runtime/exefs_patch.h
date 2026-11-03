// exefs_patch.h: ExeFS patch files, as Atmosphere and emulators take them (runtime/mods.c mods_exefs_apply): IPS and
// IPS32 (<build id>.ips) and IPSwitch's .pchtxt (its build id inside: @nsobid-<id>). Each record goes to fn with its
// offset in the module's image (the IPS offsets count the NSO's 0x100-byte header, as Atmosphere's loader applies them;
// a .pchtxt's are shifted by its offset_shift flag first). Pure functions: tools/test_mods.c checks them.
#pragma once
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef void (*ExefsPatchFn)(void *ctx, int64_t off, const uint8_t *bytes, uint32_t n);
#define EXEFS_NSO_HEADER 0x100

// An .ips / IPS32 file: its records to fn; returns how many, -1 not one
static inline int exefs_ips(const uint8_t *d, size_t n, ExefsPatchFn fn, void *ctx) {
    bool ips32 = n >= 5 && !memcmp(d, "IPS32", 5);
    if (!ips32 && (n < 5 || memcmp(d, "PATCH", 5))) return -1;
    size_t at = 5, ow = ips32 ? 4 : 3;
    int records = 0;
    while (at + ow <= n) {
        if (!ips32 && !memcmp(d + at, "EOF", 3)) return records;
        if (ips32 && at + 4 <= n && !memcmp(d + at, "EEOF", 4)) return records;
        uint32_t off = 0;
        for (size_t i = 0; i < ow; i++) off = off << 8 | d[at + i];
        at += ow;
        if (at + 2 > n) return -1;
        uint32_t size = (uint32_t)d[at] << 8 | d[at + 1];
        at += 2;
        if (size) {
            if (at + size > n) return -1;
            fn(ctx, (int64_t)off - EXEFS_NSO_HEADER, d + at, size);
            at += size;
        } else {  // RLE: a 16-bit count and the byte
            if (at + 3 > n) return -1;
            uint32_t count = (uint32_t)d[at] << 8 | d[at + 1];
            uint8_t *run = malloc(count ? count : 1);
            memset(run, d[at + 2], count);
            fn(ctx, (int64_t)off - EXEFS_NSO_HEADER, run, count);
            free(run);
            at += 3;
        }
        records++;
    }
    return -1;  // (no end marker)
}

// A .pchtxt's build id (@nsobid-<hex>) into bid (lower case); false: none
static inline bool exefs_pchtxt_bid(const char *t, size_t n, char *bid, size_t cap) {
    for (size_t at = 0; at + 8 < n; at++) {
        if ((at && t[at - 1] != '\n') || strncasecmp(t + at, "@nsobid-", 8)) continue;
        size_t k = 0;
        for (const char *p = t + at + 8; p < t + n && isxdigit((unsigned char)*p) && k + 1 < cap; p++) bid[k++] = (char)tolower(*p);
        bid[k] = 0;
        return k >= 16;
    }
    return false;
}

// A .pchtxt: the records of its enabled blocks to fn ("@enabled" / "@disabled" start a block, "@stop" ends the file;
// lines before the first are enabled). A value is hex bytes or a "quoted string". Returns how many.
static inline int exefs_pchtxt(const char *t, size_t n, ExefsPatchFn fn, void *ctx) {
    int64_t shift = 0;
    bool on = true;
    int records = 0;
    for (size_t at = 0; at < n;) {
        size_t end = at;
        while (end < n && t[end] != '\n') end++;
        char line[1024];
        size_t len = end - at < sizeof line - 1 ? end - at : sizeof line - 1;
        memcpy(line, t + at, len);
        line[len] = 0;
        at = end + 1;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!strncasecmp(s, "@stop", 5)) break;
        if (!strncasecmp(s, "@enabled", 8)) { on = true; continue; }
        if (!strncasecmp(s, "@disabled", 9)) { on = false; continue; }
        if (!strncasecmp(s, "@flag offset_shift", 18)) { shift = strtoll(s + 18, NULL, 0); continue; }
        if (*s == '@' || *s == '/' || !*s || !on || !isxdigit((unsigned char)*s)) continue;
        char *e;
        unsigned long long off = strtoull(s, &e, 16);
        if (e == s || (*e != ' ' && *e != '\t')) continue;
        while (*e == ' ' || *e == '\t') e++;
        uint8_t bytes[512];
        uint32_t nb = 0;
        if (*e == '"') {
            for (e++; *e && *e != '"' && nb < sizeof bytes; e++) bytes[nb++] = (uint8_t)*e;
        } else {
            for (; isxdigit((unsigned char)e[0]) && isxdigit((unsigned char)e[1]) && nb < sizeof bytes; e += 2) {
                char hx[3] = {e[0], e[1], 0};
                bytes[nb++] = (uint8_t)strtoul(hx, NULL, 16);
            }
        }
        if (!nb) continue;
        fn(ctx, (int64_t)off + shift - EXEFS_NSO_HEADER, bytes, nb);
        records++;
    }
    return records;
}

// Does a patch file's name (<build id>.ips, often cut short of the id's trailing zeros) or its @nsobid name this build
// id (64 hex digits, lower case)? A prefix of at least 16 digits.
static inline bool exefs_bid_match(const char *named, const char *build_id) {
    size_t k = 0;
    while (named[k] && named[k] != '.' && k < 64) {
        if (tolower((unsigned char)named[k]) != build_id[k]) return false;
        k++;
    }
    return k >= 16 && (!named[k] || named[k] == '.');
}
