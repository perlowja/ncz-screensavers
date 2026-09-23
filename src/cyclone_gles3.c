/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Cyclone.
 *
 * Cyclone is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Cyclone is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * cyclone_gles3.c — Round-15 native GLES3 port of Terence Welsh's
 * "Cyclone" saver, sourced via erik-larsen/rss-sdl2-gles2's
 * SDL2/GLES2 wrapper around the original RSS-GLX cyclone.cpp.
 *
 * Algorithm preserved: a "cyclone" is a chain of N control points
 * (dComplexity+3) whose positions, widths, and HSL color are smoothly
 * tweened between current and target values over random time windows.
 * Each frame, every cyclone emits particles that flow along the
 * cubic-Bezier defined by its control points, with the particle's
 * width, spin, and trail-stretch all modulating by the local chain
 * width at that step.
 *
 * The HSL color tween (`hslTween`) is from the upstream Rgbhsl helper
 * (vendored inline as a single static function — the 6-line body is
 * small enough to not warrant its own translation unit).
 */

#define DEFAULTS	"*delay:   20000   \n" \
			"*cyclones:1      \n" \
			"*particles:400   \n" \
			"*size:  7        \n" \
			"*complexity: 3   \n" \
			"*speed:  10      \n" \
			"*stretch: True   \n" \
			"*showcurves: False\n"

#define release_cyclone 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#ifdef USE_GL

#define DEF_CYCLONES   "1"
#define DEF_PARTICLES  "400"
#define DEF_SIZE       "7"
#define DEF_COMPLEXITY "3"
#define DEF_SPEED      "10"
#define DEF_STRETCH    "True"
#define DEF_SHOWCURVES "False"

#define PIx2 6.28318530718f
#define PI 3.14159265359f
#define WIDE 200.0f
#define HIGH 200.0f

typedef struct {
    int n_points;             /* dComplexity + 3 */
    float *targetxyz;
    float *xyz;
    float *oldxyz;
    float *targetWidth;
    float *width;
    float *oldWidth;
    float targethsl[3];
    float hsl[3];
    float oldhsl[3];
    float *xyzChange;
    float *widthChange;
    float hslChange[2];
} cyclone;

typedef struct {
    float r, g, b;
    float xyz[3], lastxyz[3];
    float width;
    float step;
    float spinAngle;
    cyclone *cy;
} particle;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_cyclones;
    int d_particles;
    int d_size;
    int d_complexity;
    int d_speed;
    int d_stretch;
    int d_showcurves;

    cyclone **cyclones;
    particle **particles;
    float fact[13];
} cyclone_configuration;

static cyclone_configuration *bps = NULL;

static int d_cyclones;
static int d_particles;
static int d_size;
static int d_complexity;
static int d_speed;
static int d_stretch;
static int d_showcurves;

static XrmOptionDescRec opts[] = {
    { "-cyclones",   ".cyclones",   XrmoptionSepArg, 0 },
    { "-particles",  ".particles",  XrmoptionSepArg, 0 },
    { "-size",       ".size",       XrmoptionSepArg, 0 },
    { "-complexity", ".complexity", XrmoptionSepArg, 0 },
    { "-speed",      ".speed",      XrmoptionSepArg, 0 },
    { "-stretch",    ".stretch",    XrmoptionNoArg, "True" },
    { "+stretch",    ".stretch",    XrmoptionNoArg, "False" },
    { "-showcurves", ".showcurves", XrmoptionNoArg, "True" },
    { "+showcurves", ".showcurves", XrmoptionNoArg, "False" },
};

static argtype vars[] = {
    { &d_cyclones,   "cyclones",   "Cyclones",   DEF_CYCLONES,   t_Int  },
    { &d_particles,  "particles",  "Particles",  DEF_PARTICLES,  t_Int  },
    { &d_size,       "size",       "Size",       DEF_SIZE,       t_Int  },
    { &d_complexity, "complexity", "Complexity", DEF_COMPLEXITY, t_Int  },
    { &d_speed,      "speed",      "Speed",      DEF_SPEED,      t_Int  },
    { &d_stretch,    "stretch",    "Stretch",    DEF_STRETCH,    t_Bool },
    { &d_showcurves, "showcurves", "ShowCurves", DEF_SHOWCURVES, t_Bool },
};

