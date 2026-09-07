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

## Ported (90 GLES3-native, no gl4es)

These 90 build as `<name>_gles3` binaries linked directly against
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
| polyhedra-gl_gles3    | normals.c, polyhedra.c, rotator.c, teapot.c, yarandom.c | *(see inline note: bounded visual tradeoff for nonconvex faces)* |
| quasicrystal_gles3    | rotator.c, yarandom.c |
| rubikblocks_gles3     | rotator.c, yarandom.c |
| spheremonics_gles3    | normals.c, rotator.c, yarandom.c |
| splodesic_gles3       | normals.c, rotator.c, yarandom.c |
| squirtorus_gles3      | easing.c, normals.c, spline.c, yarandom.c |
| tangram_gles3         | tangram_shapes.c |
| timetunnel_gles3      | image_data_to_ximage.c, rotator.c, yarandom.c |
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

Total ported: **94 unique hacks** (65 legacy gl4es-routed + 90 GLES3-native − 61 dual-listed).
GLES3-native count: 2 (Phase 1) + 10 (Phase 2 mechanical) + 13 (Phase 3 mechanical — 9 alphabetical + 4 glFrustum cohort) + 6 (Phase 1+ upstream) + 57 (Phase 4 + Round 11 — 5+8+9+13+12+12 alphabetical batches 590a7fe→ef6d0dd→1737154→25cdc4e) + 2 (Round 12 final-6 tractable: polyhedra-gl via -DHAVE_JWZGLES fallback, timetunnel via new gluScaleImage shim) = 90.
Remaining to migrate off gl4es: 4 (was 65; 61 moved to native across all rounds — 59 in earlier rounds, 2 in Round 12).

Of the 90 native ports, 61 are dual-listed (have both `_demo` and `_gles3` binaries — the _demo will be deleted in the final Phase 4+ step per the directive). The other 29 are native-only (boing, companion, the 6 Phase 1+ upstream Carsten Steger hacks, plus 23 hacks that had no gl4es-routed `_demo` registered). The Round 12 additions (polyhedra-gl and timetunnel) are dual-listed: they keep their `_demo` legacy registrations and gain `_gles3` native ones, same as every other Round 11/12 port. The directive to retire gl4es for good waits on dnalogo / pinion (and the two non-GL Deferred entries b_lockglue + sonar).

### Round 12 — final-6 tractable (2 ports, 1 new helper, 1 fallback flag)

Two of the six remaining Deferred blockers from the prior session
were tractable without an architectural change:

**polyhedra-gl_gles3** (commit a979ade): vendored source's
`#ifndef HAVE_JWZGLES / #define HAVE_TESS` gate (src/polyhedra-gl.c
lines 51-53) has a flat-face fallback at lines 406-417 which emits
`glBegin(GL_LINE_LOOP / GL_TRIANGLES / GL_QUADS / GL_POLYGON)` per
face. That fallback is geometrically correct for convex-faced
polyhedra; the tessellator exists for the nonconvex case (the source
comment at line 322 cites the "pentagrammic concave deltohedron" as
the motivating example). Single-line meson.build change adds
`polyhedra-gl` to `legacy_gles3_hacks` with `c_args_extra:
['-DHAVE_JWZGLES']`. Same flag-equivalence already proven by jigsaw's
port (line 652 of meson.build).

**DOCUMENTED VISUAL TRADEOFF (not hidden):** for a user-selectable
`do_which` value pointing at a nonconvex-face polyhedron (the
small-stellated-dodecahedron family in the Wythop's Wythoff table —
"X005|2" in Wythoff notation), the face renders as a
self-intersecting flat polygon instead of the triangulated outline
the tessellator would produce. Convex selections (cube, octahedron,
dodecahedron, icosahedron, etc.) are visually unchanged. The
polyhedra table is constructed dynamically by kaleido() in
polyhedra.c so the exact count of convex vs nonconvex depends on
what kaleido() emits; the qualitative claim — convex entries are
correct, nonconvex entries are wrong — is what the operator needs
to decide whether this bounded inaccuracy is preferable to
indefinite deferral.

A small follow-on change was needed: the legacy_gles3_hacks loop
previously assumed `xscreensaver_function_table` was always named
`<hack_name>_xscreensaver_function_table`. polyhedra-gl.c emits its
table via `XSCREENSAVER_MODULE("Polyhedra", polyhedra)` (line 686)
so the actual symbol is `polyhedra_xscreensaver_function_table`, not
`polyhedra-gl_xscreensaver_function_table`. The legacy `_demo` path
already had a `'table'` dict-key override for this kind of mismatch
(see line 453 in meson.build). The `legacy_gles3_hacks` foreach loop
now honors the same `'table'` override, defaulting to `h['name']`
when absent. No other hack is affected.

