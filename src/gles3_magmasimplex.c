/* gles3_magmasimplex.c — GLES3-native volumetric molten-wax-in-a-
 *                        luminous-fluid piece driven by a real fluid
 *                        simulation.
 *
 * Round 20 (replace kinematic metaballs with a real fluid sim).
 *
 * Why this round exists: the round-19 build was kinematic metaballs
 * raymarched per pixel — the blobs followed scripted paths, were
 * unioned by a smooth-min SDF, and never deformed. The visible
 * defects — perfectly smooth surfaces, no necking, no pinch-off,
 * no internal churn, motion that reads as "objects being moved"
 * rather than "fluid flowing" — are not fixable in that pipeline.
 * They need an actual fluid.
 *
 * What this round does: runs a real two-phase fluid simulation on a
 * low-resolution grid, ping-ponged in FBOs every frame, then renders
 * the result. Stable-fluids velocity (semi-Lagrangian advection +
 * Jacobi pressure projection + viscosity); temperature field driven
 * by an emitter low in frame and cooled at the top, advected by the
 * velocity; an Allen-Cahn phase field with surface tension that
 * gives necking, coalescence and breakup for free; per-cell hue
 * advected by the flow so merges mix colour automatically.
 *
 * Tier model (per docs/superpowers/specs/2026-09-25-screensaver-
 * family-doctrine.md):
 *   - Low (default on Intel UHD): 2D field simulation, raymarched
 *     surface using the phase field for thickness/SDF. Cheaper than
 *     the round-19 SDF march because the march steps are bound to a
 *     single bilinear field lookup rather than N analytic SDF
 *     evaluations per step.
 *   - High/Medium (reference target): voxel volumetric simulation,
 *     64x64x64 atlas, raymarched with real Beer-Lambert absorption
 *     and emission. Opt-in via NCZ_MAGMASIMPLEX_3D=1.
 *
 * Tier is auto-selected: Intel UHD (detected via GL_RENDERER) lands
 * on Low. Anything else lands on High. Both tiers go through the
 * same simulation primitives; the 2D tier is not a degraded version
 * of the 3D tier but a separate, honestly-modeled fluid field.
 *
 * Optics unchanged in spirit from round 19: real subsurface
 * scattering (thin edges glow hot, thick cores go deep-saturated),
 * the fluid is a scattering medium (Beer-Lambert attenuation along
 * the camera ray), saturated psychedelic colourways, per-blob hue
 * that mixes visibly at merges. What changes is HOW these are
 * computed: from a real field, not from a sum of analytic SDF
 * blobs.
 *
 * Per-launch randomisation (seeded from /dev/urandom, overridable
 * via NCZ_MAGMASIMPLEX_FIXED_SEED) covers: way_idx (30 colourways),
 * emitter strength, surface tension coefficient (the single most
 * characterful knob — controls how readily blobs merge and split),
 * viscosity, scale, hotness, and the initial seed blob layout.
 *
 * Printed to stderr in the established [diag] format. The first
 * frame also samples eight points along a horizontal slice and
 * prints their RGB to prove multicolour on screen. Frame times are
 * logged every 30 frames when NCZ_MAGMASIMPLEX_PERF_LOG is set.
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
#ifdef NCZ_GLES3_BUILD
extern void ncz_harness_die(int code);
#endif

#define MAX_BLOBS 8

/* ------------------------------------------------------------------ */
/* Tier selection                                                       */
/* ------------------------------------------------------------------ */
typedef enum {
  TIER_LOW_2D = 0,   /* 2D field on the plane. Default on Intel UHD.  */
  TIER_HIGH_3D = 1   /* Voxel volumetric on a tiled 3D atlas.        */
} Tier;

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */
typedef struct {
  /* ---- Public GL handles ---- */
  GLuint vbo;
  GLuint program_render;
  /* 2D simulation programs. */
  GLuint program_advect_v;
  GLuint program_advect_t;
  GLuint program_advect_p;
  GLuint program_advect_h;
  GLuint program_buoyancy;
  GLuint program_project;
  GLuint program_heat;
  GLuint program_phase;
  GLuint program_init_seed;
  GLuint program_zero;
  /* 3D simulation programs (only used in TIER_HIGH_3D). */
  GLuint program_advect_v_3d;
  GLuint program_advect_t_3d;
  GLuint program_advect_p_3d;
  GLuint program_advect_h_3d;
  GLuint program_buoyancy_3d;
  GLuint program_project_3d;
  GLuint program_heat_3d;
  GLuint program_phase_3d;
  GLuint program_init_seed_3d;

  /* ---- Field grids (ping-pong) ---- */
  /* 2D tier: 256x256 fields. */
  GLuint fbo_v_a, fbo_v_b;     GLuint tex_v_a, tex_v_b;
  GLuint fbo_t_a, fbo_t_b;     GLuint tex_t_a, tex_t_b;
  GLuint fbo_p_a, fbo_p_b;     GLuint tex_p_a, tex_p_b;
  GLuint fbo_hue;              GLuint tex_hue;
  GLuint fbo_pressure_a, fbo_pressure_b;
  GLuint                       tex_pressure_a, tex_pressure_b;

  /* 3D tier: 64^3 atlas (RGBA16F) tiled in a 2D texture. */
  GLuint fbo_atlas_v_a, fbo_atlas_v_b;       GLuint atlas_v_a, atlas_v_b;
  GLuint fbo_atlas_t_a, fbo_atlas_t_b;       GLuint atlas_t_a, atlas_t_b;
  GLuint fbo_atlas_p_a, fbo_atlas_p_b;       GLuint atlas_p_a, atlas_p_b;
  GLuint fbo_atlas_hue_a, fbo_atlas_hue_b;   GLuint atlas_hue_a, atlas_hue_b;
  GLuint fbo_atlas_pr_a, fbo_atlas_pr_b;     GLuint atlas_pr_a, atlas_pr_b;

  /* ---- Uniform locations (cached) ---- */
  GLint u_render_seed, u_render_time, u_render_resolution;
  GLint u_render_liquid, u_render_wax, u_render_secondary;
  GLint u_render_emitter, u_render_hotness, u_render_tier;
  GLint u_render_tex_phase, u_render_tex_hue, u_render_tex_temp;

  GLint u_sim_tex_src;
  GLint u_sim_resolution;
  GLint u_sim_dt;
  GLint u_sim_viscosity;
  GLint u_sim_emitter_pos;
  GLint u_sim_emitter_r;
  GLint u_sim_emitter_strength;
  GLint u_sim_hotness;
  GLint u_sim_amb_cool;
  GLint u_sim_buoy_alpha;
  GLint u_sim_surface_tension;
  GLint u_sim_seed;
  GLint u_sim_blob_count;
  GLint u_sim_blob0, u_sim_blob1, u_sim_blob2, u_sim_blob3;
  GLint u_sim_blob4, u_sim_blob5, u_sim_blob6, u_sim_blob7;

  /* ---- Per-launch state ---- */
  Tier tier;
  int grid_w, grid_h;       /* 2D grid dimensions */
  int grid_n;               /* 3D grid (cubic) */
  int atlas_w, atlas_h;     /* 3D atlas texture dims */
  int sim_tick;
  double started;
  uint32_t seed;
  int way_idx;
  const char *way_name;
  /* Per-launch knobs */
  float emitter_x, emitter_y, emitter_r;
  float emitter_strength;
  float amb_cool;
  float buoyancy_alpha;
  float surface_tension;
  float viscosity;
  float scale;
  float hotness;
  float liquid_r, liquid_g, liquid_b;
  float wax_r, wax_g, wax_b;
  float secondary_r, secondary_g, secondary_b;
  int clear_liquid;
  int rainbow;
  int low_perf;
  /* Per-blob layout (only used to seed the fields initially) */
  float bx[MAX_BLOBS], by[MAX_BLOBS], bz[MAX_BLOBS], br[MAX_BLOBS];
  float bhue[MAX_BLOBS];
  int render_3d;
  int have_half_float_rt;
} State;

