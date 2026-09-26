# cyclone_gles3

**Classification:** BROKEN
**GPU:** Intel UHD (CML GT2)
**Frame count:** 20 captured (sweep timed out at 30s)
**Coverage:** 0.1%   **Saturation:** 0.0%   **Motion:** 0.2%   **Intensity:** 112.1
**Frame_ms:** 996.5
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
The framebuffer is essentially black — 0.1% coverage, 0.2% motion — and what little
content is visible is a sparse vertical column of white dots in the center, a tiny
hint of a spiral pattern. The intended render is a colorful swirling cyclone. Only
the cursor-marker widget sprites are getting drawn; the actual cyclone geometry is
not. This is consistent with the rss-sdl2 family defect documented in `MEMORY.md`:
the gles3_compat layer handles only the cursor widget, not the renderer-specific
particle/vector content. BROKEN.

## Evidence
- `small_00.png` … `small_19.png` — twenty sample frames, all essentially identical
- `framebuffer.log` — nonblack count never rises meaningfully
