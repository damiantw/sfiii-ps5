#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Home-screen backgrounds for the PS5 shell: ps5/sce_sys/pic0.png (16:9 title art) ->
# sce_sys/pic0.dds, 3840x2160 single-level BC7 (DXGI_FORMAT_BC7_UNORM) in a DX10 DDS.
# pic0 is shown while the tile is highlighted, pic1 while the game launches. The shell ignores a PNG.
# BC7 encoder: bc7enc_rdo (MIT/public domain), a build tool only: --bc7enc PATH.
# Usage: make-pic-dds.py --bc7enc /path/bc7enc SRC.png OUTDIR
import argparse, os, struct, subprocess, sys, tempfile
from PIL import Image

W, H = 3840, 2160
DDSD_MIPMAPCOUNT = 0x20000
DDS_ALPHA_MODE_STRAIGHT = 1
BC7_UNORM = 98

def fix_header(path):
    d = bytearray(open(path, "rb").read())
    assert d[:4] == b"DDS " and struct.unpack_from("<I", d, 4)[0] == 124
    assert struct.unpack_from("<II", d, 12) == (H, W), "size"
    assert d[84:88] == b"DX10" and struct.unpack_from("<I", d, 128)[0] == BC7_UNORM, "not BC7 DX10"
    struct.pack_into("<I", d, 8, struct.unpack_from("<I", d, 8)[0] | DDSD_MIPMAPCOUNT)
    struct.pack_into("<I", d, 24, 1)                         # depth
    struct.pack_into("<I", d, 28, 1)                         # one mip level
    struct.pack_into("<I", d, 144, DDS_ALPHA_MODE_STRAIGHT)  # DX10 miscFlags2
    assert len(d) == 148 + (W // 4) * (H // 4) * 16, "payload size"
    open(path, "wb").write(d)

def main():
    a = argparse.ArgumentParser()
    a.add_argument("--bc7enc", required=True)
    a.add_argument("src"); a.add_argument("outdir")
    o = a.parse_args()
    im = Image.open(o.src).convert("RGB")
    if im.size != (W, H): im = im.resize((W, H), Image.LANCZOS)
    with tempfile.TemporaryDirectory() as t:
        png = os.path.join(t, "pic.png")
        im.convert("RGBA").save(png)
        subprocess.run([o.bc7enc, "-q", "-g", "-u4", png], cwd=t, check=True)
        dds = os.path.join(t, "pic.dds")
        fix_header(dds)
        data = open(dds, "rb").read()
    for n in ("pic0.dds",):   # build-title.sh copies it to pic1.dds too
        open(os.path.join(o.outdir, n), "wb").write(data)
    print("wrote pic0.dds", len(data), "bytes")

if __name__ == "__main__":
    main()