ENTRYPOINT ModeSpecOpt cyclone_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

/* Inline hslTween — a linear-tween in HSL space with optional shortest-
 * arc interpolation on the hue axis (direction=1 means negative
 * progression around the color wheel). */
static void hslTween(float h1, float s1, float l1,
                     float h2, float s2, float l2, float tween, int direction,
                     float *hOut, float *sOut, float *lOut) {
    float temp;
    if (direction == 0) {
        *hOut = h1 + tween * (h2 - h1);
    } else {
        temp = h2 - h1;
        if (temp < 0.0f) temp += 1.0f;
        *hOut = h1 + tween * temp;
        if (*hOut > 1.0f) *hOut -= 1.0f;
    }
    *sOut = s1 + tween * (s2 - s1);
    *lOut = l1 + tween * (l2 - l1);
}

/* hsl -> rgb, all values in [0,1]. */
static void hsl2rgb(float h, float s, float l, float *rOut, float *gOut, float *bOut) {
    float temp1, temp2, tempr, tempg, tempb;
    if (s == 0.0f) {
        *rOut = *gOut = *bOut = l;
        return;
    }
    temp2 = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    temp1 = 2.0f * l - temp2;
    tempr = h + 1.0f / 3.0f;
    if (tempr > 1.0f) tempr -= 1.0f;
    tempg = h;
    tempb = h - 1.0f / 3.0f;
    if (tempb < 0.0f) tempb += 1.0f;
    /* red */
    if (tempr < 1.0f / 6.0f)      *rOut = temp1 + (temp2 - temp1) * 6.0f * tempr;
    else if (tempr < 0.5f)        *rOut = temp2;
    else if (tempr < 2.0f / 3.0f) *rOut = temp1 + (temp2 - temp1) * (2.0f/3.0f - tempr) * 6.0f;
    else                          *rOut = temp1;
    /* green */
    if (tempg < 1.0f / 6.0f)      *gOut = temp1 + (temp2 - temp1) * 6.0f * tempg;
    else if (tempg < 0.5f)        *gOut = temp2;
    else if (tempg < 2.0f / 3.0f) *gOut = temp1 + (temp2 - temp1) * (2.0f/3.0f - tempg) * 6.0f;
    else                          *gOut = temp1;
    /* blue */
    if (tempb < 1.0f / 6.0f)      *bOut = temp1 + (temp2 - temp1) * 6.0f * tempb;
    else if (tempb < 0.5f)        *bOut = temp2;
    else if (tempb < 2.0f / 3.0f) *bOut = temp1 + (temp2 - temp1) * (2.0f/3.0f - tempb) * 6.0f;
    else                          *bOut = temp1;
}

static cyclone *cyclone_new(int n_points) {
    cyclone *c = (cyclone *)calloc(1, sizeof(cyclone));
    if (!c) exit(1);
    c->n_points = n_points;
    c->targetxyz  = (float *)calloc((size_t)n_points * 3, sizeof(float));
    c->xyz        = (float *)calloc((size_t)n_points * 3, sizeof(float));
    c->oldxyz     = (float *)calloc((size_t)n_points * 3, sizeof(float));
    c->targetWidth = (float *)calloc((size_t)n_points, sizeof(float));
    c->width       = (float *)calloc((size_t)n_points, sizeof(float));
    c->oldWidth    = (float *)calloc((size_t)n_points, sizeof(float));
    c->xyzChange   = (float *)calloc((size_t)n_points * 2, sizeof(float));
    c->widthChange = (float *)calloc((size_t)n_points * 2, sizeof(float));
    return c;
}

static void cyclone_free(cyclone *c) {
    if (!c) return;
    free(c->targetxyz); free(c->xyz); free(c->oldxyz);
    free(c->targetWidth); free(c->width); free(c->oldWidth);
    free(c->xyzChange); free(c->widthChange);
    free(c);
}

