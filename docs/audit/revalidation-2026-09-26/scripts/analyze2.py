#!/usr/bin/env python3
"""Re-analyze targets where analyze.py failed (mixed frame sizes).

Reads ALL frame_*.png in the dir, infers W,H per file from byte-size,
and computes coverage/saturation/motion metrics. Writes metrics.json + small_*.png.
"""
import sys, os, json, glob
from pathlib import Path
from PIL import Image
import numpy as np

CANDIDATES = [(1920, 1080), (1280, 720), (960, 720), (1440, 1440), (1080, 1920), (2560, 1440)]

def infer_wh(nbytes):
    if nbytes == 0:
        return None, None  # sentinel: empty file
    npix = nbytes // 4
    for W, H in CANDIDATES:
        if W * H == npix:
            return W, H
    return None, None  # non-canonical / truncated size — skip

def load(path):
    with open(path, 'rb') as f:
        data = f.read()
    W, H = infer_wh(len(data))
    if W is None:
        return None, None, None
    return np.frombuffer(data, dtype=np.uint8).reshape(H, W, 4), W, H

def metrics(im):
    rgb = im[:, :, :3].astype(np.float32)
    visible = (rgb.sum(axis=2) > 24)
    cov = float(visible.mean())
    r, g, b = rgb[:,:,0], rgb[:,:,1], rgb[:,:,2]
    maxc = np.maximum(np.maximum(r, g), b)
    minc = np.minimum(np.minimum(r, g), b)
    chroma = maxc - minc
    sat = float((chroma > 30).mean())
    vis_int = rgb[visible].mean() if visible.any() else 0.0
    return cov, sat, float(vis_int)

def main(capture_dir, target, downscale_w=960):
    p = Path(capture_dir)
    frames = sorted([f for f in p.glob("frame_*.png") if not str(f).endswith('.rgba')])
    if not frames:
        print(f"no frames in {capture_dir}")
        return
    summary = {"target": target, "nframes_total": len(frames), "frames": []}
    metrics_by_frame = []
    if len(frames) >= 5:
        sample_idx = [0, len(frames)//4, len(frames)//2, 3*len(frames)//4, len(frames)-1]
    else:
        sample_idx = list(range(len(frames)))
    sample_idx = sorted(set(sample_idx))
    primary_wh = None
    for i, fp in enumerate(frames):
        im_arr, W, H = load(fp)
        if im_arr is None:
            continue
        if primary_wh is None:
            primary_wh = (W, H)
        cov, sat, vis_int = metrics(im_arr)
        metrics_by_frame.append({"frame": fp.name, "cov": cov, "sat": sat, "vis_int": vis_int})
        if i in sample_idx:
            rgb_only = im_arr[:, :, :3]
            img = Image.fromarray(rgb_only, mode='RGB')
            ratio = downscale_w / W
            new_size = (downscale_w, int(H * ratio))
            small = img.resize(new_size, Image.LANCZOS)
            small.save(str(p / f"small_{i:02d}.png"))
    if primary_wh:
        summary["width"], summary["height"] = primary_wh
    if len(metrics_by_frame) >= 2:
        a, _, _ = load(frames[0])
        if a is not None:
            a = a.astype(np.int32)
            diffs = []
            for fp in frames[1:]:
                b, _, _ = load(fp)
                if b is None: continue
                b = b.astype(np.int32)
                if b.shape != a.shape:
                    # size mismatch mid-capture; skip
                    continue
                d = np.abs(a - b).sum(axis=2)
                changed = float((d > 12).mean())
                diffs.append(changed)
                a = b
            summary["motion"] = {
                "mean_changed_pct": float(np.mean(diffs)) if diffs else 0.0,
                "max_changed_pct": float(np.max(diffs)) if diffs else 0.0,
                "min_changed_pct": float(np.min(diffs)) if diffs else 0.0,
            }
    else:
        summary["motion"] = {"mean_changed_pct": 0.0, "max_changed_pct": 0.0, "min_changed_pct": 0.0}
    covs = [m["cov"] for m in metrics_by_frame]
    sats = [m["sat"] for m in metrics_by_frame]
    ints = [m["vis_int"] for m in metrics_by_frame]
    summary["coverage_mean"] = float(np.mean(covs)) if covs else 0.0
    summary["coverage_max"] = float(np.max(covs)) if covs else 0.0
    summary["saturation_mean"] = float(np.mean(sats)) if sats else 0.0
    summary["saturation_max"] = float(np.max(sats)) if sats else 0.0
    summary["intensity_mean"] = float(np.mean(ints)) if ints else 0.0
    summary["per_frame"] = metrics_by_frame
    log = p / "framebuffer.log"
    if log.exists():
        lines = log.read_text().splitlines()
        frames_log = []
        for ln in lines:
            if "framebuffer frame=" in ln:
                try:
                    fn = int(ln.split("frame=")[1].split()[0])
                    frames_log.append(fn)
                except Exception:
                    pass
        if len(frames_log) >= 2:
            deltas = np.diff(frames_log) * (1000.0/60.0)
            summary["frame_ms_mean"] = float(np.mean(deltas))
            summary["frame_ms_var"] = float(np.var(deltas))
            summary["n_frame_samples"] = len(frames_log)
    (p / "metrics.json").write_text(json.dumps(summary, indent=2))
    print(f"[{target}] valid={len(metrics_by_frame)}/{len(frames)} cov={summary['coverage_mean']*100:.1f}% sat={summary['saturation_mean']*100:.1f}% motion={summary['motion']['mean_changed_pct']*100:.1f}% wh={primary_wh}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("usage: analyze2.py <capture-dir> <target>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
