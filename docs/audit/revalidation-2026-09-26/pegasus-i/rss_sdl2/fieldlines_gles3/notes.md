# fieldlines_gles3

**Classification:** BROKEN
**GPU:** Intel UHD (CML GT2)
**Frame count:** 19
**Coverage:** 0.0%   **Saturation:** 0.0%   **Motion:** 0.1%   **Intensity:** 255.0
**Frame_ms:** 996.3
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
The metrics look paradoxical — cov=0% but vis_int=255 — which means the few
visible pixels are pure white. Inspecting `small_10.png`: a black canvas with a
small cluster of white X-shaped cursor sprites in the upper-right and a single
stray dot near center. The intended render is a colorful vector field with many
curved field-line traces. Only the cursor widget renders. BROKEN.

The 255-mean-intensity artifacts are a curiosity: the renderer is writing to the
back buffer correctly for the cursor sprite (one pure-white pixel), but my
"visible" predicate (`rgb.sum > 24`) sees a near-zero fraction of those pixels,
so coverage=0%. The mean is dragged to 255 by those few bright pixels.

## Evidence
- `small_00.png` … `small_18.png` — nineteen sample frames, all near-identical
