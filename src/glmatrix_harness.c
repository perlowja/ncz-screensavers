/* _POSIX_C_SOURCE MUST come before ANY system header so glibc exposes
 * sigaction + clock_gettime at compile time. */
#define _POSIX_C_SOURCE 200809L

/*
 * glmatrix_harness.c — driver binary that hosts the vendored
 * xscreensaver `glmatrix.c` on a Wayland/EGL/GLES2 surface via
 * GL4ES + the xscreensaver_compat shim.
 *
 * Pipeline:
 *
 *   ┌────────────────────────────────────────┐
 *   │ glmatrix_harness.c (this file)         │
 *   │  - Owns Wayland + EGL + frame loop     │
 *   │  - Calls init_matrix once              │
 *   │  - Calls draw_matrix each frame        │
 *   │  - Sets shim globals so glX* calls     │
 *   │    from the hack reach our EGL surface │
 *   └──────────────┬─────────────────────────┘
 *                  │
 *                  ▼
 *   ┌────────────────────────────────────────┐
 *   │ xscreensaver_compat.{h,c}              │
 *   │  - glXMakeCurrent / glXSwapBuffers     │
 *   │    passthroughs to EGL                 │
 *   │  - XGetPixel/XPutPixel over RGBA       │
 *   │  - init_GL / do_fps / etc. stubs       │
 *   └──────────────┬─────────────────────────┘
 *                  │
 *                  ▼
 *   ┌────────────────────────────────────────┐
 *   │ src/glmatrix.c (vendored, ~3 line edit)│
 *   │  - Calls glBegin/glVertex3f/glRotatef  │
 *   │  - Calls glXMakeCurrent/glXSwapBuffers │
 *   │  - Calls image_data_to_ximage()        │
 *   │  - Calls XSCREENSAVER_MODULE_2 wiring  │
 *   └──────────────┬─────────────────────────┘
 *                  │ linked together, gl* calls go through...
 *                  ▼
 *   ┌────────────────────────────────────────┐
 *   │ libGL.so.1 → GL4ES                     │
 *   │  - Translates glBegin/glMatrixMode     │
 *   │    etc. to GLES2 vertex calls +        │
 *   │    shader-based fixed-function         │
 *   │    emulation                           │
 *   └──────────────┬─────────────────────────┘
 *                  │ uses our EGL context (current)
 *                  ▼
 *   ┌────────────────────────────────────────┐
 *   │ libGLESv2.so → Mesa panfrost           │
 *   │  - Mali-G720 GLES 3.2 driver           │
 *   │  - Outputs to our Wayland wl_egl_      │
 *   │    window surface                      │
 *   └────────────────────────────────────────┘
 *
 * Build: linked against GL4ES (-lGL with rpath to /usr/lib/gl4es).
 *        Linked against system libEGL, libGLESv2 for EGL display.
 *
 * Run:   export LIBGL_NOTEST=1 LIBGL_ES=2 LIBGL_NOBANNER=1
 *        ./glmatrix_demo
 *
 * Expected on real hardware: rotating green "Matrix" glyph cascades
 * over a black background, like the movie title sequence. Quits on
 * any key press.
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
#include "gl4es_include/GL/gl.h"  /* Vendored Mesa-derived GL1 header. */

#include "xscreensaver_compat.h"
#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* The vendored glmatrix.c emits this via XSCREENSAVER_MODULE_2:
 *     glmatrix_xscreensaver_function_table
 * We pick it up via the linker symbol below. */
extern struct xscreensaver_function_table glmatrix_xscreensaver_function_table;

/* ----------------------------------------------------------------------- */
/* Macros                                                                  */
/* ----------------------------------------------------------------------- */

