# Controller, motion and haptics: progress

Baseline: `73dea53` on `experiment/totk-on-moj-test`. Work branch: `feature/better-controller-support`.

## Current state

Phase 0, in progress. Stopping at the Phase 0 checkpoint before any Phase 1 work.

Done:
- Branch created.
- Runtime and game library built for both games at the baseline.
- Existing input tests run: all pass.
- SDL 3.4.16 and 3.4.18 source read.
- BOTW and TOTK `nn::hid` imports compared.

Open:
- Hardware session, terminal launch: X / on-screen keyboard, yaw sign, L4 / L5 / R4 / left pad click, sensor stall,
  `0x83` tone.
- Steam launch: process environment of both shortcuts.
- `docs/CONTROLLER_IMPLEMENTATION_AUDIT.md`.

## Next step

Read the hardware session logs and the Steam environment snapshots, write the audit, commit, stop at the checkpoint.

## Build and test commands

Output directories are `build/ctl-botw`, `build/ctl-totk` and `build/ctl-tests`. The first two are reflink copies of
earlier output directories, with `rt/` removed so the runtime is rebuilt from the baseline. The generated trees were
reused, not regenerated.

```
make -j14 B=build/ctl-botw GEN=<BOTW generated tree>     # rc 0, 143 s, 0 warnings
make -j14 B=build/ctl-totk GEN=build/g-totk-gen          # rc 0, 873 s, 0 warnings
```

Both game libraries were recompiled because the generated `.c` files were newer than the copied objects. The library
id is unchanged (`b4fb85f26aa62e00`). Apparent size: `ctl-botw` 651 MB, `ctl-totk` 1.1 GB, `ctl-tests` 8 MB. Reflinks
share extents, so the real disk use is lower.

| Command (`B=build/ctl-tests`) | Result | Time, clean |
| --- | --- | --- |
| `make test-gyro` | pass | 3.6 s |
| `make test-vibration` | pass | 0.2 s |
| `make test-input-focus` | pass | 0.6 s |
| `make test-pc-controls` | pass | 0.5 s |
| `make test-bind-capture` | pass | 0.1 s |
| `make test-control` | pass, 41 checks | 0.3 s |
| `make test-swkbd` | pass, 80 checks | 0.2 s |
| `tools/test_dsu.sh` | pass | 0.4 s |

No pre-existing failures.

## Verified findings so far

From SDL source:
- `SDL_hidapi_steam_triton.c` is byte-identical in 3.4.16 and 3.4.18. `src/joystick/` differs between them in device
  tables, Flydigi, and mapping the Steam Controller families to `SDL_GAMEPAD_TYPE_STEAM`.
- `SETTING_IMU_MODE` is sent only from `SetSensorsEnabled`. Nothing re-sends it. `SDL_SetGamepadSensorEnabled` is a
  no-op when the state does not change, so a recovery must go off and then on.
- Lizard mode off is re-sent every 3000 ms while a joystick is open. Rumble is re-sent every 40 ms while non-zero.
- A sensor event needs `report_sensors` and an IMU timestamp that differs from the last one. A controller that stops
  sending IMU data produces no events and no error.
- On the Puck, a joystick exists only after a slot has sent a state report. With the controller asleep SDL lists 0
  joysticks. Seen on this machine.
- Steam hides devices through `SDL_GAMECONTROLLER_IGNORE_DEVICES` and allows its pad through
  `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD`. `SteamVirtualGamepadInfo` names a file with per-slot `name`, `VID`,
  `PID`, `type` and `handle`. The slot comes from the digits after `pad ` in the evdev name. What Steam really sets for
  these shortcuts is not yet measured.

From the game files, read locally:
- TOTK's `main` does not import `SetGyroscopeZeroDriftMode`; BOTW's does. Both import `StartSixAxisSensor`,
  `GetSixAxisSensorHandles`, `GetSixAxisSensorStates` and `GetNpadStates`.
- BOTW also imports the `SevenSixAxisSensor` family. TOTK does not. Neither is implemented here.
- TOTK imports the `VibrationPlayer`, `VibrationMixer`, `VibrationTarget`, `VibrationWriter` and
  `VibrationNodeConnection` node graph. BOTW's `main` imports only `SendVibrationValue` of the send path.
- TOTK imports `GetKeyboardState`, `GetMouseStates` and `DisconnectNpad`. BOTW does not.

## Decisions

- Phase 0 changes no runtime code.
- Probes and hardware logs stay in temporary directories, outside the repository.

## Files changed

- `docs/CONTROLLER_PROGRESS.md`
