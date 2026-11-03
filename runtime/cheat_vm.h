// cheat_vm.h: Atmosphere cheats (<mod>/cheats/<build id>.txt, the format of Atmosphere's docs/features/cheats.md): the
// file's cheats (cheat_parse) and the virtual machine that runs their codes (cheat_vm_run), written here from that
// document. Memory goes through the caller's functions (a read or write that fails is skipped, as on the console);
// the regions are the caller's bases. Pure functions: tools/test_mods.c checks them.
#pragma once
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHEAT_OPS_MAX 0x100  // code words in one cheat
#define CHEAT_REGS 16

typedef struct {
    char name[64];
    bool master;  // {name}: the file's master code, run before the others
    bool broken;  // a line of its codes has one that isn't 8 hex digits: left out (its codes would decode wrong)
    uint32_t op[CHEAT_OPS_MAX];
    uint32_t n;
} Cheat;

// A cheat file's cheats ("[name]" or "{name}" and then its codes, 8 hex digits each) to out; returns how many (at most
// max). Line by line, as the files people share are: a line whose first word is 8 hex digits holds codes, any other
// line (a note under a name, a name left open) is skipped. Codes before the first name are left out.
static inline int cheat_parse(const char *t, size_t n, Cheat *out, int max) {
    int k = -1;
    for (size_t at = 0; at < n;) {
        size_t end = at;
        while (end < n && t[end] != '\n') end++;
        size_t i = at;
        at = end + 1;
        while (i < end && isspace((unsigned char)t[i])) i++;
        if (i == end) continue;
        if (t[i] == '[' || t[i] == '{') {
            char close = t[i] == '[' ? ']' : '}';
            size_t e = i + 1;
            while (e < end && t[e] != close) e++;
            if (e == end) continue;  // (not a name)
            if (k + 1 >= max) return max;
            Cheat *ch = &out[++k];
            memset(ch, 0, sizeof *ch);
            size_t len = e - i - 1 < sizeof ch->name - 1 ? e - i - 1 : sizeof ch->name - 1;
            memcpy(ch->name, t + i + 1, len);
            ch->master = close == '}';
            continue;
        }
        bool first = true;
        while (i < end) {
            size_t e = i;
            while (e < end && !isspace((unsigned char)t[e])) e++;
            bool code = e - i == 8;
            for (size_t j = i; j < e && code; j++) code = isxdigit((unsigned char)t[j]);
            if (first && !code) break;  // (a note)
            first = false;
            if (k < 0) break;
            if (!code || out[k].n == CHEAT_OPS_MAX) { out[k].broken = true; break; }
            char hx[9];
            memcpy(hx, t + i, 8), hx[8] = 0;
            out[k].op[out[k].n++] = (uint32_t)strtoul(hx, NULL, 16);
            i = e;
            while (i < end && isspace((unsigned char)t[i])) i++;
        }
    }
    return k + 1;
}

typedef struct {
    uint64_t base[5];  // memory types 0-4: main module, heap, alias, ASLR region, 0 (non-relative)
    uint64_t keys;     // buttons held (the Npad bits: 1 A, 2 B ... 0x8000 Down, 0x10000.. the sticks' directions)
    bool (*rd)(uint64_t addr, void *buf, size_t n);
    bool (*wr)(uint64_t addr, const void *buf, size_t n);
    void (*log)(void *ctx, unsigned id, uint64_t v);  // FFF: debug log (NULL: none)
    void (*pause)(void *ctx, bool on);                // FF0 / FF1: pause / resume the game (NULL: they do nothing)
    void *ctx;
} CheatEnv;

typedef struct {
    uint64_t reg[CHEAT_REGS], saved[CHEAT_REGS], statics[256];  // (statics: kept from run to run)
    uint32_t loop_top[CHEAT_REGS];
    uint8_t *edge;  // C4 without auto-repeat: its keys matched at the last run (by code word)
    uint32_t nedge;
} CheatVm;

enum { CHEAT_DONE, CHEAT_BAD /* an instruction it can't decode: the run stops there */, CHEAT_LONG /* step budget */ };

#define CVM_NB(f, i) (((f) >> (28 - 4 * (i))) & 0xF)  // nibble i of a code word, from the left

static inline void cheat_vm_init(CheatVm *vm, uint32_t n) {
    memset(vm, 0, sizeof *vm);
    vm->edge = calloc(n ? n : 1, 1), vm->nedge = n;
}
static inline void cheat_vm_free(CheatVm *vm) { free(vm->edge), vm->edge = NULL; }

