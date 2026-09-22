#!/usr/bin/env python3
"""
compare_results.py — cross-platform visual diff for the GLES3 validation.

Reads three classify_results.py output CSVs (o6n, medusa, pegasus) and for
each binary that's PASS on all three, computes:
  - per-platform mean brightness, distinct color buckets, non-dark ratio
  - max pairwise cross-platform distance (rough metric)
  - "consistent" vs "vendor-divergent" classification based on a brightness
    and color-count threshold.

Goal: surface VISUAL_DIFFERENCE cases where the hack renders, but the
visual output diverges between GLES3 implementations.

Also detects BLACK cases (process ran, GLES context live, but screenshot
is essentially identical to per-platform baseline brightness) that the
classify heuristic missed because it only compared to ONE baseline.
"""

import argparse
import csv
import json
import os
import sys
from pathlib import Path

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


def image_summary(path, max_dim=384):
    if not HAVE_PIL or not os.path.exists(path):
        return None
    try:
        img = Image.open(path).convert("RGB")
    except Exception:
        return None
    w, h = img.size
    if w * h > max_dim * max_dim:
        if w >= h:
            new_w = max_dim
            new_h = max(1, int(h * max_dim / w))
        else:
            new_h = max_dim
            new_w = max(1, int(w * max_dim / h))
        img = img.resize((new_w, new_h), Image.BILINEAR)
    pixels = list(img.getdata())
    n = len(pixels)
    if n == 0:
        return {"n": 0, "brightness": 0, "colors": 0, "non_dark": 0}
    bucket = {}
    brightness = 0
    non_dark = 0
    for r, g, b in pixels:
        brightness += r + g + b
        if r + g + b > 30:
            non_dark += 1
        key = (r >> 4, g >> 4, b >> 4)
        bucket[key] = bucket.get(key, 0) + 1
    return {
        "size": (w, h),
        "n": n,
        "brightness": brightness / (n * 3),
        "colors": len(bucket),
        "non_dark": non_dark / n,
    }


