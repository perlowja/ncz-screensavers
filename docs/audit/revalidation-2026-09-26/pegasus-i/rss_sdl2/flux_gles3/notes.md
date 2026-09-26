# flux_gles3

**Classification:** BROKEN
**GPU:** Intel UHD (CML GT2)
**Frame count:** 19
**Coverage:** 1.9%   **Saturation:** 1.3%   **Motion:** 2.9%   **Intensity:** 225.6
**Frame_ms:** 996.3
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Coverage 1.9% with intensity 225.6 — a near-black canvas with a sparse cluster of
near-white grayscale sprites. Visual inspection of `small_10.png` confirms: black
canvas with one tiny cluster of cursor-marker sprites in the lower-right. The
intended render is a colorful fluid/flux visualization. Same rss-sdl2 family
defect. BROKEN.

## Evidence
- `small_00.png` … `small_18.png` — nineteen sample frames
