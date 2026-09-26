# lattice_gles3

**Classification:** BROKEN (partial render)
**GPU:** Intel UHD (CML GT2)
**Frame count:** 18
**Coverage:** 8.6%   **Saturation:** 8.6%   **Motion:** 16.6%   **Intensity:** 151.2
**Frame_ms:** 996.1
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Visual inspection of `small_09.png`: a wireframe lattice of small blue/cyan cubes
on a black background in the lower-left, plus a colorful gradient strip on the
right edge. Only 8.6% coverage — most of the frame is empty black. The wireframe
geometry that IS rendering looks correct (3D cubes with proper perspective),
but the scene is missing most of its content. Likely a partial draw or scene
that was designed to fill more space. BROKEN (partial).

## Evidence
- `small_00.png` … `small_17.png` — eighteen sample frames
