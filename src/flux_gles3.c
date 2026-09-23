/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Flux.
 *
 * Flux is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Flux is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * flux_gles3.c — Round-15 native GLES3 port of Terence Welsh's
 * "Flux" saver, sourced via erik-larsen/rss-sdl2-gles2's
 * SDL2/GLES2 wrapper around the original RSS-GLX flux.cpp.
 *
 * Algorithm preserved (simplified): each "flux" has 8 oscillating
 * constants c[0..7] (random initial sign, random velocity in
 * [-bound, +bound] with sign-flips at the extremes). Particles move
 * under the linear combination
 *   p' = p + p × c,   z' = z + blower
 * with each component treated as one element of the cross product of
 * the constants vector with the position. Each particle carries a
 * trailing line of "trail" previous positions; lines fade with
 * luminosity. Particles also re-position themselves when they leave
 * a soft bounding cube.
 *
 * Three geometry modes are supported (dGeometry):
 *   0: GL_POINTS with point size scaling by depth
 *   1: low-poly sphere (lit), camera-orbited
 *   2: textured light-source quads (additive blending)
 *
 * This is a SIMPLIFICATION of the original: flux.cpp models per-flux
 * expansion/instability windows, luminosity-driven trails, and a
 * randomization reset pulse — preserved at a representative level
 * here. The visual signature (swirling 3D particles in a wind field)
 * is maintained.
 */

#define DEFAULTS	"*delay:    20000   \n" \
			"*fluxes:   1       \n" \
			"*particles:1500    \n" \
			"*trail:    30      \n" \
			"*geometry: 0       \n" \
			"*size:     30      \n" \
			"*complexity: 8     \n" \
			"*randomize: 1      \n" \
			"*expansion: 30     \n" \
			"*instability: 30   \n" \
			"*rotation:  0      \n" \
			"*blur:      0      \n"

#define release_flux 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_GL

#define DEF_FLUXES      "1"
#define DEF_PARTICLES   "1500"
#define DEF_TRAIL       "30"
#define DEF_GEOMETRY    "0"
#define DEF_SIZE        "30"
#define DEF_COMPLEXITY  "8"
#define DEF_RANDOMIZE   "1"
#define DEF_EXPANSION   "30"
#define DEF_INSTABILITY "30"
#define DEF_ROTATION    "0"
#define DEF_BLUR        "0"

#define NUMCONSTS 8
#define PIx2 6.28318530718f
#define DEG2RAD 0.0174532925f
#define LIGHTSIZE 64
#define TRAILMAX 32

typedef struct {
    float x, y, z;
    float trail_x[TRAILMAX];
    float trail_y[TRAILMAX];
    float trail_z[TRAILMAX];
    float trail_r[TRAILMAX];
    float trail_g[TRAILMAX];
    float trail_b[TRAILMAX];
    int trail_head;
    float luminosity;
    int life;
} particle;

typedef struct {
    particle *particles;
    int randomize;
    float c[NUMCONSTS];
    float cv[NUMCONSTS];
} flux;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_fluxes;
    int d_particles;
    int d_trail;
    int d_geometry;
    int d_size;
    int d_complexity;
    int d_randomize;
    int d_expansion;
    int d_instability;
    int d_rotation;
    int d_blur;

    float cos_camera, sin_camera;
    float camera_angle;
    flux *fluxes;
    unsigned int tex;
    unsigned char light_texture[LIGHTSIZE * LIGHTSIZE];
    float lumdiff;
    int whichparticle;
} flux_configuration;

static flux_configuration *bps = NULL;

static int d_fluxes;
static int d_particles;
static int d_trail;
static int d_geometry;
static int d_size;
static int d_complexity;
static int d_randomize;
static int d_expansion;
static int d_instability;
static int d_rotation;
static int d_blur;

