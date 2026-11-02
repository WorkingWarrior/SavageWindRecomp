# Controller compatibility

Only the Steam Controller 2026 on its USB Puck was tested on hardware. Everything else is synthetic streams only.

| Controller | Buttons | Motion | Vibration | Hardware |
| --- | --- | --- | --- | --- |
| Steam Controller 2026, Puck, SDL 3.4+ | Yes | Yes, 248 Hz | Tone | See `docs/CONTROLLER_TESTING.md` |
| Steam Controller 2026, wired or Bluetooth | Expected | Expected | Motors | NOT TESTED |
| Steam Controller 2026, SDL 3.2 | Through Steam's virtual pad, if live | No: SDL has no driver | Through Steam | NOT TESTED |
| Steam Deck, Steam Controller 2015 | As before | As before | As before | NOT TESTED |
| Xbox, PlayStation, Switch and others | As before, with last-used switching | As before | As before | NOT TESTED |

SDL versions: local builds 3.4.18, AppImage 3.4.16 (the Triton driver is byte-identical), Windows MSYS2 rolling, distro builds
3.2+. Code compiles against 3.2 headers. Windows: NOT TESTED; the Steam ignore-list change is Linux only.

## Steam launch

Keep Steam Input on. Motion, vibration and a working controller need no Steam-side setup on Linux with SDL 3.4+.

If the Steam layout should apply, check Controls > Controllers. "Steam's virtual pad works" means buttons follow the layout.
If it says the pad has sent nothing, the game reads the controller directly: everything works, but with the game's own
bindings. Why the virtual pad can be silent wasn't found; it was silent in both games on the test machine.

Motion doesn't go through Steam, so a layout needs no gyro setting for the game.

The Steam and "..." buttons stop the sensors for as long as Steam keeps them off. The game turns them on again within about
a second. It's visible in the Controls tab as "Sensor stops recovered".

## X opens Steam's on-screen keyboard

Steam's, not the game's. Started from a terminal with Steam running, Steam applies its Desktop Layout to the controller, and
there X can open the keyboard. Turn it off in Steam: Settings > Controller, the desktop layout. Turning off desktop mode fixed
it on the test machine. Started through Steam, X opened nothing in either game.

## Closing Steam

Not tested. Nothing here depends on Steam running.
