/* _POSIX_C_SOURCE for M_PI; _DEFAULT_SOURCE for the legacy math.h bits. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/*
 * gles3_companion.c — Phase 1 pilot port of xscreensaver's companion.c
 *                     to native GLES3, routed through the gles3_compat
 *                     foundation. Companion is the test case for the
 *                     display-list-to-VBO conversion: it loads 3
 *                     pre-baked gllist chains (companion_quad,
 *                     companion_disc, companion_heart) and renders
 *                     them via `renderList()` per frame, and it uses
 *                     glNewList/glEndList/glCallList to record and
 *                     replay the FULL_CUBE display list (a mixed
 *                     batch of inline quads and the 3 gllist chains).
 *
 * This file IS a hand-port — the same model as gles3_boing.c. The
 * mechanical "drop-in GL1 stub" route (Phase 4+ in the migration doc)
 * would let companion.c be ported with near-zero source edits; we
 * do it by hand here to prove the lower layer end-to-end on a real
 * load-bearing piece of the gllist cluster — the cluster that the
 * audit flagged as the largest single cause of "renders black" in
 * the gl4es era.
 *
 * Companion.c's algorithm — the bouncing-cube animation, per-cube
 * rotation, trackball, etc. — is preserved exactly; only the GL1
 * calls are translated.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "gles3_compat.h"
#include "xscreensaver_compat.h"
#include "gllist.h"   /* for struct gllist (already includes xlockmoreI.h) */
#include "rotator.h"

#define DEFAULTS	"*delay:	30000       \n" \
			"*showFPS:      False       \n" \
			"*count:        3           \n" \
			"*wireframe:    False       \n" \

#define release_companion 0

#define release_cube 0

#define DEF_SPEED  "1.0"
#define DEF_SPIN   "False"
#define DEF_WANDER "False"

typedef struct {
  GLfloat x, y, z;
  GLfloat ix, iy, iz;
  GLfloat dx, dy, dz;
  GLfloat ddx, ddy, ddz;
  GLfloat zr;
  rotator *rot;
  Bool spinner_p;
} floater;

typedef struct {
  trackball_state *trackball;
  Bool button_down_p;

  int cube_polys;

  int nfloaters;
  floater *floaters;

  /* The 3 gllist chains for the cube — uploaded once to VBOs at init,
   * played every frame. */
  nczGLListChain chain_quad;
  nczGLListChain chain_disc;
  nczGLListChain chain_heart;

} companion_configuration;

static companion_configuration *bps = NULL;

static Bool do_spin;
static Bool do_wander;

static XrmOptionDescRec opts[] = {
  { "-speed",      ".speed",    XrmoptionSepArg, 0       },
  { "-spin",       ".spin",     XrmoptionNoArg,  "True"  },
  { "+spin",       ".spin",     XrmoptionNoArg,  "False" },
  { "-wander",     ".wander",   XrmoptionNoArg,  "True"  },
  { "+wander",     ".wander",   XrmoptionNoArg,  "False" },
};

static argtype vars[] = {
  {&do_spin,   "spin",     "Spin",    DEF_SPIN,     t_Bool},
  {&do_wander, "wander",   "Wander",  DEF_WANDER,   t_Bool},
};

/* Hack: speed isn't in xscreensaver's vars for companion, but upstream
 * uses it. We add it here as a float (default 1.0). */
static GLfloat speed = 1.0f;

ENTRYPOINT ModeSpecOpt cube_opts = {countof(opts), opts, countof(vars), vars, NULL};

/* Companion object gllists — declared in src/companion_{quad,disc,heart}.c */
extern const struct gllist *companion_quad, *companion_disc, *companion_heart;

static void
reset_floater (ModeInfo *mi, floater *f)
{
  companion_configuration *bp = &bps[MI_SCREEN(mi)];

  f->y = -28.0f;
  f->x = f->ix;
  f->z = f->iz;

  f->dy = 5.0f;
  f->dx = 0;
  f->dz = 0;

  f->ddy = speed * 0.2f * (-0.6f + (GLfloat)frand(0.45));
  f->ddx = 0;
  f->ddz = 0;

  if (do_spin || do_wander)
    f->spinner_p = 0;
  else
    f->spinner_p = !(random() % (3 * bp->nfloaters));

  if (! (random() % (30 * bp->nfloaters)))
    {
      f->dx = (GLfloat)frand(1.8) * (random() & 1 ? 1.0f : -1.0f);
      f->dz = (GLfloat)frand(1.8) * (random() & 1 ? 1.0f : -1.0f);
    }

  f->zr = (GLfloat)frand(180);
  if (do_spin || do_wander)
    {
      f->y = 0;
      if (bp->nfloaters > 2)
        f->y += (GLfloat)frand(3.0) * (random() & 1 ? 1.0f : -1.0f);
    }
}

