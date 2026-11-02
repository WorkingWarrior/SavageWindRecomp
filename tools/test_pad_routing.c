// Several controllers through runtime/hle/input.c: no game files, no controller.
#include <assert.h>
#include <stdio.h>
#include "../runtime/hle.h"
// PE/COFF retains `used` registration entries. This harness calls the HID reader directly.
#undef HLE_FUNC
#define HLE_FUNC(sym, fn) static void fn(Cpu *c)
#undef RT_ON_RESET
#define RT_ON_RESET(fn) static void fn(void)
#include "../runtime/hle/input.c"
#include "test_pointer_disabled.h"

Settings g_settings;
int16_t g_bind_key[BIND_KEY_COUNT], g_bind_pad[BIND_KEY_COUNT];
uint64_t g_watch_lo, g_watch_hi;
static SDL_Gamepad *sensor_on, *vibrating_on;
static int opens, closes;

bool menu_input_captured(void) { return false; }
bool menu_cursor_free(void) { return false; }
void motion_tilt_held(bool held) { (void)held; }
void motion_mouse(float x, float y) { (void)x; (void)y; }
void motion_pad_opened(SDL_Gamepad *p) { sensor_on = p; opens++; }
void motion_pad_closed(void) { sensor_on = NULL; closes++; }
void motion_sensor_event(const SDL_Event *e) { (void)e; }
void vibration_update(SDL_Gamepad *p, bool focused) { vibrating_on = focused ? p : NULL; }
uint64_t renderer_present_count(void) { return 0; }
uint64_t plat_now_ns(void) { return 0; }

// PE linkers also keep exported helpers from input.c. These paths are outside this harness.
static _Noreturn void unexpected_helper(void) { fputs("unexpected runtime helper\n", stderr); abort(); }
Cpu *kernel_current_cpu(void) { unexpected_helper(); }
void watch_arm(void) { unexpected_helper(); }
int bind_pad_from_name(const char *name) { (void)name; unexpected_helper(); }
int bind_slot_from_name(const char *name) { (void)name; unexpected_helper(); }
bool bind_capture_event(const SDL_Event *e) { (void)e; unexpected_helper(); }
void bind_capture_begin(int dev, int slot) { (void)dev; (void)slot; unexpected_helper(); }
void bind_reset(int dev) { (void)dev; unexpected_helper(); }
void motion_script(float p, float r, float ps, float ys) { (void)p; (void)r; (void)ps; (void)ys; unexpected_helper(); }
// (the control endpoint, the virtual gyro and the DSU poll: off here)
bool control_enabled(void) { return false; }
bool control_fly_toggle(void) { unexpected_helper(); }
void control_fly_wheel(float notches) { (void)notches; unexpected_helper(); }
void control_input_frame(uint64_t frame) { (void)frame; }
void control_input_apply(uint64_t *b, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) { (void)b; (void)lx; (void)ly; (void)rx; (void)ry; }
bool pc_gyro_keyboard_active(void) { return false; }
void pc_gyro_scene_tick(void) {}
bool motion_has_sensors(void) { return false; }
void motion_keyboard(int p, int r, int y, bool c) { (void)p; (void)r; (void)y; (void)c; }
void motion_mouse_yaw(float s) { (void)s; unexpected_helper(); }
void motion_dualstick(float lx, float ly, float rx, float ry, double dt, bool reset) { (void)lx; (void)ly; (void)rx; (void)ry; (void)dt; (void)reset; unexpected_helper(); }
void motion_poll(void) {}
void motion_watch(bool focused) { (void)focused; }
bool framepace_title_ui(void) { return false; }
bool framepace_menu_ui(void) { return false; }


// Through input.c as the window loop drives it, with SDL's own virtual joysticks standing for the devices. Steam's
// handle can't be faked on one: the harness writes it where input.c keeps it.
static SDL_JoystickID attach(uint16_t vendor, uint16_t product, const char *name) {
    SDL_VirtualJoystickDesc d = {0};
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.vendor_id = vendor, d.product_id = product;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    d.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    d.name = name;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
    assert(id);
    return id;
}

