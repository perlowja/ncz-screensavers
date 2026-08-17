/* _POSIX_C_SOURCE for sigaction + clock_gettime. */
#define _POSIX_C_SOURCE 200809L

/*
 * xscreensaver_compat.h — the PUBLIC shim surface that vendored xscreensaver
 * GL hacks see when compiled into ncz-screensavers.
 *
 * Background: xscreensaver's GL hacks (the C files under hacks/glx/ in the
 * xscreensaver
 * source tree) are written against a header chain that abstracts the GL
 * context + windowing + per-frame dispatch behind a small API:
 *
 *     typedef struct ModeInfo ModeInfo;
 *     void init_<hack>(ModeInfo *);
 *     void draw_<hack>(ModeInfo *);
 *     void reshape_<hack>(ModeInfo *, int, int);
 *     Bool <hack>_handle_event(ModeInfo *, XEvent *);
 *     void free_<hack>(ModeInfo *);
 *     GLXContext *init_GL(ModeInfo *);
 *
 * plus per-frame calls to `glXMakeCurrent` / `glXSwapBuffers`, and a
 * `ModeSpecOpt` table of X resource defaults.
 *
 * On real X11 the implementation is xlockmore.c + xlock-gl-utils.c — they
 * create an X window, a GLX context, and dispatch X events to the hack.
 *
 * In THIS project, we are NOT running X11 or GLX. The window is a
 * wlr-layer-shell surface, the GL context is EGL/GLES2 (or GLES3.2),
 * and the only "events" we care about are key presses (quit) and
 * mouse-button toggles (which glmatrix handles).
 *
 * This header exposes a *subset* of the xlockmore API that
 * (a) compiles a real, vendored xscreensaver GL hack as-is (with
 *     only header-path edits), AND
 * (b) implements the calls via the EGL/Wayland foundation in
 *     wl-screenhack.c — making the hack's GL1 fixed-function calls
 *     flow through GL4ES into our existing EGL context.
 *
 * LICENSING: Vendored xscreensaver hack source files retain their
 * original MIT-style X Consortium "Permission Notice" copyright.
 * This shim is part of ncz-screensavers, GPL-2.0-or-later.
 *
 * See PORTING.md §4 for the design rationale.
 */

#ifndef NCZ_XSCREENSAVER_COMPAT_H
#define NCZ_XSCREENSAVER_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>      /* xscreensaver hacks call strlen/strcpy/memcpy etc */
#include <strings.h>     /* strcasecmp (POSIX, in <strings.h> not <string.h>) */
#include <stdlib.h>      /* calloc/free/malloc used by shim + hack calls */
#include <time.h>        /* time/localtime/strftime (dead-code-gated but linked) */
#include <math.h>        /* sin/cos/sqrt used by gluPerspective/gluLookAt shims */

/*
 * Vendored GL 1.x header from gl4es master. See src/gl4es_include/GL/gl.h
 * for provenance + license. We do NOT use the system /usr/include/GL/gl.h
 * (Mesa's header doesn't expose the full fixed-function API we need).
 */
#include "gl4es_include/GL/gl.h"

/* ----------------------------------------------------------------------- */
/* Section 1 — opaque X11 types (declared, never dereferenced)             */
/* ----------------------------------------------------------------------- */
/*
 * Vendored hacks reference Display, Window, Visual, Drawable, GC, Colormap,
 * Screen, Pixmap, XColor, XGCValues, XWindowAttributes — but only as
 * pointer types or as plain struct fields read via the MI_* macros. They
 * never call any X11 function on them (except via ximage-loader.c, which
 * we replace). Forward-declaring as opaque structs is sufficient.
 *
 * EXCEPTION: hacks call XGetPixel(xi, x, y) and XPutPixel(xi, x, y, pixel)
 * directly on XImage* (line 742, 744, 767, 774 of glmatrix.c). We DO
 * implement those — see section 6.
 */

