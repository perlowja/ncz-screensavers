/* _POSIX_C_SOURCE MUST come before ANY system header so glibc exposes
 * sigaction + clock_gettime at compile time. Same bar as wl-screenhack.c. */
#define _POSIX_C_SOURCE 200809L

/*
 * gl4es_triangle.c — ROUND 7, PART 1 TEST.
 *
 * Minimal proof that GL4ES (the libGL.so.1 fixed-function-translation shim)
 * can route classic OpenGL 1.x immediate-mode calls (glBegin/glVertex3f/etc.)
 * into our existing Wayland/EGL/GLES2 (or GLES3.2) surface.
 *
 * Architectural intent (do NOT change without a round-N+1 task):
 *
 *   ┌───────────────────────┐
 *   │  THIS PROGRAM         │
 *   │  (gl4es_triangle)     │
 *   │                       │
 *   │  Calls glBegin,       │     link-time
 *   │  glVertex3f, glEnd    │ ◄────────────────────┐
 *   │  as if linked against │                      │
 *   │  a real libGL.        │                      │
 *   └──────────┬────────────┘                      │
 *              │ resolves at runtime to            │
 *              ▼                                   │
 *   ┌───────────────────────┐                      │
 *   │ libGL.so.1  (GL4ES)   │ ◄──── built this    │
 *   │ fixed-function        │      round, NOEGL,   │
 *   │ translation shim      │      NOX11,          │
 *   │ ~50K LOC, MIT         │      DEFAULT_ES=2    │
 *   └──────────┬────────────┘                      │
 *              │ gl4es's internal dlopen of        │
 *              ▼                                   │
 *   ┌───────────────────────┐                      │
 *   │ libGLESv2.so  (Mesa)  │ ◄──── supplied by   │
 *   │ GLES 2/3 driver       │      the platform;   │
 *   │ Mali-G720 / panfrost  │      we make the EGL │
 *   └───────────────────────┘      context current │
 *                                  BEFORE GL4ES's  │
 *                                  first call ─────┘
 *
 *   ┌───────────────────────┐
 *   │ THIS PROGRAM also     │
 *   │ creates the EGL       │
 *   │ context, the wl_egl_  │
 *   │ window, makes context │
 *   │ current — all using   │
 *   │ the same pattern as   │
 *   │ wl-screenhack.c.      │
 *   └───────────────────────┘
 *
 * GL4ES is told (via LIBGL_NOTEST=1) to skip its own throwaway hardware-probe
 * PBuffer; we provide the real ES context.
 *
 * This is a STANDALONE binary, not a library. It does NOT share code with
 * wl-screenhack.c — keeping wl-screenhack.c untouched (rounds 1-6 are
 * hardware-verified working; do not regress them). The duplication of the
 * EGL/Window setup is intentional and minimal (~250 lines).
 *
 * Build-time requirements:
 *   - wayland-client, wayland-egl, egl, glesv2 (system, via pkg-config)
 *   - GL4ES built and installed to /usr/lib/gl4es/libGL.so.1
 *
 * Run-time requirements:
 *   - LD_LIBRARY_PATH (or rpath) including /usr/lib/gl4es
 *   - LIBGL_NOTEST=1 exported (skip gl4es's broken hardware probe)
 *   - LIBGL_ES=2 (default, but explicit)
 *
 * The success criteria on real hardware are documented in PORTING.md.
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

#include <wayland-client.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext.h>
#include "gl4es_include/GL/gl.h"  /* Vendored Mesa-derived GL1 header from
                                     gl4es master. See src/gl4es_include/GL/gl.h
                                     for provenance. */

/* Same generated protocol headers as wl-screenhack.c — reused from
 * build/<config>/ via the meson-generated build dir. We include them via
 * the build-include path the meson.build exposes. */
#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* ----------------------------------------------------------------------- */
/* Macros                                                                  */
/* ----------------------------------------------------------------------- */

