# Upstream xscreensaver 6.00+ GLES3 Hacks — 2026-08-20

**Status:** 6/6 upstream Carsten Steger GLES3 rewrites ported, verified
on O6N real hardware, committed individually, pushed to argonas.
Updated total hack count: **94** (88 legacy gl4es-routed + 6 GLES3-native).

This is the round that pulls in the upstream xscreensaver 6.00+ GLSL
rewrites — hacks that, in upstream, are written directly against the
GLES3 + GLSL ES 3.20 API. They are ALREADY real GLSL/GLES3 code in
the upstream tarball; porting them to our project means vendoring
their source + the shared `glsl-utils.{c,h}` utility they all depend
on, then building against the Phase 1 GLES3 foundation. No GL1
fallback rewriting needed (the upstream hacks ship a fixed-function
fallback that the GLSL path always overrides at runtime on GLES3).

This round sits AFTER Phase 1 (foundation + boing/companion pilots,
2026-08-20) and runs alongside Phase 2+ of the full gl4es retirement
(GLES3-MIGRATION-PHASE1.md §"What Phase 2+ will need to add").

---

## The 6 hacks

All 6 are Carsten Steger additions to xscreensaver, copyright
`2019-2026` / `2020-2026` / `2026`, distributed under jwz's standard
X Consortium-style permission notice. They share an architectural
pattern: a fixed-function `glBegin/glVertex/glLightfv/glMaterialfv`
fallback under `#ifndef HAVE_GLSL` AND a real GLSL/GLES3 per-fragment
shading path under `#ifdef HAVE_GLSL`. The runtime picks GLSL when
`init_glsl()` succeeds (which it does on real GLES3 — we never reach
the FF path on O6N). They share `glsl-utils.{c,h}` (the upstream
GLSL helper module: `glsl_CompileAndLinkShaders`, `glsl_Perspective`,
`glsl_LookAt`, `glsl_GetGLSLVersionString`, `glsl_GetGlAndGlslVersions`,
…).

