/* _POSIX_C_SOURCE for M_PI; _DEFAULT_SOURCE for the legacy math.h bits. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/* DEFAULTS must be defined BEFORE xscreensaver_compat.h is included
 * (it has `#ifndef DEFAULTS`). Upstream boing.c declares DEFAULTS at
 * the top of the file, before xlockmore.h, so we follow that order. */
#define DEFAULTS	"*delay:	30000            \n" \
			"*showFPS:      False            \n" \
			"*wireframe:    False            \n" \

#define release_boing 0

/*
 * gles3_boing.c — Phase 1 pilot port of xscreensaver's boing.c to
 *                 native GLES3, routed through the gles3_compat
 *                 foundation. The vendored boing.c (kept untouched,
 *                 still building through gl4es) is the reference for
 *                 the algorithm. The function names match upstream:
 *                 init_boing, draw_boing, reshape_boing, free_boing,
 *                 boing_handle_event. The XSCREENSAVER_MODULE_2 macro
 *                 emits a struct xscreensaver_function_table named
 *                 `gles3_boing_xscreensaver_function_table`, which
 *                 gles3_harness.c drives via -DHACK_TABLE=.
 *
 * Why this is alongside the existing boing.c, not replacing it:
 *   - The 86 other _demo binaries still link against the gl4es path.
 *     Keeping the GL4ES-routed boing.c working until every hack is
 *     migrated is per the operator's directive (no dual-binary
 *     split, but the legacy path stays until the very last step).
 *   - This file IS a hand-port. The mechanical "drop-in stub" route
 *     (a Phase 4+ optimization per GLES3-MIGRATION-PHASE1.md) would
 *     let boing.c itself be ported with near-zero source edits, but
 *     Phase 1 is about proving the foundation end-to-end with two
 *     real ports.
 *
 * Differences from the upstream boing.c, by design:
 *   - No GLXContext / Display / Window. The GLES3 harness owns the
 *     context; the hack just calls the ncz_* draw helpers.
 *   - gluPerspective / gluLookAt replaced by ncz_mat_stack_perspective
 *     / ncz_mat_stack_lookAt, operating on the matrix stack.
 *   - glBegin/glVertex3f/glNormal3f/glColor3f/glMaterialfv/glLightfv
 *     replaced by ncz_im_* equivalents that accumulate into a CPU
 *     vertex buffer and issue ONE glDrawArrays per glEnd.
 *   - glPushMatrix/glPopMatrix/glTranslatef/glRotatef/glScalef/glOrtho
 *     replaced by ncz_mat_stack_* equivalents.
 *   - glClearColor/glClear/glViewport/glEnable/glDisable/glBlendFunc
 *     replaced by the matching ncz_im_* passthroughs (which call the
 *     real GLES3 gl*).
 *   - glLineWidth replaced by ncz_im_line_width.
 *   - glShadeModel replaced by ncz_im_shade_model (GL_SMOOTH/GL_FLAT).
 *
 * What we did NOT have to change:
 *   - parse_color — uses XParseColor + XColor (a shim that returns a
 *     packed 16-bit color from a name string). The vendored xscreensaver
 *     code path is identical.
 *   - The physics: tick_physics, BELLRAND, frand, etc.
 *   - The drawing routines' geometry: draw_grid, draw_box, draw_ball,
 *     draw_shadow, draw_scanlines — they all stay the same except
 *     for the GL1→GLES3 call renames.
 *
 * Algorithm note: the ball is a tessellated sphere. Each face is a
 * quad (4 vertices). The total vertex count per frame is small
 * (≈ meridians * parallels * 4) so the immediate-mode flush is
 * cheap.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "gles3_compat.h"
#include "xscreensaver_compat.h"

#define DEF_SPIN        "True"
#define DEF_LIGHTING    "False"
#define DEF_SMOOTH      "False"
#define DEF_SCANLINES   "True"
#define DEF_SPEED       "1.0"
#define DEF_BALL_SIZE   "0.5"
#define DEF_ANGLE       "15"
#define DEF_MERIDIANS   "16"
#define DEF_PARALLELS   "8"
#define DEF_TILES       "12"
#define DEF_THICKNESS   "0.05"

#define DEF_BALL_COLOR1  "#CC1919"
#define DEF_BALL_COLOR2  "#F2F2F2"
#define DEF_GRID_COLOR   "#991999"
#define DEF_SHADOW_COLOR "#303030"
#define DEF_BACKGROUND   "#8C8C8C"

typedef struct { GLfloat x, y, z; } XYZ;

typedef struct {
    trackball_state *trackball;
    Bool button_down_p;

    GLfloat speed;

    double ball_x,   ball_y,   ball_z,   ball_th;
    double ball_dx,  ball_dy,  ball_dz,  ball_dth;
    double ball_ddx, ball_ddy, ball_ddz;

    GLfloat ball_color1[4], ball_color2[4], grid_color[4];
    GLfloat bg_color[4], shadow_color[4];
    GLfloat lightpos[4];

} boing_configuration;

static boing_configuration *bps = NULL;

static Bool spin;
static Bool lighting_p;
static Bool smooth_p;
static Bool scanlines_p;
static GLfloat speed;
static int angle;
static GLfloat ball_size;
static unsigned int meridians;
static unsigned int parallels;
static unsigned int tiles;
static GLfloat thickness;
static char *ball_color1_str, *ball_color2_str, *grid_color_str,
  *shadow_str, *bg_str;

static XrmOptionDescRec opts[] = {
  { "-spin",       ".spin",      XrmoptionNoArg,  "True"  },
  { "+spin",       ".spin",      XrmoptionNoArg,  "False" },
  { "-lighting",   ".lighting",  XrmoptionNoArg,  "True"  },
  { "+lighting",   ".lighting",  XrmoptionNoArg,  "False" },
  { "-smooth",     ".smooth",    XrmoptionNoArg,  "True"  },
  { "+smooth",     ".smooth",    XrmoptionNoArg,  "False" },
  { "-scanlines",  ".scanlines", XrmoptionNoArg,  "True"  },
  { "+scanlines",  ".scanlines", XrmoptionNoArg,  "False" },
  { "-speed",      ".speed",     XrmoptionSepArg, 0 },
  { "-angle",      ".angle",     XrmoptionSepArg, 0 },
  { "-size",       ".ballSize",  XrmoptionSepArg, 0 },
  { "-meridians",  ".meridians", XrmoptionSepArg, 0 },
  { "-parallels",  ".parallels", XrmoptionSepArg, 0 },
  { "-tiles",      ".tiles",     XrmoptionSepArg, 0 },
  { "-thickness",  ".thickness", XrmoptionSepArg, 0 },
  { "-ball-color1",".ballColor1",XrmoptionSepArg, 0 },
  { "-ball-color2",".ballColor2",XrmoptionSepArg, 0 },
  { "-grid-color", ".gridColor", XrmoptionSepArg, 0 },
  { "-shadow-color",".shadowColor",XrmoptionSepArg, 0 },
  { "-background",  ".boingBackground",XrmoptionSepArg, 0 },
  { "-bg",          ".boingBackground",XrmoptionSepArg, 0 },
};

static argtype vars[] = {
  {&spin,      "spin",      "Spin",       DEF_SPIN,      t_Bool},
  {&lighting_p,"lighting",  "Lighting",   DEF_LIGHTING,  t_Bool},
  {&smooth_p,  "smooth",    "Smooth",     DEF_SMOOTH,    t_Bool},
  {&scanlines_p,"scanlines","Scanlines",  DEF_SCANLINES, t_Bool},
  {&speed,     "speed",     "Speed",      DEF_SPEED,     t_Float},
  {&angle,     "angle",     "Angle",      DEF_ANGLE,     t_Int},
  {&ball_size, "ballSize",  "BallSize",   DEF_BALL_SIZE, t_Float},
  {&meridians, "meridians", "meridians",  DEF_MERIDIANS, t_Int},
  {&parallels, "parallels", "parallels",  DEF_PARALLELS, t_Int},
  {&tiles,     "tiles",     "Tiles",      DEF_TILES,     t_Int},
  {&thickness, "thickness", "Thickness",  DEF_THICKNESS, t_Float},
  {&ball_color1_str, "ballColor1", "BallColor1", DEF_BALL_COLOR1, t_String},
  {&ball_color2_str, "ballColor2", "BallColor2", DEF_BALL_COLOR2, t_String},
  {&grid_color_str,  "gridColor",  "GridColor",  DEF_GRID_COLOR,  t_String},
  {&shadow_str,      "shadowColor","ShadowColor",DEF_SHADOW_COLOR,t_String},
  {&bg_str,        "boingBackground", "Background", DEF_BACKGROUND, t_String},
};

ENTRYPOINT ModeSpecOpt boing_opts = {countof(opts), opts, countof(vars), vars, NULL};

static void
parse_color (ModeInfo *mi, const char *name, const char *s, GLfloat *a)
{
  XColor c;
  a[3] = 1.0;  /* alpha */

  if (! XParseColor (MI_DISPLAY(mi), MI_COLORMAP(mi), s, &c))
    {
      fprintf (stderr, "%s: can't parse %s color %s", progname, name, s);
      exit (1);
    }
  a[0] = c.red   / 65536.0;
  a[1] = c.green / 65536.0;
  a[2] = c.blue  / 65536.0;
}