typedef struct _Display   Display;
typedef unsigned long     Window;          /* XID -- an integer, not a pointer/struct; matches real Xlib.h */
typedef struct _Visual    Visual;
typedef struct _Drawable  Drawable;
typedef struct _GC        GC;
typedef struct _Colormap  Colormap;
typedef struct _Screen     Screen;
typedef struct _Pixmap     Pixmap;
typedef struct _XColor     XColor;
typedef struct _XGCValues  XGCValues;
/* XImage is fully defined in section 6 below. */
/* XEvent — see union definition below in this section. We use a union of
 * minimal structs; the hack only reads xany.type (offset 0) and
 * xbutton.button (a fixed offset we control). The harness synthesizes
 * events and writes to the same struct definitions, so layout is
 * self-consistent regardless of whether it matches real Xlib. */
typedef struct _wl_display wl_display;

typedef struct {
    int type;
    /* Pad out so XButtonEvent::button lands at a known offset. The hack
     * only reads type (offset 0) and button; we make the union members
     * sized so the field positions are predictable and consistent. */
    unsigned char _pad_to_button[72];
    unsigned int button;
    unsigned char _rest[192 - 76];
} XButtonEvent;

typedef struct {
    int type;
    unsigned char _rest[192 - sizeof(int)];
} XAnyEvent;

typedef union {
    int type;
    XAnyEvent    xany;
    XButtonEvent xbutton;
    unsigned char _pad[192];   /* round to real XEvent size */
} XEvent;

/* XEvent type constants used by glmatrix. The full X11 event type set
 * would be a lot more — we only define what vendored hacks actually
 * switch on. */
#define ButtonPress    4
#define ButtonRelease  5
#define KeyPress       2
#define KeyRelease     3
#define MotionNotify   6
#define ConfigureNotify 22
#define Expose         12
#define ClientMessage  33

#define Button1   1
#define Button2   2
#define Button3   3

/* ----------------------------------------------------------------------- */
/* Section 2 — Bool / True / False                                         */
/* ----------------------------------------------------------------------- */

typedef int Bool;
#ifndef True
#define True  1
#endif
#ifndef False
#define False 0
#endif

/* ----------------------------------------------------------------------- */
/* Section 3 — ModeInfo struct                                             */
/* ----------------------------------------------------------------------- */
/*
 * Vendored hacks access fields through the MI_* macros defined below.
 * Each MI_* macro expands to a member access on ModeInfo*. The struct
 * below is what those macros deref. Fields used by glmatrix:
 *   - dpy                : Display*
 *   - window             : Window
 *   - xgwa.{visual, width, height}  : for image loading
 *   - polygon_count      : FPS bookkeeping (we stub)
 *   - fps_p              : FPS display toggle (we stub)
 *   - glx_context        : GLXContext* (typedef'd to our egl_data*)
 *
 * We use HAVE_EGL semantics — glx_context is repurposed as a pointer to
 * our internal EGL context wrapper. See xscreensaver_compat.c.
 */
struct _XWindowAttributes {
    int     depth;        /* only referenced via MI_DEPTH (we don't use) */
    void   *visual;       /* MI_VISUAL → xgwa.visual */
    void   *colormap;     /* MI_WIN_COLORMAP → xgwa.colormap */
    int     width;        /* MI_WIN_WIDTH */
    int     height;       /* MI_WIN_HEIGHT */
};
typedef struct _XWindowAttributes XWindowAttributes;

struct ModeInfo {
    /* Fields used directly by glmatrix.c */
    Display            *dpy;
    Window              window;
    XWindowAttributes   xgwa;
    int                 screen_number;
    unsigned long       polygon_count;   /* we ignore, fps_p is off */
    int                 fps_p;           /* we ignore */
    void               *glx_context;     /* pointer to our egl_data_t */
    /* Fields accessed by MI_* macros that other hacks might use. We
     * define them with safe defaults; glmatrix never touches most. */
    int                 npixels;
    unsigned long      *pixels;
    XColor             *colors;
    Bool                writable_p;
    unsigned long       white, black;
    void               *gc;
    long                pause;
    Bool                fullrandom;
    long                cycles, batchcount, size;
    Bool                threed;
    long                threed_left_color, threed_right_color;
    long                threed_both_color, threed_none_color;
    long                threed_delta;
    Bool                wireframe_p;
    Bool                is_drawn;
    Bool                root_p;
    void               *eraser;
    Bool                needs_clear;
    void               *fpst;
};
typedef struct ModeInfo ModeInfo;

/* ----------------------------------------------------------------------- */
/* Section 4 — MI_* accessor macros                                        */
/* ----------------------------------------------------------------------- */
/*
 * xscreensaver's xlockmore.h defines these. We replicate exactly what
 * glmatrix uses; the rest are harmless identity macros that resolve to
 * safe struct fields.
 */
