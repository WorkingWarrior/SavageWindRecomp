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

## Inventory (source evidence, before implementation)

Paths in the left column are in SavageWind; `reference:` means `../totk-recomp`.
Line locations below refer to the experiment base before edits. The coordinator
checked the key cited code after the delegated inventories. None of this proves
TotK execution or visual correctness.

### Graphics and shaders (priority)

| Area | Counterpart and concrete difference |
| --- | --- |
| NVN entry | `runtime/hle/nvn_boot.c:34-52` resolves names into host implementations. A missing implementation is registered as a null HLE slot; `runtime/dispatch.c:139-147` routes it to the unimplemented handler. Reference `runtime/hle_nvn.cpp` also has a fallback; neither proc list proves game compatibility. |
| Maxwell and SPIR-V | Both have `runtime/nvn/maxwell/translate.c` and Vulkan shader compilation. Reference `runtime/nvn/vulkan/shaders.c:65-117` applies NoContraction and `:119-137` offers **opt-in**, device-checked SignedZeroInfNanPreserve. Reference also has `spvgen.c`; Savage uses `runtime/nvn/vulkan/spirv.c`. No TotK shader corpus was run on Savage. |
| Synchronization | Savage `runtime/nvn/vulkan/vk.h:6-8` documents GENERAL layouts, barriers and timeline sequence numbers. Reference `runtime/nvn/vulkan/exec.c:681-693` additionally submits at a game fence to delimit per-submission copies. Do not treat their synchronization as interchangeable. |
| GPU-written storage | Savage `runtime/nvn/vulkan/exec.c:3286-3304` waits when subsequent CPU copies overlap GPU writes. Reference `runtime/nvn/vulkan/exec.c:4682-4715` has a VRAM device window with upload/writeback and fallback to host memory. This is a different strategy, not evidence that Savage cannot execute any TotK shader. |
| BOTW target scaling | `runtime/nvn/vulkan/textures.c:175-195` classifies targets using BOTW 1920x1080, 1600x900 and square-shadow assumptions. `runtime/nvn/output_scale.c:66-116` reads BOTW UI/event state and applies projection/FOV policy. These must be neutralized for TotK before rendering. |
| Resource fixes | Reference `tools/rstb_slack.py:1-16` supplies an overlay RESTBL with more heap slack; `make_gameover_fix.py` and `make_quickmenu_fix.py` change frame-based resource behavior. These are **not** NVN implementations. No such fixes will be transferred before a corresponding observed blocker. |

### SDK, memory and CPU contract

Savage executes a generated SDK over its own SVC kernel and IPC services.
The reference replaces more SDK calls with direct HLE. Counting HLE macros, or
finding no identically named HLE source file, does **not** establish a missing
service in Savage.

| Area | Savage | Reference | Evidence limit / concrete gap |
| --- | --- | --- | --- |
| CPU | `runtime/cpu.h:7-35`: x[32], separate sp/pc, NZCV bits 31..28, TLS registers, thread pointer, FPCR/FPSR, v[32] at offset 512 | `runtime/rt.h:31-41`: Ctx x/sp/v, separate n/z/c/vf bytes, 32-bit FPCR/FPSR, tpidr, extra exclusive value and host pointer | Layouts cannot be cast or linked directly. |
| Dispatch | `runtime/dispatch.c:139-170`: c->pc and thread-local target cache, generated GuestFn(Cpu*) | `runtime/rt_main.cpp:155-179`: import thunks and rt_call_indirect(Ctx*, target) | Prefer regenerating NSOs into the existing Cpu contract; actual generation/link must establish feasibility. |
| Address space | `runtime/rt.h:16-31`: modules back-to-back at 0x7000000000, heap 0x7240000000 / max 3.25 GiB | `runtime/rt_main.cpp`, `runtime/hle_os_mem.cpp:24-40`: reference-specific layout and heap accounting | Both use host pointers for guest addresses, with different placement and accounting. No address translation bridge is implied. |
| Memory calls | `runtime/kernel.c:847-875`: SetHeapSize, QueryMemory, Map/UnmapMemory | `runtime/hle_os_mem.cpp:30-34`: QueryMemoryInfo plus direct allocation HLE | Savage permissions/attributes are no-ops; MapMemory copies. Whether TotK requires different semantics is unverified. |
| Threads | `runtime/kernel.c`, `runtime/kernel.h`: SVC kernel threads, waits and synchronization | `runtime/hle_os_thread.cpp:135-145`: SDK HLE creates host threads | Both have threads; TLS/scheduling semantics are not proven equivalent. |
| Files / saves | `runtime/services/fs.c`, `runtime/ipc.c:57-74`: fsp-srv IPC and host/RomFS storage | `runtime/hle_fs.cpp:221-227`: direct MountRom/SaveData/cache/temp HLE | Unknown Savage services use a logging stub (`runtime/ipc.c:19-40`); unknown commands can also return success. A logged stub is a research stop, not acceptance. |
| Account | `runtime/services/acc.c:1-22`: acc:u0, one local UID | `runtime/hle_account.cpp`: local-user HLE | Source counterpart, untested on TotK. |
| Audio | `runtime/services/audio.c:1021-1040`: audren:u renderer | `runtime/hle_audio.cpp`: nn::audio HLE | Different boundary/ABI. Source presence does not prove TotK audio. |
| Input / settings | `runtime/services/hid_set.c`, `runtime/hle/input.c`: HID and set services | `runtime/hle_hid.cpp`: HID HLE | Host keybindings and walking threshold were chosen for BOTW; no physical controls tested here. |
| Time | `runtime/services/time.c`: time:u/a/s | `runtime/hle_time.cpp`: clocks and calendar HLE | Source counterpart; no behavioral acceptance. |

