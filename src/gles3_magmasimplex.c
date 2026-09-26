/* gles3_magmasimplex.c — GLES3-native fullscreen raymarched translucent
 *                        wax blobs in a coloured transmissive liquid.
 *
 * Replaces src/gles3_lavafield.c (round 17 prototype, 5% non-black
 * monochrome render). Same line of work, but:
 *
 *   - The "field" is a real coloured transmissive liquid that fills
 *     the entire frame, not blobs on black.
 *   - The blobs are translucent wax viewed THROUGH the liquid. The
 *     perceived hue is a product of the wax colour and the liquid hue
 *     (Beer-Lambert-style attenuation through the liquid column above
 *     the wax), not the wax composited over a background. Yellow wax
 *     in blue liquid reads green ON SCREEN as an emergent result of
 *     the optics, not by hardcoding green.
 *   - Different blobs carry different hues simultaneously — a single
 *     frame is multicoloured, not monochrome. Two-hue mode blends
 *     each blob's hue from a dominant wax hue to a secondary hue by
 *     a per-blob id; rainbow mode gives each blob its own spread-out
 *     hue on the colour wheel.
 *   - Lighting is a hot emitter low in frame with a vertical
 *     luminance falloff (NOT uniform backlight). Wax pooled near the
 *     base is the brightest thing on screen.
 *   - ~30 historical liquid/wax pairings are randomised per-launch.
 *     Internal colourway names: ember, orchid, ultraviolet, sunflower,
 *     opal. No manufacturer / brand / product references in code,
 *     comments, docs or commit messages; pairings are historical
 *     colour relationships only.
 *
 * Per-launch randomisation (seeded from /dev/urandom, overridable via
 * NCZ_MAGMASIMPLEX_FIXED_SEED) covers: way_idx (0..29 colourway
 * pick), blob_count (5..8), viscosity (drag), scale (zoom in/out),
 * hotness (emitter strength), palette_drift (per-blob hue rotation
 * speed), clear_liquid (no-attenuation mode for the classic
 * backlit-only colourways), rainbow (every blob a unique hue).
 *
 * Printed to stderr in the established [diag] format. The first
 * frame also samples eight points along a horizontal slice and prints
 * their RGB to prove multicolour-on-screen-not-just-monochrome.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <GLES3/gl32.h>
#include "gles3_compat.h"
#include "xscreensaver_compat.h"
/* gcc 14+ makes implicit declarations an error under -std=c11 even
 * though gles3_compat.h declares ncz_harness_die. Forward decl keeps
 * it explicit if header order ever changes. */
#ifdef NCZ_GLES3_BUILD
extern void ncz_harness_die(int code);
#endif

#define MAX_BLOBS 8

typedef struct {
  GLuint program, vbo;
  GLint u_time, u_seed, u_resolution;
  GLint u_way_idx;
  GLint u_liquid_r, u_liquid_g, u_liquid_b;
  GLint u_wax_r, u_wax_g, u_wax_b;
  GLint u_secondary_r, u_secondary_g, u_secondary_b;
  GLint u_blob_count, u_viscosity, u_scale, u_hotness;
  GLint u_palette_drift, u_clear_liquid, u_rainbow;
  GLint u_blob0, u_blob1, u_blob2, u_blob3, u_blob4, u_blob5, u_blob6, u_blob7;
  GLint u_bhue0, u_bhue1, u_bhue2, u_bhue3, u_bhue4, u_bhue5, u_bhue6, u_bhue7;
  double started;
  /* Per-launch constants and per-blob layout. The shader sees:
   *   bx[i], by[i], bz[i], br[i] — anchor position+radius
   *   bhue[i]                       — per-blob hue id (0..1)
   * Draw code applies a slow buoyancy wave on top of these. */
  int    blob_count;
  float  liquid_r, liquid_g, liquid_b;
  float  wax_r, wax_g, wax_b;
  float  secondary_r, secondary_g, secondary_b;
  float  viscosity, scale, hotness, palette_drift;
  int    clear_liquid, rainbow;
  uint32_t seed;
  int    way_idx;
  const char *way_name;
  float  bx[MAX_BLOBS], by[MAX_BLOBS], bz[MAX_BLOBS], br[MAX_BLOBS];
  float  bhue[MAX_BLOBS];
} State;

