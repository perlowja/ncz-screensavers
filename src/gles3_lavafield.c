/* gles3_lavafield.c — GLES3-native fullscreen raymarched metaball lava
 *                     field. First piece on the new engine.
 *
 * Mirrors the proven pattern of src/gles3_blackhole.c: one .c file
 * driven by a single fragment shader in vendor/lavafield/lavafield.frag,
 * loadable from the source tree (cwd or two levels up) or from
 * /usr/share/ncz-screensavers/shaders/lavafield.frag.
 *
 * Algorithm: a small number of buoyant blobs (4..6) follow slow,
 *   vertically-coupled buoyancy waves and detach / recombine as their
 *   smooth-min SDF in the shader merges. The slab is raymarched
 *   straight-on, with a Reinhard tonemap + gamma + per-launch contrast
 *   toe on the back end.
 *
 * Per-launch randomisation (palette, palette drift phase/rate, blob
 * count, viscosity, scale, warmth, contrast) seeded via /dev/urandom
 * and printed to stderr in the established [diag] format.
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
/* gcc 14+ makes implicit declarations an error under -std=c11 even though
 * both headers above declare this; explicit forward decl avoids the
 * regression if the header order ever changes. */
#ifdef NCZ_GLES3_BUILD
extern void ncz_harness_die(int code);
#endif

typedef struct {
  GLuint program, vbo;
  GLint u_time, u_seed, u_resolution;
  GLint u_palette, u_palette_phase, u_palette_rate, u_palette_contrast;
  GLint u_blob_count, u_viscosity, u_scale, u_warmth;
  GLint u_blob0, u_blob1, u_blob2, u_blob3, u_blob4, u_blob5;
  double started;
  /* Per-launch constants: 0:seed 1:palette 2:palette_phase 3:palette_rate
   * 4:blob_count 5:viscosity 6:scale 7:warmth 8:contrast */
  float v[9];
  /* Per-blob base positions and radii (xz in [-1.0, 1.0], y in [-1.3, 1.3],
   * radius in [0.18, 0.42]). Slow wave animation on the GPU uses these
   * as anchors; per-blob phase offsets in phases[6]. */
  float bx[6], by[6], bz[6], br[6], bphase[6];
} State;

