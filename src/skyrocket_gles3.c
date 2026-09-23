/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Skyrocket.
 *
 * Skyrocket is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Skyrocket is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * skyrocket_gles3.c — Round-15 SIMPLIFIED native GLES3 port of
 * Terence Welsh's "Skyrocket" saver, sourced via
 * erik-larsen/rss-sdl2-gles2's SDL2/GLES2 wrapper around the
 * original RSS-GLX skyrocket.cpp.
 *
 * The original is a firework simulation with a full particle engine
 * (particle.h), world-level physics, shockwave + smoke + flare
 * sprites, sound effects, and a LaunchBox. Porting the full particle
 * + world + sound + flare chain (~5000 lines) is out of scope.
 *
 * This SIMPLIFIED port preserves the VISUAL ESSENCE — rockets
 * launching from below and exploding into colored starbursts at
 * peak altitude — by:
 *   - keeping a small ring buffer of N "active" rockets + N
 *     "burst" particles per burst
 *   - launching a new rocket every ~0.5 seconds at a random x
 *   - the rocket accelerates upward with gravity, leaving a fading
 *     trail (line segments)
 *   - at apex the rocket spawns a burst of 60+ colored particles
 *     radiating outward with gravity, fading to black over ~2s
 *
 * No audio, no sound engine, no shockwave / smoke sprites — those
 * are explicit "out of scope" simplifications. The "lots of color
 * in the sky" signature is preserved.
 */

#define DEFAULTS	"*delay:    20000   \n" \
			"*rockets:  8       \n" \
			"*particles: 100    \n" \
			"*speed:    50      \n" \
			"*gravity:  2.0     \n" \
			"*size:     5       \n"

#define release_skyrocket 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_GL

#define DEF_ROCKETS   "8"
#define DEF_PARTICLES "100"
#define DEF_SPEED     "50"
#define DEF_GRAVITY   "2.0"
#define DEF_SIZE      "5"

#define PIx2 6.28318530718f
#define MAXROCKETS 16
#define PARTICLES_PER_BURST 60
#define MAX_PARTICLES 800

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int active;
    float hue;
    float trail_y[12];
    float trail_x[12];
    float trail_z[12];
} rocket;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int active;
    float life;
    float r, g, b;
} burst_particle;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_rockets;
    int d_particles;     /* max particles per burst */
    int d_speed;
    float d_gravity;
    int d_size;

    rocket rockets[MAXROCKETS];
    burst_particle particles[MAX_PARTICLES];

    float next_launch;
    int next_rocket_idx;
    int next_particle_idx;
} skyrocket_configuration;

static skyrocket_configuration *bps = NULL;

static int d_rockets;
static int d_particles;
static int d_speed;
static float d_gravity;
static int d_size;

static XrmOptionDescRec opts[] = {
    { "-rockets",   ".rockets",   XrmoptionSepArg, 0 },
    { "-particles", ".particles", XrmoptionSepArg, 0 },
    { "-speed",     ".speed",     XrmoptionSepArg, 0 },
    { "-gravity",   ".gravity",   XrmoptionSepArg, 0 },
    { "-size",      ".size",      XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_rockets,   "rockets",   "Rockets",   DEF_ROCKETS,   t_Int  },
    { &d_particles, "particles", "Particles", DEF_PARTICLES, t_Int  },
    { &d_speed,     "speed",     "Speed",     DEF_SPEED,     t_Int  },
    { &d_gravity,   "gravity",   "Gravity",   DEF_GRAVITY,   t_Float },
    { &d_size,      "size",      "Size",      DEF_SIZE,      t_Int  },
};

ENTRYPOINT ModeSpecOpt skyrocket_opts = {
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

static void launch_rocket(skyrocket_configuration *bp) {
    int idx = bp->next_rocket_idx;
    rocket *r = &bp->rockets[idx];
    r->x = frand(40.0f) - 20.0f;
    r->y = -50.0f;
    r->z = frand(20.0f) - 10.0f;
    r->vx = frand(2.0f) - 1.0f;
    r->vy = 80.0f + frand(40.0f);
    r->vz = frand(2.0f) - 1.0f;
    r->active = 1;
    r->hue = frand(1.0f);
    int i;
    for (i = 0; i < 12; i++) {
        r->trail_x[i] = r->x;
        r->trail_y[i] = r->y;
        r->trail_z[i] = r->z;
    }
    bp->next_rocket_idx = (bp->next_rocket_idx + 1) % MAXROCKETS;
}

static void explode(skyrocket_configuration *bp, rocket *r) {
    int i;
    for (i = 0; i < bp->d_particles; i++) {
        burst_particle *p = &bp->particles[bp->next_particle_idx];
        float phi = (float)i / (float)bp->d_particles * PIx2;
        float theta = acosf(2.0f * frand(1.0f) - 1.0f);
        float speed = 20.0f + frand(30.0f);
        p->x = r->x; p->y = r->y; p->z = r->z;
        p->vx = speed * sinf(theta) * cosf(phi);
        p->vy = speed * cosf(theta);
        p->vz = speed * sinf(theta) * sinf(phi);
        p->active = 1;
        p->life = 1.0f;
        /* slight color variation per particle */
        float h = r->hue + (frand(0.1f) - 0.05f);
        if (h < 0.0f) h += 1.0f;
        if (h > 1.0f) h -= 1.0f;
        hsl2rgb(h, 1.0f, 0.6f, &p->r, &p->g, &p->b);
        bp->next_particle_idx = (bp->next_particle_idx + 1) % MAX_PARTICLES;
    }
}

ENTRYPOINT void
reshape_skyrocket(ModeInfo *mi, int width, int height) {
    skyrocket_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, bp->aspect_ratio, 1.0, 10000.0);
}

