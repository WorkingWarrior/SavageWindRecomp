// Which controller does what (runtime/pad_policy.h): every decision, no SDL.
#include <assert.h>
#include <stdio.h>
#include "pad_policy.h"

enum { TRITON = 0x1302, PUCK = 0x1304, XBOX = 0x45e };

static PadEntry real(uint16_t vendor, uint16_t product, uint64_t used) { return (PadEntry){vendor, product, 0, used}; }
static PadEntry steam(uint64_t handle) { return (PadEntry){PAD_VALVE, TRITON, handle, 0}; }

int main(void) {
    assert(pad_is_triton(PAD_VALVE, 0x1302) && pad_is_triton(PAD_VALVE, 0x1305));
    assert(!pad_is_triton(PAD_VALVE, 0x1205) && !pad_is_triton(PAD_VALVE, 0x1306) && !pad_is_triton(XBOX, 0x1304));

    // touch sensors and Steam's buttons, this controller only: on another controller misc3 is a button like any other
    assert(pad_is_touch(PAD_VALVE, PUCK, 22, 22) && pad_is_touch(PAD_VALVE, PUCK, 25, 22));
    assert(!pad_is_touch(PAD_VALVE, PUCK, 21, 22) && !pad_is_touch(PAD_VALVE, PUCK, 26, 22) && !pad_is_touch(XBOX, 1, 22, 22));
    assert(pad_is_system(PAD_VALVE, PUCK, 5, 5, 15) && pad_is_system(PAD_VALVE, PUCK, 15, 5, 15));
    assert(!pad_is_system(PAD_VALVE, PUCK, 0, 5, 15) && !pad_is_system(XBOX, 1, 5, 5, 15));

    PadRoutes r = pad_routes(NULL, 0, -1, STEAM_INPUT_AUTO, false);  // nothing there
    assert(r.input == -1 && r.sensors == -1 && r.haptics == -1 && r.pair == PAD_PAIR_NONE);

    // one ordinary controller
    PadEntry one[] = {real(XBOX, 1, 0)};
    r = pad_routes(one, 1, -1, STEAM_INPUT_AUTO, false);
    assert(r.input == 0 && r.sensors == 0 && r.haptics == 0 && r.pair == PAD_PAIR_NONE);

    // Steam's virtual pad alone (distro SDL without the driver, or the opt-out): it's what there is
    PadEntry alone[] = {steam(7)};
    r = pad_routes(alone, 1, -1, STEAM_INPUT_AUTO, false);
    assert(r.input == 0 && r.pair == PAD_PAIR_NONE);

    // Both of one controller. The virtual pad silent: everything real, in either order they appeared
    PadEntry pair[] = {real(PAD_VALVE, PUCK, 0), steam(7)};
    r = pad_routes(pair, 2, -1, STEAM_INPUT_AUTO, false);
    assert(r.pair == PAD_PAIR_OK && r.physical == 0 && r.virt == 1);
    assert(r.input == 0 && r.sensors == 0 && r.haptics == 0);
    PadEntry rev[] = {steam(7), real(PAD_VALVE, PUCK, 0)};
    r = pad_routes(rev, 2, -1, STEAM_INPUT_AUTO, false);
    assert(r.pair == PAD_PAIR_OK && r.physical == 1 && r.input == 1 && r.sensors == 1 && r.haptics == 1);

    // ... alive: buttons from it, the rest stays. One source for each (input_user_blocked keeps the other half from the menus)
    r = pad_routes(pair, 2, -1, STEAM_INPUT_AUTO, true);
    assert(r.input == 1 && r.sensors == 0 && r.haptics == 0);

    // The setting overrides what was seen
    r = pad_routes(pair, 2, -1, STEAM_INPUT_PHYSICAL, true);
    assert(r.input == 0 && r.sensors == 0);
    r = pad_routes(pair, 2, -1, STEAM_INPUT_STEAM, false);
    assert(r.input == 1 && r.sensors == 0 && r.haptics == 0);

    // Two identical controllers and two virtual pads: no telling which is which, so no pair, and nothing virtual
    PadEntry two[] = {real(PAD_VALVE, PUCK, 1), real(PAD_VALVE, PUCK, 2), steam(7), steam(8)};
    r = pad_routes(two, 4, -1, STEAM_INPUT_AUTO, true);
    assert(r.pair == PAD_PAIR_AMBIGUOUS && r.input == 1 && r.sensors == 1 && r.haptics == 1 && r.virt == -1);
    two[0].last_use = 3;
    r = pad_routes(two, 4, -1, STEAM_INPUT_STEAM, true);
    assert(r.input == 0 && r.sensors == 0);

    // One real, two virtual (a second controller Steam knows and SDL can't open): the same, no guessing
    PadEntry odd[] = {real(PAD_VALVE, PUCK, 0), steam(7), steam(8)};
    r = pad_routes(odd, 3, -1, STEAM_INPUT_AUTO, true);
    assert(r.pair == PAD_PAIR_AMBIGUOUS && r.input == 0 && r.sensors == 0);

    // Used last wins; a choice in settings beats it; a chosen virtual half is the pair
    PadEntry mix[] = {real(XBOX, 1, 5), real(PAD_VALVE, PUCK, 9), steam(7)};
    r = pad_routes(mix, 3, -1, STEAM_INPUT_AUTO, true);
    assert(r.input == 2 && r.sensors == 1 && r.haptics == 1);
    r = pad_routes(mix, 3, 0, STEAM_INPUT_AUTO, true);
    assert(r.input == 0 && r.sensors == 0 && r.haptics == 0 && r.pair == PAD_PAIR_OK);
    r = pad_routes(mix, 3, 2, STEAM_INPUT_AUTO, false);
    assert(r.input == 1);
    r = pad_routes(mix, 3, 7, STEAM_INPUT_AUTO, false);  // (gone from the list)
    assert(r.input == 1);

    // Another controller's virtual pad is an ordinary controller
    PadEntry other[] = {(PadEntry){XBOX, 1, 7, 2}, real(PAD_VALVE, PUCK, 1)};
    r = pad_routes(other, 2, -1, STEAM_INPUT_AUTO, false);
    assert(r.pair == PAD_PAIR_NONE && r.input == 0);

    char out[128];
    const char *list = "0x28de/0x1002,0x28de/0x1302,0x28de/0x1303,0x28de/0x1304,0x28de/0x1305,0x28de/0x12ff,0x45e/0x28e";
    assert(steam_ignore_filter(list, out, sizeof out));
    assert(!strcmp(out, "0x28de/0x1002,0x28de/0x12ff,0x45e/0x28e"));
    assert(!steam_ignore_filter("0x28de/0x1002", out, sizeof out) && !strcmp(out, "0x28de/0x1002"));
    assert(steam_ignore_filter("0x28de/0x1304", out, sizeof out) && !out[0]);  // (empty: nothing left to ignore)
    assert(!steam_ignore_filter("", out, sizeof out) && !out[0]);
    assert(!steam_ignore_filter(NULL, out, sizeof out));
    assert(!steam_ignore_filter("0x28de/0x1002,0x28de/0x1304", out, 8));  // (doesn't fit: leave Steam's list alone)
    assert(steam_ignore_filter("nonsense,0x28de/0x1304,0x28de/zzz", out, sizeof out) && !strcmp(out, "nonsense,0x28de/zzz"));
    puts("pad policy checks passed");
}
