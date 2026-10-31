# TotK on SavageWind: bring-up and experiment log

## Final report (end of the continuation task)

**Result: TotK 1.0.0 now boots on the SavageWind runtime, plays its opening and renders the world.**
Observed headless (gamescope, RX 9070), `build/totk-experiment/claude-61` and `claude-60`: "Nintendo
Presents", "Tears of the Kingdom", the opening cutscene in the textured cave (torch, Link, Zelda with the
"Zelda, Princess of Hyrule" caption and subtitles), then gameplay with HUD hearts and control hints
(frames 1500-3000). No fatal error in 150 s runs and a 420 s run. No title *menu* was seen: a clean save goes
straight to the intro; the reference shows the menu only once a save exists.

**What did not work / is not verified**
- (Fixed after the first version of this report: Zelda's broken body and the white streaks were `LDC c[reg + imm]`
  read with an unsigned imm16, so skinning shaders read their bone palette 64 KB off; now sign-extended as in
  the reference. `claude-67`: Zelda and Link render correctly in the cutscene, close-up and gameplay.)
- An intermittent SIGSEGV in SDK audio mixing `f_7004f31b50` (read of address 0, ~9 s into the run) **is not fixed**.
  It did not occur in 72 consecutive 25 s starts through the plain runner (24 serial, 48 as four parallel loops, 24 more
  with `SAVAGEWIND_AUDIO_LOG=1`), but it did occur once in a headless-gamescope run of the same branch (`u6-totk`), so
  the rate is low and may depend on timing. The cause is still not identified.
  What is known (session 3): the faulting function varies: `nn::audio::ApplyLightLimiterEffect` (reads address 0, its
  input pointer array from `Process` holds a null) or `ApplyI3dl2ReverbEffect4ch` (reads -4), both called from
  `CommandListProcessor::Process`, ~8 s into the run, in the first frames whose command list has the reverb (0x16) and
  limiter (0x1b) commands. In a frame where it works those commands' states are all zero at first and `Process`
  itself initialises them (the next frame shows delay-line pointers). The earlier theory (a null `list + 0xf30` table:
  skip the frame) is WRONG: that field takes other values (`4b000001c20`) and skipping the frames before the first
  update kept the states from being initialised, so the very next frame crashed every time (6 of 6). The same frames
  run fine without the skip. The renderer's mutex (`system + 0x1f0`) is the one `AudioRenderSystem::Update` takes for
  its whole body. 0 crashes in 27 gamescope starts with diagnostics (3 right after a rebuild, 10 under full CPU load);
  the earlier ones had all come in runs with extra `--env` switches. Under load one start crashed elsewhere: a null
  dereference in the game's own `main` code (`f_70007a5078`, called from `0x70024067d4`) after 22 s.
- TotK's audio renderer runs (about 265 us per frame, no underruns) but its output peak is 0.000 for the first 60 s in
  the opening cave, with the code before and after the unification commits alike: no sound is produced there yet.
- Audio output, input, saving, performance (10-30 fps headless), a physical display, anything past the
  opening cave. 20 more minutes of play or the open world were not exercised.

**Changes (27 commits after 860f372, 35 files, +1738/-26; nothing generated or game-owned committed)**
| Change | Cost / scope |
| --- | --- |
| VAMM budget, ADR continuations, physical-memory intervals, audio renderer, cache info reader (earlier fixes) | kernel, generator, fs, audio |
| Conditional render + rasterizer discard | cmdbuf.c, exec.c, TotK only |
| `totk_texels_2d` ReadTexels/WriteTexels (tested against a layout oracle) | objects.c |
| Region table 1024 -> 65536 | kernel.c, one constant |
| hwopus decoder service on libopus (`-lopus` on Linux) | new `services/hwopus.c`; BOTW keeps the stub |
| Compute shaders with the 0x100-byte TotK header | translate.c |
| VOTE, LEA, VMNMX, RED, ATOM, DADD, DMUL, DFMA, F2F.F64; `shaderFloat64` | translate.c, device.c |
| `nvnTextureGetViewOffset` (the cause of the magenta terrain) | objects.c, TotK only |
| LDC with register address: signed imm16, LDC.64 latch, sub-word loads (broken skinned meshes) | translate.c, shared with BOTW (semantics now as hardware) |
| Diagnostics: per-draw texture log, page watch, big-read log, RomFS check, runner `--env` | opt-in |
No change to Cpu, dispatcher, lifter or the BOTW generated code (997 files unchanged by construction).
BOTW rechecked after every shared change; last run `claude-botw-06` ("Open your eyes..." frame 3100,
294k draws/interval).

**Recommendation.** Continue on Savage: the remaining defects are specific (one mesh, one race), not
architectural, and every blocker so far was a missing or stubbed NVN/translator feature. Next: diagnose
Zelda's mesh with the pick tool, fix the audio race, then run a real scene change (open world) and measure
performance on a display. The reference remains the playable build until then.

## Active scope and current result

The later user goal supersedes the bounded/diagnostic-only stops below: continue
with scoped fixes, builds and one startup per change until a rendered TotK menu
or world, a missing required input/design decision, or the same failure after
three distinct repair attempts. Cpu/dispatcher redesign requires a decision.
Historical conclusions below are retained as evidence, not the current stop rule
or recommendation. Work remains on `experiment/totk-support`; protected dumps,
keys, reference `totk-build/data` and the existing local raw RomFS remain untouched.

**Fix 01: VammManager initialization now passes.** Its constructor
`f_7004e3cf88` clears the manager's +0x40 pointer. Initialization
`f_7004e3d00c` queries GetInfo(16) and only initializes the backend and stores
`0x7005a344a8` there when that query succeeds with a nonzero value. The former
kernel always returned zero. TotK's NPDM declares SystemResourceSizeTotal
`0x1800000` at offset +0x14; boot now supplies that process budget to GetInfo(16).
BOTW retains the previous zero value. GetInfo(17) remains zero; resource-use
accounting is not implemented by this fix. No SDK HLE replacement was needed.

Runtime evidence: `build/totk-experiment/fix01-system-resource-headless/run.log`
records `AllocateAddressRegion`, x0=`0x7005a34430`, +0x40=`0x7005a344a8`,
guest tid=80, handle=0x101. The original SIGSEGV at 0xc0 is absent. Startup
initializes the NVN device/window, creates worker threads, and then stops at
`FATAL: no function at 700086344c (lr=713fe89473 ?+713fe89473)` after 1.910 s.
This address is an internal continuation in `f_70008631a0`; a local BR through
x19 incorrectly escapes to the global dispatcher. No TotK draw or menu has
yet been observed. This is the next repair, not a completed port.

