# Ported xscreensaver hacks

A hack counts as ported only if `ninja -C build` LINKS its <name>_demo
target. Compiling is not enough: missing companion sources surface only at
link time.

## Ported (88)

| hack | companion sources |
|---|---|
| antinspect | sphere.c |
| antspotlight | rotator.c, sphere.c, tube.c, yarandom.c |
| beats | sphere.c |
| boing | - |
| blinkbox | sphere.c |
| blocktube | - |
| bouncingcow | cow_face.c, cow_hide.c, cow_hoofs.c, cow_horns.c, cow_tail.c, cow_udder.c, gllist.c, rotator.c, yarandom.c |
| chompytower | doubletime.c, easing.c, gllist.c, normals.c, rotator.c, sphere.c, spline.c, teeth_model.c, yarandom.c |
| crumbler | quickhull.c, rotator.c, yarandom.c |
| cube21 | - |
| cubestack | rotator.c, yarandom.c |
| cubestorm | rotator.c, yarandom.c |
| cubetwist | rotator.c, yarandom.c |
| dangerball | rotator.c, sphere.c, tube.c, yarandom.c |
| cubicgrid | rotator.c, yarandom.c |
| cityflow | - |
| companion | companion_disc.c, companion_heart.c, companion_quad.c, gllist.c, rotator.c, yarandom.c |
| covid19 | rotator.c, sphere.c, tube.c, yarandom.c |
| crackberg | - |
| cubenetic | rotator.c, yarandom.c |
| discoball | rotator.c, yarandom.c |
| dnalogo | normals.c, rotator.c, sphere.c, tube.c, yarandom.c |
| energystream | rotator.c, yarandom.c |
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
| highvoltage | gllist.c, highvoltage_model.c, normals.c, tube.c |
| hydrostat | sphere.c |
| hypnowheel | rotator.c, yarandom.c |
| kallisti | gllist.c, kallisti_model.c, rotator.c, yarandom.c |
| lament | gllist.c, image_data_to_ximage.c, lament_model.c, normals.c, rotator.c, yarandom.c |
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
| stonerview | stonerview-move.c, stonerview-osc.c, stonerview-view.c, yarandom.c |
| timetunnel | rotator.c, yarandom.c |
| topblock | sphere.c, tube.c |
| tronbit | doubletime.c, gllist.c, rotator.c, sphere.c, tronbit_idle1.c, tronbit_idle2.c, tronbit_no.c, tronbit_yes.c, yarandom.c |
| unicrud | rotator.c, yarandom.c |
| unknownpleasures | doubletime.c, easing.c |
| vigilance | gllist.c, normals.c, seccam.c |
| voronoi | - |

## Deferred (2)

Blocked on architectural gaps that exceed the scope of this round.

| hack | blocking reason |
|---|---|
| b_lockglue | This is an xlock-mode hack, not a GL hack. It depends on the xlock pipeline (xlock.h, bubble3d.h, vis.h) which we have not ported because the whole xlockmode / `xlockmore_passwd_authenticate` flow does not apply to a compositor-driven Wayland build. |
| sonar | Uses POSIX threads via thread_util.h to parallelize the FFT across CPU cores, AND raw ICMP sockets (sonar-icmp.c) AND DNS resolution (sonar-sim.c). Sonar's recorded Display-conflict error is misleading; the actual blocker is the missing threading + network support. |
