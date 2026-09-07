/* Standalone shim build for sanity-testing only.
 * Pulls just gluScaleImage + gluScaleImage_rgba8 from xscreensaver_compat.c
 * by including them directly. The rest of xscreensaver_compat.c needs
 * the full GLES3 stack; we sidestep that here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int GLint;
typedef unsigned int GLenum;
typedef double GLdouble;
typedef float GLfloat;
typedef unsigned char GLubyte;

#define GL_RGBA          0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GLU_ERROR        100

/* Replicate the implementation verbatim from xscreensaver_compat.c. */
static int
gluScaleImage_rgba8 (GLint srcW, GLint srcH, const unsigned char *src,
                     GLint dstW, GLint dstH, unsigned char *dst)
{
    GLint dx, dy;

    if (srcW == dstW && srcH == dstH) {
        memcpy(dst, src, (size_t)srcW * (size_t)srcH * 4);
        return 0;
    }

    {
        const double sx_scale = (double)srcW / (double)dstW;
        const double sy_scale = (double)srcH / (double)dstH;

        for (dy = 0; dy < dstH; dy++) {
            double sy = ((double)dy + 0.5) * sy_scale - 0.5;
            int    sy0, sy1;
            double fy;

            if (sy < 0.0)              sy = 0.0;
            if (sy > (double)(srcH-1)) sy = (double)(srcH - 1);

            sy0 = (int)sy;
            fy  = sy - (double)sy0;
            sy1 = sy0 + 1;
            if (sy1 >= srcH) sy1 = srcH - 1;

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
