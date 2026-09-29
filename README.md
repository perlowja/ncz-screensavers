# ncz-screensavers

Native Wayland GLES3 screensavers for NCZ-OS: 84 hacks that run directly on
wlr-layer-shell and EGL, with no X11, no Xwayland and no OpenGL 1 emulation.

| set | count | source |
|---|---|---|
| Black Hole | 1 | `src/gles3_blackhole.c`, `vendor/blackhole/` (MIT, see `vendor/blackhole-PORTED.md`) |
| hyprsaver shaders | 35 | `src/gles3_hyprsaver.c`, `vendor/hyprsaver/` (MIT) |
| xshadertoy shaders | 36 | `src/gles3_xshadertoy.c`, `vendor/xshadertoy/` (per-shader licenses) |
| classics | 12 | voronoi, gravitywell, hexstrut, cubestorm, crackberg, cityflow, noof, geodesic, gibson (shader-engine ports in `src/classics_shader/`), hypertorus, klein, projectiveplane (upstream GLSL) |

Every binary is `<hack>_gles3`, installed in `/usr/lib/ncz-screensavers`, driven by
one Wayland/EGL harness (`src/gles3_harness.c`). Shaders are built once at startup,
geometry lives in retained buffers, and nothing reads pixels back inside the frame
loop. The build links `libGLESv2`, `libEGL` and the Wayland client libraries only.

## Build

    meson setup build && ninja -C build && meson test -C build

Build-time options: `-Dinstall-extras` (install the shaders of development-only
hacks), `-Dsingularity-plugin` (Singularity settings plugin), `-Dgtk4-reference-app`
(reference GTK4 front end). Package: `debian/`.

## Install matrix

| Where | Install | Settings UI |
|---|---|---|
| NCZ-OS (Singularity) | `ncz-screensavers` + `ncz-screensavers-plugin` | Settings > Plugins > Screensaver and Lockscreen |
| Another GTK/libadwaita distribution | `ncz-screensavers` + `ncz-screensavers-gtk4-reference` | the reference GTK4 application |
| Another desktop | `ncz-screensavers` | write a front end against `docs/CLI-CONTRACT.md` |

All three packages come from this source and are released together. NCZ-OS ships no
standalone settings application: the Singularity plugin is its only UI.
`docs/IMPLEMENTING-ON-ANOTHER-DISTRO.md` lists what the engine needs from a distribution.

## Run

    <hack>_gles3 --help              # options of that hack
    <hack>_gles3 --list-options
    <hack>_gles3 --style=classic|enhanced|auto --speed=1 --seed=N

The classics take `--style`: `classic` is the faithful xscreensaver look, `enhanced`
adds anti-aliasing, lighting, glow and smooth palettes, `auto` picks by GPU class.
Options are declared per hack in `assets/screensaver-chooser/options/<hack>.tsv`.
The launcher, idle timer and settings live in `launcher/` and `plugin/`.

## Documents

* `docs/CLASSICS-SHADER-PORT.md` - how the classics were ported, measurements, review record
* `docs/LAUNCHER-DESIGN.md`, `docs/RENDER-AND-GUARD.md`, `docs/BLACKHOLE-*.md`
* `docs/archive/gl1-era/` - history of the retired OpenGL 1 / gl4es path (not built)

## Licenses

GPL-2.0-or-later for the glue; each ported hack keeps its original notice (see
`debian/copyright` and `PORTED.md`).
