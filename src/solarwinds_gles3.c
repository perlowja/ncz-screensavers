/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Solar Winds.
 *
 * Solar Winds is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Solar Winds is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * solarwinds_gles3.c — Round-15 native GLES3 port of Terence Welsh's
 * "Solar Winds" saver, sourced via erik-larsen/rss-sdl2-gles2's
 * SDL2/GLES2 wrapper around the original RSS-GLX solarWinds.cpp.
 *
 * Algorithm preserved: each "wind" is a closed system with N emitters
 * that drift toward the camera (z increasing). At each frame one new
 * particle is emitted per emitter; particles are integrated through a
 * linear wind field driven by 9 oscillating constants c[0..8]. The
 * difference between the previous and current position determines the
 * particle's RGB color (movement = brightness on that axis). Three
 * rendering modes are supported:
 *
 *   geometry == 0: textured camera-facing quads (light sources)
 *   geometry == 1: GL_POINTS with size scaled by depth
 *   geometry == 2: GL_LINES connecting each particle to its predecessor
 *                  in the same emitter chain (cosmic-strings look)
 *
 * Display-list path: solarWinds.cpp records a single textured quad
 * into list 1 once, then glCallList(1) per particle. The GLES3 path
 * goes through gles3_compat's glNewList/glCallList shim, which records
 * the immediate-mode draws and replays them on call — semantically
 * equivalent to the GL1 display list at the call site.
 */

#define DEFAULTS	"*delay:     20000   \n" \
			"*winds:     1       \n" \
			"*emitters:  30      \n" \
			"*particles: 2000    \n" \
			"*geometry:  0       \n" \
			"*size:      50      \n" \
			"*windspeed: 20      \n" \
			"*emitterspeed: 15   \n" \
			"*particlespeed: 10  \n" \
			"*blur:      40      \n"

#define release_solarwinds 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>

#ifdef USE_GL

#define DEF_WINDS        "1"
#define DEF_EMITTERS     "30"
#define DEF_PARTICLES    "2000"
#define DEF_GEOMETRY     "0"
#define DEF_SIZE         "50"
#define DEF_WINDSPEED    "20"
#define DEF_EMITTERSPEED "15"
#define DEF_PARTICLESPEED "10"
#define DEF_BLUR         "40"

#define NUMCONSTS 9
#define PIx2 6.28318530718f
#define LIGHTSIZE 64

typedef struct {
    float *emitters;          /* [n_emitters][3] */
    float *particles;         /* [n_particles][6]: xyz + rgb */
    int *line_next;           /* [n_particles] -> index of previous particle in chain */
    int *line_prev;           /* [n_particles] -> index of next particle in chain */
    int *lastparticle;        /* [n_emitters] -> last particle emitted by this emitter */

    int whichparticle;
    float c[NUMCONSTS];
    float ct[NUMCONSTS];
    float cv[NUMCONSTS];
} wind;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_winds;
    int d_emitters;
    int d_particles;
    int d_geometry;
    int d_size;
    int d_windspeed;
    int d_emitterspeed;
    int d_particlespeed;
    int d_blur;

    unsigned int tex;
    wind *winds;
} solarwinds_configuration;

static solarwinds_configuration *bps = NULL;

static int d_winds;
static int d_emitters;
static int d_particles;
static int d_geometry;
static int d_size;
static int d_windspeed;
static int d_emitterspeed;
static int d_particlespeed;
static int d_blur;

static XrmOptionDescRec opts[] = {
    { "-winds",        ".winds",        XrmoptionSepArg, 0 },
    { "-emitters",     ".emitters",     XrmoptionSepArg, 0 },
    { "-particles",    ".particles",    XrmoptionSepArg, 0 },
    { "-geometry",     ".geometry",     XrmoptionSepArg, 0 },
    { "-size",         ".size",         XrmoptionSepArg, 0 },
    { "-windspeed",    ".windspeed",    XrmoptionSepArg, 0 },
    { "-emitterspeed", ".emitterspeed", XrmoptionSepArg, 0 },
    { "-particlespeed",".particlespeed",XrmoptionSepArg, 0 },
    { "-blur",         ".blur",         XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_winds,         "winds",         "Winds",         DEF_WINDS,         t_Int },
    { &d_emitters,      "emitters",      "Emitters",      DEF_EMITTERS,      t_Int },
    { &d_particles,     "particles",     "Particles",     DEF_PARTICLES,     t_Int },
    { &d_geometry,      "geometry",      "Geometry",      DEF_GEOMETRY,      t_Int },
    { &d_size,          "size",          "Size",          DEF_SIZE,          t_Int },
    { &d_windspeed,     "windspeed",     "WindSpeed",     DEF_WINDSPEED,     t_Int },
    { &d_emitterspeed,  "emitterspeed",  "EmitterSpeed",  DEF_EMITTERSPEED,  t_Int },
    { &d_particlespeed, "particlespeed", "ParticleSpeed", DEF_PARTICLESPEED, t_Int },
    { &d_blur,          "blur",          "Blur",          DEF_BLUR,          t_Int },
};

