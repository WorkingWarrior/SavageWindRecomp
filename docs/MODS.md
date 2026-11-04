# Mods

Savage Wind loads mods made for the Switch version of the game (1.9.0 for Breath of the Wild), the way emulators and a
modded Switch take them. No mods ship with it: use mods you trust.

## Where mods go

Each mod is a folder of its own in the game's mods folder: `mods/botw` in the data folder
([data-folder.md](data-folder.md)) for Breath of the Wild, `mods/totk` for Tears of the Kingdom. Settings > Mods > Open
mods folder opens it. A folder whose name starts with `_` or `.` is left out, a quick way to park a mod.

Any of these layouts works (names in any case):

| Layout | Example |
|---|---|
| Emulator mod folder | `mods/botw/Better Rain/romfs/...` |
| Switch SD card | `mods/botw/Better Rain/atmosphere/contents/01007EF00011E000/romfs/...` |
| Title folder (BCML's output) | `mods/botw/Better Rain/01007EF00011E000/romfs/...` or `.../contents/01007EF00011E000/romfs/...` |
| Bare RomFS tree | `mods/botw/Better Rain/Actor/...`, `.../Pack/...` (the game's own top folders) |
| Zip unpacked into a folder of its own name | `mods/botw/Better Rain/Better Rain/romfs/...` |
| The DLC's files | `.../01007EF00011F001/romfs/...` (BCML) or `.../romfs/aoc/0010/...` (as on the Wii U) |
| ExeFS patches | `mods/botw/Some Patch/exefs/<build id>.ips` or `*.pchtxt`, or the SD card's `atmosphere/exefs_patches/<name>/` |
| Cheats | `mods/botw/Infinite Stamina/cheats/<build id>.txt`, or `.../atmosphere/contents/01007EF00011E000/cheats/...` |

A whole emulator mods folder can be copied in as it is (yuzu's `load/01007EF00011E000`, Ryujinx's
`mods/contents/01007EF00011E000`): each mod in it is listed on its own, as `<folder>/<mod>`. The examples are Breath
of the Wild's; Tears of the Kingdom's title is 0100F2C0115B6000.

## Order and switches

Settings > Mods lists every mod, top first; in the game F1 (Back + Start on a controller) opens the same menu. The top
mod wins a file two mods change. A on a mod shows its switch, Move up / Move down and its cheats. The list is kept in
`mods.ini` in the game's mods folder; new mods come in switched on, below the known ones. Changes apply at the next
start (cheats: at once). Each mod's description says where its files were found
and, once the game has loaded, how many game files it replaces and adds, and how many a mod above it takes.

## The resource size table

The game sizes each file's memory from a table, and a bigger file needs a bigger entry. You need no RSTB tool: at start
the tables mods bring are merged into the game's (each entry the larger), and an entry no table raised grows with its
file. In Tears of the Kingdom files a mod adds get an estimated entry too. In Breath of the Wild a file the table has
no entry for is sized by the game from the file, as its own are.

## ExeFS patches

The game's code is recompiled ahead of time, so a patch to its code (`.text`) has nothing to change: only what a patch
changes in the game's data (`.rodata`, `.data`) takes effect. Most frame rate and resolution patches change code and do
nothing here; use Savage Wind's own settings instead. The log (`[mods]` lines) and the mod's description say how many
changes of each kind a patch had. Patches are read only from files named for the build you play (by build id, in the
file name or a `.pchtxt`'s `@nsobid`). Mods that bring code modules (`subsdk`, a replaced `main`) cannot work in a
recompiled game.

## Cheats

Atmosphere cheat files (`cheats/<build id>.txt`, the first 16 digits of the build id) list their cheats under the mod
in Settings > Mods, once the game is running: each cheat has a switch of its own, off until you switch it on, and
works from that moment, twelve times a second. The mod's own switch is for its game files, not its cheats. The cheats
on are kept in `cheats.ini` in the game's mods folder. A file's master code (`{name}`) runs with its cheats on. A
cheat file copied in or changed while the game runs comes in with Look for new mods (a changed file's switches stay on
as they were, by the cheats' names). As with ExeFS patches, a cheat that changes the game's code can't work: it is
left out whole, with what else it writes (often code of its own, put in a free spot of the game's data), and shown
greyed. Cheats that change the game's data (health, stamina, items, its values in memory) work. When a code can't be
run, the file's cheats stop until a switch changes. The codes that pause the game (FF0 / FF1) pause it while the codes
between them run, as on the console.

A cheat is made for one version of the game: its file is named for that build, and its addresses are that build's.
Most cheats shared for Breath of the Wild 1.9.0 change its code (they write instructions, like `04000000 00CE9B60
D503201F`), so they do nothing here; a cheat that follows pointers to the game's data (`58...` codes) works once its
addresses are the ones of the version you play. A line of a cheat file that is not codes (a note under a cheat's name)
is skipped, an entry with no codes at all (a file's title, a `-NOTE-`) is not listed, and a cheat with a broken code
(not 8 hex digits) is left out, the rest of the file still loads. In the list, a mod of cheats alone shows how many of
them are on; B folds an open mod.

[docs/cheats](cheats) has a cheat file for Breath of the Wild 1.9.0 written for Savage Wind, from data addresses only:
infinite health and stamina, 999999 rupees, 999 arrows of every kind, 999 of each material and food, weapons, bows and
the shield that never break, and the clock stopped and started again. Copy its folder (`BOTW 1.9.0 data cheats (Savage
Wind)`) into the game's mods folder; each cheat's notes say what it changes and where.

## Limits

- A file a mod adds must go into a folder the game's RomFS has; one in a new folder is left out (the log says so).
- Mods are not installed from archives: unpack them into the `mods` folder.

## When something goes wrong

Switch a mod off (or rename its folder to start with `_`) and start again. `savagewind.log` in the data folder has a
`[mods]` line per mod (on, off, or why it is not used) and what each one's files did.