#define DIE(call, expected, fmt, ...)                                      \
    do {                                                                   \
        long long _ret = (long long)(call);                                \
        long long _exp = (long long)(expected);                            \
        if (_ret != _exp) {                                                \
            fprintf(stderr,                                               \
                    "gl4es_triangle: %s failed at %s:%d: " fmt "\n",       \
                    #call, __FILE__, __LINE__, ##__VA_ARGS__);             \
            exit(1);                                                       \
        }                                                                  \
    } while (0)

/* ----------------------------------------------------------------------- */
/* State                                                                   */
/* ----------------------------------------------------------------------- */

struct app {
    /* Wayland */
    struct wl_display              *display;
    struct wl_registry             *registry;
    struct wl_compositor           *compositor;
    struct wl_seat                 *seat;
    struct wl_keyboard             *keyboard;
    struct wl_output               *output;
    struct zwlr_layer_shell_v1     *layer_shell;
    struct wl_surface              *surface;
    struct zwlr_layer_surface_v1   *layer_surface;

    /* EGL */
    struct wl_egl_window  *egl_window;
    EGLDisplay             egl_display;
    EGLConfig              egl_config;
    EGLSurface             egl_surface;
    EGLContext             egl_context;

    int width, height;
    struct wl_callback *frame_cb;
    bool frame_in_flight;
    bool configured;
    volatile sig_atomic_t running;
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
        fprintf(stderr, "gl4es_triangle: clock_gettime failed\n");
        exit(1);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ----------------------------------------------------------------------- */
/* Wayland registry + seat + keyboard                                      */
/* ----------------------------------------------------------------------- */

static void registry_handle_global(void *data, struct wl_registry *r,
                                   uint32_t name, const char *iface,
                                   uint32_t version) {
    (void)version;
    struct app *app = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        app->compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_seat_interface.name) == 0)
        app->seat = wl_registry_bind(r, name, &wl_seat_interface, 7);
    else if (strcmp(iface, wl_output_interface.name) == 0 && app->output == NULL)
        app->output = wl_registry_bind(r, name, &wl_output_interface, 4);
    else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0)
        app->layer_shell = wl_registry_bind(r, name,
                                            &zwlr_layer_shell_v1_interface, 4);
}

