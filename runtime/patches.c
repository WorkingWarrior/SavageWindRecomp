// Code patch sets (tools/wordpatches.py, gen.py --patches): at a patched site the generated C reads
// `if (rt_patch_on[set]) { replacement } else { original }`, so a set is switched at start from its environment
// variable (SAVAGEWIND_PATCH_<NAME>=0/1) and while the game runs (rt_patch_switch). The sets, their variables and the
// patch page come from the generated patches.h; without it (C generated without --patches) there are no sets.
//
// The patch page is a guest page inside the module window, within an ADRP's reach of the patched code; its layout
// belongs to the sets that use it (scene: +16 the scene size {w, h} as floats).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code_patches.h"
#include "rt.h"

#if __has_include("patches.h")
#include "patches.h"
#endif
#ifndef RT_PATCH_COUNT
#define RT_PATCH_COUNT 0
#endif

uint8_t rt_patch_on[RT_PATCH_COUNT > 0 ? RT_PATCH_COUNT : 1];  // read by the generated C at every patched site

#if RT_PATCH_COUNT
static const struct { const char *name, *env; int on; } sets[] = {
#define X(i, n, e, d) {#n, e, d},
    RT_PATCH_SETS(X)
#undef X
};

void rt_patches_init(void) {
    memset((void *)(uintptr_t)RT_PATCH_PAGE, 0, 0x1000);  // (guest memory is mapped whole; a reboot cleared it)
    char list[256];
    size_t n = 0;
    list[0] = 0;
    for (int i = 0; i < RT_PATCH_COUNT; i++) {
        const char *e = getenv(sets[i].env);
        rt_patch_on[i] = (uint8_t)(e && *e ? strcmp(e, "0") != 0 : sets[i].on);
        n += (size_t)snprintf(list + n, sizeof list - n, "%s%s %s", i ? ", " : "", sets[i].name, rt_patch_on[i] ? "on" : "off");
        if (n >= sizeof list) n = sizeof list - 1;
    }
    LOG("[patches] %s (page %#llx)\n", list, (unsigned long long)RT_PATCH_PAGE);
}

int rt_patch_set(const char *name) {
    for (int i = 0; i < RT_PATCH_COUNT; i++)
        if (!strcmp(sets[i].name, name)) return i;
    return -1;
}

uint8_t *rt_patch_page(void) { return (uint8_t *)(uintptr_t)RT_PATCH_PAGE; }
#else
void rt_patches_init(void) {}
int rt_patch_set(const char *name) { (void)name; return -1; }
uint8_t *rt_patch_page(void) { return NULL; }
#endif

void rt_patch_switch(int set, int on) {
    if (set >= 0 && set < RT_PATCH_COUNT) __atomic_store_n(&rt_patch_on[set], (uint8_t)(on != 0), __ATOMIC_RELAXED);
}

int rt_patch_enabled(int set) { return set >= 0 && set < RT_PATCH_COUNT && rt_patch_on[set]; }
