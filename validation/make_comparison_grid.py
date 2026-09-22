#!/usr/bin/env python3
"""
make_comparison_grid.py — compose a single PNG that arranges the same
binary's screenshots from o6n / medusa / pegasus side-by-side for easy
visual diff'ing in the validation doc.

Usage:
  ./make_comparison_grid.py --bin antinspect_gles3 --out /tmp/foo.png
  ./make_comparison_grid.py --all --out-dir /tmp/grids
"""

import argparse
import os
import sys
from PIL import Image, ImageDraw, ImageFont


PLATFORM_LABELS = {
    "o6n":    "O6N    (Sky1/Panthor, Mali-G720)",
    "medusa": "MEDUSA (T2, AMD Navi14, RADV)",
    "pegasus":"PEGASUS(T2, Intel CML iGPU, iris)",
}


def thumb(im, max_w=480, max_h=320):
    w, h = im.size
    if w * h > max_w * max_h:
        if w / max_h > h / max_w:
            new_w = max_w
            new_h = int(h * max_w / w)
        else:
            new_h = max_h
            new_w = int(w * max_h / h)
        return im.resize((new_w, new_h), Image.LANCZOS)
    return im


def grid_for_bin(bin_name, root, platforms):
    """Build a single image: one row per platform, one column (the shot)."""
    cols = []
    for plat in platforms:
        path = f"{root}/validation/{plat}/raw/shots/{bin_name}.png"
        if os.path.exists(path):
            im = Image.open(path).convert("RGB")
            im_t = thumb(im)
            label_h = 24
            canvas = Image.new("RGB", (im_t.width, im_t.height + label_h), "white")
            canvas.paste(im_t, (0, label_h))
            d = ImageDraw.Draw(canvas)
            d.text((4, 4), PLATFORM_LABELS[plat], fill="black")
            cols.append(canvas)
    if not cols:
        return None
    # Stack vertically (each platform gets its own row)
    w = max(c.width for c in cols)
    h = sum(c.height for c in cols)
    out = Image.new("RGB", (w, h), "white")
    y = 0
    for c in cols:
        out.paste(c, (0, y))
        y += c.height
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--bin", help="binary name (without _gles3 suffix)")
    ap.add_argument("--all", action="store_true", help="generate one grid per binary listed in --list")
    ap.add_argument("--list", help="file with binary names (one per line, without _gles3 suffix)")
    ap.add_argument("--out", help="output file (single-bin mode)")
    ap.add_argument("--out-dir", help="output directory (--all mode)")
    args = ap.parse_args()

    platforms = ["o6n", "medusa", "pegasus"]
    if args.bin:
        bins = [args.bin]
    elif args.all and args.list:
        with open(args.list) as f:
            bins = [l.strip() for l in f if l.strip()]
    else:
        print("Need --bin or --all --list", file=sys.stderr)
        sys.exit(2)

    for b in bins:
        full = f"{b}_gles3"
        grid = grid_for_bin(full, args.root, platforms)
        if grid is None:
            print(f"  skip {full} (no data)")
            continue
        if args.out_dir:
            os.makedirs(args.out_dir, exist_ok=True)
            out_path = f"{args.out_dir}/{full}_grid.png"
        else:
            out_path = args.out
        grid.save(out_path)
        print(f"  wrote {out_path} ({grid.size})")


if __name__ == "__main__":
    main()