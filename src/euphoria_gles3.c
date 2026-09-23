/*
 * Copyright (C) 2000-2010  Terence M. Welsh
 *
 * This file is part of Euphoria.
 *
 * Euphoria is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Euphoria is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * euphoria_gles3.c — Round-15 SIMPLIFIED native GLES3 port of Terence
 * Welsh's "Euphoria" saver, sourced via erik-larsen/rss-sdl2-gles2's
 * SDL2/GLES2 wrapper around the original RSS-GLX euphoria.cpp.
 *
 * The original is a feedback-texture driven saver: it renders a
 * particle field into a feedback texture, blurs / attenuates the
 * previous frame, and additively blends the result back over the
 * current frame to create a flowing organic cloud. Three pre-baked
 * procedural textures (plasma, stringy, lines; each 256x256) are
 * superimposed with an animated grid of "knots".
 *
 * GLES3-native feedback textures (via glCopyTexSubImage2D into a
 * power-of-two RGBA8 target) work, but the full knot-grid / particle
 * / 3-texture-blend pipeline is ~1000 lines of C++. This SIMPLIFIED
 * port preserves the VISUAL ESSENCE by:
 *   - generating an animated 256x256 plasma-style texture per frame
 *     (CPU-side scalar-field update + glTexSubImage2D upload)
 *   - drawing a textured full-screen quad with a slowly drifting UV
 *     and additive blending (the feedback-loop look)
 *   - drawing N "knot" sprites (camera-facing textured quads) at
 *     positions drifting along Lissajous curves
 *
 * Upstream feedback-loop subtlety is lost; the result is a swirling
 * plasma cloud with orbiting knots. See PORTED.md §14 for the
 * "what is preserved vs simplified" call-out.
 */

#define DEFAULTS	"*delay:    20000   \n" \
			"*feedback: 95      \n" \
			"*feedbacksize: 8   \n" \
			"*knots:    12      \n" \
			"*size:     50      \n" \
			"*saturation: 100   \n" \
			"*blur:     0       \n"

#define release_euphoria 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_GL

#define DEF_FEEDBACK   "95"
#define DEF_FEEDBACKSIZE "8"
#define DEF_KNOTS      "12"
#define DEF_SIZE       "50"
#define DEF_SATURATION "100"
#define DEF_BLUR       "0"

#define NUMCONSTS 9
#define PIx2 6.28318530718f
#define TEXSIZE 256

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_feedback;
    int d_feedbacksize;
    int d_knots;
    int d_size;
    int d_saturation;
    int d_blur;

    unsigned int tex;
    float c[NUMCONSTS];
    float ct[NUMCONSTS];
    float cv[NUMCONSTS];
    float u_off, v_off;
} euphoria_configuration;

static euphoria_configuration *bps = NULL;

static int d_feedback;
static int d_feedbacksize;
static int d_knots;
static int d_size;
static int d_saturation;
static int d_blur;

static XrmOptionDescRec opts[] = {
    { "-feedback",   ".feedback",   XrmoptionSepArg, 0 },
    { "-feedbacksize",".feedbacksize",XrmoptionSepArg, 0 },
    { "-knots",      ".knots",      XrmoptionSepArg, 0 },
    { "-size",       ".size",       XrmoptionSepArg, 0 },
    { "-saturation", ".saturation", XrmoptionSepArg, 0 },
    { "-blur",       ".blur",       XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_feedback,    "feedback",    "Feedback",    DEF_FEEDBACK,    t_Int },
    { &d_feedbacksize,"feedbacksize","FeedbackSize",DEF_FEEDBACKSIZE,t_Int },
    { &d_knots,       "knots",       "Knots",       DEF_KNOTS,       t_Int },
    { &d_size,        "size",        "Size",        DEF_SIZE,        t_Int },
    { &d_saturation,  "saturation",  "Saturation",  DEF_SATURATION,  t_Int },
    { &d_blur,        "blur",        "Blur",        DEF_BLUR,        t_Int },
};

ENTRYPOINT ModeSpecOpt euphoria_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

