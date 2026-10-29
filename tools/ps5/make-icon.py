#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Write the title's 512x512 sce_sys/icon0.png: the plain text "Shining Force III" on a dark
background, no artwork. Needs Pillow and a TrueType font (DejaVu Sans Bold by default; FONT=
overrides). ShadowMountPlus will not register a title folder without icon0.png.
usage: make-icon.py <out.png>"""
import os, sys
from PIL import Image, ImageDraw, ImageFont
font = ImageFont.truetype(os.environ.get("FONT", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"), 64)
img = Image.new("RGB", (512, 512), (18, 20, 28)); d = ImageDraw.Draw(img)
lines = ["Shining", "Force III"]; boxes = [d.textbbox((0, 0), l, font=font) for l in lines]
y = (512 - sum(b[3] - b[1] for b in boxes) - 24) // 2
for l, b in zip(lines, boxes):
    d.text(((512 - (b[2] - b[0])) // 2 - b[0], y - b[1]), l, font=font, fill=(235, 235, 240)); y += b[3] - b[1] + 24
img.save(sys.argv[1])