static void cyclone_init(cyclone *c, cyclone_configuration *bp) {
    int i, n = c->n_points;
    int d_complexity = bp->d_complexity;

    /* initialize positions; the formula mirrors cyclone.cpp */
    c->xyz[(n-1)*3+0] = frand(WIDE*2.0f) - WIDE;
    c->xyz[(n-1)*3+1] = HIGH;
    c->xyz[(n-1)*3+2] = frand(WIDE*2.0f) - WIDE;
    c->xyz[(n-2)*3+0] = c->xyz[(n-1)*3+0];
    c->xyz[(n-2)*3+1] = frand(HIGH/3.0f) + HIGH/4.0f;
    c->xyz[(n-2)*3+2] = c->xyz[(n-1)*3+2];
    for (i = d_complexity; i > 1; i--) {
        c->xyz[i*3+0] = c->xyz[(i+1)*3+0] + frand(WIDE) - WIDE/2.0f;
        c->xyz[i*3+1] = frand(HIGH*2.0f) - HIGH;
        c->xyz[i*3+2] = c->xyz[(i+1)*3+2] + frand(WIDE) - WIDE/2.0f;
    }
    c->xyz[1*3+0] = c->xyz[2*3+0] + frand(WIDE/2.0f) - WIDE/4.0f;
    c->xyz[1*3+1] = -frand(HIGH/2.0f) - HIGH/4.0f;
    c->xyz[1*3+2] = c->xyz[2*3+2] + frand(WIDE/2.0f) - WIDE/4.0f;
    c->xyz[0*3+0] = c->xyz[1*3+0] + frand(WIDE/8.0f) - WIDE/16.0f;
    c->xyz[0*3+1] = -HIGH;
    c->xyz[0*3+2] = c->xyz[1*3+2] + frand(WIDE/8.0f) - WIDE/16.0f;

    c->width[(n-1)] = frand(175.0f) + 75.0f;
    c->width[(n-2)] = frand(60.0f)  + 15.0f;
    for (i = d_complexity; i > 1; i--) c->width[i] = frand(25.0f) + 15.0f;
    c->width[1] = frand(25.0f) + 5.0f;
    c->width[0] = frand(15.0f) + 5.0f;

    for (i = 0; i < n; i++) {
        c->xyzChange[i*2+0]   = 0.0f;
        c->xyzChange[i*2+1]   = 0.0f;
        c->widthChange[i*2+0] = 0.0f;
        c->widthChange[i*2+1] = 0.0f;
    }

    c->hsl[0] = c->oldhsl[0] = frand(1.0f);
    c->hsl[1] = c->oldhsl[1] = frand(1.0f);
    c->hsl[2] = c->oldhsl[2] = 0.0f;
    c->targethsl[0] = frand(1.0f);
    c->targethsl[1] = frand(1.0f);
    c->targethsl[2] = 1.0f;
    c->hslChange[0] = 0.0f;
    c->hslChange[1] = 10.0f;
}

