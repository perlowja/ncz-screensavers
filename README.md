# ncz-screensavers — native Wayland-neutral EGL/GLES2 screensaver framework

Goal: a native Wayland (wlr-layer-shell) + EGL (GLES2/3) host that renders
xscreensaver-style GL screensavers WITHOUT X11/Xwayland. Reuses the GL rendering
algorithms from xscreensaver's hacks; replaces the X11/GLX windowing with a
Wayland/EGL layer-shell shell. Idle via ext-idle-notify-v1, lock coordination via
ext-session-lock-v1.

## Attribution

The visual-effect algorithms this project ports (e.g. `src/glmatrix.c`) are
the work of **Jamie Zawinski (jwz)**, author and maintainer of
[xscreensaver](https://www.jwz.org/xscreensaver/) since 1992 — one of the
longest-running, most widely-ported free software projects in existence.
Every vendored hack keeps jwz's original copyright header and permission
notice unmodified (xscreensaver hacks are released under a permissive
MIT-style X Consortium notice, not the GPL); see `PORTING.md` §4 for the
exact, minimal set of edits made to port each file and the file-level headers
in `src/` for per-file provenance.

This project is an independent, unofficial Wayland-native reimplementation of
the *windowing and dispatch layer* xscreensaver hacks run under — it does not
use, wrap, or depend on xscreensaver, Xlib, or Xwayland, and it is not
affiliated with or endorsed by jwz. jwz has publicly and consistently stated
he will not support Wayland in xscreensaver itself; that position is his to
hold, and is unrelated to the credit owed him for the hacks' original
authorship, which this project maintains in full.

DESKTOP-ENVIRONMENT NEUTRAL BY DESIGN: depends only on standard wlroots
protocols (wlr-layer-shell-unstable-v1, ext-session-lock-v1, ext-idle-notify-v1,
xdg-shell) -- nothing Singularity-specific, nothing labwc-specific. Runs on any
compositor implementing those protocols (labwc, sway, hyprland, wayfire, etc.).
Developed and hardware-verified on labwc 0.9.5 / wlroots 0.20.2, Mali-G720
(CIX Sky1), Debian forky arm64 -- that is the test target, not a dependency.
Ships as a singularity deb for NCZ-OS; the binary itself has no Singularity
dependency and is intended for upstream PR to any interested compositor/DE
project, not exclusively singularity/mirko.

protocols/  wlr-layer-shell-unstable-v1.xml, ext-idle-notify-v1.xml,
            ext-session-lock-v1.xml, xdg-shell.xml, wayland.xml
src/        the framework + effects

## Design decision (2026-08-17, agreed with Mirko Brombin / singularityos-lab)

The screensaver renders on ONE OF TWO surface types, selected at runtime by
user config (screensaver lock-protection setting):

1. **Lock-protected**: `ext-session-lock-v1` surface — the SAME surface class
   the native Singularity lockscreen (`ncz-lock`) uses. Compositor-enforced,
   input cannot pass through to the desktop underneath.
2. **Unlocked**: standard `wlr-layer-shell` overlay surface (the current
   round-1..4 implementation path) — dismissible by any keypress.

We are NOT wrapping or porting xscreensaver / Xwayland / the xscreensaver
daemon. This is a native Wayland/EGL/GLES rendering engine; only the
visual-effect *mechanism* (the GL shader approach) is inspired by xscreensaver's
hacks, not its codebase or X11 windowing.

The effect core (shader compile, draw loop, EGL context/surface) must stay
surface-agnostic so it can attach to either a `zwlr_layer_surface_v1` or an
`ext_session_lock_surface_v1` — only the surface-acquisition code branches on
the lock-enabled setting. `ext-session-lock-v1` protocol already in `protocols/`.

## The lockscreen IS the screensaver (2026-08-17, hardware-confirmed)

The dual-surface design above is not a preference. On Wayland it is the only
arrangement that can work, and that was confirmed the hard way on O6N.

**`ext-session-lock-v1` is exclusive by protocol.** While a session is locked
the compositor presents the lock surface and nothing else. A `wlr-layer-shell`
screensaver is therefore INVISIBLE while locked no matter which layer it
requests — `OVERLAY` included. Measured repeatedly on O6N 2026-08-17: every
screenshot taken during a lock showed the lockscreen (clock + password card)
while `glmatrix_demo` was demonstrably alive, rendering frames and holding a
live Mali GPU context underneath it. "Run the screensaver, let the locker cover
it" is not a bug to fix; it is the protocol working as designed.

So a screensaver that must survive locking has to BE the lock surface. There
is no composition path where a separate screensaver process draws beneath a
separate locker process.

### Shape of the implementation

`singularity-lockscreen` already owns the `ext-session-lock-v1` surface and
already draws wallpaper + clock + password card, and already carries the
PAM/auth path. The minimal, lowest-risk change is to replace only its STATIC
WALLPAPER LAYER with a GL-rendered animated one, leaving the widget tree and
every line of authentication code untouched. That yields the animated GL
lockscreen with the password prompt composited on top, without editing
security-critical code.

### Non-negotiable: keep the hack out of the authenticator

A ported hack is third-party C running legacy fixed-function GL through a
translation shim. If it segfaults inside the process that owns the lock
surface, the locker dies — and depending on how the compositor treats a
vanishing lock client, that can expose the desktop. This is a real exposure
risk, not a theoretical one, and it is the single thing that must not be
handwaved.

Wayland subsurfaces must come from the same `wl_client`, which constrains the
options to:

1. **Separate renderer process, compositor-composed** — the hack renders into
   its own lock-owned surface, the prompt is a separate surface above it.
   Strongest isolation; requires the lock client to coordinate two surfaces.
2. **In-process with a hard fail-closed watchdog** — simplest to build. On
   hack crash OR hang, fall back instantly to the current static wallpaper and
   keep the prompt alive. The locker must be written so a dead renderer can
   never leave the session unlocked, and never leave a blank screen with no
   way to authenticate.

Ship (2) with a strict fail-closed watchdog; treat (1) as the target.

### Prerequisite before building any of this

`glmatrix_demo` currently renders OPAQUE BLACK. The surface, the frame loop and
the compositing are all correct as of `f77a79f` — the overlay genuinely covers
the desktop now — but the glyph atlas does not appear. Wiring a GL lockscreen
around a renderer that draws nothing would just produce a black lockscreen, so
the atlas bug is the gate on starting this work.

What is already ruled out: the compiled-in asset is fine
(`src/images/gen/matrix3_png.h`, 368 KB, valid PNG signature, IHDR 512x598),
and GL4ES initialises cleanly with no GL errors reported. The remaining
suspects are in the decode/upload path — `image_data_to_ximage` (libpng) ->
`XGetPixel`/`XPutPixel` -> `glTexImage2D` through GL4ES.

### Runtime requirement for every port

GL4ES must be INSTALLED ON THE TARGET (`apt install libgl4es0`). The binaries
carry `RUNPATH=/usr/lib/aarch64-linux-gnu/gl4es/`; when that directory is
absent the loader silently resolves `libGL.so.1` to the CIX libglvnd/GLX stack
instead, where legacy fixed-function calls land with no current GLX context and
become no-ops — no error, no crash, no pixels. Measured on O6N 2026-08-17.
