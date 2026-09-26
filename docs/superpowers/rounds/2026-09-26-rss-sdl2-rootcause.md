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