// Host harness for tools/test_exclusive_pair.py.  Pair instruction bodies in
// exclusive_pair_generated.h are emitted by the production lifter.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "cpu.h"
#include "exclusive_pair_generated.h"

#define DATA_BASE 0x7200000000ull
#define DATA_SIZE 0x1000
#define NTHREADS 4
#define ITERATIONS 10000

_Static_assert(offsetof(Cpu, excl_val2) == 336, "exclusive pair high value offset");
_Static_assert(offsetof(Cpu, v) == 512, "V register offset preserved");
_Static_assert(sizeof(Cpu) == 1024, "Cpu size preserved");

struct Input {
    uint64_t which;
    uint64_t x[32];
    uint64_t sp;
    uint8_t memory[16];
};

struct Output {
    uint64_t x[32];
    uint64_t sp;
    uint8_t memory[16];
};

_Static_assert(sizeof(struct Input) == 288, "input wire size");
_Static_assert(sizeof(struct Output) == 280, "output wire size");

static GuestFn const cases[] = {
    pair_ldxp_stxp,
    pair_ldaxp_stlxp,
    pair_second_store_fails,
    pair_clrex_fails,
    pair_changed_high_half_fails,
    pair_xzr_sp,
    pair_load_base_result_overlap,
    pair_store_base_result_overlap,
};

void rt_udf(Cpu *c, uint64_t pc) {
    (void)c;
    fprintf(stderr, "unexpected rt_udf at %016llx\n", (unsigned long long)pc);
    abort();
}

static int run_lifted_case(void) {
    struct Input in;
    if (fread(&in, sizeof in, 1, stdin) != 1 || in.which >= sizeof cases / sizeof cases[0]) {
        fputs("invalid test input\n", stderr);
        return 2;
    }
    void *p = mmap((void *)(uintptr_t)DATA_BASE, DATA_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void *)(uintptr_t)DATA_BASE) {
        perror("mmap test data");
        return 2;
    }
    memcpy(p, in.memory, sizeof in.memory);
    Cpu c = {0};
    memcpy(c.x, in.x, sizeof c.x);
    c.sp = in.sp;
    c.excl_addr = ~0ull;
    cases[in.which](&c);
    struct Output out;
    memcpy(out.x, c.x, sizeof out.x);
    out.sp = c.sp;
    memcpy(out.memory, p, sizeof out.memory);
    return fwrite(&out, sizeof out, 1, stdout) == 1 ? 0 : 2;
}

static _Alignas(16) __uint128_t counter_pair;
static _Alignas(16) __uint128_t toggle_pair;
static const __uint128_t TOGGLE_A = ((__uint128_t)0xaaaaaaaaaaaaaaaaull << 64) | 0xaaaaaaaaaaaaaaaaull;
static const __uint128_t TOGGLE_B = ((__uint128_t)0x5555555555555555ull << 64) | 0x5555555555555555ull;
static _Atomic int toggle_ready;
static _Atomic int toggle_go;
static _Atomic int toggle_done;
static _Atomic int torn_observed;
static _Atomic uint64_t observations;

static uint64_t address_of(const __uint128_t *p) {
    return (uint64_t)(uintptr_t)p;
}

static void *increment_worker(void *unused) {
    (void)unused;
    Cpu c = {0};
    uint64_t address = address_of(&counter_pair);
    for (unsigned i = 0; i < ITERATIONS; i++) {
        for (;;) {
            __uint128_t old = ldxp128(&c, address, false, 0x1000);
            uint64_t lo = (uint64_t)old + 1;
            uint64_t hi = (uint64_t)(old >> 64) + (lo == 0);
            if (stxp128(&c, address, lo, hi, false) == 0) break;
        }
    }
    return NULL;
}

static void *toggle_writer(void *unused) {
    (void)unused;
    Cpu c = {0};
    uint64_t address = address_of(&toggle_pair);
    atomic_fetch_add_explicit(&toggle_ready, 1, memory_order_release);
    while (!atomic_load_explicit(&toggle_go, memory_order_acquire)) { }
    for (unsigned i = 0; i < 50000; i++) {
        for (;;) {
            __uint128_t old = ldxp128(&c, address, false, 0x2000);
            __uint128_t next = old == TOGGLE_A ? TOGGLE_B : TOGGLE_A;
            if (stxp128(&c, address, (uint64_t)next, (uint64_t)(next >> 64), false) == 0) break;
        }
    }
    atomic_store_explicit(&toggle_done, 1, memory_order_release);
    return NULL;
}

static void *toggle_reader(void *unused) {
    (void)unused;
    atomic_fetch_add_explicit(&toggle_ready, 1, memory_order_release);
    while (!atomic_load_explicit(&toggle_go, memory_order_acquire)) { }
    do {
        __uint128_t value = __atomic_load_n(&toggle_pair, __ATOMIC_ACQUIRE);
        if (value != TOGGLE_A && value != TOGGLE_B)
            atomic_store_explicit(&torn_observed, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&observations, 1, memory_order_relaxed);
    } while (!atomic_load_explicit(&toggle_done, memory_order_acquire));
    return NULL;
}

static int run_stress(void) {
    pthread_t workers[NTHREADS];
    const __uint128_t initial = ((__uint128_t)7 << 64) | (uint64_t)(UINT64_MAX - 100);
    __atomic_store_n(&counter_pair, initial, __ATOMIC_SEQ_CST);
    for (unsigned i = 0; i < NTHREADS; i++)
        if (pthread_create(&workers[i], NULL, increment_worker, NULL) != 0) {
            perror("pthread_create increment");
            return 2;
        }
    for (unsigned i = 0; i < NTHREADS; i++) pthread_join(workers[i], NULL);
    __uint128_t expected = initial + (uint64_t)(NTHREADS * ITERATIONS);
    __uint128_t actual = __atomic_load_n(&counter_pair, __ATOMIC_SEQ_CST);
    if (actual != expected) {
        fprintf(stderr, "128-bit increment mismatch: got %016llx:%016llx expected %016llx:%016llx\n",
                (unsigned long long)(actual >> 64), (unsigned long long)actual,
                (unsigned long long)(expected >> 64), (unsigned long long)expected);
        return 1;
    }

    __atomic_store_n(&toggle_pair, TOGGLE_A, __ATOMIC_SEQ_CST);
    pthread_t writer, reader;
    if (pthread_create(&writer, NULL, toggle_writer, NULL) != 0 ||
        pthread_create(&reader, NULL, toggle_reader, NULL) != 0) {
        perror("pthread_create atomicity");
        return 2;
    }
    while (atomic_load_explicit(&toggle_ready, memory_order_acquire) != 2) { }
    atomic_store_explicit(&toggle_go, 1, memory_order_release);
    pthread_join(writer, NULL);
    pthread_join(reader, NULL);
    if (atomic_load_explicit(&torn_observed, memory_order_relaxed)) {
        fputs("reader observed a torn 128-bit pair store\n", stderr);
        return 1;
    }
    printf("ok: 4 pthreads x 10000 pair increments; atomic 128-bit store (%llu reads)\n",
           (unsigned long long)atomic_load_explicit(&observations, memory_order_relaxed));
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "stress") == 0) return run_stress();
    return run_lifted_case();
}
