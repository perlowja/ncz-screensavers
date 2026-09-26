# plasma_gles3

**Classification:** BROKEN (partial render)
**GPU:** Intel UHD (CML GT2)
**Frame count:** 17
**Coverage:** 20.4%   **Saturation:** 20.4%   **Motion:** 19.9%   **Intensity:** 106.4
**Frame_ms:** 995.8
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Visual inspection of `small_08.png`: a small rainbow-colored plasma-like swirl
in the lower-left of the frame, with the rest of the frame completely black.
The intended render is a fullscreen plasma effect — this captures only a
small portion of it. Coverage 20.4% (mostly empty), saturation 20.4%
(only the swirl has chroma), motion 19.9%. BROKEN (partial).

## Evidence
- `small_00.png` … `small_16.png` — seventeen sample frames
