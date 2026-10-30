# sfiii-ps5

PS5 port of [sfiii-native](https://github.com/damiantw/sfiii-native) (the Shining Force III static recompilation).
This repo holds the PS5-only parts:
- packaging and the title ID (PPSA93303)
- the Vulkan renderer backend
- the on-console disc patcher and menu
- dev tools

The recompiler port and the SH-2 interpreter fallback live in the fork, which is the submodule at `external/sfiii-native`.

**No game data.** Nothing here is derived from the discs: no code, no bytes, no BIOS, no translation
patch files. Disc images are read and built **on the console** from the user's own BIN/CUE or CHD files.

## Status
Early. Only the swapchain test title (below) has run on the console. What exists, all tested on Linux:
- **Disc layer** (`src/disc`): reads BIN/CUE and CHD images, identifies the four discs by their
  Redump hashes (IP.BIN as a fallback), and scans a folder (default `/data/shiningforceiii`) for
  any combination of them.
- **Translation patcher** (`src/patch`): a C++ port of sfiii-native's `tools/sf3patch`. Its
  output is byte-identical to the Python patcher's for all five discs, with or without add-ons.
  It can write a new image or patch in place. Add-ons can be `.zip`, `.7z` or folders
  (`.7z` through the LZMA SDK's public-domain decoder; encrypted archives are not supported).
  Downloading the translation project's latest
  release and add-ons is opt-in. On the console it goes through the system HTTP library
  (`src/patch/fetch_sce`): HTTPS with the system's certificate checks and the console's own DNS.
  If the console's DNS blocks the server, copy the files by hand instead. Tested against a fake
  of the library, not yet on the console.
- **Menu** (`src/menu`, Dear ImGui): the launcher has the four disc slots, disc status,
  the translation page and the download page; the in-game overlay has save/load state (10 slots),
  speed 1x/2x/3x with hold-to-fast-forward, and 4:3 or 16:9 display. It draws through Vulkan
  (`src/vulkan/menu_vk`) on top of the game picture, and the gamepad drives it: touchpad or
  Options opens and closes it, L1/R1 switch tabs, the d-pad or left stick moves, Cross picks,
  Circle goes back. While it is closed, holding R2 fast-forwards. Tested on Linux (lavapipe and an
  SDL2 virtual controller) and on the console (save, load and slot switching, 1x/2x/3x and R2,
  4:3 and 16:9). The slot list shows each slot's age; Save and Load close the menu so the
  machine reaches its safe point, and "slot N saved/loaded" shows when it is done. `src/runtime/runtime_host` connects it to
  the saturn-recomp runtime: save/load go to the runtime's native states (one folder per disc,
  10 slots; the result shows in the menu once the runtime has written or loaded the state at its
  next safe moment), the game pauses while the overlay is open, and the speed and R2
  fast-forward go to the pacer.
- **PS5 host** (`src/host`, `docs/host.md`): the runtime's `host_*` calls and the app's `main`.
  It shows the game field over Vulkan, placed 4:3 or 16:9, with the menu drawn over it. It paces
  at 1x/2x/3x and holds R2 to fast-forward. The DualSense works as a Saturn pad, laid out as in
  sfiii-native: Square A, Cross B, Circle C, Triangle X, L2 Y, R2 Z, L1 L, R1 R, Options START.
  With hold-to-fast-forward on, Z moves to R3. Sound plays at 48 kHz, through SDL2 on PC and
  `libSceAudioOut` on the console. On the console the pad is read with `libScePad`.
  Launching a disc or quitting to the launcher restarts the app (one disc per process).
  On the PS5 this uses `sceSystemServiceLaunchApp` and has not been tried on the console. If it
  fails, the app just exits and you start it again from the home screen. Tested on Linux with
  synthetic frames, and on PC with the game (`docs/pc-run.md`): boot, controls, sound, the
  menu, save/load states, fast-forward, switching discs and quitting to the launcher.
  Not yet on the console.
  Our build refuses saturn-recomp's optional `SATURN_VDP1_GPL` renderer, which would make it GPL.
- the debug server, its client and the crash handler (`docs/devtools.md`)
- the clean-room MIT title platform (`docs/platform.md`) and the console title build (below)

Still to come: the first run of the full app on the console.

### Supported discs
Redump dumps, as BIN/CUE or CHD: Scenario 1 (USA), Scenario 2 (Japan, 1.003 and Rev A 2.000),
Scenario 3 (Japan) and the Premium Disc (Japan). An image that is already translated is also
recognised. Put them, in any combination, in `/data/shiningforceiii` (subfolders up to 3 levels
deep are fine, e.g. one folder per disc with its `.cue` and `(Track 1)`/`(Track 2)` `.bin` files).
If a scan finds nothing, the Discs log says why: the folder cannot be opened (with the system's
reason) or which entries it holds. On the PS5 the title's sandbox currently cannot open `/data`
("Operation not permitted"), so discs are not found there yet.

### Translating on the console
In the menu: **Discs → Translate...** (or the **Translation** tab). It needs the translation
project's `SF3Patch.exe` in `/data/shiningforceiii/translation/`, and optionally its add-on zips
in `translation/V25+/`. Copy them there yourself, or turn on **Download → Allow downloading** and
press **Download**. Nothing is downloaded unless you do that. Tick or untick patches as in the
wizard, then choose one of:
- Default (**Replace the original** unticked): writes a new `<name> (Translated).bin/.cue`
  beside the original.
- **Replace the original** ticked (BIN/CUE only, asks first): the original files are kept as `.bak`
  files; delete them once the game works. (A checkbox, as the pad could not reach the old radio
  buttons on the console.)

The launcher picks a translated image over the original for each slot.

### First launch of a disc
The first time you start each disc, the app shows **Preparing** while it builds that disc's
game code from the disc itself (once per disc, in C++ on the console). The result is kept in
`/data/shiningforceiii/cache/`, so later launches start straight away. Delete that folder to
rebuild it. Nothing made from a disc ships with the app. See [docs/pc-run.md](docs/pc-run.md).

### Settings
They are stored in `/data/shiningforceiii/settings.txt`: speed, fast-forward hold, 4:3/16:9,
sharp pixels, menu size, download opt-in, add-ons, and the patch mode.

## Console title

`tools/ps5/build-title.sh` builds the app as a homebrew folder title,
`build/ps5-title/PPSA93303/` (title ID: `docs/title-id.md`). It contains `eboot.bin`,
`sce_module/libc.prx`, `sce_sys/param.json` and `profiles/`, which holds sfiii-native's revision
profiles, read at `/app0/profiles`. It is sfiii-native's generic build, so it has no code or data
made from a disc: each disc's game code is built on the console at its first launch (see
[First launch of a disc](#first-launch-of-a-disc)). It needs the same tools as the test title
below, plus `SATURN_RUNTIME` (saturn-recomp's `runtime/` with sfiii-native's
`tools/runtime-diagnostics.patch`). `VULKAN_INCLUDE` must hold Vulkan-Headers 1.4.354 or newer and
nothing else (not `/usr/include`, which puts the host libc ahead of libc++). `TITLE_ID=` and `OUT=`
override the defaults. The build also writes `sce_sys/icon0.png` (a generated gradient).

To install, copy the folder to a staging path such as `/data/sf3stage/` first, then rename it into
`/data/homebrew/` so ShadowMountPlus sees it complete. If it finds the folder half-copied or without
`icon0.png`, it stops retrying that title ID until the console reboots.

Both `eboot.bin` and `sce_module/libc.prx` are wrapped as fake SELFs by `tools/ps5/make-fself.py`
(MIT, written from the container layout); the module uses the PS5 module magic `54 14 F5 EE`.
`tools/ps5/prx-fixup.py` turns the converter's output for the companion into a module (ELF type,
module param, SONAME, module info and its export library). The build output is byte-for-byte
reproducible.

The loader's libSceLibcInternal looks up the companion's exports, so `tools/ps5/companion-exports.py`
gives it the 2566 NIDs listed in `tools/ps5/libc-exports.tsv` (libraries `libc` and `libc_setjmp`;
names, sizes and kinds only). The converter cannot publish exports, so the tool adds them to the
converted module and the build fails unless the list matches exactly. setjmp/longjmp, atexit,
`__cxa_finalize`, `_malloc_init` and `Need_sceLibc` are real; every other function stores its table
row in `sf3_last_row` and returns 0. The dynlib data is rebuilt in the layout system modules use, and
the thread-local exports sit in the RELRO segment. `SF3_COMPANION_EXPORTS=none|notls|full` (default
full) selects a variant for bisecting.

Status: the companion loads on the console and the title reaches the launcher. Two things were
required: the module's DT_INIT must return 0 (a bare `ret` left eax set and libSceLibcInternal
rejected the module with `PRX_SCE_MODULE_LOAD_ERROR`), and the needed-module entries carry version
1.1 with export library ids following the import libraries, as system modules do. The companion no
longer imports libSceLibcInternal. Discs are not reachable yet: the title's sandbox cannot open
`/data` ("Operation not permitted"), so the Discs tab finds no images (see the disc section).

