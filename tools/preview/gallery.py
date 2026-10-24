#!/usr/bin/env python3
"""Tile selected preview frames (out/*.ppm) into docs/gallery.png for the README.
Usage: python3 gallery.py [out_dir] [dest.png]   (run ./preview out first)"""
import os, sys
from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else "out"
dest = sys.argv[2] if len(sys.argv) > 2 else "../../docs/gallery.png"
FRAMES = ["home_1", "pomo_home", "home_dock",
          "liuren_done", "qian_slip", "jiao_done",
          "dice_six", "coin_done", "rec_live",
          "lib_play", "pomo_run", "kindle_ready"]
S, COLS, GAP = 2, 3, 10          # 2x nearest-neighbour, 3 columns
W, H = 240 * S, 135 * S
rows = (len(FRAMES) + COLS - 1) // COLS
sheet = Image.new("RGB", (COLS * W + (COLS + 1) * GAP, rows * H + (rows + 1) * GAP), (24, 24, 28))
for i, name in enumerate(FRAMES):
    im = Image.open(os.path.join(out, name + ".ppm")).resize((W, H), Image.NEAREST)
    sheet.paste(im, (GAP + (i % COLS) * (W + GAP), GAP + (i // COLS) * (H + GAP)))
os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
sheet.save(dest, optimize=True)
print(dest, sheet.size)