static void
draw_grid (ModeInfo *mi)
{
  boing_configuration *bp = &bps[MI_SCREEN(mi)];
  int x, y;
  GLfloat t2  = (GLfloat) tiles / 2;
  GLfloat s = 1.0f / (tiles + thickness);
  GLfloat z = 0;

  GLfloat lw = (GLfloat)MI_HEIGHT(mi) * 0.06f * thickness;

  /* Material = the grid color (so the per-vertex color isn't used). */
  ncz_im_material(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, bp->grid_color);
  ncz_im_color3fv(bp->grid_color);  /* match the material for non-lit path */

  ncz_mat_stack_push(g_model_stack_ptr);
  ncz_mat_stack_scale(g_model_stack_ptr, s, s, s);
  ncz_mat_stack_translate(g_model_stack_ptr, -t2, -t2, 0);

  ncz_im_line_width(lw);
  ncz_im_begin(GL_LINES);
  for (y = 0; y <= (int)tiles; y++)
    {
      ncz_im_vertex3f(0,     (GLfloat)y, z);
      ncz_im_vertex3f((GLfloat)tiles, (GLfloat)y, z);
    }
  for (x = 0; x <= (int)tiles; x++)
    {
      ncz_im_vertex3f((GLfloat)x, (GLfloat)tiles, z);
      ncz_im_vertex3f((GLfloat)x, 0,             z);
    }
  ncz_im_end();
  ncz_mat_stack_pop(g_model_stack_ptr);
}

