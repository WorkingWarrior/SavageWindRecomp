# Controller, motion and haptics: implementation audit

Baseline `73dea53`. Host: CachyOS, KDE Wayland, system SDL 3.4.18, Steam Controller 2026 on its USB Puck (`28de:1304`).

Tags: **[V]** verified here, with the proof named. **[O]** observed by the person at the controller. **[A]** assumption.
Build and test results are in `docs/CONTROLLER_PROGRESS.md`. Raw captures were kept outside the repository.

## Reconnaissance corrections

| Claim | Result |
| --- | --- |
| Triton driver in 3.4.16 and 3.4.18 | [V] The source file is byte-identical |
| `SETTING_IMU_MODE` sent only on enable | [V] Only `SetSensorsEnabled` sends it; nothing re-sends it |
| Lizard mode off every 3000 ms, rumble re-sent every 40 ms | [V] `UpdateDevice` |
| Nothing restored on close | [V] `CloseJoystick` is empty; the driver's comment names a watchdog |
| Trigger rumble unsupported | [V] Returns unsupported |
| Stale first sample | [V] Explained below |
| Virtual pad reports the real PID | Corrected: it reports `0x1302`, the controller, not `0x1304`, the Puck [V] |
| `SDL_GAMEPAD_TYPE_STEAM` usable everywhere | Corrected: 3.4.18 maps these pads to it, 3.4.16 does not, 3.2 has no such value [V] |

`SDL_GetGamepadType` therefore cannot identify this controller across our SDL versions. VID and PID can.

## Steam launch path

[V] Process environment of both shortcuts, read from `/proc`:

- `SDL_GAMECONTROLLER_IGNORE_DEVICES` lists Valve's controllers, including `0x28de/0x1302` to `0x1305`.
- `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1`.
- `SteamVirtualGamepadInfo` names a file with one slot: `name=Steam Controller`, `VID=0x28de`, `PID=0x1302`, `type=steam`,
  and a non-zero `handle`.
- `SteamAppId`, `SteamGameId`, `SteamClientLaunch=1`, `SDL_JOYSTICK_HIDAPI_STEAMXBOX=0`.
- The process holds the virtual pad's evdev node and no hidraw node.

[V] `SDL_ShouldIgnoreJoystick` applies the ignore list to the HIDAPI device, so SDL lists only the virtual pad. That pad
has 11 buttons, no touchpads and no sensors. This is the source of `#SettingsController_SteamController has no motion
sensors`.

[V] Overriding that hint before `SDL_Init` (`SDL_SetHintWithPriority(..., SDL_HINT_OVERRIDE)`) lists both devices under
Steam. Probe run under the real shortcut:

- The physical device gave gyro and accelerometer at 248 Hz, 10252 and 7412 gyro samples in two runs.
- It gave all buttons, the D-pad and the sticks.
- Stall recovery worked there too.
- Rumble was felt every 4 s, so through both pads. [O] The person was not fully sure of this.
- Steam still received the Steam and QAM buttons: `Guide button sent to JS`.

**[V] The virtual pad produced no input at all in these runs:**

| Run | Layout | Physical open | Virtual pad events |
| --- | --- | --- | --- |
| TOTK game | last-resort config | no | none [O] no buttons or sticks |
| BOTW game | selected template | no | none [O] no buttons or sticks |
| Probe, TOTK shortcut | last-resort config | no | none |
| Probe, BOTW shortcut, window focused | selected template | no | none |
| Probe, both shortcuts, window focused | as above | yes | none |

So opening the physical device is not what silences it. `docs/totk-experiment.md` and an earlier local note record the virtual pad
working in earlier sessions, with the D-pad missing. Why it is silent now is unknown. [A] The Steam-side controller
settings changed today may be involved: the person turned desktop mode off. This must not be assumed to stay true on other
machines, so the design has to work whether or not the pad is live.

## X and the on-screen keyboard

- Terminal launch: [O] with Steam's desktop mode turned off, X no longer opens the keyboard. No keyboard action in Steam's
  logs around the presses [V]. It was the desktop layout, as an earlier local note suspected.
