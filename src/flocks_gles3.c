/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Flocks.
 *
 * Flocks is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Flocks is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * flocks_gles3.c — Round-15 native GLES3 port of Terence Welsh's
 * "Flocks" saver, sourced via erik-larsen/rss-sdl2-gles2's
 * SDL2/GLES2 wrapper around the original RSS-GLX flocks.cpp.
 *
 * Algorithm preserved: N "leader" bugs and M "follower" bugs fly
 * through a bounded box. Leaders randomly reverse acceleration axes
 * every "craziness" seconds, bouncing off the box walls. Followers
 * chase the nearest leader, occasionally re-picking a closer leader
 * (1-in-10 per frame). Each bug renders either as a textured sphere
 * (geometry=1, lit) or a stretched line/point (geometry=0). When
 * dConnections is on, every follower draws a line back to its leader.
 *
 * HSL→RGB: same inline hsl2rgb as cyclone_gles3.c. The Rgbhsl helper
 * library isn't vendored — the 6-line core is small enough to share.
 */

#define DEFAULTS	"*delay: 20000     \n" \
			"*leaders: 4      \n" \
			"*followers: 1000 \n" \
			"*geometry: 1     \n" \
			"*size: 5         \n" \
			"*complexity: 1   \n" \
			"*speed: 15       \n" \
			"*stretch: 20     \n" \
			"*fadespeed: 15   \n" \
			"*chromatek: False\n" \
			"*connections: False\n"

#define release_flocks 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#ifdef USE_GL

#define DEF_LEADERS   "4"
#define DEF_FOLLOWERS "1000"
#define DEF_GEOMETRY  "1"
#define DEF_SIZE      "5"
#define DEF_COMPLEXITY "1"
#define DEF_SPEED     "15"
#define DEF_STRETCH   "20"
#define DEF_FADESPEED "15"
#define DEF_CHROMATEK "False"
#define DEF_CONNECTIONS "False"

#define R2D 57.2957795131f

typedef struct {
    int type;       /* 0 = leader, 1 = follower */
    float h, s, l;
    float r, g, b;
    float halfr, halfg, halfb;
    float x, y, z;
    float xSpeed, ySpeed, zSpeed, maxSpeed;
    float accel;
    int right, up, forward;
    int leader;
    float craziness;
    float nextChange;
} bug;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float frame_time;

    int d_leaders;
    int d_followers;
    int d_geometry;
    int d_size;
    int d_complexity;
    int d_speed;
    int d_stretch;
    int d_colorfadespeed;
    int d_chromatek;
    int d_connections;

    int wide, high, deep;
    float color_fade;

    bug *lBugs;
    bug *fBugs;
} flocks_configuration;

static flocks_configuration *bps = NULL;

static int d_leaders;
static int d_followers;
static int d_geometry;
static int d_size;
static int d_complexity;
static int d_speed;
static int d_stretch;
static int d_colorfadespeed;
static int d_chromatek;
static int d_connections;

static XrmOptionDescRec opts[] = {
    { "-leaders",    ".leaders",    XrmoptionSepArg, 0 },
    { "-followers",  ".followers",  XrmoptionSepArg, 0 },
    { "-geometry",   ".geometry",   XrmoptionSepArg, 0 },
    { "-size",       ".size",       XrmoptionSepArg, 0 },
    { "-complexity", ".complexity", XrmoptionSepArg, 0 },
    { "-speed",      ".speed",      XrmoptionSepArg, 0 },
    { "-stretch",    ".stretch",    XrmoptionSepArg, 0 },
    { "-fadespeed",  ".fadespeed",  XrmoptionSepArg, 0 },
    { "-chromatek",  ".chromatek",  XrmoptionNoArg, "True" },
    { "+chromatek",  ".chromatek",  XrmoptionNoArg, "False" },
    { "-connections",".connections",XrmoptionNoArg, "True" },
    { "+connections",".connections",XrmoptionNoArg, "False" },
};

