# xshadertoy_neongravity-0_gles3

**Classification:** BLACK
**GPU:** Intel UHD (CML GT2)
**Frame count:** 19 captured (all empty)
**Coverage:** 0.0%   **Saturation:** 0.0%   **Motion:** 0.0%   **Intensity:** 0.0
**Frame_ms:** 996.3
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Every one of the 19 captured frames is empty: coverage 0%, intensity 0.0, motion 0%.
The framebuffer diagnostic in `framebuffer.log` shows `nonblack=0` for every
sample, meaning `glReadPixels` returned a fully-black buffer. Yet `log.txt` shows
the harness got `gles3_compat: shader program N compiled` messages without errors,
and the run ran for the full ~30s before timeout. So the shader compiles and the
loop iterates — but this target is producing no visible output at all on this
GPU. This is a real defect on neongravity-0 specifically; its sibling
`xshadertoy_neongravity-1_gles3` (same source family) renders correctly, so it
isn't a class-wide issue.

## Evidence
- `framebuffer.log` — 19 entries all with nonblack=0
- `log.txt` — clean compilation, no GL errors, normal frame pacing
- `small_00.png` — single black frame (2759 bytes confirms near-empty)
