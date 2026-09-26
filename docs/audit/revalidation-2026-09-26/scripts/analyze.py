#!/usr/bin/env python3
"""
Analyze captured frame dumps: compute coverage, saturation, motion metrics.
Also produce downscaled PNGs (max 960px wide) for repo commit.
Also produce a downscaled frame for visual inspection.
"""
import sys, os, json, glob, struct
from pathlib import Path
from PIL import Image
import numpy as np

def load_rgba(path):
    """Load raw RGBA from a frame_*.png (the harness writes raw RGBA bytes there)."""
    with open(path, 'rb') as f:
        data = f.read()
    # 1920x1080x4 = 8294400 bytes. Detect width/height from filename's pair.
    # All targets use 1920x1080, but be safe.
    W, H = 1920, 1080
    if len(data) == W*H*4:
        pass
    elif len(data) == 960*720*4:
        W, H = 960, 720
    elif len(data) == 1280*720*4:
        W, H = 1280, 720
    else:
        # fallback: try to guess by sqrt
        npix = len(data) // 4
        wh = int(npix ** 0.5)
        W = H = wh
    return np.frombuffer(data, dtype=np.uint8).reshape(H, W, 4)

def metrics(im):
    """Return coverage, saturation, mean_intensity, colorfulness."""
    rgb = im[:, :, :3].astype(np.float32)
    a = im[:, :, 3]
    # Coverage: any RGB channel > 8 (avoid "near-black" noise).
    # IMPORTANT: alpha can be 0 on renderers that write RGB only; do NOT filter on alpha.
    visible = (rgb.sum(axis=2) > 24)
    coverage = float(visible.mean())
    # Saturation: pixels with strong chroma
    r, g, b = rgb[:,:,0], rgb[:,:,1], rgb[:,:,2]
    maxc = np.maximum(np.maximum(r, g), b)
    minc = np.minimum(np.minimum(r, g), b)
    chroma = maxc - minc
    sat = float((chroma > 30).mean())
    # Mean intensity of visible pixels
    vis_int = rgb[visible].mean() if visible.any() else 0.0
    return coverage, sat, float(vis_int)

def main(capture_dir, target, downscale_w=960):
    """Process all frame_*.png in capture_dir; write small pngs + metrics.json."""
    p = Path(capture_dir)
    frames = sorted(p.glob("frame_*.png"))
    # skip duplicate (.rgba) files
    frames = [f for f in frames if not str(f).endswith('.rgba')]

    # Determine canvas size from first frame
    if not frames:
        print(f"no frames in {capture_dir}")
        return
    raw = np.frombuffer(open(frames[0],'rb').read(), dtype=np.uint8)
    npix = len(raw) // 4
    H = W = int(npix ** 0.5)

    summary = {"target": target, "nframes": len(frames), "width": W, "height": H, "frames": []}
    metrics_by_frame = []

    # Pick representative frames: earliest, mid, latest, and spaced samples
    if len(frames) >= 5:
        sample_idx = [0, len(frames)//4, len(frames)//2, 3*len(frames)//4, len(frames)-1]
    else:
        sample_idx = list(range(len(frames)))
    sample_idx = sorted(set(sample_idx))

    for i, fp in enumerate(frames):
        im_arr = load_rgba(fp)
        cov, sat, vis_int = metrics(im_arr)
        metrics_by_frame.append({"frame": fp.name, "cov": cov, "sat": sat, "vis_int": vis_int})

        # Save a downscaled PNG at sample frames
        if i in sample_idx:
            # The harness writes raw RGBA but renderers often leave alpha=0,
            # which makes PIL display a black frame and LANCZOS resize kill
            # content. Save RGB-only (alpha stripped) so the image renders
            # correctly in viewers and resizes don't blend to black.
            rgb_only = im_arr[:, :, :3]
            img = Image.fromarray(rgb_only, mode='RGB')
            ratio = downscale_w / W
            new_size = (downscale_w, int(H * ratio))
            small = img.resize(new_size, Image.LANCZOS)
            small.save(str(p / f"small_{i:02d}.png"))

    # Motion: inter-frame diff between adjacent captured frames
    if len(frames) >= 2:
        a = load_rgba(frames[0]).astype(np.int32)
        diffs = []
        for fp in frames[1:]:
            b = load_rgba(fp).astype(np.int32)
            d = np.abs(a - b).sum(axis=2)
            changed = float((d > 12).mean())
            diffs.append(changed)
            a = b
        summary["motion"] = {
            "mean_changed_pct": float(np.mean(diffs)),
            "max_changed_pct": float(np.max(diffs)),
            "min_changed_pct": float(np.min(diffs)),
        }
    else:
        summary["motion"] = {"mean_changed_pct": 0.0, "max_changed_pct": 0.0, "min_changed_pct": 0.0}

    # Coverage/saturation summary
    covs = [m["cov"] for m in metrics_by_frame]
    sats = [m["sat"] for m in metrics_by_frame]
    ints = [m["vis_int"] for m in metrics_by_frame]
    summary["coverage_mean"] = float(np.mean(covs))
    summary["coverage_max"] = float(np.max(covs))
    summary["saturation_mean"] = float(np.mean(sats))
    summary["saturation_max"] = float(np.max(sats))
    summary["intensity_mean"] = float(np.mean(ints))
    summary["per_frame"] = metrics_by_frame

    # Frame-time stats (approximation: from log if available)
    log = p / "framebuffer.log"
    if log.exists():
        lines = log.read_text().splitlines()
        frames_log = []
        for ln in lines:
            if "framebuffer frame=" in ln:
                # parse frame=N
                try:
                    fn = int(ln.split("frame=")[1].split()[0])
                    frames_log.append(fn)
                except Exception:
                    pass
        if len(frames_log) >= 2:
            deltas = np.diff(frames_log) * (1000.0/60.0)  # ~60fps baseline
            summary["frame_ms_mean"] = float(np.mean(deltas))
            summary["frame_ms_var"] = float(np.var(deltas))
            summary["n_frame_samples"] = len(frames_log)
        else:
            summary["frame_ms_mean"] = None
            summary["frame_ms_var"] = None

    (p / "metrics.json").write_text(json.dumps(summary, indent=2))
    print(f"[{target}] cov={summary['coverage_mean']*100:.1f}% sat={summary['saturation_mean']*100:.1f}% motion={summary['motion']['mean_changed_pct']*100:.1f}% frames={len(frames)}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("usage: analyze.py <capture-dir> <target>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
