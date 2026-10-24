#!/usr/bin/env python3
"""Convert preview frames to PNG (3x, nearest) and tile them into one contact sheet."""
import glob, os, sys
from PIL import Image, ImageDraw
out = sys.argv[1] if len(sys.argv) > 1 else "out"
names = sys.argv[2:] or None
files = sorted(glob.glob(os.path.join(out, "*.ppm")), key=os.path.getmtime)
if names: files = [f for f in files if any(os.path.basename(f).startswith(n) for n in names)]
S = 3
imgs = []
for f in files:
    im = Image.open(f).resize((240 * S, 135 * S), Image.NEAREST)
    im.save(f[:-4] + ".png")
    imgs.append((os.path.basename(f)[:-4], im))
cols = 2
rows = (len(imgs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (240 * S + 12), rows * (135 * S + 30)), (60, 60, 60))
d = ImageDraw.Draw(sheet)
for i, (n, im) in enumerate(imgs):
    x, y = (i % cols) * (240 * S + 12), (i // cols) * (135 * S + 30)
    sheet.paste(im, (x, y + 22))
    d.text((x + 4, y + 4), n, fill=(255, 255, 255))
sheet.save(os.path.join(out, "sheet.png"))
print(len(imgs), "frames")