ENTRYPOINT ModeSpecOpt solarwinds_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

static void wind_init(wind *w, solarwinds_configuration *bp) {
    int i;
    w->emitters = (float *)calloc((size_t)bp->d_emitters * 3, sizeof(float));
    w->particles = (float *)calloc((size_t)bp->d_particles * 6, sizeof(float));
    for (i = 0; i < bp->d_emitters; i++) {
        w->emitters[i*3+0] = frand(60.0f) - 30.0f;
        w->emitters[i*3+1] = frand(60.0f) - 30.0f;
        w->emitters[i*3+2] = frand(30.0f) - 15.0f;
    }
    for (i = 0; i < bp->d_particles; i++) {
        w->particles[i*6+2] = 100.0f; /* start behind viewer */
    }
    w->whichparticle = 0;
    if (bp->d_geometry == 2) {
        w->line_next = (int *)calloc((size_t)bp->d_particles, sizeof(int));
        w->line_prev = (int *)calloc((size_t)bp->d_particles, sizeof(int));
        w->lastparticle = (int *)calloc((size_t)bp->d_emitters, sizeof(int));
        for (i = 0; i < bp->d_particles; i++) {
            w->line_next[i] = -1;
            w->line_prev[i] = -1;
        }
        for (i = 0; i < bp->d_emitters; i++) w->lastparticle[i] = i;
    } else {
        w->line_next = NULL;
        w->line_prev = NULL;
        w->lastparticle = NULL;
    }
    for (i = 0; i < NUMCONSTS; i++) {
        w->ct[i] = frand(PIx2);
        w->cv[i] = frand(0.00005f * (float)bp->d_windspeed * (float)bp->d_windspeed)
                    + 0.00001f * (float)bp->d_windspeed * (float)bp->d_windspeed;
    }
}

static void wind_free(wind *w) {
    if (w->emitters)  { free(w->emitters);  w->emitters  = NULL; }
    if (w->particles) { free(w->particles); w->particles = NULL; }
    if (w->line_next) { free(w->line_next); w->line_next = NULL; }
    if (w->line_prev) { free(w->line_prev); w->line_prev = NULL; }
    if (w->lastparticle) { free(w->lastparticle); w->lastparticle = NULL; }
}