static void
tick_floater (ModeInfo *mi, floater *f)
{
  companion_configuration *bp = &bps[MI_SCREEN(mi)];

  if (bp->button_down_p) return;

  if (do_spin || do_wander) return;

  f->dx += f->ddx;
  f->dy += f->ddy;
  f->dz += f->ddz;

  f->x += f->dx * speed * 0.2f;
  f->y += f->dy * speed * 0.2f;
  f->z += f->dz * speed * 0.2f;

  if (f->y < -28.0f ||
      f->x < -28.0f*8 || f->x > 28.0f*8 ||
      f->z < -28.0f*8 || f->z > 28.0f*8)
    reset_floater (mi, f);
}

/* Build a corner of the companion cube (the heart-shaped piece).
 *
 * Upstream companion.c does this with renderList inside glNewList, but
 * in the GLES3 port we play the uploaded gllist chain directly: build
 * a transform stack, push, translate, scale, rotate, draw the chain,
 * pop. The number of triangles drawn is reported via cube_polys. */
static int
build_corner (ModeInfo *mi)
{
  companion_configuration *bp = &bps[MI_SCREEN(mi)];

  ncz_mat_stack_push(g_model_stack_ptr);
  ncz_mat_stack_translate(g_model_stack_ptr, -0.5f, -0.5f, -0.5f);
  {
    GLfloat s = 0.659f;
    ncz_mat_stack_scale(g_model_stack_ptr, s, s, s);
  }
  ncz_mat_stack_rotate(g_model_stack_ptr, 180, 0, 1, 0);
  ncz_mat_stack_rotate(g_model_stack_ptr, 180, 0, 0, 1);
  ncz_mat_stack_translate(g_model_stack_ptr, -0.12f, -1.64f, 0.12f);
  /* In the GLES3 port, the cube_polys count comes from chain_quad.count/3
   * since each quad in the gllist chain has 4 vertices and the cube is
   * one quad per corner. */
  nczGLList_draw(&bp->chain_quad);
  ncz_mat_stack_pop(g_model_stack_ptr);
  return bp->chain_quad.count / 3;
}

/* Build one face of the companion cube — the rectangular face plus
 * the heart-on-top inset. This is the inline immediate-mode portion
 * of the FULL_CUBE display list; in GLES3 we just issue the ncz_im_*
 * calls directly when assembling the cube. */
