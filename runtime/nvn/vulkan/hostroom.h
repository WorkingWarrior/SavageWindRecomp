// hostroom.h: the transient ring's chunk sizes when host-visible memory runs short (exec.c ring_grow). Header-only so
// tools/test_hostroom.c checks it without a GPU.
// Adapted from weeping-of-the-empire v0.1.8.2 engine/runtime/nvn/vulkan/hostroom.h (Copyright (c) 2026
// daniel-madrid-07, MIT License). Their report (GTX 1660 6 GB, 8 GB RAM, driver 561.09): the imported guest heap is
// counted against the host-visible heap by the driver, outside the allocator's books, so at a fast travel's load a
// 64 MB ring chunk found no host-visible type with room (the 256 MB BAR heap full too) and its failure was FATAL.
#pragma once
#include <stdint.h>

// The ring chunk sizes to try for a request of `size` bytes, largest first, into out[] (at most `max`): the usual chunk
// (or the request, when larger), then halves down to the request rounded up to 1 MB, then that rounded request itself.
// Smaller chunks still fit where a full heap refuses the usual one.
static inline int hostroom_chunk_sizes(uint64_t size, uint64_t chunk, uint64_t *out, int max) {
    const uint64_t mb = 1ull << 20, need = size ? (size + mb - 1) & ~(mb - 1) : mb;
    int n = 0;
    if (n < max) out[n++] = size > chunk ? size : chunk;
    if (size > chunk) return n;
    for (uint64_t s = chunk / 2; s >= need && n < max; s /= 2) out[n++] = s;
    if (n < max && out[n - 1] > need) out[n++] = need;
    return n;
}