#define MI_DISPLAY(MI)             ((MI)->dpy)
#define MI_WINDOW(MI)              ((MI)->window)
#define MI_NUM_SCREENS(MI)         (1)             /* always single-screen */
#define MI_SCREEN(MI)              ((MI)->screen_number)
#define MI_SCREENPTR(MI)           ((MI)->xgwa.visual)  /* unused */
#define MI_WIN_WHITE_PIXEL(MI)     ((MI)->white)
#define MI_WIN_BLACK_PIXEL(MI)     ((MI)->black)
#define MI_NPIXELS(MI)             ((MI)->npixels)
#define MI_PIXEL(MI, N)            ((MI)->pixels[(N)])
#define MI_WIN_WIDTH(MI)           ((MI)->xgwa.width)
#define MI_WIN_HEIGHT(MI)          ((MI)->xgwa.height)
#define MI_WIN_DEPTH(MI)           ((MI)->xgwa.depth)
#define MI_DEPTH(MI)               ((MI)->xgwa.depth)
#define MI_WIN_COLORMAP(MI)        ((MI)->xgwa.colormap)
#define MI_VISUAL(MI)              ((MI)->xgwa.visual)
#define MI_GC(MI)                  ((MI)->gc)
#define MI_PAUSE(MI)               ((MI)->pause)
#define MI_DELAY(MI)               ((MI)->pause)
#define MI_WIN_IS_FULLRANDOM(MI)   ((MI)->fullrandom)
#define MI_WIN_IS_VERBOSE(MI)      (0)
#define MI_WIN_IS_INSTALL(MI)      (1)
#define MI_WIN_IS_MONO(MI)         (0)
#define MI_WIN_IS_INROOT(MI)       ((MI)->root_p)
#define MI_WIN_IS_INWINDOW(MI)     (!((MI)->root_p))
#define MI_WIN_IS_ICONIC(MI)       (0)
#define MI_WIN_IS_WIREFRAME(MI)    ((MI)->wireframe_p)
#define MI_WIN_IS_USE3D(MI)        ((MI)->threed)
#define MI_LEFT_COLOR(MI)          ((MI)->threed_left_color)
#define MI_RIGHT_COLOR(MI)         ((MI)->threed_right_color)
#define MI_BOTH_COLOR(MI)          ((MI)->threed_both_color)
#define MI_NONE_COLOR(MI)          ((MI)->threed_none_color)
#define MI_DELTA3D(MI)             ((MI)->threed_delta)
#define MI_CYCLES(MI)              ((MI)->cycles)
#define MI_BATCHCOUNT(MI)          ((MI)->batchcount)
#define MI_SIZE(MI)                ((MI)->size)
#define MI_IS_DRAWN(MI)            ((MI)->is_drawn)
#define MI_IS_FPS(MI)              ((MI)->fps_p)
#define MI_NCOLORS(MI)             ((MI)->npixels)
#define MI_NAME(MI)                (progname)
/* Aliases */
#define MI_WIDTH(MI)               (MI_WIN_WIDTH((MI)))
#define MI_HEIGHT(MI)              (MI_WIN_HEIGHT((MI)))
#define MI_COUNT(MI)               (MI_BATCHCOUNT((MI)))
#define MI_BLACK_PIXEL(MI)         (MI_WIN_BLACK_PIXEL((MI)))
#define MI_WHITE_PIXEL(MI)         (MI_WIN_WHITE_PIXEL((MI)))
#define MI_IS_FULLRANDOM(MI)       (MI_WIN_IS_FULLRANDOM((MI)))
#define MI_IS_VERBOSE(MI)          (MI_WIN_IS_VERBOSE((MI)))
#define MI_IS_INSTALL(MI)          (MI_WIN_IS_INSTALL((MI)))
#define MI_IS_DEBUG(MI)            (0)
#define MI_IS_MOUSE(MI)            (0)

/* MI_INIT — allocates per-screen state array. On real xlockmore this
 * is a multi-screen-aware alloc; we always have one screen. */
extern void xlockmore_mi_init(ModeInfo *mi, size_t sz, void **parray);
#define MI_INIT(mi, sa) \
    xlockmore_mi_init((mi), sizeof(*(sa)), (void **)&(sa))
