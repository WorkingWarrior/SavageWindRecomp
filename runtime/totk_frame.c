// TotK's frame rate and game clock: the host half of the patch sets fps60, realtime and bounce
// (tools/patches-totk-100.txt; the sites, the page layout and the stepping follow totk-pc-recomp's runtime/frame_rate.c
// and MacNativeGames' game_patches.c, both MIT).
//
// The game's adaptive present-interval controller turns the main thread's measured work into a number of 60 Hz ticks;
// shipping data keeps it at 2 or more (30 fps), the set fps60 lowers its minimum to 1. The controller raises the interval
// again when a frame doesn't fit. Shipping code then steps the game by whole vsyncs (ratio = interval * 0.5, seconds =
// interval / 60), so the game's speed is right only when frames take exactly their interval; with the set realtime both
// read the patch page instead: +0 ratio = seconds * 30, +4 seconds, the moving average of the last 5 display intervals,
// at most 1/15 s (a slower frame slows the game down, as on the console). The pacer then holds the frame limit and
// ignores the game's interval.
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code_patches.h"
#include "platform.h"
#include "rt.h"
#include "settings.h"
#include "totk_frame.h"

#define STEP_SAMPLES 5
#define STEP_MAX_S (1.0 / 15.0)  // the longest step: a longer frame slows the game down
#define STEP_RESET_S 0.25        // a gap this long (loading) restarts the average
#define STEP_FIRST_S (1.0 / 30.0)

static _Atomic uint64_t *step_word;  // patch page +0: ratio (low float), seconds (high float)
static int set_fps60 = -1, set_realtime = -1, set_bounce = -1;
static int limit_now = 30;
static uint64_t last_ns;
static double samples[STEP_SAMPLES];
static unsigned sample_count, sample_next;

static void step_store(float seconds) {
    float ratio = seconds * 30.0f;
    uint32_t lo, hi;
    memcpy(&lo, &ratio, 4);
    memcpy(&hi, &seconds, 4);
    atomic_store_explicit(step_word, (uint64_t)lo | (uint64_t)hi << 32, memory_order_release);
}

// Video > Frame rate (30 or 60 here; the env override SAVAGEWIND_FPS wins): 30 keeps the controller's shipped minimum.
static int limit_wanted(void) {
    const char *e = getenv("SAVAGEWIND_FPS");
    int v = e && *e ? atoi(e) : g_settings.fps_limit;
    return v == 30 ? 30 : 60;
}

static void limit_apply(void) {
    int hz = limit_wanted();
    if (hz == limit_now) return;
    limit_now = hz;
    rt_patch_switch(set_fps60, hz > 30);
    LOG("[frame] limit %d fps\n", hz);
}

void totk_frame_init(void) {
    set_fps60 = rt_patch_set("fps60"), set_realtime = rt_patch_set("realtime"), set_bounce = rt_patch_set("bounce");
    uint8_t *page = rt_patch_page();
    if (set_fps60 < 0 || set_realtime < 0 || !page) {
        LOG("[frame] the game library has no frame rate patch sets: the game's own 30 fps, whole-vsync steps\n");
        return;
    }
    if (rt_patch_enabled(set_realtime)) {
        step_word = (_Atomic uint64_t *)page;
        step_store((float)STEP_FIRST_S);
    } else {
        rt_patch_switch(set_fps60, 0);  // (above 30 fps needs the real-time step)
    }
    limit_now = rt_patch_enabled(set_fps60) ? 60 : 30;
    limit_apply();
    LOG("[frame] fps60 %s, realtime %s, bounce %s; limit %d\n", rt_patch_enabled(set_fps60) ? "on" : "off",
        rt_patch_enabled(set_realtime) ? "on" : "off", rt_patch_enabled(set_bounce) ? "on" : "off", limit_now);
}

int totk_frame_limit(void) { return step_word ? limit_now : 30; }
int totk_frame_realtime(void) { return step_word != NULL; }

void totk_frame_present(uint64_t display_ns) {
    if (!step_word) return;
    limit_apply();
    double dt = last_ns ? (double)(display_ns - last_ns) * 1e-9 : 0;
    if (last_ns) {
        if (dt > STEP_RESET_S || dt < 0.001) {
            sample_count = sample_next = 0;
        } else {
            if (dt > STEP_MAX_S) dt = STEP_MAX_S;
            samples[sample_next++ % STEP_SAMPLES] = dt;
            if (sample_count < STEP_SAMPLES) sample_count++;
            double sum = 0;
            for (unsigned i = 0; i < sample_count; i++) sum += samples[i];
            step_store((float)(sum / sample_count));
        }
    }
    last_ns = display_ns;
}