static void wind_update(wind *w, solarwinds_configuration *bp) {
    int i;
    float x, y, z;
    float temp;
    static float evel = 0.0f;
    static float pvel = 0.0f;
    static float pointsize = 0.0f;
    static float linesize = 0.0f;
    int d_emitters = bp->d_emitters;
    int d_particles = bp->d_particles;
    int d_geometry = bp->d_geometry;

    if (evel == 0.0f) {
        evel = (float)bp->d_emitterspeed * 0.01f;
        pvel = (float)bp->d_particlespeed * 0.01f;
        pointsize = 0.04f * (float)bp->d_size;
        linesize = 0.005f * (float)bp->d_size;
    }

    /* update constants */
    for (i = 0; i < NUMCONSTS; i++) {
        w->ct[i] += w->cv[i] * bp->frame_time * 60.0f;
        if (w->ct[i] > PIx2) w->ct[i] -= PIx2;
        w->c[i] = cosf(w->ct[i]);
    }

    /* calculate emissions */
    for (i = 0; i < d_emitters; i++) {
        w->emitters[i*3+2] += evel;
        if (w->emitters[i*3+2] > 15.0f) {
            w->emitters[i*3+0] = frand(60.0f) - 30.0f;
            w->emitters[i*3+1] = frand(60.0f) - 30.0f;
            w->emitters[i*3+2] = -15.0f;
        }
        int wp = w->whichparticle;
        w->particles[wp*6+0] = w->emitters[i*3+0];
        w->particles[wp*6+1] = w->emitters[i*3+1];
        w->particles[wp*6+2] = w->emitters[i*3+2];
        if (d_geometry == 2) {
            if (w->line_next[wp] >= 0)
                w->line_prev[w->line_next[wp]] = -1;
            w->line_next[wp] = -1;
            if (w->emitters[i*3+2] == -15.0f)
                w->line_prev[wp] = -1;
            else
                w->line_prev[wp] = w->lastparticle[i];
            w->line_next[w->lastparticle[i]] = wp;
            w->lastparticle[i] = wp;
        }
        w->whichparticle++;
        if (w->whichparticle >= d_particles) w->whichparticle = 0;
    }

    /* calculate particle positions and colors */
    w->c[6] *= 9.0f / (float)bp->d_particlespeed;
    w->c[7] *= 9.0f / (float)bp->d_particlespeed;
    w->c[8] *= 9.0f / (float)bp->d_particlespeed;
    for (i = 0; i < d_particles; i++) {
        x = w->particles[i*6+0];
        y = w->particles[i*6+1];
        z = w->particles[i*6+2];
        w->particles[i*6+0] = x + (w->c[0] * y + w->c[1] * z) * pvel;
        w->particles[i*6+1] = y + (w->c[2] * z + w->c[3] * x) * pvel;
        w->particles[i*6+2] = z + (w->c[4] * x + w->c[5] * y) * pvel;
        w->particles[i*6+3] = fabsf((w->particles[i*6+0] - x) * w->c[6]);
        w->particles[i*6+4] = fabsf((w->particles[i*6+1] - y) * w->c[7]);
        w->particles[i*6+5] = fabsf((w->particles[i*6+2] - z) * w->c[8]);
        if (w->particles[i*6+3] > 1.0f) w->particles[i*6+3] = 1.0f;
        if (w->particles[i*6+4] > 1.0f) w->particles[i*6+4] = 1.0f;
        if (w->particles[i*6+5] > 1.0f) w->particles[i*6+5] = 1.0f;
    }

    /* draw particles */
    switch (d_geometry) {
    case 0:  /* lights */
        for (i = 0; i < d_particles; i++) {
            glColor3f(w->particles[i*6+3], w->particles[i*6+4], w->particles[i*6+5]);
            glPushMatrix();
                glTranslatef(w->particles[i*6+0], w->particles[i*6+1], w->particles[i*6+2]);
                glCallList(1);
            glPopMatrix();
        }
        break;
    case 1:  /* points */
        for (i = 0; i < d_particles; i++) {
            temp = w->particles[i*6+2] + 40.0f;
            if (temp < 0.01f) temp = 0.01f;
            glPointSize(pointsize * temp);
            glBegin(GL_POINTS);
                glColor3f(w->particles[i*6+3], w->particles[i*6+4], w->particles[i*6+5]);
                glVertex3f(w->particles[i*6+0], w->particles[i*6+1], w->particles[i*6+2]);
            glEnd();
        }
        break;
    case 2:  /* lines */
        for (i = 0; i < d_particles; i++) {
            temp = w->particles[i*6+2] + 40.0f;
            if (temp < 0.01f) temp = 0.01f;
            if (w->line_prev[i] >= 0) {
                glLineWidth(linesize * temp);
                glBegin(GL_LINES);
                    if (w->line_next[i] == -1)
                        glColor3f(0.0f, 0.0f, 0.0f);
                    else
                        glColor3f(w->particles[i*6+3], w->particles[i*6+4], w->particles[i*6+5]);
                    glVertex3f(w->particles[i*6+0], w->particles[i*6+1], w->particles[i*6+2]);
                    if (w->line_next[w->line_prev[i]] == -1)
                        glColor3f(0.0f, 0.0f, 0.0f);
                    else
                        glColor3f(w->particles[w->line_prev[i]*6+3],
                                  w->particles[w->line_prev[i]*6+4],
                                  w->particles[w->line_prev[i]*6+5]);
                    glVertex3f(w->particles[w->line_prev[i]*6+0],
                               w->particles[w->line_prev[i]*6+1],
                               w->particles[w->line_prev[i]*6+2]);
                glEnd();
            }
        }
        break;
    }
}

ENTRYPOINT void
reshape_solarwinds(ModeInfo *mi, int width, int height) {
    solarwinds_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(90.0, bp->aspect_ratio, 1.0, 10000.0);
}