static int
build_face (ModeInfo *mi)
{
  int polys = 0;
  int wire = MI_IS_WIREFRAME(mi);
  companion_configuration *bp = &bps[MI_SCREEN(mi)];

  GLfloat base_color[4]   = {0.53f, 0.60f, 0.66f, 1.0f};
  GLfloat heart_color[4]  = {0.92f, 0.67f, 1.0f, 1.0f};

  if (!wire)
    {
      GLfloat w = 0.010f;
      ncz_im_material(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, base_color);
      ncz_im_color3fv(base_color);
      ncz_mat_stack_push(g_model_stack_ptr);
      ncz_im_normal3f(0, 0, -1);
      ncz_mat_stack_translate(g_model_stack_ptr, -0.5f, -0.5f, -0.5f);

      ncz_im_begin(GL_QUADS);

      ncz_im_vertex3f(0,     0,     0);
      ncz_im_vertex3f(0,     0.5f-w, 0);
      ncz_im_vertex3f(0.5f-w, 0.5f-w, 0);
      ncz_im_vertex3f(0.5f-w, 0,     0);

      ncz_im_vertex3f(0.5f+w, 0,     0);
      ncz_im_vertex3f(0.5f+w, 0.5f-w, 0);
      ncz_im_vertex3f(1,     0.5f-w, 0);
      ncz_im_vertex3f(1,     0,     0);

      ncz_im_vertex3f(0,     0.5f+w, 0);
      ncz_im_vertex3f(0,     1,     0);
      ncz_im_vertex3f(0.5f-w, 1,     0);
      ncz_im_vertex3f(0.5f-w, 0.5f+w, 0);

      ncz_im_vertex3f(0.5f+w, 0.5f+w, 0);
      ncz_im_vertex3f(0.5f+w, 1,     0);
      ncz_im_vertex3f(1,     1,     0);
      ncz_im_vertex3f(1,     0.5f+w, 0);

      ncz_im_end();

      ncz_im_material(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, heart_color);

      ncz_im_normal3f(0, -1, 0);
      ncz_im_begin(GL_QUADS);
      ncz_im_vertex3f(0, 0.5f+w, 0);
      ncz_im_vertex3f(1, 0.5f+w, 0);
      ncz_im_vertex3f(1, 0.5f+w, w);
      ncz_im_vertex3f(0, 0.5f+w, w);
      ncz_im_end();

      ncz_im_normal3f(0, 1, 0);
      ncz_im_begin(GL_QUADS);
      ncz_im_vertex3f(0, 0.5f-w, w);
      ncz_im_vertex3f(1, 0.5f-w, w);
      ncz_im_vertex3f(1, 0.5f-w, 0);
      ncz_im_vertex3f(0, 0.5f-w, 0);
      ncz_im_end();

      ncz_im_normal3f(-1, 0, 0);
      ncz_im_begin(GL_QUADS);
      ncz_im_vertex3f(0.5f+w, 0, w);
      ncz_im_vertex3f(0.5f+w, 1, w);
      ncz_im_vertex3f(0.5f+w, 1, 0);
      ncz_im_vertex3f(0.5f+w, 0, 0);
      ncz_im_end();

      ncz_im_normal3f(1, 0, 0);
      ncz_im_begin(GL_QUADS);
      ncz_im_vertex3f(0.5f-w, 0, 0);
      ncz_im_vertex3f(0.5f-w, 1, 0);
      ncz_im_vertex3f(0.5f-w, 1, w);
      ncz_im_vertex3f(0.5f-w, 0, w);
      ncz_im_end();

      ncz_im_material(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, heart_color);

      ncz_im_normal3f(0, 0, -1);
      ncz_mat_stack_translate(g_model_stack_ptr, 0, 0, w);
      ncz_im_begin(GL_QUADS);
      ncz_im_vertex3f(0, 0, 0);
      ncz_im_vertex3f(0, 1, 0);
      ncz_im_vertex3f(1, 1, 0);
      ncz_im_vertex3f(1, 0, 0);
      ncz_im_end();

      ncz_mat_stack_pop(g_model_stack_ptr);
    }

  ncz_im_material(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, base_color);

  ncz_mat_stack_push(g_model_stack_ptr);
  polys += build_corner(mi);
  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 0, 0, 1);
  polys += build_corner(mi);
  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 0, 0, 1);
  polys += build_corner(mi);
  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 0, 0, 1);
  polys += build_corner(mi);

  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 0, 0, 1);
  ncz_mat_stack_translate(g_model_stack_ptr, 0.585f, -0.585f, -0.5655f);

  {
    GLfloat s = 10.5f;
    ncz_mat_stack_scale(g_model_stack_ptr, s, s, s);
  }
  ncz_mat_stack_rotate(g_model_stack_ptr, 180, 0, 1, 0);

  if (! wire)
    {
      ncz_im_material(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, base_color);
      nczGLList_draw(&bp->chain_heart);
      polys += bp->chain_heart.count / 3;
    }

  nczGLList_draw(&bp->chain_disc);
  polys += bp->chain_disc.count / 3;

  ncz_mat_stack_pop(g_model_stack_ptr);
  return polys;
}

/* build_cube: assemble all 6 faces of the cube. In the upstream
 * version this is compiled into a glNewList/glEndList block (the
 * "FULL_CUBE" display list) and replayed per floater per frame.
 * In the GLES3 port we don't need the display-list machinery for this
 * case — the cube is a small amount of geometry and we just emit the
 * draws directly each frame. (For the gllist VBO path to be the load-
 * bearing piece, the 3 companion_*_model gllists are the part that
 * matters; the inline quad face geometry is small.) */
