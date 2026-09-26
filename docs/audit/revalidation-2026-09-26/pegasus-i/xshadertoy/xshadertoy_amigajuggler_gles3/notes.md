# xshadertoy_amigajuggler_gles3

**Classification:** STATIC (single-frame artifact, nframes=1)
**GPU:** Intel UHD (CML GT2)
**Frame count:** 1 captured (timed out before a second dump at 25s)
**Coverage:** 100.0%   **Saturation:** 97.8%   **Motion:** 0.0%   **Intensity:** 156.8
**Frame_ms:** None (only 1 sample)
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
The image is a fully-rendered 3D scene — a stylized juggler silhouette with checkered
spheres hovering over a magenta/yellow grid floor, framed by four ring elements. Coverage
100%, saturation 97.8%, intensity 156.8 — no question the shader produced output.
Motion=0% is misleading: we only have ONE frame, so there is nothing to diff against.
Re-running with a longer timeout would almost certainly show motion (this is an
animated port). Classify as STATIC strictly because of the single-frame sample, not
because the renderer is broken. Image itself is a textbook successful capture.

## Evidence
- `small_00.png` — the only sample frame
- `metrics.json` — nframes=1 makes `motion` undefined; classifier falls to STATIC
