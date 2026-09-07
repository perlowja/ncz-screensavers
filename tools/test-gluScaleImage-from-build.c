/* Same as tools/test-gluScaleImage.c but links against the ACTUAL
 * compiled xscreensaver_compat.c.o that the timetunnel_gles3 binary
 * uses, not a duplicated copy. This is the strongest end-to-end
 * shim-correctness check that doesn't require dlopen() of a PIE
 * executable (which Linux disallows).
 *
 * Build:
 *   cd /home/jasonperlow/work-screensavers
 *   gcc -o /tmp/test-gluScaleImage-from-build \
 *       tools/test-gluScaleImage-from-build.c \
 *       build/timetunnel_gles3.p/src_xscreensaver_compat.c.o \
 *       -lm
 *   /tmp/test-gluScaleImage-from-build
 *
 * If this passes, the shim that will actually be loaded into the
 * deployed timetunnel_gles3 binary is correct.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int gluScaleImage(unsigned int format,
                         int srcW, int srcH, unsigned int srcType,
                         const void *srcData,
                         int dstW, int dstH, unsigned int dstType,
                         void *dstData);

#define GL_RGBA          0x1908
#define GL_RGB           0x1907
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GLU_ERROR        100

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
    else          { fprintf(stdout, "ok:   %s\n", msg); } \
} while (0)

int main(void) {
    /* identity */
    {
        unsigned char src[4*4*4], dst[4*4*4];
        int i;
        for (i = 0; i < 16; i++) {
            src[i*4+0] = (unsigned char)i;
            src[i*4+1] = (unsigned char)(i*2);
            src[i*4+2] = (unsigned char)(i*3);
            src[i*4+3] = (unsigned char)(255 - i);
        }
        memset(dst, 0xCC, sizeof(dst));
        int rc = gluScaleImage(GL_RGBA,
                               4, 4, GL_UNSIGNED_BYTE, src,
                               4, 4, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == 0, "identity 4x4->4x4 returns 0");
        CHECK(memcmp(src, dst, sizeof(src)) == 0, "identity 4x4->4x4 preserves bytes");
    }
    /* 2x2->4x4 upsize, full expected matrix */
    {
        unsigned char src[2*2*4] = {
            10, 20, 30, 40,    50, 60, 70, 80,
            90,100,110,120,   130,140,150,160,
        };
        unsigned char dst[4*4*4];
        memset(dst, 0xCC, sizeof(dst));
        int rc = gluScaleImage(GL_RGBA,
                               2, 2, GL_UNSIGNED_BYTE, src,
                               4, 4, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == 0, "2x2->4x4 upsize returns 0");
        unsigned char expected[4*4*4] = {
            10, 20, 30, 40,    20, 30, 40, 50,    40, 50, 60, 70,    50, 60, 70, 80,
            30, 40, 50, 60,    40, 50, 60, 70,    60, 70, 80, 90,    70, 80, 90,100,
            70, 80, 90,100,    80, 90,100,110,   100,110,120,130,   110,120,130,140,
            90,100,110,120,   100,110,120,130,   120,130,140,150,   130,140,150,160,
        };
        CHECK(memcmp(dst, expected, sizeof(expected)) == 0,
              "2x2->4x4 bilinear matches hand-computed expectations");
        if (memcmp(dst, expected, sizeof(expected)) != 0) {
            int k;
            fprintf(stderr, "  actual dst:\n");
            for (k = 0; k < 16; k++) {
                fprintf(stderr, "    pixel %2d: (%3u,%3u,%3u,%3u)\n", k,
                        dst[k*4+0], dst[k*4+1], dst[k*4+2], dst[k*4+3]);
            }
        }
    }
    /* 10x10 -> 8x8 uniform red */
    {
        unsigned char src[10*10*4], dst[8*8*4];
        int i;
        for (i = 0; i < 100; i++) { src[i*4+0]=255; src[i*4+1]=0; src[i*4+2]=0; src[i*4+3]=255; }
        memset(dst, 0xCC, sizeof(dst));
        int rc = gluScaleImage(GL_RGBA,
                               10, 10, GL_UNSIGNED_BYTE, src,
                               8, 8, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == 0, "10x10->8x8 returns 0");
        int all_red = 1;
        for (i = 0; i < 64; i++) {
            if (dst[i*4+0]!=255 || dst[i*4+1]!=0 || dst[i*4+2]!=0 || dst[i*4+3]!=255) {
                all_red = 0; break;
            }
        }
        CHECK(all_red, "10x10->8x8 uniform red -> all dst pixels red");
    }
    /* unsupported format */
    {
        unsigned char src[4] = {1,2,3,4};
        unsigned char dst[4] = {0xCC,0xCC,0xCC,0xCC};
        int rc = gluScaleImage(GL_RGB,
                               2, 2, GL_UNSIGNED_BYTE, src,
                               2, 2, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == GLU_ERROR, "GL_RGB input rejected with GLU_ERROR");
        CHECK(memcmp(dst, ((unsigned char[]){0xCC,0xCC,0xCC,0xCC}), 4) == 0,
              "GL_RGB input did not write dst");
    }
    /* unsupported type */
    {
        unsigned char src[4] = {1,2,3,4};
        unsigned char dst[4] = {0xCC,0xCC,0xCC,0xCC};
        int rc = gluScaleImage(GL_RGBA,
                               2, 2, GL_UNSIGNED_SHORT, src,
                               2, 2, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == GLU_ERROR, "GL_UNSIGNED_SHORT input rejected with GLU_ERROR");
        CHECK(memcmp(dst, ((unsigned char[]){0xCC,0xCC,0xCC,0xCC}), 4) == 0,
              "GL_UNSIGNED_SHORT input did not write dst");
    }
    /* degenerate dims */
    {
        unsigned char src[4] = {1,2,3,4};
        unsigned char dst[4];
        int rc = gluScaleImage(GL_RGBA,
                               0, 4, GL_UNSIGNED_BYTE, src,
                               4, 4, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == GLU_ERROR, "srcW=0 rejected with GLU_ERROR");
        rc = gluScaleImage(GL_RGBA,
                           4, 4, GL_UNSIGNED_BYTE, src,
                           4, 0, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == GLU_ERROR, "dstH=0 rejected with GLU_ERROR");
    }
    /* NULL pointers */
    {
        unsigned char src[4] = {1,2,3,4};
        int rc = gluScaleImage(GL_RGBA,
                               4, 4, GL_UNSIGNED_BYTE, src,
                               4, 4, GL_UNSIGNED_BYTE, NULL);
        CHECK(rc == GLU_ERROR, "NULL dst rejected with GLU_ERROR");
        rc = gluScaleImage(GL_RGBA,
                           4, 4, GL_UNSIGNED_BYTE, NULL,
                           4, 4, GL_UNSIGNED_BYTE, src);
        CHECK(rc == GLU_ERROR, "NULL src rejected with GLU_ERROR");
    }

    fprintf(stdout, "\n%s -- %d failure(s)\n",
            failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
            failures);
    return failures == 0 ? 0 : 1;
}