static argtype vars[] = {
    { &d_leaders,        "leaders",     "Leaders",     DEF_LEADERS,     t_Int  },
    { &d_followers,      "followers",   "Followers",   DEF_FOLLOWERS,   t_Int  },
    { &d_geometry,       "geometry",    "Geometry",    DEF_GEOMETRY,    t_Int  },
    { &d_size,           "size",        "Size",        DEF_SIZE,        t_Int  },
    { &d_complexity,     "complexity",  "Complexity",  DEF_COMPLEXITY,  t_Int  },
    { &d_speed,          "speed",       "Speed",       DEF_SPEED,       t_Int  },
    { &d_stretch,        "stretch",     "Stretch",     DEF_STRETCH,     t_Int  },
    { &d_colorfadespeed, "fadespeed",   "FadeSpeed",   DEF_FADESPEED,   t_Int  },
    { &d_chromatek,      "chromatek",   "Chromatek",   DEF_CHROMATEK,   t_Bool },
    { &d_connections,    "connections", "Connections", DEF_CONNECTIONS, t_Bool },
};

ENTRYPOINT ModeSpecOpt flocks_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

static void hsl2rgb(float h, float s, float l, float *rOut, float *gOut, float *bOut) {
    float temp1, temp2, tempr, tempg, tempb;
    if (s == 0.0f) { *rOut = *gOut = *bOut = l; return; }
    temp2 = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    temp1 = 2.0f * l - temp2;
    tempr = h + 1.0f / 3.0f; if (tempr > 1.0f) tempr -= 1.0f;
    tempg = h;
    tempb = h - 1.0f / 3.0f; if (tempb < 0.0f) tempb += 1.0f;
    if (tempr < 1.0f/6.0f)      *rOut = temp1 + (temp2-temp1) * 6.0f*tempr;
    else if (tempr < 0.5f)      *rOut = temp2;
    else if (tempr < 2.0f/3.0f) *rOut = temp1 + (temp2-temp1) * (2.0f/3.0f-tempr) * 6.0f;
    else                        *rOut = temp1;
    if (tempg < 1.0f/6.0f)      *gOut = temp1 + (temp2-temp1) * 6.0f*tempg;
    else if (tempg < 0.5f)      *gOut = temp2;
    else if (tempg < 2.0f/3.0f) *gOut = temp1 + (temp2-temp1) * (2.0f/3.0f-tempg) * 6.0f;
    else                        *gOut = temp1;
    if (tempb < 1.0f/6.0f)      *bOut = temp1 + (temp2-temp1) * 6.0f*tempb;
    else if (tempb < 0.5f)      *bOut = temp2;
    else if (tempb < 2.0f/3.0f) *bOut = temp1 + (temp2-temp1) * (2.0f/3.0f-tempb) * 6.0f;
    else                        *bOut = temp1;
}

static void bug_initLeader(bug *b, flocks_configuration *bp) {
    b->type = 0;
    b->h = frand(1.0f); b->s = 1.0f; b->l = 1.0f;
    b->x = frand((float)bp->wide * 2.0f) - (float)bp->wide;
    b->y = frand((float)bp->high * 2.0f) - (float)bp->high;
    b->z = frand((float)bp->wide * 2.0f) + (float)bp->wide * 2.0f;
    b->right = b->up = b->forward = 1;
    b->xSpeed = b->ySpeed = b->zSpeed = 0.0f;
    b->maxSpeed = 8.0f * (float)bp->d_speed;
    b->accel = 13.0f * (float)bp->d_speed;
    b->craziness = frand(4.0f) + 0.05f;
    b->nextChange = 1.0f;
}

static void bug_initFollower(bug *b, flocks_configuration *bp) {
    b->type = 1;
    b->h = frand(1.0f); b->s = 1.0f; b->l = 1.0f;
    b->x = frand((float)bp->wide * 2.0f) - (float)bp->wide;
    b->y = frand((float)bp->high * 2.0f) - (float)bp->high;
    b->z = frand((float)bp->wide * 5.0f) + (float)bp->wide * 2.0f;
    b->right = b->up = b->forward = 0;
    b->xSpeed = b->ySpeed = b->zSpeed = 0.0f;
    b->maxSpeed = (frand(6.0f) + 4.0f) * (float)bp->d_speed;
    b->accel = (frand(4.0f) + 9.0f) * (float)bp->d_speed;
    b->leader = 0;
}

