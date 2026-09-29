Round 2: fix ALL compiler errors in src/wl-screenhack.c so it builds clean (Debian forky arm64, gcc, meson warning_level=2, -std=c11). You are on macOS and CANNOT build; the coordinator builds on forky and will report back. The meson.build is already fixed (protocol header names now match) — do NOT touch meson.build. Fix ONLY src/wl-screenhack.c.

EXACT errors from the forky build:
- error: storage size of 'sa'/'ign' isn't known; implicit sigemptyset/sigaddset/sigaction  -> add `#include <signal.h>` and put `#define _POSIX_C_SOURCE 200809L` as the VERY FIRST line (before every #include) so sigaction + clock_gettime are declared.
- error: implicit clock_gettime; CLOCK_MONOTONIC undeclared -> `#include <time.h>` (+ the _POSIX_C_SOURCE above).
- error: keyboard_listener.enter incompatible pointer type — the wl_keyboard `enter` handler signature must be (void *data, struct wl_keyboard *, uint32_t serial, struct wl_surface *, struct wl_array *keys). You currently declare the last param as struct wl_output *. Fix keyboard_handle_enter's signature to wl_array *.
- error: too few arguments to wl_egl_window_resize (expected 5) -> call wl_egl_window_resize(win, width, height, 0, 0) (the last two are dx, dy attach offsets).
- error: init_egl implicit then static redefinition -> add a `static void init_egl(struct app *app);` forward declaration near the top (after the struct app definition), OR move the full definition above its first call. Keep it static and USED.
- error: ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON undeclared -> this protocol version has NONE/EXCLUSIVE/ON_DEMAND only. Use ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE (a screensaver wants exclusive keyboard so a keypress dismisses it).
- error: EGL_PLATFORM_WAYLAND_KHR undeclared -> `#include <EGL/eglext.h>` (define EGL_EGLEXT_PROTOTYPES before it only if you call ext functions; the enum itself just needs the header).

After fixing, RE-READ the whole file for consistency (every wayland listener struct's function pointer signatures must match the protocol; every EGL/GL return checked). Goal: ZERO errors AND ZERO warnings under -Wall -Wextra. Report what you changed.
