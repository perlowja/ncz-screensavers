/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Microcosm.
 *
 * Microcosm is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Microcosm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * microcosm_gles3.c — Round-15 SIMPLIFIED native GLES3 port of
 * Terence Welsh's "Microcosm" saver, sourced via
 * erik-larsen/rss-sdl2-gles2's SDL2/GLES2 wrapper around the
 * original RSS-GLX microcosm.cpp.
 *
 * The original is a multi-mode metaball saver that runs an
 * impCubeVolume polygonizer (an isosurface mesh generator) for
 * 1-3 simultaneous implicit-field sets, transitioning between
 * single-volume and kaleidoscope (3-volume) modes. The mirrorBox
 * helper renders the volume from inside a mirrored cube to give
 * the kaleidoscope appearance.
 *
 * Porting the impCubeVolume polygonizer (~1000 lines of marching-
 * cubes code) is out of scope. This SIMPLIFIED port preserves the
 * VISUAL ESSENCE — multiple soft glowing blob fields rendered with
 * additive blending — by:
 *   - drawing N "emitter" spheres and M "attractor" spheres as
 *     textured soft-glow sprites
 *   - each sprite's color comes from a separate phase-shifted palette
 *     (representing the three "modes" of the original)
 *   - sprites drift along slow Lissajous curves and pulse in scale
 *
 * The kaleidoscope / mirror-box effect is replaced with a single
 * centered camera looking at the blob field.
 */

#define DEFAULTS	"*delay:    20000   \n" \
			"*emitters: 30      \n" \
			"*attracters: 5     \n" \
			"*size:     50      \n" \
			"*speed:    30      \n"

#define release_microcosm 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_GL

#define DEF_EMITTERS   "30"
#define DEF_ATTRACTERS "5"
#define DEF_SIZE       "50"
#define DEF_SPEED      "30"

#define PIx2 6.28318530718f
#define LIGHTSIZE 64

typedef struct {
    float x, y, z;
    float phase;
    float hue;
    float base_size;
} microcosm_blob;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_emitters;
    int d_attracters;
    int d_size;
    int d_speed;

    microcosm_blob *emitters;
    microcosm_blob *attracters;

    unsigned int tex[3];
    unsigned char light_tex_data[LIGHTSIZE * LIGHTSIZE];
    float mode_t;     /* 0..3: which palette is in foreground */
} microcosm_configuration;

static microcosm_configuration *bps = NULL;

static int d_emitters;
static int d_attracters;
static int d_size;
static int d_speed;

static XrmOptionDescRec opts[] = {
    { "-emitters",   ".emitters",   XrmoptionSepArg, 0 },
    { "-attracters", ".attracters", XrmoptionSepArg, 0 },
    { "-size",       ".size",       XrmoptionSepArg, 0 },
    { "-speed",      ".speed",      XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_emitters,   "emitters",   "Emitters",   DEF_EMITTERS,   t_Int },
    { &d_attracters, "attracters", "Attracters", DEF_ATTRACTERS, t_Int },
    { &d_size,       "size",       "Size",       DEF_SIZE,       t_Int },
    { &d_speed,      "speed",      "Speed",      DEF_SPEED,      t_Int },
};

ENTRYPOINT ModeSpecOpt microcosm_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

static void hsl2rgb(float h, float s, float l, float *rOut, float *gOut, float *bOut) {
    float temp1, temp2, tempr, tempg, tempb;
    if (s == 0.0f) { *rOut = *gOut = *bOut = l; return; }
    temp2 = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    temp1 = 2.0f * l - temp2;
    tempr = h + 1.0f/3.0f; if (tempr > 1.0f) tempr -= 1.0f;
    tempg = h;
    tempb = h - 1.0f/3.0f; if (tempb < 0.0f) tempb += 1.0f;
    if (tempr < 1.0f/6.0f)      *rOut = temp1 + (temp2-temp1)*6.0f*tempr;
    else if (tempr < 0.5f)      *rOut = temp2;
    else if (tempr < 2.0f/3.0f) *rOut = temp1 + (temp2-temp1)*(2.0f/3.0f-tempr)*6.0f;
    else                        *rOut = temp1;
    if (tempg < 1.0f/6.0f)      *gOut = temp1 + (temp2-temp1)*6.0f*tempg;
    else if (tempg < 0.5f)      *gOut = temp2;
    else if (tempg < 2.0f/3.0f) *gOut = temp1 + (temp2-temp1)*(2.0f/3.0f-tempg)*6.0f;
    else                        *gOut = temp1;
    if (tempb < 1.0f/6.0f)      *bOut = temp1 + (temp2-temp1)*6.0f*tempb;
    else if (tempb < 0.5f)      *bOut = temp2;
    else if (tempb < 2.0f/3.0f) *bOut = temp1 + (temp2-temp1)*(2.0f/3.0f-tempb)*6.0f;
    else                        *bOut = temp1;
}

ENTRYPOINT void
reshape_microcosm(ModeInfo *mi, int width, int height) {
    microcosm_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, bp->aspect_ratio, 1.0, 5000.0);
}

