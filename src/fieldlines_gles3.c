/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Field Lines.
 *
 * Field Lines is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Field Lines is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * fieldlines_gles3.c — Round-15 native GLES3 port of Terence Welsh's
 * "Field Lines" saver, sourced via the erik-larsen/rss-sdl2-gles2
 * SDL2/GLES2 wrapper around the original RSS-GLX fieldlines.cpp.
 *
 * Algorithm preserved: N "ions" with random positions / velocities /
 * +-1 charges. For each ion, eight field lines are traced into the
 * eight octants. Each step of the trace accumulates the inverse-square
 * force from every other ion, and the field line's color tracks the
 * direction the force is pulling it (red = z component, green = x,
 * blue = y). The original `gluPerspective`, `glBegin/glLineStrip` and
 * per-segment `glLineWidth` calls are routed through the gles3_compat
 * GL1 stub layer (gles3_compat.c provides glBegin/glVertex/glColor/
 * glLineWidth/glPushMatrix/glRotatef/glTranslatef/glLight* / glu* as
 * thin shims into the GLES3-native matrix stack + immediate-mode
 * accumulator + shader).
 */

#define DEFAULTS	"*delay:   20000      \n" \
			"*ions:    6          \n" \
			"*stepSize:10         \n" \
			"*maxSteps:300        \n" \
			"*width:   30         \n" \
			"*speed:   10         \n" \
			"*constwidth: False   \n" \
			"*electric: False     \n" \

#define release_fieldlines 0

#include "xscreensaver_compat.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>

#ifdef USE_GL

#define DEF_IONS        "6"
#define DEF_STEPSIZE    "10"
#define DEF_MAXSTEPS    "300"
#define DEF_WIDTH       "30"
#define DEF_SPEED       "10"
#define DEF_CONSTWIDTH  "False"
#define DEF_ELECTRIC    "False"

#define PIx2 6.28318530718f

typedef struct {
    float charge;
    float xyz[3];
    float vel[3];
    float angle;
    float anglevel;
} ion;

typedef struct {
    int screen_width, screen_height;
    float aspect_ratio;
    float wide, high, deep;
    float frame_time;

    int n_ions;
    int step_size;
    int max_steps;
    int line_width;
    int speed;
    Bool const_width;
    Bool electric;

    ion *ions;
} fieldlines_configuration;

static fieldlines_configuration *bps = NULL;

static int d_ions;
static int d_step_size;
static int d_max_steps;
static int d_width;
static int d_speed;
static Bool d_const_width;
static Bool d_electric;

static XrmOptionDescRec opts[] = {
    { "-ions",       ".ions",      XrmoptionSepArg, 0 },
    { "-stepsize",   ".stepSize",  XrmoptionSepArg, 0 },
    { "-maxsteps",   ".maxSteps",  XrmoptionSepArg, 0 },
    { "-width",      ".width",     XrmoptionSepArg, 0 },
    { "-speed",      ".speed",     XrmoptionSepArg, 0 },
    { "-constwidth", ".constwidth",XrmoptionNoArg, "True" },
    { "+constwidth", ".constwidth",XrmoptionNoArg, "False" },
    { "-electric",   ".electric",  XrmoptionNoArg, "True" },
    { "+electric",   ".electric",  XrmoptionNoArg, "False" },
};

static argtype vars[] = {
    { &d_ions,       "ions",       "Ions",       DEF_IONS,       t_Int  },
    { &d_step_size,  "stepSize",   "StepSize",   DEF_STEPSIZE,   t_Int  },
    { &d_max_steps,  "maxSteps",   "MaxSteps",   DEF_MAXSTEPS,   t_Int  },
    { &d_width,      "width",      "Width",      DEF_WIDTH,      t_Int  },
    { &d_speed,      "speed",      "Speed",      DEF_SPEED,      t_Int  },
    { &d_const_width,"constwidth", "ConstWidth", DEF_CONSTWIDTH, t_Bool },
    { &d_electric,   "electric",   "Electric",   DEF_ELECTRIC,   t_Bool },
};

ENTRYPOINT ModeSpecOpt fieldlines_opts = {
    countof(opts), opts, countof(vars), vars, NULL};

static double seconds_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1.0e-6;
}