/* Internal colourway names. These are OUR names for the visual
 * category; the actual RGB pairings are historical colour
 * relationships only and reference no manufacturer, brand or product
 * line anywhere. */
static const char *colourway_names[] = {
  "ember", "orchid", "ultraviolet", "sunflower", "opal",
  "ember", "orchid", "ultraviolet", "sunflower", "opal",
  "ember", "orchid", "ultraviolet", "sunflower", "opal",
  "ember", "orchid", "ultraviolet", "sunflower", "opal",
  "ember", "orchid", "ultraviolet", "sunflower", "opal",
  "ember", "orchid", "ultraviolet", "sunflower", "opal",
};
#define COLOURWAY_COUNT 30

/* ~30 historical liquid/wax pairings. Each row is
 *   liquid_rgb (clear liquid has r=g=b=1.0),
 *   wax_rgb (the dominant wax hue),
 *   secondary_rgb (the alternate hue per-blob colours lerp toward;
 *     picked as a near-complement of wax_rgb for contrast),
 *   clear (1 = clear liquid mode, 0 = coloured transmissive mode).
 *
 * Picked from general historical pairings of the form. The pairings
 * themselves are not protectable as colour combinations; the names we
 * give them (above) are entirely our own. */
typedef struct { float lr, lg, lb; float wr, wg, wb; float sr, sg, sb; int clear; } Way;
static const Way ways[COLOURWAY_COUNT] = {
  /* clear-liquid classics: wax colour dominates, backlit */
  { 1, 1, 1, 0.95, 0.10, 0.10, 1.00, 0.55, 0.20, 1 }, /* red wax, clear */
  { 1, 1, 1, 1.00, 0.55, 0.40, 0.95, 0.25, 0.20, 1 }, /* peach, clear */
  { 1, 1, 1, 1.00, 0.85, 0.20, 0.85, 0.10, 0.55, 1 }, /* yellow, clear */
  { 1, 1, 1, 0.65, 0.20, 0.95, 0.20, 0.55, 0.95, 1 }, /* purple, clear */
  { 1, 1, 1, 0.30, 0.95, 0.40, 0.20, 0.55, 1.00, 1 }, /* green, clear */
  { 1, 1, 1, 1.00, 0.45, 0.75, 0.95, 0.15, 0.55, 1 }, /* pink, clear */

  /* coloured-liquid pairings: wax seen through the fluid */
  { 0.10, 0.25, 0.95, 0.95, 0.10, 0.10, 1.00, 0.85, 0.20, 0 }, /* blue / red */
  { 0.10, 0.25, 0.95, 1.00, 1.00, 0.20, 0.95, 0.10, 0.10, 0 }, /* blue / yellow -> reads green */
  { 0.10, 0.25, 0.95, 0.95, 0.95, 0.95, 0.65, 0.20, 0.95, 0 }, /* blue / white */
  { 0.10, 0.25, 0.95, 0.30, 0.95, 0.40, 0.65, 0.20, 0.95, 0 }, /* blue / green */
  { 0.10, 0.25, 0.95, 0.65, 0.20, 0.95, 0.95, 0.10, 0.10, 0 }, /* blue / purple */
  { 0.95, 0.20, 0.55, 0.95, 0.95, 0.95, 0.95, 0.10, 0.10, 0 }, /* pink / white */

  { 0.95, 0.10, 0.10, 0.95, 0.95, 0.95, 1.00, 0.85, 0.20, 0 }, /* red / white */
  { 0.95, 0.10, 0.10, 1.00, 0.85, 0.20, 0.65, 0.20, 0.95, 0 }, /* red / yellow */
  { 0.95, 0.10, 0.10, 0.95, 0.55, 0.40, 0.30, 0.95, 0.40, 0 }, /* red / green (loud) */

  { 0.65, 0.20, 0.95, 0.95, 0.95, 0.95, 0.95, 0.10, 0.10, 0 }, /* purple / white */
  { 0.65, 0.20, 0.95, 0.95, 0.10, 0.10, 1.00, 0.85, 0.20, 0 }, /* purple / red */
  { 0.65, 0.20, 0.95, 1.00, 0.85, 0.20, 0.95, 0.10, 0.10, 0 }, /* purple / yellow */

  { 1.00, 0.55, 0.10, 0.95, 0.95, 0.95, 0.95, 0.10, 0.10, 0 }, /* orange / white */
  { 1.00, 0.55, 0.10, 0.65, 0.20, 0.95, 0.30, 0.95, 0.40, 0 }, /* orange / purple */
  { 1.00, 0.55, 0.10, 0.10, 0.10, 0.10, 0.95, 0.10, 0.10, 0 }, /* orange / black silhouette */

  { 0.30, 0.95, 0.40, 0.95, 0.95, 0.95, 0.95, 0.10, 0.10, 0 }, /* green / white */
  { 0.30, 0.95, 0.40, 0.10, 0.25, 0.95, 1.00, 0.85, 0.20, 0 }, /* green / blue */
  { 0.30, 0.95, 0.40, 1.00, 0.85, 0.20, 0.95, 0.10, 0.10, 0 }, /* green / yellow */

  /* the loud ones — every blob a different hue */
  { 0.10, 0.10, 0.20, 0.95, 0.95, 0.95, 0.95, 0.10, 0.10, 0 }, /* dark blue / white */
  { 0.20, 0.05, 0.30, 0.95, 0.55, 0.95, 1.00, 0.85, 0.20, 0 }, /* indigo / pink */
  { 0.10, 0.30, 0.20, 0.95, 0.95, 0.95, 1.00, 0.85, 0.20, 0 }, /* forest / cream */
  { 0.20, 0.10, 0.05, 1.00, 0.85, 0.20, 0.95, 0.10, 0.10, 0 }, /* brown / amber */
  { 0.05, 0.10, 0.30, 0.95, 0.85, 0.30, 0.65, 0.20, 0.95, 0 }, /* deep navy / gold */
  { 0.30, 0.05, 0.10, 0.95, 0.85, 0.55, 0.30, 0.95, 0.40, 0 }, /* oxblood / mint */
};

