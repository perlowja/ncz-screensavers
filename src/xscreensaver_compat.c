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
#include <unistd.h>
#include <sys/time.h>

/* texfont.h / xft.h provide texture_font_data, XCharStruct, XftFont
 * types. Vendored hacks reach them through texfont.h; we link the
 * stubs out of this file. */
#include "xft.h"
#include "texfont.h"
#include "utf8wc.h"
#include "textclient.h"
#include "X11/Intrinsic.h"

/* glu* — small subset of GLU. gluPerspective + gluLookAt are the only
 * ones used by glmatrix.c; expand as future ports need more.
 *
 * gles3_compat.{h,c}, and the link doesn't pull in libGL.so.1. */
/* gluPerspective / gluLookAt are implemented once, for both paths.
 *
 * They used to be empty in the NCZ_GLES3_BUILD path, on the assumption that a
 * ported hack would call ncz_mat_stack_perspective / ncz_mat_stack_lookAt
 * directly instead. That holds for hacks whose call sites were rewritten, but
 * it silently breaks any hack that still calls the GLU spelling: the
 * projection is simply never set, the binary links, and the hack renders
 * wrong. jigsaw hit exactly this.
 *
 * The bodies below need only glMultMatrixf and glTranslated, and
 * gles3_compat.c implements both for the GLES3 path
 * (gles3_compat.c:1573 and :1576), so one implementation serves both. A hack
 * whose call sites were ported to ncz_mat_stack_* does not call these at all,
 * so nothing is applied twice. */
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


int gluProject(GLdouble objx, GLdouble objy, GLdouble objz,
               const GLdouble model[16], const GLdouble proj[16],
               const GLint viewport[4],
               GLdouble *winx, GLdouble *winy, GLdouble *winz)
{
    GLdouble in[4] = { objx, objy, objz, 1.0 };
    GLdouble eye[4];
    GLdouble clip[4];
    int i;

    for (i = 0; i < 4; i++) {
        eye[i] = in[0] * model[0 * 4 + i] +
                 in[1] * model[1 * 4 + i] +
                 in[2] * model[2 * 4 + i] +
                 in[3] * model[3 * 4 + i];
    }

    for (i = 0; i < 4; i++) {
        clip[i] = eye[0] * proj[0 * 4 + i] +
                  eye[1] * proj[1 * 4 + i] +
                  eye[2] * proj[2 * 4 + i] +
                  eye[3] * proj[3 * 4 + i];
    }

    if (clip[3] == 0.0)
        return 0;

    clip[0] /= clip[3];
    clip[1] /= clip[3];
    clip[2] /= clip[3];

    if (winx) *winx = viewport[0] + (1.0 + clip[0]) * viewport[2] / 2.0;
    if (winy) *winy = viewport[1] + (1.0 + clip[1]) * viewport[3] / 2.0;
    if (winz) *winz = (1.0 + clip[2]) / 2.0;
    return 1;
}

/* gluScaleImage — bilinear-interpolation image resampler. Mesa-GLU
 * implements this with GLU's internal pixel-pack pipeline; GLES3 has
 * no equivalent (the GLES3 path's texture-upload is glTexImage2D with
 * the user's data verbatim, and any pre-resize for power-of-2 upload
 * has to happen CPU-side).
 *
 * Currently the only consumer is timetunnel (src/timetunnel.c:886),
 * which calls it to downscale a non-power-of-2 texture image to the
 * nearest power of 2 before upload:
 *
 *   gluScaleImage(GL_RGBA,
 *                 teximage->width, teximage->height, GL_UNSIGNED_BYTE,
 *                 teximage->data,
 *                 bx, by,
 *                 GL_UNSIGNED_BYTE, tmpbuf);
 *
 * That call site is inside `#ifndef HAVE_JWZGLES`, so a port that
 * defined HAVE_JWZGLES would skip the rescale entirely and upload a
 * non-power-of-2 texture — accepted on O6N's Mali-G720-Immortalis
 * (which is NPOT-texture-capable) but the original code path
 * explicitly chose to downscale for upload-cost reasons on older
 * hardware. Implementing the resample keeps that behavior intact.
 *
 * Format/type support: only the GL_RGBA / GL_UNSIGNED_BYTE combo
 * timetunnel uses is implemented. Other format/type combos (e.g.
 * GL_LUMINANCE, GL_RGB, GL_UNSIGNED_SHORT, GL_FLOAT) return 0
 * without writing dstData — the call sites in the vendored hacks
 * don't use them today, and the upstream GLU spec lists this as
 * GLU_INVALID_ENUM / GLU_INVALID_VALUE behavior, so a return-0-no-
 * write is the conservative contract.
 *
 * Return value: 0 on success, GLU_ERROR (100) on unsupported
 * format/type. Mirrors the convention of gluPerspective/gluLookAt
 * which return void; gluProject which returns int (1 on hit, 0 on
 * clip.w==0). The hack's call site (timetunnel.c:887) ignores the
 * return value, but we return it for spec fidelity.
 *
 * Implementation notes:
 *   - pixel coords are conventional GL: (0,0) bottom-left,
 *     srcData is row-major from bottom-left.
 *   - direct memcpy when srcW == dstW && srcH == dstH (no
 *     resampling → preserve exact source values).
 *   - bilinear interpolation between four nearest source pixels,
 *     with edge replication for source coordinates outside [0, srcW)
 *     or [0, srcH). Bilinear kernel weights are (1-fx)(1-fy),
 *     fx(1-fy), (1-fx)fy, fx*fy.
 *   - the sample point for destination pixel (dx, dy) is
 *     sx = (dx + 0.5) * (srcW/dstW) - 0.5
 *     sy = (dy + 0.5) * (srcH/dstH) - 0.5
 *     which is the GLU / GPU-style "center of pixel" convention;
 *     matches upstream gluScaleImage behavior.
 *   - destination buffer is treated as uninitialized; we write
 *     every pixel exactly once.
 */
#define GLU_ERROR 100   /* canonical Mesa GLU error code */

