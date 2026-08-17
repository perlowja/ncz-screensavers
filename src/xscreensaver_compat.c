/* _POSIX_C_SOURCE for sigaction + clock_gettime. */
#define _POSIX_C_SOURCE 200809L

/*
 * xscreensaver_compat.c — implementations of the shim functions declared
 * in xscreensaver_compat.h. Backs the xlockmore/screenhack API surface
 * that vendored xscreensaver GL hacks expect.
 *
 * ROUTING MODEL:
 *
 *   ┌───────────────────────┐
 *   │ Vendored hack source  │ (e.g. src/glmatrix.c, unmodified)
 *   │ Calls glXMakeCurrent, │
 *   │ glXSwapBuffers,       │
 *   │ XGetPixel, init_GL... │
 *   └──────────┬────────────┘
 *              │ linked into the same binary
 *              ▼
 *   ┌───────────────────────┐
 *   │ xscreensaver_compat.c │ (this file)
 *   │ Routes glX* calls to  │
 *   │ the harness's EGL     │
 *   │ surface via globals.  │
 *   └──────────┬────────────┘
 *              │ uses
 *              ▼
 *   ┌───────────────────────┐
 *   │ glmatrix_harness.c    │ (sets the globals, owns the EGL context)
 *   └───────────────────────┘
 *
 * The harness sets the globals (`g_harness_*`) BEFORE the hack's init
 * function is called. The hack then uses those globals to route its
 * glX* calls into the right EGL surface.
 *
 * For build cleanliness: this file is compiled with -Wall -Wextra.
 */

#include "xscreensaver_compat.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* glu* — small subset of GLU. gluPerspective + gluLookAt are the only
 * ones used by glmatrix.c; expand as future ports need more. */
void gluPerspective(GLdouble fovy, GLdouble aspect,
                    GLdouble zNear, GLdouble zFar) {
    GLdouble f = 1.0 / tan(fovy * M_PI / 360.0);  /* fovy/2 in radians */
    GLfloat m[16] = {0};
    m[0]  = (GLfloat)(f / aspect);
    m[5]  = (GLfloat)f;
    m[10] = (GLfloat)((zFar + zNear) / (zNear - zFar));
    m[11] = -1.0f;
    m[14] = (GLfloat)((2.0 * zFar * zNear) / (zNear - zFar));
    glMultMatrixf(m);
}