static void
draw_box (ModeInfo *mi)
{
  ncz_mat_stack_push(g_model_stack_ptr);
  ncz_mat_stack_translate(g_model_stack_ptr, 0, 0, -0.5f);
  draw_grid (mi);
  ncz_mat_stack_pop(g_model_stack_ptr);

  ncz_mat_stack_push(g_model_stack_ptr);
  ncz_mat_stack_rotate(g_model_stack_ptr, 90, 1, 0, 0);
  ncz_mat_stack_translate(g_model_stack_ptr, 0, 0, 0.5f);
  draw_grid (mi);
  ncz_mat_stack_pop(g_model_stack_ptr);
}

static void
draw_ball (ModeInfo *mi)
{
  boing_configuration *bp = &bps[MI_SCREEN(mi)];
  int wire = MI_IS_WIREFRAME(mi);
  int x, y;
  int xx = (int)meridians;
  int yy = (int)parallels;
  int scale = (smooth_p ? 5 : 1);

  if (lighting_p && !wire) ncz_im_enable(GL_LIGHTING);

  if (parallels < 3) scale *= 2;
  xx *= scale;
  yy *= scale;

  ncz_im_front_face(GL_CW);

  ncz_mat_stack_push(g_model_stack_ptr);
  ncz_mat_stack_translate(g_model_stack_ptr, (GLfloat)bp->ball_x, (GLfloat)bp->ball_y, (GLfloat)bp->ball_z);
  ncz_mat_stack_scale(g_model_stack_ptr, ball_size, ball_size, ball_size);
  ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)-angle, 0, 0, 1);
  ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)bp->ball_th, 0, 1, 0);

  for (y = 0; y < yy; y++)
    {
      GLfloat thy0 = (GLfloat)y     * ((GLfloat)M_PI * 2.0f) / (yy * 2) + (GLfloat)M_PI_2;
      GLfloat thy1 = (GLfloat)(y+1) * ((GLfloat)M_PI * 2.0f) / (yy * 2) + (GLfloat)M_PI_2;

      for (x = 0; x < xx; x++)
        {
          GLfloat thx0 = (GLfloat)x     * ((GLfloat)M_PI * 2.0f) / xx;
          GLfloat thx1 = (GLfloat)(x+1) * ((GLfloat)M_PI * 2.0f) / xx;
          XYZ p;
          Bool bgp = (((x/scale) & 1) ^ ((y/scale) & 1)) ? True : False;
          GLfloat *mat_color = bgp ? bp->ball_color2 : bp->ball_color1;

          if (wire && bgp) continue;

          ncz_im_material(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, mat_color);
          ncz_im_color3fv(mat_color);

          ncz_im_begin(wire ? GL_LINE_LOOP : GL_QUADS);

          if (!smooth_p)
            {
              p.x = cosf((thy0+thy1)/2) * cosf((thx0+thx1)/2);
              p.y = sinf((thy0+thy1)/2);
              p.z = cosf((thy0+thy1)/2) * sinf((thx0+thx1)/2);
              ncz_im_normal3f(-p.x, -p.y, -p.z);
            }

          p.x = cosf(thy0) * cosf(thx0) / 2.0f;
          p.y = sinf(thy0)             / 2.0f;
          p.z = cosf(thy0) * sinf(thx0) / 2.0f;
          if (smooth_p) ncz_im_normal3f(-p.x, -p.y, -p.z);
          ncz_im_vertex3f(p.x, p.y, p.z);

          p.x = cosf(thy1) * cosf(thx0) / 2.0f;
          p.y = sinf(thy1)             / 2.0f;
          p.z = cosf(thy1) * sinf(thx0) / 2.0f;
          if (smooth_p) ncz_im_normal3f(-p.x, -p.y, -p.z);
          ncz_im_vertex3f(p.x, p.y, p.z);

          p.x = cosf(thy1) * cosf(thx1) / 2.0f;
          p.y = sinf(thy1)             / 2.0f;
          p.z = cosf(thy1) * sinf(thx1) / 2.0f;
          if (smooth_p) ncz_im_normal3f(-p.x, -p.y, -p.z);
          ncz_im_vertex3f(p.x, p.y, p.z);

          p.x = cosf(thy0) * cosf(thx1) / 2.0f;
          p.y = sinf(thy0)             / 2.0f;
          p.z = cosf(thy0) * sinf(thx1) / 2.0f;
          if (smooth_p) ncz_im_normal3f(-p.x, -p.y, -p.z);
          ncz_im_vertex3f(p.x, p.y, p.z);

          ncz_im_end ();
          mi->polygon_count++;
        }
    }

  ncz_mat_stack_pop(g_model_stack_ptr);

  if (lighting_p && !wire) ncz_im_disable(GL_LIGHTING);
}