static int
gluScaleImage_rgba8 (GLint srcW, GLint srcH, const unsigned char *src,
                     GLint dstW, GLint dstH, unsigned char *dst)
{
    GLint dx, dy;

    /* Fast path: identity resize → preserve exact bytes. */
    if (srcW == dstW && srcH == dstH) {
        memcpy(dst, src, (size_t)srcW * (size_t)srcH * 4);
        return 0;
    }

    /* Per-row/column scale factors in source-pixel units. Use doubles
     * to avoid losing the +0.5 center-of-pixel offset at small dims. */
    {
        const double sx_scale = (double)srcW / (double)dstW;
        const double sy_scale = (double)srcH / (double)dstH;

        for (dy = 0; dy < dstH; dy++) {
            double sy = ((double)dy + 0.5) * sy_scale - 0.5;
            int    sy0, sy1;
            double fy;
            GLint  syo0, syo1;     /* clamped source y indices */

            /* Clamp sy to source bounds. GLU clamps destination
             * samples that fall outside the source image to the
             * edge pixels (clamp-to-edge, NOT wrap). */
            if (sy < 0.0)              sy = 0.0;
            if (sy > (double)(srcH-1)) sy = (double)(srcH - 1);

            sy0 = (int)sy;
            fy  = sy - (double)sy0;
            sy1 = sy0 + 1;
            if (sy1 >= srcH) sy1 = srcH - 1;

            /* Bilinear weights for the two source rows we're
             * blending between (top row weight = 1-fy). */
            const double wy0 = 1.0 - fy;
            const double wy1 = fy;

            for (dx = 0; dx < dstW; dx++) {
                double sx = ((double)dx + 0.5) * sx_scale - 0.5;
                int    sx0, sx1;
                double fx;
                int    c;
                unsigned char *d = dst + ((size_t)dy * dstW + dx) * 4;
                double acc[4];

                if (sx < 0.0)              sx = 0.0;
                if (sx > (double)(srcW-1)) sx = (double)(srcW - 1);

                sx0 = (int)sx;
                fx  = sx - (double)sx0;
                sx1 = sx0 + 1;
                if (sx1 >= srcW) sx1 = srcW - 1;

                {
                    const double wx0 = 1.0 - fx;
                    const double wx1 = fx;
                    const unsigned char *p00 = src + ((size_t)sy0 * srcW + sx0) * 4;
                    const unsigned char *p01 = src + ((size_t)sy0 * srcW + sx1) * 4;
                    const unsigned char *p10 = src + ((size_t)sy1 * srcW + sx0) * 4;
                    const unsigned char *p11 = src + ((size_t)sy1 * srcW + sx1) * 4;

                    for (c = 0; c < 4; c++) {
                        acc[c] = wy0 * (wx0 * p00[c] + wx1 * p01[c])
                               + wy1 * (wx0 * p10[c] + wx1 * p11[c]);
                    }
                }

                /* Clamp-and-round to 8-bit. +0.5 rounds, saturate to
                 * [0, 255] in case the kernel amplifies (shouldn't
                 * happen with bilinearly-interpolated 8-bit inputs
                 * — weights sum to 1 — but cheap insurance). */
                for (c = 0; c < 4; c++) {
                    int v = (int)(acc[c] + 0.5);
                    if (v < 0)   v = 0;
                    if (v > 255) v = 255;
                    d[c] = (unsigned char)v;
                }
            }
        }
    }
    return 0;
}

int gluScaleImage(GLenum format,
                  GLint srcW, GLint srcH, GLenum srcType,
                  const void *srcData,
                  GLint dstW, GLint dstH, GLenum dstType,
                  void *dstData)
{
    /* Only the format/type combo timetunnel uses (src/timetunnel.c:886)
     * is implemented. Other combos return GLU_ERROR without writing
     * dstData — the timetunnel call site ignores the return value,
     * so an "unsupported" failure surfaces as garbage in the scaled
     * texture, which is the same observable failure mode as the legacy
     * Mesa gluScaleImage returning GLU_ERROR on an unsupported combo. */
    if (format != GL_RGBA ||
        srcType != GL_UNSIGNED_BYTE ||
        dstType != GL_UNSIGNED_BYTE)
        return GLU_ERROR;

    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return GLU_ERROR;

    if (srcData == NULL || dstData == NULL)
        return GLU_ERROR;

    return gluScaleImage_rgba8(srcW, srcH, (const unsigned char *)srcData,
                               dstW, dstH, (unsigned char *)dstData);
}

/* gluBuild2DMipmaps — GLU GL_TEXTURE_2D mipmap builder, GLES3
 * implementation. We upload the level-0 texture via glTexImage2D
 * (using caller's 'components' as the internal format and 'format'
 * / 'type' / 'data' for the source layout), then call
 * glGenerateMipmap(GL_TEXTURE_2D) to let the driver build the rest
 * of the chain. glGenerateMipmap requires a texture that has a
 * complete level-0 image (we've just uploaded it) and that the
 * currently-bound GL_TEXTURE_2D target use a MIPMAP min filter — we
 * don't enforce that here, callers do.
 *
 * Returns 0 on success (matches GLU's documented success return),
 * GLU_ERROR on what we consider a real failure (driver couldn't
 * regenerate, mostly unreachable in practice). On any error we log
 * to stderr so flaky texture-pipeline bugs are diagnosable.
 *
 * Restrictions:
 *   - 'target' must be GL_TEXTURE_2D (the only form upstream uses;
 *     GL_TEXTURE_1D / GL_TEXTURE_3D targets don't apply to our
 *     GLES3 screensaver context anyway — there's no proxy texture
 *     support to bridge into glGenerateMipmap).
 *   - 'components' is the internal-format spec; we pass it through
 *     to glTexImage2D (caller's choice, usually GL_LUMINANCE_ALPHA
 *     / GL_RGBA / GL_RGB). The driver's internalformat table decides
 *     what storage to allocate.
 *   - 'data' must remain valid until glGenerateMipmap returns; we
 *     don't copy it.
 *
 * This deliberately does NOT replicate GLU's per-level downsampling.
 * glGenerateMipmap is a driver-native operation and is what GLES3
 * calls expect — building the chain CPU-side would duplicate driver
 * work and yield visually different results. */
