# euphoria_gles3

**Classification:** BROKEN
**GPU:** Intel UHD (CML GT2)
**Frame count:** 20
**Coverage:** 76.7%   **Saturation:** 2.2%   **Motion:** 2.4%   **Intensity:** 12.2
**Frame_ms:** 996.5
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Coverage says 76.7% but sat=2.2% and vis_int=12.2 (very dim) — this is a near-black
frame with a few dim grey box-shaped sprites, NOT the intended euphoria particle
explosion. Motion 2.4% across 20 frames means the sprites barely move. Visual
inspection of `small_10.png` shows a black canvas with a handful of dark grey
boxes and a sparse grey vertical smear. Same rss-sdl2 family defect as cyclone:
only the gles3_compat cursor widget renders. BROKEN.

## Evidence
- `small_00.png` … `small_19.png` — twenty sample frames
