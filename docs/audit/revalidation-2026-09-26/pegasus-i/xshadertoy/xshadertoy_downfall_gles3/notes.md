# xshadertoy_downfall_gles3

**Classification:** GOOD
**GPU:** Intel UHD (CML GT2)
**Frame count:** 2
**Coverage:** 78.8%   **Saturation:** 0.0%   **Motion:** 82.5%   **Intensity:** 64.0
**Frame_ms:** 933.3
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Saturation=0.0% sounds bad but is correct: this shader renders in pure grayscale
volumetric smoke, like a vertical waterfall/chimney plume against a black field.
Coverage 79% (the plume dominates), motion 82.5% across the two samples (smoke
turbulence). Visually a moody monochromatic plume — exactly the look "downfall"
implies. GOOD.

## Evidence
- `small_00.png`, `small_01.png`
