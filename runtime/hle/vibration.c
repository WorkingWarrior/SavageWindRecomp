// The SDK mixes its vibration nodes itself, then sends HID commands 201 / 206. Controllers that can play a frequency
// get the game's values (Nintendo's) or a tone (Steam Controller); SDL's two motors cannot reproduce HD rumble
// frequencies, so everything else gets an approximation with frequency-shaped motor amplitudes by default.
#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../hle.h"
#include "../vibration.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static VibrationValue values[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};
static bool permitted = true;

// These are the handles returned by hid_vibration_handles (input.c), for our one player or handheld mode.
static int device(uint32_t handle) {
    uint32_t id = (handle >> 8) & 0xff;
    return (handle & 0xfffe00ffu) == 3 && (id == 0 || id == 0x20) ? (int)((handle >> 16) & 1) : -1;
}

static float amplitude(float a) { return !isfinite(a) || a < 0 ? 0 : a > 1 ? 1 : a; }

void vibration_send(uint32_t handle, const VibrationValue *value) {
    int i = device(handle);
    static int log_on = -1;  // SAVAGEWIND_LOG_VIBRATION=1: what the game sends, each value once
    if (log_on < 0) log_on = getenv("SAVAGEWIND_LOG_VIBRATION") != NULL;
    if (log_on) {
        static _Thread_local VibrationValue seen[2];
        if (i < 0 || memcmp(&seen[i], value, sizeof *value)) {
            LOG("[vibration] %08x%s low %.3f @ %.0f high %.3f @ %.0f\n", handle, i < 0 ? " (not ours)" : "",
                value->low_amplitude, value->low_frequency, value->high_amplitude, value->high_frequency);
            if (i >= 0) seen[i] = *value;
        }
    }
    if (i < 0) return;
    pthread_mutex_lock(&lock);
    if (permitted) {
        values[i] = *value;
        values[i].low_amplitude = amplitude(value->low_amplitude);
        values[i].high_amplitude = amplitude(value->high_amplitude);
    }
    pthread_mutex_unlock(&lock);
}

VibrationValue vibration_get(uint32_t handle) {
    VibrationValue v = {0, 160, 0, 320};
    int i = device(handle);
    pthread_mutex_lock(&lock);
    if (i >= 0 && permitted) v = values[i];
    pthread_mutex_unlock(&lock);
    return v;
}

void vibration_clear(void) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 2; i++) values[i] = (VibrationValue){0, 160, 0, 320};
    pthread_mutex_unlock(&lock);
}

void vibration_permit(bool allow) {
    pthread_mutex_lock(&lock);
    permitted = allow;
    if (!allow) memset(values, 0, sizeof values);
    pthread_mutex_unlock(&lock);
}

bool vibration_permitted(void) {
    pthread_mutex_lock(&lock);
    bool allow = permitted;
    pthread_mutex_unlock(&lock);
    return allow;
}

RT_ON_RESET(vibration_reset) {
    vibration_clear();
    vibration_permit(true);
}

static float tune(const char *name, float def) {
    const char *s = getenv(name);
    return s && *s ? (float)atof(s) : def;
}

static bool shape_on(void) {
    static int on = -1;
    if (on < 0) { const char *s = getenv("SAVAGEWIND_VIBRATION_SHAPE"); on = !s || strcmp(s, "0") != 0; }
    return on;
}

// HD rumble bands (amplitude + frequency, two devices) -> big and small eccentric-mass motors.
static void shape_motors(const VibrationValue v[2], uint64_t now, uint16_t *low, uint16_t *high) {
    static float lo_hz, hi_hz, floor_big, floor_small, gamma, boost;
    static bool init;
    static float prev_big, prev_small;
    static uint64_t boost_until;
    if (!init) {
        lo_hz = tune("SAVAGEWIND_VIBRATION_XOVER_LO", 120);
        hi_hz = tune("SAVAGEWIND_VIBRATION_XOVER_HI", 300);
        floor_big = tune("SAVAGEWIND_VIBRATION_FLOOR_BIG", 0.25f);
        floor_small = tune("SAVAGEWIND_VIBRATION_FLOOR_SMALL", 0.15f);
        gamma = tune("SAVAGEWIND_VIBRATION_GAMMA", 0.8f);
        boost = tune("SAVAGEWIND_VIBRATION_BOOST", 1.6f);
        if (hi_hz <= lo_hz) hi_hz = lo_hz + 1;
        init = true;
    }
    float big = 0, small = 0;
    for (int d = 0; d < 2; d++) {
        float amp[2] = {v[d].low_amplitude, v[d].high_amplitude};
        float hz[2] = {v[d].low_frequency, v[d].high_frequency};
        float b = 0, s = 0;
        for (int k = 0; k < 2; k++) {
            float f = isfinite(hz[k]) ? hz[k] : 160;
            float w = fminf(1, fmaxf(0, (hi_hz - f) / (hi_hz - lo_hz)));  // 1 = big motor
            b += amp[k] * w;
            s += amp[k] * (1 - w);
        }
        big = fmaxf(big, b);
        small = fmaxf(small, s);
    }
    big = fminf(big, 1);
    small = fminf(small, 1);
    if (big > prev_big + 0.3f || small > prev_small + 0.3f) boost_until = now + 30;
    prev_big = big;
    prev_small = small;
    float k = now < boost_until ? boost : 1;
    float ob = big < 0.02f ? 0 : fminf(1, (floor_big + (1 - floor_big) * powf(big, gamma)) * k);
    float os = small < 0.02f ? 0 : fminf(1, (floor_small + (1 - floor_small) * powf(small, gamma)) * k);
    *low = (uint16_t)lroundf(ob * 65535);
    *high = (uint16_t)lroundf(os * 65535);
}