static inline bool cvm_width_ok(unsigned w) { return w == 1 || w == 2 || w == 4 || w == 8; }
static inline uint64_t cvm_mask(unsigned w) { return w >= 8 ? ~0ull : (1ull << (8 * w)) - 1; }

// The instruction at `at`: its length in code words, 0 when it isn't one or is cut short
static inline uint32_t cvm_len(const uint32_t *p, uint32_t n, uint32_t at) {
    uint32_t f = p[at], len;
    unsigned w = CVM_NB(f, 1);
    switch (f >> 28) {
    case 0x0: case 0x1: len = w == 8 ? 4 : 3; break;
    case 0x2: case 0x8: len = 1; break;
    case 0x3: len = w == 0 ? 2 : 1; break;
    case 0x4: case 0x6: len = 3; break;
    case 0x5: case 0x7: len = 2; break;
    case 0x9: len = CVM_NB(f, 5) ? (w == 8 ? 3 : 2) : 1; break;
    case 0xA: { unsigned o = CVM_NB(f, 5); len = o == 2 || o == 4 || o == 5 ? 2 : 1; break; }
    default:
        switch (f >> 24) {
        case 0xC0: { unsigned x = CVM_NB(f, 5); len = x == 0 || x == 2 ? 2 : x == 4 ? (CVM_NB(f, 2) == 8 ? 3 : 2) : 1; break; }
        case 0xC1: case 0xC2: case 0xC3: len = 1; break;
        case 0xC4: len = 3; break;
        case 0xFF:
            switch (f >> 20) {
            case 0xFF0: case 0xFF1: len = 1; break;
            case 0xFFF: { unsigned x = CVM_NB(f, 5); len = x == 0 || x == 2 ? 2 : 1; break; }
            default: return 0;
            }
            break;
        default: return 0;
        }
    }
    return at + len <= n ? len : 0;
}

static inline bool cvm_begins(uint32_t f) { return f >> 28 == 1 || f >> 28 == 8 || f >> 24 == 0xC0 || f >> 24 == 0xC4; }

// Past the block that starts before `at`: after its End, or (else_too) its Else
static inline uint32_t cvm_skip(const uint32_t *p, uint32_t n, uint32_t at, bool else_too) {
    for (unsigned depth = 0; at < n;) {
        uint32_t f = p[at], len = cvm_len(p, n, at);
        if (!len) return n;
        at += len;
        if (cvm_begins(f)) depth++;
        else if (f >> 28 == 2) {
            bool end = CVM_NB(f, 1) == 0;
            if (!depth) { if (end || else_too) return at; }
            else if (end) depth--;
        }
    }
    return n;
}

static inline bool cvm_cond(unsigned c, uint64_t a, uint64_t b, bool *ok) {
    switch (c) {
    case 1: return a > b;
    case 2: return a >= b;
    case 3: return a < b;
    case 4: return a <= b;
    case 5: return a == b;
    case 6: return a != b;
    }
    *ok = false;
    return false;
}

static inline uint64_t cvm_arith(unsigned op, unsigned w, uint64_t a, uint64_t b, bool *ok) {
    float fa, fb, fr;
    double da, db, dr;
    uint32_t a32 = (uint32_t)a, b32 = (uint32_t)b, r32;
    switch (op) {
    case 0: return a + b;
    case 1: return a - b;
    case 2: return a * b;
    case 3: return b >= 64 ? 0 : a << b;
    case 4: return b >= 64 ? 0 : a >> b;
    case 5: return a & b;
    case 6: return a | b;
    case 7: return ~a;
    case 8: return a ^ b;
    case 9: return a;
    case 10: case 11: case 12: case 13:
        if (w == 4) {
            memcpy(&fa, &a32, 4), memcpy(&fb, &b32, 4);
            fr = op == 10 ? fa + fb : op == 11 ? fa - fb : op == 12 ? fa * fb : fa / fb;
            memcpy(&r32, &fr, 4);
            return r32;
        }
        if (w == 8) {
            memcpy(&da, &a, 8), memcpy(&db, &b, 8);
            dr = op == 10 ? da + db : op == 11 ? da - db : op == 12 ? da * db : da / db;
            memcpy(&a, &dr, 8);
            return a;
        }
    }
    *ok = false;
    return 0;
}