Savage's registered service names, read from the source, are `acc:u0`,
`appletOE`, `apm`, `lm`, `audren:u`, `audout:u`, `fsp-srv`, `hid`, `hid:sys`,
`set`, `set:sys`, `pctl`, `pctl:s`, `pctl:a`, `pctl:r`, `aoc:u`, `nfp:user`,
`time:u`, `time:a`, `time:s`, plus `sm:` in `runtime/ipc.c`.

### BOTW assumptions and experiment boundaries

- `tools/game_sites.py` / `runtime/game_versions.h` describe BOTW versions;
  `tools/gen.py:605-617` obtains sites and applies `tools/patches.py` only when
  those sites exist. `--no-sites` alone does not make the runtime game-neutral.
- `runtime/gamelib.c:77-98` requires all BOTW sites. TotK needs a distinct,
  recognized identity that bypasses **only** those BOTW requirements, without
  fabricating addresses or weakening BOTW validation.
- `runtime/hle/framepace.c:73-102,167-180` dereferences BOTW pause/UI/event data
  and writes the controller step. `runtime/ui/game_options.mm:815-831` mutates
  BOTW title button tables. `runtime/services/fs.c:493-496` patches its version
  string. All must be inactive for TotK.
- `runtime/nvn/objects.c:1078-1102` reads the BOTW frame-loop object when its
  return address matches. TotK must keep only the ordinary NVN interval behavior.
- `runtime/services/test_warp.c:145-162` has a hard-coded BOTW world pointer;
  `runtime/services/fs.c:768-773` can patch BOTW save data. Both must be disabled
  for TotK, including when their debug environment variables are set.
- The launcher is a BOTW installer: its version list and RomFS validation are
  BOTW-specific. This experiment uses explicit generator selection and `--play`
  with separate runtime state; a multi-game installer is outside this bring-up.

## Current evidence

- Reference extracted ExeFS exists with `main`, `rtld`, `sdk`, `subsdk0` and
  `main.npdm`; it can be used in place without modifying/copying originals.
- Baseline BOTW 1.9.0 inputs found at the existing user data root, without changing
  them. Generation exit status 0: main 125144 functions / 559 chunks, subsdk0
  13936 / 179, sdk 40516 / 254. The SDK reports one unsupported instruction;
  this is a pre-existing baseline finding, not a TotK-induced regression.
- Baseline synthetic tests: game sites **35 checks PASS**, layout **59 checks
  PASS**. These do not prove gameplay.
- Baseline full build: **PASS**. TotK generation: **PASS** with unsupported SDK
  instructions recorded below. TotK link/runtime: **PENDING**.

## Implementation decision