static XrmOptionDescRec opts[] = {
    { "-fluxes",      ".fluxes",      XrmoptionSepArg, 0 },
    { "-particles",   ".particles",   XrmoptionSepArg, 0 },
    { "-trail",       ".trail",       XrmoptionSepArg, 0 },
    { "-geometry",    ".geometry",    XrmoptionSepArg, 0 },
    { "-size",        ".size",        XrmoptionSepArg, 0 },
    { "-complexity",  ".complexity",  XrmoptionSepArg, 0 },
    { "-randomize",   ".randomize",   XrmoptionSepArg, 0 },
    { "-expansion",   ".expansion",   XrmoptionSepArg, 0 },
    { "-instability", ".instability", XrmoptionSepArg, 0 },
    { "-rotation",    ".rotation",    XrmoptionSepArg, 0 },
    { "-blur",        ".blur",        XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_fluxes,      "fluxes",      "Fluxes",      DEF_FLUXES,      t_Int },
    { &d_particles,   "particles",   "Particles",   DEF_PARTICLES,   t_Int },
    { &d_trail,       "trail",       "Trail",       DEF_TRAIL,       t_Int },
    { &d_geometry,    "geometry",    "Geometry",    DEF_GEOMETRY,    t_Int },
    { &d_size,        "size",        "Size",        DEF_SIZE,        t_Int },
    { &d_complexity,  "complexity",  "Complexity",  DEF_COMPLEXITY,  t_Int },
    { &d_randomize,   "randomize",   "Randomize",   DEF_RANDOMIZE,   t_Int },
    { &d_expansion,   "expansion",   "Expansion",   DEF_EXPANSION,   t_Int },
    { &d_instability, "instability", "Instability", DEF_INSTABILITY, t_Int },
    { &d_rotation,    "rotation",    "Rotation",    DEF_ROTATION,    t_Int },
    { &d_blur,        "blur",        "Blur",        DEF_BLUR,        t_Int },
};

ENTRYPOINT ModeSpecOpt flux_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

static void particle_init(particle *p, int trail) {
    int i;
    p->x = frand(2.0f) - 1.0f;
    p->y = frand(2.0f) - 1.0f;
    p->z = frand(2.0f) - 1.0f;
    for (i = 0; i < trail; i++) {
        p->trail_x[i] = p->x;
        p->trail_y[i] = p->y;
        p->trail_z[i] = p->z;
        p->trail_r[i] = 0.0f;
        p->trail_g[i] = 0.0f;
        p->trail_b[i] = 0.0f;
    }
    p->trail_head = 0;
    p->luminosity = 0.0f;
    p->life = (int)frand(100.0f) + 50;
}

static void particle_update(particle *p, float *c, float expander,
                            float blower, float lumdiff, int trail) {
    float dx = c[0]*p->y + c[1]*p->z;
    float dy = c[2]*p->z + c[3]*p->x;
    float dz = c[4]*p->x + c[5]*p->y;
    /* the original has additional constants in p' calculation; we use 6 */
    p->x += dx * expander * 0.05f;
    p->y += dy * expander * 0.05f;
    p->z += dz * expander * 0.05f;
    p->z += blower;
    p->luminosity += lumdiff;
    if (p->luminosity < 0.0f) p->luminosity = 0.0f;
    if (p->luminosity > 1.0f) p->luminosity = 1.0f;
    /* record trail */
    p->trail_x[p->trail_head] = p->x;
    p->trail_y[p->trail_head] = p->y;
    p->trail_z[p->trail_head] = p->z;
    p->trail_r[p->trail_head] = p->luminosity;
    p->trail_g[p->trail_head] = p->luminosity * 0.6f;
    p->trail_b[p->trail_head] = p->luminosity * 0.8f;
    p->trail_head = (p->trail_head + 1) % trail;
    p->life--;
    if (p->life <= 0 ||
        p->x < -2.5f || p->x > 2.5f ||
        p->y < -2.5f || p->y > 2.5f ||
        p->z < -2.5f || p->z > 2.5f) {
        particle_init(p, trail);
    }
}

static void flux_init(flux *f, flux_configuration *bp) {
    int i;
    f->particles = (particle *)calloc((size_t)bp->d_particles, sizeof(particle));
    if (!f->particles) exit(1);
    f->randomize = 1;
    for (i = 0; i < NUMCONSTS; i++) {
        f->c[i] = frand(2.0f) - 1.0f;
        f->cv[i] = frand(0.000005f * (float)bp->d_instability * (float)bp->d_instability)
                    + 0.000001f * (float)bp->d_instability * (float)bp->d_instability;
    }
    for (i = 0; i < bp->d_particles; i++)
        particle_init(&f->particles[i], bp->d_trail);
}

static void flux_update(flux *f, flux_configuration *bp) {
    int i;
    float expander, blower;

    if (bp->d_randomize) {
        f->randomize--;
        if (f->randomize <= 0) {
            for (i = 0; i < NUMCONSTS; i++)
                f->c[i] = frand(2.0f) - 1.0f;
            int t = 101 - bp->d_randomize; t = t * t;
            f->randomize = t + (int)frand((float)t);
        }
    }
    for (i = 0; i < NUMCONSTS; i++) {
        f->c[i] += f->cv[i];
        if (f->c[i] >= 1.0f)  { f->c[i] = 1.0f;  f->cv[i] = -f->cv[i]; }
        if (f->c[i] <= -1.0f) { f->c[i] = -1.0f; f->cv[i] = -f->cv[i]; }
    }

    expander = (float)bp->d_expansion * 0.0001f + 0.001f;
    blower   = (float)bp->d_expansion * 0.00005f;
    for (i = 0; i < bp->d_particles; i++)
        particle_update(&f->particles[i], f->c, expander, blower,
                        bp->lumdiff, bp->d_trail);
}

