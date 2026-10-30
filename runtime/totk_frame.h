// TotK's frame rate and game clock (runtime/totk_frame.c): the host half of the patch sets fps60, realtime and bounce.
#pragma once
#include <stdint.h>

void totk_frame_init(void);                  // boot (after rt_patches_init): the sets' switches, the clock's first value
void totk_frame_present(uint64_t display_ns);  // each present: the game's step from the measured display intervals
int totk_frame_limit(void);                  // the frame limit in effect, frames per second (30 or 60)
int totk_frame_realtime(void);               // 1 if the game steps by the measured time (the pacer holds the limit)