static void bug_update(bug *b, bug *leaders, flocks_configuration *bp) {
    int i;
    float scale[4];

    if (!b->type) { /* leader */
        b->nextChange -= bp->frame_time;
        if (b->nextChange <= 0.0f) {
            if (frand(2.0f) > 1.0f) b->right++;
            if (frand(2.0f) > 1.0f) b->up++;
            if (frand(2.0f) > 1.0f) b->forward++;
            if (b->right   >= 2) b->right   = 0;
            if (b->up      >= 2) b->up      = 0;
            if (b->forward >= 2) b->forward = 0;
            b->nextChange = frand(b->craziness);
        }
        if (b->right)   b->xSpeed += b->accel * bp->frame_time;
        else            b->xSpeed -= b->accel * bp->frame_time;
        if (b->up)      b->ySpeed += b->accel * bp->frame_time;
        else            b->ySpeed -= b->accel * bp->frame_time;
        if (b->forward) b->zSpeed -= b->accel * bp->frame_time;
        else            b->zSpeed += b->accel * bp->frame_time;
        if (b->x < -(float)bp->wide) b->right = 1;
        if (b->x >  (float)bp->wide) b->right = 0;
        if (b->y < -(float)bp->high) b->up = 1;
        if (b->y >  (float)bp->high) b->up = 0;
        if (b->z < -(float)bp->deep) b->forward = 0;
        if (b->z >  (float)bp->deep) b->forward = 1;
        if (bp->d_chromatek) {
            b->h = 0.666667f * ((float)bp->wide - b->z) / (float)(bp->wide + bp->wide);
            if (b->h > 0.666667f) b->h = 0.666667f;
            if (b->h < 0.0f) b->h = 0.0f;
        }
    } else { /* follower */
        if (frand(10.0f) < 1.0f) {
            float oldDistance = 1.0e7f, newDistance;
            for (i = 0; i < bp->d_leaders; i++) {
                newDistance = (leaders[i].x - b->x) * (leaders[i].x - b->x)
                            + (leaders[i].y - b->y) * (leaders[i].y - b->y)
                            + (leaders[i].z - b->z) * (leaders[i].z - b->z);
                if (newDistance < oldDistance) {
                    oldDistance = newDistance;
                    b->leader = i;
                }
            }
        }
        if ((leaders[b->leader].x - b->x) > 0.0f) b->xSpeed += b->accel * bp->frame_time;
        else                                       b->xSpeed -= b->accel * bp->frame_time;
        if ((leaders[b->leader].y - b->y) > 0.0f) b->ySpeed += b->accel * bp->frame_time;
        else                                       b->ySpeed -= b->accel * bp->frame_time;
        if ((leaders[b->leader].z - b->z) > 0.0f) b->zSpeed += b->accel * bp->frame_time;
        else                                       b->zSpeed -= b->accel * bp->frame_time;
        if (bp->d_chromatek) {
            b->h = 0.666667f * ((float)bp->wide - b->z) / (float)(bp->wide + bp->wide);
            if (b->h > 0.666667f) b->h = 0.666667f;
            if (b->h < 0.0f) b->h = 0.0f;
        } else {
            float d = fabsf(b->h - leaders[b->leader].h);
            if (d < (bp->color_fade * bp->frame_time)) {
                b->h = leaders[b->leader].h;
            } else if (d < 0.5f) {
                if (b->h > leaders[b->leader].h) b->h -= bp->color_fade * bp->frame_time;
                else                             b->h += bp->color_fade * bp->frame_time;
            } else {
                if (b->h > leaders[b->leader].h) b->h += bp->color_fade * bp->frame_time;
                else                             b->h -= bp->color_fade * bp->frame_time;
                if (b->h > 1.0f) b->h -= 1.0f;
                if (b->h < 0.0f) b->h += 1.0f;
            }
        }
    }

    if (b->xSpeed >  b->maxSpeed) b->xSpeed =  b->maxSpeed;
    if (b->xSpeed < -b->maxSpeed) b->xSpeed = -b->maxSpeed;
    if (b->ySpeed >  b->maxSpeed) b->ySpeed =  b->maxSpeed;
    if (b->ySpeed < -b->maxSpeed) b->ySpeed = -b->maxSpeed;
    if (b->zSpeed >  b->maxSpeed) b->zSpeed =  b->maxSpeed;
    if (b->zSpeed < -b->maxSpeed) b->zSpeed = -b->maxSpeed;

    b->x += b->xSpeed * bp->frame_time;
    b->y += b->ySpeed * bp->frame_time;
    b->z += b->zSpeed * bp->frame_time;

    if (bp->d_stretch) {
        scale[0] = b->xSpeed * 0.04f;
        scale[1] = b->ySpeed * 0.04f;
        scale[2] = b->zSpeed * 0.04f;
        scale[3] = scale[0]*scale[0] + scale[1]*scale[1] + scale[2]*scale[2];
        if (scale[3] > 0.0f) {
            scale[3] = sqrtf(scale[3]);
            scale[0] /= scale[3]; scale[1] /= scale[3]; scale[2] /= scale[3];
        }
    }
    hsl2rgb(b->h, b->s, b->l, &b->r, &b->g, &b->b);
    b->halfr = b->r * 0.5f; b->halfg = b->g * 0.5f; b->halfb = b->b * 0.5f;
    glColor3f(b->r, b->g, b->b);

    if (bp->d_geometry) {
        /* lit blob */
        glPushMatrix();
            glTranslatef(b->x, b->y, b->z);
            if (bp->d_stretch) {
                scale[3] *= (float)bp->d_stretch * 0.05f;
                if (scale[3] < 1.0f) scale[3] = 1.0f;
                glRotatef(atan2f(-scale[0], -scale[2]) * R2D, 0.0f, 1.0f, 0.0f);
                glRotatef(asinf(scale[1]) * R2D, 1.0f, 0.0f, 0.0f);
                glScalef(1.0f, 1.0f, scale[3]);
            }
            glCallList(1);
        glPopMatrix();
    } else {
        if (bp->d_stretch) {
            glLineWidth((float)bp->d_size * (float)(700 - (int)b->z) * 0.001f);
            scale[0] *= (float)bp->d_stretch;
            scale[1] *= (float)bp->d_stretch;
            scale[2] *= (float)bp->d_stretch;
            glBegin(GL_LINES);
                glVertex3f(b->x - scale[0], b->y - scale[1], b->z - scale[2]);
                glVertex3f(b->x + scale[0], b->y + scale[1], b->z + scale[2]);
            glEnd();
        } else {
            glPointSize((float)bp->d_size * (float)(700 - (int)b->z) * 0.001f);
            glBegin(GL_POINTS);
                glVertex3f(b->x, b->y, b->z);
            glEnd();
        }
    }

    if (bp->d_connections && b->type) {
        glLineWidth(1.0f);
        glBegin(GL_LINES);
            glColor3f(b->halfr, b->halfg, b->halfb);
            glVertex3f(b->x, b->y, b->z);
            glColor3f(leaders[b->leader].halfr, leaders[b->leader].halfg, leaders[b->leader].halfb);
            glVertex3f(leaders[b->leader].x, leaders[b->leader].y, leaders[b->leader].z);
        glEnd();
    }
}

