// Which open controller does what (docs/CONTROLLER_ARCHITECTURE.md): plain decisions, no SDL, so tools/test_pad_policy.c
// can run every case. runtime/hle/input.c feeds it what SDL reports.
//
// Started through Steam, SDL hides the Steam Controller (SDL_GAMECONTROLLER_IGNORE_DEVICES) and gives only Steam's
// virtual pad, which has no sensors. runtime/hle/input.c lifts that for this controller, so both can be there: then they
// are one controller, and each job has one source. Motion and vibration always come from the real one. Buttons and sticks
// come from the virtual one, which carries the player's Steam layout, once it has shown it is alive: it can be silent
// (seen with a Steam Controller on its Puck, in both games, with and without a layout).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAD_MAX 8
#define PAD_VALVE 0x28de

enum { STEAM_INPUT_AUTO, STEAM_INPUT_PHYSICAL, STEAM_INPUT_STEAM, STEAM_INPUT_COUNT };
enum { PAD_PAIR_NONE, PAD_PAIR_OK, PAD_PAIR_AMBIGUOUS };

typedef struct {
    uint16_t vendor, product;  // (a Steam virtual pad: the real controller's, from Steam's file)
    uint64_t steam_handle;     // non-zero: Steam's virtual pad
    uint64_t last_use;         // any increasing clock; 0: never used
} PadEntry;

typedef struct {
    int input, sensors, haptics;  // indices, -1: none
    int pair;                     // PAD_PAIR_*
    int physical, virt;           // the pair's members (-1: no pair)
} PadRoutes;

// The Steam Controller 2026: wired, Bluetooth, and its two USB receivers. SDL_GetGamepadType can't say so: 3.4.18 says
// steam, 3.4.16 standard, 3.2 has no such value.
static inline bool pad_is_triton(uint16_t vendor, uint16_t product) {
    return vendor == PAD_VALVE && product >= 0x1302 && product <= 0x1305;
}

// Buttons of this controller that are not for the player to press: its stick and grip touch sensors (SDL's misc3 to
// misc6, by value here so this stays free of SDL). A hand resting on it isn't using it.
static inline bool pad_is_touch(uint16_t vendor, uint16_t product, int button, int misc3) {
    return pad_is_triton(vendor, product) && button >= misc3 && button < misc3 + 4;
}

// Its buttons Steam keeps for itself: they mean nothing to the game. Pressing them isn't playing on it.
static inline bool pad_is_system(uint16_t vendor, uint16_t product, int button, int guide, int misc1) {
    return pad_is_triton(vendor, product) && (button == guide || button == misc1);
}

// preferred: the entry chosen in settings, -1 Automatic (the one used last). virtual_live: the pair's virtual pad has
// reported input. Two controllers that can't be told apart stay two controllers, each its own motion and vibration:
// nothing Steam reports says which real one a virtual pad stands for, and the wrong one's motion is worse than none.
static inline PadRoutes pad_routes(const PadEntry *p, int n, int preferred, int steam_input, bool virtual_live) {
    PadRoutes r = {-1, -1, -1, PAD_PAIR_NONE, -1, -1};
    int nphys = 0, nvirt = 0, phys = -1, virt = -1;
    for (int i = 0; i < n; i++) {
        if (!pad_is_triton(p[i].vendor, p[i].product)) continue;
        if (p[i].steam_handle) nvirt++, virt = i;
        else nphys++, phys = i;
    }
    if (nphys && nvirt) r.pair = nphys == 1 && nvirt == 1 ? PAD_PAIR_OK : PAD_PAIR_AMBIGUOUS;
    bool skip_virtual = nphys > 0;  // (real ones present: a Steam Controller's virtual pad is only ever half of one)
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (skip_virtual && p[i].steam_handle && pad_is_triton(p[i].vendor, p[i].product)) continue;
        if (best < 0 || p[i].last_use > p[best].last_use) best = i;
    }
    if (preferred >= 0 && preferred < n) {
        bool hidden = skip_virtual && p[preferred].steam_handle && pad_is_triton(p[preferred].vendor, p[preferred].product);
        if (!hidden) best = preferred;
    }
    if (best < 0) return r;
    r.input = r.sensors = r.haptics = best;
    if (r.pair == PAD_PAIR_OK) {
        r.physical = phys, r.virt = virt;
        if (best == phys && (steam_input == STEAM_INPUT_STEAM || (steam_input == STEAM_INPUT_AUTO && virtual_live)))
            r.input = virt;
    }
    return r;
}

// SDL_GAMECONTROLLER_IGNORE_DEVICES without the Steam Controller 2026: out gets the list ("0xVVVV/0xPPPP,...") minus those
// entries, everything else as it was, in order. False: nothing to take out, or it doesn't fit.
static inline bool steam_ignore_filter(const char *in, char *out, size_t size) {
    bool removed = false;
    size_t used = 0;
    if (size) out[0] = 0;
    for (const char *s = in; s && *s;) {
        const char *end = strchr(s, ',');
        size_t len = end ? (size_t)(end - s) : strlen(s);
        unsigned vendor = 0, product = 0;
        char entry[32];
        bool hit = false;
        if (len < sizeof entry) {
            memcpy(entry, s, len);
            entry[len] = 0;
            char *p = entry, *q;
            vendor = (unsigned)strtoul(p, &q, 16);
            if (*q == '/') product = (unsigned)strtoul(q + 1, &q, 16), hit = !*q && pad_is_triton((uint16_t)vendor, (uint16_t)product);
        }
        if (hit) removed = true;
        else if (len) {
            if (used + len + 2 > size) return false;
            if (used) out[used++] = ',';
            memcpy(out + used, s, len);
            used += len;
            out[used] = 0;
        }
        s = end ? end + 1 : s + len;
    }
    return removed;
}
