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

## Ported (74 legacy gl4es-routed)

These 74 build as `<name>_demo` binaries when `-Dgl4es=enabled` is
passed AND the system has gl4es installed at a known path. On hosts
without gl4es (most), only the native `_gles3` binaries (next
section) are built.

| hack | companion sources |
|---|---|
| antspotlight | rotator.c, sphere.c, tube.c, yarandom.c |
| bouncingcow | cow_face.c, cow_hide.c, cow_hoofs.c, cow_horns.c, cow_tail.c, cow_udder.c, gllist.c, rotator.c, yarandom.c |
| cubestorm | rotator.c, yarandom.c |
| cubetwist | rotator.c, yarandom.c |
| dangerball | rotator.c, sphere.c, tube.c, yarandom.c |
| cubicgrid | rotator.c, yarandom.c |
| cityflow | - |
| covid19 | rotator.c, sphere.c, tube.c, yarandom.c |
| crackberg | - |
| cubenetic | rotator.c, yarandom.c |
| discoball | rotator.c, yarandom.c |
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

## Ported (22 GLES3-native, no gl4es)

These 22 build as `<name>_gles3` binaries linked directly against
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

### Phase 3 cohort (4 new this round, 1 deferred)

The first batch from the Phase 3 alphabetical sweep — 4 legacy hacks
that call `glFrustum(...)` directly, which needed the
`ncz_mat4_frustum` matrix builder + `glFrustum` shim added to
`gles3_compat.c` in this commit. They are mechanical ports, no
source edits inside any of the 4 vendored `.c` files:

| hack | companion sources |
|---|---|
| energystream_gles3  | rotator.c, yarandom.c |
| highvoltage_gles3   | gllist.c, highvoltage_model.c, normals.c, tube.c |
| stonerview_gles3    | stonerview-view.c, stonerview-move.c, stonerview-osc.c, yarandom.c |
| lament_gles3        | gllist.c, lament_model.c, image_data_to_ximage.c, normals.c, rotator.c, yarandom.c |

The remaining 9 alphabetical entries from the same Phase 3 batch
(cityflow, covid19, crackberg, cubenetic, cubestorm, cubetwist,
cubicgrid, dangerball, discoball) ship in a follow-up commit that
also extends `gles3_compat.c` with the 6 GLdouble immediate-mode
stubs (glVertex3d, glVertex3dv, glColor3d, glColor3dv, glNormal3d,
glNormal3dv — needed by crackberg) and the glColorMaterial no-op
stub (also needed by crackberg). The 10th alphabetical entry,
dnalogo, stays deferred — same GLU-tessellator blocker as before.

Co-located evidence for this commit's 4 ports is in
`docs/audit/phase3-cohort-link-evidence.txt`. The full Phase 3
audit (with the 9 alphabetical ports + crackberg foundations) is in
`docs/audit/phase3-fix-audit.txt` (follow-up commit).

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

Total ported: **96** (74 gl4es-routed + 22 GLES3-native).
GLES3-native count: 2 (Phase 1) + 10 (Phase 2 mechanical) + 4 (Phase 3 cohort) + 6 (Phase 1+ upstream) = 22.
Remaining to migrate off gl4es: 74 (was 78; 4 moved to native this round).

## Deferred (2)

Blocked on architectural gaps that exceed the scope of this round.

| hack | blocking reason |
|---|---|
| b_lockglue | This is an xlock-mode hack, not a GL hack. It depends on the xlock pipeline (xlock.h, bubble3d.h, vis.h) which we have not ported because the whole xlockmode / `xlockmore_passwd_authenticate` flow does not apply to a compositor-driven Wayland build. |
| sonar | Uses POSIX threads via thread_util.h to parallelize the FFT across CPU cores, AND raw ICMP sockets (sonar-icmp.c) AND DNS resolution (sonar-sim.c). Sonar's recorded Display-conflict error is misleading; the actual blocker is the missing threading + network support. |