static void
draw_shadow (ModeInfo *mi)
{
  boing_configuration *bp = &bps[MI_SCREEN(mi)];
  int wire = MI_IS_WIREFRAME(mi);
  GLfloat xoff = 0.14f;
  GLfloat yoff = 0.07f;
  int y;
  int yy = (int)parallels;
  int scale = (smooth_p ? 5 : 1);

  if (lighting_p && !wire) ncz_im_enable(GL_BLEND);

  if (parallels < 3) scale *= 2;
  yy *= scale;

  ncz_mat_stack_push(g_model_stack_ptr);
  ncz_mat_stack_translate(g_model_stack_ptr, (GLfloat)(bp->ball_x + xoff), (GLfloat)(bp->ball_y + yoff), -0.49f);
  ncz_mat_stack_scale(g_model_stack_ptr, ball_size, ball_size, ball_size);
  ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)-angle, 0, 0, 1);

  ncz_im_material(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, bp->shadow_color);
  ncz_im_color4fv(bp->shadow_color);

  ncz_im_normal3f(0, 0, 1);
  ncz_im_begin(wire ? GL_LINE_LOOP : GL_TRIANGLE_FAN);
  if (!wire) ncz_im_vertex3f(0, 0, 0);

  for (y = 0; y < yy*2+1; y++)
    {
      GLfloat thy0 = (GLfloat)y * ((GLfloat)M_PI * 2.0f) / (yy * 2) + (GLfloat)M_PI_2;
      ncz_im_vertex3f(cosf(thy0) / 2.0f, sinf(thy0) / 2.0f, 0);
      mi->polygon_count++;
    }

  ncz_im_end ();

  ncz_mat_stack_pop(g_model_stack_ptr);

  if (lighting_p && !wire) ncz_im_disable(GL_BLEND);
}