int gluBuild2DMipmaps(GLenum target,
                      GLint components,
                      GLsizei width, GLsizei height,
                      GLenum format, GLenum type,
                      const void *data)
{
    if (target != GL_TEXTURE_2D) {
        fprintf(stderr, "gluBuild2DMipmaps: target=0x%x not supported\n",
                (unsigned)target);
        return GLU_ERROR;
    }
    if (width <= 0 || height <= 0 || !data) {
        fprintf(stderr, "gluBuild2DMipmaps: invalid dimensions %dx%d\n",
                (int)width, (int)height);
        return GLU_ERROR;
    }
    /* Level-0 upload. The internalformat slot (the 'components'
     * GLU arg) is what GLES3 expects in the 'internalformat' position;
     * the second slot in upstream's gluBuild2DMipmaps is unused on
     * GLES3 — we pass 'components' through both slots for source
     * compatibility. */
    glTexImage2D(target, 0, components, width, height, 0,
                 format, type, data);
    /* Generate mipmap chain. Required to be called from within a
     * glGenerateMipmap-able state; that's enforced by the spec, and
     * we surface a real error if the driver rejects it. */
    glGenerateMipmap(target);
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            fprintf(stderr,
                    "gluBuild2DMipmaps: glGenerateMipmap failed (0x%x)\n",
                    (unsigned)err);
            return GLU_ERROR;
        }
    }
    return 0;
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
    struct timeval tv;
    unsigned int seed;
    if (frand_initialized) return;
    /*
     * Seed from the wall clock + process ID so each launch differs.
     *
     * A fixed seed (e.g. 0xDEADBEEF) used to be the choice here -- and it
     * is what made the visual verification loop in PORTING.md reproducible
     * -- but it surfaces a real upstream-xscreensaver fragility: a number
     * of hacks (gears, pinion) have data-validation aborts of the form
     * `if (g->inner_r2 > g->inner_r) abort();` that fire when the
     * deterministic random sequence produces a small gear. Roughly half
     * of all 32-bit seeds hit one of those aborts in the first few gears.
     *
     * A live seed makes most launches succeed (54% of seeds are clean
     * for gears in a 100k sweep, but each launch only samples one seed
     * and a single retry on abort would clear that). The cost of losing
     * bit-exact reproducibility across runs is the right one for a
     * screensaver -- the alternative is "deterministically broken".
     *
     * The historical "looks the same on every run" comment in this file
     * was a property of upstream xscreensaver's daemon mode, where the
     * daemon runs each hack for minutes and the deterministic seed
     * mattered because the daemon doesn't fork between hacks. Our
     * _demo binaries are one-shot, so per-run reseeding is fine.
     */
    gettimeofday(&tv, NULL);
    seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec ^ ((unsigned int)getpid() << 16));
    srandom(seed);
    /*
     * frand(n) must return a value uniformly distributed in [0, n). We
     * approximate that by shifting random() down to its top 16 bits (so
     * the result is in [0, 65535]) and dividing by 65536.0.
     *
     * The historical version calibrated frand_max to one specific
     * random() output and used it as the divisor. That calibration was
     * broken: if the very first random() after srandom returned a value
     * smaller than 0x00010000, frand_max became 0 (clamped to 1), and
     * every subsequent frand(n) call returned n * random() (i.e. up to
     * ~n * 65535) instead of n. That is exactly why gears was aborting
     * inside its data-validation assertions: tooth_w, tooth_h, nteeth
     * and the resulting r/inner_r were all inflated by ~4-9x over
     * their intended values, so the final `if (g->inner_r > g->r) abort()`
     * fired on the very first gear.
     *
     * Using a fixed 65536.0 divisor removes the calibration entirely;
     * the bias it introduced (a small fraction of the lowest bucket
     * mapping to a slightly wider range) is irrelevant for visuals.
     */
    frand_max = 65536.0;
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
    mi->xgwa.screen = NULL;
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

/*
 * Defaults block parsed from the hack's DEFAULTS macro.
 *
 * A hack declares its tunables two ways in upstream xscreensaver:
 *
 *   1. An argtype vars[] table -- the "xlockmore-style" declarations, where
 *      each entry holds the .def (default) inline. xs_compat_apply_var_defaults
 *      walks this and writes through each var pointer.
 *
 *   2. A `#define DEFAULTS "*name: value\n..."` macro at the top of the .c
 *      file -- the "screenhack-style" declarations, used for resource keys
 *      upstream treated as defaults rather than tunables: typically the
 *      color names, font names and similar that the hack reads via
 *      get_string_resource() but never puts in vars[].
 *
 * Until 2026-08-20 we parsed only the first form, which silently broke
 * chompytower (jawColor), covid19, gibson, gravitywell, handsy, headroom,
 * highvoltage, nakagin, skulloop, splitflap, squirtorus, vigilance,
 * winduprobot, fliptext, unknownpleasures, dnalogo and unicrud. The hack's
 * get_string_resource("jawColor") came back empty, XParseColor printed
 * "unparsable color in jawColor: ", and the hack exited 1 -- logged as a
 * crash even though the actual fault was a shim-side lookup miss.
 *
 * We now parse the DEFAULTS block at apply time and store the entries here,
 * in a per-hack table. get_*_resource consults this table after the
 * vars[] table, so a hack with both styles sees a single consistent view.
 *
 * Entries are malloc'd and must be freed when the harness tears down or
 * a new hack is loaded into the same process. We are single-hack-at-a-time
 * (each _demo binary runs one effect and exits), so the leak is bounded by
 * the number of entries one DEFAULTS block contributes (a few dozen).
 */
typedef struct {
    char *name;
    char *value;
} xs_defaults_entry;

static xs_defaults_entry *xs_defaults_table = NULL;
static int                 xs_defaults_count  = 0;
static int                 xs_defaults_cap    = 0;

static void xs_defaults_clear(void) {
    int i;
    for (i = 0; i < xs_defaults_count; i++) {
        free(xs_defaults_table[i].name);
        free(xs_defaults_table[i].value);
    }
    xs_defaults_count = 0;
    /* keep xs_defaults_table allocated for reuse -- freed at process exit. */
}

static void xs_defaults_add(const char *name, const char *value, int vlen) {
    char *n, *v;
    if (xs_defaults_count == xs_defaults_cap) {
        int newcap = xs_defaults_cap ? xs_defaults_cap * 2 : 32;
        xs_defaults_entry *nt = realloc(xs_defaults_table,
                                        newcap * sizeof(*nt));
        if (!nt) return;   /* OOM: silently skip */
        xs_defaults_table = nt;
        xs_defaults_cap = newcap;
    }
    n = strdup(name);
    v = (char *)malloc(vlen + 1);
    if (!n || !v) { free(n); free(v); return; }
    memcpy(v, value, vlen);
    v[vlen] = '\0';
    xs_defaults_table[xs_defaults_count].name = n;
    xs_defaults_table[xs_defaults_count].value = v;
    xs_defaults_count++;
}

static const char *xs_defaults_lookup(const char *name) {
    int i;
    if (!name || !xs_defaults_table) return NULL;
    for (i = 0; i < xs_defaults_count; i++) {
        if (strcmp(xs_defaults_table[i].name, name) == 0)
            return xs_defaults_table[i].value;
    }
    return NULL;
}

/* Parse a DEFAULTS string of the form:
 *
 *     "*name: value\n*name2: value 2\n..."
 *
 * Each line is "*" + name + ":" + value (whitespace padded). Names and
 * values are stripped of leading/trailing whitespace. Values may contain
 * internal whitespace and punctuation (font names, hex colors with spaces,
 * text strings). Empty lines are skipped. A value that extends across the
 * remaining buffer is still captured -- the DEFAULTS macro typically has a
 * trailing "\n" but some hacks omit it on the last line.
 *
 * Not strtok-based because DEFAULTS is read-only storage in the vendored
 * hack's .rodata, and we want to keep that storage immutable.
 */