Both initial fix-01 tests failed earlier in SDL because the desktop session
exposed no display outputs (Xwayland 0x0, no Wayland wl_output). Those runs do
not test VammManager. A separate Gamescope headless compositor exposes a real
1280x720 Vulkan surface on the RX 9070 without modifying desktop configuration:
`gamescope --backend headless -W 1280 -H 720 -w 1280 -h 720 -r 60 -- env
SDL_VIDEODRIVER=x11 python3 tools/totk_experiment_run.py ...`.

**BOTW after fix 01:** the separate `build/totk-baseline` build passes; all 997
files in `botw-entry-probe-gen` still match `baseline-gen` byte for byte. The
100-second run `fix01-botw-headless` renders the opening scene and "Open your
eyes…" at frame 3100 (screenshot visually inspected); renderer logs include
173447 draws in the interval ending at frame 2900. No fatal error was observed.
This verifies the opening, not later gameplay or physical display/input/audio.

Cost: runtime `kernel.c` (GetInfo, reset, budget setter), `kernel.h` (declaration),
`main.c` (boot NPDM parsing); runner `totk_experiment_run.py` permits `--seconds 0`
to remove the observation time bound. Two incremental builds, byte comparison,
one meaningful TotK startup and one BOTW opening run. Disk free before builds:
75 GiB. No generated-code, Cpu layout, dispatcher or reference-source change.

### Fix 02 verified: local ADR/BR continuations

The reference's `tools/recomp/gen.py:238-258` explicitly recognizes local ADR
continuations (including its measured `f_8631a0`). Savage's short straight-line
`computed_target` lookback misses x19 being set to `0x700086344c` across branches
and loops. `Module.scan` now supplements otherwise plain indirect BRs with
in-function ADR targets for the same register, **only with `--game totk`**. Known
jump tables and the existing all-instruction fallback retain priority. Unknown
targets retain the existing fallback. No global dispatch entry, Cpu field or
ABI change was needed.

The final TotK regeneration changes only `main/main_105.c` and
`main/main_395.c`; function counts and layout stay unchanged. A direct scan of
the input code with the option off/on confirms four newly local BR sites in two
functions: `f_700023b890` (BR `0x700023b940`, targets `0x700023b944`,
`0x700023bc2c`) and `f_70008631a0` (BR `0x70008634e4`, `0x700086351c`,
`0x7000863534`, targets `0x700086344c`, `0x7000863548`). The first trial
prioritized ADR over the existing computed-table path; that build was interrupted
before any game run, and the generator was refined to preserve that path. Its
seven stale main chunks (8,562,181 bytes, all inside our build directory) were
removed explicitly; reference artifacts and all game inputs remain untouched.

The focused test compiles the actual emitted C and compares its continuation
result against Unicorn, with controls for default-off behavior, register/range,
signed backward ADR, existing computed-table priority, and unknown external
dispatch. The 59 synthetic layout checks also pass. The final fresh BOTW
generation again has 997 byte-identical files; its build and 100-second
`fix02-botw-headless-verified` run pass the opening check (frame 3100 visually
inspected, "Open your eyes…", draw counts present). An earlier BOTW launch
rejected mismatched library build IDs after a generator edit during compilation;
metadata objects were rebuilt together before the verified run. This was not a
guest gameplay failure. TotK `fix02-adr-continuation-run` passes the old
continuation failure, starts three additional worker threads, and reaches 256
RomFS reads (62954 KB) before **FATAL: too many regions** after 2.595 s. No
TotK draw/menu is observed. The fixed continuation is accepted for this startup;
the kernel region table is the next blocker.

The final link reuses the earlier `main_755.o`: its input SHA256 equals the
pre-change manifest (`89cf192e2621995a25b4b07489310b0ee97b54cc53c6e301fe32aee715adfce3`),
and its 01:14 object postdates all Cpu/SIMD headers and unchanged build flags.
Only the source timestamp had changed through the discarded intermediate
generation. Its unnecessary compiler job was interrupted, the validated object
was retained, and final make/link completed with exit 0. Interrupted build logs
are separate from `fix02-totk-build.log`. Both library/runtime build IDs are
`76a24b23175e3da7`.

Cost so far: `tools/gen.py` (`Module.__init__`, `Module.scan`, `generate`, 19 added
lines), plus `tools/test_local_continuation.py`; two generated TotK chunks differ.
No instruction family, HLE replacement, Cpu or runtime behavior was added by
this generator fix. The generated chunks themselves are not committed.

### Fix 03 verified: physical-memory interval bookkeeping

The existing SVC trace (`--trace-svc` in the runner, runtime trace unchanged)
records 994 MapPhysicalMemory calls and 10 UnmapPhysicalMemory calls before the
1024-entry table fills. Replaying the actual map/unmap ranges with interval
coalescing/splitting yields **283 live intervals, maximum 283**, and no mapping
overlaps. The old table records one entry per map call and removes an entire
entry only when the unmap starts at exactly its base; it ignores the unmap size.
This is an observed bookkeeping failure, not evidence of a missing memory file
or a need to raise the limit.

For TotK physical mappings only, `region_add` coalesces adjacent ranges with
matching type/permissions. `physical_region_remove` trims, splits or removes
exactly the requested range; nonphysical mappings are preserved. BOTW retains
its existing add/remove paths. The limit remains **1024**. QueryMemory now sees
the resulting contiguous intervals. Host page release remains unchanged and is
not claimed as a full kernel-memory implementation.

`test_physical_regions.py` compiles the actual kernel bookkeeping section with
ASan/UBSan: the real 1004-call trace replay passes (283 intervals), together with
bridge/head/tail/middle removal, permission/type boundaries, 3000 adjacent maps,
and 10000 deterministic operations compared with a page oracle. Both incremental
builds pass; the unchanged generated BOTW set is 997 byte-identical files.
`fix03-botw-headless` again renders "Open your eyes…" at frame 3100, visually
verified, with renderer draw counts. Full progression/audio remain unverified.

TotK `fix03-physical-regions-run` passes the former table failure, reaches 512
RomFS reads (123276 KB), creates more threads and opens its audio renderer
(48000 Hz, 240 samples, 72 mixes, 96 voices, 16 effects, CPU/manual, result 0).
After 6.008 s it stops on **FATAL: guest symbol
`_ZN2nn5audio6server17AudioRenderSystem29ExecuteAudioRendererRenderingEv` not
found**. The BOTW-specific audio worker requests that absent SDK symbol. TotK
has a renderer-interface implementation instead; this is the next blocker.
No TotK draw/menu was observed. Concurrent shutdown logs also show the existing
`hwopus` logging stub returning success for command 5; decoding remains
unimplemented/unverified and is not claimed safe for later playback.

Cost: `runtime/kernel.c` (`region_add`, new `physical_region_remove`, SVC 0x2d),
`tools/totk_experiment_run.py` (opt-in existing SVC trace), and
`tools/test_physical_regions.py`. No generator/Cpu/dispatcher change; no limit
increase. Disk free before both builds: 75 GiB.

### Fix 04 verified past symbol lookup: TotK audio renderer interface

