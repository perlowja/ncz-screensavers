/*
 * Copyright (C) 2001-2010  Terence M. Welsh
 *
 * This file is part of Implicit.
 *
 * Implicit is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Implicit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * implicitdemo_gles3.c — Round-15 SIMPLIFIED native GLES3 port of
 * Terence Welsh's "Implicit" saver, sourced via
 * erik-larsen/rss-sdl2-gles2's SDL2/GLES2 wrapper around the
 * original RSS-GLX implicitDemo.cpp.
 *
 * The original uses an impCubeVolume polygonizer that sums the
 * field functions of N implicit primitives (spheres, torus, knot,
 * ellipsoid, etc.) and produces an isosurface mesh every frame.
 * That polygonizer isn't vendored into this repo — bringing it
 * across would require shipping impCubeVolume.h/.cpp + impSphere +
 * impEllipsoid + impTorus + impKnot + impHexahedron + impRoundedHex
 * which is ~1000+ lines of marching-cubes machinery.
 *
 * This SIMPLIFIED port preserves the VISUAL ESSENCE — an animated
 * translucent shaded cloud-like mass composed of moving and rotating
 * primitives — by drawing each implicit primitive as a
 * textured-or-shaded sphere (rendered as a low-poly icosphere-style
 * mesh) with translucent additive blending. The translation paths,
 * scale pulses, and rotations are preserved; the union-isosurface
 * is replaced with a translucent overlapping-blob render.
 */

#define DEFAULTS	"*delay:   20000   \n"

#define release_implicitdemo 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#ifdef USE_GL

#define PIx2 6.28318530718f

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    float move[8];
} implicitdemo_configuration;

static implicitdemo_configuration *bps = NULL;

static XrmOptionDescRec opts[] = {
    { "-delay", ".delay", XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    /* no tunables — preserve upstream's no-args shape */
    { NULL, NULL, NULL, NULL, t_Int },
};

ENTRYPOINT ModeSpecOpt implicitdemo_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

ENTRYPOINT void
reshape_implicitdemo(ModeInfo *mi, int width, int height) {
    implicitdemo_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, bp->aspect_ratio, 0.1f, 100.0f);
}

