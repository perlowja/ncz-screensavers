/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Plasma.
 *
 * Plasma is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Plasma is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * plasma_gles3.c — Round-15 native GLES3 port of Terence Welsh's
 * "Plasma" saver, sourced via erik-larsen/rss-sdl2-gles2's
 * SDL2/GLES2 wrapper around the original RSS-GLX plasma.cpp.
 *
 * Algorithm preserved: a 64x64 grid of "plasma" cells is updated every
 * frame using 18 oscillating constants c[0..17] (driven by random
 * velocities). Each cell's RGB color depends on its grid position and
 * the previous frame's RGB at that cell, clamped to a maximum per-step
 * delta so the colors can't change too fast. The grid is rendered as a
 * single textured triangle strip covering the screen.
 *
 * Texture upload path: the original uses gluBuild2DMipmaps to upload a
 * TEXSIZE x TEXSIZE luminance/alpha texture. GLES3 doesn't expose
 * gluBuild2DMipmaps directly; we use glTexImage2D with a luminance
 * internal format. Mipmap generation is requested with
 * GL_GENERATE_MIPMAP (well-supported on Mali/Panthor).
 */

#define DEFAULTS	"*delay:   20000   \n" \
			"*zoom:    20      \n" \
			"*focus:   30      \n" \
			"*speed:   5       \n" \
			"*resolution:64    \n"

#define release_plasma 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#ifdef USE_GL

#define DEF_ZOOM       "20"
#define DEF_FOCUS      "30"
#define DEF_SPEED      "5"
#define DEF_RESOLUTION "64"

#define PIx2 6.28318530718f
#define NUMCONSTS 18
#define TEXSIZE 1024

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_zoom;
    int d_focus;
    int d_speed;
    int d_resolution;

    /* Grid of colors + positions, kept at full TEXSIZE for upload
     * simplicity; the "active" sub-region is plasmasize x plasmasize/aspect. */
    float position[TEXSIZE][TEXSIZE][2];
    float plasma[TEXSIZE][TEXSIZE][3];
    float plasmamap[TEXSIZE * TEXSIZE * 3];
    unsigned int tex;

    float c[NUMCONSTS];
    float ct[NUMCONSTS];
    float cv[NUMCONSTS];
    int plasmasize;
} plasma_configuration;

static plasma_configuration *bps = NULL;

static int d_zoom;
static int d_focus;
static int d_speed;
static int d_resolution;

static XrmOptionDescRec opts[] = {
    { "-zoom",       ".zoom",       XrmoptionSepArg, 0 },
    { "-focus",      ".focus",      XrmoptionSepArg, 0 },
    { "-speed",      ".speed",      XrmoptionSepArg, 0 },
    { "-resolution", ".resolution", XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_zoom,       "zoom",       "Zoom",       DEF_ZOOM,       t_Int },
    { &d_focus,      "focus",      "Focus",      DEF_FOCUS,      t_Int },
    { &d_speed,      "speed",      "Speed",      DEF_SPEED,      t_Int },
    { &d_resolution, "resolution", "Resolution", DEF_RESOLUTION, t_Int },
};

ENTRYPOINT ModeSpecOpt plasma_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

static double seconds_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1.0e-6;
}

static inline float fabstrunc(float f) {
    if (f >= 0.0f) return (f <= 1.0f ? f : 1.0f);
    return (f >= -1.0f ? -f : 1.0f);
}

