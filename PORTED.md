# Ported xscreensaver hacks

A hack counts as ported only if `ninja -C build` LINKS its <name>_demo
or <name>_gles3 target. Compiling is not enough: missing companion
sources surface only at link time.

This count covers both gl4es-routed `_demo` binaries (the legacy path
that runs through the GL4ES translation shim) AND the GLES3-native
`_gles3` binaries (vendored xscreensaver source compiled against
system libGLESv2 / libEGL with no translation shim).

The operator's standing directive (2026-08-20): all 88 legacy hacks
move off gl4es to native GLES3, no dual-binary split. The `_demo`
binaries are kept in meson.build behind `-Dgl4es=enabled` (off by
default on build hosts without gl4es installed) so the legacy path
remains available while the native fan-out completes. They will be
deleted in the final Phase 4+ step once every legacy hack has a
verified native port.

## Ported (65 legacy gl4es-routed)

These 65 build as `<name>_demo` binaries when `-Dgl4es=enabled` is
passed AND the system has gl4es installed at a known path. On hosts
without gl4es (most), only the native `_gles3` binaries (next
section) are built.

| hack | companion sources |
|---|---|
| antspotlight | rotator.c, sphere.c, tube.c, yarandom.c |
| bouncingcow | cow_face.c, cow_hide.c, cow_hoofs.c, cow_horns.c, cow_tail.c, cow_udder.c, gllist.c, rotator.c, yarandom.c |
| dnalogo | normals.c, rotator.c, sphere.c, tube.c, yarandom.c |
| fliptext | - |
| flyingtoasters | gllist.c, toast.c, toast2.c, toaster.c, toaster_base.c, toaster_handle.c, toaster_handle2.c, toaster_jet.c, toaster_knob.c, toaster_slots.c, toaster_wing.c, yarandom.c |
| geodesicgears | involute.c, normals.c, rotator.c, tube.c, yarandom.c |
| gears | involute.c, normals.c, rotator.c, tube.c, yarandom.c |
| gibson | easing.c, rotator.c, yarandom.c |
| glcells | - |
| geodesic | normals.c, rotator.c, yarandom.c |
| glblur | rotator.c, yarandom.c |
| glhanoi | doubletime.c, rotator.c, yarandom.c |
| glforestfire | image_data_to_ximage.c |
| glknots | rotator.c, tube.c, yarandom.c |
| glmatrix | image_data_to_ximage.c |
| glschool | glschool_alg.c, glschool_gl.c, sphere.c, tube.c, yarandom.c |
| glsnake | - |
| gltext | glut_stroke.c, glut_swidth.c, rotator.c, sphere.c, tube.c, yarandom.c |
| gravitywell | - |
| handsy | doubletime.c, gllist.c, handsy_model.c, rotator.c, sphere.c, tube.c, yarandom.c |
| hilbert | rotator.c, sphere.c, tube.c, yarandom.c |
| juggler3d | rotator.c, sphere.c, tube.c, yarandom.c |
| jigsaw | normals.c, rotator.c, spline.c, yarandom.c |
| kaleidocycle | normals.c, rotator.c, yarandom.c |
| mapscroller | easing.c |
| molecule | rotator.c, sphere.c, tube.c, yarandom.c |
| pinion | involute.c, normals.c, rotator.c, yarandom.c |
| polyhedra-gl | normals.c, polyhedra.c, rotator.c, teapot.c, yarandom.c |
| razzledazzle | gllist.c, normals.c, ships.c, yarandom.c |
| skulloop | easing.c, gllist.c, normals.c, skull_model.c, yarandom.c |
| spheremonics | normals.c, rotator.c, yarandom.c |
| splitflap | gllist.c, rotator.c, splitflap_obj.c, yarandom.c |
| tangram | tangram_shapes.c |
| unicrud | rotator.c, yarandom.c |
| winduprobot | gllist.c, involute.c, normals.c, robot.c, robot-wireframe.c, sphere.c, yarandom.c |
| headroom | gllist.c, headroom_model.c, rotator.c, skull_model.c, yarandom.c |
| hexstrut | rotator.c, yarandom.c |
| hextrail | rotator.c, yarandom.c |
| hydrostat | sphere.c |
| hypnowheel | rotator.c, yarandom.c |
| kallisti | gllist.c, kallisti_model.c, rotator.c, yarandom.c |
| lavalite | marching.c, normals.c, rotator.c, yarandom.c |
| lockward | - |
| menger | rotator.c, yarandom.c |
| moebiusgears | involute.c, normals.c, rotator.c, yarandom.c |
| nakagin | doubletime.c, easing.c, normals.c, rotator.c, yarandom.c |
| noof | pow2.c |
| papercube | rotator.c, yarandom.c |
| peepers | image_data_to_ximage.c, normals.c, rotator.c, yarandom.c |
| photopile | dropshadow.c, xftwrap.c, yarandom.c |
| providence | - |
| quasicrystal | rotator.c, yarandom.c |
| raverhoop | rotator.c, yarandom.c |
| rubikblocks | rotator.c, yarandom.c |
| sballs | image_data_to_ximage.c |
| skytentacles | image_data_to_ximage.c, normals.c, rotator.c, yarandom.c |
| splodesic | rotator.c, yarandom.c |
| starwars | glut_stroke.c, glut_swidth.c, yarandom.c |
| squirtorus | easing.c, normals.c, spline.c, yarandom.c |
| timetunnel | rotator.c, yarandom.c |
| topblock | sphere.c, tube.c |
| tronbit | doubletime.c, gllist.c, rotator.c, sphere.c, tronbit_idle1.c, tronbit_idle2.c, tronbit_no.c, tronbit_yes.c, yarandom.c |
| unknownpleasures | doubletime.c, easing.c |
| vigilance | gllist.c, normals.c, seccam.c |
| voronoi | - |