ENTRYPOINT void
reshape_euphoria(ModeInfo *mi, int width, int height) {
    euphoria_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

ENTRYPOINT void
init_euphoria(ModeInfo *mi) {
    euphoria_configuration *bp;
    int i;

    if (!bps) {
        bps = (euphoria_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(euphoria_configuration));
        if (!bps) exit(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_feedback    = (d_feedback    < 0) ? 0 : (d_feedback    > 100 ? 100 : d_feedback);
    bp->d_feedbacksize= (d_feedbacksize< 1) ? 1 : (d_feedbacksize> 10  ? 10  : d_feedbacksize);
    bp->d_knots       = (d_knots       < 0) ? 0 : (d_knots       > 100 ? 100 : d_knots);
    bp->d_size        = (d_size        < 1) ? 1 : (d_size        > 100 ? 100 : d_size);
    bp->d_saturation  = (d_saturation  < 0) ? 0 : (d_saturation  > 100 ? 100 : d_saturation);
    bp->d_blur        = (d_blur        < 0) ? 0 : (d_blur        > 100 ? 100 : d_blur);

    glGenTextures(1, &bp->tex);
    glBindTexture(GL_TEXTURE_2D, bp->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    {
        unsigned char black[TEXSIZE * TEXSIZE * 4];
        memset(black, 0, sizeof(black));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXSIZE, TEXSIZE,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    }

    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    for (i = 0; i < NUMCONSTS; i++) {
        bp->ct[i] = frand(PIx2);
        bp->cv[i] = frand(0.0001f) + 0.0001f;
        bp->c[i] = 0.0f;
    }
    bp->u_off = 0.0f;
    bp->v_off = 0.0f;
    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_euphoria(ModeInfo *mi) {
    euphoria_configuration *bp = &bps[MI_SCREEN(mi)];
    int i, j;
    static unsigned char texbuf[TEXSIZE * TEXSIZE * 4];
    float dt = bp->frame_time;

    /* update oscillating constants */
    for (i = 0; i < NUMCONSTS; i++) {
        bp->ct[i] += bp->cv[i];
        if (bp->ct[i] > PIx2) bp->ct[i] -= PIx2;
        bp->c[i] = cosf(bp->ct[i]);
    }
    bp->u_off += dt * 0.02f * (float)bp->d_feedback / 100.0f;
    bp->v_off += dt * 0.013f * (float)bp->d_feedback / 100.0f;

    /* regenerate plasma-style texture */
    {
        float sat = (float)bp->d_saturation / 100.0f;
        float zoom = 0.04f * (float)bp->d_size / 50.0f;
        for (i = 0; i < TEXSIZE; i++) {
            for (j = 0; j < TEXSIZE; j++) {
                float u = (float)i / (float)TEXSIZE * 6.2831853f * zoom;
                float v = (float)j / (float)TEXSIZE * 6.2831853f * zoom;
                float p = 0.5f + 0.5f * sinf(u * bp->c[0] + v * bp->c[1] +
                                              bp->ct[2] * 2.0f)
                         * cosf(u * bp->c[3] - v * bp->c[4] + bp->ct[5] * 1.7f);
                p = p * p * sat;
                if (p > 1.0f) p = 1.0f;
                unsigned char g = (unsigned char)(255.0f * p);
                int idx = (i * TEXSIZE + j) * 4;
                /* color modulation by constant #6 (red), #7 (green), #8 (blue) */
                texbuf[idx + 0] = (unsigned char)(g * (0.5f + 0.5f * bp->c[6]));
                texbuf[idx + 1] = (unsigned char)(g * (0.5f + 0.5f * bp->c[7]));
                texbuf[idx + 2] = (unsigned char)(g * (0.5f + 0.5f * bp->c[8]));
                texbuf[idx + 3] = (unsigned char)(g * 0.9f);
            }
        }
        glBindTexture(GL_TEXTURE_2D, bp->tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXSIZE, TEXSIZE,
                        GL_RGBA, GL_UNSIGNED_BYTE, texbuf);
    }

    /* full-screen fade overlay (if blur enabled) */
    if (bp->d_blur) {
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
            glLoadIdentity();
            glOrtho(0.0, 1.0, 0.0, 1.0, 1.0, -1.0);
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
                glLoadIdentity();
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glColor4f(0.0f, 0.0f, 0.0f, 0.5f - (float)bp->d_blur * 0.0049f);
                glBegin(GL_TRIANGLE_STRIP);
                    glVertex3f(0.0f, 0.0f, 0.0f);
                    glVertex3f(1.0f, 0.0f, 0.0f);
                    glVertex3f(0.0f, 1.0f, 0.0f);
                    glVertex3f(1.0f, 1.0f, 0.0f);
                glEnd();
            glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
    } else {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBlendFunc(GL_ONE, GL_ONE);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* draw drifting textured quad as the feedback background */
    glColor3f(1.0f, 1.0f, 1.0f);
    {
        float u0 = bp->u_off;
        float v0 = bp->v_off;
        glBindTexture(GL_TEXTURE_2D, bp->tex);
        glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(u0,        v0);        glVertex2f(-1.0f, -1.0f);
            glTexCoord2f(u0 + 4.0f, v0);        glVertex2f( 1.0f, -1.0f);
            glTexCoord2f(u0,        v0 + 4.0f); glVertex2f(-1.0f,  1.0f);
            glTexCoord2f(u0 + 4.0f, v0 + 4.0f); glVertex2f( 1.0f,  1.0f);
        glEnd();
    }

    /* draw knots (camera-facing textured sprites) along Lissajous curves */
    for (i = 0; i < bp->d_knots; i++) {
        float fi = (float)i / (float)(bp->d_knots > 0 ? bp->d_knots : 1);
        float t = bp->ct[0] * 0.5f + fi * 6.2831853f;
        float x = 0.6f * sinf(t * 3.0f);
        float y = 0.6f * sinf(t * 2.0f + 1.0f);
        float z = 0.0f;
        float hue = fi;
        float r, g, b;
        float temp1 = 0.5f, temp2 = 1.0f;
        /* minimal hsl2rgb */
        if (temp2 == 0) { r = g = b = temp1; }
        else {
            float h = hue, s = 1.0f, l = 0.5f;
            float q1 = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
            float q2 = 2.0f * l - q1;
            float tr = h + 1.0f/3.0f; if (tr > 1.0f) tr -= 1.0f;
            float tg = h;
            float tb = h - 1.0f/3.0f; if (tb < 0.0f) tb += 1.0f;
            r = (tr < 1.0f/6.0f) ? q2 + (q1-q2)*6.0f*tr : (tr < 0.5f) ? q1 : (tr < 2.0f/3.0f) ? q2 + (q1-q2)*(2.0f/3.0f-tr)*6.0f : q2;
            g = (tg < 1.0f/6.0f) ? q2 + (q1-q2)*6.0f*tg : (tg < 0.5f) ? q1 : (tg < 2.0f/3.0f) ? q2 + (q1-q2)*(2.0f/3.0f-tg)*6.0f : q2;
            b = (tb < 1.0f/6.0f) ? q2 + (q1-q2)*6.0f*tb : (tb < 0.5f) ? q1 : (tb < 2.0f/3.0f) ? q2 + (q1-q2)*(2.0f/3.0f-tb)*6.0f : q2;
        }
        glPushMatrix();
            glTranslatef(x, y, z);
            float sz = 0.08f + 0.04f * sinf(t * 5.0f);
            glScalef(sz, sz, sz);
            glColor3f(r, g, b);
            glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
                glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
                glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
            glEnd();
        glPopMatrix();
    }

    glFinish();
}

ENTRYPOINT void
free_euphoria(ModeInfo *mi) {
    euphoria_configuration *bp = &bps[MI_SCREEN(mi)];
    if (bp->tex) glDeleteTextures(1, &bp->tex);
}

ENTRYPOINT Bool
euphoria_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Euphoria", euphoria, euphoria)

#endif /* USE_GL */