void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
               GLdouble cx, GLdouble cy, GLdouble cz,
               GLdouble ux, GLdouble uy, GLdouble uz) {
    /* Build view matrix from eye, center, up; multiply onto current matrix. */
    GLdouble fx = cx - ex, fy = cy - ey, fz = cz - ez;
    /* Normalize f */
    GLdouble rlf = 1.0 / sqrt(fx*fx + fy*fy + fz*fz);
    fx *= rlf; fy *= rlf; fz *= rlf;
    /* s = f × up, normalized */
    GLdouble sx = fy*uz - fz*uy;
    GLdouble sy = fz*ux - fx*uz;
    GLdouble sz = fx*uy - fy*ux;
    GLdouble rls = 1.0 / sqrt(sx*sx + sy*sy + sz*sz);
    sx *= rls; sy *= rls; sz *= rls;
    /* u' = s × f */
    GLdouble ux2 = sy*fz - sz*fy;
    GLdouble uy2 = sz*fx - sx*fz;
    GLdouble uz2 = sx*fy - sy*fx;
    GLfloat m[16] = {
        (GLfloat)sx,  (GLfloat)ux2, (GLfloat)-fx, 0.0f,
        (GLfloat)sy,  (GLfloat)uy2, (GLfloat)-fy, 0.0f,
        (GLfloat)sz,  (GLfloat)uz2, (GLfloat)-fz, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    glMultMatrixf(m);
    glTranslated(-ex, -ey, -ez);
}

/*
 * g_harness_egl_display / g_harness_egl_surface / g_harness_egl_context —
 * the live EGL objects the harness has created and made current.
 *
 * g_harness_initialized — set to 1 by the harness after init_GL succeeds.
 * The hack's draw path can call eglSwapBuffers via glXSwapBuffers
 * unconditionally; the harness is responsible for ordering.
 *
 * Declared extern in xscreensaver_compat.h so the harness (in a
 * different translation unit) can assign them.
 */

void   *g_harness_egl_display = NULL;
void   *g_harness_egl_surface = NULL;
void   *g_harness_egl_context = NULL;
int     g_harness_initialized = 0;

/*
 * g_harness_width / g_harness_height — current surface size, in pixels.
 * Updated by the harness on resize.
 */
int     g_harness_width  = 0;
int     g_harness_height = 0;

/*
 * g_harness_xscreen — the X "Display*" stub pointer the hack will see.
 * It's a void* the hack will pass back to glX* calls; we just store and
 * return it as opaque. On real X11, this would be an Xlib Display*; here
 * it's just a sentinel so the hack's draw path compiles unchanged.
 */
void   *g_harness_xscreen = NULL;

/* ----------------------------------------------------------------------- */
/* progname / mono_p                                                       */
/* ----------------------------------------------------------------------- */

const char *progname = "ncz-screensavers-glmatrix";
Bool        mono_p   = False;

/* ----------------------------------------------------------------------- */
/* frand / random — deterministic RNG                                      */
/* ----------------------------------------------------------------------- */
/*
 * Vendored hacks call random() (libc) and frand() (xscreensaver helper
 * that maps random() to [0, n)). We seed srandom() with a fixed value so
 * the screensaver looks the same on every run (deterministic).
 */

static int frand_initialized = 0;
static double frand_max;

static void frand_init(void) {
    if (frand_initialized) return;
    srandom(0xDEADBEEF);
    frand_max = (double)((unsigned long)random() >> 16);  /* 0..0xFFFF */
    if (frand_max < 1.0) frand_max = 1.0;
    frand_initialized = 1;
}

double frand(double n) {
    frand_init();
    return n * ((double)((unsigned long)random() >> 16) / frand_max);
}

/* ----------------------------------------------------------------------- */
/* xlockmore_mi_init                                                       */
/* ----------------------------------------------------------------------- */

void xlockmore_mi_init(ModeInfo *mi, size_t sz, void **parray) {
    /*
     * Real xlockmore allocates a per-screen array of size `sz`, one slot
     * per X screen. We have ONE screen — allocate one slot, initialize
     * it to zero. The hack writes its per-screen state into it.
     *
     * On the FIRST call we also set up the ModeInfo's display/window/
     * xgwa fields to point at our harness globals. Subsequent calls are
     * idempotent.
     */
    (void)mi;
    if (*parray != NULL) return;   /* already initialized */

    char *arr = (char *)calloc(1, sz);
    if (!arr) {
        fprintf(stderr, "xscreensaver_compat: xlockmore_mi_init OOM (%zu bytes)\n", sz);
        exit(1);
    }
    *parray = arr;
}

/* ----------------------------------------------------------------------- */
/* init_GL                                                                 */
/* ----------------------------------------------------------------------- */

void *init_GL(ModeInfo *mi) {
    /*
     * On real xscreensaver: pick a GLX visual, create GLXContext, bind to
     * the X Window. Return a pointer that holds the GLXContext (cast to
     * void*) so the hack can stash it in mp->glx_context and call
     * glXMakeCurrent(dpy, win, *mp->glx_context) per-frame.
     *
     * In our pipeline: the harness has ALREADY created the EGL context
     * and made it current. The hack doesn't need to create anything —
     * it just needs a non-NULL sentinel that glXMakeCurrent can ignore.
     *
     * We return a pointer to a static sentinel. The hack dereferences
     * it (glXMakeCurrent(..., *ctx)) — that's fine, the sentinel is a
     * pointer to a pointer.
     *
     * We also populate mi->xgwa.width/height from the harness globals
     * so image_data_to_ximage (later called by the hack) gets correct
     * dimensions. And we set mi->screen_number = 0, mi->dpy = the
     * harness xscreen sentinel.
     */
    static void *sentinel_ctx = NULL;

    if (!g_harness_initialized) {
        fprintf(stderr, "xscreensaver_compat: init_GL called before harness "
                "initialized — harness must set g_harness_initialized=1 "
                "before calling init_<hack>.\n");
        return NULL;
    }

    mi->dpy = (Display *)g_harness_xscreen;
    mi->window = 0;   /* unused by our shim */
    mi->screen_number = 0;
    mi->xgwa.visual = NULL;
    mi->xgwa.colormap = NULL;
    mi->xgwa.width  = g_harness_width;
    mi->xgwa.height = g_harness_height;
    mi->xgwa.depth  = 24;
    mi->fps_p = False;  /* we never display FPS */
    mi->polygon_count = 0;
    mi->glx_context = NULL;  /* unused by us; sentinel is enough */

    g_harness_initialized |= 2;  /* set "init_GL called" bit */
    return &sentinel_ctx;
}

/* ----------------------------------------------------------------------- */
/* glX passthroughs                                                        */
/* ----------------------------------------------------------------------- */

int glXMakeCurrent(Display *dpy, Window drawable, GLXContext ctx) {
    /*
     * The harness has already made the EGL context current. We just
     * record that the hack asked us to (for diagnostic purposes) and
     * return success.
     *
     * Real xscreensaver: glXMakeCurrent binds a GLX context to an X
     * drawable. We get the EGL objects from the harness globals.
     */
    (void)dpy; (void)drawable; (void)ctx;
    return 1;  /* GL_TRUE */
}

void glXSwapBuffers(Display *dpy, Window drawable) {
    /*
     * Real xscreensaver: glXSwapBuffers presents the back buffer to the
     * X server. We delegate to eglSwapBuffers on the harness's surface.
     */
    (void)dpy; (void)drawable;
    if (!g_harness_initialized || !g_harness_egl_display || !g_harness_egl_surface) {
        /* No-op before harness is ready; this can happen during
         * init_GL's check_gl_error path. */
        return;
    }
    eglSwapBuffers((EGLDisplay)g_harness_egl_display,
                   (EGLSurface)g_harness_egl_surface);
}

void glXDestroyContext(Display *dpy, GLXContext ctx) {
    /*
     * Real xscreensaver: destroys the GLX context. We let the harness
     * own context lifetime — this is a no-op.
     */
    (void)dpy; (void)ctx;
}

/* ----------------------------------------------------------------------- */
/* clear_gl_error / check_gl_error                                         */
/* ----------------------------------------------------------------------- */

void clear_gl_error(void) {
    /*
     * Real xscreensaver: drains GL errors so a fresh check_gl_error
     * call sees only errors from the operation under test.
     */
    while (glGetError() != GL_NO_ERROR) { /* drain */ }
}

void check_gl_error(const char *type) {
    /*
     * Real xscreensaver: print GL errors with a label. We do the same
     * but DON'T exit — the hack expects to be able to continue past a
     * GL error.
     */
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "xscreensaver_compat: GL error after %s: 0x%x\n",
                type ? type : "(no label)", (unsigned int)e);
    }
}

