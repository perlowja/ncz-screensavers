Round 4: diagnose (do not blind-guess) why glCreateShader returns 0 on real hardware. You are on macOS, cannot build; the coordinator builds on forky arm64 and runs on real Wayland hardware (labwc 0.9.5/wlroots 0.20.2, Mali-G720-Immortalis GPU via Mesa panfrost), reporting exact output back.

CONFIRMED WORKING (all succeed with NO error printed, verified via the program's own error paths which are already comprehensive):
- wl_registry binds compositor/seat/output/layer_shell (WAYLAND_DEBUG confirmed globals bound)
- wl_seat listener complete (capabilities + name handlers both present, round 3 fix)
- layer_surface gets its first configure, ack_configure sent
- wl_egl_window_create succeeds
- choose_egl_config succeeds (EGL_SURFACE_TYPE=EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE=EGL_OPENGL_ES2_BIT, RGBA8888, no depth/stencil)
- eglCreateContext succeeds (EGL_CONTEXT_CLIENT_VERSION=2)
- eglCreateWindowSurface succeeds
- eglBindAPI(EGL_OPENGL_ES_API) succeeds (coordinator added this in this round; confirmed no error)
- eglMakeCurrent succeeds (its own DIE check does not fire)

THEN FAILS: the very first GLES2 call after all of the above, `glCreateShader(GL_VERTEX_SHADER)` in `compile_shader()` (called from `triangle_init()`, called from the layer_surface configure handler right after eglMakeCurrent, same function, same thread), returns 0.

This means the "obvious" causes are ruled out (context created, current, bound to the right API, no error at any prior step). Something more subtle is wrong. DO NOT re-guess without adding visibility first.

TASK — in this exact order:
1. Immediately after the eglMakeCurrent call succeeds (right where `app->effect->init(app->effect)` is about to be called), add temporary diagnostic fprintf(stderr, ...) lines that print:
   - `eglGetError()` (should be EGL_SUCCESS=0x3000 if truly clean; print the hex)
   - `glGetString(GL_VERSION)`, `glGetString(GL_VENDOR)`, `glGetString(GL_RENDERER)`, `glGetString(GL_SHADING_LANGUAGE_VERSION)` — if these return NULL that is itself diagnostic (no context truly current at the GL level despite EGL believing it is)
   - `glGetError()` again right after the glGetString calls
   - Then call `glCreateShader(GL_VERTEX_SHADER)` directly inline (not through compile_shader) and print its return value AND `glGetError()` immediately after
2. Also print, right after eglMakeCurrent, the actual EGLContext/EGLDisplay/EGLSurface pointer values (%p) and confirm none are NULL/EGL_NO_*.
3. Do NOT remove or change any of the existing working code paths (registry, seat, layer-shell, config, context, surface, makecurrent) — only ADD diagnostics around the existing glCreateShader call site, non-destructively, so if diagnostics reveal the real cause you can also propose the actual fix in the same round IF it's now obvious from the printed values (e.g. if glGetString returns NULL room-temperature-obvious causes: wrong EGL/GL library actually linked at runttime (check with `ldd` mentally — is it linking Mesa's libGLESv2 or a stub?), a second implicit eglMakeCurrent(NULL,...) somewhere resetting the context, or GL_INVALID_OPERATION already set BEFORE glCreateShader from an earlier untracked call).
4. If the diagnostics make the fix obvious, apply it and remove the temporary prints (keep only real error-path handling). If not obvious, leave the diagnostics in (they'll go to the coordinator who runs it on hardware and reports back verbatim for round 5).

Build cleanliness: -Wall -Wextra, zero warnings, zero errors.