// Steam Controller (on its USB dongle): output report 0x83 plays a tone on the controller's actuators. Measured with its
// accelerometer on a desk (0 dB tones, spread of the reading): 30 Hz 0.6, 70 Hz 5.0, 160 Hz 0.4, 320 Hz 0.2. Only side 3
// (both) moves it, sides 1 and 2 give about 1% of that, and one tone plays at a time, so left and right and the two bands
// mix down to the strongest band. Frequencies are halved by default to put the SDK's usual 160 and 320 Hz on the
// strong part of the curve. A tone ends by itself after its duration (0 would be endless); 0x82 ends one at once.
#define STEAM_LEASE_MS 60
static SDL_hid_device *steam;

static bool steam_write(const uint8_t *data, size_t size) { return SDL_hid_write(steam, data, size) == (int)size; }

static bool steam_update(const VibrationValue v[2], uint64_t now) {
    static float scale, makeup, gamma;
    static bool init;
    static int last_hz, last_db;
    static bool playing;
    static uint64_t last_ms;
    if (!init) {
        scale = tune("SAVAGEWIND_VIBRATION_STEAM_SCALE", 0.5f);
        makeup = tune("SAVAGEWIND_VIBRATION_STEAM_GAIN", 3);
        gamma = tune("SAVAGEWIND_VIBRATION_GAMMA", 0.8f);
        init = true;
    }
    float amp = 0, hz = 160, top = 0;
    for (int d = 0; d < 2; d++) {
        amp = fmaxf(amp, v[d].low_amplitude + v[d].high_amplitude);
        float a[2] = {v[d].low_amplitude, v[d].high_amplitude}, f[2] = {v[d].low_frequency, v[d].high_frequency};
        for (int k = 0; k < 2; k++)
            if (a[k] > top && isfinite(f[k])) top = a[k], hz = f[k];
    }
    if (amp < 0.02f) {
        if (!playing) return true;
        playing = false;
        static const uint8_t off[4] = {0x82, 3, 0, 0};
        return steam_write(off, sizeof off);
    }
    int db = (int)lroundf(fminf(6, fmaxf(-40, 20 * log10f(powf(fminf(amp, 1), gamma)) + makeup)));
    int out = (int)lroundf(fminf(1000, fmaxf(30, hz * scale)));
    bool same = playing && out == last_hz && db == last_db;
    if (now - last_ms < (same ? STEAM_LEASE_MS / 2 : 8)) return true;  // (sensor events call this up to 1000 times a second)
    uint8_t r[10] = {0x83, 3, (uint8_t)(int8_t)db, out & 0xff, out >> 8, STEAM_LEASE_MS, 0, 0, 0, 0};
    playing = true;
    last_hz = out;
    last_db = db;
    last_ms = now;
    return steam_write(r, sizeof r);
}

// Nintendo's controllers (SDL's HIDAPI driver, SDL_SendGamepadEffect with a rumble packet) play both bands on both sides.
// Amplitude thresholds of the 101 codes: dekuNukem's rumble_data_table.md, as in SDL's SDL_hidapi_switch.c.
static const uint16_t switch_amp[101] = {
    0, 514, 775, 921, 1096, 1303, 1550, 1843, 2192, 2606, 3100, 3686, 4383, 5213, 6199, 7372, 7698, 8039, 8395, 8767, 9155,
    9560, 9984, 10426, 10887, 11369, 11873, 12398, 12947, 13520, 14119, 14744, 15067, 15397, 15734, 16079, 16431, 16790,
    17158, 17534, 17918, 18310, 18711, 19121, 19540, 19967, 20405, 20851, 21308, 21775, 22251, 22739, 23236, 23745, 24265,
    24797, 25340, 25894, 26462, 27041, 27633, 28238, 28856, 29488, 30134, 30794, 31468, 32157, 32861, 33581, 34316, 35068,
    35836, 36620, 37422, 38242, 39079, 39935, 40809, 41703, 42616, 43549, 44503, 45477, 46473, 47491, 48531, 49593, 50679,
    51789, 52923, 54082, 55266, 56476, 57713, 58977, 60268, 61588, 62936, 64315, 65535};