static void cyclone_update(cyclone *c, cyclone_configuration *bp) {
    int i, temp_i;
    float between, diff, point[3], step, blend;
    int direction;
    int n = c->n_points;
    int d_complexity = bp->d_complexity;
    float d_speed = (float)bp->d_speed;

    /* update positions */
    temp_i = n - 1;
    if (c->xyzChange[temp_i*2+0] >= c->xyzChange[temp_i*2+1]) {
        c->oldxyz[temp_i*3+0] = c->xyz[temp_i*3+0];
        c->oldxyz[temp_i*3+1] = c->xyz[temp_i*3+1];
        c->oldxyz[temp_i*3+2] = c->xyz[temp_i*3+2];
        c->targetxyz[temp_i*3+0] = frand(WIDE*2.0f) - WIDE;
        c->targetxyz[temp_i*3+1] = HIGH;
        c->targetxyz[temp_i*3+2] = frand(WIDE*2.0f) - WIDE;
        c->xyzChange[temp_i*2+0] = 0.0f;
        c->xyzChange[temp_i*2+1] = frand(150.0f/d_speed) + 75.0f/d_speed;
    }
    temp_i = n - 2;
    if (c->xyzChange[temp_i*2+0] >= c->xyzChange[temp_i*2+1]) {
        c->oldxyz[temp_i*3+0] = c->xyz[temp_i*3+0];
        c->oldxyz[temp_i*3+1] = c->xyz[temp_i*3+1];
        c->oldxyz[temp_i*3+2] = c->xyz[temp_i*3+2];
        c->targetxyz[temp_i*3+0] = c->xyz[(temp_i+1)*3+0];
        c->targetxyz[temp_i*3+1] = frand(HIGH/3.0f) + HIGH/4.0f;
        c->targetxyz[temp_i*3+2] = c->xyz[(temp_i+1)*3+2];
        c->xyzChange[temp_i*2+0] = 0.0f;
        c->xyzChange[temp_i*2+1] = frand(100.0f/d_speed) + 75.0f/d_speed;
    }
    for (i = d_complexity; i > 1; i--) {
        if (c->xyzChange[i*2+0] >= c->xyzChange[i*2+1]) {
            c->oldxyz[i*3+0] = c->xyz[i*3+0];
            c->oldxyz[i*3+1] = c->xyz[i*3+1];
            c->oldxyz[i*3+2] = c->xyz[i*3+2];
            c->targetxyz[i*3+0] = c->targetxyz[(i+1)*3+0] +
                (c->targetxyz[(i+1)*3+0] - c->targetxyz[(i+2)*3+0])/2.0f
                + frand(WIDE/2.0f) - WIDE/4.0f;
            c->targetxyz[i*3+1] = (c->targetxyz[(i+1)*3+1] + c->targetxyz[(i-1)*3+1])/2.0f
                + frand(HIGH/8.0f) - HIGH/16.0f;
            c->targetxyz[i*3+2] = c->targetxyz[(i+1)*3+2] +
                (c->targetxyz[(i+1)*3+2] - c->targetxyz[(i+2)*3+2])/2.0f
                + frand(WIDE/2.0f) - WIDE/4.0f;
            if (c->targetxyz[i*3+1] >  HIGH) c->targetxyz[i*3+1] =  HIGH;
            if (c->targetxyz[i*3+1] < -HIGH) c->targetxyz[i*3+1] = -HIGH;
            c->xyzChange[i*2+0] = 0.0f;
            c->xyzChange[i*2+1] = frand(75.0f/d_speed) + 50.0f/d_speed;
        }
    }
    if (c->xyzChange[1*2+0] >= c->xyzChange[1*2+1]) {
        c->oldxyz[1*3+0] = c->xyz[1*3+0];
        c->oldxyz[1*3+1] = c->xyz[1*3+1];
        c->oldxyz[1*3+2] = c->xyz[1*3+2];
        c->targetxyz[1*3+0] = c->targetxyz[2*3+0] + frand(WIDE/2.0f) - WIDE/4.0f;
        c->targetxyz[1*3+1] = -frand(HIGH/2.0f) - HIGH/4.0f;
        c->targetxyz[1*3+2] = c->targetxyz[2*3+2] + frand(WIDE/2.0f) - WIDE/4.0f;
        c->xyzChange[1*2+0] = 0.0f;
        c->xyzChange[1*2+1] = frand(50.0f/d_speed) + 30.0f/d_speed;
    }
    if (c->xyzChange[0*2+0] >= c->xyzChange[0*2+1]) {
        c->oldxyz[0*3+0] = c->xyz[0*3+0];
        c->oldxyz[0*3+1] = c->xyz[0*3+1];
        c->oldxyz[0*3+2] = c->xyz[0*3+2];
        c->targetxyz[0*3+0] = c->xyz[1*3+0] + frand(WIDE/8.0f) - WIDE/16.0f;
        c->targetxyz[0*3+1] = -HIGH;
        c->targetxyz[0*3+2] = c->xyz[1*3+2] + frand(WIDE/8.0f) - WIDE/16.0f;
        c->xyzChange[0*2+0] = 0.0f;
        c->xyzChange[0*2+1] = frand(100.0f/d_speed) + 75.0f/d_speed;
    }
    for (i = 0; i < n; i++) {
        between = c->xyzChange[i*2+0] / c->xyzChange[i*2+1] * PIx2;
        between = (1.0f - cosf(between)) / 2.0f;
        c->xyz[i*3+0] = (c->targetxyz[i*3+0] - c->oldxyz[i*3+0]) * between + c->oldxyz[i*3+0];
        c->xyz[i*3+1] = (c->targetxyz[i*3+1] - c->oldxyz[i*3+1]) * between + c->oldxyz[i*3+1];
        c->xyz[i*3+2] = (c->targetxyz[i*3+2] - c->oldxyz[i*3+2]) * between + c->oldxyz[i*3+2];
        c->xyzChange[i*2+0] += bp->frame_time;
    }

    /* widths */
    temp_i = n - 1;
    if (c->widthChange[temp_i*2+0] >= c->widthChange[temp_i*2+1]) {
        c->oldWidth[temp_i] = c->width[temp_i];
        c->targetWidth[temp_i] = frand(225.0f) + 75.0f;
        c->widthChange[temp_i*2+0] = 0.0f;
        c->widthChange[temp_i*2+1] = frand(50.0f/d_speed) + 50.0f/d_speed;
    }
    temp_i = n - 2;
    if (c->widthChange[temp_i*2+0] >= c->widthChange[temp_i*2+1]) {
        c->oldWidth[temp_i] = c->width[temp_i];
        c->targetWidth[temp_i] = frand(100.0f) + 15.0f;
        c->widthChange[temp_i*2+0] = 0.0f;
        c->widthChange[temp_i*2+1] = frand(50.0f/d_speed) + 50.0f/d_speed;
    }
    for (i = d_complexity; i > 1; i--) {
        if (c->widthChange[i*2+0] >= c->widthChange[i*2+1]) {
            c->oldWidth[i] = c->width[i];
            c->targetWidth[i] = frand(50.0f) + 15.0f;
            c->widthChange[i*2+0] = 0.0f;
            c->widthChange[i*2+1] = frand(50.0f/d_speed) + 40.0f/d_speed;
        }
    }
    if (c->widthChange[1*2+0] >= c->widthChange[1*2+1]) {
        c->oldWidth[1] = c->width[1];
        c->targetWidth[1] = frand(40.0f) + 5.0f;
        c->widthChange[1*2+0] = 0.0f;
        c->widthChange[1*2+1] = frand(50.0f/d_speed) + 30.0f/d_speed;
    }
    if (c->widthChange[0*2+0] >= c->widthChange[0*2+1]) {
        c->oldWidth[0] = c->width[0];
        c->targetWidth[0] = frand(30.0f) + 5.0f;
        c->widthChange[0*2+0] = 0.0f;
        c->widthChange[0*2+1] = frand(50.0f/d_speed) + 20.0f/d_speed;
    }
    for (i = 0; i < n; i++) {
        between = c->widthChange[i*2+0] / c->widthChange[i*2+1];
        c->width[i] = (c->targetWidth[i] - c->oldWidth[i]) * between + c->oldWidth[i];
        c->widthChange[i*2+0] += bp->frame_time;
    }

    /* colors */
    if (c->hslChange[0] >= c->hslChange[1]) {
        c->oldhsl[0] = c->hsl[0];
        c->oldhsl[1] = c->hsl[1];
        c->oldhsl[2] = c->hsl[2];
        c->targethsl[0] = frand(1.0f);
        c->targethsl[1] = frand(1.0f);
        c->targethsl[2] = frand(1.0f) + 0.5f;
        if (c->targethsl[2] > 1.0f) c->targethsl[2] = 1.0f;
        c->hslChange[0] = 0.0f;
        c->hslChange[1] = frand(30.0f) + 2.0f;
    }
    between = c->hslChange[0] / c->hslChange[1];
    diff = c->targethsl[0] - c->oldhsl[0];
    direction = 0;
    if ((c->targethsl[0] > c->oldhsl[0] && diff > 0.5f) ||
        (c->targethsl[0] < c->oldhsl[0] && diff < -0.5f))
        if (diff > 0.5f) direction = 1;
    hslTween(c->oldhsl[0], c->oldhsl[1], c->oldhsl[2],
             c->targethsl[0], c->targethsl[1], c->targethsl[2],
             between, direction,
             &c->hsl[0], &c->hsl[1], &c->hsl[2]);
    c->hslChange[0] += bp->frame_time;

    if (bp->d_showcurves) {
        int d_complexity = bp->d_complexity;
        glDisable(GL_LIGHTING);
        glColor3f(0.0f, 1.0f, 0.0f);
        glBegin(GL_LINE_STRIP);
        for (step = 0.0f; step < 1.0f; step += 0.02f) {
            point[0] = point[1] = point[2] = 0.0f;
            for (i = 0; i < n; i++) {
                blend = bp->fact[d_complexity+2] / (bp->fact[i] * bp->fact[d_complexity+2-i])
                        * powf(step, (float)i)
                        * powf(1.0f - step, (float)(d_complexity+2-i));
                point[0] += c->xyz[i*3+0] * blend;
                point[1] += c->xyz[i*3+1] * blend;
                point[2] += c->xyz[i*3+2] * blend;
            }
            glVertex3fv(point);
        }
        glEnd();
        glColor3f(1.0f, 0.0f, 0.0f);
        glBegin(GL_LINE_STRIP);
        for (i = 0; i < n; i++)
            glVertex3fv(&c->xyz[i*3]);
        glEnd();
        glEnable(GL_LIGHTING);
    }
}