TotK does not export BOTW's AudioRenderSystem render method. Its public
`ExecuteAudioRendererRendering` (`f_7004ee9a50`) invokes renderer vtable +0x78;
the observed target `f_7004f16418` adjusts +0x10 to AudioRendererImpl, whose
`f_7004f158c4` adds +0x60 to reach AudioRenderSystem. Its calls to
GenerateCommand and CommandListProcessor::Setup/Process were resolved against
the SDK's actual JUMP_SLOT symbols. `AudioRenderSystem::Update`
(`f_7004f14c74`) locks system +0x1f0, confirmed by its Lock/Unlock PLT bindings.
The existing worker now uses this interface and matching mutex for TotK;
BOTW retains its original direct method and system +0x2d0 mutex.

Both builds pass; BOTW's 997 generated files remain identical and
`fix04-botw-headless` again visibly renders "Open your eyes…" at frame 3100
with draw counts in its 100-second log. Cost: one source file,
`runtime/services/audio.c`, one changed function `sdk_render_thread` plus the
profile include; no generator/ABI change. SDK/C hardware audio playback and
DeviceSink format correctness for TotK remain **UNVERIFIED**.

TotK `fix04-audio-interface-run` logs renderer target `0x7004f16418` and mutex
`0x7005a67360`, passes the absent-symbol boundary, mounts its isolated save and
starts more workers. The next failure is SIGSEGV reading address 0 in
`f_7004deb36c+0x372`, guest LR `0x7004deb43c`, after the existing `fsp-srv`
command 62 default returned success without an object. Causal IPC decoding is
under investigation. The process exits -6; reported elapsed 143.949 s includes
crash/core handling and is not a boot-to-menu time. No TotK frame/menu yet.
Subsequent isolated launches disable core-file output (`ulimit -c 0`) while
preserving the runtime's text crash diagnostics.

### Fix 05 verified: cache-storage info-reader object

Command 62 is OpenSaveDataInfoReaderOnlyCacheStorage, returning an
ISaveDataInfoReader. The SDK's `f_7004deb36c` zeroes its reader output pointer,
calls the proxy method, and on success dereferences that reader's vtable. The
old fsp handler returned success without an object. TotK now receives a reader
for the isolated namespace's empty cache inventory; command 0 returns a 64-bit
zero count. This matches the [ISaveDataInfoReader wire contract in libnx](https://github.com/switchbrew/libnx/blob/master/nx/source/services/fs.c)
and the [service command definitions](https://switchbrew.org/wiki/Filesystem_services#ISaveDataInfoReader).
Persistent/cache-registration metadata is not implemented by this bounded
reader and remains a limitation; no existing user cache is claimed supported.
BOTW command 62 retains its old behavior.

`fix05-cache-reader-run` logs the returned object and reader count, passes the
null-reader crash and stops prematurely on a valid OpenFile PathNotFound.
A separate read-only strace (`fix05-open-files.trace`) identifies the missing
path as `state/save/save_0/storage/CacheStorageKey.dat`, expected absent in the
new test namespace. The runner now retains and logs PathNotFound for host
IFileSystem GetEntryType/OpenFile/OpenDirectory without stopping; the actual
guest-visible response is unchanged. The guest demonstrably handles the absent
key and proceeds. Required missing-resource failures remain guest errors, not
fabricated successful file opens.

`fix05-cache-reader-observe` then reaches **NVN
CommandBufferSetRenderEnableConditional not implemented** after 7.544 s. No
TotK renderer frame/draw summary or screenshot was produced. The earlier
crash and audio symbol failure are absent. Additional preceding generic
fallbacks in this longer run: acc:u0 160, aoc:u 50, hwopus 5 (12 calls),
IApplicationFunctions 27, IAudioController 0/1/2, prepo:u 10105 (7 calls),
IStaticService 400, ISelfController 62, plus the eleven earlier gaps. These are
preserved in the full transcript; their later semantic adequacy is unverified.

Both builds pass. `fix05-botw-headless` still renders "Open your eyes…" at
frame 3100 (visually checked), with draw summaries in its 100-second run. The
997 generated BOTW files remain byte-identical. Cost: `runtime/services/fs.c`
(new reader handler, fsp command 62), runner negative-poll classification, this
report. No generator/Cpu changes. 75 GiB free before builds.

### Fix 06: conditional NVN command recorded; first TotK image observed

TotK's SetRenderEnableConditional now records mode/address using the reference's
encoding (2/3 = equal/not equal, ordinary SetRenderEnable stays 0/1). The Vulkan
executor supports that predicate for TotK and suppresses draw/clear operations
when false; ordinary boolean enable is respected. It completes earlier GPU
work and applies pending counter readbacks before loading the two 64-bit values
at +0 and +16. The reference itself warns that unsynchronized loads may see
stale values. No no-op or assumed-success render condition was introduced.
BOTW retains the prior command behavior. The new render state resets on reboot.

Both builds pass; all 997 generated BOTW files remain identical.
`fix06-botw-headless` again visibly renders "Open your eyes…" at frame 3100,
with draw summaries during the 100-second observation. Cost: `nvn/cmdbuf.c`
(conditional recorder), `nvn/vulkan/exec.c` (exec_list and vk_exec_reset);
no header, Cpu or generator changes. Predicate values/skipped operations have
**not yet been observed in execution**; this run verifies passing the recording
boundary, not conditional rendering throughout the game. The added GPU wait's
performance cost has not been measured.

**First TotK rendering evidence:** `fix06-conditional-render-run`, screenshot
`state/build/frames/window_00060.png`, visibly shows **Nintendo Presents**. The
renderer logs frame 63 at t=8.2, with **101 draws, 126 passes, six shader
compilations** in the interval. This is a rendered startup screen, not menu or
world acceptance. The run stops at 11.366 s on **ReadTexels not implemented**;
concurrent logs show partial WriteTexels missing for texture #214, 5120x5120,
level 0, regions 1024x64x1. CPU texture-region I/O is the next repair. Protected
inputs remain untouched; 75 GiB free before both builds.

### Session 2 (Claude): texel copy, region table, rasterizer discard, hwopus

Commits after fix 06: `e016bb5` (helper `totk_texels_2d`; `tools/test_texture_region.py`
passes under ASan/UBSan against an independent layout oracle), region table
1024 -> 65536 entries (observed 954 live disjoint physical intervals, smallest one
page, ~2.9 GB mapped: the limit was only the table size), TotK
`SetRasterizerDiscard` (draws/clears skipped while enabled; the reference only logs
it), and `runtime/services/hwopus.c` (libopus decode of the SDK's hwopus service;
BOTW keeps the logging stub; `-lopus` added on Linux when `opus.h` exists).

**Observed (headless gamescope, `claude-06`, 300 s, no fatal error, no crash):**
- `window_00060.png` "Nintendo Presents"; `claude-03/.../window_00060.png` same.
- `claude-04/.../window_00600.png` title text "Tears of the Kingdom".
- `claude-05/.../window_01200.png` opening-cutscene subtitle "This strange gloom keeps
  getting thicker...", 1889 frames, up to ~60k draws per 2 s interval.
- `claude-06/.../window_04500.png` **gameplay HUD**: hearts and the "Move / Controls /
  Camera" hints, frame ~4500 (~290 s). So the guest left the intro and entered the
  world state with `SAVAGEWIND_NO_USER_INPUT`.

**Not working:** the 3D scene is black in every captured frame (mean brightness 1/255;
only HUD/text pixels differ). Draw calls are issued and executed (not skipped), so the
fault is in scene rendering (deferred/G-buffer passes, shaders, texture contents or
GPU-owned texel readback) rather than in guest progress. Not diagnosed. Hearts show 30
(unverified whether that is correct for a new save). No title menu was observed because
a new save goes straight to the intro. Frame rate here is 13-30 fps with a software
chain of ~2 GB uploads per interval; no performance conclusion.

**Not exercised:** the GPU-owned partial ReadTexels/WriteTexels path (`needs image
readback` FATAL never fired), the "[compute] translate failed: unknown instruction 0"
log seen once before the hwopus fix, audio output correctness (hwopus decodes; sink
unverified), 32-bit/multistream Opus, saving.

**BOTW:** no generator, lifter or Cpu change in this session, so the 997 generated
files are unchanged by construction (not re-diffed). BOTW rebuilt; `claude-botw-01`
renders "Open your eyes..." at frame 3100 with 295k draws/interval.

Cost: kernel.c (1 constant), cmdbuf.c and exec.c (rasterizer discard, TotK-gated),
objects.c (helper), 1 new service file, Makefile link flag. No Cpu/dispatcher change.

**Recommendation:** next target is the black 3D scene. Start by dumping a G-buffer
attachment and the first scene pass with `SAVAGEWIND` trace tools, and compare shader
compile/draw state with the reference. The reference remains the playable option.

### Session 2, part 2: the black 3D scene (diagnosed and fixed)

Pass dumps of a gameplay frame (`SAVAGEWIND_DEBUG_FRAME`, `claude-17`) showed the
G-buffer (normals, Link and the cave) rendered correctly and the lit result almost
black, so lighting was missing, not geometry. Two causes, both in Maxwell shader
translation, both observed in `translate failed` logs:

1. **TotK compute binaries** carry a 0x100-byte header (magic 0x12345679); Savage
   started decoding at byte 0, so every compute translation failed at the first
   instruction (stats showed `0 dispatches`). Fixed (`translate.c`, same rule as the
   reference); dispatches then ran (~100-400 per interval).
2. **Missing instructions** in lighting fragment/compute shaders: VOTE (0x50d8),
   LEA lo/hi (0x36d0, 0x1800), VMNMX (0x3a), RED (0xebf8), ATOM (0xed). Ported from
   the reference without its device-memory window; subgroup ballot/vote extensions
   added to the GLSL prelude.

**Observed:** `claude-18/.../window_02000.png` shows the 3D world with Link (sword on
back), cave geometry and the HUD (mean brightness ~100 instead of ~1). Before: black.

**Still wrong / open:** the whole image is heavily tinted magenta (G channel near 0);
may be the intended gloom fog overexposed, not verified. One vertex shader still fails:
DMUL (0x5c80, FP64); the reference needs F64 support (DADD/DMUL/DFMA, F2F.F64) for 32
"material" vertex shaders; not ported. Speed dropped to ~3-10 fps while compute
pipelines compile. BOTW rechecked after these changes (`claude-botw-02`,
"Open your eyes..." at frame 3100, 296k draws/interval).

### Session 2, part 3: magenta world (diagnosed to the data, not fixed)

Frames over 420 s (`claude-28`) show the cave and Link standing with the dialogue box
("Link! Has something caught your attention?") but the geometry is saturated magenta,
and it does not clear with time.

Trace (`SAVAGEWIND_DEBUG_PICK`, `LOG_FS_CB`, `DEBUG_TEXDUMP*`, the new opt-in
`SAVAGEWIND_LOG_DRAW_TEX`): the G-buffer albedo is magenta where the terrain shader
`59531823a3265cec` draws. That shader samples ASTC 8x8 texture arrays #1162 (512x512)
and #1161 (1024x1024), 121 layers each, at a layer taken from a material table (cbuffer
16, populated and sane: layers 84, 93, 113 ...). Only **layer 0 holds data**; layers
1..120 are all zero in guest memory (checked at upload time up to frame 300 and by a
memory watch on layer 1 for 120 s), and an all-zero ASTC block decodes as the error
colour, magenta. Nothing writes those layers: no WriteTexels, no buffer/texture copy,
no GPU surface store (neither translator supports SUST). It is not the ASTC decoder (identical
to the reference's), the upload hashing (a larger full-hash budget changed nothing,
reverted), or the 5120x5120 mask atlas #214 (only its first 1024x1024 tile is ever
written, by design of the observed 1024x64 copies).

