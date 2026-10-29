# TotK on SavageWind: bounded research experiment

## Scope and evidence rules

Started 2026-10-30. This is a feasibility experiment, not a port. Work stops at
the first concrete missing service/graphics behavior or the first menu, whichever
comes first. Source comparisons are not runtime acceptance. BOTW gameplay and
TotK gameplay remain **UNVERIFIED** until observed. No FPS claim is made.

- Branch: `experiment/totk-support`.
- SavageWind base: `860f3722225a262bda78d049bf272e084efdc839`.
  This object was initially absent locally and fetched explicitly. The original
  local `main` stays at `d71294213781a982f13004518d91d4e37d228764`.
- Read-only reference: `../totk-recomp`, HEAD
  `a32f709e1e6a0d19160323e57e96d7701ad0d5ed`.
  Existing local modifications: `README.md`, `tools/build_from_xci.sh`, and
  untracked `tools/linux/build_static_deps.sh`; preserve all three.
- User supplied the dump and key locations. Original dumps, keys, extracted
  ExeFS/NSOs and RomFS are read-only inputs; never commit game data/generated code.
- The objective's time stop contains an unfilled `[X hours/days]`; no numerical
  time limit has been inferred. This log records concrete progress and blockers.

## Disk dry run and user decision

`git clean -ndx` was used only as an inventory. Its full output includes source
WIP and protected data, so it is **not** an executable deletion plan.

Proposed paths, relative to `../totk-recomp`, measured by `du -s -B1`:

| Path | Allocated bytes | Classification |
| --- | ---: | --- |
| `totk-build/gen` | 927006720 | Generated game C++ and metadata |
| `totk-build/build` | 334893056 | Build files, runtime objects, executable, map |
| `totk-build/obj` | 389910528 | Generated game objects |
| `totk-build/dist` | 304656384 | Assembled runtime package |
| `totk-build/work` | 45789184 | Function lists and extraction/generation logs |
| Total | 2002255872 | About 1.865 GiB |

These five directory roots are not symlinks or mount points. No tracked files
were found beneath them. `dist/totk-recomp/data` points to the preserved
`totk-build/data`; `dist/totk-recomp/lib/libSDL3.so.0` is also a symlink.
An approved deletion would remove links themselves without traversing targets.
`totk-build/data` (18064773120 allocated bytes), dependencies, Python environment,
source WIP, original dumps and keys were excluded.

**User decision: do not delete; continue research without cleanup.** Nothing was
deleted; freed space is **0 bytes**. The existing reference build remains on disk.
No reference gameplay has been claimed. For future reconstruction after an
approved cleanup, the inspected script supports `bash tools/build_from_xci.sh
--skip-extract --out "$PWD/totk-build"` using the preserved data (no extraction
or key access required). This reconstruction has not been run here.

## Execution plan

1. Inventory game-specific assumptions, CPU contracts, SDK/memory and especially
   graphics/shader differences. Verify key source claims independently.
2. Record baseline generator/build checks for BOTW when its inputs are available.
3. Separate game identity from BOTW version and disable BOTW-specific behavior
   for the TotK experiment. Recognize the observed TotK build ID explicitly.
4. Generate and link TotK `main`, `subsdk0`, `sdk` using SavageWind's generator.
   Prefer its existing `Cpu` contract if the actual generation permits it.
   Record unsupported instructions, relocations and missing symbols verbatim.
5. Only after successful linking, run with isolated output/config/save paths to
   the first concrete missing service/graphics behavior or menu; then stop.
6. Check BOTW regression evidence separately and report blockers, effort and
   recommendation based on executed results.

## Current evidence

- Reference extracted ExeFS exists with `main`, `rtld`, `sdk`, `subsdk0` and
  `main.npdm`; it can be used in place without modifying/copying originals.
- Generator, link, runtime and BOTW regression results: **PENDING**.
- Read-only inventories have been delegated by explicit user request; their
  findings remain hypotheses until checked against the cited source lines.