static particle *particle_new(cyclone *cy) {
    particle *p = (particle *)calloc(1, sizeof(particle));
    if (!p) exit(1);
    p->cy = cy;
    p->width = frand(0.8f) + 0.2f;
    p->step = 0.0f;
    p->spinAngle = frand(360.0f);
    hsl2rgb(cy->hsl[0], cy->hsl[1], cy->hsl[2], &p->r, &p->g, &p->b);
    return p;
}

static void particle_init(particle *p) {
    p->width = frand(0.8f) + 0.2f;
    p->step = 0.0f;
    p->spinAngle = frand(360.0f);
    hsl2rgb(p->cy->hsl[0], p->cy->hsl[1], p->cy->hsl[2], &p->r, &p->g, &p->b);
}

static void particle_update(particle *p, cyclone_configuration *bp) {
    int i, idx;
    float scale, temp, cyWidth, between;
    float dir[3], crossVec[3], tiltAngle;
    float up[3] = {0.0f, 1.0f, 0.0f};
    float blend;
    cyclone *cy = p->cy;
    int d_complexity = bp->d_complexity;
    float d_speed = (float)bp->d_speed;
    float d_size = (float)bp->d_size;
    int d_stretch = bp->d_stretch;
    int n = cy->n_points;

    p->lastxyz[0] = p->xyz[0];
    p->lastxyz[1] = p->xyz[1];
    p->lastxyz[2] = p->xyz[2];
    if (p->step > 1.0f) particle_init(p);
    p->xyz[0] = p->xyz[1] = p->xyz[2] = 0.0f;
    for (i = 0; i < n; i++) {
        blend = bp->fact[d_complexity+2] / (bp->fact[i] * bp->fact[d_complexity+2-i])
                * powf(p->step, (float)i)
                * powf(1.0f - p->step, (float)(d_complexity+2-i));
        p->xyz[0] += cy->xyz[i*3+0] * blend;
        p->xyz[1] += cy->xyz[i*3+1] * blend;
        p->xyz[2] += cy->xyz[i*3+2] * blend;
    }
    dir[0] = dir[1] = dir[2] = 0.0f;
    for (i = 0; i < n; i++) {
        blend = bp->fact[d_complexity+2] / (bp->fact[i] * bp->fact[d_complexity+2-i])
                * powf(p->step - 0.01f, (float)i)
                * powf(1.0f - (p->step - 0.01f), (float)(d_complexity+2-i));
        dir[0] += cy->xyz[i*3+0] * blend;
        dir[1] += cy->xyz[i*3+1] * blend;
        dir[2] += cy->xyz[i*3+2] * blend;
    }
    dir[0] = p->xyz[0] - dir[0];
    dir[1] = p->xyz[1] - dir[1];
    dir[2] = p->xyz[2] - dir[2];
    {
        float l = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        if (l > 0.0f) { dir[0]/=l; dir[1]/=l; dir[2]/=l; }
    }
    crossVec[0] = dir[1]*up[2] - up[1]*dir[2];
    crossVec[1] = dir[2]*up[0] - up[2]*dir[0];
    crossVec[2] = dir[0]*up[1] - up[0]*dir[1];
    {
        float dot = dir[0]*up[0] + dir[1]*up[1] + dir[2]*up[2];
        if (dot > 1.0f) dot = 1.0f;
        if (dot < -1.0f) dot = -1.0f;
        tiltAngle = -acosf(dot) * 180.0f / PI;
    }
    idx = (int)(p->step * (float)(d_complexity + 2));
    if (idx >= (d_complexity + 2)) idx = d_complexity + 1;
    between = (p->step - (float)idx / (float)(d_complexity + 2)) * (float)(d_complexity + 2);
    cyWidth = cy->width[idx] * (1.0f - between) + cy->width[idx+1] * between;
    p->step += (0.2f * bp->frame_time * d_speed) / (p->width * p->width * cyWidth);
    p->spinAngle += (1500.0f * bp->frame_time * d_speed) / (p->width * cyWidth);
    if (d_stretch) {
        scale = p->width * cyWidth * (1500.0f * bp->frame_time * d_speed) / (p->width * cyWidth) * 0.02f;
        temp = cyWidth * 2.0f / d_size;
        if (scale > temp) scale = temp;
        if (scale < 3.0f) scale = 3.0f;
    } else {
        scale = 1.0f;
    }
    glColor3f(p->r, p->g, p->b);
    glPushMatrix();
        glLoadIdentity();
        glTranslatef(p->xyz[0], p->xyz[1], p->xyz[2]);
        glRotatef(tiltAngle, crossVec[0], crossVec[1], crossVec[2]);
        glRotatef(p->spinAngle, 0.0f, 1.0f, 0.0f);
        glTranslatef(p->width * cyWidth, 0.0f, 0.0f);
        if (d_stretch) glScalef(1.0f, 1.0f, scale);
        glCallList(1);
    glPopMatrix();
}

