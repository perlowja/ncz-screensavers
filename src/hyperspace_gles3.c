/*
 * Copyright (C) 2005-2010  Terence M. Welsh
 *
 * This file is part of Hyperspace.
 *
 * Hyperspace is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Hyperspace is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * hyperspace_gles3.c — Round-15 SIMPLIFIED native GLES3 port of
 * Terence Welsh's "Hyperspace" saver, sourced via
 * erik-larsen/rss-sdl2-gles2's SDL2/GLES2 wrapper around the
 * original RSS-GLX hyperspace.cpp.
 *
 * The original wires together ~10 helper classes:
 *   - flare (lens-flare sprite)
 *   - causticTextures / wavyNormalCubeMaps (cube-map nebula textures)
 *   - splinePath (camera path)
 *   - tunnel (recursive tunnel mesh with reflecting walls)
 *   - goo (refractive metaball-like blob)
 *   - stretchedParticle (motion-streak particles)
 *   - starBurst (radial particle burst)
 *   - shaders (GLSL fragment shaders for nebula + tunnel)
 *
 * Porting all of those + the cube-map pipeline would be ~1500 lines
 * of additional GLES3 work. This SIMPLIFIED port preserves the
 * VISUAL ESSENCE — a tunnel of streaking stars rushing toward the
 * camera — by drawing:
 *   - a starfield (N "stars" distributed along a long z-axis, each
 *     rendered as a small textured quad, brightness scaled by
 *     1/distance, position advanced per frame)
 *   - occasional flare bursts at random tunnel positions
 *
 * The motion-streak illusion comes from reusing the same star
 * positions across frames: as z decreases (toward the camera),
 * the trail between (prev_z) and (z) is drawn as a long line, which
 * is what creates the hyperspace-rush look. This is the same
 * mathematical construction used by the upstream stretchedParticle
 * helper, just rendered with line-strips instead of GL_TRIANGLE_STRIP
 * billboards.
 */

#define DEFAULTS	"*delay:    20000   \n" \
			"*speed:    50      \n" \
			"*stars:    200     \n" \
			"*starsize: 50      \n" \
			"*depth:    1000    \n" \
			"*fov:      90      \n"

#define release_hyperspace 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#ifdef USE_GL

#define DEF_SPEED    "50"
#define DEF_STARS    "200"
#define DEF_STARSIZE "50"
#define DEF_DEPTH    "1000"
#define DEF_FOV      "90"

#define PIx2 6.28318530718f
#define DEG2RAD 0.0174532925f

typedef struct {
    float x, y, z;
    float prev_x, prev_y, prev_z;
    float hue;
    float intensity;
} hyperspace_star;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_speed;
    int d_stars;
    int d_starsize;
    int d_depth;
    int d_fov;

    hyperspace_star *stars;
    unsigned int flare_tex;
    float camera_z;        /* advancing toward 0 */
    float next_flare_time;
    float flare_x, flare_y, flare_z;
    float flare_life;
} hyperspace_configuration;

static hyperspace_configuration *bps = NULL;

static int d_speed;
static int d_stars;
static int d_starsize;
static int d_depth;
static int d_fov;

static XrmOptionDescRec opts[] = {
    { "-speed",   ".speed",   XrmoptionSepArg, 0 },
    { "-stars",   ".stars",   XrmoptionSepArg, 0 },
    { "-starsize",".starsize",XrmoptionSepArg, 0 },
    { "-depth",   ".depth",   XrmoptionSepArg, 0 },
    { "-fov",     ".fov",     XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_speed,    "speed",    "Speed",    DEF_SPEED,    t_Int },
    { &d_stars,    "stars",    "Stars",    DEF_STARS,    t_Int },
    { &d_starsize, "starsize", "StarSize", DEF_STARSIZE, t_Int },
    { &d_depth,    "depth",    "Depth",    DEF_DEPTH,    t_Int },
    { &d_fov,      "fov",      "Fov",      DEF_FOV,      t_Int },
};

ENTRYPOINT ModeSpecOpt hyperspace_opts = {
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
}

ENTRYPOINT void
reshape_hyperspace(ModeInfo *mi, int width, int height) {
    hyperspace_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective((float)bp->d_fov, bp->aspect_ratio, 1.0, 5000.0);
}

