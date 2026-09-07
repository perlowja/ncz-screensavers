# gluScaleImage shim — design notes (Round 12 timetunnel port, 2026-09-07)

## Background

`timetunnel.c:886` calls `gluScaleImage(GL_RGBA, srcW, srcH,
GL_UNSIGNED_BYTE, src, dstW, dstH, GL_UNSIGNED_BYTE, dst)` to rescale a
non-power-of-2 texture image to the nearest power-of-2 dimensions
before upload. The call site is wrapped in `#ifndef HAVE_JWZGLES`.

The legacy Mesa GLU `gluScaleImage` is a substantial piece of code
that handles many format/type combos (GL_LUMINANCE, GL_RGB, GL_RGBA,
GL_ALPHA in both 8-bit and float types) via GLU's pixel-pack pipeline.
Porting that whole thing to GLES3 is unnecessary work for our case:
**only timetunnel uses it, and timetunnel only ever uses the
GL_RGBA / GL_UNSIGNED_BYTE combo.**

So the shim implements exactly that combo and returns `GLU_ERROR`
without writing `dstData` for any other input — the conservative
contract, same as the upstream GLU returning `GLU_INVALID_ENUM` /
`GLU_INVALID_VALUE`.

## Math

For destination pixel `(dx, dy)` (0-indexed, GL bottom-left convention),
sample point in source-pixel coords:

```
sx = (dx + 0.5) * (srcW / dstW) - 0.5
sy = (dy + 0.5) * (srcH / dstH) - 0.5
```

This is the GPU-style "center of pixel" convention used by GLU, GPU
texture samplers, and `glViewport` mapping. Bilinear-interpolate between
the four source pixels `(floor(sx), floor(sy))`, `(ceil(sx), floor(sy))`,
`(floor(sx), ceil(sy))`, `(ceil(sx), ceil(sy))` with weights
`(1-fx)(1-fy)`, `fx(1-fy)`, `(1-fx)fy`, `fx*fy`.

Sample points outside `[0, srcW) × [0, srcH)` are **clamped** to the
nearest source edge pixel (clamp-to-edge, NOT wrap). This matches
GLU's documented behavior and is what a GPU sampler with
`GL_CLAMP_TO_EDGE` wrap mode would produce.

## Edge cases handled

- **`srcW == dstW && srcH == dstH`** — short-circuits to a single
  `memcpy`. No resampling, no float math, byte-exact preservation.
  Important: timetunnel always resizes (its `-which` selection may
  pick a power-of-2 input that doesn't need scaling, but checking
  costs ~2 comparisons).
- **`srcW == 1` or `srcH == 1`** — the loop still runs correctly; the
  inner `(sx1 >= srcW) ? srcW-1 : sx1` clamp catches it. With
  `srcW == 1`, both `sx0` and `sx1` end up at 0.
- **`dstW > srcW` (upsize)** — `sx_scale > 1.0`, bilinear weights
  spread the contribution of one source pixel across multiple
  destination pixels (smoothing).
- **`dstW < srcW` (downsize)** — `sx_scale < 1.0`, multiple source
  pixels blend into one destination pixel (averaging). This is
  timetunnel's actual use case.

## Sanity test (`tools/test-gluScaleImage.c`)

A standalone CPU-only test that links just the shim function (verbatim
copy at `tools/test-gluScaleImage-shim.c`) and verifies 16 cases:

  1. identity 4x4→4x4 preserves bytes (memcpy fast path)
  2. identity 4x4→4x4 returns 0
  3. 2x2→4x4 upsize returns 0
  4. 2x2→4x4 bilinear matches hand-computed expectations for all 16 pixels
  5. 4x4→2x2 downsize returns 0
  6. 4x4→2x2 bilinear matches hand-computed expectations (quadrant pickup)
  7. 10x10→8x8 returns 0 (the timetunnel actual call shape — non-Po2→Po2)
  8. 10x10→8x8 uniform red → all dst pixels red (uniform source → uniform dst)
  9. GL_RGB input rejected with GLU_ERROR
 10. GL_RGB input did not write dst (preserved 0xCC)
 11. GL_UNSIGNED_SHORT input rejected with GLU_ERROR
 12. GL_UNSIGNED_SHORT input did not write dst (preserved 0xCC)
 13. srcW=0 rejected with GLU_ERROR
 14. dstH=0 rejected with GLU_ERROR
 15. NULL dst rejected with GLU_ERROR
 16. NULL src rejected with GLU_ERROR

Build & run:
```
cd /home/jasonperlow/work-screensavers
gcc -Wall -Wextra -O2 \
    -o /tmp/test-gluScaleImage \
    tools/test-gluScaleImage.c \
    tools/test-gluScaleImage-shim.c \
    -lm
/tmp/test-gluScaleImage
```

The shim file is a duplicate of the implementation in
`src/xscreensaver_compat.c` — if you change the shim, mirror the
change in `tools/test-gluScaleImage-shim.c` and re-run the test.

## What the shim does NOT do (by design)

- No GL_LUMINANCE / GL_RGB / GL_ALPHA input formats (only GL_RGBA).
- No 16-bit / float input types (only GL_UNSIGNED_BYTE).
- No row-stride / unpacking-from-buffer (assumes tightly packed
  rows).
- No Y-axis flip (the input and output are both in GL bottom-left
  convention).

None of these matter for timetunnel's actual call. If a future port
needs any of these, the shim returns GLU_ERROR and the upstream Mesa
gluScaleImage semantics match the spec's GLU_INVALID_ENUM /
GLU_INVALID_VALUE behavior.

## Verification on real hardware

Visual end-to-end verification of the scaled textures is still
BLOCKED on physical hardware access to O6N (no Wayland display
attached, labwc headless backend, no wl_output). The verifiable
evidence is:
  - the 16 sanity-test assertions above (CPU-side correctness)
  - the link/ldd/nm/strace evidence per the project's standard
    "ported only if links" rule (see `docs/audit/per-target-evidence/
    timetunnel_gles3.txt` once that target is built)

When a real display is attached: run `timetunnel_gles3` and confirm
the textures render at the right size and that the bilinear
interpolation is visually reasonable (no banding, no aliasing
artifacts at the edges of the source image's features).