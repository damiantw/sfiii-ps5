#!/bin/sh
# Headless menu test on the box: PD disc, translation package with add-ons, compared with the
# reproducible reference image. Paths are local to the box; nothing here is committed.
set -e
B=${B:-build}
D=${D:-"/workspace/discs/SFII/Shining Force III Premium Disc (Japan)"}
EXPECT_BIN=${EXPECT_BIN:-/workspace/discs/patched/pd-v25_1-addons/pd.bin} \
  "$B/menu_smoke" "$D" ${PKG:-/workspace/sf3pw/SF3Patch.exe} ${ADDONS:-/workspace/sf3pw/v25plus} ${OUT:-/tmp/menu_smoke}