static int
build_cube (ModeInfo *mi)
{
  int polys = 0;
  ncz_mat_stack_push(g_model_stack_ptr);
  polys += build_face(mi);
  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 0, 1, 0);
  polys += build_face(mi);
  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 0, 1, 0);
  polys += build_face(mi);
  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 0, 1, 0);
  polys += build_face(mi);
  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 1, 0, 0);
  polys += build_face(mi);
  ncz_mat_stack_rotate(g_model_stack_ptr, 180, 1, 0, 0);
  polys += build_face(mi);
  ncz_mat_stack_pop(g_model_stack_ptr);
  return polys;
}

ENTRYPOINT void
reshape_cube (ModeInfo *mi, int width, int height)
{
  GLfloat h = (GLfloat)height / (GLfloat)width;
  int y = 0;

  if (width > height * 5) {
    height = width * 9 / 16;
    y = -height/2;
    h = (GLfloat)height / (GLfloat)width;
  }

  ncz_im_viewport(0, y, width, height);

  ncz_mat_stack_load_identity(g_proj_stack_ptr);
  ncz_mat_stack_perspective(g_proj_stack_ptr, 30.0f, 1.0f / h, 1.0f, 100.0f);

  ncz_mat_stack_load_identity(g_model_stack_ptr);
  ncz_mat_stack_lookAt(g_model_stack_ptr, 0.0f, 0.0f, 30.0f,
                                       0.0f, 0.0f, 0.0f,
                                       0.0f, 1.0f, 0.0f);

  {
    GLfloat s = (MI_WIDTH(mi) < MI_HEIGHT(mi)
                 ? (MI_WIDTH(mi) / (GLfloat) MI_HEIGHT(mi))
                 : 1.0f);
    ncz_mat_stack_scale(g_model_stack_ptr, s, s, s);
  }

  ncz_im_clear(GL_COLOR_BUFFER_BIT);
}

ENTRYPOINT Bool
cube_handle_event (ModeInfo *mi, XEvent *event)
{
  companion_configuration *bp = &bps[MI_SCREEN(mi)];

  if (gltrackball_event_handler(event, bp->trackball,
                                 MI_WIDTH(mi), MI_HEIGHT(mi),
                                 &bp->button_down_p))
    return True;

  return False;
}

ENTRYPOINT void
init_cube (ModeInfo *mi)
{
  companion_configuration *bp;
  int i;

  MI_INIT(mi, bps);
  bp = &bps[MI_SCREEN(mi)];

  /* Background: opaque black (the upstream default). The gl4es-routed
   * demo clears to (0, 0, 0, 0) which becomes transparent and lets the
   * compositor's desktop show through — for a screensaver that wants
   * fullscreen coverage we want opaque. The Mali blob on O6N honours
   * alpha=1 the same way it honours alpha=0, so this is correct. */
  ncz_im_clear_color(0, 0, 0, 1);

  reshape_cube(mi, MI_WIDTH(mi), MI_HEIGHT(mi));

  ncz_im_shade_model(GL_SMOOTH);

  ncz_im_enable(GL_DEPTH_TEST);
  ncz_im_enable(GL_CULL_FACE);

  ncz_im_enable(GL_NORMALIZE);

  /* The 3 gllist chains uploaded ONCE at init. This is the single
   * most load-bearing piece of the foundation: gl4es was failing on
   * this exact path for companion (and the 4 other gllist-cluster
   * hacks) which is why companion is one of the 88 that "rendered
   * black" through gl4es. */
  if (nczGLList_upload(companion_quad, &bp->chain_quad) < 0) {
    fprintf(stderr, "companion: gllist upload quad failed\n");
    exit(1);
  }
  if (nczGLList_upload(companion_disc, &bp->chain_disc) < 0) {
    fprintf(stderr, "companion: gllist upload disc failed\n");
    exit(1);
  }
  if (nczGLList_upload(companion_heart, &bp->chain_heart) < 0) {
    fprintf(stderr, "companion: gllist upload heart failed\n");
    exit(1);
  }

  bp->trackball = gltrackball_init(False);

  /* Pre-count the cube polys: one build_cube call, but we don't run
   * it here because we want the matrices set up only at draw time.
   * bp->cube_polys is filled in lazily on the first draw_cube. */
  bp->cube_polys = 0;

  bp->nfloaters = MI_COUNT(mi);
  bp->floaters = (floater *)calloc(bp->nfloaters, sizeof(floater));

  for (i = 0; i < bp->nfloaters; i++)
    {
      floater *f = &bp->floaters[i];
      double spin_speed   = do_spin ? 0.7 : 10.0;
      double wander_speed = do_wander ? 0.02 : 0.05 * speed * 0.2;
      double spin_accel   = 0.5;
      f->rot = make_rotator(spin_speed, spin_speed, spin_speed,
                             spin_accel,
                             wander_speed,
                             True);
      if (bp->nfloaters == 2)
        {
          f->x = (i ? 2.0f : -2.0f);
        }
      else if (i != 0)
        {
          double th = (i - 1) * M_PI * 2 / (bp->nfloaters - 1);
          double r = 3.0;
          f->x = (GLfloat)(r * cos(th));
          f->z = (GLfloat)(r * sin(th));
        }

      f->ix = f->x;
      f->iy = f->y;
      f->iz = f->z;
      reset_floater(mi, f);
    }
}


