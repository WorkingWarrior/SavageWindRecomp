# Controller testing

## Automated

No game files, no controller. Everything runs with `make test-input`, in CI too (`.github/workflows/tests.yml`, job `input`).

| Target | Covers |
| --- | --- |
| `test-pad` | Who does what (`runtime/pad_policy.h`), then through `input.c` with SDL's virtual joysticks: physical + Steam virtual, correlation ok / ambiguous / none, switching with buttons held, removal during polling, reconnection, touch sensors, two identical controllers |
| `test-motion-stream` | Readings through `motion.c` to the game's samples: stale first reading, stall and recovery with its limits, 4 / 8 ms jitter, SDL <-> DSU, refocus, recalibration, what the games read |
| `test-controller-settings` | `savagewind.ini`: old files, a save and reload, nonsense values, a kept `bind_pad_fly` |
| `test-vibration` | HID commands, routes, one route at a time, bounds, rapid updates, handover, settings, 207 to 209 |
| `test-bind-capture` | Capture: back buttons bind, touch sensors don't, Steam Controller names |
| `test-input-focus`, `test-pc-controls`, `test-gyro`, `test-control`, `test-swkbd`, `tools/test_dsu.sh` | The existing suites |

## Manual checklist

Steam Controller 2026 on its Puck, the only controller at hand. Build: this branch, its own output directories. Other
controllers: NOT TESTED.

Row 1 under Steam: sticks, D-pad, shoulders, triggers and gyro reported working in both games. Vibration under Steam: the game
sent non-zero values in both (TOTK: 12660 changes in one session, from its own node graph), and it was felt in TOTK.
These runs used the build before `0e3a691`; that commit was smoke-tested only.

| # | Action | Expected | BOTW terminal | BOTW Steam | TOTK terminal | TOTK Steam |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Buttons, sticks, D-pad, triggers in a menu and in play | All work, none doubled | NOT TESTED | PASS | NOT TESTED | PASS |
| 2 | Log after start | Sensors on, motion from the controller | PASS (log, title) | PASS (log) | PASS (log, title) | PASS (log) |
| 3 | Aim with motion: small corrections | Follows without lag | NOT TESTED | PASS, as reported: gyro works | NOT TESTED | PASS, as reported: gyro works |
| 4 | Aim with motion: fast turns | Follows, no jump | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 5 | Lay it still 60 s, watch the aim | No drift | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 6 | Press "...", close it, aim again | Motion back within a second, no jump | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 7 | Press Steam, close it, aim again | Motion back within a second, no jump | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 8 | A motion puzzle or apparatus | Tilts as held | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 9 | Rumble in play (hit, bomb) | Felt, stops | NOT TESTED | NOT TESTED | NOT TESTED | PASS, felt |
| 10 | Test vibration, Controls tab | A pulse; route shown | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 11 | Recalibrate | Hint, then normal | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 12 | Turn the controller off in play, on again | Nothing stuck, works again, motion back | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 13 | Press X | No on-screen keyboard | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 14 | Back buttons bound, used in play | Do the bound action | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| 15 | Steam overlay and QAM in play | Work | n/a | NOT TESTED | n/a | NOT TESTED |
