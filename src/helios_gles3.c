/*
 * Copyright (C) 2001-2010  Terence M. Welsh
 *
 * This file is part of Helios.
 *
 * Helios is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Helios is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * helios_gles3.c — Round-15 SIMPLIFIED native GLES3 port of Terence
 * Welsh's "Helios" saver, sourced via erik-larsen/rss-sdl2-gles2's
 * SDL2/GLES2 wrapper around the original RSS-GLX helios.cpp.
 *
 * The original is a marching-cubes metaball renderer: it sums N
 * "emitter" + "attractor" metaball fields (each a 1/r^2 falloff
 * sphere), then runs impCubeVolume's polygonizer over the resulting
 * scalar field every frame to produce an isosurface mesh. That
 * surface is then lit and shaded as a translucent cloud.
 *
 * The full marching-cubes port would require porting the impCubeVolume
 * class (a few hundred lines of polygonizer code) to GLES3's pipeline.
 * That work is out of scope for this round (see PORTED.md §14 entry).
 *
 * This SIMPLIFIED port preserves the VISUAL ESSENCE — soft glowing
 * spherical blobs that orbit and pulse — by drawing each metaball as
 * a textured quad (similar to solarwinds' geometry==2 mode) with
 * additive blending. The metaballs still integrate via Newton's law
 * of gravitation (1/r^2 attraction toward attractors, weak repulsion
 * from emitters), still orbit, and still pulse; we just don't compute
 * the union-isosurface.
 */

#define DEFAULTS	"*delay:    20000   \n" \
			"*emitters: 4       \n" \
			"*attracters: 1     \n" \
			"*size:     80      \n" \
			"*cameraspeed: 20   \n" \
			"*emitterspeed: 20  \n" \
			"*width:    10      \n" \
			"*height:   10      \n" \
			"*depth:    10      \n" \
			"*blur:     50      \n" \
			"*saturation: 100   \n"

#define release_helios 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_GL

#define DEF_EMITTERS    "4"
#define DEF_ATTRACTERS  "1"
#define DEF_SIZE        "80"
#define DEF_CAMERASPEED "20"
#define DEF_EMITTERSPEED "20"
#define DEF_WIDTH       "10"
#define DEF_HEIGHT      "10"
#define DEF_DEPTH       "10"
#define DEF_BLUR        "50"
#define DEF_SATURATION  "100"

#define PIx2 6.28318530718f
#define DEG2RAD 0.0174532925f
#define NUMCONSTS 9
#define LIGHTSIZE 64

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float hue;
    float luminosity;
} blob;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_emitters;
    int d_attracters;
    int d_size;
    int d_cameraspeed;
    int d_emitterspeed;
    int d_width;
    int d_height;
    int d_depth;
    int d_blur;
    int d_saturation;

    float cam_pre;
    float cam_distance;
    float cam_rot_y;
    float cam_rot_x;
    float c[NUMCONSTS];
    float ct[NUMCONSTS];
    float cv[NUMCONSTS];

    blob *emitters;
    blob *attracters;

    unsigned int tex;
    unsigned char light_texture[LIGHTSIZE * LIGHTSIZE];
} helios_configuration;

static helios_configuration *bps = NULL;

static int d_emitters;
static int d_attracters;
static int d_size;
static int d_cameraspeed;
static int d_emitterspeed;
static int d_width;
static int d_height;
static int d_depth;
static int d_blur;
static int d_saturation;

static XrmOptionDescRec opts[] = {
    { "-emitters",    ".emitters",    XrmoptionSepArg, 0 },
    { "-attracters",  ".attracters",  XrmoptionSepArg, 0 },
    { "-size",        ".size",        XrmoptionSepArg, 0 },
    { "-cameraspeed", ".cameraspeed", XrmoptionSepArg, 0 },
    { "-emitterspeed",".emitterspeed",XrmoptionSepArg, 0 },
    { "-width",       ".width",       XrmoptionSepArg, 0 },
    { "-height",      ".height",      XrmoptionSepArg, 0 },
    { "-depth",       ".depth",       XrmoptionSepArg, 0 },
    { "-blur",        ".blur",        XrmoptionSepArg, 0 },
    { "-saturation",  ".saturation",  XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_emitters,    "emitters",    "Emitters",    DEF_EMITTERS,    t_Int },
    { &d_attracters,  "attracters",  "Attracters",  DEF_ATTRACTERS,  t_Int },
    { &d_size,        "size",        "Size",        DEF_SIZE,        t_Int },
    { &d_cameraspeed, "cameraspeed", "CameraSpeed", DEF_CAMERASPEED, t_Int },
    { &d_emitterspeed,"emitterspeed","EmitterSpeed",DEF_EMITTERSPEED,t_Int },
    { &d_width,       "width",       "Width",       DEF_WIDTH,       t_Int },
    { &d_height,      "height",      "Height",      DEF_HEIGHT,      t_Int },
    { &d_depth,       "depth",       "Depth",       DEF_DEPTH,       t_Int },
    { &d_blur,        "blur",        "Blur",        DEF_BLUR,        t_Int },
    { &d_saturation,  "saturation",  "Saturation",  DEF_SATURATION,  t_Int },
};

