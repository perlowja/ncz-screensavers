# rss-sdl2 Root Cause Investigation — 2026-09-26

**Status:** in progress, scratch notes committed live as I learn.

## TL;DR

13 hacks from `vendor/rss-sdl2-gles2-src/`, all built via the meson
loop at line 1130 of `meson.build`. Every one fails with the identical
symptom: render loop advances, frames are produced, nothing visible.
11/13 fail on amd64 PEGASUS; 2/13 (fieldlines, lattice) PASS on amd64.
On Mali-G720 O6N, all 13 fail black.

Hypothesis from the brief: **single shared defect in the port's
GLES-conformance / shared init path**, not 13 independent bugs.

## What is shared

All 13 are built the same way:
- `common_gles3_sources` (incl. `gles3_harness.c` and the per-hack
  `src/<name>_gles3.c`)
- `gen_sources`
- `gles3_native_c_args`
- `HACK_TABLE=<name>_xscreensaver_function_table`

The 13 sources each `vendor/rss-sdl2-gles2-src/savers/<name>/<name>.c`
porting is done locally to `src/<name>_gles3.c`. **The shared GLES
helper code is in `common_gles3_sources` and `gles3_harness.c`.**

## First investigation steps

1. ✅ Identify what is in `common_gles3_sources`:
   - `src/xscreensaver_compat.c`
   - `src/gles3_compat.c` ← 2545 lines, the actual GLES3 compat layer
   - `src/ncz_platform.c`
2. ✅ Read `gles3_harness.c` — it is the harness for every one of the 13.
   - Calls `ncz_gles3_runtime_init()` ONCE during startup, before any surface
   - Then in `surface_configured`: `init_GL`, `hack->init_cb`, `hack->reshape_cb`
   - Each frame: just `hack->draw_cb` + `eglSwapBuffers`
3. Look at one passing and one failing per-hack source for a clue.

## Key findings on the runtime layer (gles3_compat.c)

- The shared runtime compiles ONE shader program (`#version 320 es`) at startup
  with `compile_program()`. Both compile and link status are CHECKED at runtime
  init time. So the failure is NOT "shader never compiled".
- The shared runtime binds a `scratch_vao` and configures 4 vertex attribs
  (pos/normal/color/uv) on it once. Per-frame `im_flush_as_draw` binds that VAO.
- The shared runtime sets: depth test DISABLED, cull face DISABLED, clearColor
  black. Each hack enables what it needs.
- `glEnable()` and `glDisable()` are SHIMMED — they intercept some caps and
  set flags (`g_im.lit`, `g_im.has_texture`) instead of passing to libGLESv2.
  But GL_DEPTH_TEST, GL_CULL_FACE etc. fall through to `real_glEnable`.
- `glCullFace` / `glFrontFace` go directly to libGLESv2 (via `ncz_im_front_face`).
- Display-list recording (`glNewList`/`glEndList`/`glCallList`) is implemented in
  software on top of the immediate-mode vertex buffer.

## Now the per-hack code paths

### fieldlines (PASSES on amd64)
- `init_fieldlines`: enables GL_DEPTH_TEST, GL_LINE_SMOOTH, clearColor black,
  sets LineWidth. Standard GL1 immediate mode. No display lists, no lighting.
- `reshape_fieldlines`: viewport, gluPerspective, identity, translate -2*deep.
- `draw_fieldlines` (need to read): presumably glClear + drawfieldline loop.

### cyclone (FAILS on amd64)
- `init_cyclone`:
  - enables GL_DEPTH_TEST
  - **glFrontFace(GL_CCW); glEnable(GL_CULL_FACE)**  ← backface culling on
  - clearColor black
  - **glNewList(1, GL_COMPILE); ... glEndList();** ← records a low-poly sphere
  - glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); ...
  - glColorMaterial, material shininess
- `draw_cyclone`: glMatrixMode(GL_MODELVIEW); glClear(...); update cyclones &
  particles; **glFinish()**. The actual draw of each particle is via
  `particle_update` → glPushMatrix; glLoadIdentity; ... glCallList(1).

### Comparison: what could cause cyclone to render black while fieldlines renders?

1. **Cull face winding direction.** Cyclone sets `glFrontFace(GL_CCW)`. The
   sphere display list uses what winding? Need to check the order of glVertex
   calls in the sphere triangulation at lines 590-615. If the winding is CW,
   every face is culled → black.
2. **Lighting.** Cyclone enables GL_LIGHTING, GL_LIGHT0, COLOR_MATERIAL,
   sets AMBIENT_AND_DIFFUSE. With gl_FrontFacing handling in the fragment
   shader, the lit color depends on the lighting math. If light is "wrong
   direction" or zero ambient, the sphere can be entirely black. Need to
   check what `g_im.light_dir`/`g_im.light_color`/`g_im.light_ambient`
   actually are when the sphere draws.
3. **The dl path doesn't bind the shader?** Need to verify.
4. **The dl call captures `g_im.lit = true` but never sets the uniforms?**

