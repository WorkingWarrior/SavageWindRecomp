// The sensor stream through runtime/hle/motion.c to the game's six-axis samples, synthetic readings as SDL delivers them:
// a stale first reading, a stream that stops and comes back, 4 / 8 ms jitter, SDL <-> DSU, focus, recalibration, what each
// game reads. No game files, no controller. make test-motion-stream
#include <assert.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

static int fake_off, fake_on;
static bool fake_set(SDL_Gamepad *p, SDL_SensorType t, bool on) { (void)p; (void)t; on ? fake_on++ : fake_off++; return true; }
static bool fake_has(SDL_Gamepad *p, SDL_SensorType t) { (void)p; (void)t; return true; }
static float fake_rate(SDL_Gamepad *p, SDL_SensorType t) { (void)p; (void)t; return 248; }
static const char *fake_name(SDL_Gamepad *p) { (void)p; return "test pad"; }
#define SDL_SetGamepadSensorEnabled fake_set
#define SDL_GamepadHasSensor fake_has
#define SDL_GetGamepadSensorDataRate fake_rate
#define SDL_GetGamepadName fake_name
#include "../runtime/hle/motion.c"

Settings g_settings = {.motion_sens = 1};
static uint64_t clock_ns = 1000000000;
uint64_t plat_now_ns(void) { return clock_ns; }
bool pc_gyro_keyboard_active(void) { return false; }
#ifdef __linux__
bool dsu_init(DsuClient *c, const char *e, uint32_t id) { (void)e; (void)id; memset(c, 0, sizeof *c); c->fd = -1; return true; }
void dsu_poll(DsuClient *c, uint64_t now, DsuSampleFn fn, void *u) { (void)c; (void)now; (void)fn; (void)u; }
void dsu_close(DsuClient *c) { c->fd = -1; }
#endif

static SDL_Gamepad *const pad_a = (SDL_Gamepad *)8;
static uint64_t sensor_ns = 5000000000;

static void reading(SDL_SensorType type, uint64_t ts, float x, float y, float z) {
    SDL_Event e = {0};
    e.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
    e.gsensor.sensor = type;
    e.gsensor.data[0] = x, e.gsensor.data[1] = y, e.gsensor.data[2] = z;
    e.gsensor.sensor_timestamp = ts;
    e.gsensor.timestamp = clock_ns;
    motion_sensor_event(&e);
}

// One report: gyroscope (rad/s) and accelerometer (SDL's axes; flat on a table, its face up, is +Y)
static void report(uint64_t ts, double yaw_rate, float ax, float ay, float az) {
    reading(SDL_SENSOR_GYRO, ts, 0, (float)yaw_rate, 0);
    reading(SDL_SENSOR_ACCEL, ts, ax, ay, az);
}

static void stream(int n, uint64_t step_ns, double yaw_rate) {
    for (int i = 0; i < n; i++) {
        clock_ns += step_ns, sensor_ns += step_ns;
        report(sensor_ns, yaw_rate, 0, 9.80665f, 0);
        motion_watch(true);
    }
}

static const SixAxisState *newest(void) { update(clock_ns); return &s.ring[s.head]; }

static double game_yaw(void) { update(clock_ns); return s.yaw * 180 / PI; }

static void start(void) {
    motion_reboot_reset();
    motion_pad_closed();
    memset(&g, 0, sizeof g);
    g.q.w = 1, g.acc[2] = -1;
    memset(&hs, 0, sizeof hs);
    motion_pad_opened(pad_a);
    fake_on = fake_off = 0;
    motion_poll();
}

static void pitch_deg_near(double want, double tol) {
    double yaw, pitch, roll;
    Mat m;
    quat_mat(g.q, m);
    euler_of(m, &yaw, &pitch, &roll);
    if (fabs(pitch * 180 / PI - want) > tol) fprintf(stderr, "pitch %.2f, wanted %.2f\n", pitch * 180 / PI, want);
    assert(fabs(pitch * 180 / PI - want) <= tol);
}