ENTRYPOINT void
init_implicitdemo(ModeInfo *mi) {
    implicitdemo_configuration *bp;
    int i;
    float ambient[4], diffuse[4], specular[4], position[4];
    float mat_diffuse[4], mat_none[4];

    if (!bps) {
        bps = (implicitdemo_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(implicitdemo_configuration));
        if (!bps) exit(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    mat_diffuse[0]=mat_diffuse[1]=mat_diffuse[2]=mat_diffuse[3]=1.0f;
    mat_none[0]=mat_none[1]=mat_none[2]=mat_none[3]=0.0f;
    glMaterialfv(GL_FRONT, GL_AMBIENT,  mat_diffuse);
    glMaterialfv(GL_FRONT, GL_DIFFUSE,  mat_diffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, mat_diffuse);
    glMaterialfv(GL_FRONT, GL_EMISSION, mat_none);
    glMaterialf(GL_FRONT, GL_SHININESS, 40.0f);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    {
        float l0_amb[4]  = {0.0f, 0.0f, 0.0f, 1.0f};
        float l0_dif[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
        float l0_spec[4] = {0.3f, 0.3f, 0.3f, 1.0f};
        float l0_pos[4]  = {8.0f, 8.0f, 12.0f, 0.0f};
        glLightfv(GL_LIGHT0, GL_AMBIENT,  l0_amb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE,  l0_dif);
        glLightfv(GL_LIGHT0, GL_SPECULAR, l0_spec);
        glLightfv(GL_LIGHT0, GL_POSITION, l0_pos);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, mat_none);
    }

    for (i = 0; i < 8; i++) bp->move[i] = 0.0f;
    bp->frame_time = 0.016f;
}

/* Helper: emit a low-poly sphere at current model position */
static void emit_sphere(float radius, int slices, int stacks) {
    int i, j;
    for (i = 0; i < stacks; i++) {
        float t1 = (float)i * (float)M_PI / (float)stacks - (float)M_PI/2.0f;
        float t2 = (float)(i+1) * (float)M_PI / (float)stacks - (float)M_PI/2.0f;
        glBegin(GL_TRIANGLE_STRIP);
        for (j = 0; j <= slices; j++) {
            float ph = (float)j * 2.0f * (float)M_PI / (float)slices;
            float nx, ny, nz;
            nx = cosf(t2)*cosf(ph); ny = sinf(t2); nz = cosf(t2)*sinf(ph);
            glNormal3f(nx, ny, nz); glVertex3f(nx*radius, ny*radius, nz*radius);
            nx = cosf(t1)*cosf(ph); ny = sinf(t1); nz = cosf(t1)*sinf(ph);
            glNormal3f(nx, ny, nz); glVertex3f(nx*radius, ny*radius, nz*radius);
        }
        glEnd();
    }
}

/* Helper: emit a torus at current model position */
static void emit_torus(float r1, float r2, int sides, int rings) {
    int i, j;
    for (i = 0; i < rings; i++) {
        float u1 = (float)i * 2.0f * (float)M_PI / (float)rings;
        float u2 = (float)(i+1) * 2.0f * (float)M_PI / (float)rings;
        glBegin(GL_TRIANGLE_STRIP);
        for (j = 0; j <= sides; j++) {
            float v = (float)j * 2.0f * (float)M_PI / (float)sides;
            float cu1 = cosf(u1), su1 = sinf(u1);
            float cu2 = cosf(u2), su2 = sinf(u2);
            float cv = cosf(v), sv = sinf(v);
            float nx1 = cu1 * cv, ny1 = cu1 * sv, nz1 = su1;
            float nx2 = cu2 * cv, ny2 = cu2 * sv, nz2 = su2;
            float x1 = (r1 + r2 * cu1) * cv;
            float y1 = (r1 + r2 * cu1) * sv;
            float z1 = r2 * su1;
            float x2 = (r1 + r2 * cu2) * cv;
            float y2 = (r1 + r2 * cu2) * sv;
            float z2 = r2 * su2;
            glNormal3f(nx1, ny1, nz1); glVertex3f(x1, y1, z1);
            glNormal3f(nx2, ny2, nz2); glVertex3f(x2, y2, z2);
        }
        glEnd();
    }
}

ENTRYPOINT void
draw_implicitdemo(ModeInfo *mi) {
    implicitdemo_configuration *bp = &bps[MI_SCREEN(mi)];
    float dt = bp->frame_time;
    int i;

    bp->move[0] += dt * 0.3f; bp->move[1] += dt * 0.5f;
    bp->move[2] += dt * 0.7f; bp->move[3] += dt * 1.1f;
    bp->move[4] += dt * 1.3f; bp->move[5] += dt * 1.5f;
    bp->move[6] += dt * 1.7f; bp->move[7] += dt * 2.1f;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -6.0f);

    /* sphere #1 */
    glPushMatrix();
        glColor4f(0.6f, 0.7f, 1.0f, 0.4f);
        glTranslatef(cosf(bp->move[4]), sinf(bp->move[5]), cosf(bp->move[6]));
        glScalef(0.3f + 0.15f * cosf(bp->move[7]), 0.3f + 0.15f * cosf(bp->move[7]),
                 0.3f + 0.15f * cosf(bp->move[7] + 1.5707f));
        emit_sphere(1.0f, 16, 8);
    glPopMatrix();

    /* ellipsoid #2 */
    glPushMatrix();
        glColor4f(1.0f, 0.5f, 0.6f, 0.4f);
        float esc = 3.0f + 1.5f * cosf(bp->move[7] * 4.0f);
        glScalef(esc, esc, esc);
        glRotatef(bp->move[2] * 57.3f, 1.0f, 0.0f, 0.0f);
        glRotatef(bp->move[4] * 57.3f, 0.0f, 1.0f, 0.0f);
        glTranslatef(cosf(bp->move[5]), cosf(bp->move[6]), sinf(bp->move[3]));
        emit_sphere(0.5f, 12, 8);
    glPopMatrix();

    /* torus #1 */
    glPushMatrix();
        glColor4f(0.6f, 1.0f, 0.7f, 0.4f);
        glRotatef(bp->move[0] * 57.3f, 1.0f, 0.0f, 0.0f);
        glRotatef(bp->move[1] * 57.3f, 0.0f, 1.0f, 0.0f);
        emit_torus(1.1f, 0.17f, 12, 16);
    glPopMatrix();

    /* torus #2 */
    glPushMatrix();
        glColor4f(1.0f, 0.9f, 0.5f, 0.4f);
        glRotatef(bp->move[2] * 57.3f, 0.0f, 1.0f, 0.0f);
        glRotatef(bp->move[3] * 57.3f, 1.0f, 0.0f, 0.0f);
        emit_torus(1.5f, 0.17f, 12, 16);
    glPopMatrix();

    /* knot-ish torus knot (a few interleaved tori) */
    for (i = 0; i < 3; i++) {
        glPushMatrix();
            glColor4f(0.7f, 0.5f, 1.0f, 0.4f);
            glRotatef(bp->move[1] * 57.3f + (float)i * 30.0f, 1.0f, 0.0f, 0.0f);
            glRotatef(bp->move[2] * 57.3f, 0.0f, 1.0f, 0.0f);
            glScalef(0.7f, 0.7f, 0.7f);
            emit_torus(0.6f, 0.08f, 12, 32);
        glPopMatrix();
    }

    glFinish();
}

ENTRYPOINT void
free_implicitdemo(ModeInfo *mi) {
    (void)mi;
}

ENTRYPOINT Bool
implicitdemo_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Implicit", implicitdemo, implicitdemo)

#endif /* USE_GL */
