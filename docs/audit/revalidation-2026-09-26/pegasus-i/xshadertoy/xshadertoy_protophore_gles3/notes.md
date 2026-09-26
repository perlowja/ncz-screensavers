# xshadertoy_protophore_gles3

**Classification:** GOOD
**GPU:** Intel UHD (CML GT2)
**Frame count:** 9
**Coverage:** 100.0%   **Saturation:** 0.3%   **Motion:** 75.2%   **Intensity:** 118.6
**Frame_ms:** 991.7
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
A glossy 3D sculptural form rendered in metallic purple — looks like a procedural
organic shape with PBR-style shading. Saturation 0.3% is misleading: the actual
image is dominated by purple highlights and reflections, but the chroma-bearing
pixels stay below my 30-chroma threshold because the shader uses wide gradient
transitions rather than saturated color blocks. Coverage 100%, motion 75.2% across
9 frames (the object rotates / evolves). Visually correct.

## Evidence
- `small_00.png` … `small_08.png` — nine sample frames