Use `gen.py --game totk` to lift the existing TotK NSOs into SavageWind's **Cpu**
contract. No Ctx adapter, Cpu layout change or dispatcher change is introduced.
`--game botw` remains the generator/runtime default; BOTW version/site lookup
retains its existing checks. `runtime/game_profile.h` holds the independently
recognized TotK 1.0.0 build prefix `082ce09b06e33a12`, read by the Python profile.
Runtime `--game totk --play <data>` checks the main module ID, version and absence
of BOTW sites before accepting the game library. GameLib ABI remains unchanged.

TotK disables BOTW code patches, site dereferences, title/options modification,
FPS/controller/VFR patches, voice/RSTB/version rewrites, test warp/save rewriting
and render-target/shadow scaling. It retains guest target sizes and a neutral
16:9 output fit. This is preparation for a bounded attempt, not rendering proof.
The BOTW installer is deliberately unavailable in TotK mode. Packaged TotK setup
and other platforms are not part of this experiment.

Baseline full build **PASS**: exit 0, 284.84 s, `savagewind` and `swgame.so` linked.
The original executable/library were preserved under `build/totk-experiment/`
before rebuilding. Baseline runtime started with isolated config/cache/save paths;
its frame-180 screenshot was black. Later frame-3100 window captures from fresh
baseline and post-change runs both visibly show the opening scene and subtitle
**“Open your eyes…”**. Both runs used scratch saves; no existing save was copied.
These are visual startup checks, not full gameplay/physical input/audio acceptance.
The two captures are under `build/totk-experiment/botw-{before,after}-menu-state/`
in `build/frames/window_03100.png`. The directory name “menu” is just a log name;
the captured scene is the game's opening, not a title menu.

## Executed generation and regression checks

| Check | Result |
| --- | --- |
| Post-change BOTW sites / layout tests | PASS: 35 / 59 checks, matching baseline |
| BOTW generation before/after | PASS: all **997 files byte-identical**, compared by relative names and SHA-256 |
| Post-change BOTW full build | PASS, exit 0; generated object reuse is valid because generated C and cpu.h are unchanged |
| BOTW visual startup | Opening scene/subtitle observed before and after, same frame index, separate fresh state |
| Wrong-game generator negative control | BOTW NSO with `--game totk` exits 1, refuses ID cd57b23fa4bbad65; creates no layout output |
| Wrong-game library negative control | BOTW library with `--game totk` is rejected before module loading: “TotK experiment requires recognized 1.0.0 code with no BOTW sites” |

The first visual runs ended at their external observation timeout (90/100 s,
exit 124), not a successful game exit. Screenshots were produced before timeout.
No timeout was increased to fix startup behavior; later captures were selected
because frame 180 showed no content. No causal performance comparison was made.

TotK generation command (source checkout, read-only ExeFS):

```sh
PYTHONDONTWRITEBYTECODE=1 tools/venv/bin/python tools/gen.py all \
  ../totk-recomp/totk-build/data/exefs build/totk-experiment/totk-gen \
  --game totk --simd c --hle runtime/hle/hle_symbols.txt
```

Exit **0**, with the following layout and results:

| Module | Guest range | Functions | C chunks | Unsupported counter |
| --- | --- | ---: | ---: | ---: |
| main | 0x7000000000–0x7004666000 | 243504 | 1332 | 0 |
| subsdk0 | 0x7004666000–0x7004d0f000 | 14007 | 181 | 0 |
| sdk | 0x7004d0f000–0x7005ae8000 | 40275 | 231 | 277 |

The generated layout carries version 1.0.0 and **zero BOTW sites**. Generation
times reported per module: 61 s / 4 s / 9 s. This does not prove every generated
function is semantically correct or reachable. In particular, the SDK's 277
unsupported cases remain potential runtime traps pending execution.

Their generated C contains **277 unique guest PCs**, duplicated into 434 emitted
`rt_udf` calls: 197 PCs tagged `exclusive pair / CAS` and 80 tagged `ldst reg
atomic/pac`. These tags do not identify the exact instructions or prove reachability.
No instruction implementation has been added speculatively.

Build attempt: `make -j4 B=build/totk-run GEN=build/totk-experiment/totk-gen`,
with output in `build/totk-experiment/totk-build.log` (**PENDING**).

