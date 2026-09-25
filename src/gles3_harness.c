/* _POSIX_C_SOURCE for sigaction + clock_gettime. */
#define _POSIX_C_SOURCE 200809L

/*
 * gles3_harness.c — driver binary for a vendored xscreensaver hack
 *                   running on a Wayland/EGL/GLES3 surface through the
 *                   GLES3-native compat layer (gles3_compat.{h,c}).
 *
 * This is the GLES3 analog of glmatrix_harness.c, but stripped of
 * everything GL4ES used to do: no libGL.so.1 / no glX passthrough /
 * no GL1 emulation layer. The hack calls the ncz_* helpers in
 * gles3_compat.h directly (or, in later phases, calls GL1-style
 * stubs that route into them).
 *
 * Pipeline:
 *
 *   ┌───────────────────────────────────────────────┐
 *   │ gles3_harness.c (this file)                   │
 *   │  - Owns Wayland + EGL + frame loop            │
 *   │  - Calls init_<hack> once                     │
 *   │  - Calls draw_<hack> each frame               │
 *   │  - Calls ncz_gles3_runtime_init() at startup  │
 *   └──────────────┬────────────────────────────────┘
 *                  │
 *                  ▼
 *   ┌───────────────────────────────────────────────┐
 *   │ gles3_compat.{h,c}                            │
 *   │  - EGL context (just the surface handle)      │
 *   │  - Shader (vertex+fragment, lit, textured)    │
 *   │  - ncz_im_* immediate-mode helpers            │
 *   │  - ncz_mat_stack_* matrix stack               │
 *   │  - nczGLList_* VBO-based gllist upload        │
 *   │  - ncz_dl_* display-list recorder             │
 *   └──────────────┬────────────────────────────────┘
 *                  │
 *                  ▼
 *   ┌───────────────────────────────────────────────┐
 *   │ libGLESv2.so → Mesa panfrost / Mali blob      │
 *   │  - GLES 3.2 native                            │
 *   │  - Mali-G720 GLES 3.2 driver                  │
 *   │  - Outputs to our Wayland wl_egl_window       │
 *   └───────────────────────────────────────────────┘
 *
 * Build: linked against system libwayland-client, libwayland-egl,
 *        libEGL, libGLESv2 (NO libGL.so.1 / gl4es).
 *
 * Run:   ./boing_gles3
 *
 * Expected on real hardware: rotating "Boing" ball in a colored
 * grid cage, like the 1984 Amiga demo. Quits on any key press.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <math.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include "gles3_compat.h"
#include "xscreensaver_compat.h"

/* xscreensaver_compat.h transitively includes gl4es_include/GL/gl.h,
 * which redefines the same GL_FALSE/GL_TRUE/etc. values. Same fix as
 * in gles3_compat.c — undef the GLES3 ones so the gl4es headers win. */
#ifdef GL_FALSE
#  undef GL_FALSE
#endif
#ifdef GL_TRUE
#  undef GL_TRUE
#endif
#ifdef GL_ZERO
#  undef GL_ZERO
#endif
#ifdef GL_ONE
#  undef GL_ONE
#endif
#ifdef GL_NONE
#  undef GL_NONE
#endif
#ifdef GL_NO_ERROR
#  undef GL_NO_ERROR
#endif

#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* The vendored hack emits this via its own registration macro
 * (XSCREENSAVER_MODULE_2 or similar). For our pilots we point at
 * the GLES3-native port directly:
 *   - boing_gles3  -> gles3_boing_xscreensaver_function_table
 *   - companion_gles3 -> gles3_companioncube_xscreensaver_function_table
 *
 * The hack's table is what the harness drives via init_cb / draw_cb.
 * HACK_TABLE is set by meson per target. */
#ifndef HACK_TABLE
#define HACK_TABLE gles3boing_xscreensaver_function_table
#endif
extern struct xscreensaver_function_table HACK_TABLE;
static struct xscreensaver_function_table *hack = &HACK_TABLE;