static void
draw_floater (ModeInfo *mi, floater *f)
{
  companion_configuration *bp = &bps[MI_SCREEN(mi)];
  double x, y, z;

  get_position(f->rot, &x, &y, &z, !bp->button_down_p);

  ncz_mat_stack_push(g_model_stack_ptr);
  ncz_mat_stack_translate(g_model_stack_ptr, f->x, f->y, f->z);

  if (do_wander)
    ncz_mat_stack_translate(g_model_stack_ptr, (GLfloat)x, (GLfloat)y, (GLfloat)z);

  if (do_spin)
    get_rotation(f->rot, &x, &y, &z, !bp->button_down_p);

  if (do_spin || f->spinner_p)
    {
      ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)(x * 360), 1, 0, 0);
      ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)(y * 360), 0, 1, 0);
      ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)(z * 360), 0, 0, 1);
    }
  else
    {
      ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)(f->zr * 360), 0, 1, 0);
    }

  {
    GLfloat n = 1.5f;
    if      (bp->nfloaters > 99) n *= 0.05f;
    else if (bp->nfloaters > 25) n *= 0.18f;
    else if (bp->nfloaters >  9) n *= 0.3f;
    else if (bp->nfloaters >  1) n *= 0.7f;
    n *= 2.0f;
    if ((do_spin || do_wander) && bp->nfloaters > 1)
      n *= 0.7f;
    ncz_mat_stack_scale(g_model_stack_ptr, n, n, n);
  }

  if (bp->cube_polys == 0)
    bp->cube_polys = build_cube(mi);
  else
    build_cube(mi);

  mi->polygon_count += bp->cube_polys;

  ncz_mat_stack_pop(g_model_stack_ptr);
}


ENTRYPOINT void
draw_cube (ModeInfo *mi)
{
  companion_configuration *bp = &bps[MI_SCREEN(mi)];
  int i;

  ncz_im_clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!bp->button_down_p)
    {
      ncz_mat_stack_push(g_model_stack_ptr);
      ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)current_device_rotation(), 0, 0, 1);
      gltrackball_rotate(bp->trackball);

      ncz_mat_stack_scale(g_model_stack_ptr, 2, 2, 2);

      mi->polygon_count = 0;
      for (i = 0; i < bp->nfloaters; i++)
        {
          floater *f = &bp->floaters[i];
          draw_floater(mi, f);
          tick_floater(mi, f);
        }

      ncz_mat_stack_pop(g_model_stack_ptr);
    }

  if (mi->fps_p) do_fps(mi);
  glFinish();
}


ENTRYPOINT void
free_cube (ModeInfo *mi)
{
  companion_configuration *bp = &bps[MI_SCREEN(mi)];
  int i;
  if (!bp) return;
  for (i = 0; i < bp->nfloaters; i++)
    if (bp->floaters[i].rot) free_rotator(bp->floaters[i].rot);
  if (bp->floaters) free(bp->floaters);
  nczGLList_free(&bp->chain_quad);
  nczGLList_free(&bp->chain_disc);
  nczGLList_free(&bp->chain_heart);
  if (bp->trackball) gltrackball_free(bp->trackball);
}

XSCREENSAVER_MODULE_2("CompanionCubeGLES3", gles3companioncube, cube)
