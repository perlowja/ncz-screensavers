# xshadertoy_logarithmiccircles_gles3

**Classification:** GOOD (WASHED-likely-at-edge, but visually still content-bearing)
**GPU:** Intel UHD (CML GT2)
**Frame count:** 20
**Coverage:** 50.9%   **Saturation:** 0.0%   **Motion:** 40.4%   **Intensity:** 235.3
**Frame_ms:** 996.5
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
This is a deliberate near-white rendering — a textured grayscale surface with the
logarithmic-spiral pattern faintly visible. Coverage 50.9%, sat=0.0% (pure
grayscale), intensity 235.3 (very bright, near-saturated white). At first glance
the metrics look "washed" but `small_10.png` clearly shows a structured grayscale
texture with logarithmic spiral pattern — NOT a blown-out frame. The intensity is
intentional: this shader uses a paper-like white background with subtle grey
spiral overlay. Motion 40% reflects the spiral subtly evolving. Renders as
intended.

## Evidence
- `small_00.png` … `small_19.png` — twenty sample frames
