// What runtime/hle/input.c has open and which does what, for the Controls tab (any thread: a copy made on the main thread).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64], guid[33];
    bool steam_virtual, has_sensors;
    bool input, sensors, haptics;     // the jobs it has now
} PadRow;

typedef struct {
    PadRow rows[8];
    int count;
    int pair;                // PAD_PAIR_*
    bool steam_launch;       // started through Steam
    bool virtual_live;       // Steam's virtual pad has reported input
    bool steam_controller;   // a Steam Controller 2026 is here
    char error[128];         // last failure, readable; "": none
} InputStatus;

void input_status(InputStatus *out);
void input_settings_changed(void);          // applied by the main thread's next turn
// Live values of the controller in use: held buttons (bit = PADB_*) and SDL's six axes
void input_live(uint32_t *buttons, int16_t axes[6]);

#ifdef __cplusplus
}
#endif