static double now(void){
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static uint32_t seed_rng(void){
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

/* Shader lookup order — first match wins. The installed-shader absolute
 * path is mandatory per the operator's "shader install is mandatory"
 * note, since the cwd-relative paths can fail when launched outside
 * the build tree. */
static char *load_shader(void){
  const char *p[] = {
    "vendor/lavafield/lavafield.frag",
    "../vendor/lavafield/lavafield.frag",
    "../../vendor/lavafield/lavafield.frag",
    "/usr/share/ncz-screensavers/shaders/lavafield.frag"
  };
  FILE *f = NULL; const char *u = NULL;
  for(unsigned i = 0; i < 4; i++)
    if((f = fopen(p[i], "rb"))){ u = p[i]; break; }
  if(!f){
    fprintf(stderr, "lavafield: cannot locate lavafield.frag\n");
    return NULL;
  }
  fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  char *b = malloc((size_t)n + 1);
  if(!b || fread(b, 1, (size_t)n, f) != (size_t)n){
    free(b); fclose(f); return NULL;
  }
  fclose(f); b[n] = 0;
  fprintf(stderr, "[diag] lavafield shader=%s\n", u);
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
    fprintf(stderr, "lavafield compile: %s\n", l);
    glDeleteShader(s); return 0;
  }
  return s;
}

static void init_lavafield(ModeInfo *m){
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
    fprintf(stderr, "lavafield link: %s\n", l);
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
  L(palette); L(palette_phase); L(palette_rate); L(palette_contrast);
  L(blob_count); L(viscosity); L(scale); L(warmth);
  L(blob0); L(blob1); L(blob2); L(blob3); L(blob4); L(blob5);
#undef L

  /* Per-launch constants. */
  uint32_t z = seed_rng();
  s->v[0] = (float)z;
  s->v[1] = (float)(z & 0xffu) / 255.0f * 5.0f;        /* palette 0..5 */
  s->v[1] = (float)((int)s->v[1] % 5);                  /* 0..4 */
  s->v[2] = rnd(&z, 0.0f, 1.0f);                        /* palette_phase */
  s->v[3] = rnd(&z, 0.012f, 0.028f);                    /* palette_rate */
  s->v[4] = (float)(4 + (int)(rnd(&z, 0.0f, 1.0f) * 3.0f)); /* blob_count 4..6 */
  s->v[5] = rnd(&z, 0.7f, 1.4f);                        /* viscosity */
  s->v[6] = rnd(&z, 0.85f, 1.25f);                      /* scale */
  s->v[7] = rnd(&z, 0.7f, 1.45f);                       /* warmth */
  s->v[8] = rnd(&z, 0.95f, 1.25f);                      /* contrast */

  /* Blob positions and radii: spread them across the visible slab so
   * the camera's straight-down ray sees most of them. With ro=(0,0,2.6)
   * and rays of the form normalize(uv, -1), a central ray can only
   * hit blobs whose xz distance from the origin is less than the blob
   * radius. We use a tight ring (0.20..0.50) so most blobs sit under
   * the central view, with a generous radius (0.45..0.70) so the SDF
   * blend produces large overlapping forms. Larger radii also mean
   * the smooth-min blends cover more screen area when blobs touch,
   * giving the "field fills the frame" look. */
  for(int i = 0; i < 6; i++){
    float a = (float)i * (2.0f * (float)M_PI / 6.0f)
            + rnd(&z, 0.0f, 0.6f);
    s->bx[i] = cosf(a) * (0.20f + 0.30f * rnd(&z, 0.0f, 1.0f));
    s->bz[i] = sinf(a) * (0.20f + 0.30f * rnd(&z, 0.0f, 1.0f));
    /* Spread the y anchors across the slab so blobs are not all on the
     * same horizontal plane at t=0 — some rise, some sink, with phase
     * offsets to break synchronisation. */
    s->by[i] = rnd(&z, -0.85f, 0.85f);
    s->br[i] = rnd(&z, 0.45f, 0.70f);
    s->bphase[i] = rnd(&z, 0.0f, (float)(2.0 * M_PI));
  }

  fprintf(stderr,
   "[diag] lavafield seed=%.0f palette=%d palette_phase=%.4f palette_rate=%.5f "
   "palette_contrast=%.3f blob_count=%d viscosity=%.3f scale=%.3f warmth=%.3f "
   "GL=%s\n",
   s->v[0], (int)s->v[1], s->v[2], s->v[3], s->v[8], (int)s->v[4],
   s->v[5], s->v[6], s->v[7], glGetString(GL_VERSION));

  /* Blob layout, second diag line — helps downstream compare two
   * launches. */
  fprintf(stderr, "[diag] lavafield blobs:");
  for(int i = 0; i < (int)s->v[4]; i++)
    fprintf(stderr, " b%d=(%.3f,%.3f,%.3f,r=%.3f)", i,
            s->bx[i], s->by[i], s->bz[i], s->br[i]);
  fprintf(stderr, "\n");

  s->started = now();
}

static void draw_lavafield(ModeInfo *m){
  State *s = m->data;
  if(!s || !s->program) return;

  int w = m->xgwa.width, h = m->xgwa.height;
  if(w < 1) w = 1; if(h < 1) h = 1;
  glViewport(0, 0, w, h);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(s->program);

  float t = (float)(now() - s->started);

  glUniform1f(s->u_time,       t);
  glUniform1f(s->u_seed,       s->v[0]);
  glUniform2f(s->u_resolution, (float)w, (float)h);
  glUniform1f(s->u_palette,        s->v[1]);
  glUniform1f(s->u_palette_phase,  s->v[2]);
  glUniform1f(s->u_palette_rate,   s->v[3]);
  glUniform1f(s->u_palette_contrast, s->v[8]);
  glUniform1f(s->u_blob_count, s->v[4]);
  glUniform1f(s->u_viscosity,  s->v[5]);
  glUniform1f(s->u_scale,      s->v[6]);
  glUniform1f(s->u_warmth,     s->v[7]);

  /* Animate blob positions: a global buoyancy wave plus per-blob phase
   * offset, slowed by u_viscosity. Vertical range stays inside
   * roughly [-1.0, 1.0] in y (matches the new CPU-side anchor range).
   * xz position also wobbles a touch to break the perfect ring. */
  float visc = s->v[5];
  float blob_data[6][4];
  int n = (int)s->v[4];
  for(int i = 0; i < 6; i++){
    if(i < n){
      float ph = s->bphase[i];
      float y_anchor = s->by[i];
      /* Global slow wave: everyone rises and falls together on a long
       * timescale; per-blob phase modulates amplitude so they do not
       * lockstep. */
      float global = 0.45f * sinf(t * (0.42f / visc) + ph * 0.7f);
      float per_blob = 0.30f * sinf(t * (0.78f / visc) + ph);
      float y = y_anchor + global + per_blob;
      /* Clamp so blobs do not drift out of view. */
      if(y >  1.00f) y =  1.00f;
      if(y < -1.00f) y = -1.00f;
      float x = s->bx[i] + 0.10f * sinf(t * (0.55f / visc) + ph);
      float z = s->bz[i] + 0.10f * cosf(t * (0.61f / visc) + ph);
      blob_data[i][0] = x;
      blob_data[i][1] = y;
      blob_data[i][2] = z;
      blob_data[i][3] = s->br[i];
    } else {
      /* Inactive slot — push offscreen so even if the shader reads it
       * it has no effect. */
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

  glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  glDisable(GL_DEPTH_TEST);
  ncz_gles3_draw_arrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);

  /* Per-vendor frame-time measurement. Same gating as blackhole: only
   * when NCZ_LAVAFIELD_PERF_LOG is set, to keep a normal run cheap on
   * Intel. Tagged [frame_t] for downstream parsing. */
  static unsigned long _ft_counter;
  static double _ft_prev;
  static int _ft_enabled;
  if(!_ft_enabled) _ft_enabled = (getenv("NCZ_LAVAFIELD_PERF_LOG") != NULL);
  if(_ft_enabled){
    double _t_now = now();
    if((++_ft_counter % 30) == 0){
      if(_ft_prev > 0.0){
        double _dt = (_t_now - _ft_prev) / 30.0;
        fprintf(stderr, "[diag] lavafield frame_t frame=%lu dt_ms=%.3f\n",
                _ft_counter, _dt * 1000.0);
        fflush(stderr);
      }
      _ft_prev = now();
    }
  }

  /* First-frame sanity samples. The point of these is to prove the
   * pipeline rendered something more than 96% black: the centre sample
   * should be warm/red-ish on a classic palette. */
  static int once;
  if(!once++){
    unsigned char px[16] = {0};
    glReadPixels(w/2, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glReadPixels(w/4, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+4);
    glReadPixels(3*w/4, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+8);
    glReadPixels(w/2, h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+12);
    GLenum e = glGetError();
    fprintf(stderr,
     "[diag] lavafield first draw gl_error=0x%x samples="
     "ctr=%u,%u,%u; l=%u,%u,%u; r=%u,%u,%u; top=%u,%u,%u "
     "uniforms=time:%d res:%d seed:%d palette:%d blobs:%d\n",
     e, px[0], px[1], px[2], px[4], px[5], px[6], px[8], px[9], px[10],
     px[12], px[13], px[14],
     s->u_time, s->u_resolution, s->u_seed, s->u_palette, s->u_blob_count);
    once = 1;
  }
}

static void free_lavafield(ModeInfo *m){
  State *s = m->data;
  if(!s) return;
  if(s->vbo) glDeleteBuffers(1, &s->vbo);
  if(s->program) glDeleteProgram(s->program);
  free(s);
  m->data = NULL;
}

static void reshape_lavafield(ModeInfo *m, int w, int h){
  (void)m; (void)w; (void)h;
}

static Bool lavafield_handle_event(ModeInfo *m, XEvent *e){
  (void)m; (void)e;
  return False;
}

static void release_lavafield(ModeInfo *m){ (void)m; }

static ModeSpecOpt lavafield_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table lavafield_xscreensaver_function_table = {
  .name = "lavafield",
  .class_ = "Lavafield",
  .init_cb = init_lavafield,
  .draw_cb = draw_lavafield,
  .reshape_cb = reshape_lavafield,
  .event_cb = lavafield_handle_event,
  .free_cb = free_lavafield,
  .release_cb = release_lavafield,
  .opts = &lavafield_opts,
  .defaults_str = DEFAULTS
};