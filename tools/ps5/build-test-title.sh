#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the swapchain test as a homebrew title folder (build/ps5-test/<TITLE_ID>/)
# from sfiii-ps5's own MIT startup code, platform shim, linker script and
# companion module. See docs/platform.md.
#
# External tools, used and not shipped:
#   PS5_PAYLOAD_SDK  prospero-clang/-lld, libc++ and the system-library stubs
#   RADV_ARCHIVE     libvulkan_radeon.ps5.a (Mesa, MIT)
#   PS5_NATIVE_TOOL  ELF -> PS5 ELF/fake SELF converter (PS5_Vulkan's ps5-native-tool)
# None of the SDK's crt1.o, libc.a, libps5platform.a or linker scripts are used.
set -euo pipefail
R=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${VULKAN_INCLUDE:?set VULKAN_INCLUDE (Vulkan-Headers include dir)}" "${PS5_PAYLOAD_SDK:?set PS5_PAYLOAD_SDK}" "${RADV_ARCHIVE:?set RADV_ARCHIVE}" "${PS5_NATIVE_TOOL:?set PS5_NATIVE_TOOL}"
TITLE_ID=${TITLE_ID:-PPSA99331}
S=$PS5_PAYLOAD_SDK; T=$PS5_NATIVE_TOOL; W=$R/build/ps5-test; P=$R/ps5/platform
rm -rf "$W"; mkdir -p "$W/obj" "$W/stubs"
CC="$S/bin/prospero-clang -std=c11 -O2 -ffunction-sections -fdata-sections -I$P"
CXX="$S/bin/prospero-clang++ -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -I$R/src -I$VULKAN_INCLUDE"
objs=()
for f in $P/crt/*.c $P/title/*.c; do o=$W/obj/$(basename $f).o; $CC -c $f -o $o; objs+=($o); done
for f in $P/crt/*.cpp $P/vk_dispatch.cpp $R/src/vulkan/vk_scene.cpp $R/src/vulkan/display.cpp $R/ps5/apps/swapchain_test.cpp; do
  o=$W/obj/$(basename $f).o; $CXX -c $f -o $o; objs+=($o); done
for l in libSceAgc:SF3_STUB_AGC libSceAgcDriver:SF3_STUB_AGC_DRIVER; do
  $S/bin/prospero-clang -O2 -fPIC -D${l#*:} -c $P/agc_stubs.c -o $W/obj/${l%%:*}.o
  $S/bin/prospero-lld --shared -soname ${l%%:*}.prx -o $W/stubs/${l%%:*}.so $W/obj/${l%%:*}.o; done
B=$(clang --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a
$S/bin/prospero-lld -T $P/ps5-title.ld --eh-frame-hdr --undefined-version --wrap=pthread_create \
  --version-script $P/title-exports.map --exclude-libs=ALL -e _start -o $W/llvm-pie.elf "${objs[@]}" \
  $W/stubs/libSceAgc.so $W/stubs/libSceAgcDriver.so -L $S/target/lib \
  --whole-archive "$RADV_ARCHIVE" --no-whole-archive \
  --start-group $S/target/lib/libc++.a $S/target/lib/libc++abi.a $S/target/lib/libunwind.a $B --end-group \
  --as-needed $S/target/lib/*.so
link=(--stub-dir $S/target/lib --module-sdk 0x02000009 --companion-sdk 0x08050001)
$T link --in $W/llvm-pie.elf --out $W/eboot.elf "${link[@]}" --stub $W/stubs/libSceAgc.so --stub $W/stubs/libSceAgcDriver.so --file-name eboot.elf >/dev/null
# companion module: sce_module/libc.prx
$S/bin/prospero-clang -O2 -fPIC -fno-plt -c $P/companion/libc_companion.c -o $W/obj/companion.o
$S/bin/prospero-lld --shared -soname libc.prx -T $P/ps5-title.ld --version-script $P/title-exports.map -o $W/companion.so $W/obj/companion.o -L$S/target/lib -lkernel
$T link --in $W/companion.so --out $W/companion.elf "${link[@]}" --file-name libc.prx >/dev/null
A=$W/$TITLE_ID; mkdir -p $A/sce_sys $A/sce_module
$T self --sign --in $W/eboot.elf --out $A/eboot.bin --magic 0x1D3D154F >/dev/null
$T self --sign --in $W/companion.elf --out $A/sce_module/libc.prx --magic 0x1D3D154F >/dev/null
python3 - "$A/sce_sys/param.json" "$TITLE_ID" <<'PY'
# The PS5 application parameter file, with the fields a homebrew folder title needs.
import json,sys
t=sys.argv[2]
json.dump({"ageLevel":{"default":0},"applicationCategoryType":0,"applicationDrmType":"free",
 "attribute":0,"attribute2":0,"attribute3":0,"conceptId":t[4:],"contentBadgeType":1,
 "contentId":f"UP9000-{t}_00-SF3SWAPTEST00001","contentVersion":"01.000.000","downloadDataSize":256,
 "gameIntent":{"permittedIntents":[{"intentType":"launchActivity"}]},
 "localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":"SF3 Swapchain Test"}},
 "masterVersion":"01.00","pubtools":{"creationDate":"2026-10-29 00:00:00","loudnessSnd0":"-28.00","toolVersion":"2.00"},
 "requiredSystemSoftwareVersion":"0x0000000000000000","sdkVersion":"0x0000000000000000",
 "titleId":t,"versionFileUri":""},open(sys.argv[1],"w"),indent=2)
PY
echo "title folder: $A"
