// Function-entry profiler. SAVAGEWIND_FNPROF=<from>:<to> counts guest function entries (every generated
// function calls FN_ENTER) between those presented frames, then writes build/fnprof.txt (addr count, most
// called first). Diffing two runs shows which game functions a change brings in; counts also show hot code.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/uio.h>
#include <unistd.h>
#endif

#include "game_sites.h"
#include "hle.h"
#include "kernel.h"
#include "platform.h"

volatile int g_fnprof;

// Opt-in, once per method: use the already emitted FN_ENTER hook. No generated
// code or CPU header change. The BOTW 1.9.0 addresses match the same SDK exports.
static bool sdk_probe_on, sdk_probe_only;
static _Atomic uint8_t sdk_probe_done[2];

static void sdk_entry_probe(uint64_t pc, uint64_t x0) {
    static const uint64_t totk[2] = {0x7004e3d1d0ull, 0x7004e3d3d4ull};
    static const uint64_t botw[2] = {0x7002649df0ull, 0x7002649f70ull};
    static const char *names[2] = {"AllocateAddressRegion", "AllocateMemory"};
    if (game_is_botw() && strcmp(g_game_version, "1.9.0")) return;
    const uint64_t *addrs = game_is_botw() ? botw : totk;
    for (int i = 0; i < 2; i++) {
        if (pc != addrs[i] || atomic_exchange_explicit(&sdk_probe_done[i], 1, memory_order_relaxed)) continue;
        uint64_t field = 0;
        bool readable = false;
#ifdef __linux__
        // Read exactly LD64(x0+0x40), without introducing another SIGSEGV if x0
        // itself is invalid. A failed read is reported as unreadable, never 0.
        if (x0 <= UINT64_MAX - 0x48) {
            struct iovec dst = {&field, sizeof field};
            struct iovec src = {(void *)(uintptr_t)(x0 + 0x40), sizeof field};
            readable = process_vm_readv(getpid(), &dst, 1, &src, 1, 0) == (ssize_t)sizeof field;
        }
#endif
        KThread *t = kernel_current_thread();
        if (readable)
            LOG("[sdk-entry-probe] %s VammManager::%s f_%llx x0=%016llx LD64(x0+0x40)=%016llx guest_tid=%llu handle=%#x\n",
                game_profile_name(), names[i], (unsigned long long)pc, (unsigned long long)x0,
                (unsigned long long)field, (unsigned long long)(t ? t->id : 0), t ? t->handle : 0);
        else
            LOG("[sdk-entry-probe] %s VammManager::%s f_%llx x0=%016llx LD64(x0+0x40)=unreadable guest_tid=%llu handle=%#x\n",
                game_profile_name(), names[i], (unsigned long long)pc, (unsigned long long)x0,
                (unsigned long long)(t ? t->id : 0), t ? t->handle : 0);
        fflush(stderr);
    }
}

#define NSLOT (1 << 18)
static struct { _Atomic uint64_t addr; _Atomic uint64_t count; } slots[NSLOT];

// SAVAGEWIND_FNPROF_BT=<hex addr>[,...]: also print the host backtrace (guest functions are f_<addr>) the first time
// each listed function is entered inside the window - who calls it
static uint64_t bt_addrs[256];
static int bt_n = -1;
static _Atomic uint8_t bt_done[256];

uint64_t g_bt_first_x0;

static void bt_check(uint64_t a, uint64_t lr, uint64_t x0) {
    if (bt_n < 0) {
        bt_n = 0;
        const char *e = getenv("SAVAGEWIND_FNPROF_BT");
        for (const char *p = e; p && *p && bt_n < 256;) {
            bt_addrs[bt_n++] = strtoull(p, (char **)&p, 16);
            while (*p == ',' || *p == ' ') p++;
        }
    }
    for (int i = 0; i < bt_n; i++)
        if (bt_addrs[i] == a && !atomic_exchange(&bt_done[i], 1)) {
            if (i == 0) g_bt_first_x0 = x0;
            uint64_t vt = x0 >= 0x7000000000ull && x0 < 0x7800000000ull ? *(uint64_t *)(uintptr_t)x0 : 0;
            uint64_t v100 = vt >= 0x7000000000ull && vt < 0x7800000000ull ? *(uint64_t *)(uintptr_t)(vt + 0x100) : 0;
            uint64_t p8 = x0 >= 0x7000000000ull && x0 < 0x7800000000ull ? *(uint64_t *)(uintptr_t)(x0 + 8) : 0;
            fprintf(stderr, "[fnprof-bt] %llx entered from guest lr %llx x0 %llx (vtable %llx, +0x100 %llx, [x0+8] %llx):\n",
                    (unsigned long long)a, (unsigned long long)lr, (unsigned long long)x0, (unsigned long long)vt,
                    (unsigned long long)v100, (unsigned long long)p8);
            plat_print_backtrace();  // (execinfo is macOS / glibc only)
        }
}

// SAVAGEWIND_FNPROF_ARGS=<hex addr>[,...]: log x0-x5 and the caller on each entry of those functions (the first
// SAVAGEWIND_FNPROF_ARGS_MAX entries each, default 64) - what a function is called with, without a debugger - and
// what one register points to: SAVAGEWIND_FNPROF_DEREF=<reg>:<qwords> (default 1:4, x1: often a descriptor or out
// argument).
static uint64_t args_addrs[64];
static int args_n = -1, args_max = 64, deref_reg = 1, deref_n = 4;
static _Atomic int args_count[64];