static void drawfieldline(fieldlines_configuration *bp, int source,
                          float x, float y, float z) {
    int i, j;
    float charge;
    float dist, distsquared, distrec;
    float xyz[3];
    float lastxyz[3];
    float dir[3];
    float end[3];
    float tempvec[3];
    float r, g, b;
    float lastr, lastg, lastb;
    static float brightness = 10000.0f;

    charge = bp->ions[source].charge;
    lastxyz[0] = bp->ions[source].xyz[0];
    lastxyz[1] = bp->ions[source].xyz[1];
    lastxyz[2] = bp->ions[source].xyz[2];
    dir[0] = x; dir[1] = y; dir[2] = z;

    /* first segment */
    r = fabsf(dir[2]) * brightness;
    g = fabsf(dir[0]) * brightness;
    b = fabsf(dir[1]) * brightness;
    if (r > 1.0f) r = 1.0f;
    if (g > 1.0f) g = 1.0f;
    if (b > 1.0f) b = 1.0f;
    lastr = r; lastg = g; lastb = b;
    glColor3f(r, g, b);
    xyz[0] = lastxyz[0] + dir[0];
    xyz[1] = lastxyz[1] + dir[1];
    xyz[2] = lastxyz[2] + dir[2];
    if (bp->electric) {
        xyz[0] += frand((float)bp->step_size * 0.2f) - (float)bp->step_size * 0.1f;
        xyz[1] += frand((float)bp->step_size * 0.2f) - (float)bp->step_size * 0.1f;
        xyz[2] += frand((float)bp->step_size * 0.2f) - (float)bp->step_size * 0.1f;
    }
    if (!bp->const_width)
        glLineWidth((xyz[2] + 300.0f) * 0.000333f * (float)bp->line_width);
    glBegin(GL_LINE_STRIP);
        glColor3f(lastr, lastg, lastb);
        glVertex3fv(lastxyz);
        glColor3f(r, g, b);
        glVertex3fv(xyz);
    if (!bp->const_width) glEnd();

    i = 0;
    for (i = 0; i < bp->max_steps; i++) {
        dir[0] = 0.0f; dir[1] = 0.0f; dir[2] = 0.0f;
        for (j = 0; j < bp->n_ions; j++) {
            float repulsion;
            repulsion = charge * bp->ions[j].charge;
            tempvec[0] = xyz[0] - bp->ions[j].xyz[0];
            tempvec[1] = xyz[1] - bp->ions[j].xyz[1];
            tempvec[2] = xyz[2] - bp->ions[j].xyz[2];
            distsquared = tempvec[0]*tempvec[0] + tempvec[1]*tempvec[1] + tempvec[2]*tempvec[2];
            dist = sqrtf(distsquared);
            if (dist < (float)bp->step_size && i > 2) {
                end[0] = bp->ions[j].xyz[0];
                end[1] = bp->ions[j].xyz[1];
                end[2] = bp->ions[j].xyz[2];
                i = 10000;
            }
            tempvec[0] /= dist;
            tempvec[1] /= dist;
            tempvec[2] /= dist;
            if (distsquared < 1.0f) distsquared = 1.0f;
            dir[0] += tempvec[0] * repulsion / distsquared;
            dir[1] += tempvec[1] * repulsion / distsquared;
            dir[2] += tempvec[2] * repulsion / distsquared;
        }
        lastr = r; lastg = g; lastb = b;
        r = fabsf(dir[2]) * brightness;
        g = fabsf(dir[0]) * brightness;
        b = fabsf(dir[1]) * brightness;
        if (bp->electric) {
            r *= 10.0f; g *= 10.0f; b *= 10.0f;
            if (r > b * 0.5f) r = b * 0.5f;
            if (g > b * 0.3f) g = b * 0.3f;
        }
        if (r > 1.0f) r = 1.0f;
        if (g > 1.0f) g = 1.0f;
        if (b > 1.0f) b = 1.0f;
        distsquared = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
        distrec = (float)bp->step_size / sqrtf(distsquared);
        dir[0] *= distrec; dir[1] *= distrec; dir[2] *= distrec;
        if (bp->electric) {
            dir[0] += frand((float)bp->step_size) - (float)bp->step_size * 0.5f;
            dir[1] += frand((float)bp->step_size) - (float)bp->step_size * 0.5f;
            dir[2] += frand((float)bp->step_size) - (float)bp->step_size * 0.5f;
        }
        lastxyz[0] = xyz[0]; lastxyz[1] = xyz[1]; lastxyz[2] = xyz[2];
        xyz[0] += dir[0]; xyz[1] += dir[1]; xyz[2] += dir[2];
        if (!bp->const_width) {
            glLineWidth((xyz[2] + 300.0f) * 0.000333f * (float)bp->line_width);
            glBegin(GL_LINE_STRIP);
        }
            glColor3f(lastr, lastg, lastb);
            glVertex3fv(lastxyz);
            if (i != 10000) {
                if (i == (bp->max_steps - 1))
                    glColor3f(0.0f, 0.0f, 0.0f);
                else
                    glColor3f(r, g, b);
                glVertex3fv(xyz);
                if (i == (bp->max_steps - 1)) glEnd();
            }
    }
    if (i == 10001) {
        glColor3f(r, g, b);
        glVertex3fv(end);
        glEnd();
    }
}

ENTRYPOINT void
reshape_fieldlines(ModeInfo *mi, int width, int height) {
    fieldlines_configuration *bp = &bps[MI_SCREEN(mi)];

    glViewport(0, 0, width, height);

    if (width > height) {
        bp->high = bp->deep = 160.0f;
        bp->wide = bp->high * (float)width / (float)height;
    } else {
        bp->wide = bp->deep = 160.0f;
        bp->high = bp->wide * (float)height / (float)width;
    }

    bp->screen_width  = width;
    bp->screen_height = height;
    bp->aspect_ratio  = (float)width / (float)height;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, bp->aspect_ratio, 1.0, bp->deep * 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -2.0f * bp->deep);
}