/* ----------------------------------------------------------------------- */
/* current_device_rotation                                                 */
/* ----------------------------------------------------------------------- */

double current_device_rotation(void) {
    /*
     * Real xscreensaver: returns 0 on desktop (X11/Wayland), real
     * rotation angle on mobile. We are always desktop.
     */
    return 0.0;
}

/* ----------------------------------------------------------------------- */
/* do_fps                                                                  */
/* ----------------------------------------------------------------------- */

void do_fps(ModeInfo *mi) {
    /*
     * Real xscreensaver: draws an FPS counter overlay. We never call
     * this because we initialize mi->fps_p = False. Stub retained for
     * compile completeness.
     */
    (void)mi;
}

/* ----------------------------------------------------------------------- */
/* xlockmore_no_events                                                     */
/* ----------------------------------------------------------------------- */

int xlockmore_no_events(ModeInfo *mi, void *event) {
    /*
     * Real xscreensaver: "did the daemon have events to dispatch this
     * frame?" — used to gate input-handling. We have no X events; the
     * harness dispatches key/mouse events directly to the hack.
     */
    (void)mi; (void)event;
    return 1;
}

/* ----------------------------------------------------------------------- */
/* get_string_resource / get_boolean_resource                             */
/* ----------------------------------------------------------------------- */
/*
 * glmatrix.c declares these in ModeSpecVar tables but doesn't actually
 * CALL them — they're consumed by xscreensaver's option-parsing layer.
 * On our pipeline we don't parse options at all, so these are stubs.
 */