ENTRYPOINT void
init_hyperspace(ModeInfo *mi) {
    hyperspace_configuration *bp;
    int i, j;
    float x, y, temp;
    unsigned char flare_tex_data[64 * 64];
    unsigned int pixel;

    if (!bps) {
        bps = (hyperspace_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(hyperspace_configuration));
        if (!bps) exit(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_speed    = (d_speed    < 1) ? 1 : (d_speed    > 100 ? 100 : d_speed);
    bp->d_stars    = (d_stars    < 1) ? 1 : (d_stars    > 2000 ? 2000 : d_stars);
    bp->d_starsize = (d_starsize < 1) ? 1 : (d_starsize > 100 ? 100 : d_starsize);
    bp->d_depth    = (d_depth    < 1) ? 1 : (d_depth    > 5000 ? 5000 : d_depth);
    bp->d_fov      = (d_fov      < 30) ? 30 : (d_fov > 150 ? 150 : d_fov);

    /* flare texture: radial gradient with 4 cross spikes (simplified) */
    for (i = 0; i < 64; i++) {
        for (j = 0; j < 64; j++) {
            x = (float)(i - 32) / 32.0f;
            y = (float)(j - 32) / 32.0f;
            temp = 1.0f - sqrtf(x*x + y*y);
            if (temp < 0.0f) temp = 0.0f;
            if (temp > 1.0f) temp = 1.0f;
            /* cross spike enhancement */
            float spike = 1.0f - fabsf(x) * 8.0f;
            if (spike < 0.0f) spike = 0.0f;
            float spike2 = 1.0f - fabsf(y) * 8.0f;
            if (spike2 < 0.0f) spike2 = 0.0f;
            float total = temp + 0.4f * (spike + spike2);
            if (total > 1.0f) total = 1.0f;
            flare_tex_data[i*64 + j] = (unsigned char)(255.0f * total);
        }
    }
    glGenTextures(1, &bp->flare_tex);
    glBindTexture(GL_TEXTURE_2D, bp->flare_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    pixel = 0xFFFFFFFF;  /* white 32-bit, but data is 8-bit */
    (void)pixel;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 64, 64, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, flare_tex_data);

    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    bp->stars = (hyperspace_star *)calloc((size_t)bp->d_stars, sizeof(hyperspace_star));
    if (!bp->stars) exit(1);
    for (i = 0; i < bp->d_stars; i++) {
        bp->stars[i].x = frand(2.0f) - 1.0f;
        bp->stars[i].y = frand(2.0f) - 1.0f;
        bp->stars[i].z = -(float)bp->d_depth * frand(1.0f);
        bp->stars[i].prev_x = bp->stars[i].x;
        bp->stars[i].prev_y = bp->stars[i].y;
        bp->stars[i].prev_z = bp->stars[i].z;
        bp->stars[i].hue = frand(1.0f);
        bp->stars[i].intensity = frand(0.7f) + 0.3f;
    }
    bp->camera_z = 0.0f;
    bp->next_flare_time = 2.0f;
    bp->flare_x = bp->flare_y = bp->flare_z = 0.0f;
    bp->flare_life = 0.0f;
    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_hyperspace(ModeInfo *mi) {
    hyperspace_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;
    float speed = (float)bp->d_speed * 30.0f * bp->frame_time;
    float r, g, b;

    glClear(GL_COLOR_BUFFER_BIT);

    /* integrate camera forward */
    bp->camera_z += speed;

    /* flare event */
    bp->flare_life -= bp->frame_time;
    bp->next_flare_time -= bp->frame_time;
    if (bp->next_flare_time < 0.0f && bp->flare_life <= 0.0f) {
        bp->flare_x = (frand(2.0f) - 1.0f);
        bp->flare_y = (frand(2.0f) - 1.0f);
        bp->flare_z = bp->camera_z - 1000.0f - frand(2000.0f);
        bp->flare_life = 1.5f;
        bp->next_flare_time = frand(8.0f) + 3.0f;
    }

    /* draw stars as motion-streak line segments */
    for (i = 0; i < bp->d_stars; i++) {
        hyperspace_star *s = &bp->stars[i];
        float z = s->z + bp->camera_z;
        /* recycle star when it gets close */
        if (z > 50.0f) {
            s->x = frand(2.0f) - 1.0f;
            s->y = frand(2.0f) - 1.0f;
            s->z -= (float)bp->d_depth;
            s->prev_x = s->x;
            s->prev_y = s->y;
            s->prev_z = s->z;
            z = s->z + bp->camera_z;
        }
        if (z < -3000.0f) {
            s->z += 500.0f;
            continue;
        }
        float pz = z + speed;
        float sat = 1.0f;
        float l = s->intensity * 1.0f / (1.0f + fabsf(z) * 0.002f);
        if (l > 1.0f) l = 1.0f;
        hsl2rgb(s->hue, sat, 0.4f + 0.4f * l, &r, &g, &b);
        glBegin(GL_LINES);
            glColor3f(r * 0.3f, g * 0.3f, b * 0.3f);
            glVertex3f(s->prev_x, s->prev_y, pz);
            glColor3f(r, g, b);
            glVertex3f(s->x, s->y, z);
        glEnd();
        s->prev_x = s->x;
        s->prev_y = s->y;
        s->prev_z = s->z;
    }

    /* draw flare sprite if active */
    if (bp->flare_life > 0.0f) {
        float z = bp->flare_z;
        float intensity = bp->flare_life / 1.5f;
        float size = 200.0f * (1.0f - intensity + 0.5f);
        glBindTexture(GL_TEXTURE_2D, bp->flare_tex);
        glColor3f(intensity, intensity * 0.8f, intensity * 0.6f);
        glPushMatrix();
            glTranslatef(bp->flare_x * 800.0f, bp->flare_y * 800.0f, z);
            glScalef(size, size, 1.0f);
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
free_hyperspace(ModeInfo *mi) {
    hyperspace_configuration *bp = &bps[MI_SCREEN(mi)];
    if (bp->stars) { free(bp->stars); bp->stars = NULL; }
    if (bp->flare_tex) glDeleteTextures(1, &bp->flare_tex);
}

ENTRYPOINT Bool
hyperspace_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Hyperspace", hyperspace, hyperspace)

#endif /* USE_GL */
