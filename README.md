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
