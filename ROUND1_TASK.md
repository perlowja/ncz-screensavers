Implement the FOUNDATION of a native Wayland/EGL GL screensaver in C, in this repo (~/ncz-wayland-screenhack). This is round 1: the layer-shell + EGL shell + one test effect. NO X11/GLX/Xlib/Xwayland anywhere — pure Wayland + EGL + GLES2.

CREATE src/wl-screenhack.c — one self-contained C11 program that:
1. Connects to the Wayland display; binds wl_compositor, wl_output, wl_seat, and zwlr_layer_shell_v1 (wlr-layer-shell protocol) from the registry.
2. Creates a wl_surface + zwlr_layer_surface_v1 on layer ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, anchored to all four edges, exclusive_zone=-1, keyboard_interactivity=ON. Commit; wait for the first configure; ack_configure; use its width/height.
3. wl_egl_window_create(surface,w,h); EGLDisplay via eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wl_display, NULL); eglInitialize; choose an EGLConfig (EGL_RENDERABLE_TYPE=EGL_OPENGL_ES2_BIT, RGBA8, double-buffered); eglCreateWindowSurface on the wl_egl_window; GLES2 context (EGL_CONTEXT_CLIENT_VERSION=2); eglMakeCurrent.
4. Render loop via wl_surface frame callbacks: each frame glViewport + clear + draw a smoothly rotating, color-cycling equilateral triangle with a GLES2 vertex+fragment shader (rotation angle from a monotonic clock, wrap-safe); eglSwapBuffers; re-request a frame callback.
5. Handle layer_surface configure (ack + wl_egl_window_resize) and closed (exit); exit cleanly on SIGINT/SIGTERM and on any wl_keyboard key press.
6. Check EVERY wayland/EGL/GL call; on failure print a clear message to stderr and exit(1). No leaks on the exit path.

CREATE meson.build:
- project('ncz-wayland-screenhack','c', default_options:['c_std=c11','warning_level=2'])
- dependency() via pkg-config: wayland-client, wayland-egl, egl, glesv2
- wayland-scanner (find_program('wayland-scanner')) + custom_target to generate client-header AND private-code for protocols/wlr-layer-shell-unstable-v1.xml and protocols/xdg-shell.xml (include protocols/wayland.xml if needed for core). Compile the generated .c into the executable.
- executable('wl-screenhack', sources + generated, dependencies: the four above), -Wall -Wextra clean.

Structure the code so an effect API (init/draw/reshape/free) can be factored out later — this shell will host ported xscreensaver GL hacks in future rounds. Comment clearly.

VERIFY before finishing: run `meson setup build && ninja -C build` in the repo; confirm it produces build/wl-screenhack with ZERO errors and ZERO warnings. Paste the exact ninja output. If it does not build clean, fix and re-run until it does.