static void
draw_scanlines (ModeInfo *mi)
{
  int wire = MI_IS_WIREFRAME(mi);
  int w = MI_WIDTH(mi);
  int h = MI_HEIGHT(mi);

  if (h <= 300) return;

  if (!wire)
    {
      ncz_im_enable(GL_BLEND);
      ncz_im_disable(GL_DEPTH_TEST);
    }

  /* For the 2D scanline overlay, the vendored boing.c calls glOrtho
   * to map (0, w, 0, h) directly to NDC. We do the same with our
   * helper: push proj, install ortho, identity model, draw; pop both.
   * The earlier attempt at using lookAt was broken — the lookAt matrix
   * has no aspect scaling so x in object space mapped directly to NDC
   * x with no viewport alignment, putting the right end of every
   * scanline off-screen. */
  ncz_mat_stack_push(g_proj_stack_ptr);
  ncz_mat_stack_ortho(g_proj_stack_ptr, 0, (GLfloat)w, 0, (GLfloat)h, -1, 1);

  ncz_mat_stack_push(g_model_stack_ptr);
  ncz_mat4_identity(g_model_stack_ptr->m[g_model_stack_ptr->top]);

  int lh, ls;
  int y;
  if      (h > 500) { lh = 4; ls = 4; }
  else if (h > 300) { lh = 2; ls = 1; }
  else              { lh = 1; ls = 1; }

  if (lh == 1) ncz_im_disable(GL_BLEND);

  ncz_im_line_width((GLfloat)lh);
  ncz_im_color4f(0, 0, 0, 0.3f);

  ncz_im_begin(GL_LINES);
  for (y = 0; y < h; y += lh + ls)
    {
      ncz_im_vertex3f(0, (GLfloat)y, 0);
      ncz_im_vertex3f((GLfloat)w, (GLfloat)y, 0);
    }
  ncz_im_end();

  ncz_mat_stack_pop(g_model_stack_ptr);
  ncz_mat_stack_pop(g_proj_stack_ptr);

  if (!wire)
    {
      ncz_im_disable(GL_BLEND);
      ncz_im_enable(GL_DEPTH_TEST);
    }
}