/* ----------------------------------------------------------------------- */
/* Macros                                                                  */
/* ----------------------------------------------------------------------- */

#define DIE(call, expected, fmt, ...)                                      \
    do {                                                                   \
        long long _ret = (long long)(call);                                \
        long long _exp = (long long)(expected);                            \
        if (_ret != _exp) {                                                \
            fprintf(stderr,                                               \
                    "gles3_harness: %s failed at %s:%d: " fmt "\n",       \
                    #call, __FILE__, __LINE__, ##__VA_ARGS__);             \
            exit(1);                                                       \
        }                                                                  \
    } while (0)

/* ----------------------------------------------------------------------- */
/* Application state                                                       */
/* ----------------------------------------------------------------------- */

struct app {
    struct wl_display              *display;
    struct wl_registry             *registry;
    struct wl_compositor           *compositor;
    struct wl_seat                 *seat;
    struct wl_keyboard             *keyboard;
    struct wl_output               *output;
    struct zwlr_layer_shell_v1     *layer_shell;
    struct xdg_wm_base             *wm_base;
    struct xdg_surface             *xdg_surface;
    struct xdg_toplevel            *xdg_toplevel;
    int                             pending_w, pending_h;
    struct wl_surface              *surface;
    struct zwlr_layer_surface_v1   *layer_surface;

    struct wl_egl_window *egl_window;
    EGLDisplay  egl_display;
    EGLConfig   egl_config;
    EGLSurface  egl_surface;
    EGLContext  egl_context;

    int width, height;
    struct wl_callback *frame_cb;
    bool frame_in_flight;
    bool configured;

    volatile sig_atomic_t running;

    /* The ModeInfo we own — passed to init_<hack> / draw_<hack> / etc. */
    ModeInfo mi;
};

static struct app *g_app = NULL;

static void on_signal(int sig) {
    (void)sig;
    if (g_app) g_app->running = 0;
}

/* ----------------------------------------------------------------------- */
/* Wayland registry / seat / keyboard                                      */
/* ----------------------------------------------------------------------- */

static void wm_base_ping(void *d, struct xdg_wm_base *b, uint32_t serial) {
    (void)d;
    xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void reg_global(void *d, struct wl_registry *r, uint32_t n,
                       const char *iface, uint32_t v) {
    (void)v;
    struct app *a = d;
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        a->compositor = wl_registry_bind(r, n, &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_seat_interface.name) == 0)
        a->seat = wl_registry_bind(r, n, &wl_seat_interface, 7);
    else if (strcmp(iface, wl_output_interface.name) == 0 && !a->output)
        a->output = wl_registry_bind(r, n, &wl_output_interface, 4);
    else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        if (!getenv("NCZ_NO_LAYER_SHELL"))
            a->layer_shell = wl_registry_bind(r, n,
                                              &zwlr_layer_shell_v1_interface, 4);
    }
    else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        a->wm_base = wl_registry_bind(r, n, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(a->wm_base, &wm_base_listener, a);
    }
}

static void reg_global_remove(void *d, struct wl_registry *r, uint32_t n) {
    (void)d; (void)r; (void)n;
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_global_remove,
};

static void keyh_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t,
                     uint32_t key, uint32_t state) {
    (void)k; (void)s; (void)t; (void)key;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        struct app *a = d;
        a->running = 0;
    }
}
static void keyh_enter(void *d, struct wl_keyboard *k, uint32_t s,
                       struct wl_surface *sf, struct wl_array *keys) {
    (void)d; (void)k; (void)s; (void)sf; (void)keys;
}
static void keyh_leave(void *d, struct wl_keyboard *k, uint32_t s,
                       struct wl_surface *sf) {
    (void)d; (void)k; (void)s; (void)sf;
}
static void keyh_keymap(void *d, struct wl_keyboard *k, uint32_t f, int fd,
                        uint32_t sz) {
    (void)d; (void)k; (void)f; (void)fd; (void)sz;
}
static void keyh_modifiers(void *d, struct wl_keyboard *k, uint32_t s,
                           uint32_t md, uint32_t ml, uint32_t lo,
                           uint32_t g) {
    (void)d; (void)k; (void)s; (void)md; (void)ml; (void)lo; (void)g;
}
static void keyh_repeat(void *d, struct wl_keyboard *k, int rate, int delay) {
    (void)d; (void)k; (void)rate; (void)delay;
}
static const struct wl_keyboard_listener kbd_listener = {
    .key = keyh_key, .enter = keyh_enter, .leave = keyh_leave,
    .keymap = keyh_keymap, .modifiers = keyh_modifiers,
    .repeat_info = keyh_repeat,
};

