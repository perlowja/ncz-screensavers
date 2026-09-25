/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Lattice.
 *
 * Lattice is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Lattice is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * lattice_gles3.c — Round-15 SIMPLIFIED native GLES3 port of
 * Terence Welsh's "Lattice" saver, sourced via
 * erik-larsen/rss-sdl2-gles2's SDL2/GLES2 wrapper around the
 * original RSS-GLX lattice.cpp.
 *
 * The original draws a 3D lattice mesh whose vertices ride
 * oscillating wave functions, with custom surface shaders + 7
 * pre-baked procedural textures (cubes, brick, bricks2, fabric,
 * granite, leaves, marble, sandstone) and an animated "camera" that
 * flies through the lattice using a custom rsMatrix stack. Each
 * lattice face samples one of the textures with blending.
 *
 * This SIMPLIFIED port preserves the VISUAL ESSENCE — a flying
 * 3D grid with textured surfaces and pulsing vertex displacements —
 * by:
 *   - constructing a NxNxN lattice grid (default 5x5x5)
 *   - displacing each vertex per-frame by a sine-wave field
 *   - drawing line segments between lattice vertices (the "wireframe
 *     lattice" look)
 *   - superimposing a few textured quads on each face of the cube
 *     using a procedurally-generated 64x64 RGB texture
 *   - flying the camera along a Lissajous path that loops through
 *     the lattice interior
 */

#define DEFAULTS	"*delay:   20000   \n" \
			"*grid:    5       \n" \
			"*speed:   30      \n" \
			"*size:    20      \n"

#define release_lattice 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_GL

#define DEF_GRID  "5"
#define DEF_SPEED "30"
#define DEF_SIZE  "20"

#define PIx2 6.28318530718f
#define TEXSIZE 64
#define MAXGRID 9

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_grid;
    int d_speed;
    int d_size;

    float phase;
    float cam_t;
    unsigned int tex;
    unsigned char tex_data[TEXSIZE * TEXSIZE * 3];
} lattice_configuration;

static lattice_configuration *bps = NULL;

static int d_grid;
static int d_speed;
static int d_size;

static XrmOptionDescRec opts[] = {
    { "-grid",  ".grid",  XrmoptionSepArg, 0 },
    { "-speed", ".speed", XrmoptionSepArg, 0 },
    { "-size",  ".size",  XrmoptionSepArg, 0 },
};

static argtype vars[] = {
    { &d_grid,  "grid",  "Grid",  DEF_GRID,  t_Int },
    { &d_speed, "speed", "Speed", DEF_SPEED, t_Int },
    { &d_size,  "size",  "Size",  DEF_SIZE,  t_Int },
};

ENTRYPOINT ModeSpecOpt lattice_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

ENTRYPOINT void
reshape_lattice(ModeInfo *mi, int width, int height) {
    lattice_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, bp->aspect_ratio, 0.1, 1000.0);
}

