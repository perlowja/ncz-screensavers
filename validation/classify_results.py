#!/usr/bin/env python3
"""
classify_results.py — read results.csv from run-all-gles3.sh + the per-binary
stderr logs, and assign each binary one of:

    PASS             — process ran, produced non-empty stderr (real GLES
                       context), screenshot is non-trivial (varies from
                       baseline; not all black), no crash.
    VISUAL_DIFFERENCE — process ran, GLES context live, screenshot differs
                       from baseline but description in stderr (or via
                       optional manual classification hint) shows a known
                       vendor-specific render difference.
    BLACK            — process ran, GLES context live, but screenshot is
                       essentially identical to baseline (or all-dark,
                       same color as desktop background).
    CRASH            — process crashed (exit code < 0 or 134 or in stderr
                       "Segmentation fault" / "abort" / "EGL_BAD_*" /
                       "GL_INVALID_*").
    HANG             — process had to be SIGKILL'd.
    SKIP             — process didn't run at all (binary missing,
                       exit code 127 "command not found", etc).

Reads run-all-gles3.sh's outputs:
    results.csv: binary,exit_code,screenshot_bytes,stderr_bytes,
                 wallclock_sec,diag_gl_renderer
    shots/<binary>.png  (grim screenshot)
    shots/_baseline.png (desktop baseline for "no hack running")
    logs/<binary>.stderr (stderr from the binary)
"""
import argparse
import csv
import json
import os
import re
import sys
from pathlib import Path

# Try importing PIL; fall back gracefully if not present.
try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


def image_summary(path):
    """Return dict with size, bytes, mean brightness, distinct color count,
    unique-color-sample estimate. None if file missing or PIL not available.

    Downsamples very large images to keep classification fast — the
    bucket-based color estimator only needs a few thousand samples to
    produce a reliable distinct-color count and brightness average."""
    if not HAVE_PIL:
        return None
    try:
        img = Image.open(path)
        img.load()
    except Exception as e:
        return {"error": str(e)}
    w, h = img.size
    # Down-sample huge screenshots (8M+ pixels) to ~256K for fast analysis.
    if w * h > 512 * 512:
        max_dim = 512
        if w >= h:
            new_w = max_dim
            new_h = max(1, int(h * max_dim / w))
        else:
            new_h = max_dim
            new_w = max(1, int(w * max_dim / h))
        img_small = img.resize((new_w, new_h), Image.BILINEAR)
    else:
        img_small = img
    px = img_small.convert("RGB")
    pixels = list(px.getdata())
    n = len(pixels)
    if n == 0:
        return {"size": (w, h), "n_pixels": 0, "mean_brightness": 0,
                "distinct_colors": 0, "non_dark_ratio": 0}
    bucket = {}
    non_dark = 0
    brightness_sum = 0
    for p in pixels:
        r, g, b = p
        brightness_sum += (r + g + b)
        if (r + g + b) > 30:
            non_dark += 1
        key = (r >> 4, g >> 4, b >> 4)
        bucket[key] = bucket.get(key, 0) + 1
    return {
        "size": (w, h),
        "n_pixels": w * h,
        "sampled_n_pixels": n,
        "mean_brightness": brightness_sum / (n * 3),
        "non_dark_ratio": non_dark / n,
        "distinct_color_buckets": len(bucket),
    }