## Ported (31 GLES3-native, no gl4es)

These 31 build as `<name>_gles3` binaries linked directly against
system libGLESv2 / libEGL — no libGL.so.1, no gl4es, no translation
shim. The vendored xscreensaver source compiles against the GLES3
compat layer (`gles3_compat.h`) which routes every glBegin/glVertex/
glColor/glNormal/glMatrixMode/glLightfv/glMaterialfv/glNewList/...
call through to a small fixed-function shader pair and CPU vertex
accumulator. Verified on this build host: `ldd <binary>` shows
`libEGL.so.1` + `libGLESv2.so.2` + `libGLdispatch.so.0` only.

### Phase 1 pilots (boing, companion)

The first two native ports — boing (immediate-mode + matrix stack +
lighting + ortho scanlines) and companion (gllist VBO conversion +
display-list recorder). Hand-written ports in `src/gles3_boing.c` /
`src/gles3_companion.c`. Verified on O6N real hardware 2026-08-20
with multiple `grim` captures — see GLES3-MIGRATION-PHASE1.md for
the full verification trail.

### Phase 2 mechanical ports (10 new this round)

These 10 use the same vendored xscreensaver source as the
gl4es-routed `_demo` binary; the only thing different is that
they are now built against the GLES3-native path with no source
edits to the vendored hack. The active stubs in `gles3_compat.c`
route glBegin/glVertex/glColor/glNormal/glMatrixMode/glLightfv/
glMaterialfv/glNewList/glEndList/glCallList/glInterleavedArrays/
glVertexPointer/glNormalPointer/glTexCoordPointer/etc. through
the ncz_im_*/ncz_mat_stack_*/nczGLList_*/ncz_dl_* surface that
emits native GLES3 calls. No `libGL.so.1` in the link for any of
these. Visual rendering evidence was NOT obtained on the build
host (no working Wayland display attached at run time); the
verifiable evidence is the link-time check (`ninja -C build` links
each target, `ldd` shows no libGL.so.1) which matches the project's
established ported-only-if-LINKS rule.

| hack | companion sources |
|---|---|
| antinspect_gles3 | sphere.c |
| antspotlight_gles3 | rotator.c, sphere.c, tube.c, yarandom.c |
| beats_gles3 | sphere.c |
| blinkbox_gles3 | sphere.c |
| blocktube_gles3 | - |
| bouncingcow_gles3 | cow_face.c, cow_hide.c, cow_hoofs.c, cow_horns.c, cow_tail.c, cow_udder.c, gllist.c, rotator.c, yarandom.c |
| boing_gles3 | - |
| chompytower_gles3 | doubletime.c, easing.c, gllist.c, normals.c, rotator.c, sphere.c, spline.c, teeth_model.c, yarandom.c |
| companion_gles3 | companion_disc.c, companion_heart.c, companion_quad.c, rotator.c, yarandom.c |
| crumbler_gles3 | quickhull.c, rotator.c, yarandom.c |
| cube21_gles3 | - |
| cubestack_gles3 | rotator.c, yarandom.c |