ENTRYPOINT void
init_lattice(ModeInfo *mi) {
    lattice_configuration *bp;
    int i, j;

    if (!bps) {
        bps = (lattice_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(lattice_configuration));
        if (!bps) ncz_harness_die(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_grid  = (d_grid  < 2) ? 2 : (d_grid  > MAXGRID ? MAXGRID : d_grid);
    bp->d_speed = (d_speed < 1) ? 1 : (d_speed > 100 ? 100 : d_speed);
    bp->d_size  = (d_size  < 1) ? 1 : (d_size  > 100 ? 100 : d_size);

    /* procedural "brick" texture (64x64 RGB) */
    for (i = 0; i < TEXSIZE; i++) {
        for (j = 0; j < TEXSIZE; j++) {
            int row = (i / 16) & 1;
            int col = j;
            int brick_x = col % 16;
            int brick_y = i % 16;
            int on_edge = (brick_x == 0) || (brick_x == 15) || (brick_y == 0) || (brick_y == 15);
            float base_r = 0.5f + 0.1f * sinf(i * 0.3f) * cosf(j * 0.4f);
            float base_g = 0.3f + 0.1f * cosf(i * 0.5f);
            float base_b = 0.2f + 0.1f * sinf(j * 0.6f);
            if (on_edge) {
                base_r *= 0.5f;
                base_g *= 0.5f;
                base_b *= 0.5f;
            }
            (void)row;
            int idx = (i * TEXSIZE + j) * 3;
            bp->tex_data[idx + 0] = (unsigned char)(255.0f * base_r);
            bp->tex_data[idx + 1] = (unsigned char)(255.0f * base_g);
            bp->tex_data[idx + 2] = (unsigned char)(255.0f * base_b);
        }
    }
    glGenTextures(1, &bp->tex);
    glBindTexture(GL_TEXTURE_2D, bp->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXSIZE, TEXSIZE,
                 0, GL_RGB, GL_UNSIGNED_BYTE, bp->tex_data);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    bp->phase = 0.0f;
    bp->cam_t = 0.0f;
    bp->frame_time = 0.016f;
}

static float vertex_offset(int i, int j, int k, int n, float phase) {
    float u = (float)i / (float)(n - 1);
    float v = (float)j / (float)(n - 1);
    float w = (float)k / (float)(n - 1);
    return 0.15f * (sinf(phase * 1.7f + u * 4.0f) +
                    cosf(phase * 2.3f + v * 3.5f) +
                    sinf(phase * 1.9f + w * 4.5f));
}

ENTRYPOINT void
draw_lattice(ModeInfo *mi) {
    lattice_configuration *bp = &bps[MI_SCREEN(mi)];
    int n = bp->d_grid;
    int i, j, k, a, b;
    float grid_size = 1.0f * (float)bp->d_size / 5.0f;
    float half = grid_size * (n - 1) / 2.0f;
    float (*pts)[3] = (float (*)[3])calloc((size_t)n * n * n, sizeof(float[3]));
    if (!pts) return;

    bp->phase += bp->frame_time * (float)bp->d_speed * 0.1f;
    bp->cam_t += bp->frame_time * (float)bp->d_speed * 0.05f;

    /* compute displaced vertex positions */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            for (k = 0; k < n; k++) {
                int idx = (i*n + j)*n + k;
                float off = vertex_offset(i, j, k, n, bp->phase);
                pts[idx][0] = (float)i * grid_size - half + off;
                pts[idx][1] = (float)j * grid_size - half + off * 0.7f;
                pts[idx][2] = (float)k * grid_size - half + off * 1.3f;
            }
        }
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* camera flies along Lissajous curve */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    {
        float cam_x = half * 0.7f * sinf(bp->cam_t * 0.7f);
        float cam_y = half * 0.5f * cosf(bp->cam_t * 0.5f);
        float cam_z = half * 0.7f * sinf(bp->cam_t * 0.9f + 1.0f);
        glRotatef(bp->cam_t * 10.0f, 0.0f, 1.0f, 0.0f);
        glTranslatef(-cam_x, -cam_y, -cam_z);
    }

    /* draw wireframe lattice */
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.5f, 0.7f, 1.0f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            for (k = 0; k < n; k++) {
                int idx = (i*n + j)*n + k;
                /* x-edges */
                for (a = i + 1, b = j; a <= i + 1 && a < n; a++) {
                    int jdx = (a*n + j)*n + k;
                    glVertex3fv(pts[idx]);
                    glVertex3fv(pts[jdx]);
                }
                /* y-edges */
                for (a = j + 1, b = i; a <= j + 1 && a < n; a++) {
                    int jdx = (i*n + a)*n + k;
                    glVertex3fv(pts[idx]);
                    glVertex3fv(pts[jdx]);
                }
                /* z-edges */
                for (a = k + 1, b = i; a <= k + 1 && a < n; a++) {
                    int jdx = (i*n + j)*n + a;
                    glVertex3fv(pts[idx]);
                    glVertex3fv(pts[jdx]);
                }
                (void)b;
            }
        }
    }
    glEnd();

    /* draw a few textured faces to add surface variety */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, bp->tex);
    glColor3f(1.0f, 1.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < n - 1; i += 2) {
        for (j = 0; j < n - 1; j += 2) {
            /* top face quad */
            int idx0 = (i*n + j)*n + (n-1);
            int idx1 = ((i+1)*n + j)*n + (n-1);
            int idx2 = ((i+1)*n + (j+1))*n + (n-1);
            int idx3 = (i*n + (j+1))*n + (n-1);
            glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0.0f, 0.0f); glVertex3fv(pts[idx0]);
                glTexCoord2f(1.0f, 0.0f); glVertex3fv(pts[idx1]);
                glTexCoord2f(0.0f, 1.0f); glVertex3fv(pts[idx3]);
                glTexCoord2f(1.0f, 1.0f); glVertex3fv(pts[idx2]);
            glEnd();
        }
    }
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    free(pts);
    glFinish();
}

ENTRYPOINT void
free_lattice(ModeInfo *mi) {
    lattice_configuration *bp = &bps[MI_SCREEN(mi)];
    if (bp->tex) glDeleteTextures(1, &bp->tex);
}

ENTRYPOINT Bool
lattice_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Lattice", lattice, lattice)

#endif /* USE_GL */