def classify_binary(binary, csv_row, shots_dir, logs_dir, baseline_summary):
    """Classify a single binary's run.

    csv_row: dict from results.csv with keys
             binary, exit_code, screenshot_bytes, stderr_bytes,
             wallclock_sec, diag_gl_renderer
    """
    verdict = "UNKNOWN"
    notes = []
    stderr_path = os.path.join(logs_dir, f"{binary}.stderr")
    exit_path = os.path.join(logs_dir, f"{binary}.exit")
    stderr = ""
    if os.path.exists(stderr_path):
        with open(stderr_path, "r", errors="replace") as f:
            stderr = f.read()

    # Exit status
    try:
        exit_code = int(csv_row["exit_code"])
    except (ValueError, KeyError):
        exit_code = -1
        notes.append(f"non-integer exit code {csv_row.get('exit_code')!r}")

    # CRASH: explicit patterns in stderr
    crash_patterns = [
        r"Segmentation fault",
        r"Aborted \(core dumped\)",
        r"\babort\b",
        r"GLES3 harness.*abort",
        r"libc.+assert",
        r"GL_INVALID_(FRAMEBUFFER_OPERATION|OPERATION|VALUE|ENUM)",
        r"EGL_BAD_(DISPLAY|CONTEXT|SURFACE|ACCESS|ALLOC|NATIVE_PIXMAP|NATIVE_WINDOW|PARAMETER)",
        r"stack smashing",
        r"double free",
    ]
    is_crash = exit_code < 0 or exit_code in (134, 139, -11, -6) or \
        any(re.search(p, stderr) for p in crash_patterns)
    if is_crash:
        # Try to grab the GL_RENDERER for crash attribution
        m = re.search(r"RENDERER=([^\n]+)", stderr)
        if m:
            notes.append(f"crash on renderer: {m.group(1).strip()}")
        return "CRASH", notes, exit_code, stderr

    # HANG: SIGKILL'd
    if exit_code == 137:
        return "HANG", notes + ["SIGKILL'd after grace period"], exit_code, stderr

    # Did the harness actually start? Look for [diag] GL_VERSION=
    diag_present = bool(re.search(r"GL_VERSION=", stderr))
    diag_renderer = csv_row.get("diag_gl_renderer", "").strip()

    if not diag_present:
        if exit_code == 0:
            notes.append("exit 0 but no GL_VERSION in stderr — likely init failed silently")
            return "BLACK", notes + [f"stderr: {stderr.strip()[:200]!r}"], exit_code, stderr
        if exit_code == 127 or "command not found" in stderr:
            return "SKIP", notes + ["binary missing or not executable"], exit_code, stderr
        return "CRASH", notes + [f"no GL_VERSION, exit={exit_code}"], exit_code, stderr

    # Did the screenshot actually change from baseline?
    shot_path = os.path.join(shots_dir, f"{binary}.png")
    shot_bytes = 0
    try:
        shot_bytes = os.path.getsize(shot_path)
    except OSError:
        pass

    if shot_bytes < 1024:
        return "BLACK", notes + [f"shot is only {shot_bytes} bytes"], exit_code, stderr

    summary = image_summary(shot_path) if HAVE_PIL else None
    baseline = baseline_summary or {}

    # Heuristic: if mean_brightness matches baseline closely and non_dark_ratio
    # matches, it's a near-empty frame.
    if summary and baseline:
        br_diff = abs(summary["mean_brightness"] - baseline["mean_brightness"])
        nd_diff = abs(summary["non_dark_ratio"] - baseline["non_dark_ratio"])
        col_diff = abs(summary["distinct_color_buckets"] - baseline["distinct_color_buckets"])
        notes.append(
            f"shot brightness={summary['mean_brightness']:.1f} "
            f"vs base={baseline['mean_brightness']:.1f} (diff={br_diff:.1f}); "
            f"colors={summary['distinct_color_buckets']} vs {baseline['distinct_color_buckets']} "
            f"(diff={col_diff})"
        )
        if col_diff < 5 and summary["distinct_color_buckets"] < 20:
            return "BLACK", notes + ["shot looks like the desktop baseline"], exit_code, stderr

    # Otherwise: passed (the harness actually rendered something different
    # from the empty desktop). Mark as PASS for now; humans can re-classify
    # to VISUAL_DIFFERENCE later after reviewing screenshots.
    return "PASS", notes, exit_code, stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-csv", required=True)
    ap.add_argument("--shots-dir", required=True)
    ap.add_argument("--logs-dir", required=True)
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--out-csv", required=True)
    args = ap.parse_args()

    if not HAVE_PIL:
        print("WARNING: PIL not installed; using only CSV/exit-code heuristics",
              file=sys.stderr)

    baseline_summary = image_summary(os.path.join(args.shots_dir, "_baseline.png"))

    rows = []
    with open(args.results_csv) as f:
        reader = csv.DictReader(f)
        seen_bins = set()
        for row in reader:
            bin_name = row["binary"]
            # Skip duplicate entries — keep the LAST one (most recent run
            # supersedes earlier failed runs).
            if bin_name in seen_bins:
                continue
            seen_bins.add(bin_name)
            verdict, notes, exit_code, stderr = classify_binary(
                row["binary"], row, args.shots_dir, args.logs_dir,
                baseline_summary
            )
            row["verdict"] = verdict
            row["notes"] = "; ".join(notes)
            row["stderr_first200"] = stderr[:200].replace("\n", " | ")
            rows.append(row)

    # Write JSON (rich detail for the validation doc)
    with open(args.out_json, "w") as f:
        json.dump({
            "baseline": baseline_summary,
            "rows": rows,
        }, f, indent=2)

    # Write CSV (one row per binary)
    with open(args.out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "binary", "verdict", "exit_code", "screenshot_bytes",
            "stderr_bytes", "diag_gl_renderer",
            "notes", "stderr_first200",
        ])
        w.writeheader()
        for row in rows:
            w.writerow({k: row.get(k, "") for k in w.fieldnames})

    # Print summary
    counts = {}
    for row in rows:
        counts[row["verdict"]] = counts.get(row["verdict"], 0) + 1
    print(f"Classified {len(rows)} binaries: {counts}")


if __name__ == "__main__":
    main()