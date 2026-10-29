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
Early; nothing runs on the console yet. What exists, all tested on Linux:
- **Disc layer** (`src/disc`): reads BIN/CUE and CHD images, identifies the four discs by their
  Redump hashes (IP.BIN as a fallback), and scans a folder (default `/data/shiningforceiii`) for
  any combination of them.
- **Translation patcher** (`src/patch`): a C++ port of sfiii-native's `tools/sf3patch`. Its
  output is byte-identical to the Python patcher's for all five discs, with or without add-ons.
  It can write a new image or patch in place. Downloading the translation project's latest
  release and add-ons is opt-in.
- **Menu skeleton** (`src/menu`, Dear ImGui): the launcher has the four disc slots, disc status,
  the translation page and the download page; the in-game overlay has save/load state (10 slots),
  speed 1x/2x/3x with hold-to-fast-forward, and 4:3 or 16:9 display. The menu does not draw on
  the PS5 yet: its renderer and gamepad backends come with the Vulkan backend.
- the debug server, its client and the crash handler (`docs/devtools.md`)
- a `ps5/param.json` skeleton

Still to come: the Vulkan backend, the PS5 network transport for downloads, `.7z` add-ons, and
the packaging scripts.

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

Tests, which need your own discs and the translation package on the machine; neither is committed:
`tests/sf3patch_vs_python.sh` (byte-identical to the Python patcher), `build/patch_quirks` with
`tests/patch_quirks.py`, and `tests/menu_smoke.sh`. The last one drives the real menu headlessly
through ImGui's null backend and checks both patch modes against the reference image.

## License
MIT (see LICENSE). Third-party code must be MIT/BSD-compatible. There is no GPL/LGPL code.