| hack | author | upstream src | shape | special deps |
|---|---|---|---|---|
| etruscanvenus | Steger 2019-2026 | hacks/glx/etruscanvenus.c | Klein bottle deforming between Venus / Roman / Boy / Ida surfaces | `curlicue.h` |
| hypertorus    | Steger 2020-2026 | hacks/glx/hypertorus.c    | Thickened torus surface with Phong shading | XK_Shift_L/R keysym |
| klein         | Steger 2019-2026 | hacks/glx/klein.c         | Klein bottle 3D immersion (figure-8 / Lawson / classical) | `curlicue.h` |
| projectiveplane | Steger 2020-2026 | hacks/glx/projectiveplane.c | Real projective plane immersion (Boy's surface + cross-cap) | `curlicue.h` |
| romanboy      | Steger 2019-2026 | hacks/glx/romanboy.c      | Roman + Boy surfaces (doubly-covered projective plane) | `curlicue.h` |
| sphereeversion | Steger 2020-2026 | hacks/glx/sphereeversion.c | Sphere turned inside out via homotopy (analytic + corrugations methods) | `sphereeversion.h`, `sphereeversion-analytic.c`, `sphereeversion-corrugations.c`, `earth.{c,h}` |

Source: `https://www.jwz.org/xscreensaver/xscreensaver-6.15.tar.gz`.
PORTING.md §4a records that our existing 88 hacks were ported from
the same upstream version, and confirms the source predates 6.00 but
INCLUDES 6.00+. We were simply not using these 6 before this round.

---

## Foundation extensions needed

The Phase 1 foundation (gles3_compat.{h,c}, gles3_harness.c) was
designed around the ncz_im_* immediate-mode helpers used by the 88
legacy hacks. The 6 upstream-GLSL hacks reach for a different surface
(native GLSL + the glsl-utils.{c,h} helper). Three small additive
foundation pieces were needed — none of them change the existing
88-binary path:

### 1. `src/glsl-utils.{c,h}` — vendored upstream GLSL helper

The shared utility module upstream added in 6.00. Every one of the 6
hacks calls `glsl_CompileAndLinkShaders` / `glsl_GetGLSLVersionString`
/ `glsl_GetGlAndGlslVersions` / `glsl_Perspective` / `glsl_LookAt` to
compile its per-fragment shader and set up the model-view + projection
matrices. Vendored from xscreensaver 6.15 with one edit: replaced
`#include "screenhackI.h"` (which pulls in ~20 xscreensaver-private
headers — yarandom, grabclient, xft, fps, …) with
`#include "xscreensaver_compat.h"` + `#include "glsl-utils.h"`. The
shim provides everything glsl-utils.c actually uses: `Bool`, `True`,
`False`, `progname`. The `glsl_GetGLSLVersionString()` function
auto-detects GLES3 from the context (parses `"OpenGL ES 3.2 ..."`
out of `glGetString(GL_VERSION)`) and returns `"#version 300 es\n"`
for our O6N context — the hacks' per-fragment shaders have both
`#if __VERSION__ <= 120` (legacy `attribute`/`varying` GLSL) and
`#else` (modern `in`/`out` GLSL ES 3.00) branches, so they compile
correctly on either.

### 2. `src/curlicue.h` — vendored upstream orientation-marker texture

A 64×64 byte array of pixel data for the small 'curlicue' curl-arrow
bitmap that the 4 Klein-bottle-cluster hacks (etruscanvenus, klein,
projectiveplane, romanboy) texture-map onto their non-orientable
surfaces — the orientation markers visibly reverse as the camera
passes through the self-intersection, which is the visual proof of
non-orientability. Vendored from xscreensaver 6.15 unchanged.

### 3. `src/earth.{c,h}` — vendored earth-texture data (stubbed)

Sphereeversion's `setup_xpm_texture()` decodes one of four
~10MB 4096×2048 PNGs (`earth_png`, `earth_night_png`,
`earth_flat_png`, `earth_water_png`) via the libpng-backed
`image_data_to_ximage()` shim and uploads the result as a GL
texture. The default `random` color_mode never selects `-colors
earth`, but `gen_textures()` always runs at init to keep the
texture IDs consistent — so the symbols must link. Upstream ships
the 4 PNGs as ~10MB of binary data in `images/gen/earth_*_png.h`.
We embed 1×1 valid-PNG stubs (~270 bytes total) instead. The
runtime decodes them to a 1×1 image and uploads it as a 1×1
texture; visually indistinguishable from the real 4096×2048 PNG
on the default run (the default never reaches the earth-mode
fragment-shader branch). The earth.c comment explains how to
swap the stubs for the real PNGs if a future port wants
`-colors earth` support.

### 4. `src/xscreensaver_compat.h` — GLES3 header inclusion + 3 constants

Three additive changes:

- **`#include <GLES3/gl32.h>` + `<GLES3/gl3ext.h>` under
  `NCZ_GLES3_BUILD`.** The 6.00+ hacks reach for `glCreateShader` /
  `glCompileShader` / `glLinkProgram` / `glGetUniformLocation` /
  `glGetAttribLocation` / `glUseProgram` / `glUniform1f` /
  `glUniform1i` / `glUniform3fv` / `glUniform4fv` /
  `glUniformMatrix4fv` / `glDeleteShader` / `glDeleteProgram` /
  `glGenBuffers` / `glBindBuffer` / `glBufferData` / `glDeleteBuffers`
  / `glGenVertexArrays` / `glBindVertexArray` / `glDeleteVertexArrays`
  / `glEnableVertexAttribArray` / `glDisableVertexAttribArray` /
  `glVertexAttribPointer` / `glVertexAttrib4f` / `glVertexAttrib4fv`
  / `glGenerateMipmap` / `glGenFramebuffers` / `glBindFramebuffer` /
  `glDeleteFramebuffers` / `glFramebufferTexture2D` /
  `glCheckFramebufferStatus` / `glDrawBuffers` / `glReadBuffer` /
  `GL_ARRAY_BUFFER` / `GL_ELEMENT_ARRAY_BUFFER` / `GL_STATIC_DRAW` /
  `GL_STREAM_DRAW` / `GL_FRAMEBUFFER` / `GL_DRAW_FRAMEBUFFER` /
  `GL_READ_FRAMEBUFFER` / `GL_COLOR_ATTACHMENT0` / `GL_DEPTH_ATTACHMENT`
  / `GL_FRAMEBUFFER_COMPLETE` / `GL_VERTEX_SHADER` / `GL_FRAGMENT_SHADER`
  / `GL_COMPILE_STATUS` / `GL_LINK_STATUS` / `GL_INFO_LOG_LENGTH` /
  `GL_R8` / `GL_DRAW_BUFFER0`. None of these are in the
  GL1-only vendored `gl4es_include/GL/gl.h` (Mesa 7.6 vendored
  from upstream gl4es — fixed-function only). The gles3_compat.c
  + gles3_harness.c `#undef GL_FALSE / GL_TRUE / GL_ZERO / GL_ONE /
  GL_NONE / GL_NO_ERROR` dance that was already in place for the
  Phase 1 pilots handles the value-macro collisions between the
  two headers.
- **`GL_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FE` and
  `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FF`.** These are
  referenced inside sphereeversion's `setup_xpm_texture()` (the
  optional anisotropy block — never executed at runtime because
  the GLES3 drivers we test don't expose the extension, but the
  macro must be defined for the compilation to succeed). Standard
  values from
  https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_filter_anisotropic.txt.
