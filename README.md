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

## Build (Linux, dev tools test)
```sh
git clone --recursive https://github.com/damiantw/sfiii-ps5
cmake -S . -B build -DSF3_PS5_DEVTOOLS=ON && cmake --build build
tools/devtools/test_sf3dev.py build/test_debug_server
```
Release builds leave `SF3_PS5_DEVTOOLS` OFF.

## License
MIT (see LICENSE). Third-party code must be MIT/BSD-compatible. There is no GPL/LGPL code.