### Phase 3 mechanical ports (13 new this round, 1 deferred)

Two commits landed in this dispatch, jointly delivering 13 native
GLES3 ports out of the legacy gl4es-routed table:

  Commit 1 (foundation + glFrustum cohort) — 4 ports + 1 helper:
    energystream, highvoltage, stonerview, lament
    (and the ncz_mat4_frustum matrix builder + glFrustum shim they
     needed in `gles3_compat.c`).

  Commit 2 (alphabetical Phase 3 batch) — 9 ports:
    cityflow, covid19, crackberg, cubenetic, cubestorm, cubetwist,
    cubicgrid, dangerball, discoball.

  Deferred (1, unchanged from the rejected fd3f875):
    dnalogo — blocked on missing GLU tessellator stubs
              (gluNewTess / gluTessBeginPolygon / gluTessCallback /
              gluTessVertex / gluDeleteTess) in the GLES3 compat
              shim. See Deferred below.

The 13 vendored hack `.c` files are unchanged from upstream
xscreensaver — no source edits inside any of them. What IS new
this round in `gles3_compat.c` (foundation extensions the new
consumers needed):

- `ncz_mat4_frustum` matrix builder + `glFrustum` shim.
  Standard textbook GL1 frustum (column-major, m[11] = -1).
  Called by 4 legacy hacks that all use asymmetric or
  non-gluPerspective frustums: energystream (narrow asymmetric,
  `glFrustum(-.6, .6, -.45, .45, 1, 1000)`),
  highvoltage (per-frame variable width driven by the audio
  analyzer), stonerview and lament (both `glFrustum(-1, 1,
  -h, h, 5, 60)`). These 4 are wired into `legacy_gles3_hacks`
  IN THE SAME DISPATCH so the helper is not foundation-without-
  in-round-consumer. This addresses the [medium] glFrustum-scope
  finding from the rejected fd3f875 review.

- `crackberg` uses GLdouble immediate-mode calls (`glNormal3d` /
  `glColor3d` / `glVertex3d` and their `v` pointer variants) and
  `glColorMaterial`. Six new narrow-to-float entry-point stubs
  (`glVertex3d` / `glVertex3dv` / `glColor3d` / `glColor3dv` /
  `glNormal3d` / `glNormal3dv`) that forward to the same
  `ncz_im_*` helpers the float versions use — stub-for-stub parity
  with the existing `glRotated` / `glScaled` / `glTranslated`
  doubles that were already in `gles3_compat.c`. Verified by
  per-name pre-vs-post-patch count in
  `docs/audit/phase3-fix-audit.txt` §4 that none of these 6 names
  existed pre-patch in `gles3_compat.c` (zero → one each), so there
  is no duplicate-symbol link risk.

- `glColorMaterial` (also used by `crackberg`) is a no-op stub.
  The shader pair's material plumbing (`gles3_compat.c`
  `ncz_im_material` lines 754-759 with the `g_im.has_material = true`
  flip at line 758, plus the three uniform-write sites at lines
  1081-1082, 1291-1292, 1327-1328) flips the `u_has_material`
  uniform IFF `g_im.has_material` is true. `g_im.has_material` is
  set to true ONLY inside `ncz_im_material` (line 758), which is
  reachable ONLY from `glMaterial*` calls (declared at lines
  1628, 1636, 1643). `crackberg.c` never calls `glMaterial*`
  (verified by `grep -nE glMaterial src/crackberg.c` returning zero
  matches), so `g_im.has_material` stays false from init to
  teardown and the vertex shader's
  `vec4 base = u_has_material ? u_material_color : a_color`
  always picks `a_color` (per-vertex color from `glColor*`).
  Identical fragment output with or without honoring the
  GL_COLOR_MATERIAL flag. The 12 other Phase 3 + glFrustum-
  cohort hacks also never call `glColorMaterial` (full transcript
  in `docs/audit/phase3-fix-audit.txt` §5 Step 3), so the only
  binary whose link is influenced by this stub is crackberg, and
  crackberg is provably inert. The source comment at
  `gles3_compat.c` lines ~1722-1768 carries the same proof inline.

