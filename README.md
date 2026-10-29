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
Early. Here is what exists:
- the debug server, its client and the crash handler (`docs/devtools.md`), tested on Linux
- a `ps5/param.json` skeleton

Planned, with placeholders still to come:
- the Vulkan backend
- the patcher and menu
- the packaging scripts

## Build (Linux, dev tools test)
```sh
git clone --recursive https://github.com/damiantw/sfiii-ps5
cmake -S . -B build -DSF3_PS5_DEVTOOLS=ON && cmake --build build
tools/devtools/test_sf3dev.py build/test_debug_server
```
Release builds leave `SF3_PS5_DEVTOOLS` OFF.

## License
MIT (see LICENSE). Third-party code must be MIT/BSD-compatible. There is no GPL/LGPL code.
