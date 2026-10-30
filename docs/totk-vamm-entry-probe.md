# VammManager entry diagnostic (2026-10-30)

Diagnostics only. No allocator, initialization, SDK response or guest behavior
was repaired. Exactly **one instrumented TotK startup** was run in this task.

## Probe

`SAVAGEWIND_SDK_ENTRY_PROBE=1` enables a once-per-method, per-process entry log.
Unset, empty or `0` leaves it disabled. The existing generated `FN_ENTER` hook is
used through `runtime/fnprof.c`; the generator, cpu.h and dispatch are unchanged.
Probe-only mode does not collect the normal function-profile hash counters.

Each entry prints the actual function address, x0, the eight-byte value at
x0+0x40, and guest thread ID/handle. Linux self-memory reads avoid introducing a
second SIGSEGV for an invalid object pointer; such a field is printed as
`unreadable`, never silently as zero. Logging does not write guest memory.

The exact SDK export names match between the supplied TotK 1.0.0 and BOTW 1.9.0
NSOs:

| Method | TotK | BOTW 1.9.0 |
| --- | --- | --- |
| nn::os::detail::VammManager::AllocateAddressRegion | f_7004e3d1d0 | f_7002649df0 |
| nn::os::detail::VammManager::AllocateMemory | f_7004e3d3d4 | f_7002649f70 |

These are equivalent exported methods, not identical literal addresses or
assumed identical implementations across the two SDK versions.

## TotK run3 result

Used the same runner and input root as the pre-existing `run3-romfs` result,
with only the opt-in probe enabled. The separate local input root and its raw
RomFS image already existed before this task; they were not created, copied or
modified here. The previous run evidence was preserved.

```sh
PYTHONDONTWRITEBYTECODE=1 tools/venv/bin/python tools/totk_experiment_run.py \
  --exe build/totk-run/savagewind --library build/totk-run/swgame.so \
  --data build/totk-experiment/totk-root \
  --out build/totk-experiment/run3-entry-probe \
  --policy blocking --sdk-entry-probe --seconds 60
```

Observed at line 98 of `run3-entry-probe/run.log`:

```text
[sdk-entry-probe] totk VammManager::AllocateAddressRegion f_7004e3d1d0 x0=0000007005a34430 LD64(x0+0x40)=0000000000000000 guest_tid=80 handle=0x101
```

The next nonempty line is `*** signal 11 accessing 0xc0`. Its host PC is
`f_7004e3e02c+0x6e`, and guest LR is `0x7004e3d298`. The handler subsequently
terminated with SIGABRT (`child_exit: -6`), recorded after 26.840 s. The original
failure was **SIGSEGV**, not an rt_udf or the observer's timeout.

| Entry | Observed before SIGSEGV | x0 | [x0+0x40] | Guest thread |
| --- | --- | --- | --- | --- |
| f_7004e3d1d0 | Yes, immediately preceding the signal record | 0x7005a34430 | **0** | **80 (0x50)**, handle 0x101 |
| f_7004e3d3d4 | No entry recorded before termination | — | — | — |

The log is once per entry: it establishes the captured first entry and the
observed signal sequence, not an unrestricted trace of every later repeated
invocation. Both functions pass their object's +0x40 field to the common helper
f_7004e3d274 in the generated SDK. The zero value does not by itself establish
why that field was not initialized; no initialization fix was attempted.

## BOTW comparison

Built separately with `B=build/totk-baseline` and its BOTW game library. The first
observation (`botw-entry-probe`) stopped at 1171 presented frames / 27.788 s because
the existing blocking-policy watcher treats `CommandBufferSetRasterizerDiscard`
as unimplemented. This was an observer stop, not a BOTW crash. Neither matching
VammManager entry had been logged by that point.

A native `script`-wrapper comparison failed earlier in SDL initialization with
“The video driver did not add any displays”; that run cannot prove anything
about the two guest functions. It was preserved as `botw-entry-probe-full` and
was not treated as a game regression or fixed.

The bounded BOTW comparison through the original Python runner uses `--policy
observe`, retaining nonfatal NVN stub messages while still stopping on fatal
errors or abnormal exit. This changes only the observer, not the runtime or any
NVN response. This run completed its **100.070 s observation window**, reaching
at least **5522 presented frames**. There was **no SIGSEGV or fatal error**, and
frame 3100 was visually checked: the opening scene says “Open your eyes…”.
The watcher ended at its timeout (`reason: observation_timeout`, exit 4;
coordinated child exit 0), not a natural completion of the game.

**Neither BOTW counterpart entry was observed in this startup.** Therefore no
BOTW x0/+0x40 value or function return result can be reported. This is a bounded
negative observation, not proof that these functions never execute in later
gameplay. Both generated entries contain their normal `FN_ENTER` and are in the
SDK dispatch table; the opt-in flag was enabled. There is no matching direct HLE
replacement for these two methods in the inspected symbol list. No function was
called artificially to manufacture a comparison value.

```sh
PYTHONDONTWRITEBYTECODE=1 tools/venv/bin/python tools/totk_experiment_run.py \
  --exe build/totk-baseline/savagewind --library build/totk-baseline/swgame.so \
  --data /home/solve/.local/share/savagewind/extracted \
  --out build/totk-experiment/botw-entry-probe-observe \
  --game botw --policy observe --sdk-entry-probe --shots 3100 --seconds 100
```

## Verification and artifacts

- Both separate runtime builds pass. No generated-code object was recompiled.
- BOTW was regenerated independently: **997 files, all byte-identical** to the
  original pre-experiment baseline.
- Both game-library binaries are also unchanged by this diagnostic. SHA-256:
  - BOTW swgame.so: `379ea197aecc4e19c0baabe14a0ec9ebbf49d6706158a610861bfc552c5704f4`
  - TotK swgame.so: `09203d35cdd9fe30766f51c794629a99b3b61df6bc38066c4e8b1e486adbac2e`
- A synthetic, game-independent harness verifies: default off produces no log;
  enabled repeated entry logs once; a valid +0x40 reads the expected 64-bit value;
  an invalid pointer prints `unreadable` without crashing. Compilation also passes
  `-Werror=format`. These synthetic inputs are not presented as BOTW game calls.
- Logs and test harness are under `build/totk-experiment/sdk-entry-probe-*`;
  actual observations are `run3-entry-probe`, `botw-entry-probe` and
  `botw-entry-probe-observe`. All output/config/save paths are isolated.

No second TotK start or behavioral repair is authorized or performed in this task.