- Steam launch: [O] X opens nothing, in both games.
- [V] The game side has no part in it. `SDL_ENABLE_STEAM_SCREEN_KEYBOARD` is read only by SDL's X11 driver when text input
  starts, and the run was on Wayland.

Fix: documentation and a diagnostic hint. No code workaround.

## Sensor stall

[V] Probe, repeated:

- QAM and the Steam button each stop the IMU stream within 400 ms. This is true of every press, also the one closing the
  menu. Buttons and axes go on.
- It does not recover by itself: stopped to the end of each window, up to 27 s.
- Sensors off and on through SDL brought it back about 18 ms later, 4 of 4 times, also with the Steam menu open. The calls
  took about 4 ms. They must go off first: `SDL_SetGamepadSensorEnabled` does nothing when the state does not change.
- The first sample afterwards has a sensor clock jump equal to the gap. Then 4 ms steps.

[V] Steam logs config activations for its own apps (443510, 413080) in the same second. [A] These set the IMU mode to off.

## Stale first sample

[V] While the IMU is off the report's IMU timestamp stays frozen. The driver tracks `last_sensor_tick`, which starts at 0, so
the first report after enabling carries the frozen pose, then the next report jumps by the whole idle time (65 s, 110 s and
335 s seen). The orientation is seeded from that old pose. Fix: never seed from the first accelerometer sample, and require a
following gyro sample with a normal step.

## Other unknowns closed

- Yaw: [V] [O] a turn to the right reads negative on SDL's Y. SDL's frame is right-handed, matching the remap in `motion.c`.
- [V] [O] `paddle1` R4, `paddle2` L4, `paddle3` R5, `paddle4` L5, `touchpad` left pad click, `misc2` right pad click, `misc1`
  QAM, `misc3` / `misc4` stick touch, `misc5` / `misc6` grip touch. All fired when pressed firmly.
- [V] Puck interfaces: one HIDAPI device per slot on interfaces 2 to 5, the vendor interface 6 is disabled. Only a slot
  that has sent a report becomes a joystick, so the Puck is one controller here. With the controller asleep SDL lists none.
- [V] The controller dropped from the Puck once on its own (Steam: `Disconnect (Spontaneous)`), returning 3 s later with a
  new SDL id. Removal has to be treated as a normal event.

## `0x83` tone

[V] [O] Run beside SDL through a second handle on the same hidraw node, at -6 dB and 80 Hz:

- Leased 60 ms tones re-sent every 30 ms: felt, no failed write.
- A single 500 ms tone: felt, ended by itself.
- `0x82` at 300 ms of a 2 s tone: ended it.
- SDL rumble joined and left a running tone, and the other way round: no failed write, nothing felt wrong.

Decision: keep the route, with hard limits, and send it through one owner so it never runs with SDL rumble on the device.

## `nn::hid` in the two games

[V] Binding is by symbol name at load (`runtime/loader.c`, `hle_lookup_impl`), the same for both. Read from each game's own
symbol table:

- Both call `StartSixAxisSensor`, `GetSixAxisSensorHandles`, `GetSixAxisSensorStates` and `GetNpadStates`.
- Only BOTW imports `SetGyroscopeZeroDriftMode`, and the `SevenSixAxisSensor` family.
- Only TOTK imports the vibration node graph (`VibrationPlayer`, `VibrationMixer`, `VibrationTarget`, `VibrationWriter`),
  plus `GetKeyboardState`, `GetMouseStates` and `DisconnectNpad`. Neither is implemented; those calls reach the SDK and
  its services.

The node graph runs recompiled and ends at `hid` 201 / 206, so vibration needs no separate path. Whether either game really
sends values is unverified and is a Phase 3 measurement. `SAVAGEWIND_LOG_VIBRATION=1` shows it.

`StartSixAxisSensor` and `SetGyroscopeZeroDriftMode`: [A] no behaviour depends on them, since samples are always available.
Phase 2 checks this with call counts in both games.

## Root causes