static void args_check(uint64_t a, uint64_t lr) {
    if (args_n < 0) {
        args_n = 0;
        const char *e = getenv("SAVAGEWIND_FNPROF_ARGS");
        if (getenv("SAVAGEWIND_FNPROF_ARGS_MAX")) args_max = atoi(getenv("SAVAGEWIND_FNPROF_ARGS_MAX"));
        if (getenv("SAVAGEWIND_FNPROF_DEREF")) sscanf(getenv("SAVAGEWIND_FNPROF_DEREF"), "%d:%d", &deref_reg, &deref_n);
        if (deref_reg < 0 || deref_reg > 30) deref_reg = 1;
        if (deref_n < 0 || deref_n > 64) deref_n = 4;
        for (const char *p = e; p && *p && args_n < 64;) {
            args_addrs[args_n++] = strtoull(p, (char **)&p, 16);
            while (*p == ',' || *p == ' ') p++;
        }
    }
    for (int i = 0; i < args_n; i++)
        if (args_addrs[i] == a && atomic_fetch_add(&args_count[i], 1) < args_max) {
            Cpu *c = kernel_current_cpu();
            if (!c) return;
            char line[1600];
            int n = snprintf(line, sizeof line, "[fnprof-args] %llx from %llx: %llx %llx %llx %llx %llx %llx [x%d:",
                             (unsigned long long)a, (unsigned long long)lr, (unsigned long long)c->x[0],
                             (unsigned long long)c->x[1], (unsigned long long)c->x[2], (unsigned long long)c->x[3],
                             (unsigned long long)c->x[4], (unsigned long long)c->x[5], deref_reg);
            uint64_t p = c->x[deref_reg];
            for (int k = 0; k < deref_n && p >= GUEST_LO && p + 8 * (k + 1) <= GUEST_HI; k++) {
                uint64_t v = ((uint64_t *)(uintptr_t)p)[k];
                n += snprintf(line + n, sizeof line - n, " %llx", (unsigned long long)v);
                const char *s = (const char *)(uintptr_t)v;  // a pointer to text (a name): show it
                int len = 0;
                if (v >= GUEST_LO && v + 64 <= GUEST_HI)
                    while (len < 64 && s[len] >= 0x20 && s[len] < 0x7f) len++;
                if (len >= 4 && (len == 64 || !s[len]) && n < (int)sizeof line - 80)
                    n += snprintf(line + n, sizeof line - n, "=\"%.*s\"", len, s);
            }
            fprintf(stderr, "%s]\n", line);
        }
}

void fnprof_hit(uint64_t a, uint64_t lr, uint64_t x0) {
    if (sdk_probe_on) {
        sdk_entry_probe(a, x0);
        if (sdk_probe_only) return;
    }
    if (bt_n) bt_check(a, lr, x0);
    if (args_n) args_check(a, lr);
    uint32_t h = (uint32_t)((a >> 2) * 0x9E3779B1u) & (NSLOT - 1);
    for (int n = 0; n < NSLOT; n++, h = (h + 1) & (NSLOT - 1)) {
        uint64_t cur = atomic_load_explicit(&slots[h].addr, memory_order_relaxed);
        if (cur == a || (cur == 0 && atomic_compare_exchange_strong(&slots[h].addr, &cur, a)) || cur == a) {
            atomic_fetch_add_explicit(&slots[h].count, 1, memory_order_relaxed);
            return;
        }
    }
}

static int cmp(const void *x, const void *y) {
    uint64_t a = ((const uint64_t *)x)[1], b = ((const uint64_t *)y)[1];
    return a < b ? 1 : a > b ? -1 : 0;
}

void fnprof_frame(uint64_t frame) {
    static long from = -2, to;
    if (from == -2) {
        from = -1;
        const char *e = getenv("SAVAGEWIND_FNPROF");
        if (e && sscanf(e, "%ld:%ld", &from, &to) != 2) from = -1;
    }
    if (from < 0) return;
    if ((long)frame == from) { g_fnprof = 1; LOG("[fnprof] on at frame %llu\n", (unsigned long long)frame); }
    if ((long)frame != to) return;
    g_fnprof = 0;
    uint64_t (*rows)[2] = malloc(sizeof(uint64_t[2]) * NSLOT);
    int n = 0;
    for (int i = 0; i < NSLOT; i++)
        if (slots[i].addr) { rows[n][0] = slots[i].addr; rows[n][1] = slots[i].count; n++; }
    qsort(rows, n, sizeof rows[0], cmp);
    const char *out = getenv("SAVAGEWIND_FNPROF_OUT") ? getenv("SAVAGEWIND_FNPROF_OUT") : "build/fnprof.txt";  // (parallel runs)
    FILE *f = fopen(out, "w");
    for (int i = 0; f && i < n; i++) fprintf(f, "%llx %llu\n", (unsigned long long)rows[i][0], (unsigned long long)rows[i][1]);
    if (f) fclose(f);
    free(rows);
    LOG("[fnprof] %d functions, written %s (frames %ld-%ld)\n", n, out, from, to);
    from = -1;
}

// SAVAGEWIND_FNPROF=0:<to>: profile from process start (threads the game starts before the first frame)
__attribute__((constructor)) static void fnprof_early(void) {
    const char *e = getenv("SAVAGEWIND_FNPROF");
    if (e && e[0] == '0' && e[1] == ':') g_fnprof = 1;
    const char *probe = getenv("SAVAGEWIND_SDK_ENTRY_PROBE");
    sdk_probe_on = probe && !strcmp(probe, "1");
    sdk_probe_only = sdk_probe_on && (!e || !*e);
    if (sdk_probe_on) g_fnprof = 1;
}
