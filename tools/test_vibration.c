// Synthetic HID -> SDL checks, with no game files or physical controller. make test-vibration
#include <assert.h>
#include <stdio.h>
#include "../runtime/hle/vibration.c"
#include "../runtime/services/hid_set.c"

Settings g_settings = {.vibration_intensity = 1};
static uint64_t now;
static unsigned calls;
static uint16_t motor_low, motor_high;
static uint32_t duration;
Uint64 SDL_GetTicks(void) { return now; }
bool SDL_RumbleGamepad(SDL_Gamepad *pad, Uint16 low, Uint16 high, Uint32 ms) {
    assert(pad);
    motor_low = low; motor_high = high; duration = ms; calls++;
    return true;
}
const char *SDL_GetError(void) { return "test"; }
// The controller behind the pad; by default one with plain motors.
static uint16_t pad_vendor, pad_product;
static SDL_GamepadType pad_type = SDL_GAMEPAD_TYPE_STANDARD;
static uint8_t sent[16];
static int sent_size, send_result = 1, stops;
Uint16 SDL_GetGamepadVendor(SDL_Gamepad *p) { (void)p; return pad_vendor; }
Uint16 SDL_GetGamepadProduct(SDL_Gamepad *p) { (void)p; return pad_product; }
SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *p) { (void)p; return pad_type; }
const char *SDL_GetGamepadPath(SDL_Gamepad *p) { (void)p; return "/dev/hidraw0"; }
SDL_hid_device *SDL_hid_open_path(const char *path) { (void)path; return (SDL_hid_device *)(uintptr_t)1; }
int SDL_hid_close(SDL_hid_device *d) { (void)d; return 0; }
int SDL_hid_write(SDL_hid_device *d, const unsigned char *data, size_t size) {
    (void)d; memcpy(sent, data, size); sent_size = (int)size;
    stops += data[0] == 0x82;
    return send_result ? (int)size : -1;
}
bool SDL_SendGamepadEffect(SDL_Gamepad *p, const void *data, int size) {
    (void)p; memcpy(sent, data, (size_t)size); sent_size = size;
    return send_result;
}

// Unrelated HID / settings dependencies. Keep the returned vibration object callable.
static Service returned_service;
Service *service_new(const char *name, ServiceHandler handler, void *state) {
    returned_service = (Service){.name = name, .handler = handler, .state = state};
    return &returned_service;
}
void ipc_return_object(IpcResp *resp, Service *s) { resp->objects[resp->nobjects++] = s; }
KSharedMem *kernel_create_shared_memory(uint64_t size, void (*on_map)(KSharedMem *), uint32_t *handle) {
    (void)size; (void)on_map; (void)handle; abort();
}
KEvent *kernel_create_event_pair(uint32_t *w, uint32_t *r) { (void)w; (void)r; abort(); }
const char *language_text_code(void) { return "en-US"; }

static IpcResp command(uint32_t cmd, const void *data, uint32_t size) {
    IpcReq req = {.cmd = cmd, .data = data, .data_size = size};
    IpcResp resp = {0};
    hid(NULL, &req, &resp);
    return resp;
}