static void xs_parse_defaults_string(const char *s) {
    const char *p;
    if (!s) return;
    xs_defaults_clear();
    p = s;
    while (*p) {
        const char *line_start = p;
        const char *nl = strchr(p, '\n');
        const char *line_end = nl ? nl : (p + strlen(p));
        const char *colon;
        const char *name_start, *name_end;
        const char *val_start, *val_end;
        int name_len, val_len;

        /* Skip leading whitespace, require a leading '*' or '.' as the
         * "this is a defaults line" marker. screenhack.c treats them as
         * equivalent: '*' is the loose binding (matches the program class),
         * '.' is the tight binding (matches the program instance). Hacks
         * mix them in the same block -- dnalogo uses '*' for tunables and
         * '.' for the colour slot specifically. */
        while (line_start < line_end &&
               (*line_start == ' ' || *line_start == '\t' ||
                *line_start == '\r'))
            line_start++;
        if (line_start >= line_end ||
            (*line_start != '*' && *line_start != '.'))
            goto next;

        name_start = line_start + 1;
        colon = name_start;
        while (colon < line_end && *colon != ':') colon++;
        if (colon >= line_end) goto next;   /* no colon, malformed line */

        name_end = colon;
        while (name_end > name_start &&
               (name_end[-1] == ' ' || name_end[-1] == '\t'))
            name_end--;
        name_len = (int)(name_end - name_start);
        if (name_len <= 0) goto next;

        val_start = colon + 1;
        while (val_start < line_end &&
               (*val_start == ' ' || *val_start == '\t'))
            val_start++;
        val_end = line_end;
        while (val_end > val_start &&
               (val_end[-1] == ' ' || val_end[-1] == '\t' ||
                val_end[-1] == '\r'))
            val_end--;
        val_len = (int)(val_end - val_start);

        /* Heap-copy both -- the source string is rodata we must not mutate. */
        {
            char *namebuf = (char *)malloc(name_len + 1);
            if (!namebuf) goto next;
            memcpy(namebuf, name_start, name_len);
            namebuf[name_len] = '\0';
            xs_defaults_add(namebuf, val_start, val_len);
            free(namebuf);
        }

      next:
        if (!nl) break;
        p = nl + 1;
    }
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

void xs_compat_apply_var_defaults(ModeSpecOpt *o, const char *defaults_str) {
    int i;
    xs_active_opts = o;

    /* Parse the screenhack-style DEFAULTS block FIRST so the side-table is
     * populated before vars[] writes go through: a hack whose vars[] table
     * points at a name that ALSO appears in DEFAULTS (e.g. "delay") still
     * resolves via vars[], but anything only in DEFAULTS (colors, fonts) is
     * findable through xs_defaults_lookup. */
    xs_parse_defaults_string(defaults_str);

    if (!o || !o->vars) return;
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
    /* Fall through to the screenhack-style DEFAULTS table. chompytower's
     * "jawColor" (and the 16 other hacks' analogous names) only appear in
     * DEFAULTS, never in vars[]. Without this fallback the call returns
     * "" and XParseColor errors out with "unparsable color in jawColor: ". */
    if (!val)
        val = xs_defaults_lookup(res_name);
    /* XScreenSaver's resource database supplies these two application-wide
     * defaults even when a hack's DEFAULTS block does not repeat them.
     * Standalone screenhack-style modules such as unknownpleasures request
     * them directly by class, so an empty string here makes XParseColor fail
     * during init. */
    if (!val && res_class) {
        if (strcmp(res_class, "Foreground") == 0)
            val = "white";
        else if (strcmp(res_class, "Background") == 0)
            val = "black";
    }
    /* MUST be freeable: xscreensaver hacks routinely free() this result, so
     * handing back a string literal or a pointer into the var table would be a
     * free() of static storage. */
    return strdup(val ? val : "");
}

Bool get_boolean_resource(void *ctx, const char *res_name,
                          const char *res_class) {
    ModeSpecVar *v;
    const char *def = NULL;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (v && v->type == t_Bool && v->var) return *(Bool *)v->var;
    if (v) def = v->def;
    /* Fall through to DEFAULTS table. dnalogo reads "doGasket"/"doHelix"
     * this way and has no vars[] table at all, so this lookup is the
     * only way those names resolve. Without it dnalogo exits with
     * "no helix or gasket?" because both booleans default to False. */
    if (!def) def = xs_defaults_lookup(res_name);
    return xs_parse_bool(def);
}

int get_integer_resource(void *ctx, const char *res_name,
                         const char *res_class) {
    ModeSpecVar *v;
    const char *def = NULL;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (v && v->type == t_Int && v->var) return *(int *)v->var;
    if (v) def = v->def;
    if (!def) def = xs_defaults_lookup(res_name);
    return def ? atoi(def) : 0;
}

double get_float_resource(void *ctx, const char *res_name,
                          const char *res_class) {
    ModeSpecVar *v;
    const char *def = NULL;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (v && v->type == t_Float && v->var) return (double)*(float *)v->var;
    if (v) def = v->def;
    if (!def) def = xs_defaults_lookup(res_name);
    return def ? atof(def) : 0.0;
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

XImage *XCreateImage(Display *dpy, Visual *visual,
                     unsigned int depth, int format,
                     int offset, char *data,
                     unsigned int width, unsigned int height,
                     int bitmap_pad, int bytes_per_line)
{
    XImage *xi;
    int stride;
    (void)dpy; (void)visual; (void)format; (void)offset; (void)bitmap_pad;

    xi = (XImage *)calloc(1, sizeof(*xi));
    if (!xi) return NULL;

    stride = bytes_per_line;
    if (stride <= 0) {
        unsigned int bytes_per_pixel = (depth + 7u) / 8u;
        if (bytes_per_pixel == 0) bytes_per_pixel = 4;
        stride = (int)(width * bytes_per_pixel);
    }

    xi->width = (int)width;
    xi->height = (int)height;
    xi->bytes_per_line = stride;
    xi->data = data;
    xi->depth = (int)depth;
    xi->bits_per_pixel = (int)depth;
    return xi;
}

Bool XQueryPointer(Display *dpy, Window window,
                   Window *root_return,
                   Window *child_return,
                   int *root_x_return,
                   int *root_y_return,
                   int *win_x_return,
                   int *win_y_return,
                   unsigned int *mask_return)
{
    int x = g_harness_width / 2;
    int y = g_harness_height / 2;
    (void)dpy;

    if (root_return) *root_return = window;
    if (child_return) *child_return = 0;
    if (root_x_return) *root_x_return = x;
    if (root_y_return) *root_y_return = y;
    if (win_x_return) *win_x_return = x;
    if (win_y_return) *win_y_return = y;
    if (mask_return) *mask_return = 0;
    return True;
}

void load_texture_async(Screen *screen, Window window, GLXContext glx_context,
                        int x, int y, Bool mipmap_p, GLuint texid,
                        void (*callback)(const char *filename,
                                         XRectangle *geometry,
                                         int image_width,
                                         int image_height,
                                         int texture_width,
                                         int texture_height,
                                         void *closure),
                        void *closure)
{
    enum { TEX_W = 64, TEX_H = 64 };
    unsigned char pixels[TEX_W * TEX_H * 4];
    XRectangle geom;
    int px, py;
    (void)screen; (void)window; (void)glx_context; (void)x; (void)y;

    for (py = 0; py < TEX_H; py++) {
        for (px = 0; px < TEX_W; px++) {
            size_t off = ((size_t)py * TEX_W + (size_t)px) * 4;
            int checker = ((px / 8) + (py / 8)) & 1;
            pixels[off + 0] = checker ? 0xd0 : 0x60;
            pixels[off + 1] = checker ? 0xe0 : 0x70;
            pixels[off + 2] = checker ? 0xf0 : 0x90;
            pixels[off + 3] = 0xff;
        }
    }

    glBindTexture(GL_TEXTURE_2D, texid);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_W, TEX_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (mipmap_p)
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);

    geom.x = 0;
    geom.y = 0;
    geom.width = TEX_W;
    geom.height = TEX_H;
    if (callback)
        callback(NULL, &geom, TEX_W, TEX_H, TEX_W, TEX_H, closure);
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

/* make_random_colormap (utils/colors.c) -- fills *colors with ncolors
 * random entries. bright_p picks from a bright-saturated HSV band;
 * otherwise each channel is independently random. screen/visual/cmap
 * are vestigial; allocate_p / writable_pP / verbose_p are ignored.
 *
 * Vendored hacks reach this through colors.h. The implementations of
 * this and make_smooth_colormap intentionally diverge from upstream
 * only in their handling of X colormap allocation, which doesn't apply
 * to a GL4ES pipeline: there's no X server to allocate cells in.
 */
void make_random_colormap(Screen *screen, Visual *visual, Colormap cmap,
                          XColor *colors, int *ncolorsP,
                          Bool bright_p, Bool allocate_p,
                          Bool *writable_pP, Bool verbose_p)
{
    int n, i;

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP; (void) verbose_p;

    if (!colors || !ncolorsP) return;
    n = *ncolorsP;
    if (n <= 0) return;

    for (i = 0; i < n; i++) {
        colors[i].flags = 0;
        colors[i].pad   = 0;
        colors[i].pixel = (unsigned long) i;
        if (bright_p) {
            int H = random() % 360;
            double S = ((double) (random() % 70) + 30) / 100.0;
            double V = ((double) (random() % 34) + 66) / 100.0;
            xs_hsv_to_rgb16((double) H, S, V,
                            &colors[i].red,
                            &colors[i].green,
                            &colors[i].blue);
        } else {
            colors[i].red   = (unsigned short) (random() & 0xFFFF);
            colors[i].green = (unsigned short) (random() & 0xFFFF);
            colors[i].blue  = (unsigned short) (random() & 0xFFFF);
        }
    }
}

void make_color_ramp(Screen *screen, Visual *visual, Colormap cmap,
                     int h1, double s1, double v1,
                     int h2, double s2, double v2,
                     XColor *colors, int *ncolorsP,
                     Bool closed_p,
                     Bool allocate_p,
                     Bool *writable_pP)
{
    int n, i, limit;

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP;

    if (!colors || !ncolorsP) return;
    n = *ncolorsP;
    if (n <= 0) return;

    limit = closed_p ? (n / 2 + 1) : n;
    if (limit <= 1) {
        xs_hsv_to_rgb16(h1, s1, v1, &colors[0].red, &colors[0].green, &colors[0].blue);
        colors[0].pixel = 0;
        colors[0].flags = 0;
        colors[0].pad = 0;
        return;
    }

    for (i = 0; i < limit; i++) {
        double t = (double)i / (double)(limit - 1);
        double dh = (double)h2 - (double)h1;
        double h;
        if (dh > 180.0) dh -= 360.0;
        if (dh < -180.0) dh += 360.0;
        h = (double)h1 + dh * t;
        xs_hsv_to_rgb16(h,
                        s1 + (s2 - s1) * t,
                        v1 + (v2 - v1) * t,
                        &colors[i].red, &colors[i].green, &colors[i].blue);
        colors[i].pixel = (unsigned long)i;
        colors[i].flags = 0;
        colors[i].pad = 0;
    }

    if (closed_p) {
        for (i = limit; i < n; i++) {
            int src = limit - 2 - (i - limit);
            if (src < 0) src = 0;
            colors[i] = colors[src];
            colors[i].pixel = (unsigned long)i;
        }
    }
}

void make_color_loop(Screen *screen, Visual *visual, Colormap cmap,
                     int h1, double s1, double v1,
                     int h2, double s2, double v2,
                     int h3, double s3, double v3,
                     XColor *colors, int *ncolorsP,
                     Bool allocate_p,
                     Bool *writable_pP)
{
    int n, i;
    const int hues[4] = { h1, h2, h3, h1 };
    const double sats[4] = { s1, s2, s3, s1 };
    const double vals[4] = { v1, v2, v3, v1 };

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP;

    if (!colors || !ncolorsP) return;
    n = *ncolorsP;
    if (n <= 0) return;

    for (i = 0; i < n; i++) {
        double pos = ((double)i * 3.0) / (double)n;
        int seg = (int)pos;
        double t = pos - seg;
        double dh, h;
        if (seg > 2) seg = 2;
        dh = (double)hues[seg + 1] - (double)hues[seg];
        if (dh > 180.0) dh -= 360.0;
        if (dh < -180.0) dh += 360.0;
        h = (double)hues[seg] + dh * t;
        xs_hsv_to_rgb16(h,
                        sats[seg] + (sats[seg + 1] - sats[seg]) * t,
                        vals[seg] + (vals[seg + 1] - vals[seg]) * t,
                        &colors[i].red, &colors[i].green, &colors[i].blue);
        colors[i].pixel = (unsigned long)i;
        colors[i].flags = 0;
        colors[i].pad = 0;
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

#define NCZ_TRACKBALL_POOL_SIZE 256
static struct trackball_state xs_trackball_pool[NCZ_TRACKBALL_POOL_SIZE];
static int                  xs_trackball_used[NCZ_TRACKBALL_POOL_SIZE];

trackball_state *gltrackball_init(int ignore_device_rotation_p)
{
    /* Vendored xscreensaver hacks free the pointer returned here
     * (jigsaw.c:1523 does `free(jc->trackball)` directly; most others
     * use the gltrackball_free wrapper). Returning a static singleton
     * makes the free a bad-free under AddressSanitizer and corrupts
     * malloc metadata in release builds. Hand out a heap-resident
     * struct from a small pool so the caller can free it cleanly.
     *
     * The pool exists so the screenhack's per-init/per-free lifecycle
     * is bounded even though we don't actually trackball-rotate
     * anything; handing out a singleton would re-introduce the same
     * bug if the caller ever called free() on it.
     * NCZ_TRACKBALL_POOL_SIZE is sized generously so a 90-binary
     * validation sweep doesn't run out. */
    (void) ignore_device_rotation_p;
    for (int i = 0; i < NCZ_TRACKBALL_POOL_SIZE; i++) {
        if (!xs_trackball_used[i]) {
            xs_trackball_used[i] = 1;
            memset(&xs_trackball_pool[i], 0, sizeof xs_trackball_pool[i]);
            return &xs_trackball_pool[i];
        }
    }
    /* Out of slots — fail loudly rather than hand back a singleton
     * that the caller will then free. */
    fprintf(stderr,
            "xscreensaver_compat: gltrackball_init pool exhausted "
            "(NCZ_TRACKBALL_POOL_SIZE=%d); failing\n",
            NCZ_TRACKBALL_POOL_SIZE);
    abort();
}

void gltrackball_free(trackball_state *ts) {
    if (!ts) return;
    /* Return the slot to the pool. Direct free() (jigsaw.c:1523 does
     * this) is not valid because the pointer is pool-resident, not
     * malloc'd. gltrackball_free is the supported release path. */
    ptrdiff_t idx = ts - xs_trackball_pool;
    if (idx >= 0 && idx < NCZ_TRACKBALL_POOL_SIZE) {
        xs_trackball_used[idx] = 0;
        memset(ts, 0, sizeof *ts);
    } else {
        /* Pointer not from our pool — refuse to free to avoid
         * corrupting unrelated malloc metadata. */
        fprintf(stderr,
                "xscreensaver_compat: gltrackball_free called on "
                "pointer 0x%lx not in the trackball pool; leaking\n",
                (unsigned long)(uintptr_t)ts);
    }
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

void gltrackball_stop(trackball_state *ts) { (void) ts; }

void gltrackball_get_quaternion(trackball_state *ts, float q[4])
{
    (void) ts;
    /* Identity quaternion -- no accumulated rotation. */
    if (!q) return;
    q[0] = 0.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 1.0f;
}

/* --- screenhack_usleep (usleep.h) ------------------------------------- *
 *
 * usleep() was removed from POSIX in 2008 and glibc flags it
 * _XOPEN_SOURCE=500 which our _POSIX_C_SOURCE=200809L doesn't expose.
 * Vendored hacks reach for it directly (glsnake does), so we shim it
 * to nanosleep. */
#include <time.h>
void screenhack_usleep(unsigned long usecs)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)  (usecs / 1000000UL);
    ts.tv_nsec = (long) ((usecs % 1000000UL) * 1000UL);
    nanosleep(&ts, NULL);
}

/* --- texture-font stubs (texfont.h) ------------------------------------ *
 *
 * We do not compile texfont.c — the upstream ~1500 line implementation
 * pulls screenhackI.h, fps.h, xshm.h, jwxyz font APIs, GLSL utilities,
 * and ends up drawing through an Xft pipeline that does not exist on
 * this build (no X11, no FontConfig, no Pango). Vendored hacks that use
 * texture fonts (dnalogo, geodesicgears, gibson, glsnake, juggler3d,
 * mapscroller, molecule, pinion, splitflap, tangram, winduprobot,
 * fliptext, skulloop, spheremonics, unicrud) all guard their text
 * rendering with `if (font)` so a NULL return here produces a clean,
 * un-fonted hack rather than a missing-symbol link failure.
 *
 * The implementations are no-ops / identity functions; they exist only
 * to satisfy the linker.
 */
struct texture_font_data { int _placeholder; };

texture_font_data *load_texture_font(Display *dpy, char *res)
{
    (void) dpy; (void) res;
    return NULL;
}

void texture_string_metrics(texture_font_data *fd, const char *s,
                            XCharStruct *m, int *ascent, int *descent)
{
    (void) fd; (void) s;
    if (m)      memset(m, 0, sizeof(*m));
    if (ascent) *ascent = 0;
    if (descent) *descent = 0;
}

void print_texture_string(texture_font_data *fd, const char *s)
{
    (void) fd; (void) s;
}

void print_texture_label(Display *dpy, texture_font_data *fd,
                         int win_w, int win_h, int position, const char *s)
{
    (void) dpy; (void) fd; (void) win_w; (void) win_h;
    (void) position; (void) s;
}

void string_to_texture(texture_font_data *fd, const char *s,
                       XCharStruct *ext, int *tw, int *th)
{
    (void) fd; (void) s;
    if (ext) memset(ext, 0, sizeof(*ext));
    if (tw)  *tw = 0;
    if (th)  *th = 0;
}

void enable_texture_string_parameters(texture_font_data *fd)
{
    (void) fd;
}

Bool blank_character_p(texture_font_data *fd, const char *s)
{
    (void) fd; (void) s;
    return True;
}

void free_texture_font(texture_font_data *fd)
{
    (void) fd;
}

XftFont *texfont_xft(texture_font_data *fd)
{
    (void) fd;
    return NULL;
}

/* --- utf8wc.c helpers (utils/utf8wc.c) --------------------------------- *
 *
 * Vendored hacks that include utf8wc.h call these to convert UTF-8
 * input to Latin1 for XChar2b rendering. With texfont.c absent we
 * never feed the result to XDrawString16, but the hacks call them
 * unconditionally during init. utf8_to_latin1 is the only one whose
 * result is consulted (the others return values into ignored locals);
 * we keep their behaviour realistic enough to avoid surprises.
 */
char *utf8_to_latin1(const char *string, int ascii_p)
{
    /* Length-bounded malloc: copy the input and convert anything outside
     * Latin1 to '?'. ascii_p forces every non-ASCII byte to '?' too. */
    if (!string) return NULL;
    size_t n = strlen(string);
    char *out = (char *) malloc(n + 1);
    if (!out) return NULL;
    size_t i = 0, j = 0;
    while (i < n) {
        unsigned char c = (unsigned char) string[i];
        if (c < 0x80) {
            out[j++] = (char) c;
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            unsigned int cp = ((c & 0x1F) << 6) | (string[i + 1] & 0x3F);
            i += 2;
            if (ascii_p || cp > 0xFF) out[j++] = '?';
            else out[j++] = (char) cp;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
            unsigned int cp = ((c & 0x0F) << 12)
                             | ((string[i + 1] & 0x3F) << 6)
                             |  (string[i + 2] & 0x3F);
            i += 3;
            if (ascii_p || cp > 0xFF) out[j++] = '?';
            else out[j++] = (char) cp;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
            i += 4;
            out[j++] = '?';
        } else {
            i++;
            out[j++] = '?';
        }
    }
    out[j] = '\0';
    return out;
}

int utf8_encode(unsigned long uc, char *out, long length)
{
    if (!out || length <= 0) return 0;
    if (uc < 0x80) {
        if (length < 1) return 0;
        out[0] = (char) uc;
        return 1;
    } else if (uc < 0x800) {
        if (length < 2) return 0;
        out[0] = (char) (0xC0 | (uc >> 6));
        out[1] = (char) (0x80 | (uc & 0x3F));
        return 2;
    } else if (uc < 0x10000) {
        if (length < 3) return 0;
        out[0] = (char) (0xE0 | (uc >> 12));
        out[1] = (char) (0x80 | ((uc >> 6) & 0x3F));
        out[2] = (char) (0x80 | (uc & 0x3F));
        return 3;
    } else if (uc < 0x110000) {
        if (length < 4) return 0;
        out[0] = (char) (0xF0 | (uc >> 18));
        out[1] = (char) (0x80 | ((uc >> 12) & 0x3F));
        out[2] = (char) (0x80 | ((uc >> 6) & 0x3F));
        out[3] = (char) (0x80 | (uc & 0x3F));
        return 4;
    }
    return 0;
}

long utf8_decode(const unsigned char *in, long length,
                 unsigned long *unicode_ret)
{
    if (!in || length <= 0) {
        if (unicode_ret) *unicode_ret = 0;
        return 0;
    }
    unsigned char c = in[0];
    if (c < 0x80) {
        if (unicode_ret) *unicode_ret = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0 && length >= 2) {
        if (unicode_ret) *unicode_ret = ((c & 0x1F) << 6) | (in[1] & 0x3F);
        return 2;
    } else if ((c & 0xF0) == 0xE0 && length >= 3) {
        if (unicode_ret) *unicode_ret =
            ((c & 0x0F) << 12) | ((in[1] & 0x3F) << 6) | (in[2] & 0x3F);
        return 3;
    } else if ((c & 0xF8) == 0xF0 && length >= 4) {
        if (unicode_ret) *unicode_ret =
            ((c & 0x07) << 18) | ((in[1] & 0x3F) << 12)
            | ((in[2] & 0x3F) << 6) | (in[3] & 0x3F);
        return 4;
    }
    if (unicode_ret) *unicode_ret = c;
    return 1;
}

/* utf8_to_XChar2b: convert a UTF-8 string to a 2-byte-packed XChar2b
 * array. Bytes outside Latin1 are stored as '?'. The returned array
 * is malloc'd; the caller is responsible for freeing it. */
XChar2b *utf8_to_XChar2b(const char *string, int *length_ret)
{
    if (!string) {
        if (length_ret) *length_ret = 0;
        return NULL;
    }
    size_t n = strlen(string);
    XChar2b *out = (XChar2b *) malloc((n + 1) * sizeof(XChar2b));
    if (!out) {
        if (length_ret) *length_ret = 0;
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < n; ) {
        unsigned long cp = 0;
        long adv = utf8_decode((const unsigned char *) (string + i),
                               (long) (n - i), &cp);
        if (adv <= 0) { i++; continue; }
        i += adv;
        if (cp > 0xFF) cp = '?';
        out[j].byte1 = (unsigned char) (cp & 0xFF);
        out[j].byte2 = 0;
        j++;
    }
    out[j].byte1 = 0;
    out[j].byte2 = 0;
    if (length_ret) *length_ret = (int) j;
    return out;
}

char *XChar2b_to_utf8(const XChar2b *str, int *length_ret)
{
    if (!str) {
        if (length_ret) *length_ret = 0;
        return NULL;
    }
    size_t cap = 16, len = 0;
    char *out = (char *) malloc(cap);
    if (!out) {
        if (length_ret) *length_ret = 0;
        return NULL;
    }
    int i = 0;
    while (str[i].byte1 != 0 || str[i].byte2 != 0) {
        if (len + 4 >= cap) {
            cap *= 2;
            char *p = (char *) realloc(out, cap);
            if (!p) { free(out); if (length_ret) *length_ret = 0; return NULL; }
            out = p;
        }
        unsigned long cp = str[i].byte1 | ((unsigned long) str[i].byte2 << 8);
        len += (size_t) utf8_encode(cp, out + len, (long) (cap - len));
        i++;
    }
    out[len] = '\0';
    if (length_ret) *length_ret = (int) len;
    return out;
}

/* --- textclient.c stubs (utils/textclient.c) --------------------------- *
 *
 * textclient.c spawns `xscreensaver-text` and pipes bytes back. We are
 * not running an X11 session, so no helper process is available.
 * Vendored hacks that include textclient.h (fliptext, splitflap) only
 * call textclient_getc to read text characters and textclient_puts /
 * textclient_putc_event to feed input back; they ALL guard the reads
 * with NULL checks on the text_data handle. Returning NULL / EOF / True
 * makes them behave like "no live text source" -- the screensaver runs,
 * it just doesn't show scroll-in text.
 */
struct text_data { int _placeholder; };

text_data *textclient_open(Display *dpy)
{
    (void) dpy;
    return NULL;
}

void textclient_close(text_data *td)
{
    (void) td;
}

void textclient_reshape(text_data *td,
                        int pix_w, int pix_h,
                        int char_w, int char_h,
                        int max_lines)
{
    (void) td; (void) pix_w; (void) pix_h;
    (void) char_w; (void) char_h; (void) max_lines;
}

int textclient_getc(text_data *td)
{
    (void) td;
    return -1;   /* EOF -- callers treat as "no more text" */
}

Bool textclient_puts(text_data *td, const char *s)
{
    (void) td; (void) s;
    return True;
}

Bool textclient_putc_event(text_data *td, XKeyEvent *e)
{
    (void) td; (void) e;
    return True;
}

/* --- X Toolkit (Xt) shim (X11/Intrinsic.h) ----------------------------- *
 *
 * mapscroller.c uses XtAppAddInput / XtRemoveInput / the XtInput*Mask
 * constants to register a callback that watches the stdout pipe of an
 * external "xscreensaver-text" helper. We do not run that helper and
 * have no X11 display, so XtAppAddInput simply returns a sentinel
 * input id and XtRemoveInput is a no-op. The callback never fires,
 * which means mapscroller renders without scroll-in text -- still a
 * valid screensaver.
 */
XtAppContext XtDisplayToApplicationContext(Display *dpy)
{
    (void) dpy;
    return (XtAppContext) 0;
}

XtInputId XtAppAddInput(XtAppContext app,
                        int           source,
                        XtPointer     condition,
                        void        (*proc)(XtPointer, int *, XtInputId *),
                        XtPointer     closure)
{
    (void) app; (void) source; (void) condition;
    (void) proc; (void) closure;
    return (XtInputId) 1;   /* sentinel: not 0, so XtRemoveInput can match */
}

void XtRemoveInput(XtInputId id)
{
    (void) id;
}

/* --- file_to_ximage (ximage-loader.c) ---------------------------------- *
 *
 * Upstream's file_to_ximage uses GdkPixbuf to load a PNG/JPEG/GIF from
 * a filename into an XImage. We don't have GdkPixbuf; we have libpng
 * and the image_data_to_ximage() shim that takes in-memory PNG data.
 *
 * A simple stub: return NULL. mapscroller / lavalite / maze3d / pulsar /
 * gleidescope / extrusion / worldpieces / timetunnel / glplanet all
 * check the return value and skip texture rendering when it fails --
 * the rest of the hack runs unaffected. A future round could replace
 * this with a libpng-backed implementation if any of those textures
 * turn out to be visually essential. */
XImage *file_to_ximage(Display *dpy, Visual *visual, const char *filename)
{
    (void) dpy; (void) visual; (void) filename;
    return NULL;
}

/* utf8_decode_combining -- decode the next UTF-8 character, skipping
 * zero-width and combining marks that follow the base character.
 * starwars.c uses this to render one logical character at a time from
 * a UTF-8 string. Our stub ignores the combining logic and just
 * returns the first character's advance; visual quality of combining
 * marks in the scrolling text is the only thing that degrades.
 */
long utf8_decode_combining(const unsigned char *in, long length,
                           unsigned long *unicode_ret)
{
    (void) in; (void) length; (void) unicode_ret;
    /* Reuse the simpler utf8_decode; it already returns the byte
     * advance for the leading character. The "combining" aspect is
     * a no-op in this shim. */
    return utf8_decode(in, length, unicode_ret);
}

/* make_uniform_colormap (utils/colors.c) -- fills *colors with ncolors
 * entries that sweep once around the hue wheel at a fixed random
 * saturation and value. Equivalent to make_color_ramp(0..360) with
 * one less call frame on the stack.
 *
 * Vendored hacks reach this through colors.h (hilbert, kaleidocycle,
 * and probably more). The screen/visual/cmap/allocate_p/writable_pP/
 * verbose_p parameters are vestigial -- no X server to allocate cells
 * in. The implementation reuses our make_color_ramp shim. */
void make_uniform_colormap(Screen *screen, Visual *visual, Colormap cmap,
                           XColor *colors, int *ncolorsP,
                           Bool allocate_p, Bool *writable_pP,
                           Bool verbose_p)
{
    int ncolors;
    double S, V;

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP; (void) verbose_p;

    if (!colors || !ncolorsP || *ncolorsP <= 0) return;
    ncolors = *ncolorsP;

    S = ((double) (random() % 34) + 66) / 100.0;
    V = ((double) (random() % 34) + 66) / 100.0;

    make_color_ramp(screen, visual, cmap,
                    0, S, V, 359, S, V,
                    colors, &ncolors,
                    False, allocate_p, writable_pP);
}

/* --- Xft function stubs (xft.h) ---------------------------------------- *
 *
 * photopile.c pulls in xftwrap.c, which calls XftTextExtentsUtf8 and
 * XftDrawStringUtf8 for word-wrapping title text. We have no real Xft.
 * Stub all of them out: NULL returns, zero-fill extents, no-op draws.
 * The vendor hacks guard all real text rendering with `if (font)`, so
 * the no-op draws simply skip the title -- photopile's images still
 * shuffle and stack. */
XftFont *XftFontOpenXlfd(Display *dpy, int screen, const char *xlfd)
{ (void) dpy; (void) screen; (void) xlfd; return NULL; }

XftFont *XftFontOpenName(Display *dpy, int screen, const char *name)
{ (void) dpy; (void) screen; (void) name; return NULL; }

void XftFontClose(Display *dpy, XftFont *font)
{ (void) dpy; (void) font; }

Bool XftColorAllocName(Display *dpy, void *visual, Colormap cmap,
                       const char *name, XftColor *result)
{
    (void) dpy; (void) visual; (void) cmap; (void) name; (void) result;
    return False;
}

Bool XftColorAllocValue(Display *dpy, void *visual, Colormap cmap,
                        const XRenderColor *color, XftColor *result)
{
    (void) dpy; (void) visual; (void) cmap; (void) color; (void) result;
    return False;
}

void XftColorFree(Display *dpy, void *visual, Colormap cmap, XftColor *color)
{ (void) dpy; (void) visual; (void) cmap; (void) color; }

XftDraw *XftDrawCreate(Display *dpy, void *drawable, void *visual,
                       Colormap colormap)
{ (void) dpy; (void) drawable; (void) visual; (void) colormap; return NULL; }

Display *XftDrawDisplay(XftDraw *draw)
{ (void) draw; return NULL; }

void XftDrawDestroy(XftDraw *draw)
{ (void) draw; }

void XftTextExtentsUtf8(Display *dpy, XftFont *pub,
                        const unsigned char *string, int len,
                        XGlyphInfo *extents)
{
    (void) dpy; (void) pub; (void) string; (void) len;
    if (extents) memset(extents, 0, sizeof(*extents));
}

void XftDrawStringUtf8(XftDraw *draw, const XftColor *color,
                       XftFont *pub, int x, int y,
                       const unsigned char *string, int len)
{
    (void) draw; (void) color; (void) pub;
    (void) x; (void) y; (void) string; (void) len;
}

/* uc_isspace / uc_ispunct / uc_is_combining -- upstream lives in
 * utils/utf8wc.c. Used by xftwrap.c to skip whitespace/punctuation in
 * word-wrap decisions. We don't have real Unicode classification
 * tables here; the no-op approximation is fine for a shim -- the
 * text rendering is dead anyway. */
int uc_isspace(unsigned long uc)
{
    /* ASCII-only subset is sufficient for the use site. */
    return uc == ' ' || uc == '\t' || uc == '\n' || uc == '\r';
}

int uc_ispunct(unsigned long uc)
{
    (void) uc;
    return 0;
}

int uc_is_combining(unsigned long uc)
{
    (void) uc;
    return 0;
}
