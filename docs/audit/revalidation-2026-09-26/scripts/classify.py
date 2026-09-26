#!/usr/bin/env python3
"""Classify a target from metrics.json. Returns one of:
GOOD, BLACK, WASHED, STATIC, BROKEN, HUNG.
"""
import json, sys

def classify(m):
    cov = m.get("coverage_mean", 0.0)
    sat = m.get("saturation_mean", 0.0)
    motion = m.get("motion", {}).get("mean_changed_pct", 0.0)
    vis = m.get("intensity_mean", 0.0)
    nf = m.get("nframes", 0) or m.get("nframes_total", 0)
    fm = m.get("frame_ms_mean")
    # BLACK: nearly no visible content
    if cov < 0.05 and vis < 5.0:
        return "BLACK"
    # BROKEN: no frames captured at all
    if nf == 0:
        return "BROKEN"
    # WASHED: high coverage but very low saturation and high intensity = blown out
    if cov > 0.85 and sat < 0.05 and vis > 200:
        return "WASHED"
    # STATIC: lots of visible content but no motion
    if cov > 0.5 and motion < 0.05:
        return "STATIC"
    # GOOD: visible, animated, colorful (or appropriately monochromatic)
    return "GOOD"

def main(path):
    m = json.load(open(path))
    cls = classify(m)
    cov = m.get("coverage_mean", 0.0) * 100
    sat = m.get("saturation_mean", 0.0) * 100
    motion = m.get("motion", {}).get("mean_changed_pct", 0.0) * 100
    vis = m.get("intensity_mean", 0.0)
    fm = m.get("frame_ms_mean")
    nf = m.get("nframes", 0) or m.get("nframes_total", 0)
    print(f"{cls}\tn={nf}\tcov={cov:5.1f}%\tsat={sat:5.1f}%\tmotion={motion:5.1f}%\tvis={vis:5.1f}\tfm={fm}")

if __name__ == "__main__":
    main(sys.argv[1])
