#!/bin/bash
# SPDX-License-Identifier: MIT
# Release package for the homebrew store: build/ps5-title/<TITLE_ID>/ (tools/ps5/build-title.sh with
# SF3_PS5_DEVTOOLS=OFF) as dist/<TITLE_ID>.zip plus SHA256SUMS. Refuses a dev build.
# The ZIP holds the title folder (eboot.bin, sce_module/, sce_sys/, profiles/): no game data.
# PS5 homebrew has no clean signed .pkg path; stores install the folder (or mount an image made
# from it). Extract it to a staging folder, then rename it into /data/homebrew/ (README, Install).
set -euo pipefail
R=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
TITLE_ID=${TITLE_ID:-PPSA93303}
W=${OUT:-$R/build/ps5-title}
A=$W/$TITLE_ID
VERSION=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['contentVersion'])" "$A/sce_sys/param.json")
sh "$R/tools/check-release.sh" "$W/eboot.elf"
if [ "$(strings "$A/eboot.bin" | grep -c 'devtools.enable')" != 0 ]; then echo "release: $A/eboot.bin is a dev build"; exit 1; fi
D=${DIST:-$R/dist}; mkdir -p "$D"
Z=$D/$TITLE_ID.zip   # contentVersion in param.json: $VERSION
python3 - "$A" "$Z" "$TITLE_ID" <<'PY'
import os, sys, zipfile
src, out, tid = sys.argv[1:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.join(tid, os.path.relpath(p, src)))
PY
(cd "$D" && sha256sum "$(basename "$Z")" > SHA256SUMS)
echo "release package: $Z (version $VERSION)"; cat "$D/SHA256SUMS"
