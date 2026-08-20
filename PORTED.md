# Ported xscreensaver hacks

A hack counts as ported only if `ninja -C build` LINKS its <name>_demo
target. Compiling is not enough: missing companion sources surface only at
link time.

## Ported (80)

| hack | companion sources |
|---|---|
| antinspect | sphere.c |
| antspotlight | rotator.c, sphere.c, tube.c, yarandom.c |
| beats | sphere.c |
| boing | - |
| blinkbox | sphere.c |
| blocktube | - |
| chompytower | doubletime.c, easing.c, gllist.c, normals.c, rotator.c, sphere.c, spline.c, teeth_model.c, yarandom.c |
| crumbler | quickhull.c, rotator.c, yarandom.c |
| cube21 | - |
| cubestack | rotator.c, yarandom.c |
| cubestorm | rotator.c, yarandom.c |
| cubetwist | rotator.c, yarandom.c |
| cubicgrid | rotator.c, yarandom.c |
| cityflow | - |
| covid19 | rotator.c, sphere.c, tube.c, yarandom.c |
| crackberg | - |
| cubenetic | rotator.c, yarandom.c |
| discoball | rotator.c, yarandom.c |
| dnalogo | normals.c, rotator.c, sphere.c, tube.c, yarandom.c |
| energystream | rotator.c, yarandom.c |
| fliptext | - |
| geodesicgears | involute.c, normals.c, rotator.c, tube.c, yarandom.c |
| gears | involute.c, normals.c, rotator.c, tube.c, yarandom.c |
| gibson | easing.c, rotator.c, yarandom.c |
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
| lockward | - |
| menger | rotator.c, yarandom.c |
| moebiusgears | involute.c, normals.c, rotator.c, yarandom.c |
| nakagin | doubletime.c, easing.c, normals.c, rotator.c, yarandom.c |
| noof | pow2.c |
| papercube | rotator.c, yarandom.c |
| peepers | image_data_to_ximage.c, normals.c, rotator.c, yarandom.c |
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
| topblock | sphere.c, tube.c |
| tronbit | doubletime.c, gllist.c, rotator.c, sphere.c, tronbit_idle1.c, tronbit_idle2.c, tronbit_no.c, tronbit_yes.c, yarandom.c |
| vigilance | gllist.c, normals.c, seccam.c |
| voronoi | - |

## Deferred (11)

Blocked on gaps in `src/xscreensaver_compat.h`, not on porting mechanics.
Each entry is the FIRST error; closing one usually reveals the next.

> Some entries below read `rebuilding build.ninja: subcommand failed`.
> Those are NOT real per-hack failures -- the tree was transiently broken
> during that batch and every hack after it inherited the error. They need
> re-running before being believed.

| hack | blocking error |
|---|---|
| b_lockglue | `../src/b_lockglue.c:49:10: fatal error: vis.h: No such file or directory` |
| bouncingcow | `/usr/bin/aarch64-linux-gnu-ld.bfd: bouncingcow_demo.p/src_ximage-loader.c.o: undefined reference to symbol 'XGetWindowAttributes'` |
| companion | `/usr/bin/aarch64-linux-gnu-ld.bfd: companion_demo.p/src_glmatrix_harness.c.o:/home/jasonperlow/ncz-screensavers/build/../src/glmatrix_harnes` |
| dangerball | `../src/voronoi.c:21:10: fatal error: xlockmore.h: No such file or directory` |
| flyingtoasters | `../src/screenhackI.h:170:10: fatal error: xft.h: No such file or directory` |
| glcells | `/tmp/xscreensaver-6.15/hacks/glx/glcells.c:40:30: error: initialization of ‘Bool (*)(ModeInfo *, XEvent *)’ {aka ‘int (*)(ModeInfo *, XEvent *)’} from incompatible pointer type ‘int (*)(ModeInfo *, void *)’ [-Wincompatible-pointer-types]` |
| lavalite | `/tmp/xscreensaver-6.15/hacks/glx/lavalite.c:343:11: error: implicit declaration of function ‘file_to_ximage’ [-Wimplicit-function-declaration]` |
| photopile | `../src/photopile.c:33:11: fatal error: X11/Intrinsic.h: No such file or directory` |
| sonar | `src/xscreensaver_compat.h:96:27: error: conflicting types for ‘Display’; have ‘struct _Display’` |
| timetunnel | `/tmp/xscreensaver-6.15/hacks/glx/timetunnel.c:88:10: fatal error: images/gen/logo-180_png.h: No such file or directory` |
| unknownpleasures | `../src/grab-ximage.h:70:44: error: unknown type name ‘XRectangle’` |