def load_platform(platform, csv_path, shots_dir):
    """Load classify_results.py output and pair it with per-shot summary."""
    rows = {}
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            bin_name = row["binary"]
            shot_path = os.path.join(shots_dir, f"{bin_name}.png")
            shot_summary = image_summary(shot_path)
            baseline_path = os.path.join(shots_dir, "_baseline.png")
            baseline_summary = image_summary(baseline_path) if os.path.exists(baseline_path) else None
            rows[bin_name] = {
                "verdict": row["verdict"],
                "exit_code": row["exit_code"],
                "renderer": row.get("diag_gl_renderer", ""),
                "shot": shot_summary,
                "baseline": baseline_summary,
            }
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platforms", required=True,
                    help="comma-separated name:csv:shots-dir triples, e.g. o6n:validation/o6n/results.csv:validation/o6n/raw/shots,medusa:...,pegasus:...")
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--out-csv", required=True)
    ap.add_argument("--brightness-threshold", type=float, default=8.0,
                    help="max mean_brightness below which a shot is 'visually black' (vs per-platform baseline)")
    ap.add_argument("--color-divergence-pct", type=float, default=70.0,
                    help="if relative color count between two platforms exceeds this, mark VISUAL_DIFFERENCE")
    ap.add_argument("--nd-div-threshold", type=float, default=30.0,
                    help="max percentage-point difference in non_dark_ratio between two platforms above which to flag VISUAL_DIFFERENCE")
    args = ap.parse_args()

    if not HAVE_PIL:
        print("FATAL: PIL not installed", file=sys.stderr)
        sys.exit(2)

    platforms = {}
    for spec in args.platforms.split(","):
        name, csv_path, shots_dir = spec.split(":")
        platforms[name] = load_platform(name, csv_path, shots_dir)

    # All binaries that appear in any platform
    all_bins = set()
    for p in platforms.values():
        all_bins.update(p.keys())

    # Compute baseline brightness per platform
    baselines = {}
    for name, rows in platforms.items():
        # Use the first binary's baseline value (all binaries share _baseline.png)
        for r in rows.values():
            if r["baseline"]:
                baselines[name] = r["baseline"]["brightness"]
                break

    # For each binary, build per-platform view and decide visual verdict
    out_rows = []
    summary_by_binary = {}
    for bin_name in sorted(all_bins):
        row = {"binary": bin_name}
        any_pass = False
        all_black = True  # tracks if PASSes are actually blank
        any_crash = False
        any_hang = False
        for pname, prows in platforms.items():
            r = prows.get(bin_name)
            if not r:
                row[pname] = "MISSING"
                continue
            row[f"{pname}_verdict"] = r["verdict"]
            row[f"{pname}_renderer"] = r["renderer"][:40]
            if r["verdict"] == "CRASH":
                any_crash = True
            if r["verdict"] == "HANG":
                any_hang = True
            if r["verdict"] == "PASS":
                any_pass = True
                shot = r["shot"] or {}
                bl = r["baseline"] or {}
                shot_b = shot.get("brightness", 0)
                base_b = bl.get("brightness", 0)
                shot_colors = shot.get("colors", 0)
                base_colors = bl.get("colors", 0)
                # VISUAL_BLACK heuristic. A screensaver is "black" only if
                # the shot is essentially identical to the per-platform
                # baseline (which means the screensaver didn't render
                # anything visible). Threshold: shot must be within
                # 5 brightness units AND within 20% of baseline colors.
                # We deliberately do NOT use absolute brightness — many
                # legitimate screensavers (antinspect, molecule, etc.)
                # are intentionally dark but still draw with variation.
                brightness_close = abs(shot_b - base_b) < 5
                if base_colors > 0:
                    color_close = abs(shot_colors - base_colors) < base_colors * 0.2 + 10
                else:
                    color_close = shot_colors < 20
                if brightness_close and color_close:
                    row[pname] = "BLACK"
                else:
                    row[pname] = "PASS"
                    all_black = False
            else:
                row[pname] = r["verdict"]
        # Aggregate verdict
        if any_crash:
            row["agg"] = "CRASH"
        elif any_hang:
            row["agg"] = "HANG"
        elif not any_pass:
            row["agg"] = "SKIP"
        elif all_black:
            row["agg"] = "BLACK"
        else:
            # Check for vendor divergence: did some platform show a
            # real screensaver render and another show BLACK?
            verdicts = [row.get(p, "?") for p in platforms]
            if any(v == "BLACK" for v in verdicts) and any(v == "PASS" for v in verdicts):
                row["agg"] = "VISUAL_DIFFERENCE (vendor-dependent BLACK)"
            else:
                row["agg"] = "PASS"
        out_rows.append(row)

    # Detect color/non-dark divergence among PASS+BLACK rows
    for row in out_rows:
        if row.get("agg") not in ("PASS", "BLACK", "VISUAL_DIFFERENCE (vendor-dependent BLACK)"):
            continue
        # Compare pairwise color counts and non-dark ratios
        max_rel_div = 0
        max_nd_div = 0
        for a in platforms:
            for b in platforms:
                if a >= b:
                    continue
                ra = platforms[a].get(row["binary"], {}).get("shot") or {}
                rb = platforms[b].get(row["binary"], {}).get("shot") or {}
                ca = ra.get("colors", 0)
                cb = rb.get("colors", 0)
                na = ra.get("non_dark", 0)
                nb = rb.get("non_dark", 0)
                # color bucket divergence
                if ca == 0 and cb == 0:
                    rel = 0
                elif max(ca, cb) == 0:
                    rel = 0
                else:
                    rel = abs(ca - cb) / max(ca, cb) * 100
                if rel > max_rel_div:
                    max_rel_div = rel
                # non-dark ratio divergence (more resolution-independent)
                nd_div = abs(na - nb) * 100
                if nd_div > max_nd_div:
                    max_nd_div = nd_div
        row["color_max_rel_div_pct"] = round(max_rel_div, 1)
        row["non_dark_max_div_pp"] = round(max_nd_div, 1)
        # If two platforms show VERY different "filled-screen" ratios,
        # that's a real visual difference regardless of absolute colors.
        if max_nd_div > args.nd_div_threshold and row["agg"] == "PASS":
            row["agg"] = f"VISUAL_DIFFERENCE (non_dark ratio varies {max_nd_div:.0f}pp)"
        elif max_rel_div > args.color_divergence_pct and row["agg"] == "PASS":
            row["agg"] = "VISUAL_DIFFERENCE (color palette varies)"

    # Write outputs
    with open(args.out_json, "w") as f:
        json.dump({
            "platforms": list(platforms.keys()),
            "baselines": baselines,
            "rows": out_rows,
        }, f, indent=2)

    fieldnames = ["binary", "agg"]
    for p in platforms:
        fieldnames += [p, f"{p}_verdict", f"{p}_renderer"]
    fieldnames += ["color_max_rel_div_pct", "non_dark_max_div_pp"]

    with open(args.out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for row in out_rows:
            w.writerow({k: row.get(k, "") for k in fieldnames})

    # Summary
    counts = {}
    for r in out_rows:
        counts[r.get("agg", "?")] = counts.get(r.get("agg", "?"), 0) + 1
    print(f"Cross-platform summary ({len(out_rows)} binaries):")
    for k, v in sorted(counts.items(), key=lambda x: -x[1]):
        print(f"  {k}: {v}")


if __name__ == "__main__":
    main()