Worker threads are idle on arbiters (`SAVAGEWIND_THREAD_DUMP_AT`), RomFS reads stop at
2048 reads / 296 MB, and the only missing paths are save files (new game). So the game
does not request the layers in this run. **Open question:** what fills these layers in
the reference (streaming that depends on something Savage does not provide?). Next step:
run the reference once on the same scene with a watch on its layer memory.

Also observed: an intermittent (about 1 start in 8) SIGSEGV at address 0 in SDK audio
mixing `f_7004f31b50`, during early boot; not investigated. Still unfixed: FP64 (DMUL) in one
vertex shader. BOTW rechecked (`claude-botw-03`: "Open your eyes..." frame 3100).

### Session 2, part 4: reference comparison of the magenta terrain (stopped after three attempts)

With permission the reference was run once (its own `run.sh`, state and screenshots in
`build/totk-experiment/ref-0*`; no reference file modified). On a clean save it plays the same
opening and renders the same cave **correctly textured** (`ref-02`), so this is a Savage fault.

Findings:
- The two arrays (#1161, #1162, 121 layers) are filled completely in the reference before their first
  use (frame 146: 41.2 of 42.6 MB nonzero, `LARECOMP_TEX_WATCH_FMT`). In Savage layer 0 only.
- Their source is `TexToGo/MaterialAlb.txtg` (36.7 MB) and `MaterialCmb.txtg` (9.9 MB), a packed
  format (magic `6PK0`). Savage reads both **completely and correctly** (`[fs] big read`, full sizes),
  into buffers at `7154001000` / `7156401000`; the raw image matches the source for all 304207
  files (`tools/check_romfs_image.py`).
- A page watchpoint on the MaterialCmb buffer (`SAVAGEWIND_WATCH_PAGE`) shows the guest **never reads
  the buffer after the read** in 30 s: the unpack step does not start. All other threads idle on arbiters.
  This looks like a lost wake-up or an unsatisfied condition between the loader and the unpacking
  job, not a data or graphics problem.

Tried without effect (three distinct attempts): larger full-hash budget for big textures; keeping the
contents of already-mapped pages in MapPhysicalMemory (no overlap ever occurs); serving the reference's
RomFS overlay (`SAVAGEWIND_ROMFS_OVERLAY`, replaces RESTBL and two files). All three reverted or
shown unnecessary (the overlay is only an env var).

New debugging aids: `tools/check_romfs_image.py`, `SAVAGEWIND_WATCH_PAGE=<addr>:<len>` (+
`_DELAY`, with `SAVAGEWIND_WATCH_WRITES`), `[fs] big read` and `GetEntryType` missing-path logs for TotK.
BOTW rechecked (`claude-botw-04`, 294k draws/interval).

### Session 2, part 5: the magenta terrain is fixed (`nvnTextureGetViewOffset`)

Cause: Savage's `nvnTextureGetViewOffset` was a stub returning 0. TotK derives the destination of every
layer and mip of its material arrays (`TexToGo/MaterialAlb.txtg`, `MaterialCmb.txtg`, zstd-packed) from it:
`f_70026235f0` loops over layers/mips and calls the zstd decompressor (`f_7000860b40`, identified by its
`-N` error codes and the `0x78` threshold) with the destination `base + ViewOffset(layer, mip)`. With 0,
all 121 layers were decompressed over the first layer (logged: identical `dst 728591e000` for every call),
layers 1..120 stayed zero, and an all-zero ASTC block decodes as magenta. It was not the file read, the
RomFS, thread scheduling or a lifted instruction. Found by: reference comparison (arrays full at first use),
a page watch showing the unpack loop running, and a temporary log of the decompress calls (an edit of a local
generated chunk, since restored; nothing generated is committed).

Fix: implement it as the reference does (layer * layer size + level offset), TotK only; BOTW keeps 0.

**Observed after the fix** (`claude-60`, 150 s, headless): textured cave walls, torch light, Link, Zelda
with the "Zelda, Princess of Hyrule" caption, then gameplay with HUD hearts and control hints at frames
2500 and 3000 (`state/build/frames/window_0{1500,2000,2500,3000}.png`). BOTW rechecked
(`claude-botw-05`: "Open your eyes..." at frame 3100, 296k draws/interval).

**Still wrong:** overbright white streaks near the debris at the right of the gameplay frame; the face
of Zelda in the close-up looks patterned (a texture probably still missing); one vertex shader needs FP64
(DMUL, not ported); an intermittent crash (about 1 start in 12) in SDK audio mixing `f_7004f31b50` early in
boot; no title menu (a clean save goes straight to the intro; the reference shows it only once a save exists).
Frame rate 10-30 fps headless; unmeasured otherwise.

## Render size and code patch sets (session 3)

Scaling the 3D scene's render targets from the host (what BOTW does) does not suit TotK: its shaders see the guest's
1600x900, so the lighting came out 10-40% darker the further the scene was from that (frame brightness 32.8 at the
game's size, 29.4 at the window's size, 20-23 with FSR 1 modes). The game has to render the scene at the wanted size
itself, so the generator now supports **instruction-word patch sets** (`tools/wordpatches.py`, `gen.py --patches`; the
format, the idea and the TotK sites come from totk-pc-recomp's `recomp/patches.py` and `patches-v121.txt` (MIT), which took
them from MacNativeGames' `game_patches.c` (MIT); the 1.0.0 offsets are MacNativeGames' `game_version.h` mapping, and every
original word was checked against this module: 25 of 26 matched, the last was the `bl` word its table lists for 1.0.0).
At a site the generated C reads `if (rt_patch_on[set]) { replacement } else { original }`; `runtime/patches.c` owns the
switches (`SAVAGEWIND_PATCH_<NAME>`) and the patch page (guest `0x707ff00000`, within ADRP's reach of main).

Generation command for TotK now:

```sh
PYTHONDONTWRITEBYTECODE=1 tools/venv/bin/python tools/gen.py all ../totk-recomp/totk-build/data/exefs \
  build/totk-experiment/totk-gen --game totk --simd c --hle runtime/hle/hle_symbols.txt \
  --patches tools/patches-totk-100.txt
```

`tools/patches-totk-100.txt` has one set so far, `scene`: the renderer's `+0xcc4/+0xcc8` read two floats from the page
(`+16`) instead of 1600x900. `output_scale.c` writes the size the Video settings ask for (window size for Upscaler Off,
divided by the FSR 1 mode's factor) and switches the set on when it differs. Measured in the cave scene at a 1280x720
window: scene 640x360 (Performance) brightness 33.0, 852x480 (Quality) 32.9, against 32.8 at the game's size; BOTW
unchanged (298k draws/interval, "Open your eyes..."). Not done: the final-image size (the host still scales
that) and the dynamic resolution controller (its scale is still the game's).

**Scenes above 1600x900** (session 3): the game itself allocates its scene targets from its texture heap, so a bigger scene needs
that heap grown (BOTW doesn't: the host makes bigger images and the guest keeps its 1600x900 constants, which BOTW's
shaders tolerate; TotK's lighting came out darker that way, see above). `heapdef_patch` (`runtime/services/fs.c`) serves
`System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs` from memory with `DynamicTextureAllocatorMem` (shipped 113664 KiB) and its
parent `Graphics` (201728 KiB) grown by 113664 KiB x (scene pixels / 1600x900 - 1), as totk-pc-recomp's `resolution.c` does,
and `svc GetInfo` (`kernel.c`) reports the same growth in the application memory, because the game sizes its heap from that
before it reads any file (`SetHeapSize` 2140 -> 2670 MB). The cap is `SAVAGEWIND_SCENE_MAX=<w>x<h>` (default 3840x2160, +529 MiB;
`1600x900`: no growth, scene capped at 1600x900); the scene size follows the window and Upscaler setting up to it.
Measured headless at 1280x720 with `SAVAGEWIND_SCALE`: 1920x1080 scene 60 fps, 3840x2160 scene about 50 fps, both boot and
render correctly (4K: sharp, lit as at the shipped size). Needs libzstd (`-lzstd`). The heap is GPU-backed, so the default
costs about 530 MB more of it even at 1600x900; set a smaller `SAVAGEWIND_SCENE_MAX` to save it.

**60 fps** (sets `fps60`, `realtime`, `bounce`, `cloth`; `runtime/totk_frame.c`): the present-interval controller's minimum goes from 2
to 1, the game's step comes from the page (average of the last 5 display intervals, at most 1/15 s) instead of whole vsyncs, and
the pacer holds the limit (Video > Frame rate: 30 or 60). Headless: 60.0 fps for 100 s; at 80 s of wall time the game is in
the same place (Link at the cave entrance, controllable) as at 30 fps, so its speed is right. Higher rates are not offered. The `cloth` set (hair and cloth clock: three inlined places and one call, sites and
formulas from MacNativeGames' per-version table; the 1.0.0 words differ from the 1.2.1 ones where the factor is stored with
`stp`) is on with realtime; all seven original words match. Its effect on the hair and cloak speed is not measured: a
frame-difference test (1.82 on, 1.72 off) cannot separate it from the character's own motion.

## Historical bounded feasibility phase

Later, separately authorized diagnostics are recorded in
[VammManager entry probe](totk-vamm-entry-probe.md). That task used an already
existing local raw RomFS image, ran TotK once, and observed AllocateAddressRegion
with a zero +0x40 field immediately before SIGSEGV. It added no behavioral repair;
the result below describes the earlier bounded feasibility phase.

## Earlier result — one additional iteration reached a RomFS input failure

**TotK 1.0.0 was generated, compiled, linked, loaded and started on SavageWind's
Cpu runtime.** After the user revised the stop rule and allowed **one additional
iteration**, startup passed command 900, entered subsdk0/main initialization and
initialized the host Vulkan device and GPU-backed heap. It then stopped on
**FATAL: missing `totk-build/data/romfs.bin`** after 1.685 s. The reference has a
RomFS **directory**; Savage's storage interface requires a raw image. No menu,
TotK NVN device initialization or game frame was observed. Playability and graphics compatibility
remain **UNVERIFIED**; this is not evidence that TotK cannot proceed further.

**Recommendation: retain totk-recomp for TotK rather than undertake a full
migration to SavageWind.** The executed experiment proves compiler/loader/early
SDK/host-device feasibility, but provides no rendering or gameplay evidence to justify the
larger migration. The reference was described as working by the user; its runtime
was not independently rerun here. The positive Savage result is enough to justify
only a separately authorized, bounded continuation, not a finished port or FPS claim.

BOTW regression evidence: **997 generated files are byte-identical to the original
baseline; the full build passes; the opening scene/subtitle was visually confirmed
before changes, after game-profile isolation and again after the pair extension.**
Full progression, physical controls/audio and other platforms were not tested.

## Original scope and evidence rules (superseded above)

Started 2026-10-30. This is a feasibility experiment, not a port. The initial
stop was the first missing service/graphics behavior or menu; the latest user
override allowed exactly one further observation to a **blocking** failure or
menu, tolerating logged fallbacks. That iteration is now exhausted. Source
comparisons are not runtime acceptance. BOTW gameplay and
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
   the applicable user stop rule; the last allowed observation ended at the
   blocking missing-RomFS-image error, and no more runs are authorized.
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
- Baseline full build: **PASS**. TotK generation/link/loading/early SDK startup:
  **PASS to the final missing-RomFS-image stop**. Unsupported SDK instructions and the
  untested graphics/gameplay boundary are detailed below.

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
function is semantically correct or reachable. At this initial pre-pair stage,
the SDK's 277 unsupported cases were potential runtime traps; the later pair
extension reduces that set to the 100 guarded LSE PCs.

Their generated C contains **277 unique guest PCs**, duplicated into 434 emitted
`rt_udf` calls: 197 PCs tagged `exclusive pair / CAS` and 80 tagged `ldst reg
atomic/pac`. These tags do not identify the exact instructions or prove reachability.
No instruction implementation has been added speculatively.

Build attempt: `make -j4 B=build/totk-run GEN=build/totk-experiment/totk-gen`,
with output in `build/totk-experiment/totk-build.log` (initial attempt interrupted
by the temporary user stop; the completed post-pair attempt is recorded below).

`tools/totk_experiment_run.py` runs one attempt under a pty, preserving a transcript
and JSON result in a new subdirectory of `build/totk-experiment`. It clears inherited
SavageWind debug switches and isolates config, cache and saves. It terminates at
the first logged missing behavior/fatal error or observation timeout; it never
retries or supplies replacement service results. A negative-control run already
verified rejection of a BOTW library. An early trap still needs classification:
it is not automatically evidence of a graphics/service blocker.

## Cost before the pair extension

The initial profile/observation changes span **17 files**: 12 runtime files,
3 tools, Makefile and this document. There are 24 added/changed runtime functions
including the inline profile predicate; most changes are one guard at an existing
function boundary. Generator changes are limited to profile selection in
`main_sites` and its CLI option. The game library ABI is unchanged.

**Before the revised pair scope: 0 changed Cpu/Ctx/dispatcher/lifter/SIMD files and
0 changed functions in that contract.** NSOs were regenerated rather than linking
the reference's incompatible Ctx-generated code. No CPU/GPU performance delta
was measured. The final extension cost is recorded below.

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

Additional executed check: loaded the untouched SDK image (including its zero
BSS byte) into Unicorn and called **all 100 outline helper entries**. All returned
through LL/SC fallbacks; an instruction hook observed **zero LSE instructions**.
An emulator-only positive control changed the byte to 1 and reached CASB at
0x700527ce5c, confirming the guard rather than an accidental skipped test.
The actual game files and runtime flag were never changed. Evidence:
`build/totk-experiment/outline-guard-emulation.log`. This establishes conditional
reachability in the emulator, not a physical-console measurement or all-game trace.

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
Full post-pair BOTW and TotK builds **PASS**. BOTW: exit 0, 294.671 s; its 35/59
sites/layout checks pass. The final post-pair BOTW frame-3100 screenshot again
shows “Open your eyes…” (`build/totk-experiment/botw-pair-state/build/frames/`).
The bounded visual run ended at its external timeout, not a normal game exit.

## Completed TotK build and first observation (earlier stop rule)

The post-pair build produced `build/totk-run/savagewind` (10295608 bytes),
`swgame.so` (417456168 bytes) and `.id` **78ef8dfbf61dd9f0**. Build log:
`build/totk-experiment/totk-pair-build.log`. The elapsed time is approximately
17m31s from the log's filesystem birth timestamp to the final ID file; this
includes scheduling alongside the BOTW build and is not a performance benchmark.
The coordinator independently checked the completed outputs and reran the exact
make command: **exit 0, nothing to do**.
Actual runtime `dlopen` then successfully resolved the game library's host symbols.

One startup attempt was executed:

```sh
PYTHONDONTWRITEBYTECODE=1 tools/venv/bin/python tools/totk_experiment_run.py \
  --exe build/totk-run/savagewind --library build/totk-run/swgame.so \
  --data ../totk-recomp/totk-build/data \
  --out build/totk-experiment/totk-start-01 --seconds 60
```

Use a **new** output directory for any future authorized attempt; the runner
refuses to overwrite evidence. No repeat was performed under the earlier rule;
the single subsequently authorized attempt is recorded below.

Authoritative transcript and result:
`build/totk-experiment/totk-start-01/run.log` and `result.json`.

- Loaded all three modules and accepted their recorded build IDs.
- Relocated main, subsdk0 and sdk. The existing loader reported 47 / 2 / 0 imports
  bound to host stubs; that count is a binding category, not proof of missing
  implementations. No relocation error was observed in this startup.
- Observed the outline-atomics byte at **0x7005ae4780 = 0**, read only, after loading.
- Created the main thread for program **0100f2c0115b6000**, found
  `nn::init::Start` at **0x7004e09060**, and entered SDK DT_INIT / constructors.
- Opened `lm`, `apm` and `appletOE`, then logged the first missing operation:

```text
[am] ICommonStateGetter cmd 900 (unhandled, ok; logged once)
```

The runner returned **3** (`first_missing_behavior`) and deliberately terminated
the child with **SIGTERM**, recorded as `child_exit: -15`; elapsed **1.327 s**.
This is an intentional research stop, **not a demonstrated fatal game failure**.
The same buffered read also contains `ISelfController cmd 91`; it was already
queued before termination. Neither command was implemented and the game was not
resumed. No `rt_udf`, missing function, graphics failure or menu was reached.

### Concrete service gap and reference comparison

`runtime/services/am.c:65-92` has no command 900 case, so it falls through to
`unhandled()` (`:52-61`). IPC initializes the response to zero
(`runtime/ipc.c:114-116`), making the default response successful without
implementing the command's state change. This behavior predates the experiment.

The libnx implementation identifies command 900 as
**SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled**, a no-input/no-output
operation; it is not SetCpuBoostMode. Its ISelfController command 91 obtains
the accumulated-suspended-tick change event. These protocol names are sourced
from [libnx applet.c](https://raw.githubusercontent.com/switchbrew/libnx/master/nx/source/services/applet.c),
observed 2026-10-30; they are not guessed from the numeric IDs.

The reference has **no corresponding command-900 IPC implementation to copy**.
Its boot sequence (`reference:runtime/rt_main.cpp:599-624`) calls main's DT_INIT,
`nninitStartup`, main's init array and `nnMain` directly, rather than invoking
SDK `nn::init::Start` and SDK constructors as Savage does. The reference's
`runtime/hle_oe.cpp:16-23` supplies operation/performance/focus/notification HLE,
and its import dispatch (`runtime/rt_main.cpp:155-166`) supplies HLE or a logged
zero stub. This explains the different service boundary in source; it is not
evidence of a valid one-line semantic fix for Savage. No such bypass was added.

## Final cost, blockers and limits

Total source delta against 860f372: **22 files** (14 runtime, 6 tools, Makefile,
one report). No generated game code, game assets or keys are tracked. The original
`main` remains at d712942; reference source WIP and all cleanup candidates remain.
Freed space: **0 bytes**, following the user's decision against deletion.

Pair/Cpu cost: **2 production files, 2 new inline helpers, 1 modified decoder
function, 1 extra monitor value in padding; 2 test files**. Existing Cpu offsets,
size, packed flags, dispatcher and GameLib ABI are preserved. This extends Cpu's
monitor state; it is not a Ctx-to-Cpu adapter. No game-speed delta was measured.

| Item | Evidence/status | Further effort estimate |
| --- | --- | --- |
| NSO generation / host linking / module relocation | Executed successfully to early SDK startup | Completed for this build only |
| Four 64-bit exclusive-pair instructions | 177 PCs supported; 8 Unicorn sequences and concurrent host tests pass | Completed within the existing CAS-monitor approximation |
| LSE variants | 100 guarded PCs remain traps; initial zero flag and 100 LL/SC fallback calls observed | Small-to-medium if a future executed path enables them; unnecessary for the observed start |
| ICommonStateGetter command 900 | Initially triggered the old stop rule; tolerated in the one authorized continuation | No implementation added; wire-compatible empty success for this bounded startup, full next-program semantics unverified |
| RomFS input representation | **Final actual blocker:** raw `romfs.bin` absent; reference supplies a directory | Small for separately authorized image preparation, medium for directory-backed storage adaptation |
| Other SDK/IPC compatibility | One continuation passed 11 logged fallbacks and reached RomFS opening; later semantics unverified | Medium-to-large, scope unknown |
| TotK NVN / shaders / synchronization | Source inventory only; no TotK device, shader, draw or frame executed | Large / high uncertainty; not a confirmed runtime graphics failure |
| TotK installer / configuration / controls | Source CLI experiment only; BOTW installer excluded | Medium for product integration; outside this experiment |
| BOTW | Generation byte equality, full build and opening scene verified | Full progression / physical input / audio remain unverified |

The earlier stop was the first logged service gap. A later user instruction
authorized exactly one additional observation with a stricter blocking criterion,
documented next. The experiment establishes
that the generator/Cpu contract is not the first hard barrier for this image;
it does not establish complete TotK support on SavageWind or disprove it.

## One additional iteration — blocking criterion, limit exhausted

The user changed the stop rule after the first observation: confirm command 900,
permit nonblocking fallbacks, and stop on an actual blocking failure or menu.
Only **one** additional TotK process was started. No third attempt was made.

### Why command 900 was permitted

The SDK's exported CMIF method at **0x7004ebfbb4** has the suffix
`SetRequestExitToLibraryAppletAtExecuteNextProgramEnabledEv`; its thunk passes
command **0x384 (900)** with no method arguments. The libnx contract cited above
uses a **no-input/no-output** IPC command. The existing Savage default therefore
returns a correctly shaped empty success result: no fabricated data or handles
are needed for this call. Its future effect concerns leaving to a library applet
during a next-program transition, not data required to construct the startup state.
The reference boot bypasses this SDK initialization sequence entirely.

Conclusion: retaining this success fallback was justified for the **bounded,
single-program startup observation**. Full program-transition behavior is not
implemented or declared safe. The additional run empirically passed the call.
No command-900 handler or state bypass was added.

### What changed for the observation

- `runtime/ipc.c`: a TotK-only, readonly diagnostic logs nonzero CMIF response
  results with interface, command and caller LR. It does not alter any response.
- `tools/totk_experiment_run.py`: optional `--policy blocking` records earlier
  fallbacks, stops on fatal errors, missing NVN implementations, non-normal IPC
  errors or abnormal process exit. Normal no-message/no-launch-parameter poll
  results remain logged. Optional frame captures support menu observation.
- No service implementation, CPU instruction, graphics feature or game data was
  added/changed for this extra iteration. BOTW's execution behavior is unchanged.
- Both runtime builds after the diagnostic change completed with exit 0; logs
  `blocking-policy-build.log` and `botw-ipc-trace-build.log`.

Executed command, once:

```sh
PYTHONDONTWRITEBYTECODE=1 tools/venv/bin/python tools/totk_experiment_run.py \
  --exe build/totk-run/savagewind --library build/totk-run/swgame.so \
  --data ../totk-recomp/totk-build/data \
  --out build/totk-experiment/totk-start-02 \
  --policy blocking --shots 60,300,600,1200 --seconds 60
```

### All preceding logged fallback gaps

The transcript is authoritative: `build/totk-experiment/totk-start-02/run.log`.
It contains these **11** distinct fallback gaps before the fatal error, in order:

| # | Interface / command | Existing fallback |
| ---: | --- | --- |
| 1 | ICommonStateGetter 900 | Unhandled, empty success |
| 2 | ISelfController 91 | Unhandled, empty success |
| 3 | IWindowController 10 | Unhandled, empty success |
| 4 | ISelfController 13 | Unhandled, empty success |
| 5 | ISelfController 16 | Unhandled, empty success |
| 6 | Service `ectx:aw` | Logging-stub session; no failing command observed |
| 7 | IParentalControlService 1 | Default success with 16 zero bytes (`runtime/services/misc.c:24`) |
| 8 | ICommonStateGetter 66 | Unhandled, empty success |
| 9 | ISelfController 14 | Unhandled, empty success |
| 10 | ISelfController 11 | Unhandled, empty success |
| 11 | ISelfController 12 | Unhandled, empty success |

`result.json` records the 10 explicitly marked missing/unhandled entries; the
source audit adds the generic `(ok)` parental-control default, which is also in
the original transcript. Existing log-once behavior means this is a list of
distinct logged gaps, not a count of every repeated invocation. Their presence
did not stop this particular startup; later semantic correctness is unverified.
No nonzero CMIF response was logged in this run.

### Actual blocking failure

```text
FATAL: cannot open /mnt/Dane/Development/totk-recomp/totk-build/data/romfs.bin (set up the game files on the start screen)
```

`runtime/services/fs.c:586-591` appends `.bin` to `g_romfs_dir` and opens that path
read-only, failing fatally when it is absent. The supplied reference data has
`romfs/` and **no `romfs.bin`**. The reference instead mounts `kRomDir` directly
through HLE (`reference:runtime/hle_fs.cpp:32-43,222`). This is a verified **input
representation mismatch**, not a demonstrated NVN/shader failure or CPU trap.

Before this failure, the log shows SDK initialization passing the earlier
fallbacks, a host Vulkan device/swapchain on RADV, a **2140 MB GPU-backed guest
heap**, and entry into subsdk0/main DT_INIT and constructor processing. This does
not prove guest graphics execution: no TotK NVN initialization, shader, draw,
frame or menu was observed. The outline-atomics flag was again **0** after loading.

The runner returned **3**, `reason: first_blocking_behavior`, elapsed **1.685 s**.
The child exit was **0** after coordinated termination; it does not override the
recorded fatal error or establish a successful game exit. No screenshot was
produced. Evidence: `totk-start-02/run.log` and `result.json`.

Current first blocker: **RomFS input compatibility**. Preparing a separate raw
image with the existing image builder is a small input-preparation task but
would require permission under the dump-copying restriction and storage space;
adapting storage to a directory is a medium code task. Neither was done. What
would happen after supplying that image is **unverified**. Service/graphics
continuation was not attempted because the one-additional-iteration limit is
exhausted. Recommendation remains to use the reference for TotK rather than
claim a completed Savage port; the compiler/early-runtime feasibility result is
positive and the later rendering risk remains unmeasured.

## Crashes under host CPU load (session 3)

Reported: the audio crash comes more often while another game runs. Reproduced with 16 busy loops on 16 cores (TotK
headless, 22-30 s runs): without load none crashed in 10+ runs; with load roughly every second to third run died after
16-23 s. There are **two different** crashes, both a read of address 0.

**1. Audio (`f_7004f31b50` = `ApplyLightLimiterEffect`, sometimes `ApplyI3dl2ReverbEffect4ch`): found and fixed.**
An effect's state advances with each `AudioRenderSystem::Update`: the Update that enables a new effect marks it new (state
byte 0 in the command, +0x70), the command that runs it in the first frame afterwards initialises its work state (pointers
into the effect's work buffer, `f_7004f31a50`), and the next Update moves it to running (2). The game sends some Updates
0.1 ms apart (two RequestUpdate calls in a row). On the console the DSP's 5 ms frames fall between Updates; here our render
thread had to be scheduled in between, and with the host's CPU busy it was not: both Updates came before any frame, the first
frame that had the limiter already carried state 2, the initialisation never ran, and `Apply` read the null pointers of its
zeroed state (traced: the first generated list with the limiter command had state 2 in every crashed run and 0 in every good
one). `sdk_update` (audio.c) now hooks `AudioRenderSystem::Update`: when no frame was rendered since the last Update ended it
asks the render thread for one and waits (at most 100 ms) before the Update runs. 14 of 14 loaded runs lived (the same load
killed 4 of 10 before); audio is unchanged otherwise (peak > 0, 0 underruns).

**2. A data parser in a worker thread (`f_70007a5078`, called from `EventMgr::EventSettingLoad`, prio 55 on core 1): NOT
fixed, avoided.** It is caused by `kernel_preempt` (priority preemption at indirect calls, added for BOTW's open world): with
it on, about three of four loaded runs died; with `SAVAGEWIND_NO_PREEMPT=1` 3 of 3 lived, with `SAVAGEWIND_CORES=0` 2 of 2 lived 90 s,
`SAVAGEWIND_ONE_CORE=1` still crashed 2 of 2. The preemption is off for TotK by default (`SAVAGEWIND_PREEMPT=1` turns it on);
this is a workaround. What is known:
- The thread is always the first `EventSettingLoad` worker; it parses a name through a copy of an object whose first field
  comes from `[[0x7004557b18]]+0x78`, a slot of a per-index heap table of the event manager that reads 0 (its neighbours are
  set). The same slot also reads 0 in runs that do not crash, and in those runs the function is not entered in the first 60 s,
  so the difference is in what the game decides before it (the opening is at a different point under load).
- Not a start-latency race: delaying every new thread by 10 ms does not help (4 of 4 still crashed). Not the creator being
  preempted (it loses its core for 0.07 ms), not the worker's own preemption: exempting the worker (159) or its creator from
  preemption changes nothing, but exempting **thread 127** (prio 56, core 0, a resource worker that runs the same parser and
  shares a mutex with the loader) from preemption made 4 of 4 loaded runs live. Preempting only at the parser's own virtual
  call (the most frequent point, 334 of 1304) is not what matters (5 of 6 still crashed).
- Tools left for the next try: `SAVAGEWIND_LOG_PREEMPT=1` (every preemption with its call target and how long the core was
  lost, StartThread with its creator), `SAVAGEWIND_TRACE_SVC=1` (now with the thread handle), `SAVAGEWIND_CRASH_MEM=1`
  (memory behind each register and the frame records at a crash) and `SAVAGEWIND_CRASH_THREADS=1` (every thread's state).

Also fixed: `thread_new` took the thread id and the thread-local area outside `klock`, so two threads creating threads at once
could share a TLS slot (not the cause of either crash).
