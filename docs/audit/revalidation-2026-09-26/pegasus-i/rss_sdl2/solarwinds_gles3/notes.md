# solarwinds_gles3

**Classification:** BROKEN (partial render)
**GPU:** Intel UHD (CML GT2)
**Frame count:** 25 (re-captured after /tmp-full recovery)
**Coverage:** 46.1%   **Saturation:** 0.0%   **Motion:** 60.8%   **Intensity:** 151.0
**Frame_ms:** 997.2
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 124

## Verdict
Visual inspection of `small_12.png` and the other captured frames: bright white
particle bursts and spiral structures on a black background. Coverage 46.1%,
motion 60.8% — animation is clearly happening and there's plenty of visible
content. But saturation is 0.0% (pure grayscale) and the visual is a
firework-like particle burst, not the colorful "solar wind" effect the target
is named for. Likely the renderer's colour mapping is failing or the shader
compiled without colour state. Either way: structurally it renders content
but the visual is wrong — BROKEN.

Note: initial sweep produced 17 frames, all 0-byte because PEGASUS `/tmp` was
100% full from the prior batch (see recovery commit). After `rm -rf /tmp/cap-*`
on the remote, re-capture produced 25 valid frames.

## Evidence
- `small_07.png` … `small_24.png` — particle bursts in monochrome white
- `metrics.json` — nframes_total=25