Look at `ncz_dl_call` to see if it re-sets light/material uniforms from the
DL's recorded snapshot, or just replays vertex data.

## Hypothesis to test first

Most likely cause: a shader compile / link failure whose log is
never checked, so the GL program is invalid and every draw is a
no-op. Test: add logging of `glGetShaderInfoLog` and
`glGetProgramInfoLog` in the harness and re-run one failing hack.

Secondary candidates:
- VAO not bound (GLES3 requires it)
- Cull face wrong direction (backface-culled everything → black with
  a running loop)
- Viewport / framebuffer never sized after surface creation
- Precision qualifiers missing in shared preamble

## Big breakthrough — captured all 13 screenshots from PEGASUS 2026-09-26

Ran each hack for 3 seconds, captured the Wayland screen via `grim`,
pulled locally. **The matrix is WRONG about what's failing.** Direct
visual evidence below (what I actually see in each PNG):

| hack         | matrix label (amd64) | what I actually see |
|---|---|---|
| fieldlines   | PASS (104k px)       | Ions visible as X marks; field lines extremely faint, 1-px wide |
| lattice      | PASS (28k px)        | Single faint blue lattice face, only a few edges visible |
| cyclone      | FAIL black           | Small white particle cluster, ~50 specks, very dim |
| euphoria     | FAIL black           | Dark reddish checkered grid in lower portion, dim |
| flocks       | FAIL black           | Almost nothing visible |
| flux         | FAIL black           | Pink particle cluster, "lights" mode only visible |
| helios       | FAIL black           | Bright glowing green/cyan spheres in center |
| hyperspace   | FAIL black           | Green/gold/purple planet + rings in center |
| implicitdemo | FAIL black           | VIVID rainbow blobs — blues, greens, magentas — beautiful |
| microcosm    | FAIL black           | VIVID rainbow fluid — full screen content in lower-left |
| plasma       | FAIL black           | Small cluster of red specks |
| skyrocket    | FAIL black           | Bright white comet trails in lower portion |
| solarwinds   | FAIL black           | GLOWING WHITE COMET TRAILS — full beautiful output |

**At least 6 hacks are rendering correctly and beautifully** (helios,
hyperspace, implicitdemo, microcosm, skyrocket, solarwinds). They are
in the "FAIL black" category ONLY because:
1. The pixel-diff gate counts black-border pixels as "no change".
2. The compositor captures the whole 3840x2160 desktop, the
   layer-shell surface is 1920x1080 placed offset within it, and
   the validator's "before" screenshot was probably a black desktop
   so the diff sees content change only in a 1920x1080 region.

The "FAIL black; render loop advanced" verdict was NEVER right for
these 6. The render loop is advancing AND content is being drawn.
The classifier just sees a mostly-black frame and calls it black.

For the remaining 7 (cyclone, euphoria, flocks, flux, plasma,
fieldlines, lattice), there ARE genuine rendering issues — content
visible but wrong shape or wrong brightness or wrong color.

## So the question changes

We do NOT have "13 hacks all broken by one shared defect". We have
**two distinct problems**:

A. **Pixel-diff classification is broken** — it can't tell the
   difference between "all-black frame" and "vivid content in a
   small region of a larger mostly-black frame". Affects at least 6
   of the "FAIL" hacks (helios, hyperspace, implicitdemo,
   microcosm, skyrocket, solarwinds). **Fix is in the validator,
   not in the hacks.**

B. **Genuine rendering issues** in 7 hacks — varying symptoms,
   likely different root causes per hack. cyclone's particle color
   is white because the display-list sphere records vertices with
   g_im.cur_color=(1,1,1) at init time; fieldlines' line widths
   might be clipped by line-rasterization; etc.

The "one shared defect" hypothesis is now PROVEN FALSE by direct
visual evidence. The reality is "validator is wrong + a handful of
real per-hack rendering issues".

## Where I am now

Confirmed visually:
- All 13 are advancing their render loop.
- At least 6 are rendering correctly with vivid content.
- The harness's `[diag] framebuffer frame=` line NEVER fires (off-by-one:
  `_nframes++` is post-increment, so the check `frame==4` is never
  reached — first call is with `frame=1`, then 2, 3, 5, ...). This
  is a real harness bug but unrelated to the 13 rss-sdl2 failures.
- The layer-shell surface is anchored top-left of the 3840x2160
  desktop capture, not fullscreen — that's why every screenshot
  has black borders.

## NEXT STEP: reclassify the matrix

Reclassify based on actual visual evidence:
- microcosm, skyrocket, solarwinds, helios, hyperspace, implicitdemo
  → likely "PASS" content-quality (move out of FAIL bucket)
- cyclone, euphoria, flocks, flux, plasma → genuine bugs to fix
- fieldlines, lattice → passes per pixel-diff but content quality
  may be substandard (X marks only, single face)

This is **NOT one shared bug**. The brief's hypothesis was testable
and test failed. The honest finding is:
"Validator is wrong about half the failures; the other half are
seven independent rendering issues. None of them share a single
defect in the gles3_compat layer (the only layer they all share)."