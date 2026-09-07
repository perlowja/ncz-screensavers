/*
 * test-gluScaleImage: standalone sanity test for the gluScaleImage
 * shim in src/xscreensaver_compat.c.
 *
 * This test was written when the shim was added in support of the
 * timetunnel native GLES3 port (Round 12, 2026-09-07). The shim is
 * a bilinear-interpolation image resampler — see the long comment
 * block above gluScaleImage_rgba8 in src/xscreensaver_compat.c for
 * the full contract.
 *
 * Build:
 *   cd /home/jasonperlow/work-screensavers
 *   gcc -Wall -Wextra -O2 \
 *       -o /tmp/test-gluScaleImage \
 *       tools/test-gluScaleImage.c \
 *       tools/test-gluScaleImage-shim.c \
 *       -lm
 *   /tmp/test-gluScaleImage
 *
 * The "shim" file is a verbatim copy of gluScaleImage + gluScaleImage_rgba8
 * from src/xscreensaver_compat.c with the unrelated shim-stack
 * dependencies stripped (gles3_compat.h pulls in EGL/GLES3 headers
 * which we don't want to drag into a pure-CPU correctness test).
 * If you change the shim, mirror the change here.
 *
 * Exit code 0 = all 16 tests passed.
 * Exit code 1 = at least one test failed; diff is printed.
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
    /* ---------- Test 1: identity resize (src == dst) ---------- */
    {
        unsigned char src[4*4*4];
        unsigned char dst[4*4*4];
        int i;
        for (i = 0; i < 4*4; i++) {
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

    /* ---------- Test 2: 2x upsize 2x2 -> 4x4 ---------- */
    {
        /* 2x2 source, bottom-left convention:
         *   row 0 (bottom): (10,20,30,40)  (50,60,70,80)
         *   row 1 (top):    (90,100,110,120)  (130,140,150,160)
         */
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

        /* Hand-computed expectations for every pixel. See
         * tools/test-gluScaleImage-design.md for the per-pixel math. */
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

    /* ---------- Test 3: 2x downsize 4x4 -> 2x2 ---------- */
    {
        unsigned char src[4*4*4];
        int r, c;
        /* 4 quadrants: Q0=(10,20,30,40), Q1=(50,60,70,80),
         *              Q2=(90,100,110,120), Q3=(130,140,150,160). */
        for (r = 0; r < 4; r++) {
            for (c = 0; c < 4; c++) {
                int idx = (r*4 + c) * 4;
                if (r < 2 && c < 2) {       src[idx+0]=10; src[idx+1]=20; src[idx+2]=30; src[idx+3]=40; }
                else if (r < 2) {            src[idx+0]=50; src[idx+1]=60; src[idx+2]=70; src[idx+3]=80; }
                else if (c < 2) {            src[idx+0]=90; src[idx+1]=100; src[idx+2]=110; src[idx+3]=120; }
                else {                       src[idx+0]=130; src[idx+1]=140; src[idx+2]=150; src[idx+3]=160; }
            }
        }
        unsigned char dst[2*2*4];
        memset(dst, 0xCC, sizeof(dst));
        int rc = gluScaleImage(GL_RGBA,
                               4, 4, GL_UNSIGNED_BYTE, src,
                               2, 2, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == 0, "4x4->2x2 downsize returns 0");

        /* Center-of-pixel sampling for 4x4->2x2 puts each dst pixel
         * squarely inside one quadrant (sx,sy in {0.5, 2.5}); bilinear
         * weights collapse to (1,0,0,0), so dst = exact quadrant value. */
        unsigned char expected[2*2*4] = {
            10, 20, 30, 40,    50, 60, 70, 80,
            90,100,110,120,   130,140,150,160,
        };
        CHECK(memcmp(dst, expected, sizeof(expected)) == 0,
              "4x4->2x2 bilinear matches hand-computed expectations (quadrant pickup)");
        if (memcmp(dst, expected, sizeof(expected)) != 0) {
            int k;
            fprintf(stderr, "  actual dst:\n");
            for (k = 0; k < 4; k++) {
                fprintf(stderr, "    pixel %d: (%3u,%3u,%3u,%3u)\n", k,
                        dst[k*4+0], dst[k*4+1], dst[k*4+2], dst[k*4+3]);
            }
        }
    }

    /* ---------- Test 4: power-of-2 sanity for timetunnel's actual call ---------- */
    {
        unsigned char src[10*10*4];
        int i;
        for (i = 0; i < 10*10; i++) { src[i*4+0]=255; src[i*4+1]=0; src[i*4+2]=0; src[i*4+3]=255; }
        unsigned char dst[8*8*4];
        memset(dst, 0xCC, sizeof(dst));
        int rc = gluScaleImage(GL_RGBA,
                               10, 10, GL_UNSIGNED_BYTE, src,
                               8, 8, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == 0, "10x10->8x8 returns 0");
        int all_red = 1;
        for (i = 0; i < 8*8; i++) {
            if (dst[i*4+0]!=255 || dst[i*4+1]!=0 || dst[i*4+2]!=0 || dst[i*4+3]!=255) {
                all_red = 0; break;
            }
        }
        CHECK(all_red, "10x10->8x8 uniform red -> all dst pixels red");
    }

    /* ---------- Test 5: unsupported format/type rejection ---------- */
    {
        unsigned char src[4] = {1,2,3,4};
        unsigned char dst[4] = {0xCC,0xCC,0xCC,0xCC};
        int rc = gluScaleImage(GL_RGB,
                               2, 2, GL_UNSIGNED_BYTE, src,
                               2, 2, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == GLU_ERROR, "GL_RGB input rejected with GLU_ERROR");
        CHECK(memcmp(dst, ((unsigned char[]){0xCC,0xCC,0xCC,0xCC}), 4) == 0,
              "GL_RGB input did not write dst (preserved 0xCC)");
    }
    {
        unsigned char src[4] = {1,2,3,4};
        unsigned char dst[4] = {0xCC,0xCC,0xCC,0xCC};
        int rc = gluScaleImage(GL_RGBA,
                               2, 2, GL_UNSIGNED_SHORT, src,
                               2, 2, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == GLU_ERROR, "GL_UNSIGNED_SHORT input rejected with GLU_ERROR");
        CHECK(memcmp(dst, ((unsigned char[]){0xCC,0xCC,0xCC,0xCC}), 4) == 0,
              "GL_UNSIGNED_SHORT input did not write dst (preserved 0xCC)");
    }

    /* ---------- Test 6: zero/negative dimensions rejected ---------- */
    {
        unsigned char src[4] = {1,2,3,4};
        unsigned char dst[4];
        int rc = gluScaleImage(GL_RGBA,
                               0, 4, GL_UNSIGNED_BYTE, src,
                               4, 4, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == GLU_ERROR, "srcW=0 rejected with GLU_ERROR");
    }
    {
        unsigned char src[4] = {1,2,3,4};
        unsigned char dst[4];
        int rc = gluScaleImage(GL_RGBA,
                               4, 4, GL_UNSIGNED_BYTE, src,
                               4, 0, GL_UNSIGNED_BYTE, dst);
        CHECK(rc == GLU_ERROR, "dstH=0 rejected with GLU_ERROR");
    }

    /* ---------- Test 7: NULL pointers rejected ---------- */
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