/* ------------------------------------------------------------------ */
/* Colourways                                                           */
/* ------------------------------------------------------------------ */
static const char *colourway_names[] = {
  "ember", "orchid", "ultraviolet", "sunflower", "neon",
  "ember", "orchid", "ultraviolet", "sunflower", "neon",
  "ember", "orchid", "ultraviolet", "sunflower", "neon",
  "ember", "orchid", "ultraviolet", "sunflower", "neon",
  "ember", "orchid", "ultraviolet", "sunflower", "neon",
  "ember", "orchid", "ultraviolet", "sunflower", "neon",
};
#define COLOURWAY_COUNT 30

typedef struct { float lr, lg, lb; float wr, wg, wb; float sr, sg, sb; int clear; } Way;
static const Way ways[COLOURWAY_COUNT] = {
  { 1, 1, 1, 1.00, 0.10, 0.10, 1.00, 0.55, 0.20, 1 },
  { 1, 1, 1, 1.00, 0.55, 0.20, 0.95, 0.15, 0.55, 1 },
  { 1, 1, 1, 1.00, 0.85, 0.10, 0.85, 0.10, 0.55, 1 },
  { 1, 1, 1, 0.70, 0.10, 0.95, 0.10, 0.55, 1.00, 1 },
  { 1, 1, 1, 0.20, 0.95, 0.30, 0.10, 0.55, 1.00, 1 },
  { 1, 1, 1, 1.00, 0.30, 0.75, 1.00, 0.10, 0.45, 1 },
  { 0.10, 0.25, 0.95, 1.00, 0.10, 0.20, 1.00, 0.85, 0.10, 0 },
  { 0.10, 0.25, 0.95, 1.00, 1.00, 0.20, 0.95, 0.10, 0.10, 0 },
  { 0.10, 0.25, 0.95, 0.95, 0.95, 0.95, 0.70, 0.10, 0.95, 0 },
  { 0.10, 0.25, 0.95, 0.20, 0.95, 0.30, 0.70, 0.10, 0.95, 0 },
  { 0.10, 0.25, 0.95, 0.70, 0.10, 0.95, 1.00, 0.10, 0.20, 0 },
  { 0.95, 0.20, 0.55, 0.95, 0.95, 0.95, 1.00, 0.10, 0.10, 0 },
  { 0.95, 0.10, 0.10, 0.95, 0.95, 0.95, 1.00, 0.85, 0.10, 0 },
  { 0.95, 0.10, 0.10, 1.00, 0.85, 0.10, 0.70, 0.10, 0.95, 0 },
  { 0.95, 0.10, 0.10, 0.20, 0.95, 0.40, 1.00, 0.30, 0.85, 0 },
  { 0.70, 0.10, 0.95, 0.95, 0.95, 0.95, 1.00, 0.10, 0.10, 0 },
  { 0.70, 0.10, 0.95, 1.00, 0.20, 0.10, 1.00, 0.85, 0.10, 0 },
  { 0.70, 0.10, 0.95, 1.00, 0.85, 0.10, 1.00, 0.10, 0.20, 0 },
  { 1.00, 0.50, 0.10, 0.95, 0.95, 0.95, 1.00, 0.10, 0.10, 0 },
  { 1.00, 0.50, 0.10, 0.70, 0.10, 0.95, 0.20, 0.95, 0.30, 0 },
  { 1.00, 0.50, 0.10, 0.05, 0.05, 0.05, 1.00, 0.10, 0.10, 0 },
  { 0.20, 0.95, 0.30, 0.95, 0.95, 0.95, 1.00, 0.10, 0.10, 0 },
  { 0.20, 0.95, 0.30, 0.10, 0.25, 0.95, 1.00, 0.85, 0.10, 0 },
  { 0.20, 0.95, 0.30, 1.00, 0.85, 0.10, 1.00, 0.10, 0.20, 0 },
  { 0.05, 0.05, 0.25, 0.95, 0.95, 0.95, 1.00, 0.10, 0.10, 0 },
  { 0.25, 0.05, 0.45, 1.00, 0.30, 0.85, 1.00, 0.85, 0.10, 0 },
  { 0.05, 0.40, 0.15, 0.95, 0.95, 0.95, 1.00, 0.85, 0.10, 0 },
  { 0.30, 0.10, 0.05, 1.00, 0.85, 0.10, 1.00, 0.10, 0.10, 0 },
  { 0.05, 0.10, 0.45, 1.00, 0.85, 0.20, 0.70, 0.10, 0.95, 0 },
  { 0.95, 0.05, 0.20, 0.20, 1.00, 0.50, 1.00, 0.85, 0.20, 0 },
};

/* ------------------------------------------------------------------ */
/* Time + RNG                                                            */
/* ------------------------------------------------------------------ */
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
static float rnd(uint32_t *s, float a, float b){
  *s ^= *s << 13;
  *s ^= *s >> 17;
  *s ^= *s << 5;
  return a + (b - a) * (float)(*s & 0x00ffffffu) / 16777215.0f;
}
static int rndi(uint32_t *s, int a, int b){
  return a + (int)(rnd(s, 0.0f, 1.0f) * (float)(b - a + 1));
}

