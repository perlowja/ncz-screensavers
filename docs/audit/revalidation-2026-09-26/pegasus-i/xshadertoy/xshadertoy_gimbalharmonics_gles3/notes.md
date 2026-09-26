# xshadertoy_gimbalharmonics_gles3

**Classification:** GOOD
**GPU:** Intel UHD (CML GT2)
**Frame count:** 6
**Coverage:** 100.0%   **Saturation:** 0.2%   **Motion:** 86.3%   **Intensity:** 130.8
**Frame_ms:** 986.7
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
A 3D metallic gimbal/gyroscope device with concentric rotating rings around a glowing
pink core. Saturation 0.2% is misleading — the actual rendered image uses mostly
greys with a tiny pink hotspot, so my saturation threshold (chroma > 30) catches
almost nothing because most of the chroma-bearing pixels are below the threshold.
Coverage 100%, motion 86.3% across 6 frames — the rings are clearly rotating.
Visually correct.

## Evidence
- `small_00.png` … `small_05.png` — six sample frames