static void stale_first_sample(void) {
    start();
    // Flat, but the first report still holds the pose from when the IMU was last on: standing on its edge, 65 s ago
    clock_ns += 4000000;
    report(sensor_ns, 0, 0, 0, 9.80665f);
    assert(!g.started);
    clock_ns += 4000000, sensor_ns += 65000000000ull;
    report(sensor_ns, 0, 0, 9.80665f, 0);
    assert(!g.started);  // (the jump: not a step yet)
    stream(2, 4000000, 0);
    assert(g.started);
    pitch_deg_near(0, 0.01);
    assert(!g.gyro_ts || hs.jumped == 0);
    puts("stale first reading: never seeds the orientation PASS");

    // A controller whose clock stands still is still taken, after a few readings
    start();
    for (int i = 0; i < WARM_READINGS + 2; i++) { clock_ns += 4000000; report(sensor_ns, 0, 0, 9.80665f, 0); }
    assert(g.started);
    puts("a clock that stands still: readings count after a few PASS");
}

static void stall_and_recovery(void) {
    start();
    stream(100, 4000000, 0);
    double before = game_yaw();
    stream(250, 4000000, 1.0);  // (a turn, one second)
    double turned = game_yaw() - before;
    assert(fabs(fabs(turned) - 57.3) < 2);
    // Steam turns the IMU off
    clock_ns += 200000000;
    motion_watch(true);
    assert(!watch.stalled && !fake_on);
    clock_ns += 110000000;
    motion_watch(true);
    assert(watch.stalled && fake_off == 2 && fake_on == 2);
    MotionStatus st;
    motion_status(&st);
    assert(st.stalled && st.sensors);
    double held = game_yaw();
    assert(g.sensors && g.started);  // (nothing else took over: no DSU is live)
    // Rate limit: not again within a second
    for (int i = 0; i < 90; i++) { clock_ns += 10000000; motion_watch(true); }
    assert(fake_on == 2);
    clock_ns += 110000000;
    motion_watch(true);
    assert(fake_on == 4);
    // ... slower after a few tries
    for (int i = 0; i < RETRY_FEW; i++) { clock_ns += RETRY_NS + 1; motion_watch(true); }
    int tried = fake_on;
    clock_ns += RETRY_NS + 1;
    motion_watch(true);
    assert(fake_on == tried);
    clock_ns += RETRY_SLOW_NS;
    motion_watch(true);
    assert(fake_on == tried + 2);
    // Back: its first report carries the whole gap on the sensor clock. No snap, no integration of the gap
    clock_ns += 18000000, sensor_ns += 3320000000ull;
    report(sensor_ns, 2.0, 0, 9.80665f, 0);
    assert(!watch.stalled);
    stream(1, 4000000, 0);
    double after = game_yaw();
    assert(fabs(wrap((after - held) * PI / 180)) * 180 / PI < 1);
    assert(fabs(newest()->gyro[2]) < 0.1);
    assert(hs.jumped == 0);
    motion_status(&st);
    assert(!st.stalled && st.recovered == 1);
    puts("stream stops: found at 300 ms, retried with limits, resumes without a jump PASS");
}

static void jitter(void) {
    start();
    stream(100, 4000000, 0);
    double y0 = game_yaw(), done = 0;
    uint64_t t0 = clock_ns;
    for (int i = 0; i < 400; i++) {
        uint64_t step = i % 7 == 3 ? 8000000 : 4000000;
        clock_ns += step, sensor_ns += step;
        report(sensor_ns, 1.0, 0, 9.80665f, 0);
        if (i % 4 == 0) update(clock_ns);
    }
    done = fabs(wrap((game_yaw() - y0) * PI / 180));
    double want = (double)(clock_ns - t0) / 1e9;
    assert(fabs(done - want) / want < 0.02);
    assert(hs.bad_run == 0);
    puts("4 / 8 ms readings: the turn is right to 2% PASS");
}

#ifdef __linux__
static void source_switch(void) {
    start();
    stream(100, 4000000, 0);
    double y0 = game_yaw();
    clock_ns += DSU_TIMEOUT_NS + 1000000;
    motion_poll();
    assert(backend.source == 1);  // (no DSU service: stays, holding)
    backend.dsu.fresh = true;
    DsuPacket p = {0};
    p.acc[2] = -1;
    p.timestamp = 1000;
    dsu_sample(&p, 1, NULL);
    assert(backend.source == 2);
    for (int i = 0; i < 50; i++) { clock_ns += 4000000; p.timestamp += 4000; dsu_sample(&p, 1, NULL); update(clock_ns); }
    double y1 = game_yaw();
    assert(fabs(wrap((y1 - y0) * PI / 180)) * 180 / PI < 1);
    for (int i = 0; i < 3; i++) assert(fabs(newest()->gyro[i]) < 0.1);
    stream(20, 4000000, 0);
    assert(backend.source == 1);
    double y2 = game_yaw();
    assert(fabs(wrap((y2 - y1) * PI / 180)) * 180 / PI < 1);
    for (int i = 0; i < 3; i++) assert(fabs(newest()->gyro[i]) < 0.1);
    // A setting holds the source
    g_settings.motion_source = MOTION_SOURCE_DSU;
    stream(5, 4000000, 0);
    assert(backend.source == 1 && !native_fresh(clock_ns + DSU_TIMEOUT_NS + 1));
    g_settings.motion_source = MOTION_SOURCE_AUTO;
    backend.dsu.fresh = false;
    puts("SDL <-> DSU: no jump, no silent fall to a dead DSU PASS");
}
#endif

