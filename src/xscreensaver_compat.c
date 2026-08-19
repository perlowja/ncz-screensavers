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

#define _DEFAULT_SOURCE
#include "xscreensaver_compat.h"
#include <EGL/egl.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
    mi->xgwa.colormap = 0;
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

Bool xlockmore_no_events(ModeInfo *mi, XEvent *event) {
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
 * A hack declares its tunables in a ModeSpecVar (argtype) table and NEVER
 * assigns them in code. In real xscreensaver the option-parsing layer walks
 * that table and WRITES each parsed value THROUGH the stored var pointer.
 *
 * We do not parse X resources or argv, but we must still perform that write,
 * because a hack whose table is never walked runs with every tunable at its
 * BSS default -- every Bool False, every float 0.0, every string NULL.
 *
 * That is not a cosmetic difference. MEASURED on O6N 2026-08-18: glmatrix
 * rendered pure black because `do_texture` stayed False, so init_matrix
 * skipped `load_textures()` entirely and no glyph atlas was ever uploaded.
 * The tell was in the log ordering -- "init_matrix returned" printed BEFORE
 * GL4ES initialised, i.e. init_matrix had made no GL call at all. The decode
 * and upload path that looked guilty was never reached.
 *
 * xs_compat_apply_var_defaults() below performs the write, using each entry's
 * own DEF_* string as the source, which is exactly what upstream does when no
 * resource or command-line override is present.
 */

static Bool xs_parse_bool(const char *s) {
    if (!s) return False;
    while (*s == ' ' || *s == '\t') s++;
    return (*s == 't' || *s == 'T' ||        /* true  */
            *s == 'y' || *s == 'Y' ||        /* yes   */
            *s == '1' ||
            ((*s == 'o' || *s == 'O') &&     /* on, but not off */
             (s[1] == 'n' || s[1] == 'N')));
}

/* Look up an override for one tunable in the environment.
 *
 * Upstream takes these from X resources or argv; we have neither, and a hack
 * whose behaviour can only be changed by editing and rebuilding it is very
 * hard to bisect. XS_<NAME> covers that: XS_FOG=False, XS_DENSITY=40. */
static const char *xs_env_override(const char *name) {
    char key[64];
    size_t i;
    if (!name) return NULL;
    key[0] = 'X'; key[1] = 'S'; key[2] = '_';
    for (i = 0; name[i] && i + 4 < sizeof key; i++) {
        char c = name[i];
        key[3 + i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    key[3 + i] = '\0';
    return getenv(key);
}

/* The table most recently applied. The resource getters below consult it so
 * that a hack which CALLS get_*_resource() sees the same values as a hack that
 * reads the var pointers directly. */
static ModeSpecOpt *xs_active_opts = NULL;

/* Find a var entry by resource name. */
static ModeSpecVar *xs_find_var(const char *name) {
    int i;
    if (!xs_active_opts || !xs_active_opts->vars || !name) return NULL;
    for (i = 0; i < xs_active_opts->numvars; i++) {
        ModeSpecVar *v = &xs_active_opts->vars[i];
        if (v->name && strcmp(v->name, name) == 0) return v;
    }
    return NULL;
}

void xs_compat_apply_var_defaults(ModeSpecOpt *o) {
    int i;
    if (!o || !o->vars) return;
    xs_active_opts = o;
    for (i = 0; i < o->numvars; i++) {
        ModeSpecVar *v = &o->vars[i];
        const char *ov;
        if (!v->var) continue;
        ov = xs_env_override(v->name);
        if (ov) {
            fprintf(stderr, "[xs-compat] override %s = %s\n", v->name, ov);
            v->def = (char *)ov;
        }
        switch (v->type) {
        case t_String:
            *(char **)v->var = v->def;
            break;
        case t_Bool:
            *(Bool *)v->var = xs_parse_bool(v->def);
            break;
        case t_Int:
            *(int *)v->var = v->def ? atoi(v->def) : 0;
            break;
        case t_Float:
            /* t_Float targets a float-width variable; hacks commonly declare
             * these as GLfloat, which is float on every platform we build. */
            *(float *)v->var = v->def ? (float)atof(v->def) : 0.0f;
            break;
        }
    }
}

/*
 * Resource getters, backed by the hack's own var table.
 *
 * Hacks reach their tunables two ways: through the var pointer (handled by
 * xs_compat_apply_var_defaults above) or by calling these. Both must agree, or
 * a hack behaves differently depending on which style its author used -- and
 * the failure is silent, which is how do_texture stayed False and glmatrix
 * rendered black.
 *
 * An unknown name returns the caller's stated fallback rather than a zero
 * value, because "" and False are legitimate settings and are indistinguishable
 * from "not found" otherwise.
 */

char *get_string_resource(void *ctx, const char *res_name,
                          const char *res_class) {
    ModeSpecVar *v;
    const char *val = NULL;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (v && v->type == t_String && v->var && *(char **)v->var)
        val = *(char **)v->var;        /* live value, may carry an override */
    else if (v)
        val = v->def;
    /* MUST be freeable: xscreensaver hacks routinely free() this result, so
     * handing back a string literal or a pointer into the var table would be a
     * free() of static storage. */
    return strdup(val ? val : "");
}

Bool get_boolean_resource(void *ctx, const char *res_name,
                          const char *res_class) {
    ModeSpecVar *v;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (!v) return False;
    if (v->type == t_Bool && v->var) return *(Bool *)v->var;
    return xs_parse_bool(v->def);
}

int get_integer_resource(void *ctx, const char *res_name,
                         const char *res_class) {
    ModeSpecVar *v;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (!v) return 0;
    if (v->type == t_Int && v->var) return *(int *)v->var;
    return v->def ? atoi(v->def) : 0;
}

double get_float_resource(void *ctx, const char *res_name,
                          const char *res_class) {
    ModeSpecVar *v;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (!v) return 0.0;
    if (v->type == t_Float && v->var) return (double)*(float *)v->var;
    return v->def ? atof(v->def) : 0.0;
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
/* ------------------------------------------------------------------------- */
/* Colour ramps                                                              */
/* ------------------------------------------------------------------------- */

/* HSV -> RGB, channels 0..65535 to match XColor.
 * h in degrees [0,360), s and v in [0,1]. */
static void xs_hsv_to_rgb16(double h, double s, double v,
                            unsigned short *r, unsigned short *g,
                            unsigned short *b)
{
    double rr = v, gg = v, bb = v;
    if (s > 0.0) {
        double f, p, q, t;
        int i;
        h = fmod(h, 360.0);
        if (h < 0.0) h += 360.0;
        h /= 60.0;
        i = (int) h;
        f = h - i;
        p = v * (1.0 - s);
        q = v * (1.0 - s * f);
        t = v * (1.0 - s * (1.0 - f));
        switch (i) {
            case 0:  rr = v; gg = t; bb = p; break;
            case 1:  rr = q; gg = v; bb = p; break;
            case 2:  rr = p; gg = v; bb = t; break;
            case 3:  rr = p; gg = q; bb = v; break;
            case 4:  rr = t; gg = p; bb = v; break;
            default: rr = v; gg = p; bb = q; break;
        }
    }
    *r = (unsigned short) (rr * 65535.0 + 0.5);
    *g = (unsigned short) (gg * 65535.0 + 0.5);
    *b = (unsigned short) (bb * 65535.0 + 0.5);
}

/* Fill `colors` with a smooth CYCLIC colour ramp.
 *
 * Upstream picks a handful of random points in HSV and interpolates between
 * them, wrapping so the last colour blends back into the first. The hacks rely
 * on that cyclicity: they walk the array with an incrementing index modulo
 * ncolors, so a discontinuity at the wrap shows up as a visible flash.
 *
 * Not a byte-for-byte reimplementation of utils/colors.c -- the hacks only
 * require "a smooth loop of pleasant colours of the requested length", and the
 * exact sequence is random per run anyway.
 *
 * screen/visual/cmap/allocate_p/writable_pP are ignored: no X server, nothing
 * to allocate. *ncolorsP is left as the caller set it (we always fill exactly
 * that many) so callers that re-read it stay consistent.
 */
void make_smooth_colormap(Screen *screen, Visual *visual, Colormap cmap,
                          XColor *colors, int *ncolorsP,
                          Bool allocate_p, Bool *writable_pP,
                          Bool verbose_p)
{
    int n, i, npoints, seg;
    double hues[6], sat, val;

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP; (void) verbose_p;

    if (!colors || !ncolorsP) return;
    n = *ncolorsP;
    if (n <= 0) return;

    /* 3-5 waypoints around the wheel; fewer looks like a two-tone gradient,
     * more turns into noise at typical ncolors (64-256). */
    npoints = 3 + (random() % 3);
    for (i = 0; i < npoints; i++)
        hues[i] = (random() % 360000) / 1000.0;
    hues[npoints] = hues[0];          /* close the loop */

    sat = 0.6 + (random() % 400) / 1000.0;   /* 0.6 .. 1.0 */
    val = 0.7 + (random() % 300) / 1000.0;   /* 0.7 .. 1.0 */

    for (i = 0; i < n; i++) {
        double t = (double) i * npoints / (double) n;
        double frac;
        double h0, h1, dh;
        seg = (int) t;
        if (seg >= npoints) seg = npoints - 1;
        frac = t - seg;

        h0 = hues[seg];
        h1 = hues[seg + 1];
        /* Interpolate the SHORT way around the wheel, otherwise a pair like
         * 350 -> 10 sweeps backwards through the entire spectrum. */
        dh = h1 - h0;
        if (dh > 180.0)  dh -= 360.0;
        if (dh < -180.0) dh += 360.0;

        xs_hsv_to_rgb16(h0 + dh * frac, sat, val,
                        &colors[i].red, &colors[i].green, &colors[i].blue);
        colors[i].pixel = (unsigned long) i;
        colors[i].flags = 0;
        colors[i].pad   = 0;
    }
}

static int xs_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Bool xs_parse_hex_component(const char *s, int digits,
                                   unsigned short *out)
{
    int i;
    unsigned int v = 0;
    for (i = 0; i < digits; i++) {
        int h = xs_hexval(s[i]);
        if (h < 0) return False;
        v = (v << 4) | (unsigned int)h;
    }

    if (digits == 1) v = v * 0x1111u;
    else if (digits == 2) v = (v << 8) | v;
    else if (digits == 4) { /* already 16-bit */ }
    else return False;

    *out = (unsigned short)v;
    return True;
}

static Bool xs_named_color(const char *spec, XColor *c)
{
    struct named_color { const char *name; unsigned short r, g, b; };
    static const struct named_color colors[] = {
        {"black",   0x0000, 0x0000, 0x0000},
        {"blue",    0x0000, 0x0000, 0xffff},
        {"cyan",    0x0000, 0xffff, 0xffff},
        {"gray",    0x8080, 0x8080, 0x8080},
        {"green",   0x0000, 0xffff, 0x0000},
        {"grey",    0x8080, 0x8080, 0x8080},
        {"magenta", 0xffff, 0x0000, 0xffff},
        {"orange",  0xffff, 0xa5a5, 0x0000},
        {"purple",  0x8080, 0x0000, 0x8080},
        {"red",     0xffff, 0x0000, 0x0000},
        {"white",   0xffff, 0xffff, 0xffff},
        {"yellow",  0xffff, 0xffff, 0x0000},
    };
    size_t i;
    for (i = 0; i < countof(colors); i++) {
        if (strcasecmp(spec, colors[i].name) == 0) {
            c->red = colors[i].r;
            c->green = colors[i].g;
            c->blue = colors[i].b;
            return True;
        }
    }
    return False;
}

int XParseColor(Display *dpy, Colormap cmap, const char *spec,
                XColor *exact_def_return)
{
    size_t len;
    int digits;
    XColor c = {0};

    (void)dpy;
    (void)cmap;
    if (!spec || !exact_def_return) return 0;
    while (isspace((unsigned char)*spec)) spec++;

    if (spec[0] == '#') {
        len = strlen(spec + 1);
        if (len != 3 && len != 6 && len != 12) return 0;
        digits = (int)(len / 3);
        if (!xs_parse_hex_component(spec + 1, digits, &c.red) ||
            !xs_parse_hex_component(spec + 1 + digits, digits, &c.green) ||
            !xs_parse_hex_component(spec + 1 + 2 * digits, digits, &c.blue))
            return 0;
    } else if (!xs_named_color(spec, &c)) {
        return 0;
    }

    c.pixel = ((unsigned long)(c.red >> 8) << 16) |
              ((unsigned long)(c.green >> 8) << 8) |
              (unsigned long)(c.blue >> 8);
    c.flags = 0;
    c.pad = 0;
    *exact_def_return = c;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Input plumbing (see the header for why these are inert)                   */
/* ------------------------------------------------------------------------- */

/* Upstream carries a quaternion and drag origin. With no pointer events there
 * is nothing to accumulate, so the struct exists only to give the hacks a
 * non-NULL handle to pass around. */
struct trackball_state { int unused; };

static struct trackball_state xs_trackball_singleton = { 0 };

trackball_state *gltrackball_init(int ignore_device_rotation_p)
{
    (void) ignore_device_rotation_p;
    /* A shared singleton is safe precisely because the state is empty; if this
     * ever holds a real rotation it must become a per-hack allocation. */
    return &xs_trackball_singleton;
}

void gltrackball_rotate(trackball_state *ts)
{
    (void) ts;   /* identity rotation: no glMultMatrix, leave the modelview as-is */
}

void gltrackball_reset(trackball_state *ts, float x, float y)
{
    (void) ts; (void) x; (void) y;
}

Bool gltrackball_event_handler(XEvent *event, trackball_state *ts,
                               int window_width, int window_height,
                               Bool *button_down_p)
{
    (void) event; (void) ts; (void) window_width; (void) window_height;
    /* Report "not handled" and never claim a button is held. A hack that saw a
     * stuck button_down would freeze its own animation waiting for a release
     * that cannot arrive. */
    if (button_down_p) *button_down_p = 0;
    return 0;
}

void gltrackball_start(trackball_state *ts, int x, int y, int w, int h)
{ (void) ts; (void) x; (void) y; (void) w; (void) h; }

void gltrackball_track(trackball_state *ts, int x, int y, int w, int h)
{ (void) ts; (void) x; (void) y; (void) w; (void) h; }

void gltrackball_mousewheel(trackball_state *ts, int button, int percent, int flip_p)
{ (void) ts; (void) button; (void) percent; (void) flip_p; }

double gltrackball_get_x(trackball_state *ts) { (void) ts; return 0.0; }
double gltrackball_get_y(trackball_state *ts) { (void) ts; return 0.0; }

Bool screenhack_event_helper(Display *dpy, Window window, XEvent *event)
{
    (void) dpy; (void) window; (void) event;
    return 0;    /* not handled */
}

int XLookupString(XKeyEvent *event, char *buffer, int nbytes,
                  KeySym *keysym, void *status)
{
    (void) event; (void) status;
    /* No keyboard mapping table here. Report zero characters and a zero
     * keysym; hacks compare the keysym against XK_* constants and fall through
     * to "ignore" when it matches nothing. */
    if (keysym) *keysym = 0;
    if (buffer && nbytes > 0) buffer[0] = '\0';
    return 0;
}

/* --- colour conversion (utils/colors.c) ---------------------------------- */

/* h in degrees, s/v in [0,1], channels out in 0..65535. */
void hsv_to_rgb(int h, double s, double v,
                unsigned short *r, unsigned short *g, unsigned short *b)
{
    xs_hsv_to_rgb16((double) h, s, v, r, g, b);
}

void rgb_to_hsv(unsigned short r, unsigned short g, unsigned short b,
                int *h, double *s, double *v)
{
    double rr = r / 65535.0, gg = g / 65535.0, bb = b / 65535.0;
    double maxv = rr > gg ? (rr > bb ? rr : bb) : (gg > bb ? gg : bb);
    double minv = rr < gg ? (rr < bb ? rr : bb) : (gg < bb ? gg : bb);
    double d = maxv - minv, hh = 0.0;

    if (d > 0.0) {
        if (maxv == rr)      hh = 60.0 * fmod(((gg - bb) / d), 6.0);
        else if (maxv == gg) hh = 60.0 * (((bb - rr) / d) + 2.0);
        else                 hh = 60.0 * (((rr - gg) / d) + 4.0);
        if (hh < 0.0) hh += 360.0;
    }
    if (h) *h = (int) (hh + 0.5);
    if (s) *s = (maxv > 0.0) ? (d / maxv) : 0.0;
    if (v) *v = maxv;
}

void gltrackball_free(trackball_state *ts) { (void) ts; }  /* singleton: nothing to free */
void gltrackball_stop(trackball_state *ts) { (void) ts; }

void gltrackball_get_quaternion(trackball_state *ts, float q[4])
{
    (void) ts;
    /* Identity quaternion -- no accumulated rotation. */
    if (!q) return;
    q[0] = 0.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 1.0f;
}