static int switch_amp_code(float a) {
    uint16_t v = (uint16_t)lroundf(fminf(a, 1) * 65535);
    int i = 0;
    while (i < 100 && v > switch_amp[i]) i++;
    return i;
}

// One side's four bytes: the frequencies as 32 steps per octave from 10 Hz, high band 81.75-1252 Hz, low 40.9-626 Hz.
static void switch_encode(const VibrationValue *v, uint8_t out[4]) {
    float lf = isfinite(v->low_frequency) ? fminf(626, fmaxf(41, v->low_frequency)) : 160;
    float hf = isfinite(v->high_frequency) ? fminf(1252, fmaxf(82, v->high_frequency)) : 320;
    int hi = ((int)lroundf(log2f(hf / 10) * 32) - 0x60) * 4, lo = (int)lroundf(log2f(lf / 10) * 32) - 0x40;
    int ha = switch_amp_code(v->high_amplitude) * 2, c = switch_amp_code(v->low_amplitude), la = 0x40 + c / 2;
    out[0] = hi & 0xff;
    out[1] = (uint8_t)(ha | (hi >> 8 & 1));
    out[2] = (uint8_t)(lo | (c & 1) << 7);
    out[3] = (uint8_t)la;
}

static bool switch_update(SDL_Gamepad *pad, const VibrationValue v[2], uint64_t now) {
    static uint8_t last[10];
    static uint64_t last_ms;
    uint8_t packet[10] = {0x10};  // (SDL numbers it)
    switch_encode(&v[0], packet + 2);
    switch_encode(&v[1], packet + 6);
    bool same = !memcmp(packet, last, sizeof packet);
    if (now - last_ms < (same ? 50 : 8)) return true;
    memcpy(last, packet, sizeof last);
    last_ms = now;
    return SDL_SendGamepadEffect(pad, packet, sizeof packet);
}

enum { HD_NONE, HD_STEAM, HD_SWITCH };

static int hd_open(SDL_Gamepad *pad) {
    if (steam) { SDL_hid_close(steam); steam = NULL; }
    const char *off = getenv("SAVAGEWIND_VIBRATION_HD");
    if (!pad || (off && !strcmp(off, "0"))) return HD_NONE;
    uint16_t vendor = SDL_GetGamepadVendor(pad), product = SDL_GetGamepadProduct(pad);
    const char *path = SDL_GetGamepadPath(pad);
    if (vendor == 0x28de && (product == 0x1304 || product == 0x1305) && path && (steam = SDL_hid_open_path(path))) {
        LOG("[input] HD rumble: Steam Controller tones\n");
        return HD_STEAM;
    }
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        LOG("[input] HD rumble: Nintendo controller\n");
        return HD_SWITCH;
    default: return HD_NONE;
    }
}

void vibration_update(SDL_Gamepad *pad, bool focused) {
    static SDL_Gamepad *last_pad;
    static uint16_t last_low, last_high;
    static uint64_t last_ms;
    static bool failed;
    static int hd;
    if (!pad || !focused) vibration_clear();
    bool changed = pad != last_pad;
    if (changed) {
        last_pad = pad;
        hd = hd_open(pad);
        last_low = last_high = 0;
        last_ms = 0;
        failed = false;
    }
    if (!pad) return;
    pthread_mutex_lock(&lock);
    VibrationValue snap[2] = {values[0], values[1]};
    // Both vibration and shaping are opt-out: only an explicit =0 disables them.
    const char *setting = getenv("SAVAGEWIND_VIBRATION");
    bool on = focused && permitted && (!setting || strcmp(setting, "0") != 0);
    pthread_mutex_unlock(&lock);
    uint64_t now = SDL_GetTicks();
    if (hd) {
        static const VibrationValue quiet[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};
        const VibrationValue *v = on ? snap : quiet;
        if (hd == HD_STEAM ? steam_update(v, now) : switch_update(pad, v, now)) return;
        LOG("[input] HD rumble failed (%s): motors instead\n", SDL_GetError());
        hd = HD_NONE;
    }
    uint16_t low = 0, high = 0;
    if (on) {
        if (shape_on()) {
            shape_motors(snap, now, &low, &high);
        } else {
            low = (uint16_t)lroundf(fmaxf(snap[0].low_amplitude, snap[1].low_amplitude) * 65535);
            high = (uint16_t)lroundf(fmaxf(snap[0].high_amplitude, snap[1].high_amplitude) * 65535);
        }
    }
    // A short lease stops the motors if the event loop stalls; refresh unchanged effects while it is alive.
    if (!changed && low == last_low && high == last_high && (!(low || high) || now - last_ms < 50)) return;
    if (!SDL_RumbleGamepad(pad, low, high, 100) && !failed) {
        LOG("[input] controller rumble unavailable: %s\n", SDL_GetError());
        failed = true;
    }
    last_low = low;
    last_high = high;
    last_ms = now;
}
