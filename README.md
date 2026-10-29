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
  SDL2 virtual controller), not yet on the console. `src/runtime/runtime_host` connects it to
  the saturn-recomp runtime: save/load go to the runtime's native states (one folder per disc,
  10 slots; the result shows in the menu once the runtime has written or loaded the state at its
  next safe moment), the game pauses while the overlay is open, and the speed and R2
  fast-forward go to the pacer.
- **PS5 host** (`src/host`, `docs/host.md`): the runtime's `host_*` calls and the app's `main`.
  It shows the game field over Vulkan, placed 4:3 or 16:9, with the menu drawn over it. It paces
  at 1x/2x/3x and holds R2 to fast-forward. The DualSense works as a Saturn pad, laid out as in
  sfiii-native: Square A, Cross B, Circle C, Triangle X, L2 Y, R2 Z, L1 L, R1 R, Options START.
  With hold-to-fast-forward on, Z moves to R3. Sound goes through SDL2 at 48 kHz.
  Launching a disc or quitting to the launcher restarts the app (one disc per process).
  On the PS5 this uses `sceSystemServiceLaunchApp` and has not been tried on the console. If it
  fails, the app just exits and you start it again from the home screen. Tested on Linux with
  synthetic frames, and on PC with the game (`docs/pc-run.md`): boot, controls, sound, the
  menu, save/load states, fast-forward, switching discs and quitting to the launcher.
  Not yet on the console. There, switching discs or quitting currently just exits the app.
  Our build refuses saturn-recomp's optional `SATURN_VDP1_GPL` renderer, which would make it GPL.
- the debug server, its client and the crash handler (`docs/devtools.md`)
- a `ps5/param.json` skeleton and the clean-room MIT title platform (`docs/platform.md`)

Still to come: the Vulkan backend and the packaging scripts.

### Supported discs
Redump dumps, as BIN/CUE or CHD: Scenario 1 (USA), Scenario 2 (Japan, 1.003 and Rev A 2.000),
Scenario 3 (Japan) and the Premium Disc (Japan). An image that is already translated is also
recognised. Put them, in any combination, in `/data/shiningforceiii` (subfolders up to 3 levels
deep are fine).

### Translating on the console
In the menu: **Discs → Translate...** (or the **Translation** tab). It needs the translation
project's `SF3Patch.exe` in `/data/shiningforceiii/translation/`, and optionally its add-on zips
in `translation/V25+/`. Copy them there yourself, or turn on **Download → Allow downloading** and
press **Download**. Nothing is downloaded unless you do that. Tick or untick patches as in the
wizard, then choose one of:
- **New image beside the original** (default): writes `<name> (Translated).bin/.cue`.
- **Replace the original** (BIN/CUE only, asks first): the original files are kept as `.bak`
  files; delete them once the game works.

The launcher picks a translated image over the original for each slot.

### Settings
They are stored in `/data/shiningforceiii/settings.txt`: speed, fast-forward hold, 4:3/16:9,
sharp pixels, menu size, download opt-in, add-ons, and the patch mode.

## Console test title (swapchain)

`tools/ps5/build-test-title.sh` builds a 10-second 4K swapchain test as a
homebrew folder title (`build/ps5-test/PPSA99331/`). It uses only this repo's
MIT platform layer; see [docs/platform.md](docs/platform.md) for what each
piece replaces and how it was written. It needs `VULKAN_INCLUDE`,
`PS5_PAYLOAD_SDK` (compiler and system stubs only), `RADV_ARCHIVE` and
`PS5_NATIVE_TOOL`. Copy the folder to `/data/homebrew/` and launch it. The test
writes `/data/sfiii-ps5-test/swapchain.txt` (create that folder first).
Status: the startup code, platform shim and linker script pass on the console
(3840x2160, 60 fps). The companion `libc.prx` has not been run on the console
yet, and the title crashes on exit.

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
