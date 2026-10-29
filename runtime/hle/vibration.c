// The SDK mixes its vibration nodes itself, then sends HID commands 201 / 206. SDL's two motors cannot reproduce
// HD rumble frequencies: use the strongest low / high band across the two device values instead.
#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
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
    if (i < 0) return;
    pthread_mutex_lock(&lock);
    if (permitted) {
        VibrationValue previous = values[i];
        values[i] = *value;
        values[i].low_amplitude = amplitude(value->low_amplitude);
        values[i].high_amplitude = amplitude(value->high_amplitude);
        if ((previous.low_amplitude != values[i].low_amplitude || previous.high_amplitude != values[i].high_amplitude) &&
            getenv("SAVAGEWIND_VIBRATION"))
            LOG("[vibration] handle %#x low %.3f@%.0fHz high %.3f@%.0fHz\n", handle,
    values[i].low_amplitude, values[i].low_frequency, values[i].high_amplitude, values[i].high_frequency);
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
    if (on < 0) { const char *s = getenv("SAVAGEWIND_VIBRATION_SHAPE"); on = s && *s && *s != '0'; }
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

void vibration_update(SDL_Gamepad *pad, bool focused) {
    static SDL_Gamepad *last_pad;
    static uint16_t last_low, last_high;
    static uint64_t last_ms;
    static bool failed;
    if (!pad || !focused) vibration_clear();
    bool changed = pad != last_pad;
    if (changed) {
        last_pad = pad;
        last_low = last_high = 0;
        last_ms = 0;
        failed = false;
    }
    if (!pad) return;
    pthread_mutex_lock(&lock);
    VibrationValue snap[2] = {values[0], values[1]};
    bool on = focused && permitted;
    pthread_mutex_unlock(&lock);
    uint64_t now = SDL_GetTicks();
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
