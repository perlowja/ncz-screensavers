# hyperspace_gles3

**Classification:** BROKEN
**GPU:** Intel UHD (CML GT2)
**Frame count:** 18
**Coverage:** 0.1%   **Saturation:** 0.1%   **Motion:** 0.1%   **Intensity:** 97.7
**Frame_ms:** 996.1
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Coverage 0.1% with motion 0.1% — essentially a black frame with one sparse
cluster of faint grayscale cursor sprites. Visual inspection of `small_09.png`:
mostly black with one short grey trail/sprite in the lower-left and a tiny dot
above it. The intended render is a hyperspace star-streak effect with many
streaking stars. Same rss-sdl2 family defect. BROKEN.

## Evidence
- `small_00.png` … `small_17.png` — eighteen sample frames
