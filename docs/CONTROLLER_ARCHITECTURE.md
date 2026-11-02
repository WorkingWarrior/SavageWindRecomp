# Controllers: architecture

Findings behind this are in `docs/CONTROLLER_IMPLEMENTATION_AUDIT.md`.

## Device ownership

`runtime/hle/input.c` keeps every SDL gamepad open, because using one has to be noticed. `runtime/pad_policy.h` decides who
does what from plain data, with no SDL, so all of it is tested.

Three pointers carry the result: `pad` (buttons and sticks), `sensor_pad` (motion), `haptic_pad` (vibration). `routes_apply`
is the only writer, on the main thread. It stops a job on the old device before giving it to the new one, and
`pad_remove` calls it before `SDL_CloseGamepad`.

- One controller: all three.
- Several: the one used last, or the one chosen on the Controls tab (`controller_preferred`, by SDL GUID). Another controller
  takes over only after the current one has been idle 500 ms, since a remapper's copy of a pad reports every press too.
- Removed: the most recently used of the rest. Held buttons go with the next `recompute_pad`.
- Activity means a button down or a stick past half. A Steam Controller's touch sensors and its Steam and "..." buttons are
  not activity.

## Steam launch

Steam sets `SDL_GAMECONTROLLER_IGNORE_DEVICES`, hiding the real controller, and offers a virtual pad without sensors.

1. Before `SDL_Init`, `input_steam_env` takes `0x1302` to `0x1305` off the list and sets the rest back with override
   priority. Linux, SDL 3.4+, and only when `SteamVirtualGamepadInfo` is set. `SAVAGEWIND_STEAM_PHYSICAL=0` turns it off.
   Other controllers stay hidden as Steam asks.
2. A pair needs exactly one virtual pad of this family (non-zero `SDL_GetGamepadSteamHandle`, Valve VID, family PID) and
   exactly one real one. The handle comes from Steam's file; SDL rereads that every 3 s, so
   `SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED` refreshes the handle and the ids.
3. More than one of either is ambiguous: nothing identifies which is which (SDL reports no serial for this device). The real
   ones stay controllers of their own, the virtual ones are ignored, and the Controls tab says so.
4. In a pair, motion and vibration are always the real device. Input follows `steam_input`:
   - `auto`: the real device until the virtual pad reports input, then the virtual pad, which carries the Steam layout.
     Three real presses in a row with no virtual report for 500 ms move it back.
   - `physical`, `steam`: forced.
5. `input_user_blocked` is the gate before menus, the pointer and rebinding. It stops the half that isn't feeding the game
   (its presses still count as evidence), touch sensors, and under Steam the Steam button.

Steam keeps its hidraw handles and goes on receiving the Steam and "..." buttons. SDL's driver turns lizard mode off every
3 s while the device is open, as it does from a terminal.

## Motion pipeline

SDL sensor event -> `motion_sensor_event` -> `fuse_gyro` (main thread, under `g_lock`) -> `update` at the game's read
(`GetSixAxisSensorStates`, its own thread) -> 200 Hz samples.

- Watch: `motion_watch` on the window loop sees 300 ms without a reading, turns the sensors off and on, at most once a second,
  every 5 s after five tries, and stops after 30 until a reading or focus returns. It rests while unfocused.
- Warm-up: readings count from the first normal step of the sensor clock, or after 16 readings for a clock that never steps.
  Covers the first reading after enabling, after recovery and after refocus.
- Gaps aren't integrated. The orientation holds through a stall and gravity levels it.
- Source: SDL, then a live DSU service, then the mouse tilt, keyboard gyro and script. A stalled SDL stream doesn't give way
  to a dead DSU. `motion_has_sensors` means usable, so a stream gone for good sends shrine apparatus to the sticks.
- Fused gyro: the game's angular velocity comes from the fused orientation. See the measurements in
  `docs/CONTROLLER_PROGRESS.md`.

## Vibration pipeline

Game -> SDK vibration nodes (recompiled) -> `hid` 201 / 206 -> `vibration_send` -> `vibration_update` on the window loop -> one
route:

- Tone: output report `0x83` on the real Steam Controller, 60 ms lease, at most +6 dB, 30 to 1000 Hz, one write per 8 ms,
  `0x82` to stop.
- Nintendo packets: `SDL_SendGamepadEffect`.
- Motors: `SDL_RumbleGamepad`, shaped, 100 ms lease.

A failed route falls to motors. A change of device stops the tone first. Settings: on / off, intensity, quality; the
`SAVAGEWIND_VIBRATION*` variables still override.

## Threads

Pads and SDL calls: main window loop only. Game threads read atomics and `pad` as a null test. The Controls tab reads
`input_status`, `input_live`, `motion_status`, `vibration_route`, and asks for changes through `input_settings_changed`, which
`input_mouse_update` applies on the main thread. Sensor toggles run on the main loop, not a render thread: game threads
present.