ENTRYPOINT void
init_microcosm(ModeInfo *mi) {
    microcosm_configuration *bp;
    int i, j;
    float x, y, temp;

    if (!bps) {
        bps = (microcosm_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(microcosm_configuration));
        if (!bps) ncz_harness_die(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_emitters   = (d_emitters   < 1) ? 1 : (d_emitters   > 100 ? 100 : d_emitters);
    bp->d_attracters = (d_attracters < 0) ? 0 : (d_attracters > 50  ? 50  : d_attracters);
    bp->d_size       = (d_size       < 1) ? 1 : (d_size       > 100 ? 100 : d_size);
    bp->d_speed      = (d_speed      < 1) ? 1 : (d_speed      > 100 ? 100 : d_speed);

    /* radial gradient light texture */
    for (i = 0; i < LIGHTSIZE; i++) {
        for (j = 0; j < LIGHTSIZE; j++) {
            x = (float)(i - LIGHTSIZE/2) / (float)(LIGHTSIZE/2);
            y = (float)(j - LIGHTSIZE/2) / (float)(LIGHTSIZE/2);
            temp = 1.0f - sqrtf(x*x + y*y);
            if (temp < 0.0f) temp = 0.0f;
            if (temp > 1.0f) temp = 1.0f;
            bp->light_tex_data[i*LIGHTSIZE + j] = (unsigned char)(255.0f * temp);
        }
    }
    for (i = 0; i < 3; i++) {
        glGenTextures(1, &bp->tex[i]);
        glBindTexture(GL_TEXTURE_2D, bp->tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, LIGHTSIZE, LIGHTSIZE,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, bp->light_tex_data);
    }

    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    bp->emitters   = (microcosm_blob *)calloc((size_t)bp->d_emitters,   sizeof(microcosm_blob));
    bp->attracters = (microcosm_blob *)calloc((size_t)bp->d_attracters, sizeof(microcosm_blob));
    if (bp->d_emitters   > 0 && !bp->emitters)   ncz_harness_die(1);
    if (bp->d_attracters > 0 && !bp->attracters) ncz_harness_die(1);
    for (i = 0; i < bp->d_emitters; i++) {
        bp->emitters[i].phase = frand(PIx2);
        bp->emitters[i].hue = frand(1.0f);
        bp->emitters[i].base_size = 20.0f + frand(40.0f);
    }
    for (i = 0; i < bp->d_attracters; i++) {
        bp->attracters[i].phase = frand(PIx2);
        bp->attracters[i].hue = frand(1.0f);
        bp->attracters[i].base_size = 30.0f + frand(50.0f);
    }
    bp->mode_t = 0.0f;
    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_microcosm(ModeInfo *mi) {
    microcosm_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;
    float dt = bp->frame_time;
    float r, g, b;
    float scale, size_factor = (float)bp->d_size / 30.0f;

    bp->mode_t += dt * (float)bp->d_speed * 0.05f;
    if (bp->mode_t > PIx2) bp->mode_t -= PIx2;

    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -300.0f);
    glRotatef(bp->mode_t * 5.0f, 0.0f, 1.0f, 0.0f);

    glBlendFunc(GL_ONE, GL_ONE);

    /* draw emitters */
    glBindTexture(GL_TEXTURE_2D, bp->tex[0]);
    for (i = 0; i < bp->d_emitters; i++) {
        microcosm_blob *em = &bp->emitters[i];
        em->phase += dt * 0.4f * (float)bp->d_speed / 30.0f;
        em->x = 200.0f * sinf(em->phase * 1.3f) * cosf(em->phase * 0.7f);
        em->y = 200.0f * cosf(em->phase * 0.9f) * sinf(em->phase * 1.7f);
        em->z = 200.0f * sinf(em->phase * 1.1f + 1.0f);
        scale = 1.0f + 0.3f * sinf(em->phase * 3.0f);
        hsl2rgb(em->hue + bp->mode_t * 0.05f, 1.0f, 0.5f, &r, &g, &b);
        glPushMatrix();
            glTranslatef(em->x, em->y, em->z);
            float sz = em->base_size * scale * size_factor;
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

    /* draw attractors (different palette / tex) */
    glBindTexture(GL_TEXTURE_2D, bp->tex[1]);
    for (i = 0; i < bp->d_attracters; i++) {
        microcosm_blob *at = &bp->attracters[i];
        at->phase += dt * 0.15f * (float)bp->d_speed / 30.0f;
        at->x = 150.0f * sinf(at->phase * 0.7f + 2.0f);
        at->y = 150.0f * cosf(at->phase * 1.1f + 1.0f);
        at->z = 150.0f * sinf(at->phase * 0.9f + 0.5f);
        scale = 1.0f + 0.5f * cosf(at->phase * 2.0f);
        hsl2rgb(at->hue + 0.5f + bp->mode_t * 0.03f, 1.0f, 0.6f, &r, &g, &b);
        glPushMatrix();
            glTranslatef(at->x, at->y, at->z);
            float sz = at->base_size * scale * size_factor;
            glScalef(sz, sz, sz);
            glColor3f(r * 1.2f, g * 1.2f, b * 1.2f);
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
free_microcosm(ModeInfo *mi) {
    microcosm_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;
    if (bp->emitters)   { free(bp->emitters);   bp->emitters   = NULL; }
    if (bp->attracters) { free(bp->attracters); bp->attracters = NULL; }
    for (i = 0; i < 3; i++) {
        if (bp->tex[i]) { glDeleteTextures(1, &bp->tex[i]); bp->tex[i] = 0; }
    }
}

ENTRYPOINT Bool
microcosm_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Microcosm", microcosm, microcosm)

#endif /* USE_GL */