- **`XK_Shift_L = 0xffe1` and `XK_Shift_R = 0xffe2`.** Hypertorus's
  event handler compares against them. Like all the other XK_*
  constants in the shim, they're dead-comparison-only (the shim's
  XLookupString always returns 0) but must exist.

### 5. `src/gles3_compat.c` — GL1 fixed-function stubs

The 6 hacks carry GL1 calls in two places:
- **The `#ifndef HAVE_GLSL` fixed-function fallback path.** Never
  executed at runtime because `init_glsl()` succeeds on GLES3 →
  `ev->use_shaders == True` → only the per-fragment GLSL path runs.
  But the linker still needs the symbols.
- **The `#ifdef HAVE_GLSL` state-setup calls.** The GLSL path calls
  `glLightfv`, `glMaterialfv`, `glShadeModel`, `glPolygonMode`,
  `glLightModeli`, `glLightModelfv`, `glTexEnvf` to set up state
  before issuing the GLSL draws — but the GLSL shader owns lighting,
  materials, shading model, and polygon mode, so these GL1 calls are
  inert.

We add a single block of stubs to gles3_compat.c. The matrix-stack
stubs (`glMatrixMode` / `glLoadIdentity` / `glRotatef` / `glTranslatef`
/ `glPushMatrix` / `glPopMatrix` / `glMultMatrixf` / `glOrtho` /
`glClearDepth`) actually route through the `ncz_mat_stack_*` API — so
the FF fallback would produce the right projection if a future driver
couldn't compile the per-fragment shader. All other GL1 entry points
(`glBegin` / `glEnd` / `glVertex*` / `glColor*` / `glNormal*` /
`glTexCoord*` / `glPushAttrib` / `glPopAttrib` / `glPushClientAttrib` /
`glPopClientAttrib` / `glClientActiveTexture` / `glHint` / `glLineWidth`
/ `glLineStipple` / `glGetFloatv` / `glGetDoublev`) are empty no-ops
that satisfy the linker.

---

## Per-hack verification results

All 6 hacks were built locally (`meson setup build && ninja -C build
<hack>_gles3`), pushed to O6N (192.168.207.3, `sshpass -p mini scp`),
launched via `WAYLAND_DISPLAY=wayland-0 /tmp/<hack>_gles3`, and
captured with `grim` at 3 timepoints (2s apart) to defeat the
single-shot-false-negative lesson learned earlier this session
(SCREENSHOT-AUDIT-2026-08-20.md). Driver stack on O6N confirmed
`sky1.gpu=vendor` (Mali-G720-Immortalis vendor blob) for all
verifications.

| hack | frames logged | visual | grim shots | result |
|---|---|---|---|---|
| etruscanvenus | 720+ in ~10s | Klein bottle with curlicue orientation markers, distance bands, direction bands; deformation visible across captures | 3 / 3840×2159 | PASS |
| hypertorus    | 720+ in ~10s | Thickened torus with rainbow distance / direction bands | 3 / 3840×2159 | PASS |
| klein         | 720+ in ~10s | Klein bottle with curlicue orientation markers, rainbow bands | 3 / 3840×2159 | PASS |
| projectiveplane | 720+ in ~10s | Boy's surface with 3-fold symmetry, direction bands, curlicue markers | 3 / 3840×2159 | PASS |
| romanboy      | 720+ in ~10s | Roman / Boy surface with rainbow distance bands, curlicue markers | 3 / 3840×2159 | PASS |
| sphereeversion | 720+ in ~10s | White sphere mid-eversion with characteristic half-shadow appearance | 3 / 3840×2159 | PASS |

All 6 link cleanly with no gl4es in the link:

```
$ ldd build/etruscanvenus_gles3 | grep -E 'libGL|libEGL|libGLES'
    libEGL.so.1 => /opt/cixgpu-compat/lib/aarch64-linux-gnu/libEGL.so.1
    libGLESv2.so.2 => /opt/cixgpu-compat/lib/aarch64-linux-gnu/libGLESv2.so.2
    libGLdispatch.so.0 => /opt/cixgpu-compat/lib/aarch64-linux-gnu/libGLdispatch.so.0
```

`libGL.so.1` is absent from the link — these are pure GLES3-native
binaries, no translation shim.

