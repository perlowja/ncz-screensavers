# ncz-wayland-screenhack — native Wayland/EGL GL screensaver framework

Goal: a native Wayland (wlr-layer-shell) + EGL (GLES2/3) host that renders
xscreensaver-style GL screensavers WITHOUT X11/Xwayland. Reuses the GL rendering
algorithms from xscreensaver's GPL hacks; replaces the X11/GLX windowing with a
Wayland/EGL layer-shell shell. Idle via ext-idle-notify-v1, lock coordination via
ext-session-lock-v1. Target: labwc 0.9.5 / wlroots 0.20.2, Mali-G720 (CIX Sky1),
Debian forky arm64. Destined for upstream PR (singularity / mirko) + a singularity deb.

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