## Console test title (swapchain)

`tools/ps5/build-test-title.sh` builds a 10-second 4K swapchain test as a
homebrew folder title (`build/ps5-test/PPSA99331/`). It uses only this repo's
MIT platform layer; see [docs/platform.md](docs/platform.md) for what each
piece replaces and how it was written. It needs `VULKAN_INCLUDE`,
`PS5_PAYLOAD_SDK` (compiler and system stubs only), `RADV_ARCHIVE` and
`PS5_NATIVE_TOOL`. Copy the folder to `/data/homebrew/` and launch it. The test
writes `/data/sfiii-ps5-test/swapchain.txt` (create that folder first).
Status: the startup code, platform shim and linker script pass on the console
(3840x2160, 60 fps). The title crashes on exit.

## Build (Linux: tools and tests)
```sh
git clone --recursive https://github.com/damiantw/sfiii-ps5
cmake -S . -B build -DSF3_PS5_DEVTOOLS=ON && cmake --build build
tools/devtools/test_sf3dev.py build/test_debug_server
```
Release builds leave `SF3_PS5_DEVTOOLS` OFF.

PC tools (the same code the console runs):
```sh
build/sf3disc scan /path/to/discs                 # which disc fills which slot
build/sf3disc identify disc.cue|disc.chd
build/sf3disc probe disc.cue [EXTRACT_DIR [PROFILES_DIR]]  # content manifest + ID (as sfiii-native tools/probe.py), root .BIN files, matching profile
build/sf3prepare disc.cue DATA_DIR PROFILES_DIR      # the first-launch step: module set in DATA_DIR/cache/
build/sf3patch fetch --dest translation           # opt-in download (curl): SF3Patch.exe + V25+ add-ons
build/sf3patch list  disc.cue --package translation/SF3Patch.exe --addons translation/V25+
build/sf3patch patch disc.chd --package translation/SF3Patch.exe --addons translation/V25+ -o out.bin
build/sf3patch patch disc.cue --package ... --in-place     # replaces the BIN/CUE; originals kept as .bak
```
`--enable/--disable/--only NAME` change which patches are ticked. `--reproducible` gives fixed
timestamps; that is how the tests compare against the Python patcher.

