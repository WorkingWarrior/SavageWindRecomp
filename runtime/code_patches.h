// Code patch sets: instruction replacements in the lifted C that the runtime switches on and off (tools/wordpatches.py,
// gen.py --patches; runtime/patches.c). A game library generated without them has no sets and every site runs the
// original instruction.
#pragma once
#include <stdint.h>

void rt_patches_init(void);        // boot: the sets' switches from their environment variables, the patch page
int rt_patch_set(const char *name);  // the index of a set, -1 if there is none
void rt_patch_switch(int set, int on);
int rt_patch_enabled(int set);
uint8_t *rt_patch_page(void);      // the patch page (guest memory), NULL without sets
// TotK's largest scene render size: the heap definition (runtime/services/fs.c) is grown for it at boot, the scene
// patch (runtime/nvn/output_scale.c) stops there. SAVAGEWIND_SCENE_MAX=<w>x<h> sets it (default 3840x2160; 1600x900: no growth).
void rt_scene_max(unsigned *w, unsigned *h);
uint32_t rt_totk_heap_extra_kib(void);  // what the texture and graphics heaps grow by (KiB; 0 for no growth)
uint64_t rt_totk_pool_extra(void);     // the application memory it adds (bytes): the game sizes its heaps from what svc GetInfo says
