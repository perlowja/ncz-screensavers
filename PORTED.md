# Ported xscreensaver hacks

A hack counts as ported only if `ninja -C build` LINKS its <name>_demo
target. Compiling is not enough: missing companion sources surface only at
link time.

## Ported (19)

| hack | companion sources |
|---|---|
| beats | sphere.c |
| crumbler | quickhull.c, rotator.c, yarandom.c |
| cube21 | - |
| cubestack | rotator.c, yarandom.c |
| cubestorm | rotator.c, yarandom.c |
| cubetwist | rotator.c, yarandom.c |
| cubicgrid | rotator.c, yarandom.c |
| discoball | rotator.c, yarandom.c |
| energystream | rotator.c, yarandom.c |
| glblur | rotator.c, yarandom.c |
| glknots | rotator.c, tube.c, yarandom.c |
| hexstrut | rotator.c, yarandom.c |
| hextrail | rotator.c, yarandom.c |
| hypnowheel | rotator.c, yarandom.c |
| lockward | - |
| noof | pow2.c |
| raverhoop | rotator.c, yarandom.c |
| rubikblocks | rotator.c, yarandom.c |
| splodesic | rotator.c, yarandom.c |

## Deferred (71)

Blocked on gaps in `src/xscreensaver_compat.h`, not on porting mechanics.
Each entry is the FIRST error; closing one usually reveals the next.

> Some entries below read `rebuilding build.ninja: subcommand failed`.
> Those are NOT real per-hack failures -- the tree was transiently broken
> during that batch and every hack after it inherited the error. They need
> re-running before being believed.

