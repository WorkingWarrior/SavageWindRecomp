// The transient ring's chunk sizes under host-memory pressure (runtime/nvn/vulkan/hostroom.h). make test-hostroom
#include <assert.h>
#include <stdio.h>
#include "../runtime/nvn/vulkan/hostroom.h"

#define MB (1ull << 20)

static void check(uint64_t size, const uint64_t *want, int nwant) {
    uint64_t got[8];
    int n = hostroom_chunk_sizes(size, 64 * MB, got, 8);
    assert(n == nwant);
    for (int i = 0; i < n; i++) assert(got[i] == want[i]);
}

int main(void) {
    check(100, (const uint64_t[]){64 * MB, 32 * MB, 16 * MB, 8 * MB, 4 * MB, 2 * MB, MB}, 7);  // (a small request: down to 1 MB)
    check(5 * MB + 1, (const uint64_t[]){64 * MB, 32 * MB, 16 * MB, 8 * MB, 6 * MB}, 5);      // (the request, rounded up, last)
    check(8 * MB, (const uint64_t[]){64 * MB, 32 * MB, 16 * MB, 8 * MB}, 4);                  // (a power of two ends on itself)
    check(64 * MB, (const uint64_t[]){64 * MB}, 1);
    check(100 * MB, (const uint64_t[]){100 * MB}, 1);                                         // (larger than a chunk: only itself)
    check(0, (const uint64_t[]){64 * MB, 32 * MB, 16 * MB, 8 * MB, 4 * MB, 2 * MB, MB}, 7);
    uint64_t few[3];
    assert(hostroom_chunk_sizes(100, 64 * MB, few, 3) == 3 && few[2] == 16 * MB);             // (max respected)
    puts("hostroom: ok");
    return 0;
}