ENTRYPOINT void
reshape_flocks(ModeInfo *mi, int width, int height) {
    flocks_configuration *bp = &bps[MI_SCREEN(mi)];
    glViewport(0, 0, width, height);
    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio = (float)width / (float)height;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(50.0, bp->aspect_ratio, 0.1, 2000.0);
    glMatrixMode(GL_MODELVIEW);

    if (bp->aspect_ratio >= 1.0f) {
        bp->high = bp->deep = 160;
        bp->wide = (int)((float)bp->high * bp->aspect_ratio);
    } else {
        bp->wide = bp->deep = 160;
        bp->high = (int)((float)bp->wide / bp->aspect_ratio);
    }
}

ENTRYPOINT void
init_flocks(ModeInfo *mi) {
    flocks_configuration *bp;
    int i, stacks, slices;
    float ambient[4], diffuse[4], specular[4], position[4];

    if (!bps) {
        bps = (flocks_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(flocks_configuration));
        if (!bps) ncz_harness_die(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->d_leaders        = (d_leaders   < 1) ? 1 : (d_leaders   > 100  ? 100  : d_leaders);
    bp->d_followers      = (d_followers < 0) ? 0 : (d_followers > 10000? 10000: d_followers);
    bp->d_geometry       = (d_geometry  < 0) ? 0 : (d_geometry  > 1    ? 1    : d_geometry);
    bp->d_size           = (d_size      < 1) ? 1 : (d_size      > 100  ? 100  : d_size);
    bp->d_complexity     = (d_complexity<1) ? 1 : (d_complexity> 10   ? 10   : d_complexity);
    bp->d_speed          = (d_speed     < 1) ? 1 : (d_speed     > 100  ? 100  : d_speed);
    bp->d_stretch        = (d_stretch   < 0) ? 0 : (d_stretch   > 100  ? 100  : d_stretch);
    bp->d_colorfadespeed = (d_colorfadespeed < 0) ? 0 : (d_colorfadespeed > 100 ? 100 : d_colorfadespeed);
    bp->d_chromatek      = d_chromatek;
    bp->d_connections    = d_connections;
    bp->color_fade       = (float)bp->d_colorfadespeed * 0.01f;

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_DEPTH_TEST);
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_LINE_SMOOTH);

    if (bp->d_geometry) {
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        ambient[0]=0.25f; ambient[1]=0.25f; ambient[2]=0.25f; ambient[3]=0.0f;
        diffuse[0]=1.0f;  diffuse[1]=1.0f;  diffuse[2]=1.0f;  diffuse[3]=0.0f;
        specular[0]=1.0f; specular[1]=1.0f; specular[2]=1.0f; specular[3]=0.0f;
        position[0]=500.0f; position[1]=500.0f; position[2]=500.0f; position[3]=0.0f;
        glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
        glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
        glLightfv(GL_LIGHT0, GL_POSITION, position);
        glEnable(GL_COLOR_MATERIAL);
        glMaterialf(GL_FRONT, GL_SHININESS, 10.0f);
        glColorMaterial(GL_FRONT, GL_SPECULAR);
        glColor3f(0.7f, 0.7f, 0.7f);
        glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

        /* record display list 1: low-poly sphere */
        stacks = bp->d_complexity + 2;
        slices = bp->d_complexity + 1;
        glNewList(1, GL_COMPILE);
        {
            int i2, j2;
            float radius = (float)bp->d_size * 0.5f;
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
    } else {
        if (bp->d_stretch == 0) {
            glEnable(GL_POINT_SMOOTH);
        }
    }

    bp->lBugs = (bug *)calloc((size_t)bp->d_leaders, sizeof(bug));
    bp->fBugs = (bug *)calloc((size_t)bp->d_followers, sizeof(bug));
    if (bp->d_leaders > 0 && !bp->lBugs) ncz_harness_die(1);
    if (bp->d_followers > 0 && !bp->fBugs) ncz_harness_die(1);
    for (i = 0; i < bp->d_leaders; i++) bug_initLeader(&bp->lBugs[i], bp);
    for (i = 0; i < bp->d_followers; i++) bug_initFollower(&bp->fBugs[i], bp);

    bp->frame_time = 0.016f;
}

ENTRYPOINT void
draw_flocks(ModeInfo *mi) {
    flocks_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -(float)bp->wide * 2.0f);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (i = 0; i < bp->d_leaders; i++) bug_update(&bp->lBugs[i], bp->lBugs, bp);
    for (i = 0; i < bp->d_followers; i++) bug_update(&bp->fBugs[i], bp->lBugs, bp);

    glFinish();
}

ENTRYPOINT void
free_flocks(ModeInfo *mi) {
    flocks_configuration *bp = &bps[MI_SCREEN(mi)];
    if (bp->lBugs) { free(bp->lBugs); bp->lBugs = NULL; }
    if (bp->fBugs) { free(bp->fBugs); bp->fBugs = NULL; }
    glDeleteLists(1, 1);
}

ENTRYPOINT Bool
flocks_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("Flocks", flocks, flocks)

#endif /* USE_GL */