char *get_string_resource(ModeInfo *mi, const char *res_name,
                          const char *res_class) {
    (void)mi; (void)res_name; (void)res_class;
    /* The hack stores the default value in its ModeSpecVar table and
     * reads it directly via the var pointer. We return an empty string
     * just in case. */
    static char empty[] = "";
    return empty;
}

Bool get_boolean_resource(ModeInfo *mi, const char *res_name,
                          const char *res_class) {
    (void)mi; (void)res_name; (void)res_class;
    return False;
}

/* ----------------------------------------------------------------------- */
/* XImage pixel accessors                                                  */
/* ----------------------------------------------------------------------- */
/*
 * glmatrix.c calls:
 *     unsigned long p = XGetPixel(xi, x, y);
 *     XPutPixel(xi, x, y, p);
 * where `p` is treated as a packed RGBA word (the hack unpacks it into
 * r/g/b/a bytes). Our XImage stores RGBA8 data tightly packed (R at
 * offset 0, G at 1, B at 2, A at 3). XGetPixel returns the 32-bit word
 * in the order expected by glmatrix's bit-shifts.
 *
 * glmatrix.c bit-shifts with rpos=0, gpos=8, bpos=16, apos=24 (line 762
 * of original) — so it expects the word to be in memory order R|G|B|A
 * with R in the LSB. We pack as 0xAABBGGRR in memory order, which means
 * when read as a little-endian 32-bit int the value is 0xRRGGBBAA. That
 * matches: extracting `(p >> 0) & 0xFF` gives R, `(p >> 8) & 0xFF` gives
 * G, etc.
 */

unsigned long XGetPixel(XImage *xi, int x, int y) {
    if (!xi || !xi->data || x < 0 || y < 0 ||
        x >= xi->width || y >= xi->height) return 0;
    uint8_t *p = (uint8_t *)xi->data + (size_t)y * xi->bytes_per_line + (size_t)x * 4;
    /* Read 4 bytes; on little-endian this is R|G|B|A → 0xAABBGGRR
     * interpreted as a uint32. */
    uint32_t r = p[0], g = p[1], b = p[2], a = p[3];
    return (unsigned long)(r | (g << 8) | (b << 16) | (a << 24));
}

void XPutPixel(XImage *xi, int x, int y, unsigned long pixel) {
    if (!xi || !xi->data || x < 0 || y < 0 ||
        x >= xi->width || y >= xi->height) return;
    uint8_t *p = (uint8_t *)xi->data + (size_t)y * xi->bytes_per_line + (size_t)x * 4;
    p[0] = (uint8_t)( pixel        & 0xFF);   /* R */
    p[1] = (uint8_t)((pixel >>  8) & 0xFF);   /* G */
    p[2] = (uint8_t)((pixel >> 16) & 0xFF);   /* B */
    p[3] = (uint8_t)((pixel >> 24) & 0xFF);   /* A */
}

void XDestroyImage(XImage *xi) {
    if (!xi) return;
    if (xi->data) free(xi->data);
    free(xi);
}