static void
tick_physics (ModeInfo *mi)
{
  boing_configuration *bp = &bps[MI_SCREEN(mi)];
  GLfloat s2 = ball_size / 2.0f;
  GLfloat max = 0.5f - s2;
  GLfloat min = -max;

  bp->ball_th += bp->ball_dth;
  while (bp->ball_th > 360) bp->ball_th -= 360;
  while (bp->ball_th < 0)   bp->ball_th += 360;

  bp->ball_dx += bp->ball_ddx;
  bp->ball_x  += bp->ball_dx;
  if      (bp->ball_x < min) {
      bp->ball_x = min; bp->ball_dx = -bp->ball_dx;
      bp->ball_dth = -bp->ball_dth;
      bp->ball_dx += (frand(bp->speed/2) - bp->speed);
  } else if (bp->ball_x > max) {
      bp->ball_x = max; bp->ball_dx = -bp->ball_dx;
      bp->ball_dth = -bp->ball_dth;
      bp->ball_dx += (frand(bp->speed/2) - bp->speed);
  }

  bp->ball_dy += bp->ball_ddy;
  bp->ball_y  += bp->ball_dy;
  if      (bp->ball_y < min) { bp->ball_y = min; bp->ball_dy = -bp->ball_dy; }
  else if (bp->ball_y > max) { bp->ball_y = max; bp->ball_dy = -bp->ball_dy; }

  bp->ball_dz += bp->ball_ddz;
  bp->ball_z  += bp->ball_dz;
  if      (bp->ball_z < min) { bp->ball_z = min; bp->ball_dz = -bp->ball_dz; }
  else if (bp->ball_z > max) { bp->ball_z = max; bp->ball_dz = -bp->ball_dz; }
}

/* Window management, etc. */
ENTRYPOINT void
reshape_boing (ModeInfo *mi, int width, int height)
{
  GLfloat h = (GLfloat) height / (GLfloat) width;
  int y = 0;

  h *= 4.0f / 3.0f;   /* Back in the caveman days we couldn't even afford
                         square pixels! */

  if (width > height * 5) {   /* tiny window: show middle */
    height = width * 3/4;
    y = -height/2;
    h = (GLfloat)height / (GLfloat)width;
  }

  ncz_im_viewport(0, y, width, height);

  /* Install projection. */
  ncz_mat_stack_load_identity(g_proj_stack_ptr);
  if (height > width)
    {
      GLfloat sc = (GLfloat)width / (GLfloat)height;
      ncz_mat_stack_scale(g_proj_stack_ptr, sc, sc, sc);
    }
  ncz_mat_stack_perspective(g_proj_stack_ptr, 8.0f, 1.0f / h, 1.0f, 10.0f);

  /* Install modelview. */
  ncz_mat_stack_load_identity(g_model_stack_ptr);
  ncz_mat_stack_lookAt(g_model_stack_ptr, 0, 0, 8, 0, 0, 0, 0, 1, 0);

  ncz_im_clear(GL_COLOR_BUFFER_BIT);
}

ENTRYPOINT Bool
boing_handle_event (ModeInfo *mi, XEvent *event)
{
  boing_configuration *bp = &bps[MI_SCREEN(mi)];

  if (gltrackball_event_handler (event, bp->trackball,
                                 MI_WIDTH (mi), MI_HEIGHT (mi),
                                 &bp->button_down_p))
    return True;

  return False;
}