ENTRYPOINT void
reshape_flux(ModeInfo *mi, int width, int height) {
    flux_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, bp->aspect_ratio, 0.1, 2000.0);
}

ENTRYPOINT void
init_flux(ModeInfo *mi) {
    flux_configuration *bp;
    int i, j;
    float x, y, temp;

    if (!bps) {
        bps = (flux_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(flux_configuration));
        if (!bps) exit(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_fluxes      = (d_fluxes      < 1) ? 1 : (d_fluxes      > 10   ? 10   : d_fluxes);
    bp->d_particles   = (d_particles   < 1) ? 1 : (d_particles   > 10000? 10000: d_particles);
    bp->d_trail       = (d_trail       < 1) ? 1 : (d_trail       > TRAILMAX? TRAILMAX : d_trail);
    bp->d_geometry    = (d_geometry    < 0) ? 0 : (d_geometry    > 2    ? 2    : d_geometry);
    bp->d_size        = (d_size        < 1) ? 1 : (d_size        > 100  ? 100  : d_size);
    bp->d_complexity  = (d_complexity  < 1) ? 1 : (d_complexity  > 100  ? 100  : d_complexity);
    bp->d_randomize   = (d_randomize   < 0) ? 0 : (d_randomize   > 100  ? 100  : d_randomize);
    bp->d_expansion   = (d_expansion   < 0) ? 0 : (d_expansion   > 100  ? 100  : d_expansion);
    bp->d_instability = (d_instability < 1) ? 1 : (d_instability > 100  ? 100  : d_instability);
    bp->d_rotation    = (d_rotation    < 0) ? 0 : (d_rotation    > 100  ? 100  : d_rotation);
    bp->d_blur        = (d_blur        < 0) ? 0 : (d_blur        > 100  ? 100  : d_blur);
    bp->lumdiff       = 0.05f * (float)bp->d_instability / 30.0f;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if (bp->d_geometry == 2) {
        for (i = 0; i < LIGHTSIZE; i++) {
            for (j = 0; j < LIGHTSIZE; j++) {
                x = (float)(i - LIGHTSIZE/2) / (float)(LIGHTSIZE/2);
                y = (float)(j - LIGHTSIZE/2) / (float)(LIGHTSIZE/2);
                temp = 1.0f - sqrtf(x*x + y*y);
                if (temp > 1.0f) temp = 1.0f;
                if (temp < 0.0f) temp = 0.0f;
                bp->light_texture[i*LIGHTSIZE + j] = (unsigned char)(255.0f * temp);
            }
        }
        glGenTextures(1, &bp->tex);
        glBindTexture(GL_TEXTURE_2D, bp->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, LIGHTSIZE, LIGHTSIZE,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, bp->light_texture);

        glNewList(1, GL_COMPILE);
            temp = 0.005f * (float)bp->d_size;
            glBindTexture(GL_TEXTURE_2D, bp->tex);
            glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0.0f, 0.0f); glVertex3f(-temp, -temp, 0.0f);
                glTexCoord2f(1.0f, 0.0f); glVertex3f( temp, -temp, 0.0f);
                glTexCoord2f(0.0f, 1.0f); glVertex3f(-temp,  temp, 0.0f);
                glTexCoord2f(1.0f, 1.0f); glVertex3f( temp,  temp, 0.0f);
            glEnd();
        glEndList();
    } else if (bp->d_geometry == 1) {
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        {
            float ambient[4]  = {0.4f, 0.4f, 0.4f, 0.0f};
            float diffuse[4]  = {0.8f, 0.8f, 0.8f, 0.0f};
            float specular[4] = {1.0f, 1.0f, 1.0f, 0.0f};
            float position[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
            glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
            glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
            glLightfv(GL_LIGHT0, GL_POSITION, position);
            glEnable(GL_COLOR_MATERIAL);
            glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
        }
        glNewList(1, GL_COMPILE);
        {
            int slices = 6, stacks = 4;
            int i2, j2;
            float radius = 0.005f * (float)bp->d_size;
            for (i2 = 0; i2 < stacks; i2++) {
                float t1 = (float)i2 * (float)M_PI / (float)stacks - (float)M_PI/2.0f;
                float t2 = (float)(i2+1) * (float)M_PI / (float)stacks - (float)M_PI/2.0f;
                glBegin(GL_TRIANGLE_STRIP);
                for (j2 = 0; j2 <= slices; j2++) {
                    float ph = (float)j2 * 2.0f * (float)M_PI / (float)slices;
                    float nx, ny, nz;
                    nx = cosf(t2)*cosf(ph); ny = sinf(t2); nz = cosf(t2)*sinf(ph);
                    glNormal3f(nx, ny, nz); glVertex3f(nx*radius, ny*radius, nz*radius);
                    nx = cosf(t1)*cosf(ph); ny = sinf(t1); nz = cosf(t1)*sinf(ph);
                    glNormal3f(nx, ny, nz); glVertex3f(nx*radius, ny*radius, nz*radius);
                }
                glEnd();
            }
        }
        glEndList();
    }

    bp->fluxes = (flux *)calloc((size_t)bp->d_fluxes, sizeof(flux));
    if (!bp->fluxes) exit(1);
    for (i = 0; i < bp->d_fluxes; i++) flux_init(&bp->fluxes[i], bp);

    bp->frame_time = 0.016f;
    bp->camera_angle = 0.0f;
    bp->cos_camera = 1.0f;
    bp->sin_camera = 0.0f;
    bp->whichparticle = 0;
}

ENTRYPOINT void
draw_flux(ModeInfo *mi) {
    flux_configuration *bp = &bps[MI_SCREEN(mi)];
    int i, j, k;

    if (bp->d_blur) {
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
            glLoadIdentity();
            glOrtho(0.0, 1.0, 0.0, 1.0, 1.0, -1.0);
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
                glLoadIdentity();
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_BLEND);
                glDisable(GL_DEPTH_TEST);
                glColor4f(0.0f, 0.0f, 0.0f, 0.5f - (sqrtf(sqrtf((float)bp->d_blur)) * 0.15495f));
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

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -2.5f);

    bp->camera_angle += 0.01f * (float)bp->d_rotation;
    if (bp->camera_angle >= 360.0f) bp->camera_angle -= 360.0f;
    if (bp->d_geometry == 1) {
        glRotatef(bp->camera_angle, 0.0f, 1.0f, 0.0f);
    } else {
        bp->cos_camera = cosf(bp->camera_angle * DEG2RAD);
        bp->sin_camera = sinf(bp->camera_angle * DEG2RAD);
    }

    switch (bp->d_geometry) {
    case 0:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glEnable(GL_BLEND);
        glEnable(GL_POINT_SMOOTH);
        break;
    case 1:
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
        break;
    case 2:
        glBlendFunc(GL_ONE, GL_ONE);
        glEnable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, bp->tex);
        glEnable(GL_TEXTURE_2D);
        break;
    }

    /* update particles */
    for (i = 0; i < bp->d_fluxes; i++) flux_update(&bp->fluxes[i], bp);

    /* render */
    for (i = 0; i < bp->d_fluxes; i++) {
        flux *f = &bp->fluxes[i];
        for (j = 0; j < bp->d_particles; j++) {
            particle *p = &f->particles[j];
            /* trail lines */
            glLineWidth(1.0f);
            glBegin(GL_LINE_STRIP);
            for (k = 0; k < bp->d_trail; k++) {
                int idx = (p->trail_head + k) % bp->d_trail;
                glColor3f(p->trail_r[idx], p->trail_g[idx], p->trail_b[idx]);
                glVertex3f(p->trail_x[idx], p->trail_y[idx], p->trail_z[idx]);
            }
            glEnd();
            switch (bp->d_geometry) {
            case 0: {
                glPointSize((float)bp->d_size * 0.1f * (1.0f + p->luminosity));
                glBegin(GL_POINTS);
                    glColor3f(p->luminosity, p->luminosity * 0.7f, p->luminosity * 0.9f);
                    glVertex3f(p->x, p->y, p->z);
                glEnd();
                break;
            }
            case 1:
                glPushMatrix();
                    glTranslatef(p->x, p->y, p->z);
                    glColor3f(p->luminosity, p->luminosity * 0.7f, p->luminosity * 0.9f);
                    glScalef(0.5f, 0.5f, 0.5f);
                    glCallList(1);
                glPopMatrix();
                break;
            case 2:
                glPushMatrix();
                    glTranslatef(p->x, p->y, p->z);
                    glColor3f(p->luminosity, p->luminosity * 0.7f, p->luminosity * 0.9f);
                    glCallList(1);
                glPopMatrix();
                break;
            }
        }
    }

    glFinish();
}

ENTRYPOINT void
free_flux(ModeInfo *mi) {
    flux_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;
    if (bp->fluxes) {
        for (i = 0; i < bp->d_fluxes; i++) free(bp->fluxes[i].particles);
        free(bp->fluxes);
        bp->fluxes = NULL;
    }
    if (bp->tex) glDeleteTextures(1, &bp->tex);
    glDeleteLists(1, 1);
}

ENTRYPOINT Bool
flux_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Flux", flux, flux)

#endif /* USE_GL */