#define DIE(call, expected, fmt, ...)                                      \
    do {                                                                   \
        long long _ret = (long long)(call);                                \
        long long _exp = (long long)(expected);                            \
        if (_ret != _exp) {                                                \
            fprintf(stderr,                                               \
                    "glmatrix_harness: %s failed at %s:%d: " fmt "\n",    \
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
    struct xdg_wm_base             *wm_base;      /* fallback shell */
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

    /* The ModeInfo we own — passed to init_matrix / draw_matrix / etc.
     * glmatrix.c writes `mps` (a global pointer in the hack) to point
     * at a per-screen state array allocated via MI_INIT. */
    ModeInfo mi;
};

static struct app *g_app = NULL;

static void on_signal(int sig) {
    (void)sig;
    if (g_app) g_app->running = 0;
}

/* ----------------------------------------------------------------------- */
/* Monotonic clock                                                         */
/* ----------------------------------------------------------------------- */

static double monotonic_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "glmatrix_harness: clock_gettime failed\n");
        exit(1);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ----------------------------------------------------------------------- */
/* Wayland registry / seat / keyboard                                      */
/* ----------------------------------------------------------------------- */

static void wm_base_ping(void *d, struct xdg_wm_base *b, uint32_t serial) {
    (void)d;
    /* Mandatory. A client that does not pong is considered unresponsive and
     * the compositor is entitled to kill it. */
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
        /* NCZ_NO_LAYER_SHELL makes a wlroots compositor behave, for this
         * client, like one that has no layer-shell. Without it the xdg
         * fallback can only be exercised by installing a different desktop,
         * so it would ship untested -- which is how a fallback path rots. */
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

/* Shared by BOTH shells.
 *
 * The surface this renders on differs by compositor -- a wlr layer surface
 * where that protocol exists, an xdg_toplevel where it does not -- but
 * everything after the shell has handed us a size is identical: create the EGL
 * window, hand the hack its ModeInfo, call init. Keeping one body means the
 * xdg path cannot drift from the tested layer-shell path. */
static void surface_configured(struct app *a, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;

    if (!a->configured) {
        a->width = (int)w;
        a->height = (int)h;
        a->egl_window = wl_egl_window_create(a->surface, a->width, a->height);
        if (!a->egl_window) {
            fprintf(stderr, "glmatrix_harness: wl_egl_window_create failed\n");
            exit(1);
        }
        a->egl_surface = eglCreateWindowSurface(a->egl_display, a->egl_config,
                                                (EGLNativeWindowType)a->egl_window,
                                                NULL);
        if (a->egl_surface == EGL_NO_SURFACE) {
            fprintf(stderr, "glmatrix_harness: eglCreateWindowSurface failed (0x%x)\n",
                    (unsigned int)eglGetError());
            exit(1);
        }
        DIE(eglMakeCurrent(a->egl_display, a->egl_surface, a->egl_surface,
                           a->egl_context), EGL_TRUE, "eglMakeCurrent failed");

        /* Set up the shim globals BEFORE init_matrix is called. The hack
         * uses these to route its glX* calls. */
        g_harness_egl_display = (void *)a->egl_display;
        g_harness_egl_surface = (void *)a->egl_surface;
        g_harness_egl_context = (void *)a->egl_context;
        g_harness_width  = a->width;
        g_harness_height = a->height;
        g_harness_xscreen = (void *)(uintptr_t)0x1;  /* sentinel */
        g_harness_initialized = 1;

        /* Declare the surface OPAQUE.
         *
         * The EGL config asks for EGL_ALPHA_SIZE 8, so the buffer has an
         * alpha channel, and glmatrix clears to transparent black --
         * xscreensaver hacks assume they own an opaque X drawable and never
         * think about compositing. A wl_surface whose buffer carries alpha
         * is blended against what is behind it unless it declares an opaque
         * region, so the overlay composited to "whatever the desktop was
         * already showing": invisible, while rendering perfectly.
         *
         * MEASURED on O6N 2026-08-17: frame_done reached 180+ frames in 8
         * seconds with every eglSwapBuffers succeeding and a live Mali GPU
         * context, and the screen still showed only the desktop. Frames were
         * never the problem; blending was.
         *
         * The region is set in SURFACE-LOCAL (logical) coordinates, which is
         * what configure hands us -- note that is 2194x1234 here, not the
         * panel's 3840x2160, because this output runs a 1.75 fractional
         * scale. */
        struct wl_region *opaque = wl_compositor_create_region(a->compositor);
        if (opaque) {
            wl_region_add(opaque, 0, 0, a->width, a->height);
            wl_surface_set_opaque_region(a->surface, opaque);
            wl_region_destroy(opaque);
        }

        /* Call the vendored hack's init. This will:
         *   - MI_INIT → xlockmore_mi_init (allocates state array)
         *   - init_GL → records ModeInfo fields, returns sentinel
         *   - load_textures → image_data_to_ximage → libpng decode →
         *     XGetPixel/XPutPixel → glTexImage2D (through GL4ES)
         *   - reshape_matrix → gluPerspective/gluLookAt (math + matrix
         *     stack via GL4ES)
         *
         * If anything fails inside GL4ES, the hack will print to stderr
         * via check_gl_error. */
        /* Apply the hack's own declared defaults BEFORE init.
         *
         * The hack never assigns its tunables itself -- upstream xscreensaver
         * writes them through the ModeSpecVar table during option parsing. We
         * do not parse options, but we must still do that write, or every
         * tunable stays at its BSS default. That is what made this render
         * black: do_texture was False, so init_matrix skipped load_textures()
         * and no glyph atlas was ever uploaded. */
        xs_compat_apply_var_defaults(glmatrix_xscreensaver_function_table.opts);

        fprintf(stderr, "[diag] glmatrix_harness: calling init_matrix...\n");
        glmatrix_xscreensaver_function_table.init_cb(&a->mi);
        fprintf(stderr, "[diag] glmatrix_harness: init_matrix returned\n");
        a->configured = true;
    } else if (w != (uint32_t)a->width || h != (uint32_t)a->height) {
        wl_egl_window_resize(a->egl_window, (int)w, (int)h, 0, 0);
        a->width = (int)w;
        a->height = (int)h;
        g_harness_width = a->width;
        g_harness_height = a->height;
        if (glmatrix_xscreensaver_function_table.reshape_cb) {
            glmatrix_xscreensaver_function_table.reshape_cb(&a->mi,
                                                            a->width, a->height);
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
/*
 * wlr-layer-shell is the right surface for a screensaver -- it can sit on the
 * OVERLAY layer above everything and take no input focus. But it is a wlroots
 * protocol, and GNOME/Mutter does not implement it. Requiring it made this
 * engine exit with "missing Wayland globals" on the single most widely
 * deployed Wayland desktop.
 *
 * So: layer-shell when present, a fullscreen xdg_toplevel when not. The
 * fallback is a normal window, which means the compositor may show it with
 * decorations behind other windows and the user can alt-tab away from it. That
 * is a genuinely weaker screensaver, and it is stated rather than hidden --
 * but it runs, which beats exiting.
 */

static void xdg_surf_configure(void *d, struct xdg_surface *xs,
                               uint32_t serial) {
    struct app *a = d;
    xdg_surface_ack_configure(xs, serial);
    /* xdg_toplevel.configure may legitimately propose 0x0, meaning "you
     * choose". Pick the output size we already know, or a sane default. */
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

static void frame_done(void *d, struct wl_callback *cb, uint32_t t);
static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void request_frame(struct app *a) {
    if (a->frame_in_flight || !a->configured) return;
    a->frame_cb = wl_surface_frame(a->surface);
    if (!a->frame_cb) { a->running = 0; return; }
    wl_callback_add_listener(a->frame_cb, &frame_listener, a);
    a->frame_in_flight = true;
}

static void frame_done(void *d, struct wl_callback *cb, uint32_t t) {
    (void)t;
    struct app *a = d;
    wl_callback_destroy(cb);
    a->frame_cb = NULL;
    a->frame_in_flight = false;
    if (!a->running) return;

    /* The hack calls glViewport itself (inside reshape_matrix / draw_matrix).
     * We just need to call draw_cb; it will issue glBegin/.../glEnd via
     * GL4ES, then call glXSwapBuffers (our shim) which routes to
     * eglSwapBuffers. */
    {
        static unsigned long _nframes = 0;
        if (_nframes < 5 || (_nframes % 60) == 0)
            fprintf(stderr, "[diag] frame_done #%lu\n", _nframes);
        _nframes++;
    }
    glmatrix_xscreensaver_function_table.draw_cb(&a->mi);

    if (!eglSwapBuffers(a->egl_display, a->egl_surface)) {
        fprintf(stderr, "glmatrix_harness: eglSwapBuffers failed (0x%x)\n",
                (unsigned int)eglGetError());
        a->running = 0;
        return;
    }
    request_frame(a);
}

/* ----------------------------------------------------------------------- */
/* EGL setup                                                               */
/* ----------------------------------------------------------------------- */

static EGLConfig choose_egl_config(EGLDisplay dpy) {
    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE,
    };
    EGLint num = 0;
    DIE(eglChooseConfig(dpy, cfg_attr, NULL, 0, &num), EGL_TRUE,
        "eglChooseConfig probe");
    if (num < 1) {
        fprintf(stderr, "glmatrix_harness: no EGL configs match\n");
        exit(1);
    }
    EGLConfig cfg;
    DIE(eglChooseConfig(dpy, cfg_attr, &cfg, 1, &num), EGL_TRUE,
        "eglChooseConfig");
    return cfg;
}

static void init_egl(struct app *a) {
    a->egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR,
                                          a->display, NULL);
    if (a->egl_display == EGL_NO_DISPLAY) {
        a->egl_display = eglGetDisplay((EGLNativeDisplayType)a->display);
        if (a->egl_display == EGL_NO_DISPLAY) {
            fprintf(stderr, "glmatrix_harness: eglGetPlatformDisplay failed\n");
            exit(1);
        }
    }
    EGLint maj = 0, minn = 0;
    if (!eglInitialize(a->egl_display, &maj, &minn)) {
        fprintf(stderr, "glmatrix_harness: eglInitialize failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "glmatrix_harness: eglBindAPI failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
    a->egl_config = choose_egl_config(a->egl_display);
    /* GL4ES targets GLSL 100 (ES2). Context must be CLIENT_VERSION=2. */
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    a->egl_context = eglCreateContext(a->egl_display, a->egl_config,
                                      EGL_NO_CONTEXT, ctx_attr);
    if (a->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "glmatrix_harness: eglCreateContext failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
}

/* ----------------------------------------------------------------------- */
/* Shutdown                                                                */
/* ----------------------------------------------------------------------- */

static void app_fini(struct app *a) {
    if (a->frame_cb) { wl_callback_destroy(a->frame_cb); a->frame_cb = NULL; }

    /* Tell the hack to free its GL resources before we tear down the
     * EGL context. */
    if (a->configured && glmatrix_xscreensaver_function_table.free_cb) {
        glmatrix_xscreensaver_function_table.free_cb(&a->mi);
    }

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
        fprintf(stderr, "glmatrix_harness: atexit failed\n");
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
        fprintf(stderr, "glmatrix_harness: wl_display_connect failed\n");
        exit(1);
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &reg_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) exit(1);
    if (!app.compositor || !app.seat || (!app.layer_shell && !app.wm_base)) {
        fprintf(stderr,
            "glmatrix_harness: missing Wayland globals "
            "(compositor=%d seat=%d layer_shell=%d xdg_wm_base=%d)\n",
            !!app.compositor, !!app.seat, !!app.layer_shell, !!app.wm_base);
        fprintf(stderr,
            "  This needs wl_compositor + wl_seat, plus EITHER "
            "zwlr_layer_shell_v1 or xdg_wm_base.\n");
        exit(1);
    }
    wl_seat_add_listener(app.seat, &seat_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) exit(1);

    /* EGL. */
    init_egl(&app);

    /* Layer surface. */
    app.surface = wl_compositor_create_surface(app.compositor);
    if (!app.surface) {
        fprintf(stderr, "glmatrix_harness: create_surface failed\n");
        exit(1);
    }
    if (!app.layer_shell) {
        /* xdg-shell fallback: a fullscreen toplevel. Weaker than a layer
         * surface -- it is an ordinary window the user can alt-tab away from,
         * and it cannot claim the OVERLAY layer -- but it is what exists on
         * compositors without wlr-layer-shell, GNOME/Mutter chief among them. */
        fprintf(stderr, "[diag] no zwlr_layer_shell_v1; "
                        "falling back to a fullscreen xdg_toplevel\n");
        app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
        if (!app.xdg_surface) {
            fprintf(stderr, "glmatrix_harness: get_xdg_surface failed\n");
            exit(1);
        }
        xdg_surface_add_listener(app.xdg_surface, &xdg_surf_listener, &app);
        app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
        if (!app.xdg_toplevel) {
            fprintf(stderr, "glmatrix_harness: get_toplevel failed\n");
            exit(1);
        }
        xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_top_listener, &app);
        xdg_toplevel_set_title(app.xdg_toplevel, "GLMatrix");
        xdg_toplevel_set_app_id(app.xdg_toplevel, "org.nclawzero.screensaver");
        xdg_toplevel_set_fullscreen(app.xdg_toplevel, app.output);
        wl_surface_commit(app.surface);
        if (wl_display_roundtrip(app.display) < 0) exit(1);
        goto shell_ready;
    }

    app.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell, app.surface, app.output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "glmatrix");
    if (!app.layer_surface) {
        fprintf(stderr, "glmatrix_harness: get_layer_surface failed\n");
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
    if (wl_display_roundtrip(app.display) < 0) exit(1);

shell_ready:
    if (!app.configured) {
        fprintf(stderr, "glmatrix_harness: never configured\n");
        exit(1);
    }

    /* Render loop.
     *
     * DRAW AND SWAP ONCE BEFORE ASKING FOR A FRAME CALLBACK. A wl_surface
     * that has never had a buffer attached is not mapped, and a compositor
     * does not deliver frame callbacks for an unmapped surface -- so
     * requesting one first and waiting for frame_done() to perform the first
     * draw deadlocks: the callback that would trigger the draw is itself
     * waiting on the draw.
     *
     * MEASURED on O6N 2026-08-17: the process ran happily, init_matrix
     * returned, the event loop spun, and there were zero frames, zero
     * errors and a blank screen -- indistinguishable from a hung hack.
     * wl-screenhack.c has always primed the pump this way (initial draw +
     * eglSwapBuffers before request_frame) and renders correctly on the
     * same machine; this harness did not, which is the entire runtime
     * difference between the two binaries.
     */
    fprintf(stderr, "[diag] initial draw: %dx%d configured=%d\n",
            app.width, app.height, (int)app.configured);
    glmatrix_xscreensaver_function_table.draw_cb(&app.mi);
    fprintf(stderr, "[diag] initial draw_cb returned; swapping\n");
    if (!eglSwapBuffers(app.egl_display, app.egl_surface)) {
        fprintf(stderr, "glmatrix_harness: initial eglSwapBuffers failed (0x%x)\n",
                (unsigned int)eglGetError());
        return 1;
    }
    request_frame(&app);
    while (app.running) {
        while (wl_display_prepare_read(app.display) != 0) {
            if (wl_display_dispatch_pending(app.display) < 0) {
                app.running = 0; break;
            }
        }
        if (!app.running) break;
        struct pollfd pfd = { .fd = wl_display_get_fd(app.display),
                              .events = POLLIN };
        int n = poll(&pfd, 1, -1);
        if (n < 0) {
            if (errno == EINTR) { wl_display_cancel_read(app.display); continue; }
            app.running = 0; break;
        }
        wl_display_read_events(app.display);
        wl_display_dispatch_pending(app.display);
    }
    if (app.display) wl_display_roundtrip(app.display);
    return 0;
}