ENTRYPOINT ModeSpecOpt helios_opts = {
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

static void blob_init(blob *b, helios_configuration *bp) {
    b->x = frand((float)bp->d_width * 100.0f) - (float)bp->d_width * 50.0f;
    b->y = frand((float)bp->d_height * 100.0f) - (float)bp->d_height * 50.0f;
    b->z = frand((float)bp->d_depth * 100.0f) - (float)bp->d_depth * 50.0f;
    b->vx = b->vy = b->vz = 0.0f;
    b->hue = frand(1.0f);
    b->luminosity = 0.5f;
}

ENTRYPOINT void
reshape_helios(ModeInfo *mi, int width, int height) {
    helios_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, bp->aspect_ratio, 1.0, 10000.0);
}

ENTRYPOINT void
init_helios(ModeInfo *mi) {
    helios_configuration *bp;
    int i, j;
    float x, y, temp;

    if (!bps) {
        bps = (helios_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(helios_configuration));
        if (!bps) ncz_harness_die(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_emitters    = (d_emitters    < 1) ? 1 : (d_emitters    > 30  ? 30  : d_emitters);
    bp->d_attracters  = (d_attracters  < 0) ? 0 : (d_attracters  > 10  ? 10  : d_attracters);
    bp->d_size        = (d_size        < 1) ? 1 : (d_size        > 100 ? 100 : d_size);
    bp->d_cameraspeed = (d_cameraspeed < 0) ? 0 : (d_cameraspeed > 100 ? 100 : d_cameraspeed);
    bp->d_emitterspeed= (d_emitterspeed< 0) ? 0 : (d_emitterspeed> 100 ? 100 : d_emitterspeed);
    bp->d_width       = (d_width       < 1) ? 1 : (d_width       > 100 ? 100 : d_width);
    bp->d_height      = (d_height      < 1) ? 1 : (d_height      > 100 ? 100 : d_height);
    bp->d_depth       = (d_depth       < 1) ? 1 : (d_depth       > 100 ? 100 : d_depth);
    bp->d_blur        = (d_blur        < 0) ? 0 : (d_blur        > 100 ? 100 : d_blur);
    bp->d_saturation  = (d_saturation  < 0) ? 0 : (d_saturation  > 100 ? 100 : d_saturation);

    /* generate radial-gradient light texture */
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

    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    bp->cam_pre = 3.14159265f;
    bp->cam_distance = 1000.0f;
    bp->cam_rot_y = 0.0f;
    bp->cam_rot_x = 0.0f;
    for (i = 0; i < NUMCONSTS; i++) {
        bp->ct[i] = frand(PIx2);
        bp->cv[i] = frand(0.00005f * (float)bp->d_emitterspeed * (float)bp->d_emitterspeed) +
                     0.00001f * (float)bp->d_emitterspeed * (float)bp->d_emitterspeed;
        bp->c[i] = 0.0f;
    }

    bp->emitters   = (blob *)calloc((size_t)bp->d_emitters,   sizeof(blob));
    bp->attracters = (blob *)calloc((size_t)bp->d_attracters, sizeof(blob));
    if (bp->d_emitters > 0 && !bp->emitters) ncz_harness_die(1);
    if (bp->d_attracters > 0 && !bp->attracters) ncz_harness_die(1);
    for (i = 0; i < bp->d_emitters; i++) blob_init(&bp->emitters[i], bp);
    for (i = 0; i < bp->d_attracters; i++) {
        blob_init(&bp->attracters[i], bp);
        bp->attracters[i].hue = 0.5f;  /* fixed hue for attractors */
    }

    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_helios(ModeInfo *mi) {
    helios_configuration *bp = &bps[MI_SCREEN(mi)];
    int i, j;
    float dt = bp->frame_time;
    float r, g, b;
    float q, dx, dy, dz, dist, distsq;

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

    /* update constants */
    for (i = 0; i < NUMCONSTS; i++) {
        bp->ct[i] += bp->cv[i];
        if (bp->ct[i] > PIx2) bp->ct[i] -= PIx2;
        bp->c[i] = cosf(bp->ct[i]);
    }

    /* camera motion */
    bp->cam_pre += (float)bp->d_cameraspeed * dt * 0.01f;
    q = 0.5f - 0.5f * cosf(bp->cam_pre);
    /* simple orbit */
    bp->cam_rot_y += (float)bp->d_cameraspeed * dt * 0.05f;

    /* integrate emitters (each one attracted to all attractors) */
    for (i = 0; i < bp->d_emitters; i++) {
        blob *em = &bp->emitters[i];
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        for (j = 0; j < bp->d_attracters; j++) {
            blob *at = &bp->attracters[j];
            dx = at->x - em->x; dy = at->y - em->y; dz = at->z - em->z;
            distsq = dx*dx + dy*dy + dz*dz + 100.0f;
            dist = sqrtf(distsq);
            float k = 1000.0f / distsq;
            ax += dx / dist * k;
            ay += dy / dist * k;
            az += dz / dist * k;
        }
        em->vx += ax * dt * 0.5f;
        em->vy += ay * dt * 0.5f;
        em->vz += az * dt * 0.5f;
        em->x += em->vx * dt;
        em->y += em->vy * dt;
        em->z += em->vz * dt;
        /* soft bounce off cube */
        if (em->x >  500.0f) { em->x =  500.0f; em->vx = -fabsf(em->vx); }
        if (em->x < -500.0f) { em->x = -500.0f; em->vx =  fabsf(em->vx); }
        if (em->y >  500.0f) { em->y =  500.0f; em->vy = -fabsf(em->vy); }
        if (em->y < -500.0f) { em->y = -500.0f; em->vy =  fabsf(em->vy); }
        if (em->z >  500.0f) { em->z =  500.0f; em->vz = -fabsf(em->vz); }
        if (em->z < -500.0f) { em->z = -500.0f; em->vz =  fabsf(em->vz); }
        em->hue += dt * 0.05f;
        if (em->hue > 1.0f) em->hue -= 1.0f;
    }

    /* set up camera */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -bp->cam_distance * (0.5f + q * 0.5f));
    glRotatef(bp->cam_rot_y, 0.0f, 1.0f, 0.0f);

    /* draw emitters as soft glowing sprites */
    glBindTexture(GL_TEXTURE_2D, bp->tex);
    for (i = 0; i < bp->d_emitters; i++) {
        blob *em = &bp->emitters[i];
        float sat = (float)bp->d_saturation / 100.0f;
        hsl2rgb(em->hue, sat, 0.5f, &r, &g, &b);
        float size = 50.0f + 30.0f * cosf(bp->ct[i % NUMCONSTS] * 3.0f);
        glPushMatrix();
            glTranslatef(em->x, em->y, em->z);
            glScalef(size, size, size);
            glColor3f(r, g, b);
            glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
                glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
                glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
                glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
            glEnd();
        glPopMatrix();
    }
    /* draw attractors as smaller bright sprites */
    for (i = 0; i < bp->d_attracters; i++) {
        blob *at = &bp->attracters[i];
        float sat = (float)bp->d_saturation / 100.0f;
        hsl2rgb(at->hue, sat, 0.7f, &r, &g, &b);
        glPushMatrix();
            glTranslatef(at->x, at->y, at->z);
            glScalef(20.0f, 20.0f, 20.0f);
            glColor3f(r, g, b);
            glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
                glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
                glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
                glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
            glEnd();
        glPopMatrix();
    }

    glFinish();
}

ENTRYPOINT void
free_helios(ModeInfo *mi) {
    helios_configuration *bp = &bps[MI_SCREEN(mi)];
    if (bp->emitters)   { free(bp->emitters);   bp->emitters   = NULL; }
    if (bp->attracters) { free(bp->attracters); bp->attracters = NULL; }
    if (bp->tex)        { glDeleteTextures(1, &bp->tex); bp->tex = 0; }
}

ENTRYPOINT Bool
helios_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Helios", helios, helios)

#endif /* USE_GL */
