# rss-sdl2-gles2 — [Really Slick Screensavers](https://erik-larsen.github.io/rss-sdl2-gles2/) on SDL2 / OpenGL ES2

![screenshot](media/screenshot.png)

A port of [Terence M. Welsh's Really Slick Screensavers](https://web.archive.org/web/20260417100255/http://reallyslick.com/screensavers/)
([upstream repo](https://github.com/reallyslickscreensavers/reallyslickscreensavers))
to SDL2 + OpenGL ES2, with native builds for Windows, Linux, and macOS and a
browser build via Emscripten.

## How it works

```
original saver code (OpenGL 1.x, immediate mode, display lists, GLU)
        │  compiled UNMODIFIED with -DRS_XSCREENSAVER
        ▼
libs/librs       SDL2 shell impersonating the rsXScreenSaver framework
        ▼
libs/gl4es       OpenGL 1.x/2.x  →  OpenGL ES2 translation
libs/glues       GLU (gluPerspective, mipmaps, quadrics) on ES
        ▼
libs/libgles     ANGLE libGLESv2/libEGL (native)  ·  WebGL via emscripten (browser)
```

The original savers' `#ifdef RS_XSCREENSAVER` code paths compile against a
compatibility header in `libs/librs/src/rsXScreenSaver/`; even their
`glXSwapBuffers(xdisplay, xwindow)` calls are routed to `SDL_GL_SwapWindow`
by macro. Saver sources stay byte-identical to upstream wherever possible.

## Building

Prerequisites: `make`, a C/C++ toolchain, `cmake` (for gl4es), SDL2 dev
package (`libsdl2-dev` / `brew install sdl2` / MSYS2 `mingw-w64-x86_64-SDL2`),
and for browser builds the [emsdk](https://emscripten.org).

```sh
make            # build all libs + all savers (native)
make native     # savers only, native
make browser    # savers only, emscripten (also assembles web/ gallery)
./scripts/deploy-web.sh   # (re)assemble web/ gallery from built savers
python3 scripts/serve_web.py   # serve web/ locally at http://localhost:8000
make -C savers/flux run-native     # build & run one saver windowed
```

Run options for every saver:

```
--window WxH | -w WxH   set the window size (default 1024x640, windowed)
--fullscreen | -f       run fullscreen (default windowed)
--saver                 screensaver mode: fullscreen, exit on any key/click
--fps-limit N           cap the frame rate (default: display refresh; 0 = uncapped)
```

plus each saver's original options (e.g. flux: `-fluxes`, `-particles`,
`-trail`, `-geometry`, `-complexity`, ... and `-default 1..6` presets).

## Status

| Component | State |
|---|---|
| Build system (sgi-demos style) | done |
| libs: gl4es, glues, rsmath (rslibs subset), librs (SDL2 shell) | done, building |
| testsaver (stack verification) | done, verified rendering |
| flux, solarwinds, flocks, plasma | ported (unmodified source), verified rendering |
| cyclone, fieldlines, euphoria | ported (minimal dual-platform diffs), verified rendering |
| helios, lattice | ported (texgen + GLU mipmaps + Implicit surfaces), verified rendering |
| hyperspace, microcosm | ported (GLSL via gl4es shaderconv), verified rendering |
| skyrocket | ported (fireworks; OpenAL audio, muted-capable), verified rendering |
| implicitDemo | ported (mini-GLUT-on-SDL shim), verified rendering |
| Emscripten browser builds (all 14 targets) | done, verified rendering in-browser |
| Web gallery + custom fullscreen shell | done |
| GitHub Actions CI (native matrix + web + Pages) | done |

## Licenses

Savers: GPL-2.0 (upstream). rslibs-derived code and librs: LGPL-2.1.
gl4es: MIT-style (see libs/gl4es/LICENSE). glues: SGI Free Software License B.
ANGLE: BSD-style.
