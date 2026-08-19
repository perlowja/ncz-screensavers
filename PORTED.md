# Ported xscreensaver hacks

A hack counts as ported only if `ninja -C build` LINKS its <name>_demo
target. Compiling is not enough: missing companion sources surface only at
link time.

## Ported (45)

| hack | companion sources |
|---|---|
| antinspect | sphere.c |
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
| energystream | rotator.c, yarandom.c |
| gears | involute.c, normals.c, rotator.c, tube.c, yarandom.c |
| geodesic | normals.c, rotator.c, yarandom.c |
| glblur | rotator.c, yarandom.c |
| glknots | rotator.c, tube.c, yarandom.c |
| glmatrix | image_data_to_ximage.c |
| glschool | glschool_alg.c, glschool_gl.c, sphere.c, tube.c, yarandom.c |
| gravitywell | - |
| headroom | gllist.c, headroom_model.c, rotator.c, skull_model.c, yarandom.c |
| hexstrut | rotator.c, yarandom.c |
| hextrail | rotator.c, yarandom.c |
| highvoltage | gllist.c, highvoltage_model.c, normals.c, tube.c |
| hydrostat | sphere.c |
| hypnowheel | rotator.c, yarandom.c |
| lockward | - |
| menger | rotator.c, yarandom.c |
| moebiusgears | involute.c, normals.c, rotator.c, yarandom.c |
| noof | pow2.c |
| papercube | rotator.c, yarandom.c |
| providence | - |
| quasicrystal | rotator.c, yarandom.c |
| raverhoop | rotator.c, yarandom.c |
| rubikblocks | rotator.c, yarandom.c |
| splodesic | rotator.c, yarandom.c |
| squirtorus | easing.c, normals.c, spline.c, yarandom.c |
| stonerview | stonerview-move.c, stonerview-osc.c, stonerview-view.c, yarandom.c |
| topblock | sphere.c, tube.c |
| voronoi | - |

## Deferred (45)

Blocked on gaps in `src/xscreensaver_compat.h`, not on porting mechanics.
Each entry is the FIRST error; closing one usually reveals the next.

> Some entries below read `rebuilding build.ninja: subcommand failed`.
> Those are NOT real per-hack failures -- the tree was transiently broken
> during that batch and every hack after it inherited the error. They need
> re-running before being believed.

| hack | blocking error |
|---|---|
| antspotlight | `../src/antspotlight.c:25:10: fatal error: xlock.h: No such file or directory` |
| b_lockglue | `../src/b_lockglue.c:49:10: fatal error: vis.h: No such file or directory` |
| bouncingcow | `/usr/bin/aarch64-linux-gnu-ld.bfd: bouncingcow_demo.p/src_ximage-loader.c.o: undefined reference to symbol 'XGetWindowAttributes'` |
| companion | `/usr/bin/aarch64-linux-gnu-ld.bfd: companion_demo.p/src_glmatrix_harness.c.o:/home/jasonperlow/ncz-screensavers/build/../src/glmatrix_harnes` |
| dangerball | `../src/voronoi.c:21:10: fatal error: xlockmore.h: No such file or directory` |
| dnalogo | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| fliptext | `/usr/include/X11/X.h:97:13: error: conflicting types for ‘Drawable’; have ‘XID’ {aka ‘long unsigned int’}` |
| flyingtoasters | `../src/screenhackI.h:170:10: fatal error: xft.h: No such file or directory` |
| geodesicgears | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| gibson | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| glcells | `/tmp/xscreensaver-6.15/hacks/glx/glcells.c:40:30: error: initialization of ‘Bool (*)(ModeInfo *, XEvent *)’ {aka ‘int (*)(ModeInfo *, XEvent *)’} from incompatible pointer type ‘int (*)(ModeInfo *, void *)’ [-Wincompatible-pointer-types]` |
| glforestfire | `../src/glforestfire.c:88:10: fatal error: xlock.h: No such file or directory` |
| glhanoi | `object compile probe succeeded; no link target added yet` |
| glsnake | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| gltext | `../src/utf8wc.h:21:8: error: unknown type name ‘XChar2b’` |
| handsy | `object compile probe succeeded; no link target added yet` |
| hilbert | `../src/hilbert.c:1071:9: error: ‘ModeInfo’ has no member named ‘recursion_depth’` |
| jigsaw | `/tmp/xscreensaver-6.15/utils/spline.h:39:3: error: unknown type name ‘XPoint’` |
| juggler3d | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| kaleidocycle | `../src/kaleidocycle.c:558:5: error: ‘ModeInfo’ has no member named ‘recursion_depth’` |
| kallisti | `/home/jasonperlow/ncz-screensavers/build/../src/kallisti.c:167:(.text+0x4d0): undefined reference to `kallisti_model'` |
| lament | `/tmp/xscreensaver-6.15/hacks/glx/lament.c:168:10: fatal error: images/gen/lament512_png.h: No such file or directory` |
| lavalite | `/tmp/xscreensaver-6.15/hacks/glx/lavalite.c:343:11: error: implicit declaration of function ‘file_to_ximage’ [-Wimplicit-function-declaration]` |
| mapscroller | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| molecule | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| nakagin | `object compile probe succeeded; no link target added yet` |
| peepers | `/tmp/xscreensaver-6.15/hacks/glx/peepers.c:46:10: fatal error: images/gen/sclera_png.h: No such file or directory` |
| photopile | `../src/photopile.c:33:11: fatal error: X11/Intrinsic.h: No such file or directory` |
| pinion | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| polyhedra-gl | `<command-line>: error: expected ‘=’, ‘,’, ‘;’, ‘asm’ or ‘__attribute__’ before ‘-’ token` |
| razzledazzle | `../src/razzledazzle.c:476:34: error: ‘XEvent’ has no member named ‘xmotion’` |
| sballs | `../src/sballs.c:55:10: fatal error: xlock.h: No such file or directory` |
| skulloop | `/usr/include/X11/X.h:97:13: error: conflicting types for ‘Drawable’; have ‘XID’ {aka ‘long unsigned int’}` |
| skytentacles | `/tmp/xscreensaver-6.15/hacks/glx/skytentacles.c:27:10: fatal error: images/gen/scales_png.h: No such file or directory` |
| sonar | `src/xscreensaver_compat.h:96:27: error: conflicting types for ‘Display’; have ‘struct _Display’` |
| spheremonics | `/usr/include/X11/X.h:97:13: error: conflicting types for ‘Drawable’; have ‘XID’ {aka ‘long unsigned int’}` |
| splitflap | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| starwars | `../src/utf8wc.h:21:8: error: unknown type name ‘XChar2b’` |
| tangram | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
| timetunnel | `/tmp/xscreensaver-6.15/hacks/glx/timetunnel.c:88:10: fatal error: images/gen/logo-180_png.h: No such file or directory` |
| tronbit | `/usr/bin/aarch64-linux-gnu-ld.bfd: tronbit_demo.p/src_tronbit.c.o:/home/jasonperlow/ncz-screensavers/build/../src/tronbit.c:32:(.data.rel+0x` |
| unicrud | `/usr/include/X11/X.h:97:13: error: conflicting types for ‘Drawable’; have ‘XID’ {aka ‘long unsigned int’}` |
| unknownpleasures | `../src/grab-ximage.h:70:44: error: unknown type name ‘XRectangle’` |
| vigilance | `object compile probe succeeded; no link target added yet` |
| winduprobot | `/usr/include/X11/X.h:102:13: error: conflicting types for ‘Pixmap’; have ‘XID’ {aka ‘long unsigned int’}` |