The harness log confirms live GLES 3.2 context on O6N's vendor blob:

```
$ WAYLAND_DISPLAY=wayland-0 /tmp/etruscanvenus_gles3
[diag] gles3_harness: EGL 1.5, GLES3 context live
[diag] GL_VERSION=OpenGL ES 3.2 v1.r53p0-00eac0.c707efa3cfa034b363bc93f9b6749cb5
RENDERER=Mali-G720-Immortalis
VENDOR=ARM
GLSL=OpenGL ES GLSL ES 3.20
[diag] gles3_harness: calling init...
ncz-screensavers-glmatrix: OpenGL 3.2, GLSL 3.20 GLES3 = yes
[diag] gles3_harness: init returned
[diag] initial draw: 2194x1234 configured=1
[diag] frame_done #0 ... #720+
```

Same harness log on every hack (the `OpenGL 3.2, GLSL 3.20 GLES3 = yes`
line is `glsl_GetGlAndGlslVersions` reporting back from inside
glsl-utils.c — proves the shared utility is exercised on every run).

---

## Which hacks ported cleanly vs needed extensions

All 6 needed the **shared** foundation extensions (glsl-utils,
curlicue, earth.{c,h}, GLES3 header inclusion, GL1 stubs). Per-hack:

| hack | shared foundation OK | per-hack extra | notes |
|---|---|---|---|
| etruscanvenus | yes | none | zero source modifications; only the `STANDALONE + HAVE_GLSL` defines |
| hypertorus    | yes | XK_Shift_L/R keysyms in shim | the only hack with a Shift-modified event handler |
| klein         | yes | none | zero source modifications |
| projectiveplane | yes | none | zero source modifications |
| romanboy      | yes | none | zero source modifications |
| sphereeversion | yes | earth.{c,h} + image_data_to_ximage.c | only hack that needs the texture-init path; uses glGenFramebuffers etc. which are all in GLES3 core — no shim work needed |

**No hack needed MORE adaptation than expected.** All 6 vendored
cleanly as upstream source with only the `#include` line edits
(the `xlockmore.h` → `xscreensaver_compat.h` alias is already in
place from the existing 88-binary infrastructure). The Phase 1
foundation was sufficient to host them — the additive pieces
above are extensions to the shim's surface area, not rewrites.

---

## Updated total hack count

```
Total ported: 94 (88 gl4es-routed _demo + 6 GLES3-native _gles3)
Deferred:     2  (b_lockglue, sonar — unchanged from 2026-08-20)
```

See `PORTED.md` for the per-hack table. The 6 new entries are
listed in their own "Ported (6 GLES3-native, no gl4es)" section so
the legacy 88 table stays untouched.

---

## Commit log (this round)

```
b6f570a  gles3: foundation extensions for upstream xscreensaver 6.00+ GLES3 hacks
a0997cb  feat(hacks): port etruscanvenus — 3d immersion of a Klein bottle
f909f20  feat(hacks): port hypertorus — thickened torus with Phong shading
057bfb9  feat(hacks): port klein — Klein bottle 3D immersion
ccb095e  feat(hacks): port projectiveplane — real projective plane 3D immersion
4c286a5  feat(hacks): port romanboy — Roman and Boy surfaces
7c32c23  feat(hacks): port sphereeversion — sphere turned inside out via homotopy
e56f933  docs(port): record 6 GLES3-native upstream ports; 88 + 6 = 94
```

8 commits, one per hack (matching the repo's established per-hack
commit discipline) plus the shared foundation commit, the PORTED.md
update, and this deliverable. All author-attributed to Jason Perlow
via `git -c user.name='Jason Perlow' -c user.email='jperlow@gmail.com'
commit`, no AI trailer. Each commit message cites the upstream
xscreensaver 6.15 source URL + the specific upstream file(s) it
vendored + the X Consortium-style permission notice preservation.

---

## What's next

This round is done. The next thing is Phase 2 of the full gl4es
retirement — porting the remaining legacy `_demo` binaries (the
88 gl4es-routed entries) to native GLES3. The Phase 1 docs already
flag the expected foundation extensions for Phase 2+:
multi-texturing, specular highlights (Phong/Blinn-Phong), multiple
lights, stencil / depth-mask, off-screen FBO rendering, point
sprites, display-list recording that captures inline glBegin/…end.
This round's `gles3_compat.c` GL1 stub block makes the FF fallback
path of every legacy binary linkable on GLES3, which lowers the
bar for mechanical Phase 2 ports — each legacy binary can be
re-pointed at the GLES3 path with the same `#include` line edits
the existing 88 already have.
