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
