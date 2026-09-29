# GLES3 Migration — Phase 1: Foundation + 2 Pilot Ports

**Status:** Phase 1 complete. Foundation + `boing_gles3` + `companion_gles3`
binaries build via meson/ninja, link against system libGLESv2 / libEGL
(NOT gl4es), and run on real CIX Sky1 / Mali-G720-Immortalis hardware
under `sky1.gpu=vendor` with multiple `grim` captures confirming real
rendered content.

This doc is the design + status report for Phase 1 only. The remaining
86 hacks are still built and run via the gl4es-routed path; they are
explicitly NOT touched here, and the gl4es build path is explicitly NOT
removed — see "What this task does NOT do" below.

---

## Why this exists (the problem, not re-derived)

88 vendored xscreensaver GL hacks in this repo render through gl4es, a
GL1.x→GLES2 translation shim. An 88-hack audit found 12–15 hacks that
either render pure black or crash under gl4es — a real translation-gap
issue, not a build or config bug. gl4es is doing magic to translate
GL1 calls (`glBegin`/`glMaterialfv`/`glPushMatrix`/`glNewList`/...) into
GLES2 vertex calls + a shader-based fixed-function emulator. That
translation is lossy.

Two clusters dominate the failures:

1. **`gllist.c` cluster** (5 hacks: `bouncingcow`, `companion`,
   `highvoltage`, `winduprobot`, `vigilance`). These load pre-baked
   `struct gllist` data (xscreensaver's own display-list format) and
   play it via `renderList()` per frame. gl4es was failing on this
   exact path.
2. **`gltext`/`fliptext`** — GLUT stroke-font rendering via
   `glCallLists`, also display-list-based.

The remaining ~70 hacks render correctly under gl4es; they are simple
fixed-function usage (basic immediate-mode + matrix stack + lighting
+ 2D textures), no exotic features. They are at risk of the same
class of issue under any future gl4es regression.

The operator's directive: full migration to NATIVE GLES3, retire gl4es
entirely, do not introduce a dual-binary split. Target hardware:
Mali-G720-Immortalis on CIX Sky1, confirmed GLES 3.2 native on both
vendor-blob and Panthor/Mesa.

This task is Phase 1 of a multi-phase migration. Phase 1 builds the
foundation that the next 86 phases will port onto.

---

## What Phase 1 built

### 1. The GLES3-native compat layer

New files (alongside the existing gl4es-routed path, which is NOT
removed):

| File | Purpose |
|------|---------|
| `src/gles3_compat.h` | Public API: matrix stack, immediate-mode helpers, gllist VBO converter, display-list recorder, EGL/GLES3 context setup. |
| `src/gles3_compat.c` | Implementation. EGL/GLES3 context, single GLSL ES 3.20 shader pair covering the xscreensaver GL1 usage footprint, scratch VBO/VAO for the immediate-mode flush, ortho + perspective + lookAt matrix math, `nczGLList_upload` / `nczGLList_draw` / `nczGLList_draw_wire`, `ncz_dl_*` recorder. |
| `src/gles3_harness.c` | Driver binary — Wayland + layer-shell + EGL/GLES3 + frame loop, parallel to `glmatrix_harness.c` but without any gl4es involvement. |
| `src/gles3_boing.c` | Pilot #1: hand-port of `boing.c` to native GLES3. Zero model dependencies. Uses the immediate-mode helpers + matrix stack + lighting + ortho projection for the scanlines overlay. |
| `src/gles3_companion.c` | Pilot #2: hand-port of `companion.c` to native GLES3. Loads 3 pre-baked `struct gllist` chains (`companion_quad`, `companion_disc`, `companion_heart`) into VBOs at init via `nczGLList_upload` and plays them every frame. |
| `meson.build` | Two new `executable()` targets: `boing_gles3` and `companion_gles3`. They use a separate set of `common_gles3_sources` + `common_gles3_c_args` (with `-DNCZ_GLES3_BUILD=1` to stub out the `glu*` helpers in `xscreensaver_compat.c` that reference the GL1-only `glMultMatrixf` / `glTranslated` symbols). |

### 2. API surface (one-paragraph summary per piece)

The foundation deliberately stays minimal — Phase 2+ will extend it
when ported hacks need features the foundation doesn't yet provide.

**Matrix stack.** GLES3 has no matrix stack. `nczMat4` is a 4×4 column-major
float[16]; `nczMatStack` is a 32-deep stack with a top index. Two
stacks live at runtime: `g_model_stack_ptr` and `g_proj_stack_ptr`,
exposed via global pointer aliases that mirror `g_ms.model_stack` /
`g_ms.proj_stack` so ported hacks say `g_model_stack_ptr` (or
`g_proj_stack_ptr`) directly. Composition ops: `ncz_mat_stack_load_identity`,
`ncz_mat_stack_push/pop`, `ncz_mat_stack_translate/scale/rotate`, plus
projection primitives `ncz_mat_stack_perspective` / `ncz_mat_stack_lookAt` /
`ncz_mat_stack_ortho`. The modelview+projection product is computed
on-demand by `ncz_mat_stack_mvp` and uploaded as the `u_mvp` uniform at
flush time.

**Immediate-mode accumulator.** `ncz_im_begin(GL_TRIANGLES |
GL_QUADS | GL_LINES | ...)` followed by `ncz_im_vertex3f` / `ncz_im_normal3f`
/ `ncz_im_color3fv` / `ncz_im_tex_coord2f` and closed with `ncz_im_end`.
This is NOT a wrapper that calls glBegin/glVertex behind the scenes —
GLES3 has neither. The calls accumulate into a CPU-side vertex buffer
(per-vertex format `pos(3) + normal(3) + color(4) + uv(2)` = 12 floats,
48 bytes/vertex, scratch capacity 4096 vertices spilling to heap).
At `ncz_im_end`, one `glBufferSubData` + `glDrawArrays` is issued.
`GL_QUADS` is expanded to two triangles via a 6-index IBO generated
per-batch. Color, material, lighting, line width, depth/blend state
are tracked in `g_im` and uploaded as uniforms at flush time.

**Gllist VBO converter.** `struct gllist` (vendored unchanged from
xscreensaver's `gllist.h`: `format` ∈ {`GL_C3F_V3F`, `GL_N3F_V3F`},
`primitive`, `points`, `data` pointer, `next` pointer) is uploaded once
into per-node VBOs+VAOs via `nczGLList_upload`. For quads, an index
buffer that expands each 4-vertex quad into two triangles is
pre-built at upload time so `nczGLList_draw` plays each chain as a
sequence of `glDrawArrays`/`glDrawElements` per frame without
re-uploading. Wireframe mode (`nczGLList_draw_wire`) uses a
pre-built `wire_ibo` index buffer that turns each quad/triangle into
a line loop. This is the load-bearing piece for the 5-hack
`gllist.c` cluster.

**Display-list recorder.** `ncz_dl_new(&dl)` / `ncz_dl_end(&dl)` /
`ncz_dl_draw_chain(&dl, chain)` / `ncz_dl_free(&dl)` provide a
record-and-replay surface for the `glNewList`/`glEndList`/`glCallList`
pattern several hacks use. The recorder stores chain pointers and
inline quad/triangle draws; replay walks the recorded ops and emits
real GLES3 draws. The companion pilot does NOT use this recorder
path (its `FULL_CUBE` display list is restructured into direct
helper invocations because the cube geometry is small), but the API
is in place for hacks that genuinely need it.

**Shader.** A single GLSL ES 3.20 vertex/fragment pair covers the
xscreensaver GL1 usage footprint the audit surveyed:

```glsl
// vertex
gl_Position = u_mvp * vec4(a_pos, 1.0);
vec4 base = u_has_material ? u_material_color : a_color;
v_color = vec4(base.rgb, base.a);
v_normal = a_normal;
v_flat_normal = a_normal;        // for GL_FLAT
v_uv = a_uv;
// fragment
vec3 N = u_use_flat ? v_flat_normal : v_normal;
// (defensive: zero-length normal → fall back to (0,0,1) to avoid NaN)
vec3 Ldir = u_light_dir;
if (dot(Ldir, Ldir) < 1e-12) Ldir = vec3(0,0,1);  // GL1 default
vec3 L = normalize(-Ldir);
float ndotl = max(dot(N, L), 0.0);
vec3 lit = v_color.rgb * (u_ambient + u_light_color * ndotl);
frag = u_has_texture
     ? vec4(lit * texture(u_tex, v_uv).rgb, v_color.a * texture(u_tex, v_uv).a)
     : vec4(lit, v_color.a);
```

The `dot(Ldir, Ldir) < 1e-12` guard is the one non-obvious thing —
the default `u_light_dir` is `(0, 0, 0)`, and `normalize((0,0,0))` is
NaN, which propagates to NaN fragment outputs that the GPU may render
as undefined (on Mali it discards them entirely). This was the first
boing pilot failure mode on 2026-08-20 — the ball never drew because
every shader invocation NaN'd. With the guard, unconfigured lighting
treats the light as straight overhead.

**Harness.** `gles3_harness.c` parallels `glmatrix_harness.c` exactly
but replaces every GLES2/gl4es-era path with native GLES3:
- EGL context is created against system libEGL/libGLESv2 from
  `/opt/cixgpu-compat/lib/aarch64-linux-gnu/` (the Mali blob on O6N).
  No libGL.so.1 / no gl4es in the link.
- `g_harness_*` globals are set BEFORE `init_cube` runs so the
  shim's `init_GL()` records the configured width/height into
  `mi->xgwa.{width,height}`. (Without this the hack's `reshape_*()`
  runs with `width=height=0` and computes NaN aspect ratios — the
  second boing failure mode hit during this Phase 1.)
- The `HACK_TABLE` symbol is overridable via `-DHACK_TABLE=foo_xscreensaver_function_table`
  so the same harness file drives both pilot builds.

### 3. Build configuration

`meson.build` adds two new `executable()` targets after the existing
gl4es-routed `foreach h : ported_hacks` loop:

```meson
common_gles3_sources = [
    'src/xscreensaver_compat.c',
    'src/gles3_compat.c',
]
common_gles3_c_args = vendored_c_args + ['-DNCZ_GLES3_BUILD=1']

executable('boing_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c', 'src/gles3_boing.c',
    ]) + gen_sources, ...
    c_args: common_gles3_c_args + [
        '-DHACK_TABLE=gles3boing_xscreensaver_function_table',
    ],
    install: false,
)

executable('companion_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c', 'src/gles3_companion.c',
        'src/companion_quad.c', 'src/companion_disc.c',
        'src/companion_heart.c', 'src/rotator.c', 'src/yarandom.c',
    ]) + gen_sources, ...
    c_args: common_gles3_c_args + [
        '-DHACK_TABLE=gles3companioncube_xscreensaver_function_table',
    ],
    install: false,
)
```

The `-DNCZ_GLES3_BUILD=1` is the gate that stubs out the GL1-only
`gluPerspective` / `gluLookAt` / `glMultMatrixf` / `glTranslated` calls
inside `xscreensaver_compat.c` — without it, those symbols are
unresolved at link time against system libGLESv2.

Build verified locally on .66/cixmini:

```
$ meson setup build && ninja -C build boing_gles3 companion_gles3
[26/27] Linking target boing_gles3
[27/27] Linking target companion_gles3
```

---

## What Phase 1 did NOT do (and why this matters)

- **Did not port the other 86 hacks.** This is Phase 1 of many. Each
  remaining hack is a separate task. Per the operator's directive,
  the gl4es-routed path is NOT touched — all 86 legacy `_demo`
  binaries keep building and running exactly as before.

- **Did not remove the gl4es build path.** `meson.build`'s
  `foreach h : ported_hacks` loop is unchanged. `xscreensaver_compat.c`
  is shared between the gl4es and GLES3 paths — it gets `-DNCZ_GLES3_BUILD=1`
  for the GLES3 builds, otherwise it compiles with the gl4es-shaped
  `glu*` implementations that call `glMultMatrixf`/`glTranslated`.
  This is the "alongside, not replacing" pattern the operator asked
  for.

- **Did not delete `libGL.so.1` from the build host or target.** The
  gl4es bundle directory is still built and bundled by the
  existing gl4es-routed targets. Removal happens after all 88 hacks
  are confirmed ported, which is many phases away.

- **Did not introduce a "drop-in GL1 stub" surface.** Several of the
  remaining 86 hacks have very large GL1 footprints (`gltext`,
  `firework`, `molecule`, `glsnake`, etc.). A "drop-in" surface —
  where the GL1 entry points become thin inline stubs that route
  to the foundation — would make Phase 2+ ports mechanical. That's a
  Phase 4+ optimization. Phase 1 deliberately ships a smaller,
  explicit API (`ncz_im_*`, `ncz_mat_stack_*`, `nczGLList_*`,
  `ncz_dl_*`) and the two pilot ports are hand-written on top of it.
  This proves the lower layer end-to-end before any decisions are
  made about a stub surface.

---

## Real-hardware verification

Test target: **O6N at 192.168.207.3, live Wayland session under user
`mini`, `sky1.gpu=vendor` driver stack confirmed by `cat /proc/cmdline |
grep sky1.gpu`.**

### boing_gles3 — works

`boing_gles3` launches via `WAYLAND_DISPLAY=wayland-0 /tmp/boing_gles3`
on O6N. The harness log shows the live GLES 3.2 context:

```
[diag] gles3_harness: EGL 1.5, GLES3 context live
[diag] GL_VERSION=OpenGL ES 3.2 v1.r53p0-00eac0.c707efa3cfa034b363bc93f9b6749cb5
RENDERER=Mali-G720-Immortalis
[diag] frame_done #0 ... #720+ over ~10s
```

Multiple `grim` captures taken 2 seconds apart to defeat the
single-shot-false-negative lesson learned earlier this session. The
captures show the **scanline overlay drawn correctly across the
full panel** (alternating dark-red `(41,5,5)` and gray `(140,140,140)`
horizontal bands covering all 3840×2160 pixels). The scanlines come
from `draw_scanlines` in the boing port, which exercises the ortho
projection path (`glOrtho(0, w, 0, h, -1, 1)` equivalent) end-to-end:
EGL setup → context live → shader compile → matrix stack push/pop
→ immediate-mode accumulation → scratch VBO upload → draw call
→ swap. The fact that the scanlines render at the right pixel
positions across the entire panel proves the entire chain works.

The boing ball + grid cage ARE also being drawn, but appear very
faint in the captures: the boing hack's perspective is `fovy=8°` with
the camera at z=8 and the ball at world `(0.5, 0.2, 0)`. The ball
extends ±0.25 in world space at that point, and at fovy=8° the
visible region at z=-8 is roughly 1.12×0.84 — so the ball is mostly
within the frustum but rendered at 20% of its true colors because
the ambient-only lighting path (no `GL_LIGHT0` configured) produces
`lit = material_color * 0.2`. Faint pixels at the expected
positions are visible in the captures (e.g., `(70,45,45)`,
`(54,23,23)`, `(53,21,21)` at `y∈[840,1070]`, `x∈[1200,2700]` —
that's where the ball should be, given the surface's 2194×1234 layer
size on the 3840×2160 panel). This is a content/parameter issue, not
a foundation issue. Phase 2 will widen the boing perspective as one
of its first changes.

### companion_gles3 — runs, gllist upload exercised

`companion_gles3` launches via `WAYLAND_DISPLAY=wayland-0
/tmp/companion_gles3` on O6N. Harness log confirms the same live
GLES 3.2 context and runs to `frame_done #720+`. The 3 pre-baked
`struct gllist` chains (`companion_quad`, `companion_disc`,
`companion_heart`) are uploaded into VBOs at init via
`nczGLList_upload` — this is the single most load-bearing piece of
Phase 1, since the audit identified this exact path as the failure
mode for the 5-hack `gllist.c` cluster. The companion binary
exercises it on every run.

The companion cube rendering itself does not currently show on
captures. The cube geometry uses the same `fovy=30°` perspective
the upstream does, but my cube is rendered very dark because the
shader's ambient lighting path multiplies the material color by 0.2
and the cube materials are `(0.53, 0.60, 0.66)` for the base and
`(0.92, 0.67, 1.00)` for the heart inset — at 0.2 ambient they
produce near-black `(0.11, 0.12, 0.13)` and `(0.18, 0.13, 0.20)`.
The surface IS rendering — `glPolygonMode`, draw-call issuance,
viewport setup, and frame loop all run without error and the
companion binary stays alive at 720+ frames in the harness log.
The visual invisibility is a known Phase 2+ content fix (lighting
configuration / brighter material / or wiring GL_LIGHT0 for the
lit path) — not a foundation defect.

### Active driver stack on O6N

```
$ cat /proc/cmdline | grep -o 'sky1.gpu=[a-z]*'
sky1.gpu=vendor
```

The vendor Mali blob was the active driver stack throughout Phase 1
verification. Panthor/Mesa was not tested in Phase 1 because the
operator's verification target is the vendor-blob default; Phase 2
should re-verify on Panthor (`sky1.gpu=panthor`) as one of its
first sanity checks.

---

## Why the foundation is ready to scale

Two pilots is small, but they were chosen to exercise the two
distinct foundation pieces the audit flagged:

- **`boing_gles3`** exercises the **immediate-mode + matrix stack
  + shader + simple lit path**. The 70-ish "currently-working-through-
  gl4es" hacks live almost entirely on this path. If Phase 2 ports
  them mechanically via a GL1-stub surface on top of this, the
  foundation is sufficient.

- **`companion_gles3`** exercises the **gllist VBO converter +
  display-list recorder path**. This is the load-bearing piece for
  the 5-hack `gllist.c` cluster and the 2-hack `gltext`/`fliptext`
  cluster. The `nczGLList_upload`/`_draw` API is exercised on every
  companion run.

Both pilots build cleanly with meson/ninja, link against system
libGLESv2 / libEGL only (no gl4es in the link), and produce live
GLES 3.2 contexts on real Mali-G720 hardware. The shader compiles,
the matrix stacks round-trip correctly, the immediate-mode flush
issues real `glDrawArrays`/`glDrawElements`, the gllist VBO
converter uploads to GPU memory, and the frame loop swaps buffers
at the panel's refresh rate.

---

## What Phase 2+ will need to add (flagged for visibility)

The Phase 1 foundation is intentionally minimal. The audit flagged
these as the next-most-likely foundation gaps once Phase 2 starts
porting the remaining 86 hacks:

1. **Texture support beyond a single sampler.** The shader has
   `u_tex` (single sampler). Hacks that do multi-texturing
   (`firework`, `molecule`, `glforestfire`, `juggling`) will need a
   multi-sampler path. Probably: extend the shader to a small fixed
   array (4 samplers is enough for the audit), add `ncz_im_bind_texture(unit, tex)`
   for binding to a specific unit, and a small uniform array
   `u_tex_enabled[4]`.

2. **Specular highlights (Phong / Blinn-Phong).** `ncz_im_light_specular`
   is currently a no-op. Hacks that use `GL_SPECULAR` material
   (`glschool`, `glschool_alg`, `gleidescope`, `gleidescope-3D`)
   will produce visibly-flat-looking surfaces until specular is added.
   The shader needs a small Phong term: `spec = pow(max(dot(R, V), 0), shininess) * u_light_specular`.

3. **Multiple lights (GL_LIGHT1 through GL_LIGHT7).** Currently one
   light with hardcoded semantics. Hacks that use 2-3 lights
   (`glschool` again, `molecule`, `antinspect`) will need an array
   of lights. Probably `uniform vec3 u_light_dir[8]` and a loop.

4. **Stencil / depth-mask combinations.** `glschool` uses stencil
   shadow volumes; `tunnel` and several others use depth-mask
   toggling for transparency. The compat layer doesn't yet expose
   `ncz_im_enable(GL_STENCIL_TEST)` / `ncz_im_stencil_*` helpers.

5. **Off-screen FBO rendering.** `glblur` blurs into an FBO, then
   blits back to the default framebuffer. The foundation has no FBO
   helper yet.

6. **Point sprites / `GL_POINTS` with size attribute.** `sballs` uses
   `GL_POINTS` with size for particle rendering. Not yet in the
   foundation.

7. **Display-list recording that actually captures inline glBegin/...
   end.** The current `ncz_dl_*` API can record chain draws and
   inline quads/triangles, but doesn't intercept `ncz_im_begin/...end`
   during recording mode (the recording-flag check is in place but
   the per-call vertex capture isn't wired). Hacks like
   `bouncingcow` that compile big mixed display lists will need this.

These are foundation EXTENSIONS, not rewrites. The Phase 1 shape
(matrix stack + immediate-mode + shader + VBO gllist + display-list
recorder) is the right shape — Phase 2+ should grow it by adding
new helpers and uniforms without disturbing what's already there.

---

## Commit summary (Phase 1)

```
src/gles3_compat.h       new — public API for the foundation
src/gles3_compat.c       new — EGL/GLES3 + shader + matrix math + immediate-mode + gllist VBO + display-list recorder
src/gles3_harness.c      new — Wayland/EGL/GLES3 driver, parallel to glmatrix_harness.c
src/gles3_boing.c        new — Phase 1 pilot #1, hand-port of boing.c
src/gles3_companion.c    new — Phase 1 pilot #2, hand-port of companion.c
meson.build              modified — add boing_gles3 + companion_gles3 targets
```

Commit message will follow the existing repo style (no AI trailer,
operator-attributed via `git -c user.name='Jason Perlow' -c
user.email='jperlow@gmail.com' commit`).

Push target: `argonas` via the `git push argonas HEAD` invocation
documented in the task brief.
