# flocks_gles3

**Classification:** BROKEN
**GPU:** Intel UHD (CML GT2)
**Frame count:** 19
**Coverage:** 0.3%   **Saturation:** 0.0%   **Motion:** 0.6%   **Intensity:** 100.0
**Frame_ms:** 996.3
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Coverage 0.3%, sat=0%, motion 0.6% — the framebuffer is essentially black with a
few grayscale cursor-marker sprites in the upper-right and a single dot in the
lower-right. The intended render is a flocking/boids animation with many
colored birds. Visual inspection of `small_10.png` confirms: only the cursor
widget is drawing. Same rss-sdl2 family defect as cyclone/fieldlines. BROKEN.

## Evidence
- `small_00.png` … `small_18.png` — nineteen sample frames