ENTRYPOINT void
init_solarwinds(ModeInfo *mi) {
    int i, j;
    solarwinds_configuration *bp;
    float x, y, temp;
    unsigned char light_texture[LIGHTSIZE * LIGHTSIZE];

    if (!bps) {
        bps = (solarwinds_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(solarwinds_configuration));
        if (!bps) ncz_harness_die(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_winds         = (d_winds         < 1) ? 1 : (d_winds         > 10  ? 10  : d_winds);
    bp->d_emitters      = (d_emitters      < 1) ? 1 : (d_emitters      > 1000? 1000: d_emitters);
    bp->d_particles     = (d_particles     < 1) ? 1 : (d_particles     > 10000?10000:d_particles);
    bp->d_geometry      = (d_geometry      < 0) ? 0 : (d_geometry      > 2   ? 2   : d_geometry);
    bp->d_size          = (d_size          < 1) ? 1 : (d_size          > 100 ? 100 : d_size);
    bp->d_windspeed     = (d_windspeed     < 1) ? 1 : (d_windspeed     > 100 ? 100 : d_windspeed);
    bp->d_emitterspeed  = (d_emitterspeed  < 1) ? 1 : (d_emitterspeed  > 100 ? 100 : d_emitterspeed);
    bp->d_particlespeed = (d_particlespeed < 1) ? 1 : (d_particlespeed > 100 ? 100 : d_particlespeed);
    bp->d_blur          = (d_blur          < 1) ? 1 : (d_blur          > 100 ? 100 : d_blur);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!bp->d_geometry) glBlendFunc(GL_ONE, GL_ONE);
    else                 glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_BLEND);

    if (!bp->d_geometry) {
        /* light-texture: 2D radial brightness falloff */
        for (i = 0; i < LIGHTSIZE; i++) {
            for (j = 0; j < LIGHTSIZE; j++) {
                x = (float)(i - LIGHTSIZE/2) / (float)(LIGHTSIZE/2);
                y = (float)(j - LIGHTSIZE/2) / (float)(LIGHTSIZE/2);
                temp = 1.0f - sqrtf(x*x + y*y);
                if (temp > 1.0f) temp = 1.0f;
                if (temp < 0.0f) temp = 0.0f;
                light_texture[i*LIGHTSIZE + j] = (unsigned char)(255.0f * temp);
            }
        }
        glEnable(GL_TEXTURE_2D);
        glGenTextures(1, (GLuint *)&bp->tex);
        glBindTexture(GL_TEXTURE_2D, bp->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, LIGHTSIZE, LIGHTSIZE,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, light_texture);

        /* record display list 1: textured quad sized to d_size */
        temp = 0.02f * (float)bp->d_size;
        glNewList(1, GL_COMPILE);
            glBindTexture(GL_TEXTURE_2D, bp->tex);
            glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0.0f, 0.0f); glVertex3f(-temp, -temp, 0.0f);
                glTexCoord2f(1.0f, 0.0f); glVertex3f( temp, -temp, 0.0f);
                glTexCoord2f(0.0f, 1.0f); glVertex3f(-temp,  temp, 0.0f);
                glTexCoord2f(1.0f, 1.0f); glVertex3f( temp,  temp, 0.0f);
            glEnd();
        glEndList();
    }

    if (bp->d_geometry == 1) {
        glEnable(GL_POINT_SMOOTH);
    }
    if (bp->d_geometry == 2) {
        glEnable(GL_LINE_SMOOTH);
    }

    bp->winds = (wind *)calloc((size_t)bp->d_winds, sizeof(wind));
    if (!bp->winds) ncz_harness_die(1);
    for (i = 0; i < bp->d_winds; i++) wind_init(&bp->winds[i], bp);

    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_solarwinds(ModeInfo *mi) {
    solarwinds_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;

    if (!bp->winds) return;

    if (!bp->d_blur) {
        glClear(GL_COLOR_BUFFER_BIT);
    } else {
        /* overlay a semi-transparent black quad to fade the previous frame */
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
            glLoadIdentity();
            glOrtho(0.0, 1.0, 0.0, 1.0, 1.0, -1.0);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.0f, 0.0f, 0.0f, 0.5f - (float)bp->d_blur * 0.0049f);
            glBegin(GL_TRIANGLE_STRIP);
                glVertex3f(0.0f, 0.0f, 0.0f);
                glVertex3f(1.0f, 0.0f, 0.0f);
                glVertex3f(0.0f, 1.0f, 0.0f);
                glVertex3f(1.0f, 1.0f, 0.0f);
            glEnd();
            if (bp->d_geometry == 0) glBlendFunc(GL_ONE, GL_ONE);
            else                     glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -15.0f);

    for (i = 0; i < bp->d_winds; i++) wind_update(&bp->winds[i], bp);

    glFinish();
}

ENTRYPOINT void
free_solarwinds(ModeInfo *mi) {
    solarwinds_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;
    if (bp->winds) {
        for (i = 0; i < bp->d_winds; i++) wind_free(&bp->winds[i]);
        free(bp->winds);
        bp->winds = NULL;
    }
    if (bp->tex) {
        glDeleteTextures(1, (GLuint *)&bp->tex);
        glDeleteLists(1, 1);
        bp->tex = 0;
    }
}

ENTRYPOINT Bool
solarwinds_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("SolarWinds", solarwinds, solarwinds)

#endif /* USE_GL */