`tools/totk_experiment_run.py` runs one attempt under a pty, preserving a transcript
and JSON result in a new subdirectory of `build/totk-experiment`. It clears inherited
SavageWind debug switches and isolates config, cache and saves. It terminates at
the first logged missing behavior/fatal error or observation timeout; it never
retries or supplies replacement service results. A negative-control run already
verified rejection of a BOTW library. An early trap still needs classification:
it is not automatically evidence of a graphics/service blocker.

## Cost so far

The initial profile/observation changes span **17 files**: 12 runtime files,
3 tools, Makefile and this document. There are 24 added/changed runtime functions
including the inline profile predicate; most changes are one guard at an existing
function boundary. Generator changes are limited to profile selection in
`main_sites` and its CLI option. The game library ABI is unchanged.

**Cpu/Ctx conversion cost: 0 changed Cpu/Ctx/dispatcher/lifter/SIMD files and
0 changed functions in that contract.** NSOs were regenerated rather than linking
the reference's incompatible Ctx-generated code. No CPU/GPU performance delta
was measured. This count will be updated if an executed trap requires a change.

## Pair instructions and outline atomics (revised user scope)

The user replaced the temporary “large mnemonic set” stop: implement the observed
64-bit exclusive pairs, investigate outline-atomic guards, and continue to the
first missing service/graphics behavior or menu. The initial build was stopped
at that temporary boundary after about 9m52s, preserving 1688 objects. There was
no compiler failure and no completed TotK executable/library at that point.

### Exact decoding of the original 277 unsupported PCs

The coordinator decoded the original SDK words with Capstone 5.0.7, matching
**277 unique PCs / 80 exact mnemonics**. Full address/word/operand metadata is
local only: `build/totk-experiment/unsupported-sdk-decoded.json` (not committed).
The 177 exclusive pairs are four mnemonics; the other 100 PCs are size/order
variants of CAS/CASP, SWP, LDADD, LDCLR, LDEOR and LDSET. Counting spelling variants
does not by itself determine implementation effort.

| Mnemonic | Unique PCs | Mnemonic | Unique PCs | Mnemonic | Unique PCs | Mnemonic | Unique PCs |
| --- | ---: | --- | ---: | --- | ---: | --- | ---: |
| `cas` | 2 | `ldaddal` | 2 | `ldclrlh` | 1 | `ldsetb` | 1 |
| `casa` | 2 | `ldaddalb` | 1 | `ldeor` | 2 | `ldseth` | 1 |
| `casab` | 1 | `ldaddalh` | 1 | `ldeora` | 2 | `ldsetl` | 2 |
| `casah` | 1 | `ldaddb` | 1 | `ldeorab` | 1 | `ldsetlb` | 1 |
| `casal` | 2 | `ldaddh` | 1 | `ldeorah` | 1 | `ldsetlh` | 1 |
| `casalb` | 1 | `ldaddl` | 2 | `ldeoral` | 2 | `ldxp` | 26 |
| `casalh` | 1 | `ldaddlb` | 1 | `ldeoralb` | 1 | `stlxp` | 63 |
| `casb` | 1 | `ldaddlh` | 1 | `ldeoralh` | 1 | `stxp` | 38 |
| `cash` | 1 | `ldaxp` | 50 | `ldeorb` | 1 | `swp` | 2 |
| `casl` | 2 | `ldclr` | 2 | `ldeorh` | 1 | `swpa` | 2 |
| `caslb` | 1 | `ldclra` | 2 | `ldeorl` | 2 | `swpab` | 1 |
| `caslh` | 1 | `ldclrab` | 1 | `ldeorlb` | 1 | `swpah` | 1 |
| `casp` | 1 | `ldclrah` | 1 | `ldeorlh` | 1 | `swpal` | 2 |
| `caspa` | 1 | `ldclral` | 2 | `ldset` | 2 | `swpalb` | 1 |
| `caspal` | 1 | `ldclralb` | 1 | `ldseta` | 2 | `swpalh` | 1 |
| `caspl` | 1 | `ldclralh` | 1 | `ldsetab` | 1 | `swpb` | 1 |
| `ldadd` | 2 | `ldclrb` | 1 | `ldsetah` | 1 | `swph` | 1 |
| `ldadda` | 2 | `ldclrh` | 1 | `ldsetal` | 2 | `swpl` | 2 |
| `ldaddab` | 1 | `ldclrl` | 2 | `ldsetalb` | 1 | `swplb` | 1 |
| `ldaddah` | 1 | `ldclrlb` | 1 | `ldsetalh` | 1 | `swplh` | 1 |