CI (`.github/workflows/ci.yml`) builds everything and runs the tests that need no game data: the
debug server test, `patch_quirks` against the Python patcher, `runtime_host_test` (the menu's runtime glue on a fake runtime), `fetch_sce_test` (the console's download transport on a fake sceHttp), a compile of
`src/runtime/runtime_bind.cpp` against the pinned saturn-recomp with sfiii-native's patch,
`tests/sevenzip_test.sh` (synthetic files packed
five ways with the 7-Zip CLI), `vk_scene_test`, `vk_menu_test`, `host_test` (the PS5 host: pacer,
pad layout, a sine's pitch through the 48 kHz path on SDL's disk driver, the menu over synthetic
fields, relaunch through execv), a compile of `src/host/host_runtime.cpp` against the runtime
(Mesa's lavapipe software Vulkan, SDL2 virtual controller), and the disc-data and release checks.

The Vulkan tests and the menu's gamepad code need the Vulkan headers/loader and SDL2 on PC
(Debian/Ubuntu: `libvulkan-dev mesa-vulkan-drivers libsdl2-dev`); without them those targets are
skipped. `build/vk_menu_test [shot.ppm]` draws the menu over a test picture, drives it with a
virtual pad, and can save the frame.

Tests, which need your own discs and the translation package on the machine; neither is committed:
`tests/sf3patch_vs_python.sh` (byte-identical to the Python patcher), `build/patch_quirks` with
`tests/patch_quirks.py`, and `tests/menu_smoke.sh`. The last one drives the real menu headlessly
through ImGui's null backend and checks both patch modes against the reference image.

## License
MIT (see LICENSE). Third-party code must be MIT/BSD-compatible. There is no GPL/LGPL code.

### PS5: reading /data

A PS5 title may not use libc `opendir`/`realpath`/`getcwd` (EPERM). The disc scan lists directories with `open(O_DIRECTORY)` + `getdents`, and MIT shims give `getcwd` = `/app0` and a lexical `realpath`. Put discs under `/data/shiningforceiii/` (any subfolder layout); launch the title from the home screen.

### PS5: title runtime notes

- `fstatat`/`openat`/`unlinkat`/`fdopendir` exist only in libkernel_sys, which a title cannot import; left unresolved they became calls through NULL (the crash on starting any disc). MIT shims (`ps5/platform/title/path_shims.c`, `dir_shims.c`) cover them, and first-launch prepare removes folders with its own getdents walk.
- The C heap (`ps5/platform/title/heap.c`, linked with `--wrap=malloc` etc.) comes from direct memory; the system malloc refused tens-of-MB blocks. Disc files are hashed and extracted in chunks.
- A fault handler (`fault_klog.c`) writes signal, registers and stack to the kernel log.
- Profiler: `/data/sfiii/sample.enable` turns on a sampler in `fault_klog.c`. Any content logs the
  main thread's rip every 500 ms; `perf` takes 1 kHz samples instead and sorts them into
  x64 (the module set the loader mapped), eboot, other (system libraries) and wait (present/pace),
  plus frame time and work time per frame. Every 7 s it writes `[sf3 perf]` lines to klog: totals,
  a frame-time histogram, present time and each frame over 25 ms split into work / acquire / draw /
  queue. `perf frames` skips the sampling (frame times only); `perf file` also appends to
  `/data/sfiii/perf.txt`. Any frame over 50 ms prints a `hitch:` line to stderr.
- Frame pacing: the FIFO swapchain (3 images) paces the machine at the TV's 60 Hz (`Pacer::follow`);
  the audio resampler nudges its rate by up to 0.5% to keep its queue near 90 ms, so nothing is
  dropped. `/data/sfiii/pace-timer` brings back the Saturn-clock timer, `/data/sfiii/swap2` two images.
- stderr: the system libc's stderr took 40-100 ms a write on the console, and RADV logs a line from its
  submit path every 10 s, a 100 ms hitch each time. `stderr_fast.c` (`--wrap=fprintf` etc.) formats
  stderr text itself and writes it to a pipe that a thread drains into `/data/sfiii/stderr.txt`.
- A `.chd` given to the runtime (`launch=` or the menu) is unpacked once to
  `/data/shiningforceiii/cache/chd-<disc id>/` and played from there.
- 4:3 leaves out the frame's first column, which on many screens holds the VDP2 back-screen colour
  (a blue stripe a CRT hides in overscan).
- The dev-build debug server listens on port 9200 by default: a title's bind on 9300/9301 fails with EACCES.
- Japanese Scenario 2/3 and Premium Disc images must be translated (v25.1 patch, Translate page) before they play; untranslated images report "not a supported revision".

### PS5: controllers

The launcher reads every logged-in user's controller, so a Remote Play pad drives the menu too. It does not open the remote-control port: a title holding that port takes the PS button away from the system. The launcher's last line shows the pad state. Dev builds can also be driven without a pad (docs/devtools.md: `launch=` in /data/sfiii/devtools.enable).

- Disc switch / quit to launcher relaunches via `sceSystemServiceLoadExec("/app0/eboot.bin")`; a running title cannot `LaunchApp` itself (0x8094000F).
- JIT unwind info is registered per FDE (`__register_frame(fde)`); the SDK libunwind rejects a CIE pointer.
- Stale `cache/work-*` folders are swept at prepare time.