#define MI_ABORT(mi)              abort()
#define FreeAllGL(dpy)             /* no-op on our pipeline */

/* ----------------------------------------------------------------------- */
/* Section 5 — math macros + utility macros                                */
/* ----------------------------------------------------------------------- */

#define SINF(n)   ((float)sin((double)(n)))
#define COSF(n)   ((float)cos((double)(n)))
#define FABSF(n)  ((float)fabs((double)(n)))

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef ABS
#define ABS(a)   ((a) < 0 ? -(a) : (a))
#endif

#define NUMCOLORS 256

/* frand(n) — uniform random in [0, n). Uses libc random(). Deterministic
 * seed at startup so the screensaver is reproducible across runs. */
extern double frand(double n);
#define countof(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Section 6 — XImage type + pixel accessors                               */
/* ----------------------------------------------------------------------- */
/*
 * glmatrix.c loads a PNG via image_data_to_ximage(), then manipulates the
 * RGBA pixel buffer directly via XGetPixel/XPutPixel, then uploads the
 * buffer via glTexImage2D.
 *
 * Our XImage is just a struct wrapping an RGBA byte buffer. The data
 * layout is: width*height*4 bytes, RGBA8, byte order = R, G, B, A.
 * bytes_per_line = width*4. This is what GL4ES + glTexImage2D expects
 * when given GL_RGBA + GL_UNSIGNED_BYTE.
 *
 * glmatrix accesses pixel (x, y) via:
 *     unsigned long p = XGetPixel(xi, x, y);
 *     XPutPixel(xi, x, y, p);
 * where "unsigned long p" is treated as a packed 32-bit RGBA value
 * (it unpacks into r, g, b, a bytes via bit-shifts). So XGetPixel must
 * return the RGBA word, and XPutPixel must write it.
 */

struct _XImage_real {
    int     width;
    int     height;
    int     bytes_per_line;
    void   *data;
    int     depth;           /* bits per pixel — 32 for RGBA */
    int     bits_per_pixel;  /* alias */
};
typedef struct _XImage_real XImage_real;
#define XImage XImage_real

extern unsigned long XGetPixel(XImage *xi, int x, int y);
extern void           XPutPixel(XImage *xi, int x, int y, unsigned long pixel);
extern void           XDestroyImage(XImage *xi);

/* image_data_to_ximage — loads a PNG into a freshly allocated XImage.
 * Backed by libpng. The `Display *` and `Visual *` args are IGNORED — we
 * always produce an RGBA8 byte buffer. */
extern XImage *image_data_to_ximage(Display *dpy, Visual *visual,
                                    const unsigned char *data,
                                    unsigned long size);

/* ----------------------------------------------------------------------- */
/* Section 7 — xrm/X resources                                             */
/* ----------------------------------------------------------------------- */
/*
 * Vendored hacks declare XrmOptionDescRec opts[] tables (for
 * command-line parsing) and ModeSpecOpt {count, opts, ...} globals.
 * We don't parse command-line options, but we need the TYPES to compile.
 */

#define XrmoptionNoArg  3    /* value not actually used by us */
#define XrmoptionSepArg 1
#define XrmoptionIsArg  2
#define XrmoptionStickyArg 4
#define XrmoptionResArg    5    /* resource name only */
#define XrmoptionSkipArg   7    /* skip this arg */

typedef struct {
    char *option;       /* command-line flag, e.g. "-clock" */
    char *specifier;    /* X resource spec, e.g. ".clock" */
    int   argKind;      /* one of XrmoptionNoArg etc. */
    void *value;        /* string value, e.g. "True" */
} XrmOptionDescRec;

/*
 * xlockmore option-variable declarations.  Despite its historical name,
 * `argtype` is the complete table-entry struct (not the enum): hacks declare
 * `static argtype vars[] = { ... }`.  The final member is the parser tag.
 *
 * glmatrix currently uses t_String, t_Float, and t_Bool.  t_Int is included
 * because it is part of the standard xlockmore declaration machinery and is
 * used by many other hacks.  The table is metadata only in this port for now;
 * ModeSpecOpt retains it for a future argv/resource-compatible consumer.
 */