static void seat_caps(void *d, struct wl_seat *s, enum wl_seat_capability c) {
    (void)s;
    struct app *a = d;
    if ((c & WL_SEAT_CAPABILITY_KEYBOARD) && !a->keyboard) {
        a->keyboard = wl_seat_get_keyboard(a->seat);
        if (a->keyboard)
            wl_keyboard_add_listener(a->keyboard, &kbd_listener, a);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {
    (void)d; (void)s; (void)n;
}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps, .name = seat_name,
};

/* ----------------------------------------------------------------------- */
/* Layer surface                                                           */
/* ----------------------------------------------------------------------- */

static void surface_configured(struct app *a, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;

    if (!a->configured) {
        a->width = (int)w;
        a->height = (int)h;
        a->egl_window = wl_egl_window_create(a->surface, a->width, a->height);
        if (!a->egl_window) {
            fprintf(stderr, "gles3_harness: wl_egl_window_create failed\n");
            exit(1);
        }
        a->egl_surface = eglCreateWindowSurface(a->egl_display, a->egl_config,
                                                (EGLNativeWindowType)a->egl_window,
                                                NULL);
        if (a->egl_surface == EGL_NO_SURFACE) {
            fprintf(stderr, "gles3_harness: eglCreateWindowSurface failed (0x%x)\n",
                    (unsigned int)eglGetError());
            exit(1);
        }
        DIE(eglMakeCurrent(a->egl_display, a->egl_surface, a->egl_surface,
                           a->egl_context), EGL_TRUE, "eglMakeCurrent failed");

        /* Explicitly request vblank-paced eglSwapBuffers. Measured
         * 2026-09-22 on all three platforms (O6N/Mali, MEDUSA/radeonsi,
         * PEGASUS/iris): with or without this call, boing_gles3 produced
         * exactly 120 frames in 3 seconds (~40 fps), so the Mesa/CIX
         * Wayland winsys appears to already pace via its own internal
         * wl_surface.frame. But making the request explicit is the
         * correct shape for an EGL application and protects against
         * future winsys changes or vendors that don't self-pace. */
        eglSwapInterval(a->egl_display, 1);

        /* Set up the opaque region: a transparent surface blends
         * against the desktop, which is invisible. */
        struct wl_region *opaque = wl_compositor_create_region(a->compositor);
        if (opaque) {
            wl_region_add(opaque, 0, 0, a->width, a->height);
            wl_surface_set_opaque_region(a->surface, opaque);
            wl_region_destroy(opaque);
        }

        fprintf(stderr, "[diag] gles3_harness: calling init...\n");
        /* Set the gl4es-shim globals so init_GL() (still called for
         * shape compatibility) records the configured size into
         * mi->xgwa.width/height. Without this, the hack's reshape_*()
         * runs with width=height=0 and computes NaN aspect ratios. */
        g_harness_width = a->width;
        g_harness_height = a->height;
        g_harness_initialized = 1;

        /* Apply the hack's declared defaults BEFORE init.
         *
         * The hack never assigns its tunables itself -- upstream
         * xscreensaver writes them through the ModeSpecVar table during
         * option parsing. We don't parse options, but we must still do
         * that write, or every tunable stays at its BSS default -- which
         * makes parse_color receive NULL color strings and exit.
         *
         * init_GL records the harness's width/height into the ModeInfo
         * so the hack's reshape_*() receives non-zero dimensions. The
         * gl4es path's init_GL is a thin shim that just sets those
         * fields; we call it for the same effect. */
        xs_compat_apply_var_defaults(hack->opts, hack->defaults_str);
        xs_compat_apply_mode_defaults(&a->mi);
        init_GL(&a->mi);
        hack->init_cb(&a->mi);
        fprintf(stderr, "[diag] gles3_harness: init returned\n");
        a->configured = true;
    } else if (w != (uint32_t)a->width || h != (uint32_t)a->height) {
        wl_egl_window_resize(a->egl_window, (int)w, (int)h, 0, 0);
        a->width = (int)w;
        a->height = (int)h;
        if (hack->reshape_cb) {
            hack->reshape_cb(&a->mi, a->width, a->height);
        }
    }
}

static void ls_configure(void *d, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h) {
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    surface_configured((struct app *)d, w, h);
}

static void ls_closed(void *d, struct zwlr_layer_surface_v1 *ls) {
    (void)ls; struct app *a = d; a->running = 0;
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {
    .configure = ls_configure, .closed = ls_closed,
};

/* ----------------------------------------------------------------------- */
/* xdg-shell fallback                                                      */
/* ----------------------------------------------------------------------- */

static void xdg_surf_configure(void *d, struct xdg_surface *xs,
                               uint32_t serial) {
    struct app *a = d;
    xdg_surface_ack_configure(xs, serial);
    surface_configured(a, a->pending_w ? (uint32_t)a->pending_w : 1920,
                          a->pending_h ? (uint32_t)a->pending_h : 1080);
}
static const struct xdg_surface_listener xdg_surf_listener = {
    .configure = xdg_surf_configure,
};

static void xdg_top_configure(void *d, struct xdg_toplevel *t,
                              int32_t w, int32_t h, struct wl_array *states) {
    (void)t; (void)states;
    struct app *a = d;
    if (w > 0 && h > 0) { a->pending_w = w; a->pending_h = h; }
}
static void xdg_top_close(void *d, struct xdg_toplevel *t) {
    (void)t; ((struct app *)d)->running = 0;
}
static const struct xdg_toplevel_listener xdg_top_listener = {
    .configure = xdg_top_configure, .close = xdg_top_close,
};

/* ----------------------------------------------------------------------- */
/* Frame callback                                                          */
/* ----------------------------------------------------------------------- */

/*
 * Root cause of the AMD64/Mesa (radeonsi, Intel iGPU) animation stall,
 * measured 2026-09-22 via WAYLAND_DEBUG=1 on MEDUSA: the old code here
 * called wl_surface_frame() itself AND used eglSwapBuffers() through
 * Mesa's EGL-Wayland platform, which ALSO issues its own internal
 * wl_surface.frame request as part of normal buffer presentation. Two
 * frame-callback requests landed on the same surface a few microseconds
 * apart (mesa egl surface queue's vs this file's own Default Queue one);
 * a surface only gets ONE frame callback fulfilled per commit, so this
 * file's own explicit callback never received its .done event and the
 * loop below never woke up again after the first frame. Panthor/Mesa on
 * O6N happened not to hit this (different winsys timing), which is why
 * it worked there and nowhere else.
 *
 * Fix: don't manage a manual Wayland frame callback on an EGL-owned
 * window surface at all. EGL's own Wayland winsys already paces
 * presentation inside eglSwapBuffers; drive the loop by dispatching
 * pending Wayland events (non-blocking) and drawing+swapping every
 * iteration, the same shape as any other GL-on-EGL app.
 */
static void draw_and_swap(struct app *a) {
    hack->draw_cb(&a->mi);
    if (!eglSwapBuffers(a->egl_display, a->egl_surface)) {
        fprintf(stderr, "gles3_harness: eglSwapBuffers failed (0x%x)\n",
                (unsigned int)eglGetError());
        a->running = 0;
    }
}

/* ----------------------------------------------------------------------- */
/* EGL setup                                                               */
/* ----------------------------------------------------------------------- */

static void init_egl(struct app *a) {
    a->egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR,
                                          a->display, NULL);
    if (a->egl_display == EGL_NO_DISPLAY) {
        a->egl_display = eglGetDisplay((EGLNativeDisplayType)a->display);
        if (a->egl_display == EGL_NO_DISPLAY) {
            fprintf(stderr, "gles3_harness: eglGetPlatformDisplay failed\n");
            exit(1);
        }
    }
    EGLint maj = 0, minn = 0;
    if (!eglInitialize(a->egl_display, &maj, &minn)) {
        fprintf(stderr, "gles3_harness: eglInitialize failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "gles3_harness: eglBindAPI failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
    a->egl_config = ncz_gles3_choose_config(a->egl_display);
    if (!a->egl_config) {
        fprintf(stderr, "gles3_harness: no GLES3 EGL configs\n");
        exit(1);
    }
    /* GLES 3.2 context — same client version as the runtime requires. */
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    a->egl_context = eglCreateContext(a->egl_display, a->egl_config,
                                      EGL_NO_CONTEXT, ctx_attr);
    if (a->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "gles3_harness: eglCreateContext failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
    /* Probe the GL strings once the context is live (needs MakeCurrent,
     * which we do later when the surface arrives). For now log the
     * EGL version. */
    fprintf(stderr, "[diag] gles3_harness: EGL %d.%d, GLES3 context live\n",
            maj, minn);
}

/* ----------------------------------------------------------------------- */
/* Shutdown                                                                */
/* ----------------------------------------------------------------------- */

static void app_fini(struct app *a) {
    if (a->frame_cb) { wl_callback_destroy(a->frame_cb); a->frame_cb = NULL; }

    if (a->configured && hack->free_cb) {
        hack->free_cb(&a->mi);
    }

    ncz_gles3_runtime_fini();

    if (a->egl_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(a->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        eglDestroyContext(a->egl_display, a->egl_context);
        a->egl_context = EGL_NO_CONTEXT;
    }
    if (a->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(a->egl_display, a->egl_surface);
        a->egl_surface = EGL_NO_SURFACE;
    }
    if (a->egl_window) {
        wl_egl_window_destroy(a->egl_window);
        a->egl_window = NULL;
    }
    if (a->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(a->egl_display);
        a->egl_display = EGL_NO_DISPLAY;
    }
    if (a->keyboard)  { wl_keyboard_release(a->keyboard);  a->keyboard = NULL; }
    if (a->layer_surface) { zwlr_layer_surface_v1_destroy(a->layer_surface); a->layer_surface = NULL; }
    if (a->surface) { wl_surface_destroy(a->surface); a->surface = NULL; }
    if (a->xdg_toplevel) { xdg_toplevel_destroy(a->xdg_toplevel); a->xdg_toplevel = NULL; }
    if (a->xdg_surface)  { xdg_surface_destroy(a->xdg_surface);  a->xdg_surface  = NULL; }
    if (a->wm_base)      { xdg_wm_base_destroy(a->wm_base);      a->wm_base      = NULL; }
    if (a->layer_shell) { zwlr_layer_shell_v1_destroy(a->layer_shell); a->layer_shell = NULL; }
    if (a->seat) { wl_seat_release(a->seat); a->seat = NULL; }
    if (a->output) { wl_output_release(a->output); a->output = NULL; }
    if (a->compositor) { wl_compositor_destroy(a->compositor); a->compositor = NULL; }
    if (a->registry) { wl_registry_destroy(a->registry); a->registry = NULL; }
    if (a->display) { wl_display_disconnect(a->display); a->display = NULL; }
}

static void atexit_app_fini(void) {
    if (g_app) { app_fini(g_app); g_app = NULL; }
}

/* ----------------------------------------------------------------------- */
/* main                                                                    */
/* ----------------------------------------------------------------------- */

int main(void) {
    static struct app app;
    memset(&app, 0, sizeof app);
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_surface = EGL_NO_SURFACE;
    app.egl_context = EGL_NO_CONTEXT;
    app.running = 1;

    g_app = &app;
    if (atexit(atexit_app_fini) != 0) {
        fprintf(stderr, "gles3_harness: atexit failed\n");
        exit(1);
    }

    /* Signal handlers. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTERM);
    DIE(sigaction(SIGINT,  &sa, NULL), 0, "sigaction(SIGINT) failed");
    DIE(sigaction(SIGTERM, &sa, NULL), 0, "sigaction(SIGTERM) failed");
    struct sigaction ign;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    DIE(sigaction(SIGPIPE, &ign, NULL), 0, "sigaction(SIGPIPE) failed");

    /* Wayland connect + bind. */
    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "gles3_harness: wl_display_connect failed\n");
        exit(1);
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &reg_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) exit(1);
    if (!app.compositor || !app.seat || (!app.layer_shell && !app.wm_base)) {
        fprintf(stderr,
            "gles3_harness: missing Wayland globals "
            "(compositor=%d seat=%d layer_shell=%d xdg_wm_base=%d)\n",
            !!app.compositor, !!app.seat, !!app.layer_shell, !!app.wm_base);
        exit(1);
    }
    wl_seat_add_listener(app.seat, &seat_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) exit(1);

    /* EGL. */
    init_egl(&app);

    /* The GLES3 runtime (shader + scratch VBO/VAO) needs a current
     * context. The hack's init_cb will set up state, but the runtime
     * itself must be live before any ncz_* helper runs. Bind a
     * placeholder current context — we don't have a surface yet, but
     * the EGL spec lets us bind the context with EGL_NO_SURFACE for
     * client-side compilation. */
    DIE(eglMakeCurrent(app.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       app.egl_context), EGL_TRUE,
        "eglMakeCurrent (no surface) failed");
    if (ncz_gles3_runtime_init() < 0) {
        fprintf(stderr, "gles3_harness: GLES3 runtime init failed\n");
        exit(1);
    }
    /* Log GL strings now. */
    fprintf(stderr, "[diag] GL_VERSION=%s\nRENDERER=%s\nVENDOR=%s\nGLSL=%s\n",
            (const char*)glGetString(GL_VERSION),
            (const char*)glGetString(GL_RENDERER),
            (const char*)glGetString(GL_VENDOR),
            (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
    /* Detach for the surface-config phase to rebind to the layer
     * surface. */
    eglMakeCurrent(app.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);

    /* Layer surface. */
    app.surface = wl_compositor_create_surface(app.compositor);
    if (!app.surface) {
        fprintf(stderr, "gles3_harness: create_surface failed\n");
        exit(1);
    }
    if (!app.layer_shell) {
        fprintf(stderr, "[diag] no zwlr_layer_shell_v1; "
                        "falling back to a fullscreen xdg_toplevel\n");
        app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
        if (!app.xdg_surface) {
            fprintf(stderr, "gles3_harness: get_xdg_surface failed\n");
            exit(1);
        }
        xdg_surface_add_listener(app.xdg_surface, &xdg_surf_listener, &app);
        app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
        if (!app.xdg_toplevel) {
            fprintf(stderr, "gles3_harness: get_toplevel failed\n");
            exit(1);
        }
        xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_top_listener, &app);
        xdg_toplevel_set_title(app.xdg_toplevel, "ncz-gles3");
        xdg_toplevel_set_app_id(app.xdg_toplevel, "org.nclawzero.screensaver");
        /* NULL, not app.output: reg_global binds whichever wl_output the
         * registry happens to advertise first, which on a multi-connector
         * board (O6N: DP-1/DP-2/DP-3 + 6 writeback pseudo-outputs) is not
         * necessarily the one actually connected to a monitor. Passing NULL
         * tells the compositor to pick a real output itself, per the
         * xdg_toplevel.set_fullscreen protocol spec. Fixed 2026-09-22 after
         * a live O6N test rendered 4500+ correct frames with zero errors
         * while showing nothing on the actual (DP-2) display -- it was
         * fullscreening onto a disconnected output the whole time. */
        xdg_toplevel_set_fullscreen(app.xdg_toplevel, NULL);
        wl_surface_commit(app.surface);
        for (int i = 0; i < 50 && !app.configured; i++) {
            if (wl_display_roundtrip(app.display) < 0) exit(1);
        }
        goto shell_ready;
    }

    /* NULL output -- see the comment on the xdg_toplevel_set_fullscreen
     * fallback path above; same reasoning applies to the layer-shell path. */
    app.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell, app.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "gles3-hack");
    if (!app.layer_surface) {
        fprintf(stderr, "gles3_harness: get_layer_surface failed\n");
        exit(1);
    }
    zwlr_layer_surface_v1_set_anchor(app.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(app.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        app.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(app.layer_surface, &ls_listener, &app);
    wl_surface_commit(app.surface);
    /* A single roundtrip is not always enough: some compositor/renderer
     * combinations (observed on nvidia's proprietary EGL path) take more
     * than one dispatch cycle to emit zwlr_layer_surface_v1.configure.
     * Keep round-tripping, bounded, rather than assuming one is enough. */
    for (int i = 0; i < 50 && !app.configured; i++) {
        if (wl_display_roundtrip(app.display) < 0) exit(1);
    }

shell_ready:
    if (!app.configured) {
        fprintf(stderr, "gles3_harness: never configured\n");
        exit(1);
    }

    /* Draw + swap once BEFORE asking for a frame callback. See the
     * long comment in glmatrix_harness.c — the same applies here. */
    fprintf(stderr, "[diag] initial draw: %dx%d configured=%d\n",
            app.width, app.height, (int)app.configured);
    hack->draw_cb(&app.mi);
    fprintf(stderr, "[diag] initial draw_cb returned; swapping\n");
    if (!eglSwapBuffers(app.egl_display, app.egl_surface)) {
        fprintf(stderr, "gles3_harness: initial eglSwapBuffers failed (0x%x)\n",
                (unsigned int)eglGetError());
        return 1;
    }
    {
        static unsigned long _nframes = 0;
        while (app.running) {
            if (wl_display_dispatch_pending(app.display) < 0) {
                app.running = 0;
                break;
            }
            if (wl_display_flush(app.display) < 0 && errno != EAGAIN) {
                app.running = 0;
                break;
            }
            if (_nframes < 5 || (_nframes % 60) == 0)
                fprintf(stderr, "[diag] frame #%lu\n", _nframes);
            _nframes++;
            draw_and_swap(&app);
        }
    }
    if (app.display) wl_display_roundtrip(app.display);

    /* Run cleanup explicitly here, synchronously, before main() returns
     * and exit() atexit chain runs. Previously this relied solely on
     * atexit(atexit_app_fini) -- but atexit handlers run LIFO, and the
     * NVIDIA GLES/EGL driver registers its own atexit/destructor
     * teardown during context creation, which lands AFTER ours in
     * registration order and therefore runs BEFORE ours at exit time.
     * That tore down GL dispatch trampolines out from under our
     * glDelete* calls -- confirmed via gdb: SIGSEGV jumping to an
     * unmapped address (Cannot access memory) inside
     * ncz_gles3_runtime_fini(), reproduced identically on NVIDIA RTX
     * 2060 and RTX 4500 Ada, never on Mesa/Mali. Calling app_fini()
     * here runs our GL teardown before any other library atexit
     * chain begins; atexit_app_fini g_app-NULL guard makes the
     * still-registered atexit call a safe no-op afterward. */
    app_fini(&app);
    g_app = NULL;
    return 0;
}