ENTRYPOINT void
init_skyrocket(ModeInfo *mi) {
    skyrocket_configuration *bp;
    int i;

    if (!bps) {
        bps = (skyrocket_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(skyrocket_configuration));
        if (!bps) exit(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_rockets   = (d_rockets   < 1) ? 1 : (d_rockets   > MAXROCKETS ? MAXROCKETS : d_rockets);
    bp->d_particles = (d_particles < 1) ? 1 : (d_particles > 800 ? 800 : d_particles);
    bp->d_speed     = (d_speed     < 1) ? 1 : (d_speed     > 100 ? 100 : d_speed);
    bp->d_gravity   = (d_gravity   < 0.0f) ? 0.0f : (d_gravity > 20.0f ? 20.0f : d_gravity);
    bp->d_size      = (d_size      < 1) ? 1 : (d_size      > 20 ? 20 : d_size);

    for (i = 0; i < MAXROCKETS; i++) bp->rockets[i].active = 0;
    for (i = 0; i < MAX_PARTICLES; i++) bp->particles[i].active = 0;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    bp->next_launch = 0.5f;
    bp->next_rocket_idx = 0;
    bp->next_particle_idx = 0;
    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_skyrocket(ModeInfo *mi) {
    skyrocket_configuration *bp = &bps[MI_SCREEN(mi)];
    int i, j;
    float dt = bp->frame_time * (float)bp->d_speed / 30.0f;

    bp->next_launch -= dt;
    if (bp->next_launch <= 0.0f) {
        launch_rocket(bp);
        bp->next_launch = 0.2f + frand(0.6f);
    }

    /* integrate rockets */
    for (i = 0; i < MAXROCKETS; i++) {
        rocket *r = &bp->rockets[i];
        if (!r->active) continue;
        r->vy -= 30.0f * bp->d_gravity * dt;
        r->x += r->vx * dt;
        r->y += r->vy * dt;
        r->z += r->vz * dt;
        /* shift trail */
        for (j = 11; j > 0; j--) {
            r->trail_x[j] = r->trail_x[j-1];
            r->trail_y[j] = r->trail_y[j-1];
            r->trail_z[j] = r->trail_z[j-1];
        }
        r->trail_x[0] = r->x;
        r->trail_y[0] = r->y;
        r->trail_z[0] = r->z;
        if (r->vy <= 0.0f || r->y > 200.0f) {
            explode(bp, r);
            r->active = 0;
        }
    }

    /* integrate burst particles */
    for (i = 0; i < MAX_PARTICLES; i++) {
        burst_particle *p = &bp->particles[i];
        if (!p->active) continue;
        p->vy -= 30.0f * bp->d_gravity * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->z += p->vz * dt;
        p->life -= dt * 0.5f;
        if (p->life <= 0.0f || p->y < -100.0f) p->active = 0;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -100.0f);
    glRotatef(20.0f, 1.0f, 0.0f, 0.0f);

    glPointSize((float)bp->d_size);

    /* draw rockets (point at the head, fading line trail) */
    for (i = 0; i < MAXROCKETS; i++) {
        rocket *r = &bp->rockets[i];
        if (!r->active) continue;
        float r0, g0, b0;
        hsl2rgb(r->hue, 1.0f, 0.7f, &r0, &g0, &b0);
        /* trail */
        glLineWidth(2.0f);
        glBegin(GL_LINE_STRIP);
            for (j = 0; j < 12; j++) {
                float a = 1.0f - (float)j / 12.0f;
                glColor4f(r0 * a, g0 * a, b0 * a, a);
                glVertex3f(r->trail_x[j], r->trail_y[j], r->trail_z[j]);
            }
        glEnd();
        /* head */
        glBegin(GL_POINTS);
            glColor3f(r0, g0, b0);
            glVertex3f(r->x, r->y, r->z);
        glEnd();
    }

    /* draw burst particles */
    glBegin(GL_POINTS);
    for (i = 0; i < MAX_PARTICLES; i++) {
        burst_particle *p = &bp->particles[i];
        if (!p->active) continue;
        glColor4f(p->r * p->life, p->g * p->life, p->b * p->life, p->life);
        glVertex3f(p->x, p->y, p->z);
    }
    glEnd();

    glFinish();
}

ENTRYPOINT void
free_skyrocket(ModeInfo *mi) {
    (void)mi;
}

ENTRYPOINT Bool
skyrocket_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Skyrocket", skyrocket, skyrocket)

#endif /* USE_GL */