| hack | blocking error |
|---|---|
| antinspect | `../src/antinspect.c:25:10: fatal error: xlock.h: No such file or directory` |
| antspotlight | `../src/antspotlight.c:25:10: fatal error: xlock.h: No such file or directory` |
| b_lockglue | `../src/b_lockglue.c:49:10: fatal error: vis.h: No such file or directory` |
| blinkbox | `../src/blinkbox.c:20:28: error: initialization of ‘Bool (*)(ModeInfo *, XEvent *)’ {aka ‘int (*)(ModeInfo *, XEvent *)’} from incompatible p` |
| blocktube | `../src/blocktube.c:22:33: error: initialization of ‘Bool (*)(ModeInfo *, XEvent *)’ {aka ‘int (*)(ModeInfo *, XEvent *)’} from incompatible ` |
| boing | `../src/boing.c:143:9: error: implicit declaration of function ‘XParseColor’; did you mean ‘parse_color’? [-Wimplicit-function-declaration]` |
| bouncingcow | `/usr/bin/aarch64-linux-gnu-ld.bfd: bouncingcow_demo.p/src_ximage-loader.c.o: undefined reference to symbol 'XGetWindowAttributes'` |
| chompytower | `../src/screenhackI.h:170:10: fatal error: xft.h: No such file or directory` |
| cityflow | `../src/cityflow.c:176:14: error: ‘XEvent’ has no member named ‘xmotion’` |
| companion | `/usr/bin/aarch64-linux-gnu-ld.bfd: companion_demo.p/src_glmatrix_harness.c.o:/home/jasonperlow/ncz-screensavers/build/../src/glmatrix_harnes` |
| covid19 | `../src/covid19.c:338:41: error: passing argument 1 of ‘get_string_resource’ from incompatible pointer type [-Wincompatible-pointer-types]` |
| crackberg | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| cubenetic | `/usr/bin/aarch64-linux-gnu-ld.bfd: cubenetic_demo.p/src_colors.c.o: undefined reference to symbol 'XFlush'` |
| dangerball | `../src/voronoi.c:21:10: fatal error: xlockmore.h: No such file or directory` |
| dnalogo | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| fliptext | `/usr/include/X11/X.h:97:13: error: conflicting types for ‘Drawable’; have ‘XID’ {aka ‘long unsigned int’}` |
| flyingtoasters | `../src/screenhackI.h:170:10: fatal error: xft.h: No such file or directory` |
| gears | `../src/screenhackI.h:170:10: fatal error: xft.h: No such file or directory` |
| geodesic | `../src/geodesic.c:726:7: error: ‘ModeInfo’ has no member named ‘recursion_depth’` |
| geodesicgears | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| gibson | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| glcells | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| glforestfire | `../src/glforestfire.c:88:10: fatal error: xlock.h: No such file or directory` |
| glhanoi | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| glmatrix | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| glschool | `../src/glschool.c:150:9: error: implicit declaration of function ‘make_color_ramp’ [-Wimplicit-function-declaration]` |
| glsnake | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| gltext | `../src/utf8wc.h:21:8: error: unknown type name ‘XChar2b’` |
| gravitywell | `../src/screenhackI.h:170:10: fatal error: xft.h: No such file or directory` |
| handsy | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| headroom | `../src/headroom.c:177:41: error: passing argument 1 of ‘get_string_resource’ from incompatible pointer type [-Wincompatible-pointer-types]` |
| highvoltage | `../src/highvoltage.c:169:41: error: passing argument 1 of ‘get_string_resource’ from incompatible pointer type [-Wincompatible-pointer-types` |
| hilbert | `../src/hilbert.c:1071:9: error: ‘ModeInfo’ has no member named ‘recursion_depth’` |
| hydrostat | `../src/hydrostat.c:597:14: error: ‘XEvent’ has no member named ‘xmotion’` |
| jigsaw | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| juggler3d | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| kaleidocycle | `../src/kaleidocycle.c:558:5: error: ‘ModeInfo’ has no member named ‘recursion_depth’` |
| kallisti | `/home/jasonperlow/ncz-screensavers/build/../src/kallisti.c:167:(.text+0x4d0): undefined reference to `kallisti_model'` |
| lament | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| lavalite | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| mapscroller | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| menger | `../src/menger.c:535:9: error: ‘ModeInfo’ has no member named ‘recursion_depth’` |
| moebiusgears | `/home/jasonperlow/ncz-screensavers/build/../src/involute.c:336:(.text+0x153c): undefined reference to `glEnable_fn'` |
| molecule | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| nakagin | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| papercube | `../src/screenhackI.h:170:10: fatal error: xft.h: No such file or directory` |
| peepers | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| photopile | `../src/photopile.c:33:11: fatal error: X11/Intrinsic.h: No such file or directory` |
| pinion | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| polyhedra-gl | `<command-line>: error: expected ‘=’, ‘,’, ‘;’, ‘asm’ or ‘__attribute__’ before ‘-’ token` |
| providence | `../src/providence.c:25:10: fatal error: xlock.h: No such file or directory` |
| quasicrystal | `../src/quasicrystal.c:125:38: error: ‘Button4’ undeclared (first use in this function); did you mean ‘Button2’?` |
| razzledazzle | `../src/razzledazzle.c:476:34: error: ‘XEvent’ has no member named ‘xmotion’` |
| sballs | `../src/sballs.c:55:10: fatal error: xlock.h: No such file or directory` |
| skulloop | `/usr/include/X11/X.h:97:13: error: conflicting types for ‘Drawable’; have ‘XID’ {aka ‘long unsigned int’}` |
| skytentacles | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| sonar | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| spheremonics | `/usr/include/X11/X.h:97:13: error: conflicting types for ‘Drawable’; have ‘XID’ {aka ‘long unsigned int’}` |
| splitflap | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| squirtorus | `../src/spline.h:39:3: error: unknown type name ‘XPoint’` |
| starwars | `../src/utf8wc.h:21:8: error: unknown type name ‘XChar2b’` |
| stonerview | `/home/jasonperlow/ncz-screensavers/build/../src/stonerview-view.c:42:(.text+0x80): undefined reference to `glEnable_fn'` |
| tangram | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| timetunnel | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| topblock | `../src/topblock.c:318:5: error: ‘ModeInfo’ has no member named ‘recursion_depth’` |
| tronbit | `/usr/bin/aarch64-linux-gnu-ld.bfd: tronbit_demo.p/src_tronbit.c.o:/home/jasonperlow/ncz-screensavers/build/../src/tronbit.c:32:(.data.rel+0x` |
| unicrud | `/usr/include/X11/X.h:97:13: error: conflicting types for ‘Drawable’; have ‘XID’ {aka ‘long unsigned int’}` |
| unknownpleasures | `../src/grab-ximage.h:70:44: error: unknown type name ‘XRectangle’` |
| vigilance | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
| voronoi | `../src/voronoi.c:401:40: error: ‘XEvent’ has no member named ‘xmotion’` |
| winduprobot | `ninja: error: rebuilding 'build.ninja': subcommand failed` |