typedef enum {
    t_String,
    t_Float,
    t_Int,
    t_Bool
} argtype_tag;

typedef struct {
    void        *var;        /* pointer to the variable to set */
    char        *name;       /* X resource name */
    char        *classname;  /* X resource class */
    char        *def;        /* default value string */
    argtype_tag  type;       /* how the framework parses def/overrides */
} argtype;

typedef argtype ModeSpecVar;

typedef struct {
    int             numopts;
    XrmOptionDescRec *opts;
    int             numvars;
    ModeSpecVar     *vars;
    void           *dummy;
} ModeSpecOpt;

#define ENTRYPOINT static

/* get_string_resource — we don't parse X resources; return the default
 * string the hack passed in via DEFAULTS or DEF_FOO. The hack's var
 * tables store the default strings, so we just return those. We provide
 * a stub that returns an empty string or the second arg — most hacks
 * don't actually USE these in a Wayland port. */
extern char *get_string_resource(ModeInfo *mi, const char *res_name,
                                 const char *res_class);
extern Bool  get_boolean_resource(ModeInfo *mi, const char *res_name,
                                  const char *res_class);

/* glu* — small subset of GLU. gluPerspective + gluLookAt are the only
 * ones used by glmatrix.c; expand as future ports need more. These
 * wrap glMultMatrixf + glTranslated — small fixed math, no GLU lib
 * needed. Implementation in xscreensaver_compat.c. */
extern void gluPerspective(GLdouble fovy, GLdouble aspect,
                           GLdouble zNear, GLdouble zFar);
extern void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
                      GLdouble cx, GLdouble cy, GLdouble cz,
                      GLdouble ux, GLdouble uy, GLdouble uz);

/* ----------------------------------------------------------------------- */
/* Section 8 — GLX passthroughs (these are the key bridge to EGL)          */
/* ----------------------------------------------------------------------- */
/*
 * In real xscreensaver, glXMakeCurrent binds a GLXContext to a drawable
 * (X Window). In our pipeline, GL4ES is intercepting all gl* calls and
 * using the EGL context that's already current. So:
 *
 *   - glXMakeCurrent(D, W, C)  →  we record C as the active GL context,
 *                                and (if necessary) eglMakeCurrent it.
 *                                In practice, the EGL context is already
 *                                current from the harness; we no-op.
 *   - glXSwapBuffers(D, W)     →  eglSwapBuffers(egl_display, egl_surface)
 *
 * The vendor hack compiles these calls against the GL/glx.h symbols,
 * which we provide here as wrappers that dispatch into the harness's
 * EGL state via a global pointer (set by the harness before the first
 * call).
 */

typedef struct _GLXContext *GLXContext;

extern int  glXMakeCurrent(Display *dpy, Window drawable, GLXContext ctx);
extern void glXSwapBuffers(Display *dpy, Window drawable);
extern void glXDestroyContext(Display *dpy, GLXContext ctx);

/* init_GL — the central "give me a GL context" entry point. On real
 * xlockmore this picks a GLX visual, creates a context, binds it.
 * For us, this is called once per hack from its init_<name> callback.
 * We simply record the ModeInfo* in a global so subsequent glX* calls
 * know which surface/context to use, and return a non-NULL sentinel. */
extern void *init_GL(ModeInfo *mi);

/* clear_gl_error / check_gl_error — diagnostic helpers. Real xscreensaver
 * uses these around GL calls to spot driver bugs. We implement them via
 * glGetError. */
extern void clear_gl_error(void);
extern void check_gl_error(const char *type);

/* current_device_rotation — 0 on non-mobile (X11/Wayland). */
extern double current_device_rotation(void);

/* do_fps — only called if mi->fps_p is set. We stub it; fps_p is always
 * false in our ModeInfo, so this is never reached. */
extern void do_fps(ModeInfo *mi);

/* xlockmore_no_events — used by the daemon to indicate "no input events
 * queued". We don't dispatch real X events to hacks; we just return True. */
extern int xlockmore_no_events(ModeInfo *mi, void *event);