ENTRYPOINT void
reshape_plasma(ModeInfo *mi, int width, int height) {
    plasma_configuration *bp = &bps[MI_SCREEN(mi)];
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
init_plasma(ModeInfo *mi) {
    int i, j;
    plasma_configuration *bp;

    if (!bps) {
        bps = (plasma_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(plasma_configuration));
        if (!bps) ncz_harness_die(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_zoom       = (d_zoom       < 1) ? 1 : (d_zoom       > 100 ? 100 : d_zoom);
    bp->d_focus      = (d_focus      < 1) ? 1 : (d_focus      > 100 ? 100 : d_focus);
    bp->d_speed      = (d_speed      < 1) ? 1 : (d_speed      > 100 ? 100 : d_speed);
    bp->d_resolution = (d_resolution < 1) ? 1 : (d_resolution > 128 ? 128 : d_resolution);
    bp->plasmasize   = bp->d_resolution * 4;  /* 1..128 -> 4..512 */

    /* initialize positions: each cell at fixed (i,j) grid coordinates */
    for (i = 0; i < TEXSIZE; i++) {
        for (j = 0; j < TEXSIZE; j++) {
            bp->position[i][j][0] = (float)i;
            bp->position[i][j][1] = (float)j;
            bp->plasma[i][j][0] = 0.0f;
            bp->plasma[i][j][1] = 0.0f;
            bp->plasma[i][j][2] = 0.0f;
        }
    }

    /* initialize oscillating constants */
    for (i = 0; i < NUMCONSTS; i++) {
        bp->ct[i] = frand(PIx2);
        bp->cv[i] = frand(0.05f) + 0.01f;
        bp->c[i]  = 0.0f;
    }

    /* allocate texture */
    glGenTextures(1, (GLuint *)&bp->tex);
    glBindTexture(GL_TEXTURE_2D, bp->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* initial empty upload so the texture object is complete */
    {
        unsigned char zero[TEXSIZE * TEXSIZE * 3];
        memset(zero, 0, sizeof(zero));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXSIZE, TEXSIZE,
                     0, GL_RGB, GL_UNSIGNED_BYTE, zero);
    }

    glEnable(GL_TEXTURE_2D);

    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_plasma(ModeInfo *mi) {
    plasma_configuration *bp = &bps[MI_SCREEN(mi)];
    int i, j, index;
    float rgb[3];
    float temp;
    static float focus = 0.0f;
    static float maxdiff = 0.0f;
    int plasmasize;
    int grid_w;

    if (bp->plasmasize == 0) return;
    plasmasize = bp->plasmasize;
    grid_w = (int)((float)plasmasize / bp->aspect_ratio);
    if (grid_w < 1) grid_w = 1;
    if (grid_w > plasmasize) grid_w = plasmasize;

    if (focus == 0.0f)
        focus = (float)bp->d_focus / 50.0f + 0.3f;
    if (maxdiff == 0.0f)
        maxdiff = 0.004f * (float)bp->d_speed;

    /* update oscillating constants */
    for (i = 0; i < NUMCONSTS; i++) {
        bp->ct[i] += bp->cv[i] * bp->frame_time * 60.0f;
        if (bp->ct[i] > PIx2) bp->ct[i] -= PIx2;
        bp->c[i] = sinf(bp->ct[i]) * focus;
    }

    /* update plasma cells: keep to the active sub-rectangle */
    for (i = 0; i < plasmasize; i++) {
        for (j = 0; j < grid_w; j++) {
            rgb[0] = bp->plasma[i][j][0];
            rgb[1] = bp->plasma[i][j][1];
            rgb[2] = bp->plasma[i][j][2];
            bp->plasma[i][j][0] = 0.7f *
                (bp->c[0] * bp->position[i][j][0] + bp->c[1] * bp->position[i][j][1]
                 + bp->c[2] * (bp->position[i][j][0] * bp->position[i][j][0] + 1.0f)
                 + bp->c[3] * bp->position[i][j][0] * bp->position[i][j][1]
                 + bp->c[4] * rgb[1] + bp->c[5] * rgb[2]);
            bp->plasma[i][j][1] = 0.7f *
                (bp->c[6] * bp->position[i][j][0] + bp->c[7] * bp->position[i][j][1]
                 + bp->c[8] * bp->position[i][j][0] * bp->position[i][j][0]
                 + bp->c[9] * (bp->position[i][j][1] * bp->position[i][j][1] - 1.0f)
                 + bp->c[10] * rgb[0] + bp->c[11] * rgb[2]);
            bp->plasma[i][j][2] = 0.7f *
                (bp->c[12] * bp->position[i][j][0] + bp->c[13] * bp->position[i][j][1]
                 + bp->c[14] * (1.0f - bp->position[i][j][0] * bp->position[i][j][1])
                 + bp->c[15] * bp->position[i][j][1] * bp->position[i][j][1]
                 + bp->c[16] * rgb[0] + bp->c[17] * rgb[1]);
            /* clamp delta */
            temp = bp->plasma[i][j][0] - rgb[0];
            if (temp >  maxdiff) bp->plasma[i][j][0] = rgb[0] + maxdiff;
            if (temp < -maxdiff) bp->plasma[i][j][0] = rgb[0] - maxdiff;
            temp = bp->plasma[i][j][1] - rgb[1];
            if (temp >  maxdiff) bp->plasma[i][j][1] = rgb[1] + maxdiff;
            if (temp < -maxdiff) bp->plasma[i][j][1] = rgb[1] - maxdiff;
            temp = bp->plasma[i][j][2] - rgb[2];
            if (temp >  maxdiff) bp->plasma[i][j][2] = rgb[2] + maxdiff;
            if (temp < -maxdiff) bp->plasma[i][j][2] = rgb[2] - maxdiff;
            /* Map the evolving field through a classic demoscene palette.
             * Keeping the three phase-shifted sine channels avoids the flat
             * cyan/green cast produced by displaying the raw field values. */
            {
                float x = (float)i / (float)plasmasize * PIx2;
                float y = (float)j / (float)grid_w * PIx2;
                float dx = x - 3.14159265f + sinf(bp->ct[4]) * 0.8f;
                float dy = y - 3.14159265f + cosf(bp->ct[5]) * 0.8f;
                float wave = (sinf(x * 2.0f + bp->ct[0]) +
                              sinf(y * 3.0f - bp->ct[1]) +
                              sinf((x + y) * 1.7f + bp->ct[2]) +
                              sinf(sqrtf(dx * dx + dy * dy) * 3.5f -
                                   bp->ct[3]));
                float phase = wave * 1.15f + bp->ct[6] * 0.45f;
                float red   = 0.52f + 0.48f * sinf(phase);
                float green = 0.42f + 0.40f * sinf(phase + 2.05f);
                float blue  = 0.50f + 0.46f * sinf(phase + 4.20f);

                /* Slightly deepen the troughs while preserving hot highlights. */
                red   = red   * red   * 0.85f + red   * 0.15f;
                green = green * green * 0.85f + green * 0.15f;
                blue  = blue  * blue  * 0.85f + blue  * 0.15f;

                /* pack into the linear texture array */
                index = (i * TEXSIZE + j) * 3;
                bp->plasmamap[index + 0] = fabstrunc(red);
                bp->plasmamap[index + 1] = fabstrunc(green);
                bp->plasmamap[index + 2] = fabstrunc(blue);
            }
        }
    }

    /* upload sub-rect of the texture */
    glBindTexture(GL_TEXTURE_2D, bp->tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, TEXSIZE);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, grid_w, plasmasize,
                    GL_RGB, GL_FLOAT, bp->plasmamap);

    /* draw a screen-filling triangle strip */
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 1.0f, 1.0f);
    {
        float zoom = (float)bp->d_zoom / 25.0f;
        if (zoom < 0.05f) zoom = 0.05f;
        float texright = (float)(plasmasize - 1) / (float)TEXSIZE;
        float textop   = (float)(grid_w - 1) / (float)TEXSIZE;
        float w = 1.0f * zoom;
        float h = (float)grid_w / (float)plasmasize * zoom;
        glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0.0f,  0.0f); glVertex2f(-w, -h);
            glTexCoord2f(texright, 0.0f); glVertex2f( w, -h);
            glTexCoord2f(0.0f,  textop); glVertex2f(-w,  h);
            glTexCoord2f(texright, textop); glVertex2f( w,  h);
        glEnd();
    }

    glFinish();
}

ENTRYPOINT void
free_plasma(ModeInfo *mi) {
    plasma_configuration *bp = &bps[MI_SCREEN(mi)];
    if (bp->tex) { glDeleteTextures(1, (GLuint *)&bp->tex); bp->tex = 0; }
}

ENTRYPOINT Bool
plasma_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Plasma", plasma, plasma)

#endif /* USE_GL */
