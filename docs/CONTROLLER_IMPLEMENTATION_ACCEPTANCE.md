# Controller, motion and haptics: acceptance

Baseline `73dea53`. Branch `feature/better-controller-support`, local commits only, nothing pushed.

## Changes

- Steam launch: the real Steam Controller stays visible beside Steam's virtual pad. Motion and vibration use it; buttons use the
  virtual pad once it proves alive.
- Several controllers: last used, or chosen, with takeover on removal.
- Motion: stall detection and recovery, stale readings dropped, orientation held through gaps, recalibration, on / off, source.
- Vibration: settings, one route at a time, route reported, test pulse.
- Bindings: Steam Controller names, touch sensors refused, fly mode off "...".
- Controls tab: controllers, motion, vibration, live diagnostics. The game's Options screen: the main switches.
- Tests: four new programs, `make test-input`, a CI job.

Files: see `docs/CONTROLLER_PROGRESS.md`.

## Commands and results

| Command | Result |
| --- | --- |
| `make -j B=build/ctl-botw GEN=<BOTW generated tree>` | pass, no warnings |
| `make -j B=build/ctl-totk GEN=build/g-totk-gen` | pass, no warnings |
| `make B=build/ctl-tests test-input` | pass |
| `clang -fsyntax-only` of the changed sources against SDL 3.2.30 headers | clean |
| `python3 tools/i18n_check.py` | Fails: 5 entries per language, `runtime/ui/menu.mm` FPS and Console texts, not touched here |
| Windows build | NOT TESTED: no cross compiler here. Reasoned only: new Steam code is `__linux__`, the rest uses what these files already used |
| CI job `input` | NOT TESTED: never run on GitHub. Nothing pushed |
| Smoke run, both games, terminal | pass: sensors on, motion from the controller, the game reads six-axis |
| Played, BOTW and TOTK, Steam launch | pass: pair formed, 248 Hz, motion from the controller, inputs and gyro work, the game sends non-zero vibration |

## Performance

Measured on this machine, one thread, `-O2`, baseline against this branch, three runs each.

| Path | Baseline | This branch |
| --- | --- | --- |
| `motion_sensor_event` | 33 ns | 35 ns |
| `update`, a 60 Hz read of 3 samples | 326 ns | 327 ns |

Sensor event to six-axis: the sample is taken at the game's read, so the delay is the time to that read plus these costs.
Replay of real captures through `motion.c` shows no lag against raw at read resolution. No allocations in these paths. HID
writes and sensor toggles run on the main window loop, not on a render thread. Stall messages are capped. A recovery takes about
4 ms on that loop.

`recompute_pad` gained six axis reads and an atomic store for the live view: not measured separately.

## Limitations

- Why Steam's virtual pad was silent on the test machine: unknown. The game reads the controller directly then, so a Steam
  layout doesn't apply.
- That Steam's config activation turns the IMU off: inferred from timing.
- Two identical controllers under Steam: no pairing. Both are usable; each Steam layout isn't. The Controls tab lights both when
  one is chosen, and only the first can be chosen.
- Steam Controller button names show when one is connected, even while an Xbox pad is the one in use.
- The start screen, before the game, still sees each SDL pad as it comes.
- New menu texts are English in every translation table.
- BOTW's `SevenSixAxisSensor` family is not implemented, as before; no effect seen.
- Trackpads as input: out of scope.

## Review findings

Independent reviewer, fresh agent, read only, on the state before `f18391c`.

| # | Finding | Resolution |
| --- | --- | --- |
| 1 | CI job wouldn't link SDL in four rules | Fixed: `LIBRARY_PATH` in the step |
| 2 | Menus and rebinding saw both halves of a live pair; Back + Start closed itself | Fixed: gate in `input_user_blocked`, tested |
| 3 | A late Steam handle left stale ids, so no pair and routes flapping | Fixed: ids refreshed |
| 4 | Steam button opened our overlay under Steam | Fixed: stopped at the gate under Steam, tested |
| 5 | `motion_has_sensors` true with no usable motion, no stick fallback at shrines | Fixed: means usable now, tested |
| 6 | `virtual_live` survived the real half leaving first | Fixed, tested |
| 7 | `virtual_live` one-way | Fixed: three unechoed presses move it back, tested |
| 8 | Last-used flapping with a remapper's copy of a pad | Fixed: takeover needs 500 ms idle, tested |
| 9 | Old sensor pad never turned off | Fixed in `motion_pad_closed`, tested |
| 10 | Touch sensors reached the pointer and menus | Fixed: stopped at the gate, tested |
| Nit | Label override of `SAVAGEWIND_PAD_TYPE` | Fixed |
| Nit | Labels with an idle Steam Controller | Kept: listed under limitations |
| Nit | False stalls in scripted runs | Fixed |
| Nit | Unbounded retry | Fixed: stops after 30 |
| Nit | Panel warning and status | Fixed |
| Nit | Duplicate GUIDs in the panel | Kept: listed under limitations |
| Nit | Silent overflow of the filter | Fixed: logged |
| Nit | Two weak tests | Fixed |
| Nit | Dangling doc references | Fixed |

The fixes themselves have not had a second independent review.

## Completion criteria

| # | Criterion | State |
| --- | --- | --- |
| 1 | Phase 0 and checkpoint | Met |
| 2 | Motion in both games, both launches | Steam launch met in both games, confirmed at the controller. Terminal: by log at the title |
| 3 | No double input or haptics | Met by construction and tests. On hardware NOT TESTED |
| 4 | Sensor stall recovered | Met in the probe; in game NOT TESTED |
| 5 | Stale first sample | Met: test, and pitch right in the game logs |
| 6 | Switching, removal, reconnection | Met in tests; on hardware NOT TESTED |
| 7 | Touch sensors not activity | Met in tests |
| 8 | Vibration routing | Met; in play NOT TESTED |
| 9 | Inputs, fly mode, X | Met; X documented, Steam-side fix confirmed on the machine |
| 10 | Settings and diagnostics | Met in code; on screen NOT TESTED |
| 11 | Tests, CI | Tests met; CI job NOT TESTED |
| 12 | Builds | Linux met, SDL 3.2 headers clean, Windows NOT TESTED |
| 13 | Review | Done, findings resolved |
| 14 | Manual checklist | Partly recorded in `docs/CONTROLLER_TESTING.md`; the rest NOT TESTED |
