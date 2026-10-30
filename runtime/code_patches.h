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