static void pump(void) {
    SDL_UpdateJoysticks();
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (!input_user_blocked(&e)) input_handle_event(&e);
}

static void make_steam(SDL_JoystickID id, uint64_t handle) {
    int i = pad_slot(id);
    assert(i >= 0);
    open_pads[i].e.steam_handle = handle;
    routes_apply();
}

static void press(SDL_Joystick *j, int button, bool down) {
    assert(SDL_SetJoystickVirtualButton(j, button, down));
    pump();
}

static SDL_Gamepad *gamepad(SDL_JoystickID id) { int i = pad_slot(id); return i < 0 ? NULL : open_pads[i].g; }

int main(void) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");  // (a controller on the test machine stays out of it)
    SDL_SetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC, "0");
    assert(SDL_Init(SDL_INIT_GAMEPAD));
    for (int i = 0; i < BIND_KEY_COUNT; i++) g_bind_pad[i] = -1;
    g_bind_pad[BIND_A] = SDL_GAMEPAD_BUTTON_EAST;
    g_settings.stick_deadzone[0] = g_settings.stick_deadzone[1] = 0.1f;
    const uint64_t a = 1ull << BTN_A;
    win_focused = true;

    SDL_Event first = {0};
    first.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    input_handle_event(&first);
    assert(!pad && !sensor_pad && !haptic_pad);

    // physical + Steam's virtual pad: one controller. Silent virtual pad: all real
    SDL_JoystickID real = attach(0x28de, 0x1304, "Steam Controller"), virt = attach(0x28de, 0x1302, "Steam Controller");
    pump();
    assert(npads == 2);
    make_steam(virt, 7);
    SDL_Joystick *jr = SDL_OpenJoystick(real), *jv = SDL_OpenJoystick(virt);
    assert(jr && jv);
    assert(routes.pair == PAD_PAIR_OK);
    assert(pad == gamepad(real) && sensor_pad == gamepad(real) && haptic_pad == gamepad(real));
    press(jr, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad_buttons == a);
    press(jr, SDL_GAMEPAD_BUTTON_EAST, false);
    assert(pad_buttons == 0);

    // A hand on the grips (touch sensors) and Steam's own buttons: no activity, no prompts flip
    use_device(DEVICE_KEYBOARD, "test");
    for (int b = SDL_GAMEPAD_BUTTON_MISC3; b <= SDL_GAMEPAD_BUTTON_MISC6; b++) { press(jr, b, true); press(jr, b, false); }
    press(jr, SDL_GAMEPAD_BUTTON_GUIDE, true); press(jr, SDL_GAMEPAD_BUTTON_GUIDE, false);
    press(jr, SDL_GAMEPAD_BUTTON_MISC1, true); press(jr, SDL_GAMEPAD_BUTTON_MISC1, false);
    assert(last_device == DEVICE_KEYBOARD);
    press(jr, SDL_GAMEPAD_BUTTON_SOUTH, true); press(jr, SDL_GAMEPAD_BUTTON_SOUTH, false);
    assert(last_device == DEVICE_PAD);

    // The virtual pad reports input: buttons move to it, with a button held on the real one let go, motion and vibration stay
    press(jr, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad_buttons == a);
    assert(SDL_SetJoystickVirtualButton(jv, SDL_GAMEPAD_BUTTON_SOUTH, true));
    pump();
    assert(virtual_live && pad == gamepad(virt) && sensor_pad == gamepad(real) && haptic_pad == gamepad(real));
    assert(pad_buttons == 0);  // (not read from the real one any more: no double input, nothing stuck)
    press(jv, SDL_GAMEPAD_BUTTON_SOUTH, false);
    press(jr, SDL_GAMEPAD_BUTTON_EAST, false);
    press(jv, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad_buttons == a);
    press(jv, SDL_GAMEPAD_BUTTON_EAST, false);

    // Settings: physical, at once
    g_settings.steam_input = STEAM_INPUT_PHYSICAL;
    routes_apply();
    assert(pad == gamepad(real));
    g_settings.steam_input = STEAM_INPUT_AUTO;
    routes_apply();
    assert(pad == gamepad(virt));

    // Steam's pad goes while a button is held on it: released, the real one takes over
    press(jv, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad_buttons == a);
    SDL_CloseJoystick(jv);
    assert(SDL_DetachVirtualJoystick(virt));
    pump();
    assert(pad_buttons == 0 && pad == gamepad(real) && !virtual_live && routes.pair == PAD_PAIR_NONE);

    // Another controller used: it takes over everything, the first one's held button goes
    SDL_JoystickID xbox = attach(0x045e, 0x028e, "Xbox");
    pump();
    SDL_Joystick *jx = SDL_OpenJoystick(xbox);
    assert(pad == gamepad(real));
    press(jr, SDL_GAMEPAD_BUTTON_EAST, true);
    int opened = opens;
    press(jx, SDL_GAMEPAD_BUTTON_SOUTH, true);
    assert(pad == gamepad(xbox) && sensor_pad == gamepad(xbox) && haptic_pad == gamepad(xbox) && opens == opened + 1);
    assert(pad_buttons == 0);
    press(jx, SDL_GAMEPAD_BUTTON_SOUTH, false);
    press(jr, SDL_GAMEPAD_BUTTON_EAST, false);

    // A choice in settings holds whatever else is touched
    snprintf(g_settings.pad_preferred, sizeof g_settings.pad_preferred, "%s", open_pads[pad_slot(xbox)].guid);
    routes_apply();
    press(jr, SDL_GAMEPAD_BUTTON_SOUTH, true); press(jr, SDL_GAMEPAD_BUTTON_SOUTH, false);
    assert(pad == gamepad(xbox));
    g_settings.pad_preferred[0] = 0;
    routes_apply();

    // The active one removed in mid-play, button held: released at once, the other takes over, its sensors on
    press(jx, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad_buttons == a && pad == gamepad(xbox));
    SDL_CloseJoystick(jx);
    assert(SDL_DetachVirtualJoystick(xbox));
    pump();
    assert(pad == gamepad(real) && sensor_pad == gamepad(real) && haptic_pad == gamepad(real) && pad_buttons == 0);

    // The last one goes and comes back (a Puck's controller does that by itself): nothing held, a new SDL id, working
    SDL_CloseJoystick(jr);
    assert(SDL_DetachVirtualJoystick(real));
    pump();
    assert(!pad && !sensor_pad && !haptic_pad && !npads && pad_buttons == 0);
    real = attach(0x28de, 0x1304, "Steam Controller");
    pump();
    jr = SDL_OpenJoystick(real);
    assert(pad == gamepad(real) && sensor_pad == pad);
    press(jr, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad_buttons == a);
    press(jr, SDL_GAMEPAD_BUTTON_EAST, false);

    // Two identical controllers and a virtual pad: both real ones usable by last use, nothing paired
    SDL_JoystickID second = attach(0x28de, 0x1304, "Steam Controller"), v2 = attach(0x28de, 0x1302, "Steam Controller");
    pump();
    make_steam(v2, 9);
    SDL_Joystick *j2 = SDL_OpenJoystick(second);
    assert(routes.pair == PAD_PAIR_AMBIGUOUS && pad == gamepad(real));
    press(j2, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad == gamepad(second) && sensor_pad == gamepad(second) && pad_buttons == a);
    press(j2, SDL_GAMEPAD_BUTTON_EAST, false);

    SDL_CloseJoystick(j2);
    SDL_CloseJoystick(jr);
    SDL_Quit();
    puts("pad routing checks passed");
}