### What the reference actually implements

`reference:tools/recomp/gen.py:512-547,799-817` has scalar exclusive handlers but
**no handlers for these pair or LSE instructions**. Its `:265-272` emits
`rt_trap("unhandled: ...")` for them. The existing reference SDK `report.json`
independently contains the same counts: 177 pairs and 100 LSE, among 3719 total
unhandled instructions. This is not evidence that those paths were executed.
`reference:runtime/rt.h:163-189` supplies scalar value/CAS monitors; `excl_val2`
exists in Ctx but is not used to implement pairs. Runtime SDK PLT binding prefers
HLE, then main exports, then lifted SDK (`reference:runtime/rt_main.cpp:681-728`),
so reference startup does not exercise the same SDK path as Savage. No reference
runtime was launched in this experiment.

### LSE guard and evidence limits

For **all 100 LSE PCs**, the preceding three instructions are an ADRP to
0x7005ae4000, LDRB w16 at offset 0x780, and CBZ w16 to the LL/SC fallback.
The shared feature byte is **0x7005ae4780**, SDK-relative **0xdd5780**. For example,
the guard starts at 0x700527ce50 and skips CASB at 0x700527ce5c when zero; CASP's
guard starts at 0x700527d150 and selects LDXP/STXP fallback at 0x700527d164.
The final checked LSE instruction is LDSETAL at 0x700527e12c.

The byte is in zero-initialized BSS. A scan of SDK ADRP references found only the
100 outline guards referring to this offset; no initializer/store was found.
There is no exported `lse_atomics` symbol or matching relative data pointer.
Thus these guarded LSE instructions are unreachable **while this byte stays
zero**; no instruction or flag is patched to manufacture that result. The boot
log will observe the byte after actual module loading. Its value on a physical
Switch was not measured here; absence of all possible indirect writes has not
been formally proved. LSE `rt_udf` remains visible rather than silently emulated.

For hardware context, Arm identifies Cortex-A57 as Armv8.0-A and LSE as an
Armv8.1-A feature; this does not substitute for reading the actual flag on a console.
Sources: [Arm extension guide](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Understanding%20the%20Armv8.x%20extensions.pdf),
[Arm LSE introduction](https://learn.arm.com/learning-paths/servers-and-cloud-computing/lse/intro/).

### Minimal pair implementation and CPU cost

Previously, scalar `ldxr/stxr` helpers stored an address and one value in Cpu,
loaded with acquire ordering, and used a value-based CAS with seq-cst ordering
for store-conditional. The scalar path remains unchanged. The new 64-bit pairs
use aligned 128-bit atomic load/CAS, relaxed ordering for LDXP/STXP, acquire for
LDAXP and release for STLXP; store returns 0/1 and consumes the reservation.
Both halves are captured before writing destination registers. SP/XZR and base
overlaps retain architectural register semantics. No unobserved 32-bit pair forms
or additional LSE families were implemented.

Arm requires an aligned, indivisible successful 128-bit pair store; see
[Arm A64 instruction definitions](https://documentation-service.arm.com/static/67e40f3398aa3c3b6eea6a85).
The helpers use the existing value/CAS monitor approximation: an ABA write that
restores identical data is **not detected**, as with the original scalar monitor.
This is not a complete hardware exclusive-monitor model.

Cpu adds one `excl_val2` field at offset **336**, using existing padding. **sizeof
Cpu stays 1024, SIMD v stays at 512, and all previous field offsets stay unchanged.**
No Ctx adapter, flag representation change, dispatcher change or ABI version
change is needed. Production instruction changes are **2 files** (`cpu.h`,
`lifter.py`): 2 new helpers and 1 modified decoder function. The readonly boot
observation additionally touches `game_profile.h` / `main.c`. Two test files add
8 Unicorn differential sequences plus 4×10000 contended increments and an atomic
pair-store observer. All passed on this Linux x86-64 host. Other hosts are untested.

After the pair change, all 177 original pair PCs lift successfully and the SDK
unsupported count is **100**, exactly the guarded LSE set. BOTW generation again
has **997 byte-identical files** compared with the pre-experiment baseline.
Full post-pair BOTW/TotK builds and startup observation are **PENDING**.