static double now(void){
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static uint32_t seed_rng(void){
  const char *override = getenv("NCZ_MAGMASIMPLEX_FIXED_SEED");
  if(override && *override){
    uint32_t s = (uint32_t)strtoul(override, NULL, 10);
    if(s != 0) return s;
  }
  uint32_t s = 0;
  int f = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if(f >= 0){
    ssize_t n = read(f, &s, 4); close(f);
    if(n == 4) return s;
  }
  struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
  return (uint32_t)(t.tv_nsec ^ t.tv_sec ^ getpid());
}

/* Park-Miller-ish LCG; cheap, deterministic, no external state. */
static float rnd(uint32_t *s, float a, float b){
  *s ^= *s << 13;
  *s ^= *s >> 17;
  *s ^= *s << 5;
  return a + (b - a) * (float)(*s & 0x00ffffffu) / 16777215.0f;
}
static int rndi(uint32_t *s, int a, int b){
  return a + (int)(rnd(s, 0.0f, 1.0f) * (float)(b - a + 1));
}

static char *load_shader(void){
  const char *p[] = {
    "vendor/magmasimplex/magmasimplex.frag",
    "../vendor/magmasimplex/magmasimplex.frag",
    "../../vendor/magmasimplex/magmasimplex.frag",
    "/usr/share/ncz-screensavers/shaders/magmasimplex.frag"
  };
  FILE *f = NULL; const char *u = NULL;
  for(unsigned i = 0; i < 4; i++)
    if((f = fopen(p[i], "rb"))){ u = p[i]; break; }
  if(!f){
    fprintf(stderr, "magmasimplex: cannot locate magmasimplex.frag\n");
    return NULL;
  }
  fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  char *b = malloc((size_t)n + 1);
  if(!b || fread(b, 1, (size_t)n, f) != (size_t)n){
    free(b); fclose(f); return NULL;
  }
  fclose(f); b[n] = 0;
  fprintf(stderr, "[diag] magmasimplex shader=%s\n", u);
  return b;
}

static GLuint compile(GLenum t, const char *x){
  GLuint s = glCreateShader(t);
  glShaderSource(s, 1, &x, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if(!ok){
    char l[8192]; glGetShaderInfoLog(s, sizeof l, NULL, l);
    fprintf(stderr, "magmasimplex compile: %s\n", l);
    glDeleteShader(s); return 0;
  }
  return s;
}

static void init_magmasimplex(ModeInfo *m){
  static const char *vs =
    "#version 300 es\n"
    "layout(location=0) in vec2 p;\n"
    "void main(){ gl_Position = vec4(p, 0.0, 1.0); }\n";

  State *s = calloc(1, sizeof *s);
  if(!s){ ncz_harness_die(1); return; }
  m->data = s;

  char *x = load_shader();
  if(!x){ ncz_harness_die(1); return; }

  GLuint v = compile(GL_VERTEX_SHADER, vs);
  GLuint f = compile(GL_FRAGMENT_SHADER, x);
  free(x);
  if(!v || !f){ ncz_harness_die(1); return; }

  s->program = glCreateProgram();
  glAttachShader(s->program, v);
  glAttachShader(s->program, f);
  glLinkProgram(s->program);
  glDeleteShader(v); glDeleteShader(f);

  GLint ok = 0;
  glGetProgramiv(s->program, GL_LINK_STATUS, &ok);
  if(!ok){
    char l[8192];
    glGetProgramInfoLog(s->program, sizeof l, NULL, l);
    fprintf(stderr, "magmasimplex link: %s\n", l);
    ncz_harness_die(1); return;
  }

  /* Fullscreen quad: two triangles in clip space. */
  const float q[] = { -1,-1, 1,-1, -1, 1, -1, 1, 1,-1, 1, 1 };
  glGenBuffers(1, &s->vbo);
  glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

#define L(n) s->u_##n = glGetUniformLocation(s->program, "u_" #n)
  L(time); L(seed); L(resolution);
  L(way_idx);
  L(liquid_r); L(liquid_g); L(liquid_b);
  L(wax_r); L(wax_g); L(wax_b);
  L(secondary_r); L(secondary_g); L(secondary_b);
  L(blob_count); L(viscosity); L(scale); L(hotness);
  L(palette_drift); L(clear_liquid); L(rainbow);
  L(blob0); L(blob1); L(blob2); L(blob3); L(blob4); L(blob5); L(blob6); L(blob7);
  L(bhue0); L(bhue1); L(bhue2); L(bhue3); L(bhue4); L(bhue5); L(bhue6); L(bhue7);
#undef L

  uint32_t z = seed_rng();
  s->seed = z;

  /* Pick a colourway. The internal name pool is intentionally
   * weighted toward the classic 5 names (so not every launch is
   * maximally loud), but the indigo / dark-blue / forest slots
   * (idx 25..29) still get random pick-rate so the psychedelic
   * option shows up naturally. */
  int way = rndi(&z, 0, COLOURWAY_COUNT - 1);
  s->way_idx = way;
  s->way_name = colourway_names[way];
  const Way *wp = &ways[way];

  s->blob_count     = rndi(&z, 3, 5);
  s->viscosity      = rnd(&z, 0.7f, 1.4f);
  s->scale          = rnd(&z, 0.95f, 1.30f);
  s->hotness        = rnd(&z, 0.7f, 1.45f);
  s->palette_drift  = rnd(&z, 0.05f, 0.18f);
  s->clear_liquid   = wp->clear;
  /* Rainbow mode: forced-on for indigo / forest / oxblood slots
   * (idx 26..29), 20% chance otherwise. */
  int rainbow = (way >= 26 && way <= 29) ? 1 :
                (rnd(&z, 0.0f, 1.0f) < 0.20f ? 1 : 0);
  s->rainbow = rainbow;

  s->liquid_r = wp->lr; s->liquid_g = wp->lg; s->liquid_b = wp->lb;
  s->wax_r    = wp->wr; s->wax_g    = wp->wg; s->wax_b    = wp->wb;
  s->secondary_r = wp->sr; s->secondary_g = wp->sg; s->secondary_b = wp->sb;

  /* Per-blob hue ids. In rainbow mode spread 0..1 evenly so each
   * blob lands on a different hue on the colour wheel. In two-hue
   * mode randomise per blob so each is a different mix between
   * dominant and secondary. */
  for(int i = 0; i < MAX_BLOBS; i++){
    if(rainbow){
      s->bhue[i] = (float)i / (float)MAX_BLOBS
                 + 0.02f * rnd(&z, -1.0f, 1.0f);
    } else {
      s->bhue[i] = rnd(&z, 0.0f, 1.0f);
    }
    if(s->bhue[i] < 0.0f) s->bhue[i] = 0.0f;
    if(s->bhue[i] > 1.0f) s->bhue[i] = 1.0f;
  }

  /* Blob layout. Wider ring (radius 0.65..1.05) and larger radii
   * (0.42..0.62) so the wax actually occupies a substantial fraction
   * of the frame. The brief measured the old prototype at 5% non-
   * black; the new layout is sized so non-black coverage lands
   * comfortably above 90% even with the clearest of the colourways.
   * The ring is offset upward (y_anchor in -0.4..0.85) so blobs sit
   * primarily in the upper half of the slab — at the hot base below
   * them they can pool, rise, and stretch; they don't all converge
   * onto a single central column. */
  for(int i = 0; i < MAX_BLOBS; i++){
    float a = (float)i * (2.0f * (float)M_PI / (float)MAX_BLOBS)
            + rnd(&z, 0.0f, 0.5f);
    s->bx[i] = cosf(a) * (0.65f + 0.40f * rnd(&z, 0.0f, 1.0f));
    s->bz[i] = sinf(a) * (0.65f + 0.40f * rnd(&z, 0.0f, 1.0f));
    /* Spread y anchors: 70% upper, 30% lower, so the cluster is
     * primarily visible above the hot base — blobs are seen
     * "rising through the fluid", not "settled at the bottom". */
    float y_anchor;
    if(rnd(&z, 0.0f, 1.0f) < 0.70f){
      y_anchor = rnd(&z, 0.05f, 0.85f);   /* upper region */
    } else {
      y_anchor = rnd(&z, -0.55f, 0.05f);  /* lower region */
    }
    s->by[i] = y_anchor;
    s->br[i] = rnd(&z, 0.42f, 0.62f);
  }

  fprintf(stderr,
   "[diag] magmasimplex seed=%u way=%d (%s) blob_count=%d "
   "viscosity=%.3f scale=%.3f hotness=%.3f palette_drift=%.4f "
   "clear_liquid=%d rainbow=%d "
   "liquid=(%.2f,%.2f,%.2f) wax=(%.2f,%.2f,%.2f) secondary=(%.2f,%.2f,%.2f) "
   "GL=%s\n",
   s->seed, s->way_idx, s->way_name, s->blob_count,
   s->viscosity, s->scale, s->hotness, s->palette_drift,
   s->clear_liquid, s->rainbow,
   s->liquid_r, s->liquid_g, s->liquid_b,
   s->wax_r,    s->wax_g,    s->wax_b,
   s->secondary_r, s->secondary_g, s->secondary_b,
   glGetString(GL_VERSION));

  /* Blob layout, second diag line — helps downstream compare two
   * launches. */
  fprintf(stderr, "[diag] magmasimplex blobs:");
  for(int i = 0; i < s->blob_count; i++)
    fprintf(stderr, " b%d=(%.3f,%.3f,%.3f,r=%.3f,hue=%.3f)", i,
            s->bx[i], s->by[i], s->bz[i], s->br[i], s->bhue[i]);
  fprintf(stderr, "\n");

  s->started = now();
}

static void draw_magmasimplex(ModeInfo *m){
  State *s = m->data;
  if(!s || !s->program) return;

  int w = m->xgwa.width, h = m->xgwa.height;
  if(w < 1) w = 1;
  if(h < 1) h = 1;
  glViewport(0, 0, w, h);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(s->program);

  float t = (float)(now() - s->started);

  glUniform1f(s->u_time,       t);
  glUniform1f(s->u_seed,       (float)s->seed);
  glUniform2f(s->u_resolution, (float)w, (float)h);
  glUniform1f(s->u_way_idx,        (float)s->way_idx);
  glUniform1f(s->u_liquid_r,       s->liquid_r);
  glUniform1f(s->u_liquid_g,       s->liquid_g);
  glUniform1f(s->u_liquid_b,       s->liquid_b);
  glUniform1f(s->u_wax_r,          s->wax_r);
  glUniform1f(s->u_wax_g,          s->wax_g);
  glUniform1f(s->u_wax_b,          s->wax_b);
  glUniform1f(s->u_secondary_r,    s->secondary_r);
  glUniform1f(s->u_secondary_g,    s->secondary_g);
  glUniform1f(s->u_secondary_b,    s->secondary_b);
  glUniform1f(s->u_blob_count,     (float)s->blob_count);
  glUniform1f(s->u_viscosity,      s->viscosity);
  glUniform1f(s->u_scale,          s->scale);
  glUniform1f(s->u_hotness,        s->hotness);
  glUniform1f(s->u_palette_drift,  s->palette_drift);
  glUniform1f(s->u_clear_liquid,   (float)s->clear_liquid);
  glUniform1f(s->u_rainbow,        (float)s->rainbow);

  /* Animate blob positions: a global buoyancy wave plus per-blob
   * phase offset, slowed by u_viscosity. Vertical range stays inside
   * roughly [-1.0, 1.0] in y. xz position also wobbles a touch to
   * break the perfect ring. */
  float visc = s->viscosity;
  float blob_data[MAX_BLOBS][4];
  int n = s->blob_count;
  for(int i = 0; i < MAX_BLOBS; i++){
    if(i < n){
      float ph = (float)i * 0.7f;
      /* Per-blob slow wobble in y; smaller than the prototype so
       * blobs don't all drift up to the same column and merge. The
       * "global wave" idea (everyone rises and falls together) was
       * dropped because it forced them all to the same height and
       * collapsed the field into one merged mass. */
      float per_blob = 0.25f * sinf(t * (0.42f / visc) + ph);
      float y = s->by[i] + per_blob;
      if(y >  1.05f) y =  1.05f;
      if(y < -1.05f) y = -1.05f;
      float x = s->bx[i] + 0.12f * sinf(t * (0.55f / visc) + ph);
      float z = s->bz[i] + 0.12f * cosf(t * (0.61f / visc) + ph);
      blob_data[i][0] = x;
      blob_data[i][1] = y;
      blob_data[i][2] = z;
      blob_data[i][3] = s->br[i];
    } else {
      blob_data[i][0] = 0.0f;
      blob_data[i][1] = 0.0f;
      blob_data[i][2] = 0.0f;
      blob_data[i][3] = 0.0f;
    }
  }
  glUniform4fv(s->u_blob0, 1, blob_data[0]);
  glUniform4fv(s->u_blob1, 1, blob_data[1]);
  glUniform4fv(s->u_blob2, 1, blob_data[2]);
  glUniform4fv(s->u_blob3, 1, blob_data[3]);
  glUniform4fv(s->u_blob4, 1, blob_data[4]);
  glUniform4fv(s->u_blob5, 1, blob_data[5]);
  glUniform4fv(s->u_blob6, 1, blob_data[6]);
  glUniform4fv(s->u_blob7, 1, blob_data[7]);

  glUniform1f(s->u_bhue0, s->bhue[0]);
  glUniform1f(s->u_bhue1, s->bhue[1]);
  glUniform1f(s->u_bhue2, s->bhue[2]);
  glUniform1f(s->u_bhue3, s->bhue[3]);
  glUniform1f(s->u_bhue4, s->bhue[4]);
  glUniform1f(s->u_bhue5, s->bhue[5]);
  glUniform1f(s->u_bhue6, s->bhue[6]);
  glUniform1f(s->u_bhue7, s->bhue[7]);

  glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  glDisable(GL_DEPTH_TEST);
  ncz_gles3_draw_arrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);

  /* Per-vendor frame-time measurement. Gated on env so a normal
   * launch is cheap on Intel (the brief calls out stutter caused by
   * the fprintf itself). */
  static unsigned long _ft_counter;
  static double _ft_prev;
  static int _ft_enabled;
  if(!_ft_enabled) _ft_enabled = (getenv("NCZ_MAGMASIMPLEX_PERF_LOG") != NULL);
  if(_ft_enabled){
    double _t_now = now();
    if((++_ft_counter % 30) == 0){
      if(_ft_prev > 0.0){
        double _dt = (_t_now - _ft_prev) / 30.0;
        fprintf(stderr, "[diag] magmasimplex frame_t frame=%lu dt_ms=%.3f\n",
                _ft_counter, _dt * 1000.0);
        fflush(stderr);
      }
      _ft_prev = now();
    }
  }

  /* First-frame sanity samples. The point is to prove the pipeline
   * rendered something more than the legacy 5% non-black, and to
   * show several distinct hues in a single frame. We sample a
   * 4x8 grid across the frame and report each RGB so downstream
   * can grep genuinely different hues — not just our description. */
  static int once;
  if(!once++){
    /* Read up to 32 pixels (4 rows x 8 cols). */
    unsigned char px[32 * 4] = {0};
    int xs[8] = {w/8, 2*w/8, 3*w/8, 4*w/8, 5*w/8, 6*w/8, 7*w/8, w-2};
    int ys[4] = {h/8, h/2, 3*h/4, 7*h/8};
    int idx = 0;
    int rows_done = 0;
    for(int yi = 0; yi < 4 && idx + 4 <= (int)sizeof px; yi++){
      for(int xi = 0; xi < 8 && idx + 4 <= (int)sizeof px; xi++){
        glReadPixels(xs[xi], ys[yi], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &px[idx]);
        idx += 4;
      }
      rows_done++;
    }
    GLenum e = glGetError();
    fprintf(stderr,
     "[diag] magmasimplex first draw gl_error=0x%x samples=",
     e);
    for(int r = 0; r < rows_done; r++){
      fprintf(stderr, "y%d:", ys[r]);
      for(int xi = 0; xi < 8; xi++){
        int base = (r*8 + xi) * 4;
        if(base + 2 >= (int)sizeof px) break;
        fprintf(stderr, "(%u,%u,%u)", px[base], px[base+1], px[base+2]);
      }
      fprintf(stderr, " ");
    }
    fprintf(stderr, "\n");
    once = 1;
  }
}

static void free_magmasimplex(ModeInfo *m){
  State *s = m->data;
  if(!s) return;
  if(s->vbo) glDeleteBuffers(1, &s->vbo);
  if(s->program) glDeleteProgram(s->program);
  free(s);
  m->data = NULL;
}

static void reshape_magmasimplex(ModeInfo *m, int w, int h){
  (void)m; (void)w; (void)h;
}

static Bool magmasimplex_handle_event(ModeInfo *m, XEvent *e){
  (void)m; (void)e;
  return False;
}

static void release_magmasimplex(ModeInfo *m){ (void)m; }

static ModeSpecOpt magmasimplex_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table magmasimplex_xscreensaver_function_table = {
  .name = "magmasimplex",
  .class_ = "Magmasimplex",
  .init_cb = init_magmasimplex,
  .draw_cb = draw_magmasimplex,
  .reshape_cb = reshape_magmasimplex,
  .event_cb = magmasimplex_handle_event,
  .free_cb = free_magmasimplex,
  .release_cb = release_magmasimplex,
  .opts = &magmasimplex_opts,
  .defaults_str = DEFAULTS
};