ENTRYPOINT void
init_boing (ModeInfo *mi)
{
  boing_configuration *bp;
  int wire = MI_IS_WIREFRAME(mi);

  MI_INIT (mi, bps);
  bp = &bps[MI_SCREEN(mi)];

  if (tiles < 1) tiles = 1;

  if (smooth_p)
    {
      if (meridians < 1) meridians = 1;
      if (parallels < 1) parallels = 1;
    }
  else
    {
      if (meridians < 3) meridians = 3;
      if (parallels < 2) parallels = 2;
    }

  if (meridians > 1 && meridians & 1) meridians++;  /* odd numbers look bad */


  if (thickness <= 0) thickness = 0.001f;
  else if (thickness > 1) thickness = 1;

  reshape_boing (mi, MI_WIDTH(mi), MI_HEIGHT(mi));

  parse_color (mi, "ballColor1",  ball_color1_str,  bp->ball_color1);
  parse_color (mi, "ballColor2",  ball_color2_str,  bp->ball_color2);
  parse_color (mi, "gridColor",   grid_color_str,   bp->grid_color);
  parse_color (mi, "shadowColor", shadow_str,       bp->shadow_color);
  parse_color (mi, "background",  bg_str,           bp->bg_color);

  bp->shadow_color[3] = 0.9f;

  ncz_im_clear_color(bp->bg_color[0], bp->bg_color[1], bp->bg_color[2], 1.0f);

  if (!wire)
    {
      ncz_im_enable(GL_DEPTH_TEST);
      ncz_im_enable(GL_CULL_FACE);
    }

  bp->lightpos[0] = 0.5f;
  bp->lightpos[1] = 0.5f;
  bp->lightpos[2] = -1.0f;
  bp->lightpos[3] = 0.0f;

  if (lighting_p && !wire)
    {
      GLfloat amb[4] = {0, 0, 0, 1};
      GLfloat dif[4] = {1, 1, 1, 1};
      GLfloat spc[4] = {1, 1, 1, 1};
      ncz_im_enable(GL_LIGHT0);
      ncz_im_light_ambient(0, amb);
      ncz_im_light_diffuse(0, dif);
      ncz_im_light_specular(0, spc);
    }

  ncz_im_blend_func(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  bp->speed = speed / 800.0f;

  bp->ball_dth = (spin ? -bp->speed * 7 * 360 : 0);

  bp->ball_x   = 0.5f - ((ball_size/2) + frand(1-ball_size));
  bp->ball_y   = 0.2f;
  bp->ball_dx  = bp->speed * 6 + frand(bp->speed);
  bp->ball_ddy = -bp->speed;

  bp->ball_dz  = bp->speed * 6 + frand(bp->speed);

  bp->trackball = gltrackball_init (False);
}


ENTRYPOINT void
draw_boing (ModeInfo *mi)
{
  boing_configuration *bp = &bps[MI_SCREEN(mi)];

  mi->polygon_count = 0;

  ncz_im_shade_model(GL_SMOOTH);

  ncz_im_enable(GL_NORMALIZE);

  ncz_im_clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (! bp->button_down_p)
    tick_physics (mi);

  if (! bp->button_down_p)
    tick_physics (mi);

  ncz_mat_stack_push(g_model_stack_ptr);

  {
    double rot = current_device_rotation();
    ncz_mat_stack_rotate(g_model_stack_ptr, (GLfloat)rot, 0, 0, 1);
  }

  gltrackball_rotate (bp->trackball);

  ncz_im_light_position(0, bp->lightpos);

  ncz_im_disable(GL_CULL_FACE);
  ncz_im_disable(GL_DEPTH_TEST);

  ncz_im_enable(GL_LINE_SMOOTH);
  /* glHint(GL_LINE_SMOOTH_HINT, GL_NICEST) — no equivalent in GLES3
   * (line smoothing is generally unsupported on tile-based GPUs).
   * The vendor blob on O6N accepts the call as a no-op so we skip
   * it here. */

  ncz_im_blend_func(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  ncz_im_enable(GL_BLEND);

  draw_box (mi);
  draw_shadow (mi);

  ncz_im_enable(GL_CULL_FACE);
  ncz_im_enable(GL_DEPTH_TEST);

  draw_ball (mi);
  if (scanlines_p)
    draw_scanlines (mi);

  ncz_mat_stack_pop(g_model_stack_ptr);

  ncz_im_color3f(1, 1, 1);
  if (mi->fps_p) do_fps (mi);
  glFinish();
}


ENTRYPOINT void
free_boing (ModeInfo *mi)
{
  boing_configuration *bp = &bps[MI_SCREEN(mi)];
  if (bp->trackball) gltrackball_free (bp->trackball);
}

XSCREENSAVER_MODULE_2 ("BoingGLES3", gles3boing, boing)