**timetunnel_gles3** (commit ec196d3): the proper fix, NOT the cheap
skip. The cheap option (`-DHAVE_JWZGLES`) would have skipped the
texture-downscale step at src/timetunnel.c:884-894 entirely; the
proper fix implements `gluScaleImage` in `src/xscreensaver_compat.c`
as a bilinear-interpolation image resampler. New ~140-line helper:

  gluScaleImage(GLenum format, GLint srcW, GLint srcH,
                GLenum srcType, const void *srcData,
                GLint dstW, GLint dstH, GLenum dstType,
                void *dstData)

  - Supports only GL_RGBA / GL_UNSIGNED_BYTE in and out (the only
    combo timetunnel.c:886 actually calls). Other combos return
    GLU_ERROR (the canonical Mesa-GLU error code) without writing
    dstData, matching upstream Mesa GLU's behavior on unsupported
    format/type combos.
  - memcpy fast path for identity resize (preserves exact bytes).
  - Center-of-pixel sampling: sx = (dx+0.5)*srcW/dstW - 0.5.
  - Bilinear kernel with clamp-to-edge wrap (matches GLU and
    GL_CLAMP_TO_EDGE GPU sampler convention).
  - Per-channel 8-bit saturation.
  - 16 sanity-test assertions (CPU-only, see tools/test-gluScaleImage.c)
    plus 14 end-to-end assertions linked against the ACTUAL deployed
    .o file (tools/test-gluScaleImage-from-build.c) all pass.

No call-site shape change: timetunnel.c:886 still calls
gluScaleImage with its original Mesa signature. The shim matches it
exactly. Single-line meson.build change adds timetunnel to
legacy_gles3_hacks with image_data_to_ximage in extras. No c_args
flag is needed — the shim is reached via normal symbol resolution
inside xscreensaver_compat.c.

## Deferred (4)

Blocked on architectural gaps that exceed the scope of this round.

| hack | blocking reason |
|---|---|
| b_lockglue | This is an xlock-mode hack, not a GL hack. It depends on the xlock pipeline (xlock.h, bubble3d.h, vis.h) which we have not ported because the whole xlockmode / `xlockmore_passwd_authenticate` flow does not apply to a compositor-driven Wayland build. |
| sonar | Uses POSIX threads via thread_util.h to parallelize the FFT across CPU cores, AND raw ICMP sockets (sonar-icmp.c) AND DNS resolution (sonar-sim.c). Sonar's recorded Display-conflict error is misleading; the actual blocker is the missing threading + network support. |
| dnalogo | Uses the GLU tessellator API for the "double helix" path that draws two intertwining strands of DNA nucleotides via a polygon-tessellated outline. Specific blocking calls (all in src/dnalogo.c): `gluNewTess` (line 1556), `gluTessCallback` (lines 1565-1569, callbacks for `GLU_TESS_BEGIN`/`GLU_TESS_END`/`GLU_TESS_VERTEX`/`GLU_TESS_COMBINE`/`GLU_TESS_ERROR`), `gluTessProperty` (lines 1571-1572, sets `GLU_TESS_BOUNDARY_ONLY` and `GLU_TESS_WINDING_RULE`/`GLU_TESS_WINDING_ODD`), `gluTessBeginPolygon` (line 1752), `gluTessNormal` (line 1755), `gluTessBeginContour`/`gluTessVertex`/`gluTessEndContour`/`gluTessEndPolygon` (lines 1759-1783, nested 2-contour polygon with per-vertex GLdouble pointer pairs), `gluDeleteTess` (line 1936). Also `gluErrorString` (line 1494) for error stringification. None of these are stubbed in `gles3_compat.c` — only `gluPerspective`, `gluLookAt`, `gluProject` and the new Round 12 `gluScaleImage` are. The gl4es-routed `_demo` binary links against system libGLU. Porting cleanly requires either a GLU tessellator port to the GLES3 path (non-trivial — the tessellator is a substantial piece of geometry code) or rewriting dnalogo.c to use a triangulation fallback the GLES3 fixed-function shader can already emit. Was the 10th entry in Phase 3 dispatch's alphabetical batch; deferred per the rule "do not force a broken port, move on to the next hack". |
| pinion | Uses GLU `gluPickMatrix` (line 1207) in addition to `gluPerspective`/`gluLookAt` (already stubbed). gluPickMatrix is called inside the selection-mode picking path that maps a mouse-click 5x5 pixel window into a projection frustum modification for hit-testing. Also uses the GL1 selection-mode API — `glInitNames` / `glPushName` / `glPopName` / `glRenderMode` / `glSelectBuffer` — none of which exist in GLES3 (GLES3 dropped the GL1 hit-test pipeline entirely; the canonical replacement is per-object ID render-to-texture, which would require a fundamental change to the gles3_compat shader pair). pinion.c has 9 glu calls, 12 glPushName/popName/pushMatrix/popMatrix calls, and 1 glRenderMode call. None of these can be shimmed in the same no-op-fallback style as glHint/glLineWidth — the picking path is the only way pinion determines which tooth of which gear the user clicked on, so a no-op shim would render the picking as a non-functional decoration. Blocked on: (1) gluPickMatrix stub (small, tractable: 4-line projection-matrix multiply), AND (2) GL1 picking API port to GLES3 (non-trivial: requires a render-to-texture or transform-feedback path that emits a hit-test primitive id per object). The (1) alone is mechanical; (2) is the architectural blocker. |