// Runs the codes p[0..n) once (the registers start at 0, as each run on the console); CHEAT_*
static inline int cheat_vm_run(CheatVm *vm, const uint32_t *p, uint32_t n, const CheatEnv *e) {
    memset(vm->reg, 0, sizeof vm->reg), memset(vm->saved, 0, sizeof vm->saved), memset(vm->loop_top, 0, sizeof vm->loop_top);
    uint64_t *r = vm->reg;
    for (uint32_t at = 0, steps = 0; at < n; steps++) {
        if (steps >= (1u << 20)) return CHEAT_LONG;
        uint32_t len = cvm_len(p, n, at);
        if (!len) return CHEAT_BAD;
        uint32_t f = p[at], d1 = len > 1 ? p[at + 1] : 0, d2 = len > 2 ? p[at + 2] : 0, d3 = len > 3 ? p[at + 3] : 0;
        uint32_t next = at + len;
        unsigned w = CVM_NB(f, 1);
        uint64_t rel40 = (uint64_t)(f & 0xFF) << 32 | d1, v = 0;  // (0, 1, 5: AA AAAAAAAA)
        bool ok = true;
        switch (f >> 28) {
        case 0x0: {  // store a value: region + offset register + address
            unsigned m = CVM_NB(f, 2);
            if (!cvm_width_ok(w) || m > 4) return CHEAT_BAD;
            v = (w == 8 ? (uint64_t)d2 << 32 | d3 : d2) & cvm_mask(w);
            e->wr(e->base[m] + rel40 + r[CVM_NB(f, 3)], &v, w);
            break;
        }
        case 0x1: {  // if memory compares to a value
            unsigned m = CVM_NB(f, 2), x = CVM_NB(f, 4);
            if (!cvm_width_ok(w) || m > 4 || x > 1) return CHEAT_BAD;
            uint64_t want = w == 8 ? (uint64_t)d2 << 32 | d3 : d2, have = 0;
            e->rd(e->base[m] + rel40 + (x ? r[CVM_NB(f, 5)] : 0), &have, w);
            bool c = cvm_cond(CVM_NB(f, 3), have & cvm_mask(w), want, &ok);
            if (!ok) return CHEAT_BAD;
            if (!c) next = cvm_skip(p, n, next, true);
            break;
        }
        case 0x2:  // End; Else, reached from its true part
            if (w == 1) next = cvm_skip(p, n, next, false);
            else if (w) return CHEAT_BAD;
            break;
        case 0x3: {  // loop
            unsigned k = CVM_NB(f, 3);
            if (w == 0) r[k] = d1, vm->loop_top[k] = next;
            else if (w == 1) { if (--r[k]) next = vm->loop_top[k]; }
            else return CHEAT_BAD;
            break;
        }
        case 0x4: r[CVM_NB(f, 3)] = (uint64_t)d1 << 32 | d2; break;
        case 0x5: {  // load a register from memory
            unsigned m = CVM_NB(f, 2), k = CVM_NB(f, 3), type = CVM_NB(f, 4), s = CVM_NB(f, 5);
            if (!cvm_width_ok(w) || m > 4 || type > 3) return CHEAT_BAD;
            uint64_t a = type == 0 ? e->base[m] + rel40 : type == 1 ? r[k] + rel40 : type == 2 ? r[s] + rel40
                       : e->base[m] + r[s] + rel40;
            v = 0;
            e->rd(a, &v, w);
            r[k] = v & cvm_mask(w);
            break;
        }
        case 0x6: {  // store a value at a register's address
            unsigned k = CVM_NB(f, 3);
            if (!cvm_width_ok(w)) return CHEAT_BAD;
            v = ((uint64_t)d1 << 32 | d2) & cvm_mask(w);
            e->wr(r[k] + (CVM_NB(f, 5) ? r[CVM_NB(f, 6)] : 0), &v, w);
            if (CVM_NB(f, 4)) r[k] += w;
            break;
        }
        case 0x7: {  // (legacy) arithmetic with a value
            unsigned k = CVM_NB(f, 3), op = CVM_NB(f, 4);
            if (!cvm_width_ok(w) || op > 4) return CHEAT_BAD;
            r[k] = cvm_arith(op, w, r[k], d1, &ok) & cvm_mask(w);
            break;
        }
        case 0x8: {  // if these keys are held
            uint64_t keys = f & 0x0FFFFFFF;
            if ((e->keys & keys) != keys) next = cvm_skip(p, n, next, true);
            break;
        }
        case 0x9: {  // arithmetic
            unsigned op = CVM_NB(f, 2), k = CVM_NB(f, 3), s = CVM_NB(f, 4);
            if (!cvm_width_ok(w)) return CHEAT_BAD;
            uint64_t rhs = CVM_NB(f, 5) ? (w == 8 ? (uint64_t)d1 << 32 | d2 : d1) : r[CVM_NB(f, 6)];
            v = cvm_arith(op, w, r[s], rhs, &ok);
            if (!ok) return CHEAT_BAD;
            r[k] = v & cvm_mask(w);
            break;
        }
        case 0xA: {  // store a register
            unsigned s = CVM_NB(f, 2), k = CVM_NB(f, 3), o = CVM_NB(f, 5), x = CVM_NB(f, 6);
            uint64_t a = (uint64_t)(f & 0xF) << 32 | d1;
            if (!cvm_width_ok(w) || o > 5 || (o >= 3 && x > 4)) return CHEAT_BAD;
            uint64_t addr = o == 0 ? r[k] : o == 1 ? r[k] + r[x] : o == 2 ? r[k] + a : o == 3 ? e->base[x] + r[k]
                          : o == 4 ? e->base[x] + a : e->base[x] + a + r[k];
            v = r[s] & cvm_mask(w);
            e->wr(addr, &v, w);
            if (CVM_NB(f, 4)) r[k] += w;
            break;
        }
        default:
            switch (f >> 24) {
            case 0xC0: {  // if a register compares to memory, a value or a register
                unsigned t = CVM_NB(f, 2), s = CVM_NB(f, 4), x = CVM_NB(f, 5), a6 = CVM_NB(f, 6), a7 = CVM_NB(f, 7);
                uint64_t rel = (uint64_t)a7 << 32 | d1, rhs = 0;
                if (!cvm_width_ok(t) || x > 5 || (x <= 1 && a6 > 4)) return CHEAT_BAD;
                if (x <= 3) {
                    uint64_t addr = x == 0 ? e->base[a6] + rel : x == 1 ? e->base[a6] + r[a7] : x == 2 ? r[a6] + rel
                                  : r[a6] + r[a7];
                    e->rd(addr, &rhs, t);
                } else rhs = x == 4 ? (t == 8 ? (uint64_t)d1 << 32 | d2 : d1) : r[a6];
                bool c = cvm_cond(CVM_NB(f, 3), r[s] & cvm_mask(t), rhs & cvm_mask(t), &ok);
                if (!ok) return CHEAT_BAD;
                if (!c) next = cvm_skip(p, n, next, true);
                break;
            }
            case 0xC1: case 0xC2: {  // save or restore registers (C1: one, C2: by mask)
                bool one = f >> 24 == 0xC1;
                unsigned type = one ? CVM_NB(f, 6) : CVM_NB(f, 2);
                if (type > 3) return CHEAT_BAD;
                for (unsigned i = 0; i < CHEAT_REGS; i++) {
                    if (one ? i != CVM_NB(f, 3) : !(f >> i & 1)) continue;
                    unsigned src = one ? CVM_NB(f, 5) : i;
                    if (type == 0) r[i] = vm->saved[src];
                    else if (type == 1) vm->saved[i] = r[src];
                    else if (type == 2) vm->saved[i] = 0;
                    else r[i] = 0;
                }
                break;
            }
            case 0xC3: {  // static registers: 0x00-0x7F read, 0x80-0xFF written
                unsigned idx = f >> 4 & 0xFF, k = f & 0xF;
                if (idx < 0x80) r[k] = vm->statics[idx];
                else vm->statics[idx] = r[k];
                break;
            }
            case 0xC4: {  // if these keys are held (auto-repeat off: once a press)
                uint64_t keys = (uint64_t)d1 << 32 | d2;
                bool match = (e->keys & keys) == keys, c = match;
                if (!CVM_NB(f, 2) && at < vm->nedge) c = match && !vm->edge[at], vm->edge[at] = match;
                if (!c) next = cvm_skip(p, n, next, true);
                break;
            }
            default:
                if (f >> 20 == 0xFFF) {  // debug log
                    unsigned t = CVM_NB(f, 3), x = CVM_NB(f, 5), a6 = CVM_NB(f, 6), a7 = CVM_NB(f, 7);
                    uint64_t rel = (uint64_t)a7 << 32 | d1;
                    if (!cvm_width_ok(t) || x > 4 || (x <= 1 && a6 > 4)) return CHEAT_BAD;
                    v = 0;
                    if (x <= 3) {
                        uint64_t addr = x == 0 ? e->base[a6] + rel : x == 1 ? e->base[a6] + r[a7] : x == 2 ? r[a6] + rel
                                      : r[a6] + r[a7];
                        e->rd(addr, &v, t);
                    } else v = r[a6];
                    if (e->log) e->log(e->ctx, CVM_NB(f, 4), v & cvm_mask(t));
                } else if (f >> 20 == 0xFF0 || f >> 20 == 0xFF1) {
                    if (e->pause) e->pause(e->ctx, f >> 20 == 0xFF0);
                }
            }
        }
        at = next;
    }
    return CHEAT_DONE;
}