int main(int argc, char **argv) {
    const uint32_t left = 0x00000003, right = 0x00010003;
    SDL_Gamepad *pad = (SDL_Gamepad *)(uintptr_t)1;
    vibration_reset();
    if (argc == 2) {
        // Run in separate processes to cover the cached shape switch and absent variables.
        VibrationValue pulse = {.1f, 100, 0, 320};
        vibration_send(left, &pulse);
        vibration_update(pad, true);
        assert(motor_high == 0 && duration == 100);
        if (!strcmp(argv[1], "off")) assert(motor_low == 0);
        else if (!strcmp(argv[1], "raw")) assert(motor_low == 6554);
        else { assert(!strcmp(argv[1], "shaped")); assert(motor_low > 16384); }
        printf("vibration: %s environment check passed\n", argv[1]);
        return 0;
    }
    IpcResp resp = command(205, NULL, 0);
    assert(resp.data_size == 1 && resp.data[0] == 1);
    resp = command(203, NULL, 0);
    assert(resp.nobjects == 1 && resp.objects[0]->handler == active_vibration_devices);
    IpcReq activate = {0}; IpcResp activated = {0};
    resp.objects[0]->handler(resp.objects[0], &activate, &activated);
    assert(activated.result == 0);
    resp = command(200, &right, sizeof right);
    assert(resp.data_size == 8 && resp.data[0] == 1 && resp.data[4] == 2);

    // Single-device command: the value starts at byte 4, not at the aligned user id at byte 24.
    uint8_t packet[32] = {0};
    VibrationValue value = {.25f, 160, .5f, 320};
    memcpy(packet, &left, 4); memcpy(packet + 4, &value, sizeof value);
    command(201, packet, sizeof packet);
    vibration_update(pad, true);
    assert(motor_low == 16384 && motor_high == 32768 && duration == 100);
    unsigned before = calls;
    now = 49; vibration_update(pad, true); assert(calls == before);
    now = 50; vibration_update(pad, true); assert(calls == before + 1);
    resp = command(202, packet, 16);
    assert(resp.data_size == sizeof value && memcmp(resp.data, &value, sizeof value) == 0);
    command(201, packet, 4);  // truncated: leave the current effect alone
    assert(vibration_get(left).low_amplitude == .25f);

    uint32_t handles[] = {left, right};
    VibrationValue batch[] = {{.75f, 100, .1f, 200}, {.2f, 180, .9f, 400}};
    IpcReq req = {.cmd = 206, .nx = 2, .x = {
        {(uintptr_t)handles, sizeof handles}, {(uintptr_t)batch, sizeof batch}}};
    resp = (IpcResp){0}; hid(NULL, &req, &resp);
    vibration_update(pad, true);
    assert(motor_low == 49151 && motor_high == 58982);
    // Bound iteration by the shorter buffer; an incomplete trailing value must not be read.
    batch[0].low_amplitude = .6f; req.x[1].size = sizeof(VibrationValue) + 1;
    hid(NULL, &req, &resp);
    assert(vibration_get(left).low_amplitude == .6f && vibration_get(right).high_amplitude == .9f);

    uint8_t allow = 0; command(204, &allow, 1);
    vibration_update(pad, true); assert(motor_low == 0 && motor_high == 0);
    command(201, packet, sizeof packet);
    assert(vibration_get(left).low_amplitude == 0);
    allow = 1; command(204, &allow, 1);
    vibration_update(pad, true); assert(motor_low == 0);  // do not resume an old effect

    value = (VibrationValue){NAN, 160, INFINITY, 320};
    vibration_send(left, &value); vibration_update(pad, true);
    assert(motor_low == 0 && motor_high == 0);
    value = (VibrationValue){-1, 160, 2, 320};
    vibration_send(left, &value); vibration_update(pad, true);
    assert(motor_low == 0 && motor_high == 65535);
    value = (VibrationValue){1, 160, 1, 320};
    vibration_send(0x00000103, &value);  // player 2: must not drive player 1
    assert(vibration_get(left).low_amplitude == 0);
    vibration_send(0, &value); assert(vibration_get(left).low_amplitude == 0);
    vibration_update(pad, false); assert(motor_high == 0);
    vibration_update(pad, true); assert(motor_high == 0);
    vibration_send(0x00002003, &value);  // handheld aliases the same physical controller
    vibration_update(pad, true); assert(motor_low == 65535);
    vibration_update(NULL, false);
    vibration_update(pad, true); assert(vibration_get(left).low_amplitude == 0);
    vibration_send(left, &value); command(210, NULL, 0);
    vibration_update(pad, true); assert(motor_low == 0);
    vibration_send(left, &value); vibration_reset();
    vibration_update(pad, true); assert(motor_low == 0 && vibration_permitted());
    puts("vibration: HID packets, batching, motors, focus, disconnect and reboot checks passed");

    // Steam Controller: one tone from the strongest band, at half its frequency, refreshed before it ends.
    pad_vendor = 0x28de; pad_product = 0x1304;
    SDL_Gamepad *steam_pad = (SDL_Gamepad *)(uintptr_t)2;
    now = 1000; calls = 0;
    value = (VibrationValue){.5f, 160, .25f, 320};
    vibration_send(left, &value); vibration_update(steam_pad, true);
    const uint8_t tone[10] = {0x83, 3, 1, 80, 0, 60, 0, 0, 0, 0};  // 0.75 ^ 0.8 = -2 dB, +3
    assert(sent_size == 10 && !memcmp(sent, tone, 10) && calls == 0);
    sent_size = 0; now = 1029; vibration_update(steam_pad, true); assert(sent_size == 0);
    now = 1030; vibration_update(steam_pad, true); assert(sent_size == 10);
    value = (VibrationValue){0, 160, .9f, 320};  // a change goes out at once, but not twice in 8 ms
    sent_size = 0; now = 1035; vibration_send(left, &value); vibration_update(steam_pad, true); assert(sent_size == 0);
    now = 1038; vibration_update(steam_pad, true); assert(sent_size == 10 && sent[3] == 160);
    value = (VibrationValue){0, 160, 0, 320};
    now = 1050; vibration_send(left, &value); vibration_update(steam_pad, true);
    assert(sent_size == 4 && sent[0] == 0x82 && sent[1] == 3);
    sent_size = 0; now = 1100; vibration_update(steam_pad, true); assert(sent_size == 0);  // once
    value = (VibrationValue){1, 160, 0, 320};
    send_result = 0; now = 1200; vibration_send(left, &value); vibration_update(steam_pad, true);
    assert(calls == 1 && motor_low == 65535);  // a failed write leaves the motors
    send_result = 1;

    // Nintendo: both bands of both sides as the rumble packet; neutral is the well-known 00 01 40 40.
    pad_vendor = 0x057e; pad_product = 0x2009; pad_type = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    SDL_Gamepad *pro = (SDL_Gamepad *)(uintptr_t)3;
    calls = 0; now = 2000;
    vibration_clear();
    vibration_update(pro, true);
    const uint8_t neutral[10] = {0x10, 0, 0, 1, 0x40, 0x40, 0, 1, 0x40, 0x40};
    assert(sent_size == 10 && !memcmp(sent, neutral, 10));
    value = (VibrationValue){1, 160, 1, 320};
    now = 2100; vibration_send(left, &value); vibration_update(pro, true);
    assert(sent[2] == 0 && sent[3] == 0xc9 && sent[4] == 0x40 && sent[5] == 0x72 && !memcmp(sent + 6, neutral + 6, 4));
    value = (VibrationValue){.4905f, 41, .4905f, 2000};  // the amplitude's odd code, the frequency limits
    now = 2200; vibration_send(right, &value); vibration_update(pro, true);
    assert(sent[6] == 0xfc && sent[7] == 0x87 && sent[8] == 0x81 && sent[9] == 0x61);
    sent_size = 0; now = 2249; vibration_update(pro, true); assert(sent_size == 0);
    now = 2250; vibration_update(pro, true); assert(sent_size == 10);
    send_result = 0; now = 2400; vibration_send(left, &value);  vibration_update(pro, true);
    assert(calls == 1);  // SDL refuses (a Joy-Con pair, a clone): the motors
    send_result = 1;
    puts("vibration: Steam Controller tones and Nintendo rumble packets passed");

    // One route at a time, bounds that hold whatever the tuning asks, settings. The Steam Controller again:
    pad_vendor = 0x28de; pad_product = 0x1304; pad_type = SDL_GAMEPAD_TYPE_STANDARD;
    SDL_Gamepad *a = (SDL_Gamepad *)(uintptr_t)4, *b = (SDL_Gamepad *)(uintptr_t)5;
    calls = 0; now = 5000; sent_size = 0;
    vibration_clear();
    vibration_update(a, true);
    assert(vibration_route() == VIBRATION_ROUTE_TONE);
    value = (VibrationValue){1, 160, 1, 320};
    vibration_send(left, &value); now = 5100; vibration_update(a, true);
    assert(sent_size == 10 && calls == 0);  // (never the motors beside the tone)
    assert(sent[0] == 0x83 && sent[1] == 3 && (int8_t)sent[2] <= 6 && sent[5] == 60 && sent[6] == 0);
    unsigned writes = 0;  // rapid updates: at most one write per 8 ms
    for (int i = 0; i < 80; i++) {
        value.high_amplitude = i & 1 ? 1 : .5f;
        now++; sent_size = 0;
        vibration_send(left, &value); vibration_update(a, true);
        writes += sent_size != 0;
    }
    assert(writes <= 11);
    value = (VibrationValue){1, 5000, 1, 5000};
    now += 20; vibration_send(left, &value); vibration_update(a, true);
    assert(sent[3] + (sent[4] << 8) <= 1000);
    // Handover: the tone stops on the old device before it's let go of, the new one starts clean
    now += 20; sent_size = 0;
    int stopped = stops;
    vibration_update(b, true);
    assert(stops == stopped + 1);
    pad_vendor = 0x045e; pad_product = 0x28e;
    SDL_Gamepad *c = (SDL_Gamepad *)(uintptr_t)6;
    sent_size = 0; now += 20; vibration_update(c, true);
    assert(sent_size == 4 && sent[0] == 0x82);
    assert(vibration_route() == VIBRATION_ROUTE_MOTORS);
    vibration_update(NULL, false);
    assert(vibration_route() == VIBRATION_ROUTE_NONE);
    // Settings: off stops it, intensity scales it, Standard leaves the tone, Native keeps only what was felt
    pad_vendor = 0; pad_product = 0;
    value = (VibrationValue){.5f, 160, 0, 320};
    vibration_update(c, true); vibration_send(left, &value);
    now += 100; vibration_update(c, true);
    uint16_t full = motor_low;
    g_settings.vibration_intensity = .5f;
    now += 100; vibration_update(c, true);
    assert(motor_low < full && motor_low);
    g_settings.vibration_intensity = 1;
    unsetenv("SAVAGEWIND_VIBRATION");  // (when set it beats the setting)
    g_settings.vibration_off = true;
    now += 100; vibration_update(c, true);
    assert(!motor_low);
    vibration_test();  // (the test pulse: heard whatever the game is doing, not past its time)
    now += 1; vibration_update(c, true);
    assert(motor_low);
    now += VIBRATION_TEST_MS; vibration_update(c, true);
    assert(!motor_low);
    g_settings.vibration_off = false;
    pad_vendor = 0x28de; pad_product = 0x1304;
    g_settings.vibration_quality = VIBRATION_STANDARD;
    now += 100; vibration_update(a, true);
    assert(vibration_route() == VIBRATION_ROUTE_MOTORS);
    g_settings.vibration_quality = VIBRATION_NATIVE;
    now += 100; vibration_update(a, true);
    assert(vibration_route() == VIBRATION_ROUTE_TONE);
    pad_vendor = 0x057e; pad_type = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO; pad_product = 0x2009;
    now += 100; vibration_update(pro, true);
    assert(vibration_route() == VIBRATION_ROUTE_MOTORS);
    g_settings.vibration_quality = VIBRATION_AUTO;
    now += 100; vibration_update(pro, true);
    assert(vibration_route() == VIBRATION_ROUTE_NINTENDO);
    // 207 to 209 change nothing: a session's end is 210
    vibration_send(left, &value);
    for (uint32_t cmd = 207; cmd <= 209; cmd++) command(cmd, NULL, 0);
    assert(vibration_get(left).low_amplitude == .5f);
    command(210, NULL, 0);
    assert(vibration_get(left).low_amplitude == 0);
    puts("vibration: routes, bounds, handover and settings passed");
    return 0;
}
