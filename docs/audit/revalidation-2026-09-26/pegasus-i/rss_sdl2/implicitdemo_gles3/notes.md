# implicitdemo_gles3

**Classification:** BROKEN (partial)
**GPU:** Intel UHD (CML GT2)
**Frame count:** 18
**Coverage:** 2.7%   **Saturation:** 0.3%   **Motion:** 23.5%   **Intensity:** 25.4
**Frame_ms:** 996.1
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Coverage 2.7% (slightly more than the other rss-sdl2 BROKEN targets) and motion
23.5% (the highest in this batch), but the actual rendered content is a single
faint thin ribbon-like structure with low-chroma magenta/green/blue against black.
Visual inspection of `small_09.png` shows a sparse faint vertical smear — nothing
like the implicit-surface demo the target is named for. Same rss-sdl2 family
defect: only the cursor widget renders, and in this case it produces a thin
ribbon rather than a cluster. BROKEN (partial).

## Evidence
- `small_00.png` … `small_17.png` — eighteen sample frames
