#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the console app as a homebrew title folder: build/ps5-title/<TITLE_ID>/ with eboot.bin,
# sce_module/libc.prx, sce_sys/param.json and profiles/ (sfiii-native's revision profiles, read at
# /app0/profiles). The app is sfiii-native's generic build: no code or data made from a disc. Each
# disc's game code is generated on the console at its first launch (src/prepare).
#
# External tools, used and not shipped (as tools/ps5/build-test-title.sh):
#   PS5_PAYLOAD_SDK  prospero-clang/-lld, libc++ and the system-library stubs
#   RADV_ARCHIVE     libvulkan_radeon.ps5.a (Mesa, MIT)
#   PS5_NATIVE_TOOL  ELF -> PS5 ELF/fake SELF converter
#   VULKAN_INCLUDE   Vulkan-Headers include dir
#   SATURN_RUNTIME   saturn-recomp's runtime/ with sfiii-native's tools/runtime-diagnostics.patch
set -euo pipefail
R=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${VULKAN_INCLUDE:?}" "${PS5_PAYLOAD_SDK:?}" "${RADV_ARCHIVE:?}" "${PS5_NATIVE_TOOL:?}" "${SATURN_RUNTIME:?}"
TITLE_ID=${TITLE_ID:-PPSA93303}   # docs/title-id.md
S=$PS5_PAYLOAD_SDK; T=$PS5_NATIVE_TOOL; P=$R/ps5/platform
W=${OUT:-$R/build/ps5-title}; B=$W/cmake
mkdir -p "$W/obj" "$W/stubs"
for l in libSceAgc:SF3_STUB_AGC libSceAgcDriver:SF3_STUB_AGC_DRIVER; do
  $S/bin/prospero-clang -O2 -fPIC -D${l#*:} -c $P/agc_stubs.c -o $W/obj/${l%%:*}.o
  $S/bin/prospero-lld --shared -soname ${l%%:*}.prx -o $W/stubs/${l%%:*}.so $W/obj/${l%%:*}.o; done
# find_package(Vulkan) wants a library; RADV itself goes in whole (saturn_ps5.cmake), so give it an empty one
rm -f "$W/stubs/libvulkan_none.a"; ar rc "$W/stubs/libvulkan_none.a"
cmake -G Ninja -S "$R/external/sfiii-native/native/recomp/generic" -B "$B" \
  -DCMAKE_TOOLCHAIN_FILE="$R/tools/ps5/prospero.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DSATURN_RUNTIME="$SATURN_RUNTIME" -DSATURN_NO_SDL=ON -DSATURN_EXTRA="$R/tools/ps5/saturn_ps5.cmake" \
  -DSF3_PS5_ROOT="$R" -DSF3_PS5_TITLE_ID="$TITLE_ID" -DRADV_ARCHIVE="$RADV_ARCHIVE" -DSF3_PS5_STUBS="$W/stubs" \
  -DVulkan_INCLUDE_DIR="$VULKAN_INCLUDE" -DVulkan_LIBRARY="$W/stubs/libvulkan_none.a" >"$W/cmake.log"
ninja -C "$B" sf3ps5
link=(--stub-dir $S/target/lib --module-sdk 0x02000009 --companion-sdk 0x08050001)
$T link --in $B/sf3ps5 --out $W/eboot.elf "${link[@]}" --stub $W/stubs/libSceAgc.so --stub $W/stubs/libSceAgcDriver.so --file-name eboot.elf >/dev/null
$S/bin/prospero-clang -O2 -fPIC -fno-plt -c $P/companion/libc_companion.c -o $W/obj/companion.o
$S/bin/prospero-lld --shared -soname libc.prx -T $P/ps5-title.ld --version-script $P/title-exports.map -o $W/companion.so $W/obj/companion.o -L$S/target/lib -lkernel
$T link --in $W/companion.so --out $W/companion.elf "${link[@]}" --file-name libc.prx >/dev/null
A=$W/$TITLE_ID; rm -rf "$A"; mkdir -p $A/sce_sys $A/sce_module $A/profiles
$T self --sign --in $W/eboot.elf --out $A/eboot.bin --magic 0x1D3D154F >/dev/null
$T self --sign --in $W/companion.elf --out $A/sce_module/libc.prx --magic 0x1D3D154F >/dev/null
cp "$R"/external/sfiii-native/profiles/*.json $A/profiles/
sed -e "s/@TITLE_ID@/$TITLE_ID/g" -e "s/@CONCEPT@/${TITLE_ID:4}/g" "$R/ps5/param.json" >$A/sce_sys/param.json
echo "title folder: $A"