static void registry_handle_global_remove(void *d, struct wl_registry *r,
                                          uint32_t n) {
    (void)d; (void)r; (void)n;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_handle_global,
    .global_remove = registry_handle_global_remove,
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
                           uint32_t md, uint32_t ml, uint32_t lo, uint32_t g) {
    (void)d; (void)k; (void)s; (void)md; (void)ml; (void)lo; (void)g;
}
static void keyh_repeat(void *d, struct wl_keyboard *k, int rate, int delay) {
    (void)d; (void)k; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener kbd_listener = {
    .key = keyh_key, .enter = keyh_enter, .leave = keyh_leave,
    .keymap = keyh_keymap, .modifiers = keyh_modifiers, .repeat_info = keyh_repeat,
};

static void seat_caps(void *d, struct wl_seat *s, enum wl_seat_capability c) {
    (void)s;
    struct app *a = d;
    if ((c & WL_SEAT_CAPABILITY_KEYBOARD) && a->keyboard == NULL) {
        a->keyboard = wl_seat_get_keyboard(a->seat);
        if (a->keyboard) wl_keyboard_add_listener(a->keyboard, &kbd_listener, a);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {
    (void)d; (void)s; (void)n;  /* required to be non-NULL on wl_seat v2+ */
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps, .name = seat_name,
};

/* ----------------------------------------------------------------------- */
/* Layer surface                                                           */
/* ----------------------------------------------------------------------- */

static void ls_configure(void *d, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h) {
    struct app *a = d;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w == 0 || h == 0) return;

    if (!a->configured) {
        a->width  = (int)w;
        a->height = (int)h;
        a->egl_window = wl_egl_window_create(a->surface, a->width, a->height);
        if (!a->egl_window) {
            fprintf(stderr, "gl4es_triangle: wl_egl_window_create failed\n");
            exit(1);
        }
        a->egl_surface = eglCreateWindowSurface(a->egl_display, a->egl_config,
                                                (EGLNativeWindowType)a->egl_window, NULL);
        if (a->egl_surface == EGL_NO_SURFACE) {
            fprintf(stderr, "gl4es_triangle: eglCreateWindowSurface failed (0x%x)\n",
                    (unsigned int)eglGetError());
            exit(1);
        }
        DIE(eglMakeCurrent(a->egl_display, a->egl_surface, a->egl_surface,
                           a->egl_context), EGL_TRUE, "eglMakeCurrent failed");

        /* ============================================================
         * BEGIN ROUND 7 DIAGNOSTICS — confirm GL4ES is wired correctly
         * before we issue the first glBegin().
         * ============================================================ */
        fprintf(stderr, "[diag] EGL current: dpy=%p ctx=%p srf=%p\n",
                (void *)eglGetCurrentDisplay(),
                (void *)eglGetCurrentContext(),
                (void *)eglGetCurrentSurface(EGL_DRAW));

        /* glGetString should come from GL4ES's wrapper. If GL4ES is NOT
         * loaded (e.g. we linked against system libGL by mistake), these
         * will read "Mesa" desktop GL or be NULL. */
        const char *vendor   = (const char *)glGetString(GL_VENDOR);
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        const char *version  = (const char *)glGetString(GL_VERSION);
        const char *gles_ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
        fprintf(stderr, "[diag] GL_VENDOR   = %s\n", vendor   ? vendor   : "<NULL>");
        fprintf(stderr, "[diag] GL_RENDERER = %s\n", renderer ? renderer : "<NULL>");
        fprintf(stderr, "[diag] GL_VERSION  = %s\n", version  ? version  : "<NULL>");
        fprintf(stderr, "[diag] GL_SHADER   = %s\n", gles_ver ? gles_ver : "<NULL>");

        /* GL4ES presents itself as OpenGL 1.x — note that even on a GLES3.2
         * driver, GL4ES reports GL_VERSION like "1.4 gl4es" or similar.
         * If we see "OpenGL ES 3.2" raw, GL4ES is NOT in the path and
         * glBegin below will return GL_INVALID_OPERATION immediately. */
        fprintf(stderr, "[diag] glGetError() = 0x%x\n",
                (unsigned int)glGetError());

        /* THE FIXED-FUNCTION TEST. Classic GL 1.x immediate mode:
         *   glBegin(GL_TRIANGLES);
         *     glColor3f(1,0,0); glVertex3f( 0.0,  0.8, 0.0);
         *     glColor3f(0,1,0); glVertex3f(-0.8, -0.6, 0.0);
         *     glColor3f(0,0,1); glVertex3f( 0.8, -0.6, 0.0);
         *   glEnd();
         * On a real GL 1.x driver this draws a rainbow triangle.
         * On raw GLES2 (no GL4ES) glBegin returns GL_INVALID_ENUM.
         * On GL4ES over GLES2 this should render. */
        glViewport(0, 0, a->width, a->height);
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBegin(GL_TRIANGLES);
            glColor3f(1.0f, 0.0f, 0.0f); glVertex3f( 0.0f,  0.8f, 0.0f);
            glColor3f(0.0f, 1.0f, 0.0f); glVertex3f(-0.8f, -0.6f, 0.0f);
            glColor3f(0.0f, 0.0f, 1.0f); glVertex3f( 0.8f, -0.6f, 0.0f);
        glEnd();

        fprintf(stderr, "[diag] glBegin/glVertex3f/glEnd issued; "
                "glGetError() = 0x%x\n", (unsigned int)glGetError());
        fprintf(stderr, "[diag] gldes_triangle: if you see a 3-color "
                "(red/green/blue) triangle filling the screen, "
                "GL4ES is wired correctly.\n");

        if (!eglSwapBuffers(a->egl_display, a->egl_surface)) {
            fprintf(stderr, "gl4es_triangle: eglSwapBuffers failed (0x%x)\n",
                    (unsigned int)eglGetError());
            exit(1);
        }
        a->configured = true;
    } else if (w != (uint32_t)a->width || h != (uint32_t)a->height) {
        wl_egl_window_resize(a->egl_window, (int)w, (int)h, 0, 0);
        a->width = (int)w; a->height = (int)h;
    }
}

static void ls_closed(void *d, struct zwlr_layer_surface_v1 *ls) {
    (void)ls; struct app *a = d; a->running = 0;
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {
    .configure = ls_configure, .closed = ls_closed,
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

    /* Re-draw the same triangle each frame, with a slow rotation via
     * matrix stack (fixed-function!) to PROVE the matrix stack is also
     * working through GL4ES. This exercises glMatrixMode/glRotatef/glLoad-
     * Identity — the second-most-common fixed-function surface after
     * glBegin/glVertex. */
    glViewport(0, 0, a->width, a->height);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float angle = (float)fmod(monotonic_seconds() * (6.2831853f / 12.0f),
                              6.2831853f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(angle * (180.0f / 3.14159265f), 0.0f, 0.0f, 1.0f);

    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f); glVertex3f( 0.0f,  0.8f, 0.0f);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex3f(-0.8f, -0.6f, 0.0f);
        glColor3f(0.0f, 0.0f, 1.0f); glVertex3f( 0.8f, -0.6f, 0.0f);
    glEnd();

    /* Force flush so any GL4ES-internal GL errors surface before swap. */
    glFlush();

    if (!eglSwapBuffers(a->egl_display, a->egl_surface)) {
        fprintf(stderr, "gl4es_triangle: eglSwapBuffers failed (0x%x)\n",
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
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,  /* GLES2 context; GL4ES
                                                    targets ES2 GLSL */
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,    /* GL4ES may want a depth buffer for some
                                   fixed-function ops; harmless if not used */
        EGL_STENCIL_SIZE, 0,
        EGL_NONE,
    };
    EGLint num = 0;
    DIE(eglChooseConfig(dpy, cfg_attr, NULL, 0, &num), EGL_TRUE,
        "eglChooseConfig probe failed");
    if (num < 1) {
        fprintf(stderr, "gl4es_triangle: no EGL configs match (ES2)\n");
        exit(1);
    }
    EGLConfig cfg;
    DIE(eglChooseConfig(dpy, cfg_attr, &cfg, 1, &num), EGL_TRUE,
        "eglChooseConfig failed");
    return cfg;
}

static void init_egl(struct app *a) {
    a->egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, a->display, NULL);
    if (a->egl_display == EGL_NO_DISPLAY) {
        a->egl_display = eglGetDisplay((EGLNativeDisplayType)a->display);
        if (a->egl_display == EGL_NO_DISPLAY) {
            fprintf(stderr, "gl4es_triangle: eglGetPlatformDisplay failed\n");
            exit(1);
        }
    }
    EGLint maj = 0, minn = 0;
    if (!eglInitialize(a->egl_display, &maj, &minn)) {
        fprintf(stderr, "gl4es_triangle: eglInitialize failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "gl4es_triangle: eglBindAPI(EGL_OPENGL_ES_API) failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
    a->egl_config = choose_egl_config(a->egl_display);
    /* CRITICAL: CLIENT_VERSION must be 2, not 3 — GL4ES generates GLSL 100/120
     * shaders only. */
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    a->egl_context = eglCreateContext(a->egl_display, a->egl_config,
                                      EGL_NO_CONTEXT, ctx_attr);
    if (a->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "gl4es_triangle: eglCreateContext failed (0x%x)\n",
                (unsigned int)eglGetError());
        exit(1);
    }
}

/* ----------------------------------------------------------------------- */
/* Shutdown                                                                */
/* ----------------------------------------------------------------------- */

static void app_fini(struct app *a) {
    if (a->frame_cb) { wl_callback_destroy(a->frame_cb); a->frame_cb = NULL; }
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
    if (a->egl_window) { wl_egl_window_destroy(a->egl_window); a->egl_window = NULL; }
    if (a->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(a->egl_display); a->egl_display = EGL_NO_DISPLAY;
    }
    if (a->keyboard) { wl_keyboard_release(a->keyboard); a->keyboard = NULL; }
    if (a->layer_surface) { zwlr_layer_surface_v1_destroy(a->layer_surface); a->layer_surface = NULL; }
    if (a->surface) { wl_surface_destroy(a->surface); a->surface = NULL; }
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
        fprintf(stderr, "gl4es_triangle: atexit failed\n");
        exit(1);
    }

    /* Signal handlers — same pattern as wl-screenhack.c. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTERM);
    DIE(sigaction(SIGINT, &sa, NULL), 0, "sigaction(SIGINT) failed");
    DIE(sigaction(SIGTERM, &sa, NULL), 0, "sigaction(SIGTERM) failed");
    struct sigaction ign;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    DIE(sigaction(SIGPIPE, &ign, NULL), 0, "sigaction(SIGPIPE) failed");

    /* Wayland connect + bind. */
    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "gl4es_triangle: wl_display_connect failed\n");
        exit(1);
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) { exit(1); }
    if (!app.compositor || !app.layer_shell || !app.seat) {
        fprintf(stderr, "gl4es_triangle: missing required globals\n");
        exit(1);
    }
    wl_seat_add_listener(app.seat, &seat_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) { exit(1); }

    /* EGL. */
    init_egl(&app);

    /* Surface. */
    app.surface = wl_compositor_create_surface(app.compositor);
    if (!app.surface) { fprintf(stderr, "create_surface failed\n"); exit(1); }
    app.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell, app.surface, app.output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "gl4es-triangle");
    if (!app.layer_surface) {
        fprintf(stderr, "get_layer_surface failed\n");
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
    if (wl_display_roundtrip(app.display) < 0) { exit(1); }
    if (!app.configured) {
        fprintf(stderr, "gl4es_triangle: layer surface never configured\n");
        exit(1);
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