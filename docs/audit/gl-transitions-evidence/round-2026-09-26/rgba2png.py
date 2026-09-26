#!/usr/bin/env python3
"""rgba2png.py - convert raw 256x256 RGBA files dumped by
gles3_transitions_smoke.c into PNGs for inspection.

Usage: rgba2png.py <dump_dir> <out_dir>
"""
import os, sys, struct
from PIL import Image

src_dir, out_dir = sys.argv[1], sys.argv[2]
os.makedirs(out_dir, exist_ok=True)
W = H = 256
n = 0
for name in sorted(os.listdir(src_dir)):
    if not name.endswith('.rgba'):
        continue
    raw = open(os.path.join(src_dir, name), 'rb').read()
    if len(raw) != W * H * 4:
        print(f"SKIP {name}: expected {W*H*4} bytes, got {len(raw)}")
        continue
    img = Image.frombytes('RGBA', (W, H), raw, 'raw', 'RGBA')
    # vertical flip - the smoke tester reads glReadPixels which returns
    # rows bottom-to-top; PNGs read top-to-bottom
    img = img.transpose(Image.FLIP_TOP_BOTTOM)
    out = os.path.join(out_dir, name.replace('.rgba', '.png'))
    img.save(out, 'PNG', optimize=True)
    n += 1
print(f"converted {n} files -> {out_dir}")