/* ----------------------------------------------------------------------- */
/* Section 9 — registration                                                */
/* ----------------------------------------------------------------------- */
/*
 * The xlockmore framework's XSCREENSAVER_MODULE_2 macro emits two globals:
 *   - NAME##_xlockmore_function_table   (raw function pointers + opts)
 *   - NAME##_xscreensaver_function_table (wires through xlockmore_setup)
 *
 * For our pipeline we ONLY need the xscreensaver_function_table struct,
 * because the harness reads the function pointers out of it directly to
 * drive the hack's lifecycle. We provide a streamlined macro that emits
 * only what our harness needs.
 *
 * Schema of our xscreensaver_function_table (see xscreensaver_compat.c):
 *   - init_cb:      void (*)(ModeInfo *)
 *   - draw_cb:      void (*)(ModeInfo *)
 *   - reshape_cb:   void (*)(ModeInfo *, int, int)
 *   - event_cb:     Bool (*)(ModeInfo *, XEvent *)
 *   - free_cb:      void (*)(ModeInfo *)
 *   - release_cb:   void (*)(ModeInfo *)  -- may be NULL
 *   - opts:         ModeSpecOpt *
 *   - name:         const char *
 *
 * XSCREENSAVER_MODULE_2(CLASS, NAME, PREFIX) wires:
 *   PREFIX##_init    → init_cb
 *   PREFIX##_draw    → draw_cb
 *   PREFIX##_reshape → reshape_cb
 *   PREFIX##_handle_event → event_cb
 *   PREFIX##_free    → free_cb
 *   PREFIX##_release (or 0 if not defined) → release_cb
 *   PREFIX##_opts    → opts (or NULL)
 *
 * The CLASS arg is the user-visible category string (e.g. "GLMatrix");
 * NAME is the function-table struct name (typically the lowercase hack
 * name, e.g. "glmatrix"). PREFIX is the symbol stem (e.g. "matrix"),
 * matching the hack's PREFIX##_init, PREFIX##_draw, etc.
 */
struct xscreensaver_function_table {
    const char  *name;          /* e.g. "glmatrix" */
    const char  *class_;        /* e.g. "GLMatrix" */
    void       (*init_cb)(ModeInfo *);
    void       (*draw_cb)(ModeInfo *);
    void       (*reshape_cb)(ModeInfo *, int, int);
    Bool       (*event_cb)(ModeInfo *, XEvent *);
    void       (*free_cb)(ModeInfo *);
    void       (*release_cb)(ModeInfo *);  /* may be NULL */
    ModeSpecOpt *opts;          /* may be NULL */
};

#define XSCREENSAVER_MODULE_2(CLASS, NAME, PREFIX)                          \
    struct xscreensaver_function_table                                       \
        NAME##_xscreensaver_function_table = {                               \
            .name        = #NAME,                                            \
            .class_      = (CLASS),                                          \
            .init_cb     = init_##PREFIX,                                    \
            .draw_cb     = draw_##PREFIX,                                    \
            .reshape_cb  = reshape_##PREFIX,                                 \
            .event_cb    = PREFIX##_handle_event,                            \
            .free_cb     = free_##PREFIX,                                    \
            .release_cb  = release_##PREFIX,                                 \
            .opts        = PREFIX##_opts,                                    \
        };

#define XSCREENSAVER_MODULE(CLASS, PREFIX) \
    XSCREENSAVER_MODULE_2(CLASS, PREFIX, PREFIX)

/* ----------------------------------------------------------------------- */
/* Section 10 — globals                                                    */
/* ----------------------------------------------------------------------- */

extern const char *progname;     /* set by harness to argv[0] or "ncz-screensavers" */
extern Bool         mono_p;      /* we always set to False */

/* ----------------------------------------------------------------------- */
/* Section 11 — harness globals (set by glmatrix_harness.c before init_*)  */
/* ----------------------------------------------------------------------- */
/*
 * The harness (glmatrix_harness.c) owns the Wayland display, the EGL
 * display/surface/context, and the size. These globals expose that state
 * to the shim, which routes glX* calls and other compat APIs into it.
 */
extern void *g_harness_egl_display;   /* EGLDisplay */
extern void *g_harness_egl_surface;   /* EGLSurface */
extern void *g_harness_egl_context;   /* EGLContext */
extern int   g_harness_width;
extern int   g_harness_height;
extern void *g_harness_xscreen;       /* Display* stub for hacks to round-trip */
extern int   g_harness_initialized;   /* 1 once the harness has set the others */

#endif /* NCZ_XSCREENSAVER_COMPAT_H */