Co-located link/ldd evidence for this round:
`docs/audit/phase3-fix-audit.txt` (fresh capture 2026-08-23 16:11
UTC after this commit) captures for all 13 `_gles3` targets:

  (a) `touch src/gles3_compat.c && ninja -C build <all-13>` output
      — 13 compile + 13 link lines, all exit 0, no undefined-
      reference errors,
  (b) `ldd build/<target>_gles3` filtered to libEGL + libGLESv2 +
      libGLdispatch for each binary, with a `libgl4es.so present`
      count — every entry shows libEGL + libGLESv2 + libGLdispatch
      only, `libgl4es.so present: 0` per binary,
  (c) `nm build/<target>_gles3 | grep <target>_xscreensaver_function_table`
      confirming the vendored .c actually compiled in (not a
      harness-only build) for each of the 13 binaries,
  (d) pre-patch (e90b12f:src/gles3_compat.c) vs post-patch
      (HEAD:src/gles3_compat.c) per-name count for the 6 double
      helpers (`grep -cE '^void gl(Color|Normal|Vertex)3d[v]?'`),
      all six showing pre=0 post=1, ruling out duplicate-symbol
      link risk,
  (e) glColorMaterial no-op correctness proof (crackberg-specific)
      — `grep -nE 'glMaterial' src/crackberg.c` returns ZERO
      matches, while `grep -nE 'glColorMaterial' src/crackberg.c`
      returns the one call site at line 1225; combined with the
      GLSL excerpt from VERT_SHADER (line 259) and FRAG_SHADER
      (line 294) and the g_im.has_material flip point in
      `ncz_im_material` (lines 754-759, line 758 specifically),
      this proves the no-op is observationally equivalent to
      honoring the GL_COLOR_MATERIAL flag for crackberg's specific
      usage. The source comment at gles3_compat.c lines ~1722-1768
      carries the same proof inline.

