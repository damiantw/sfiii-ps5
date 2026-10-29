# Third-party code

Every dependency is MIT/BSD-compatible. Nothing is GPL or LGPL. Each one was checked against its
license file and source headers at the pinned commit.

| Component | Where | License | Used for |
|---|---|---|---|
| libchdr 47d7941 (2026-10-11) | `external/libchdr` (submodule) | BSD-3-Clause (`LICENSE.txt`; chd.c/cdrom.c/huffman.c/bitstream.c/flac.c headers) | reading CHD images |
| LZMA SDK 26.02 (bundled in libchdr, `deps/lzma-26.02`) | libchdr | public domain (`deps/lzma-26.02/LICENSE`) | CHD `cdlz` hunks |
| miniz 3.1.2 (bundled in libchdr, `deps/miniz-3.1.2`) | libchdr | MIT (header of `miniz.c`; the unlicense text in it is for the original public-domain code) | CHD `cdzl` hunks; raw DEFLATE for the patch packages' zip files |
| zstd 1.5.7 decoder (bundled in libchdr, `deps/zstd-1.5.7/zstddeclib.c`) | libchdr | dual BSD-3-Clause / GPLv2 "at your option": **we take it under BSD** | CHD `cdzs` hunks |
| LZMA SDK 26.02, 7z decoder files only | `external/lzma-sdk` (vendored, unchanged; source URL and sha256 in its README) | public domain (`lzma-sdk.txt`, file headers) | `.7z` add-ons (`src/patch/sevenzip.cpp`) |
| Dear ImGui v1.93.0 (c1aaac65e) | `external/imgui` (submodule) | MIT (`LICENSE.txt`, Omar Cornut) | the menu; `backends/imgui_impl_vulkan.cpp` draws it; `backends/imgui_impl_null.cpp` only in the headless test |
| SDL2 (system library on PC; the PS5 SDK's port on the console) | not vendored | zlib | the menu's gamepad input (`src/menu/pad_sdl2`), and the virtual controller in `vk_menu_test` |
| dr_flac (bundled in libchdr, `include/dr_libs/dr_flac.h`) | libchdr | public domain or MIT-0, at our choice | CHD `cdfl` (CD audio) hunks |

libchdr's optional micro-flac backend (`src/libchdr_flac_microflac.cpp`, BSD-3-Clause) is not
built. We use the default dr_flac backend.

Test-only tools that are **not** linked or shipped: `chdman` (MAME, for making test CHDs) and the
Python `tools/sf3patch` in sfiii-native (MIT), which serves as the byte-for-byte reference.