static void refocus(void) {
    start();
    stream(100, 4000000, 0);
    double y0 = game_yaw();
    motion_watch(false);
    clock_ns += 8000000000ull, sensor_ns += 8000000000ull;  // (readings dropped meanwhile, runtime/hle/input.c)
    motion_watch(false);
    assert(!watch.stalled);
    motion_watch(true);
    assert(!fake_on);
    report(sensor_ns, 3.0, 0, 9.80665f, 0);
    stream(2, 4000000, 0);
    assert(fabs(wrap((game_yaw() - y0) * PI / 180)) * 180 / PI < 1);
    assert(hs.jumped == 0);
    puts("focus back: no jump, the gap isn't a stall PASS");
}

static void recalibrate(void) {
    start();
    // A bias under a hand that isn't quite still: too noisy to be learnt by itself
    for (int i = 0; i < 250; i++) {
        clock_ns += 4000000, sensor_ns += 4000000;
        reading(SDL_SENSOR_GYRO, sensor_ns, 0, (float)(0.2 + (i & 1 ? 0.05 : -0.05)), 0);
        reading(SDL_SENSOR_ACCEL, sensor_ns, 0, 9.80665f, 0);
    }
    assert(fabs(g.bias[2] - 0.2) > 0.05);
    motion_recalibrate();
    MotionStatus st;
    motion_status(&st);
    assert(st.calibrating);
    for (int i = 0; i < 400; i++) {
        clock_ns += 4000000, sensor_ns += 4000000;
        reading(SDL_SENSOR_GYRO, sensor_ns, 0, (float)(0.2 + (i & 1 ? 0.05 : -0.05)), 0);
        reading(SDL_SENSOR_ACCEL, sensor_ns, 0, 9.80665f, 0);
    }
    assert(fabs(g.bias[2] - 0.2) < 0.03);
    motion_status(&st);
    assert(!st.calibrating);
    puts("recalibration: the rest learnt, then back to normal PASS");
}

static void game_reads(void) {
    // BOTW asks for the 16 newest states of each handle every frame; TOTK's count comes from its own code. Both: newest
    // first, a sample per 5 ms, no repeats when the game reads faster or slower than the sensor.
    static const int counts[] = {16, 1, 32};
    uint32_t handle = 3;
    for (unsigned k = 0; k < 3; k++) {
        start();
        stream(50, 4000000, 1.0);
        SixAxisState out[16];
        Cpu c = {0};
        c.x[0] = (uintptr_t)out, c.x[1] = (uint64_t)counts[k], c.x[2] = (uintptr_t)&handle;
        hid_sixaxis_states(&c);
        clock_ns += 33000000;
        c.x[0] = (uintptr_t)out, c.x[1] = (uint64_t)counts[k], c.x[2] = (uintptr_t)&handle;
        hid_sixaxis_states(&c);
        int n = (int)c.x[0];
        assert(n == (counts[k] > 16 ? 16 : counts[k]));
        for (int i = 1; i < n; i++) {
            assert(out[i - 1].sampling_number == out[i].sampling_number + 1);
            assert(out[i].delta_time == (int64_t)SAMPLE_NS);
        }
        assert(out[0].attributes & 1);
    }
    uint32_t other = 3 | 1 << 8;
    SixAxisState out[2];
    Cpu c = {0};
    c.x[0] = (uintptr_t)out, c.x[1] = 2, c.x[2] = (uintptr_t)&other;
    hid_sixaxis_states(&c);
    assert(!out[0].attributes);
    puts("what the games read: newest first, numbered, other players empty PASS");
}

int main(void) {
    stale_first_sample();
    stall_and_recovery();
    jitter();
#ifdef __linux__
    source_switch();
#endif
    refocus();
    recalibrate();
    game_reads();
    puts("motion stream checks passed");
}