ENTRYPOINT void
init_fieldlines(ModeInfo *mi) {
    int i;
    fieldlines_configuration *bp;
    double now;

    if (!bps) {
        bps = (fieldlines_configuration *)
            calloc(MI_NUM_SCREENS(mi), sizeof(fieldlines_configuration));
        if (!bps) ncz_harness_die(1);
    }
    bp = &bps[MI_SCREEN(mi)];

    srand((unsigned)time(NULL));

    bp->n_ions     = (d_ions     < 1) ? 1 : (d_ions     > 50 ? 50 : d_ions);
    bp->step_size  = (d_step_size < 1) ? 1 : (d_step_size > 100 ? 100 : d_step_size);
    bp->max_steps  = (d_max_steps < 1) ? 1 : (d_max_steps > 10000 ? 10000 : d_max_steps);
    bp->line_width = (d_width    < 1) ? 1 : (d_width    > 100 ? 100 : d_width);
    bp->speed      = (d_speed    < 1) ? 1 : (d_speed    > 100 ? 100 : d_speed);
    bp->const_width = d_const_width;
    bp->electric    = d_electric;

    bp->ions = (ion *)calloc((size_t)bp->n_ions, sizeof(ion));
    if (!bp->ions) ncz_harness_die(1);
    for (i = 0; i < bp->n_ions; i++) {
        if (frand(2.0f) > 1.0f) bp->ions[i].charge = -1.0f;
        else                    bp->ions[i].charge =  1.0f;
        bp->ions[i].xyz[0] = frand(2.0f * bp->wide) - bp->wide;
        bp->ions[i].xyz[1] = frand(2.0f * bp->high) - bp->high;
        bp->ions[i].xyz[2] = frand(2.0f * bp->deep) - bp->deep;
        bp->ions[i].vel[0] = frand((float)bp->speed * 4.0f) - (float)bp->speed * 2.0f;
        bp->ions[i].vel[1] = frand((float)bp->speed * 4.0f) - (float)bp->speed * 2.0f;
        bp->ions[i].vel[2] = frand((float)bp->speed * 4.0f) - (float)bp->speed * 2.0f;
        bp->ions[i].angle = 0.0f;
        bp->ions[i].anglevel = 0.0005f * (float)bp->speed +
                                0.0005f * frand((float)bp->speed);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if (bp->const_width)
        glLineWidth((float)bp->line_width * 0.1f);

    now = seconds_now();
    bp->frame_time = 0.016f;  /* initial step ~60Hz; updates next frame */
    (void)now;
}

ENTRYPOINT void
draw_fieldlines(ModeInfo *mi) {
    fieldlines_configuration *bp = &bps[MI_SCREEN(mi)];
    int i;
    static float s;

    if (!bp->ions) return;

    if (s == 0.0f)
        s = sqrtf((float)bp->step_size * (float)bp->step_size * 0.333f);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Update ions */
    for (i = 0; i < bp->n_ions; i++) {
        ion *ion_ = &bp->ions[i];
        ion_->xyz[0] += ion_->vel[0] * bp->frame_time;
        ion_->xyz[1] += ion_->vel[1] * bp->frame_time;
        ion_->xyz[2] += ion_->vel[2] * bp->frame_time;
        if (ion_->xyz[0] >  bp->wide) ion_->vel[0] -= 0.1f * (float)bp->speed;
        if (ion_->xyz[0] < -bp->wide) ion_->vel[0] += 0.1f * (float)bp->speed;
        if (ion_->xyz[1] >  bp->high) ion_->vel[1] -= 0.1f * (float)bp->speed;
        if (ion_->xyz[1] < -bp->high) ion_->vel[1] += 0.1f * (float)bp->speed;
        if (ion_->xyz[2] >  bp->deep) ion_->vel[2] -= 0.1f * (float)bp->speed;
        if (ion_->xyz[2] < -bp->deep) ion_->vel[2] += 0.1f * (float)bp->speed;
        ion_->angle += ion_->anglevel;
        if (ion_->angle > PIx2) ion_->angle -= PIx2;
    }

    for (i = 0; i < bp->n_ions; i++) {
        drawfieldline(bp, i,  s,  s,  s);
        drawfieldline(bp, i,  s,  s, -s);
        drawfieldline(bp, i,  s, -s,  s);
        drawfieldline(bp, i,  s, -s, -s);
        drawfieldline(bp, i, -s,  s,  s);
        drawfieldline(bp, i, -s,  s, -s);
        drawfieldline(bp, i, -s, -s,  s);
        drawfieldline(bp, i, -s, -s, -s);
    }

    glFinish();
}

ENTRYPOINT void
free_fieldlines(ModeInfo *mi) {
    fieldlines_configuration *bp = &bps[MI_SCREEN(mi)];
    if (bp->ions) { free(bp->ions); bp->ions = NULL; }
}

ENTRYPOINT Bool
fieldlines_handle_event(ModeInfo *mi, XEvent *event) {
    (void)mi; (void)event;
    return False;
}

XSCREENSAVER_MODULE_2 ("FieldLines", fieldlines, fieldlines)

#endif /* USE_GL */