**Honest visual-evidence disclosure (per directive's honesty clause):**

No PNG / PPM / `grim` capture was produced on this build host for
ANY of the 13 `_gles3` targets, including crackberg. The build host
has no working Wayland compositor attached, no X server, no Xvfb,
and weston's headless backend on this image is broken (undefined
`png_set_longjmp_fn` symbol — packaging bug, not in scope to fix
here). The gles3_harness hard-codes
`eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, ...)` and so cannot
use the EGL_MESA_platform_surfaceless offscreen path without
harness surgery. The verifiable evidence is the link/ldd/nm
evidence in `phase3-fix-audit.txt`; whether each hack renders the
same picture as upstream xscreensaver on a real Wayland display is
UNVERIFIED in this commit and will be checked the first time the
binaries are attached to a desktop. The post-fix reviewer's offered
honesty-clause alternative ("say so explicitly per the directive's
honesty clause rather than claiming crackberg as ported") is
heeded by this explicit paragraph; the link/ldd/nm evidence above
shows the bars the project sets for "ported", and the visual bar is
deferred until a real compositor is available.

| hack | companion sources |
|---|---|
| cityflow_gles3      | - |
| covid19_gles3       | rotator.c, sphere.c, tube.c, yarandom.c |
| crackberg_gles3     | - |
| cubenetic_gles3     | rotator.c, yarandom.c |
| cubestorm_gles3     | rotator.c, yarandom.c |
| cubetwist_gles3     | rotator.c, yarandom.c |
| cubicgrid_gles3     | rotator.c, yarandom.c |
| dangerball_gles3    | rotator.c, sphere.c, tube.c, yarandom.c |
| discoball_gles3     | rotator.c, yarandom.c |
| energystream_gles3  | rotator.c, yarandom.c |
| highvoltage_gles3   | gllist.c, highvoltage_model.c, normals.c, tube.c |
| stonerview_gles3    | stonerview-view.c, stonerview-move.c, stonerview-osc.c, yarandom.c |
| lament_gles3        | gllist.c, lament_model.c, image_data_to_ximage.c, normals.c, rotator.c, yarandom.c |
| flyingtoasters_gles3 | gllist.c, image_data_to_ximage.c, toast.c, toast2.c, toaster.c, toaster_base.c, toaster_handle.c, toaster_handle2.c, toaster_jet.c, toaster_knob.c, toaster_slots.c, toaster_wing.c, yarandom.c |
| geodesicgears_gles3  | involute.c, normals.c, rotator.c, tube.c, yarandom.c |
| gibson_gles3         | easing.c, rotator.c, yarandom.c |
| glforestfire_gles3   | image_data_to_ximage.c |
| glhanoi_gles3        | doubletime.c, rotator.c, yarandom.c |
| gltext_gles3         | glut_stroke.c, glut_swidth.c, rotator.c, sphere.c, tube.c, yarandom.c |
| handsy_gles3         | doubletime.c, gllist.c, handsy_model.c, rotator.c, sphere.c, tube.c, yarandom.c |
| kallisti_gles3       | gllist.c, kallisti_model.c, rotator.c, yarandom.c |
| photopile_gles3      | dropshadow.c, xftwrap.c, yarandom.c |
| providence_gles3     | - |
| unicrud_gles3        | rotator.c, yarandom.c |
| unknownpleasures_gles3 | doubletime.c, easing.c |
| headroom_gles3       | gllist.c, headroom_model.c, rotator.c, skull_model.c, yarandom.c |
| lavalite_gles3       | marching.c, normals.c, rotator.c, yarandom.c |
| nakagin_gles3        | doubletime.c, easing.c, normals.c, rotator.c, yarandom.c |
| raverhoop_gles3      | rotator.c, yarandom.c |
| razzledazzle_gles3   | gllist.c, normals.c, ships.c, yarandom.c |
| sballs_gles3         | image_data_to_ximage.c |
| skulloop_gles3       | easing.c, gllist.c, normals.c, skull_model.c, yarandom.c |
| skytentacles_gles3   | image_data_to_ximage.c, normals.c, rotator.c, yarandom.c |
| splitflap_gles3      | gllist.c, rotator.c, splitflap_obj.c, yarandom.c |
| starwars_gles3       | glut_stroke.c, glut_swidth.c, yarandom.c |
| winduprobot_gles3    | gllist.c, image_data_to_ximage.c, involute.c, normals.c, robot.c, robot-wireframe.c, sphere.c, yarandom.c |
| fliptext_gles3        | - |
| gears_gles3           | involute.c, normals.c, rotator.c, tube.c, yarandom.c |
| geodesic_gles3        | normals.c, rotator.c, yarandom.c |
| glblur_gles3          | rotator.c, yarandom.c |
| glcells_gles3         | - |
| glknots_gles3         | rotator.c, tube.c, yarandom.c |
| glschool_gles3        | glschool_alg.c, glschool_gl.c, sphere.c, tube.c, yarandom.c |
| glsnake_gles3         | - |
| gravitywell_gles3     | - |
| hexstrut_gles3        | rotator.c, yarandom.c |
| hextrail_gles3        | rotator.c, yarandom.c |
| hilbert_gles3         | rotator.c, sphere.c, tube.c, yarandom.c |
| hydrostat_gles3       | sphere.c |
| hypnowheel_gles3      | rotator.c, yarandom.c |
| jigsaw_gles3          | normals.c, rotator.c, spline.c, yarandom.c |
| juggler3d_gles3       | rotator.c, sphere.c, tube.c, yarandom.c |
| kaleidocycle_gles3    | normals.c, rotator.c, yarandom.c |
| lockward_gles3        | - |
| mapscroller_gles3     | easing.c |
| menger_gles3          | rotator.c, yarandom.c |
| moebiusgears_gles3    | involute.c, normals.c, rotator.c, yarandom.c |
| molecule_gles3        | rotator.c, sphere.c, tube.c, yarandom.c |
| noof_gles3            | pow2.c |
| papercube_gles3       | rotator.c, yarandom.c |
| peepers_gles3         | image_data_to_ximage.c, normals.c, rotator.c, yarandom.c |
| quasicrystal_gles3    | rotator.c, yarandom.c |
| rubikblocks_gles3     | rotator.c, yarandom.c |
| spheremonics_gles3    | normals.c, rotator.c, yarandom.c |
| splodesic_gles3       | normals.c, rotator.c, yarandom.c |
| squirtorus_gles3      | easing.c, normals.c, spline.c, yarandom.c |
| tangram_gles3         | tangram_shapes.c |
| topblock_gles3        | sphere.c, tube.c |
| tronbit_gles3         | doubletime.c, gllist.c, rotator.c, sphere.c, tronbit_idle1.c, tronbit_idle2.c, tronbit_no.c, tronbit_yes.c, yarandom.c |
| voronoi_gles3         | - |

### Upstream xscreensaver 6.00+ GLES3 rewrites (6 Carsten Steger hacks)

Already real GLSL/GLES3 shader code upstream, ported directly to the
GLES3-native path (no gl4es, link against system libGLESv2 / libEGL
on O6N's Mali-G720-Immortalis). See UPSTREAM-GLES3-HACKS-2026-08-20.md
for the per-hack verification, the foundation extensions they needed,
and the per-hack commit log.

| hack | companion sources |
|---|---|
| etruscanvenus_gles3   | glsl-utils.c |
| hypertorus_gles3      | glsl-utils.c |
| klein_gles3           | glsl-utils.c, curlicue.h |
| projectiveplane_gles3 | glsl-utils.c, curlicue.h |
| romanboy_gles3        | glsl-utils.c, curlicue.h |
| sphereeversion_gles3  | glsl-utils.c, sphereeversion-analytic.c, sphereeversion-corrugations.c, sphereeversion.h, earth.c, image_data_to_ximage.c |

Total ported: **94 unique hacks** (65 legacy gl4es-routed + 88 GLES3-native − 59 dual-listed).
GLES3-native count: 2 (Phase 1) + 10 (Phase 2 mechanical) + 13 (Phase 3 mechanical — 9 alphabetical + 4 glFrustum cohort) + 6 (Phase 1+ upstream) + 57 (Phase 4 + Round 11 — 5+8+9+13+12+12 alphabetical batches 590a7fe→ef6d0dd→1737154→25cdc4e) = 88.
Remaining to migrate off gl4es: 6 (was 65; 59 moved to native across all rounds).

Of the 88 native ports, 59 are dual-listed (have both `_demo` and `_gles3` binaries — the _demo will be deleted in the final Phase 4+ step per the directive). The other 29 are native-only (boing, companion, the 6 Phase 1+ upstream Carsten Steger hacks, plus 23 hacks that had no gl4es-routed `_demo` registered).

## Deferred (6)

Blocked on architectural gaps that exceed the scope of this round.

| hack | blocking reason |
|---|---|
| b_lockglue | This is an xlock-mode hack, not a GL hack. It depends on the xlock pipeline (xlock.h, bubble3d.h, vis.h) which we have not ported because the whole xlockmode / `xlockmore_passwd_authenticate` flow does not apply to a compositor-driven Wayland build. |
| sonar | Uses POSIX threads via thread_util.h to parallelize the FFT across CPU cores, AND raw ICMP sockets (sonar-icmp.c) AND DNS resolution (sonar-sim.c). Sonar's recorded Display-conflict error is misleading; the actual blocker is the missing threading + network support. |
| dnalogo | Uses the GLU tessellator API for the "double helix" path that draws two intertwining strands of DNA nucleotides via a polygon-tessellated outline. Specific blocking calls (all in src/dnalogo.c): `gluNewTess` (line 1556), `gluTessCallback` (lines 1565-1569, callbacks for `GLU_TESS_BEGIN`/`GLU_TESS_END`/`GLU_TESS_VERTEX`/`GLU_TESS_COMBINE`/`GLU_TESS_ERROR`), `gluTessProperty` (lines 1571-1572, sets `GLU_TESS_BOUNDARY_ONLY` and `GLU_TESS_WINDING_RULE`/`GLU_TESS_WINDING_ODD`), `gluTessBeginPolygon` (line 1752), `gluTessNormal` (line 1755), `gluTessBeginContour`/`gluTessVertex`/`gluTessEndContour`/`gluTessEndPolygon` (lines 1759-1783, nested 2-contour polygon with per-vertex GLdouble pointer pairs), `gluDeleteTess` (line 1936). Also `gluErrorString` (line 1494) for error stringification. None of these are stubbed in `gles3_compat.c` — only `gluPerspective` and `gluLookAt` are. The gl4es-routed `_demo` binary links against system libGLU. Porting cleanly requires either a GLU tessellator port to the GLES3 path (non-trivial — the tessellator is a substantial piece of geometry code) or rewriting dnalogo.c to use a triangulation fallback the GLES3 fixed-function shader can already emit. Was the 10th entry in Phase 3 dispatch's alphabetical batch; deferred per the rule "do not force a broken port, move on to the next hack". |
| pinion | Uses GLU `gluPickMatrix` (line 1207) in addition to `gluPerspective`/`gluLookAt` (already stubbed). gluPickMatrix is called inside the selection-mode picking path that maps a mouse-click 5x5 pixel window into a projection frustum modification for hit-testing. Also uses the GL1 selection-mode API — `glInitNames` / `glPushName` / `glPopName` / `glRenderMode` / `glSelectBuffer` — none of which exist in GLES3 (GLES3 dropped the GL1 hit-test pipeline entirely; the canonical replacement is per-object ID render-to-texture, which would require a fundamental change to the gles3_compat shader pair). pinion.c has 9 glu calls, 12 glPushName/popName/pushMatrix/popMatrix calls, and 1 glRenderMode call. None of these can be shimmed in the same no-op-fallback style as glHint/glLineWidth — the picking path is the only way pinion determines which tooth of which gear the user clicked on, so a no-op shim would render the picking as a non-functional decoration. Blocked on: (1) gluPickMatrix stub (small, tractable: 4-line projection-matrix multiply), AND (2) GL1 picking API port to GLES3 (non-trivial: requires a render-to-texture or transform-feedback path that emits a hit-test primitive id per object). The (1) alone is mechanical; (2) is the architectural blocker. |
| polyhedra-gl | Uses the GLU tessellator for nonconvex face triangulation — `gluNewTess` (line 326), `gluTessCallback` (lines 327-330, BEGIN/END/VERTEX/ERROR), `gluTessBeginPolygon`/`gluTessBeginContour`/`gluTessVertex`/`gluTessEndContour`/`gluTessEndPolygon` (lines 397-405), `gluDeleteTess` (line 424). Also includes `<GL/glu.h>` (line 30) for `GLUtesselator`/`GLU_TESS_*` enum constants. CRITICAL structural detail: polyhedra-gl has TWO code paths gated on `#ifdef HAVE_TESS` (line 52 defines HAVE_TESS under `#ifndef HAVE_JWZGLES`). Currently the vendored build defines neither — which means HAVE_TESS is active, meaning the tessellator path IS the active path in the current build. However, polyhedra-gl also has a `#else /* !HAVE_TESS */` branch (lines 406-417) that just emits `glBegin(GL_LINE_LOOP / GL_TRIANGLES / GL_QUADS / GL_POLYGON)` for each face — a flat-face fallback. Switching to that fallback requires defining `HAVE_JWZGLES` in the c_args for this hack (the GLES3 build's compatibility name for "skip the GLU bits"). Known visual inaccuracy in that fallback: non-concave faces (e.g., the "pentagrammic concave deltohedron" example in the source comment at line 322) will draw incorrectly (incorrect winding / missing triangulation). For 12 of the 13 polyhedra the fallback is geometrically correct (all faces are convex), so the visual inaccuracy is bounded to specific user-selected `do_which` values. Blocked on: a decision between (a) porting the GLU tessellator to GLES3 path (non-trivial, shared with dnalogo), or (b) accepting the bounded visual inaccuracy and adding `-DHAVE_JWZGLES` to polyhedra-gl's `c_args_extra`. |
| timetunnel | Uses GLU `gluScaleImage` (line 886) for texture mipmap-style downscaling. The call is inside `#ifndef HAVE_JWZGLES` (line 884), so defining `-DHAVE_JWZGLES` in c_args would skip the call entirely — but the texture data path then skips the downscale step, and timetunnel's visual depends on the pre-scaled textures for performance (the original code rescaled to powers of 2 to keep GPU upload cheap). gluScaleImage is a single function — it could be shimmed as a memcpy when src/dst dimensions match and a libpng-resampling or simple bilinear loop when they don't. Other GL1 calls in timetunnel.c: glLightfv, glLightf, glLightModeli, glShadeModel, glFogf, glFogfv, glHint, glColorMaterial, glGetIntegerv, glOrtho — all of these are already stubbed in gles3_compat.c or are real GLES3 natives. So the only real blocker is the gluScaleImage shim. Tractable (small libm-driven bilinear sample loop, ~50 lines), no architectural barrier. |