1. Motion lost under Steam: Steam's ignore list hides the only device with sensors.
2. No input at all under Steam here: the only pad left is silent, cause unknown.
3. Motion lost after QAM or Steam: Steam turns the IMU off and nothing turns it back on.
4. Wrong first orientation: the driver delivers the frozen IMU report.
5. Stuck pad after removal, no takeover: `input.c` holds one pad and opens nothing when it goes.
6. Prompts flip on touch: `use_device` treats every button-down as use.
7. `BIND_FLY` on `misc1` collides with QAM on this controller.
8. X opens the keyboard: Steam's desktop layout.

## Plan

### Phase 1: device ownership

Candidates:

| Approach | Motion under Steam | Layout | Verdict |
| --- | --- | --- | --- |
| Honour the hiding, document a Steam-side gyro setup | no route through this virtual pad: no sensors | yes | Rejected as the only path |
| Motion from physical, buttons always from virtual | yes | yes | Fails here: no input |
| Everything from physical | yes | no | Fails the layout rule where the virtual pad works |
| Physical for motion and haptics, buttons from whichever is proven live | yes | yes, when live | **Chosen** |

Design:

- Before `SDL_Init`, Linux and SDL 3.4+ only: remove only `0x1302` to `0x1305` from the ignore list and set the rest with
  override priority. Other controllers stay hidden as Steam asks. Skipped when `SteamVirtualGamepadInfo` is unset. Windows
  stays unchanged: NOT TESTED.
- Identify by VID / PID and `SDL_GetGamepadSteamHandle`, not by type or connection state.
- A pair needs: exactly one virtual pad with a non-zero Steam handle, Valve VID and Triton PID, and exactly one physical
  Triton device. Otherwise it is ambiguous: each physical device stays a controller of its own, with its own motion, and the
  unpaired virtual pad is left closed unless nothing else exists. The state shows in diagnostics.
- One source per logical controller. In Auto the physical device feeds the game until the virtual pad reports activity
  matching a physical press, then buttons and sticks move to it. Motion and haptics stay on the physical device. Switching
  releases everything first. Setting `steam_input = auto | physical | steam`.
- Active controller: the last one used, or the one chosen in settings, one Npad, no stuck buttons, takeover on removal.
- Touch `misc3` to `misc6` never counts as use.
- Prompts: Xbox glyphs stay for Valve pads, with no new assets.

### Phase 2: motion

- Stall detection on host time, 300 ms, then off and on, at most once a second, with a stale state in diagnostics. DSU only if
  it is actually live.
- No orientation seed from the first sample. Gap samples are not integrated.
- Keep the orientation on resume; gravity levels it.
- Measure raw against fused gyro and choose on evidence.
- Add recalibration, a settings action and a bindable input.
- Call counts for `StartSixAxisSensor` and for what each game reads.

### Phase 3: vibration

- One output owner: tone or SDL rumble, never both.
- Limits: tone gain at most +6 dB, lease 60 ms, at most one write per 8 ms.
- Steam launch: physical route.
- Settings: on / off, intensity, quality Auto / Standard / Native. Native is available only for verified routes. Environment
  variables stay as overrides.
- Check stops on shutdown, removal, switch, pause and focus loss. Measure both games with `SAVAGEWIND_LOG_VIBRATION`.

### Phase 4: extra inputs

- `paddle1` to `paddle4`, `misc1` and `misc2` bindable, `misc3` to `misc6` not.
- Move `BIND_FLY`'s default off `misc1`. An explicit `misc1` in an existing file is kept.
- Diagnostic hint and documentation for X.

### Phase 5: settings and diagnostics

Extend the existing pages. New keys with safe defaults; each new text needs an entry in every `runtime/ui/i18n/*.inc`.

### Phases 6 and 7

Synthetic tests with a faked SDL, CI targets, an SDL 3.2 compile against headers already on this machine, a fresh reviewer.

## Decisions needed at the checkpoint

1. Steam launch testing uses temporary launch options pointing at this work's own output directories. Building into the
   shortcuts' directories is not needed.
2. Auto input policy as above. It reads the physical device directly while Steam is also reading it, and bypasses the Steam
   layout whenever the virtual pad is silent.
3. No SDL source or version change is needed.
