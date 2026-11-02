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
static SDL_Event menu_seen[64];  // what reaches the menus: input_user_blocked is the gate, as in the window loop
static int menu_events;

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
        if (!input_user_blocked(&e)) {
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && menu_events < 64) menu_seen[menu_events++] = e;
            input_handle_event(&e);
        }
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

// (SDL holds a quick Guide release back 250 ms)
static void guide(SDL_Joystick *j) {
    press(j, SDL_GAMEPAD_BUTTON_GUIDE, true);
    press(j, SDL_GAMEPAD_BUTTON_GUIDE, false);
    SDL_Delay(300);
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
    guide(jr);
    press(jr, SDL_GAMEPAD_BUTTON_MISC1, true); press(jr, SDL_GAMEPAD_BUTTON_MISC1, false);
    assert(last_device == DEVICE_KEYBOARD);
    press(jr, SDL_GAMEPAD_BUTTON_SOUTH, true); press(jr, SDL_GAMEPAD_BUTTON_SOUTH, false);
    assert(last_device == DEVICE_PAD);

    // The touch sensors don't get past the gate: no menu, pointer or rebind sees them
    menu_events = 0;
    press(jr, SDL_GAMEPAD_BUTTON_MISC5, true); press(jr, SDL_GAMEPAD_BUTTON_MISC5, false);
    assert(!menu_events);
    // Started through Steam, the Steam button is Steam's: our menu doesn't open with it. Outside Steam it does, as ever
    guide(jr);
    assert(menu_events == 1);
    menu_events = 0;
    setenv("SteamAppId", "1", 1);
    guide(jr);
    assert(!menu_events);
    unsetenv("SteamAppId");

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

    // One press, both halves report it: the menus see it once, from the half that feeds the game
    menu_events = 0;
    assert(SDL_SetJoystickVirtualButton(jr, SDL_GAMEPAD_BUTTON_SOUTH, true));
    assert(SDL_SetJoystickVirtualButton(jv, SDL_GAMEPAD_BUTTON_SOUTH, true));
    pump();
    assert(menu_events == 1 && menu_seen[0].gbutton.which == virt);
    press(jr, SDL_GAMEPAD_BUTTON_SOUTH, false);
    press(jv, SDL_GAMEPAD_BUTTON_SOUTH, false);

    // The virtual pad goes quiet while the real one is pressed: back to the real one after a few presses, and the time
    for (int i = 0; i < VIRTUAL_SILENT; i++) { press(jr, SDL_GAMEPAD_BUTTON_NORTH, true); press(jr, SDL_GAMEPAD_BUTTON_NORTH, false); }
    virtual_check();
    assert(virtual_live);  // (a moment ago: its reply may be on the way)
    unmatched_ms -= VIRTUAL_REPLY_MS + 1;
    virtual_check();
    assert(!virtual_live && pad == gamepad(real));
    press(jv, SDL_GAMEPAD_BUTTON_SOUTH, true); press(jv, SDL_GAMEPAD_BUTTON_SOUTH, false);  // (and it can come back)
    assert(virtual_live && pad == gamepad(virt));

    // A menu takes the virtual pad's events before input_handle_event sees them: it still counts as alive
    for (int i = 0; i < VIRTUAL_SILENT + 2; i++) {
        unmatched++, unmatched_ms = SDL_GetTicks() - VIRTUAL_REPLY_MS - 1;
        SDL_Event ev = {0};
        ev.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN, ev.gbutton.which = virt, ev.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH, ev.gbutton.down = true;
        assert(!input_user_blocked(&ev));  // (and the menu keeps it: input_handle_event never runs)
        assert(!unmatched);
    }
    // One tilt is not three presses
    assert(SDL_SetJoystickVirtualAxis(jr, SDL_GAMEPAD_AXIS_LEFTX, 30000));
    pump();
    assert(SDL_SetJoystickVirtualAxis(jr, SDL_GAMEPAD_AXIS_LEFTX, 32000));
    pump();
    assert(SDL_SetJoystickVirtualAxis(jr, SDL_GAMEPAD_AXIS_LEFTX, 0));
    pump();
    assert(!unmatched);

    // A press that went to the menus from the input half, which then changes: they still see it end
    g_settings.steam_input = STEAM_INPUT_PHYSICAL;
    routes_apply();
    assert(pad == gamepad(real));
    press(jr, SDL_GAMEPAD_BUTTON_BACK, true);
    g_settings.steam_input = STEAM_INPUT_AUTO;
    routes_apply();
    assert(pad == gamepad(virt));
    SDL_Event up = {0};
    up.type = SDL_EVENT_GAMEPAD_BUTTON_UP, up.gbutton.which = real, up.gbutton.button = SDL_GAMEPAD_BUTTON_BACK;
    assert(!input_user_blocked(&up));
    assert(input_user_blocked(&up));  // (once)
    press(jr, SDL_GAMEPAD_BUTTON_BACK, false);

    // Forced to a silent virtual pad: the game listens to it, but the menus stay reachable from the controller
    virtual_live = false;
    g_settings.steam_input = STEAM_INPUT_STEAM;
    routes_apply();
    assert(pad == gamepad(virt));
    menu_events = 0;
    press(jr, SDL_GAMEPAD_BUTTON_SOUTH, true); press(jr, SDL_GAMEPAD_BUTTON_SOUTH, false);
    assert(menu_events == 1 && pad_buttons == 0);
    virtual_live = true;
    g_settings.steam_input = STEAM_INPUT_AUTO;
    routes_apply();

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
    press(jx, SDL_GAMEPAD_BUTTON_NORTH, true);  // (used a moment ago: a remapper's copy reporting along, no swap)
    press(jx, SDL_GAMEPAD_BUTTON_NORTH, false);
    assert(pad == gamepad(real) && opens == opened);
    busy_ms = 0;
    press(jx, SDL_GAMEPAD_BUTTON_SOUTH, true);
    assert(pad == gamepad(xbox) && sensor_pad == gamepad(xbox) && haptic_pad == gamepad(xbox) && opens == opened + 1);
    assert(pad_buttons == 0);
    press(jx, SDL_GAMEPAD_BUTTON_SOUTH, false);
    press(jr, SDL_GAMEPAD_BUTTON_EAST, false);

    // A choice in settings holds whatever else is touched
    snprintf(g_settings.pad_preferred, sizeof g_settings.pad_preferred, "%s", open_pads[pad_slot(xbox)].guid);
    routes_apply();
    busy_ms = 0;
    press(jr, SDL_GAMEPAD_BUTTON_SOUTH, true); press(jr, SDL_GAMEPAD_BUTTON_SOUTH, false);
    assert(pad == gamepad(xbox));
    g_settings.pad_preferred[0] = 0;
    routes_apply();

    // The active one removed in mid-play, button held: released at once, the other takes over, its sensors on
    busy_ms = 0;
    press(jx, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad_buttons == a && pad == gamepad(xbox));
    SDL_CloseJoystick(jx);
    assert(SDL_DetachVirtualJoystick(xbox));
    pump();
    assert(pad == gamepad(real) && sensor_pad == gamepad(real) && haptic_pad == gamepad(real) && pad_buttons == 0);

    // The real half going first must not leave the next pair with a virtual pad taken for alive
    SDL_JoystickID v3 = attach(0x28de, 0x1302, "Steam Controller");
    pump();
    make_steam(v3, 8);
    SDL_Joystick *j3 = SDL_OpenJoystick(v3);
    press(j3, SDL_GAMEPAD_BUTTON_SOUTH, true); press(j3, SDL_GAMEPAD_BUTTON_SOUTH, false);
    assert(virtual_live);
    SDL_CloseJoystick(jr);
    assert(SDL_DetachVirtualJoystick(real));
    pump();
    SDL_CloseJoystick(j3);
    assert(SDL_DetachVirtualJoystick(v3));
    pump();
    assert(!virtual_live && !npads);
    real = attach(0x28de, 0x1304, "Steam Controller");
    pump();
    jr = SDL_OpenJoystick(real);

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
    busy_ms = 0;
    press(j2, SDL_GAMEPAD_BUTTON_EAST, true);
    assert(pad == gamepad(second) && sensor_pad == gamepad(second) && pad_buttons == a);
    press(j2, SDL_GAMEPAD_BUTTON_EAST, false);

    SDL_CloseJoystick(j2);
    SDL_CloseJoystick(jr);
    SDL_Quit();
    puts("pad routing checks passed");
}