/* ------------------------------------------------------------------ */
/* Shader plumbing                                                       */
/* ------------------------------------------------------------------ */
static char *load_text(const char *vendor_rel){
  const char *base = vendor_rel + strlen("vendor/magmasimplex/");
  char path[1024];
  FILE *f = NULL; const char *u = NULL;
  const char *roots[] = {
    "vendor/magmasimplex/",
    "../vendor/magmasimplex/",
    "../../vendor/magmasimplex/",
    "/usr/share/ncz-screensavers/shaders/magmasimplex/"
  };
  for(unsigned i = 0; i < 4; i++){
    snprintf(path, sizeof path, "%s%s", roots[i], base);
    f = fopen(path, "rb");
    if(f){ u = path; break; }
  }
  if(!f){
    fprintf(stderr, "magmasimplex: cannot locate %s\n", vendor_rel);
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

static GLuint make_program(const char *vs_src, const char *fs_src, const char *name){
  GLuint v = compile(GL_VERTEX_SHADER, vs_src);
  GLuint f = compile(GL_FRAGMENT_SHADER, fs_src);
  if(!v || !f){ return 0; }
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glBindAttribLocation(p, 0, "a_pos");
  glLinkProgram(p);
  glDeleteShader(v); glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if(!ok){
    char l[8192];
    glGetProgramInfoLog(p, sizeof l, NULL, l);
    fprintf(stderr, "magmasimplex link %s: %s\n", name, l);
    glDeleteProgram(p); return 0;
  }
  return p;
}

static const char *fullscreen_vs =
  "#version 300 es\n"
  "precision highp float;\n"
  "layout(location=0) in vec2 a_pos;\n"
  "void main(){ gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

/* ------------------------------------------------------------------ */
/* FBO helpers                                                           */
/* ------------------------------------------------------------------ */
static int check_fbo(const char *what){
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if(st != GL_FRAMEBUFFER_COMPLETE){
    fprintf(stderr, "magmasimplex: FBO %s incomplete 0x%x\n", what, (unsigned)st);
    return -1;
  }
  return 0;
}

static void make_2d_fbo_pair(GLuint *fbo_a, GLuint *fbo_b,
                             GLuint *tex_a, GLuint *tex_b,
                             int w, int h, GLenum internal_format,
                             GLenum format, GLenum type){
  glGenFramebuffers(1, fbo_a);
  glGenFramebuffers(1, fbo_b);
  glGenTextures(1, tex_a);
  glGenTextures(1, tex_b);
  GLuint fbos[2] = { *fbo_a, *fbo_b };
  GLuint texs[2] = { *tex_a, *tex_b };
  for(int i = 0; i < 2; i++){
    glBindTexture(GL_TEXTURE_2D, texs[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, format, type, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texs[i], 0);
    check_fbo(i == 0 ? "2d a" : "2d b");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static void make_atlas_fbo_pair(GLuint *fbo_a, GLuint *fbo_b,
                                GLuint *tex_a, GLuint *tex_b,
                                int atlas_w, int atlas_h,
                                GLenum internal_format,
                                GLenum format, GLenum type){
  glGenFramebuffers(1, fbo_a);
  glGenFramebuffers(1, fbo_b);
  glGenTextures(1, tex_a);
  glGenTextures(1, tex_b);
  GLuint fbos[2] = { *fbo_a, *fbo_b };
  GLuint texs[2] = { *tex_a, *tex_b };
  for(int i = 0; i < 2; i++){
    glBindTexture(GL_TEXTURE_2D, texs[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, atlas_w, atlas_h, 0,
                 format, type, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texs[i], 0);
    check_fbo(i == 0 ? "atlas a" : "atlas b");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

/* ------------------------------------------------------------------ */
/* Cached-uniform lookup                                                 */
/* ------------------------------------------------------------------ */
static void fetch_sim_uniforms(State *s, GLuint p){
  s->u_sim_tex_src           = glGetUniformLocation(p, "u_src");
  s->u_sim_resolution        = glGetUniformLocation(p, "u_resolution");
  s->u_sim_dt                = glGetUniformLocation(p, "u_dt");
  s->u_sim_viscosity         = glGetUniformLocation(p, "u_viscosity");
  s->u_sim_emitter_pos       = glGetUniformLocation(p, "u_emitter_pos");
  s->u_sim_emitter_r         = glGetUniformLocation(p, "u_emitter_r");
  s->u_sim_emitter_strength  = glGetUniformLocation(p, "u_emitter_strength");
  s->u_sim_hotness           = glGetUniformLocation(p, "u_hotness");
  s->u_sim_amb_cool          = glGetUniformLocation(p, "u_amb_cool");
  s->u_sim_buoy_alpha        = glGetUniformLocation(p, "u_buoy_alpha");
  s->u_sim_surface_tension   = glGetUniformLocation(p, "u_surface_tension");
  s->u_sim_seed              = glGetUniformLocation(p, "u_seed");
  s->u_sim_blob_count        = glGetUniformLocation(p, "u_blob_count");
  s->u_sim_blob0 = glGetUniformLocation(p, "u_blob0");
  s->u_sim_blob1 = glGetUniformLocation(p, "u_blob1");
  s->u_sim_blob2 = glGetUniformLocation(p, "u_blob2");
  s->u_sim_blob3 = glGetUniformLocation(p, "u_blob3");
  s->u_sim_blob4 = glGetUniformLocation(p, "u_blob4");
  s->u_sim_blob5 = glGetUniformLocation(p, "u_blob5");
  s->u_sim_blob6 = glGetUniformLocation(p, "u_blob6");
  s->u_sim_blob7 = glGetUniformLocation(p, "u_blob7");
}

/* ------------------------------------------------------------------ */
/* Sim-step helpers                                                      */
/* ------------------------------------------------------------------ */
static void sim_bind_dst(GLuint dst_fbo, int w, int h){
  glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST);
}

static void sim_set_common(State *s, GLuint src_tex){
  /* unit 0: u_src */
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, src_tex);
  if(s->u_sim_tex_src >= 0)      glUniform1i(s->u_sim_tex_src, 0);
  if(s->u_sim_dt >= 0)           glUniform1f(s->u_sim_dt, 1.0f / 60.0f);
  if(s->u_sim_viscosity >= 0)    glUniform1f(s->u_sim_viscosity, s->viscosity);
  if(s->u_sim_emitter_pos >= 0)  glUniform2f(s->u_sim_emitter_pos, s->emitter_x, s->emitter_y);
  if(s->u_sim_emitter_r >= 0)    glUniform1f(s->u_sim_emitter_r, s->emitter_r);
  if(s->u_sim_emitter_strength >= 0) glUniform1f(s->u_sim_emitter_strength, s->emitter_strength);
  if(s->u_sim_hotness >= 0)      glUniform1f(s->u_sim_hotness, s->hotness);
  if(s->u_sim_amb_cool >= 0)     glUniform1f(s->u_sim_amb_cool, s->amb_cool);
  if(s->u_sim_buoy_alpha >= 0)   glUniform1f(s->u_sim_buoy_alpha, s->buoyancy_alpha);
  if(s->u_sim_surface_tension >= 0) glUniform1f(s->u_sim_surface_tension, s->surface_tension);
  if(s->u_sim_seed >= 0)         glUniform1f(s->u_sim_seed, (float)s->seed * 1e-5f);
  if(s->u_sim_blob_count >= 0)   glUniform1f(s->u_sim_blob_count, 6.0f);
  if(s->u_sim_blob0 >= 0) glUniform4f(s->u_sim_blob0, s->bx[0], s->by[0], s->bz[0], s->br[0]);
  if(s->u_sim_blob1 >= 0) glUniform4f(s->u_sim_blob1, s->bx[1], s->by[1], s->bz[1], s->br[1]);
  if(s->u_sim_blob2 >= 0) glUniform4f(s->u_sim_blob2, s->bx[2], s->by[2], s->bz[2], s->br[2]);
  if(s->u_sim_blob3 >= 0) glUniform4f(s->u_sim_blob3, s->bx[3], s->by[3], s->bz[3], s->br[3]);
  if(s->u_sim_blob4 >= 0) glUniform4f(s->u_sim_blob4, s->bx[4], s->by[4], s->bz[4], s->br[4]);
  if(s->u_sim_blob5 >= 0) glUniform4f(s->u_sim_blob5, s->bx[5], s->by[5], s->bz[5], s->br[5]);
  if(s->u_sim_blob6 >= 0) glUniform4f(s->u_sim_blob6, s->bx[6], s->by[6], s->bz[6], s->br[6]);
  if(s->u_sim_blob7 >= 0) glUniform4f(s->u_sim_blob7, s->bx[7], s->by[7], s->bz[7], s->br[7]);
}

static void draw_quad(State *s){
  glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  ncz_gles3_draw_arrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* Bind a second field to unit 1 with sampler name u_field, if the
 * shader declares it. The caller must already be in program p. */
static void bind_field_unit1(GLuint p, GLuint field_tex){
  if(!field_tex) return;
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, field_tex);
  GLint loc = glGetUniformLocation(p, "u_field");
  if(loc >= 0) glUniform1i(loc, 1);
  glActiveTexture(GL_TEXTURE0);
}

/* Sim pass with optional second field. */
static void sim_pass2(State *s, GLuint p, GLuint src_tex, GLuint dst_fbo,
                      int w, int h, GLuint field_tex){
  glUseProgram(p);
  sim_set_common(s, src_tex);
  bind_field_unit1(p, field_tex);
  sim_bind_dst(dst_fbo, w, h);
  draw_quad(s);
  glUseProgram(0);
}

/* ------------------------------------------------------------------ */
/* 2D simulation step                                                    */
/* ------------------------------------------------------------------ */
static void step_2d(State *s){
  /* Step order, per Stam's "stable fluids" with a phase field:
   *
   *   1. Buoyancy: T (unit 1) -> v.
   *   2. Advect velocity by itself.
   *   3. Project: make v divergence-free. (6 Jacobi iters.)
   *   4. Heat: emitter adds T at the bottom, ambient cools at top.
   *   5. Advect temperature by v.
   *   6. Advect phase by v.
   *   7. Advect hue by v.
   *   8. Allen-Cahn phase update. */
  int W = s->grid_w, H = s->grid_h;

  GLuint v_tex[2] = { s->tex_v_a, s->tex_v_b };
  GLuint v_fbo[2] = { s->fbo_v_a, s->fbo_v_b };
  int v_i = 0;

  /* Buoyancy reads T on unit 1 and writes v. */
  sim_pass2(s, s->program_buoyancy, v_tex[v_i], v_fbo[1 - v_i], W, H, s->tex_t_a);
  v_i = 1 - v_i;

  /* Advect v by itself. */
  sim_pass2(s, s->program_advect_v, v_tex[v_i], v_fbo[1 - v_i], W, H, 0);
  v_i = 1 - v_i;

  /* Project. Three steps:
   *   zero pressure, iterate Jacobi, subtract grad(p) from v. */
  GLuint pr_tex[2] = { s->tex_pressure_a, s->tex_pressure_b };
  GLuint pr_fbo[2] = { s->fbo_pressure_a, s->fbo_pressure_b };
  int pr_i = 0;
  glUseProgram(s->program_zero);
  sim_bind_dst(pr_fbo[0], W, H);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  draw_quad(s);
  glUseProgram(0);

  for(int it = 0; it < 6; it++){
    sim_pass2(s, s->program_project, pr_tex[pr_i], pr_fbo[1 - pr_i], W, H, v_tex[v_i]);
    pr_i = 1 - pr_i;
  }

  /* Subtract grad(p) from v. The project shader branches on
   * u_subtract (>=0.5 = do subtraction). */
  glUseProgram(s->program_project);
  sim_set_common(s, v_tex[v_i]);
  GLint loc_sub = glGetUniformLocation(s->program_project, "u_subtract");
  if(loc_sub >= 0) glUniform1f(loc_sub, 1.0f);
  bind_field_unit1(s->program_project, pr_tex[pr_i]);
  sim_bind_dst(v_fbo[1 - v_i], W, H);
  draw_quad(s);
  glUseProgram(0);
  v_i = 1 - v_i;

  /* Heat. */
  sim_pass2(s, s->program_heat, s->tex_t_a, s->fbo_t_b, W, H, 0);
  { GLuint tt = s->tex_t_a; s->tex_t_a = s->tex_t_b; s->tex_t_b = tt; }
  { GLuint tf = s->fbo_t_a; s->fbo_t_a = s->fbo_t_b; s->fbo_t_b = tf; }

  /* Advect T by v. */
  sim_pass2(s, s->program_advect_t, v_tex[v_i], s->fbo_t_b, W, H, s->tex_t_a);
  { GLuint tt = s->tex_t_a; s->tex_t_a = s->tex_t_b; s->tex_t_b = tt; }
  { GLuint tf = s->fbo_t_a; s->fbo_t_a = s->fbo_t_b; s->fbo_t_b = tf; }

  /* Advect phase. */
  sim_pass2(s, s->program_advect_p, v_tex[v_i], s->fbo_p_b, W, H, s->tex_p_a);
  { GLuint tt = s->tex_p_a; s->tex_p_a = s->tex_p_b; s->tex_p_b = tt; }
  { GLuint tf = s->fbo_p_a; s->fbo_p_a = s->fbo_p_b; s->fbo_p_b = tf; }

  /* Allen-Cahn phase update. */
  sim_pass2(s, s->program_phase, s->tex_p_a, s->fbo_p_b, W, H, 0);
  { GLuint tt = s->tex_p_a; s->tex_p_a = s->tex_p_b; s->tex_p_b = tt; }
  { GLuint tf = s->fbo_p_a; s->fbo_p_a = s->fbo_p_b; s->fbo_p_b = tf; }

  /* Advect hue (single FBO). */
  sim_pass2(s, s->program_advect_h, v_tex[v_i], s->fbo_hue, W, H, s->tex_hue);
}

/* ------------------------------------------------------------------ */
/* 3D simulation step                                                    */
/* ------------------------------------------------------------------ */
static void step_3d(State *s){
  int AW = s->atlas_w, AH = s->atlas_h;

  GLuint v_tex[2] = { s->atlas_v_a, s->atlas_v_b };
  GLuint v_fbo[2] = { s->fbo_atlas_v_a, s->fbo_atlas_v_b };
  int v_i = 0;

  sim_pass2(s, s->program_buoyancy_3d, v_tex[v_i], v_fbo[1 - v_i], AW, AH, s->atlas_t_a);
  v_i = 1 - v_i;

  sim_pass2(s, s->program_advect_v_3d, v_tex[v_i], v_fbo[1 - v_i], AW, AH, 0);
  v_i = 1 - v_i;

  GLuint pr_tex[2] = { s->atlas_pr_a, s->atlas_pr_b };
  GLuint pr_fbo[2] = { s->fbo_atlas_pr_a, s->fbo_atlas_pr_b };
  int pr_i = 0;
  glUseProgram(s->program_zero);
  sim_bind_dst(pr_fbo[0], AW, AH);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  draw_quad(s);
  glUseProgram(0);
  for(int it = 0; it < 4; it++){
    sim_pass2(s, s->program_project_3d, pr_tex[pr_i], pr_fbo[1 - pr_i], AW, AH, v_tex[v_i]);
    pr_i = 1 - pr_i;
  }
  glUseProgram(s->program_project_3d);
  sim_set_common(s, v_tex[v_i]);
  GLint loc_sub3 = glGetUniformLocation(s->program_project_3d, "u_subtract");
  if(loc_sub3 >= 0) glUniform1f(loc_sub3, 1.0f);
  bind_field_unit1(s->program_project_3d, pr_tex[pr_i]);
  sim_bind_dst(v_fbo[1 - v_i], AW, AH);
  draw_quad(s);
  glUseProgram(0);
  v_i = 1 - v_i;

  sim_pass2(s, s->program_heat_3d, s->atlas_t_a, s->fbo_atlas_t_b, AW, AH, 0);
  { GLuint tt = s->atlas_t_a; s->atlas_t_a = s->atlas_t_b; s->atlas_t_b = tt; }
  { GLuint tf = s->fbo_atlas_t_a; s->fbo_atlas_t_a = s->fbo_atlas_t_b; s->fbo_atlas_t_b = tf; }

  sim_pass2(s, s->program_advect_t_3d, v_tex[v_i], s->fbo_atlas_t_b, AW, AH, s->atlas_t_a);
  { GLuint tt = s->atlas_t_a; s->atlas_t_a = s->atlas_t_b; s->atlas_t_b = tt; }
  { GLuint tf = s->fbo_atlas_t_a; s->fbo_atlas_t_a = s->fbo_atlas_t_b; s->fbo_atlas_t_b = tf; }

  sim_pass2(s, s->program_advect_p_3d, v_tex[v_i], s->fbo_atlas_p_b, AW, AH, s->atlas_p_a);
  { GLuint tt = s->atlas_p_a; s->atlas_p_a = s->atlas_p_b; s->atlas_p_b = tt; }
  { GLuint tf = s->fbo_atlas_p_a; s->fbo_atlas_p_a = s->fbo_atlas_p_b; s->fbo_atlas_p_b = tf; }

  sim_pass2(s, s->program_phase_3d, s->atlas_p_a, s->fbo_atlas_p_b, AW, AH, 0);
  { GLuint tt = s->atlas_p_a; s->atlas_p_a = s->atlas_p_b; s->atlas_p_b = tt; }
  { GLuint tf = s->fbo_atlas_p_a; s->fbo_atlas_p_a = s->fbo_atlas_p_b; s->fbo_atlas_p_b = tf; }

  sim_pass2(s, s->program_advect_h_3d, v_tex[v_i], s->fbo_atlas_hue_b, AW, AH, s->atlas_hue_a);
  { GLuint tt = s->atlas_hue_a; s->atlas_hue_a = s->atlas_hue_b; s->atlas_hue_b = tt; }
  { GLuint tf = s->fbo_atlas_hue_a; s->fbo_atlas_hue_a = s->fbo_atlas_hue_b; s->fbo_atlas_hue_b = tf; }
}

/* ------------------------------------------------------------------ */
/* Seed the fields with the initial per-launch blob layout              */
/* ------------------------------------------------------------------ */
static void seed_fields(State *s){
  int W = s->grid_w, H = s->grid_h;
  int AW = s->atlas_w, AH = s->atlas_h;

  /* Zero velocity and temperature (RGBA16F or RGBA8 fallback). */
  glUseProgram(s->program_zero);
  sim_bind_dst(s->fbo_v_a, W, H);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  draw_quad(s);
  glUseProgram(0);

  glUseProgram(s->program_zero);
  sim_bind_dst(s->fbo_t_a, W, H);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  draw_quad(s);
  glUseProgram(0);

  /* Seed phase from analytic blobs. We write to fbo_p_b then swap
   * so tex_p_a is "current". */
  sim_pass2(s, s->program_init_seed, s->tex_p_a, s->fbo_p_b, W, H, 0);
  { GLuint tt = s->tex_p_a; s->tex_p_a = s->tex_p_b; s->tex_p_b = tt; }
  { GLuint tf = s->fbo_p_a; s->fbo_p_a = s->fbo_p_b; s->fbo_p_b = tf; }

  /* Seed hue in the same call (init_seed branches on u_write_hue). */
  glUseProgram(s->program_init_seed);
  sim_set_common(s, s->tex_p_a);
  GLint loc_h = glGetUniformLocation(s->program_init_seed, "u_write_hue");
  if(loc_h >= 0) glUniform1f(loc_h, 1.0f);
  sim_bind_dst(s->fbo_hue, W, H);
  draw_quad(s);
  glUseProgram(0);

  /* 3D seed. */
  if(s->tier == TIER_HIGH_3D){
    glUseProgram(s->program_zero);
    sim_bind_dst(s->fbo_atlas_v_a, AW, AH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_quad(s);
    glUseProgram(0);
    glUseProgram(s->program_zero);
    sim_bind_dst(s->fbo_atlas_t_a, AW, AH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_quad(s);
    glUseProgram(0);
    sim_pass2(s, s->program_init_seed_3d, s->atlas_p_a, s->fbo_atlas_p_b, AW, AH, 0);
    { GLuint tt = s->atlas_p_a; s->atlas_p_a = s->atlas_p_b; s->atlas_p_b = tt; }
    { GLuint tf = s->fbo_atlas_p_a; s->fbo_atlas_p_a = s->fbo_atlas_p_b; s->fbo_atlas_p_b = tf; }
    glUseProgram(s->program_init_seed_3d);
    sim_set_common(s, s->atlas_p_a);
    GLint loc_h3 = glGetUniformLocation(s->program_init_seed_3d, "u_write_hue");
    if(loc_h3 >= 0) glUniform1f(loc_h3, 1.0f);
    sim_bind_dst(s->fbo_atlas_hue_b, AW, AH);
    draw_quad(s);
    glUseProgram(0);
    { GLuint tt = s->atlas_hue_a; s->atlas_hue_a = s->atlas_hue_b; s->atlas_hue_b = tt; }
    { GLuint tf = s->fbo_atlas_hue_a; s->fbo_atlas_hue_a = s->fbo_atlas_hue_b; s->fbo_atlas_hue_b = tf; }
  }
}

/* ------------------------------------------------------------------ */
/* Init                                                                   */
/* ------------------------------------------------------------------ */
static int detect_intel_uhd(void){
  const char *r = (const char *)glGetString(GL_RENDERER);
  if(!r) return 0;
  if(strstr(r, "Intel") && strstr(r, "UHD")) return 1;
  return 0;
}

static int have_ext(const char *needle){
  const char *ext = (const char *)glGetString(GL_EXTENSIONS);
  if(!ext) return 0;
  return strstr(ext, needle) != NULL;
}

static void init_magmasimplex(ModeInfo *m){
  State *s = calloc(1, sizeof *s);
  if(!s){ ncz_harness_die(1); return; }
  m->data = s;

  /* Tier selection. */
  int force_3d = (getenv("NCZ_MAGMASIMPLEX_3D") != NULL);
  int force_2d = (getenv("NCZ_MAGMASIMPLEX_2D") != NULL);
  int is_intel = detect_intel_uhd();
  if(force_3d && !force_2d) s->tier = TIER_HIGH_3D;
  else if(is_intel && !force_3d) s->tier = TIER_LOW_2D;
  else s->tier = is_intel ? TIER_LOW_2D : TIER_HIGH_3D;
  s->render_3d = (s->tier == TIER_HIGH_3D);
  s->low_perf = (s->tier == TIER_LOW_2D);

  /* Half-float RT extension check. */
  s->have_half_float_rt =
    have_ext("GL_EXT_color_buffer_half_float") ||
    have_ext("GL_EXT_color_buffer_float");
  if(!s->have_half_float_rt){
    fprintf(stderr, "[diag] magmasimplex: no half-float RT extension; "
                    "sim fields fall back to lower-precision formats.\n");
  }

  /* Load shaders. */
  char *sim_advect_v    = load_text("vendor/magmasimplex/sim_advect_v.frag");
  char *sim_advect_t    = load_text("vendor/magmasimplex/sim_advect_t.frag");
  char *sim_advect_p    = load_text("vendor/magmasimplex/sim_advect_p.frag");
  char *sim_advect_h    = load_text("vendor/magmasimplex/sim_advect_h.frag");
  char *sim_buoyancy    = load_text("vendor/magmasimplex/sim_buoyancy.frag");
  char *sim_project     = load_text("vendor/magmasimplex/sim_project.frag");
  char *sim_heat        = load_text("vendor/magmasimplex/sim_heat.frag");
  char *sim_phase       = load_text("vendor/magmasimplex/sim_phase.frag");
  char *sim_init_seed   = load_text("vendor/magmasimplex/sim_init_seed.frag");
  char *sim_zero        = load_text("vendor/magmasimplex/sim_zero.frag");
  char *render_src      = load_text("vendor/magmasimplex/render.frag");
  if(!sim_advect_v || !sim_advect_t || !sim_advect_p || !sim_advect_h ||
     !sim_buoyancy || !sim_project || !sim_heat || !sim_phase ||
     !sim_init_seed || !sim_zero || !render_src){
    free(sim_advect_v); free(sim_advect_t); free(sim_advect_p); free(sim_advect_h);
    free(sim_buoyancy); free(sim_project); free(sim_heat); free(sim_phase);
    free(sim_init_seed); free(sim_zero); free(render_src);
    ncz_harness_die(1); return;
  }

  char *sim_advect_v_3d = NULL, *sim_advect_t_3d = NULL;
  char *sim_advect_p_3d = NULL, *sim_advect_h_3d = NULL;
  char *sim_buoyancy_3d = NULL, *sim_project_3d = NULL;
  char *sim_heat_3d = NULL, *sim_phase_3d = NULL, *sim_init_seed_3d = NULL;
  if(s->tier == TIER_HIGH_3D){
    sim_advect_v_3d = load_text("vendor/magmasimplex/sim3d_advect_v.frag");
    sim_advect_t_3d = load_text("vendor/magmasimplex/sim3d_advect_t.frag");
    sim_advect_p_3d = load_text("vendor/magmasimplex/sim3d_advect_p.frag");
    sim_advect_h_3d = load_text("vendor/magmasimplex/sim3d_advect_h.frag");
    sim_buoyancy_3d = load_text("vendor/magmasimplex/sim3d_buoyancy.frag");
    sim_project_3d  = load_text("vendor/magmasimplex/sim3d_project.frag");
    sim_heat_3d     = load_text("vendor/magmasimplex/sim3d_heat.frag");
    sim_phase_3d    = load_text("vendor/magmasimplex/sim3d_phase.frag");
    sim_init_seed_3d= load_text("vendor/magmasimplex/sim3d_init_seed.frag");
    if(!sim_advect_v_3d || !sim_advect_t_3d || !sim_advect_p_3d ||
       !sim_advect_h_3d || !sim_buoyancy_3d || !sim_project_3d ||
       !sim_heat_3d || !sim_phase_3d || !sim_init_seed_3d){
      ncz_harness_die(1); return;
    }
  }

  s->program_advect_v = make_program(fullscreen_vs, sim_advect_v, "advect_v");
  s->program_advect_t = make_program(fullscreen_vs, sim_advect_t, "advect_t");
  s->program_advect_p = make_program(fullscreen_vs, sim_advect_p, "advect_p");
  s->program_advect_h = make_program(fullscreen_vs, sim_advect_h, "advect_h");
  s->program_buoyancy = make_program(fullscreen_vs, sim_buoyancy, "buoyancy");
  s->program_project  = make_program(fullscreen_vs, sim_project,  "project");
  s->program_heat     = make_program(fullscreen_vs, sim_heat,     "heat");
  s->program_phase    = make_program(fullscreen_vs, sim_phase,    "phase");
  s->program_init_seed= make_program(fullscreen_vs, sim_init_seed,"init_seed");
  s->program_zero     = make_program(fullscreen_vs, sim_zero,     "zero");
  s->program_render   = make_program(fullscreen_vs, render_src,   "render");
  free(sim_advect_v); free(sim_advect_t); free(sim_advect_p); free(sim_advect_h);
  free(sim_buoyancy); free(sim_project); free(sim_heat); free(sim_phase);
  free(sim_init_seed); free(sim_zero); free(render_src);

  if(!s->program_advect_v || !s->program_advect_t || !s->program_advect_p ||
     !s->program_advect_h || !s->program_buoyancy || !s->program_project ||
     !s->program_heat || !s->program_phase || !s->program_init_seed ||
     !s->program_zero || !s->program_render){
    ncz_harness_die(1); return;
  }
  fetch_sim_uniforms(s, s->program_advect_v);

  if(s->tier == TIER_HIGH_3D){
    s->program_advect_v_3d = make_program(fullscreen_vs, sim_advect_v_3d, "advect_v_3d");
    s->program_advect_t_3d = make_program(fullscreen_vs, sim_advect_t_3d, "advect_t_3d");
    s->program_advect_p_3d = make_program(fullscreen_vs, sim_advect_p_3d, "advect_p_3d");
    s->program_advect_h_3d = make_program(fullscreen_vs, sim_advect_h_3d, "advect_h_3d");
    s->program_buoyancy_3d = make_program(fullscreen_vs, sim_buoyancy_3d, "buoyancy_3d");
    s->program_project_3d  = make_program(fullscreen_vs, sim_project_3d,  "project_3d");
    s->program_heat_3d     = make_program(fullscreen_vs, sim_heat_3d,     "heat_3d");
    s->program_phase_3d    = make_program(fullscreen_vs, sim_phase_3d,    "phase_3d");
    s->program_init_seed_3d= make_program(fullscreen_vs, sim_init_seed_3d,"init_seed_3d");
    free(sim_advect_v_3d); free(sim_advect_t_3d); free(sim_advect_p_3d); free(sim_advect_h_3d);
    free(sim_buoyancy_3d); free(sim_project_3d); free(sim_heat_3d); free(sim_phase_3d);
    free(sim_init_seed_3d);
    if(!s->program_advect_v_3d || !s->program_advect_t_3d || !s->program_advect_p_3d ||
       !s->program_advect_h_3d || !s->program_buoyancy_3d || !s->program_project_3d ||
       !s->program_heat_3d || !s->program_phase_3d || !s->program_init_seed_3d){
      ncz_harness_die(1); return;
    }
  }

  /* Fullscreen quad. */
  const float q[] = { -1,-1, 1,-1, -1, 1, -1, 1, 1,-1, 1, 1 };
  glGenBuffers(1, &s->vbo);
  glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  /* Grid sizes. */
  s->grid_w = 256; s->grid_h = 256; s->grid_n = 64;
  {
    const char *q = getenv("NCZ_MAGMASIMPLEX_QUALITY");
    if(q){
      if(!strcasecmp(q, "low"))   { s->grid_w = s->grid_h = 192; s->grid_n = 48; }
      else if(!strcasecmp(q, "high"))   { s->grid_w = s->grid_h = 320; s->grid_n = 80; }
      else if(!strcasecmp(q, "ultra"))  { s->grid_w = s->grid_h = 384; s->grid_n = 96; }
    }
  }
  {
    int n = s->grid_n;
    int sq = (int)ceilf(sqrtf((float)n));
    s->atlas_w = n * sq;
    s->atlas_h = n * sq;
  }

  /* Allocate 2D ping-pong FBOs. */
  GLenum vf = s->have_half_float_rt ? GL_RGBA16F : GL_RGBA;
  GLenum vt = s->have_half_float_rt ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
  GLenum pf = s->have_half_float_rt ? GL_RGBA16F : GL_RGBA;
  GLenum pt = s->have_half_float_rt ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
  GLenum hf = s->have_half_float_rt ? GL_RGBA16F : GL_RGBA;
  GLenum ht = s->have_half_float_rt ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;

  make_2d_fbo_pair(&s->fbo_v_a, &s->fbo_v_b, &s->tex_v_a, &s->tex_v_b,
                   s->grid_w, s->grid_h, vf, GL_RGBA, vt);
  make_2d_fbo_pair(&s->fbo_t_a, &s->fbo_t_b, &s->tex_t_a, &s->tex_t_b,
                   s->grid_w, s->grid_h, hf, GL_RGBA, ht);
  make_2d_fbo_pair(&s->fbo_p_a, &s->fbo_p_b, &s->tex_p_a, &s->tex_p_b,
                   s->grid_w, s->grid_h, pf, GL_RGBA, pt);

  glGenFramebuffers(1, &s->fbo_hue);
  glGenTextures(1, &s->tex_hue);
  glBindTexture(GL_TEXTURE_2D, s->tex_hue);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s->grid_w, s->grid_h, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_hue);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, s->tex_hue, 0);
  check_fbo("hue");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  make_2d_fbo_pair(&s->fbo_pressure_a, &s->fbo_pressure_b,
                   &s->tex_pressure_a, &s->tex_pressure_b,
                   s->grid_w, s->grid_h, pf, GL_RGBA, pt);

  /* 3D atlas. */
  if(s->tier == TIER_HIGH_3D){
    GLenum af = s->have_half_float_rt ? GL_RGBA16F : GL_RGBA;
    GLenum at_ = s->have_half_float_rt ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
    make_atlas_fbo_pair(&s->fbo_atlas_v_a, &s->fbo_atlas_v_b,
                        &s->atlas_v_a, &s->atlas_v_b,
                        s->atlas_w, s->atlas_h, af, GL_RGBA, at_);
    make_atlas_fbo_pair(&s->fbo_atlas_t_a, &s->fbo_atlas_t_b,
                        &s->atlas_t_a, &s->atlas_t_b,
                        s->atlas_w, s->atlas_h, af, GL_RGBA, at_);
    make_atlas_fbo_pair(&s->fbo_atlas_p_a, &s->fbo_atlas_p_b,
                        &s->atlas_p_a, &s->atlas_p_b,
                        s->atlas_w, s->atlas_h, af, GL_RGBA, at_);
    make_atlas_fbo_pair(&s->fbo_atlas_hue_a, &s->fbo_atlas_hue_b,
                        &s->atlas_hue_a, &s->atlas_hue_b,
                        s->atlas_w, s->atlas_h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    make_atlas_fbo_pair(&s->fbo_atlas_pr_a, &s->fbo_atlas_pr_b,
                        &s->atlas_pr_a, &s->atlas_pr_b,
                        s->atlas_w, s->atlas_h, af, GL_RGBA, at_);
  }

  /* Render uniform locations. */
  s->u_render_seed        = glGetUniformLocation(s->program_render, "u_seed");
  s->u_render_time        = glGetUniformLocation(s->program_render, "u_time");
  s->u_render_resolution  = glGetUniformLocation(s->program_render, "u_resolution");
  s->u_render_liquid      = glGetUniformLocation(s->program_render, "u_liquid");
  s->u_render_wax         = glGetUniformLocation(s->program_render, "u_wax");
  s->u_render_secondary   = glGetUniformLocation(s->program_render, "u_secondary");
  s->u_render_emitter     = glGetUniformLocation(s->program_render, "u_emitter");
  s->u_render_hotness     = glGetUniformLocation(s->program_render, "u_hotness");
  s->u_render_tier        = glGetUniformLocation(s->program_render, "u_tier_3d");
  s->u_render_tex_phase   = glGetUniformLocation(s->program_render, "u_tex_phase");
  s->u_render_tex_hue     = glGetUniformLocation(s->program_render, "u_tex_hue");
  s->u_render_tex_temp    = glGetUniformLocation(s->program_render, "u_tex_temp");

  /* Per-launch randomisation. */
  uint32_t z = seed_rng();
  s->seed = z;
  int way = rndi(&z, 0, COLOURWAY_COUNT - 1);
  s->way_idx = way;
  s->way_name = colourway_names[way];
  const Way *wp = &ways[way];
  s->liquid_r = wp->lr; s->liquid_g = wp->lg; s->liquid_b = wp->lb;
  s->wax_r = wp->wr; s->wax_g = wp->wg; s->wax_b = wp->wb;
  s->secondary_r = wp->sr; s->secondary_g = wp->sg; s->secondary_b = wp->sb;
  s->clear_liquid = wp->clear;

  s->emitter_x = 0.0f;
  s->emitter_y = -0.85f;
  s->emitter_r = 0.55f;
  s->emitter_strength = 1.8f + rnd(&z, -0.3f, 0.6f);
  s->amb_cool        = 0.04f + rnd(&z, -0.01f, 0.03f);
  s->buoyancy_alpha  = 1.6f + rnd(&z, -0.4f, 0.8f);
  s->surface_tension = rnd(&z, 0.0008f, 0.0040f);
  s->viscosity       = rnd(&z, 0.0001f, 0.0009f);
  s->scale           = rnd(&z, 0.95f, 1.30f);
  s->hotness         = rnd(&z, 0.85f, 1.55f);

  int rainbow = (way >= 25 && way <= 29) ? 1 :
                (rnd(&z, 0.0f, 1.0f) < 0.60f ? 1 : 0);
  s->rainbow = rainbow;

  for(int i = 0; i < MAX_BLOBS; i++){
    float a = (float)i * (2.0f * (float)M_PI / 6.0f)
            + rnd(&z, 0.0f, 0.5f);
    float r;
    if(rnd(&z, 0.0f, 1.0f) < 0.35f){
      r = 0.50f + 0.40f * rnd(&z, 0.0f, 1.0f);
    } else if(rnd(&z, 0.0f, 1.0f) < 0.70f){
      r = 0.85f + 0.45f * rnd(&z, 0.0f, 1.0f);
    } else {
      r = 1.20f + 0.50f * rnd(&z, 0.0f, 1.0f);
    }
    s->bx[i] = cosf(a) * r;
    s->bz[i] = sinf(a) * r;
    s->by[i] = rnd(&z, 0.20f, 1.05f);
    if(rnd(&z, 0.0f, 1.0f) < 0.30f)
      s->by[i] = rnd(&z, -0.80f, -0.10f);
    if(rainbow){
      s->bhue[i] = (float)i / 6.0f + 0.04f * rnd(&z, -1.0f, 1.0f);
    } else {
      s->bhue[i] = rnd(&z, 0.0f, 1.0f);
    }
    if(s->bhue[i] < 0.0f) s->bhue[i] = 0.0f;
    if(s->bhue[i] > 1.0f) s->bhue[i] = 1.0f;
    s->br[i] = (i < 3) ? rnd(&z, 0.18f, 0.26f) : rnd(&z, 0.10f, 0.16f);
  }

  seed_fields(s);
  s->started = now();
  s->sim_tick = 0;

  fprintf(stderr,
   "[diag] magmasimplex seed=%u way=%d (%s) tier=%s "
   "grid=%dx%d (3d atlas %dx%d) "
   "surface_tension=%.5f visc=%.5f buoy=%.3f emitter=(%.2f,%.2f,r=%.2f,str=%.2f) "
   "amb_cool=%.3f hotness=%.3f scale=%.3f rainbow=%d clear_liquid=%d "
   "half_float_rt=%d GL=%s\n",
   s->seed, s->way_idx, s->way_name,
   s->tier == TIER_HIGH_3D ? "3d-voxel" : "2d-field",
   s->grid_w, s->grid_h, s->atlas_w, s->atlas_h,
   s->surface_tension, s->viscosity, s->buoyancy_alpha,
   s->emitter_x, s->emitter_y, s->emitter_r, s->emitter_strength,
   s->amb_cool, s->hotness, s->scale, s->rainbow, s->clear_liquid,
   s->have_half_float_rt,
   glGetString(GL_VERSION));

  fprintf(stderr, "[diag] magmasimplex blobs:");
  for(int i = 0; i < 6; i++)
    fprintf(stderr, " b%d=(%.3f,%.3f,%.3f,r=%.3f,hue=%.3f)", i,
            s->bx[i], s->by[i], s->bz[i], s->br[i], s->bhue[i]);
  fprintf(stderr, "\n");
}

/* ------------------------------------------------------------------ */
/* Draw                                                                   */
/* ------------------------------------------------------------------ */
static void draw_magmasimplex(ModeInfo *m){
  State *s = m->data;
  if(!s || !s->program_render) return;

  int w = m->xgwa.width, h = m->xgwa.height;
  if(w < 1) w = 1;
  if(h < 1) h = 1;

  /* Step the simulation. Two substeps per render frame so the
   * lava-lamp cycle emerges at modest frame rates. */
  int substeps = 2;
  for(int sub = 0; sub < substeps; sub++){
    if(s->tier == TIER_HIGH_3D) step_3d(s);
    else                        step_2d(s);
  }
  s->sim_tick += substeps;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(s->program_render);

  float t = (float)(now() - s->started);
  if(s->u_render_seed >= 0)        glUniform1f(s->u_render_seed, (float)s->seed * 1e-5f);
  if(s->u_render_time >= 0)        glUniform1f(s->u_render_time, t);
  if(s->u_render_resolution >= 0)  glUniform2f(s->u_render_resolution, (float)w, (float)h);
  if(s->u_render_liquid >= 0)      glUniform3f(s->u_render_liquid, s->liquid_r, s->liquid_g, s->liquid_b);
  if(s->u_render_wax >= 0)         glUniform3f(s->u_render_wax, s->wax_r, s->wax_g, s->wax_b);
  if(s->u_render_secondary >= 0)   glUniform3f(s->u_render_secondary, s->secondary_r, s->secondary_g, s->secondary_b);
  if(s->u_render_emitter >= 0)     glUniform2f(s->u_render_emitter, s->emitter_x, s->emitter_y);
  if(s->u_render_hotness >= 0)     glUniform1f(s->u_render_hotness, s->hotness);
  if(s->u_render_tier >= 0)        glUniform1f(s->u_render_tier, s->render_3d ? 1.0f : 0.0f);

  glActiveTexture(GL_TEXTURE0);
  if(s->render_3d) glBindTexture(GL_TEXTURE_2D, s->atlas_p_a);
  else             glBindTexture(GL_TEXTURE_2D, s->tex_p_a);
  if(s->u_render_tex_phase >= 0) glUniform1i(s->u_render_tex_phase, 0);

  glActiveTexture(GL_TEXTURE1);
  if(s->render_3d) glBindTexture(GL_TEXTURE_2D, s->atlas_hue_a);
  else             glBindTexture(GL_TEXTURE_2D, s->tex_hue);
  if(s->u_render_tex_hue >= 0) glUniform1i(s->u_render_tex_hue, 1);

  glActiveTexture(GL_TEXTURE2);
  if(s->render_3d) glBindTexture(GL_TEXTURE_2D, s->atlas_t_a);
  else             glBindTexture(GL_TEXTURE_2D, s->tex_t_a);
  if(s->u_render_tex_temp >= 0) glUniform1i(s->u_render_tex_temp, 2);

  if(s->render_3d){
    int sq = (int)ceilf(sqrtf((float)s->grid_n));
    GLint loc_aw = glGetUniformLocation(s->program_render, "u_atlas_size");
    GLint loc_an = glGetUniformLocation(s->program_render, "u_atlas_slices");
    GLint loc_gn = glGetUniformLocation(s->program_render, "u_grid_n");
    if(loc_aw >= 0) glUniform2f(loc_aw, (float)s->atlas_w, (float)s->atlas_h);
    if(loc_an >= 0) glUniform1f(loc_an, (float)sq);
    if(loc_gn >= 0) glUniform1f(loc_gn, (float)s->grid_n);
  }

  draw_quad(s);
  glUseProgram(0);

  /* Per-vendor frame-time measurement. */
  static unsigned long _ft_counter;
  static double _ft_prev;
  static int _ft_enabled;
  if(!_ft_enabled) _ft_enabled = (getenv("NCZ_MAGMASIMPLEX_PERF_LOG") != NULL);
  if(_ft_enabled){
    double _t_now = now();
    if((++_ft_counter % 30) == 0){
      if(_ft_prev > 0.0){
        double _dt = (_t_now - _ft_prev) / 30.0;
        fprintf(stderr, "[diag] magmasimplex frame_t frame=%lu dt_ms=%.3f tier=%s\n",
                _ft_counter, _dt * 1000.0,
                s->tier == TIER_HIGH_3D ? "3d" : "2d");
        fflush(stderr);
      }
      _ft_prev = now();
    }
  }

  /* First-frame sanity samples. */
  static int once;
  if(!once++){
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

/* ------------------------------------------------------------------ */
/* Teardown                                                               */
/* ------------------------------------------------------------------ */
static void destroy_fbo_pair(GLuint *fbo_a, GLuint *fbo_b, GLuint *tex_a, GLuint *tex_b){
  if(*fbo_a) glDeleteFramebuffers(1, fbo_a);
  if(*fbo_b) glDeleteFramebuffers(1, fbo_b);
  if(*tex_a) glDeleteTextures(1, tex_a);
  if(*tex_b) glDeleteTextures(1, tex_b);
  *fbo_a = *fbo_b = 0; *tex_a = *tex_b = 0;
}
static void destroy_program(GLuint *p){ if(*p) glDeleteProgram(*p); *p = 0; }

static void free_magmasimplex(ModeInfo *m){
  State *s = m->data;
  if(!s) return;
  destroy_fbo_pair(&s->fbo_v_a, &s->fbo_v_b, &s->tex_v_a, &s->tex_v_b);
  destroy_fbo_pair(&s->fbo_t_a, &s->fbo_t_b, &s->tex_t_a, &s->tex_t_b);
  destroy_fbo_pair(&s->fbo_p_a, &s->fbo_p_b, &s->tex_p_a, &s->tex_p_b);
  destroy_fbo_pair(&s->fbo_pressure_a, &s->fbo_pressure_b, &s->tex_pressure_a, &s->tex_pressure_b);
  if(s->fbo_hue){ glDeleteFramebuffers(1, &s->fbo_hue); s->fbo_hue = 0; }
  if(s->tex_hue){ glDeleteTextures(1, &s->tex_hue); s->tex_hue = 0; }
  destroy_fbo_pair(&s->fbo_atlas_v_a, &s->fbo_atlas_v_b, &s->atlas_v_a, &s->atlas_v_b);
  destroy_fbo_pair(&s->fbo_atlas_t_a, &s->fbo_atlas_t_b, &s->atlas_t_a, &s->atlas_t_b);
  destroy_fbo_pair(&s->fbo_atlas_p_a, &s->fbo_atlas_p_b, &s->atlas_p_a, &s->atlas_p_b);
  destroy_fbo_pair(&s->fbo_atlas_hue_a, &s->fbo_atlas_hue_b, &s->atlas_hue_a, &s->atlas_hue_b);
  destroy_fbo_pair(&s->fbo_atlas_pr_a, &s->fbo_atlas_pr_b, &s->atlas_pr_a, &s->atlas_pr_b);
  destroy_program(&s->program_render);
  destroy_program(&s->program_advect_v);
  destroy_program(&s->program_advect_t);
  destroy_program(&s->program_advect_p);
  destroy_program(&s->program_advect_h);
  destroy_program(&s->program_buoyancy);
  destroy_program(&s->program_project);
  destroy_program(&s->program_heat);
  destroy_program(&s->program_phase);
  destroy_program(&s->program_init_seed);
  destroy_program(&s->program_zero);
  destroy_program(&s->program_advect_v_3d);
  destroy_program(&s->program_advect_t_3d);
  destroy_program(&s->program_advect_p_3d);
  destroy_program(&s->program_advect_h_3d);
  destroy_program(&s->program_buoyancy_3d);
  destroy_program(&s->program_project_3d);
  destroy_program(&s->program_heat_3d);
  destroy_program(&s->program_phase_3d);
  destroy_program(&s->program_init_seed_3d);
  if(s->vbo) glDeleteBuffers(1, &s->vbo);
  free(s);
  m->data = NULL;
}

static void reshape_magmasimplex(ModeInfo *m, int w, int h){ (void)m; (void)w; (void)h; }
static Bool magmasimplex_handle_event(ModeInfo *m, XEvent *e){ (void)m; (void)e; return False; }
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