ENTRYPOINT void
reshape_cyclone(ModeInfo *mi, int width, int height) {
    cyclone_configuration *bp = &bps[MI_SCREEN(mi)];
    float aspect;
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    aspect = (float)width / (float)height;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(80.0, aspect, 50.0, 3000.0);
    /* random easter egg match: same 1-in-500 chance */
    if (frand(500.0f) > 499.0f) {
        glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
        glTranslatef(0.0f, -(WIDE * 2.0f), 0.0f);
    } else {
        glTranslatef(0.0f, 0.0f, -(WIDE * 2.0f));
    }
    glMatrixMode(GL_MODELVIEW);
    bp->aspect_ratio = aspect;
}

ENTRYPOINT void
init_cyclone(ModeInfo *mi) {
    cyclone_configuration *bp;
    int i, j;
    float ambient[4], diffuse[4], specular[4], position[4];

    if (!bps) {
        bps = (cyclone_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(cyclone_configuration));
        if (!bps) exit(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_cyclones   = (d_cyclones   < 1) ? 1 : (d_cyclones   > 10 ? 10 : d_cyclones);
    bp->d_particles  = (d_particles  < 1) ? 1 : (d_particles  > 10000 ? 10000 : d_particles);
    bp->d_size       = (d_size       < 1) ? 1 : (d_size       > 100 ? 100 : d_size);
    bp->d_complexity = (d_complexity < 1) ? 1 : (d_complexity > 10 ? 10 : d_complexity);
    bp->d_speed      = (d_speed      < 1) ? 1 : (d_speed      > 100 ? 100 : d_speed);
    bp->d_stretch    = d_stretch;
    bp->d_showcurves = d_showcurves;

    glEnable(GL_DEPTH_TEST);
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    /* record display list 1: low-poly sphere (3 stacks, 2 slices).
     * Inline sphere — small triangle-strip rendition rather than going
     * through gluNewQuadric (which the GLES3 path doesn't expose). */
    glNewList(1, GL_COMPILE);
    {
        const int slices = 2, stacks = 3;
        int i, j;
        float radius = (float)bp->d_size / 4.0f;
        for (i = 0; i < stacks; i++) {
            float theta1 = (float)i * (float)M_PI / (float)stacks - (float)M_PI/2.0f;
            float theta2 = (float)(i+1) * (float)M_PI / (float)stacks - (float)M_PI/2.0f;
            glBegin(GL_TRIANGLE_STRIP);
            for (j = 0; j <= slices; j++) {
                float phi = (float)j * 2.0f * (float)M_PI / (float)slices;
                float cx, cy, cz, nx, ny, nz;
                /* upper */
                nx = cosf(theta2) * cosf(phi);
                ny = sinf(theta2);
                nz = cosf(theta2) * sinf(phi);
                cx = nx * radius; cy = ny * radius; cz = nz * radius;
                glNormal3f(nx, ny, nz); glVertex3f(cx, cy, cz);
                /* lower */
                nx = cosf(theta1) * cosf(phi);
                ny = sinf(theta1);
                nz = cosf(theta1) * sinf(phi);
                cx = nx * radius; cy = ny * radius; cz = nz * radius;
                glNormal3f(nx, ny, nz); glVertex3f(cx, cy, cz);
            }
            glEnd();
        }
    }
    glEndList();

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    ambient[0]=0.25f; ambient[1]=0.25f; ambient[2]=0.25f; ambient[3]=0.0f;
    diffuse[0]=1.0f;  diffuse[1]=1.0f;  diffuse[2]=1.0f;  diffuse[3]=0.0f;
    specular[0]=1.0f; specular[1]=1.0f; specular[2]=1.0f; specular[3]=0.0f;
    position[0]=WIDE*2.0f; position[1]=-HIGH; position[2]=WIDE*2.0f; position[3]=0.0f;
    glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glEnable(GL_COLOR_MATERIAL);
    glMaterialf(GL_FRONT, GL_SHININESS, 20.0f);
    glColorMaterial(GL_FRONT, GL_SPECULAR);
    glColor3f(0.7f, 0.7f, 0.7f);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    /* initialize cyclones and particles */
    for (i = 0; i < 13; i++) {
        float x = 1.0f;
        for (j = 1; j <= i; j++) x *= (float)j;
        bp->fact[i] = x;
    }
    bp->cyclones = (cyclone **)calloc((size_t)bp->d_cyclones, sizeof(cyclone *));
    bp->particles = (particle **)calloc((size_t)(bp->d_cyclones * bp->d_particles), sizeof(particle *));
    for (i = 0; i < bp->d_cyclones; i++) {
        bp->cyclones[i] = cyclone_new(bp->d_complexity + 3);
        cyclone_init(bp->cyclones[i], bp);
        for (j = i * bp->d_particles; j < (i+1) * bp->d_particles; j++)
            bp->particles[j] = particle_new(bp->cyclones[i]);
    }

    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_cyclone(ModeInfo *mi) {
    cyclone_configuration *bp = &bps[MI_SCREEN(mi)];
    int i, j;

    glMatrixMode(GL_MODELVIEW);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (i = 0; i < bp->d_cyclones; i++) {
        cyclone_update(bp->cyclones[i], bp);
        for (j = i * bp->d_particles; j < (i+1) * bp->d_particles; j++)
            particle_update(bp->particles[j], bp);
    }

    glFinish();
}

ENTRYPOINT void
free_cyclone(ModeInfo *mi) {
    cyclone_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;
    if (bp->particles) {
        int total = bp->d_cyclones * bp->d_particles;
        for (i = 0; i < total; i++) free(bp->particles[i]);
        free(bp->particles); bp->particles = NULL;
    }
    if (bp->cyclones) {
        for (i = 0; i < bp->d_cyclones; i++) cyclone_free(bp->cyclones[i]);
        free(bp->cyclones); bp->cyclones = NULL;
    }
    glDeleteLists(1, 1);
}

ENTRYPOINT Bool
cyclone_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Cyclone", cyclone, cyclone)

#endif /* USE_GL */
