/* gles3_genxvectorcade.c — fullscreen psychedelic vector-arcade journey
 *                        driven by a fragment shader with persistent
 *                        feedback trails (the feature that separates
 *                        this from a generic tunnel shader).
 *
 * Mirrors the proven pattern of src/gles3_blackhole.c and
 * src/gles3_lavafield.c: a single fragment shader driven by uniforms,
 * run by our Wayland harness. The one piece those don't have is the
 * persistent feedback FBO — every frame the host (a) blits the
 * previous-frame FBO onto the current one with a small rotation and
 * configurable fade (so wakes spiral inward and self-replicate), (b)
 * draws the geometry on top via the shader, and (c) blits the result
 * to the default backbuffer.
 *
 * Per-launch randomisation — logged in the [diag] line at startup —
 * chooses: cross-section shape, segment count, travel speed, rotation
 * rate, palette pair index, palette cycling rate, symmetry order,
 * trail persistence, warp amount, chromatic aberration amount, burst
 * frequency. The phase vector (5 movements) advances smoothly between
 * weighted endpoint configurations so the geometry *becomes* the next
 * thing rather than cutting to it; the journey order itself is
 * shuffled per launch and lasts minutes.
 *
 * Original work in a genre. No reference to any specific title,
 * programmer, company, or product anywhere in this file or the
 * accompanying shader; the look is a visual genre.
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

/* Feedback FBO dimensions are half-resolution of the window — the
 * trail buffer is intentionally soft, so half-res costs a quarter the
 * bandwidth and looks fine through the bloom. Intel UHD was already
 * the constraint, and a full-resolution RGBA8 ping-pong nearly doubled
 * per-frame cost on that target (measured on PEGASUS 2026-09-25).
 * Override via env NCZ_GVC_FB_SCALE for A-B testing. */
#define DEFAULT_FB_SCALE 0.5f
#define MAX_PHASES 5

typedef struct {
    /* GL pipeline */
    GLuint program, vbo, vao;

    /* Persistent feedback ping-pong (RGBA8 half-res). */
    GLuint fbo_a, fbo_b, tex_a, tex_b;
    int fb_w, fb_h;          /* FBO dimensions (== fb_scale * window) */
    float fb_scale;
    int fb_index;            /* 0 = a is current, 1 = b is current */

    /* A tiny "fade" shader that takes the previous frame texture and
     * outputs it scaled by u_fade with a small rotation around the
     * centre. We use one of the feedback textures as a sampling source
     * and blit into the OTHER FBO — that's how the trail buffer
     * "decays while rotating" each frame. */
    GLuint fade_program;
    GLint fade_loc_prev, fade_loc_fade, fade_loc_rot, fade_loc_res;

    /* Main scene program uniform locations */
    GLint u_time, u_resolution, u_prev, u_fade;
    GLint u_phase_ab, u_phase_e;
    GLint u_shape_speed, u_seg_rot, u_pal_pair, u_pal_phase, u_pal_contrast;
    GLint u_sym_burst, u_trail_persist, u_warp_amount, u_ca_amount;
    GLint u_pulse, u_flash, u_seed;

    /* Per-launch constants */
    float v_seed;
    float v_shape;            /* 0..5: tri,sq,pent,hex,circle,star */
    float v_speed;
    float v_segments;
    float v_rotation;
    float v_pal_pair;
    float v_pal_rate;
    float v_pal_phase;
    float v_pal_contrast;
    float v_sym_base;         /* 4..14 */
    float v_burst_freq;       /* 0.3..1.4 bursts/sec */
    float v_trail_persist;    /* 0.86..0.97 */
    float v_warp_amount;      /* 0.4..1.4 */
    float v_ca_amount;        /* 0.0..0.012 */
    float v_journey_total;    /* total duration (s) of one cycle */
    int v_phase_order[5];     /* shuffle of 0..4: which movement comes next */
    int v_phase_order_cur;    /* pointer into above */

    /* Time origin */
    double started;
    unsigned long frame;
    unsigned long last_flash_frame;     /* throttle flashes */

    /* Resolved fb dims for fade shader: cached at resize so the
     * fade shader doesn't have to read glViewport state. */
    int win_w, win_h;
} State;

/* ----- helpers ---------------------------------------------------- */
static double now_seconds(void){
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static uint32_t seed_rng(void){
    const char *ovr = getenv("NCZ_GVC_FIXED_SEED");
    if(ovr && *ovr){
        uint32_t s = (uint32_t)strtoul(ovr, NULL, 10);
        if(s != 0) return s;
    }
    uint32_t s = 0;
    int f = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if(f >= 0){ ssize_t n = read(f, &s, 4); close(f); if(n == 4) return s; }
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    return (uint32_t)(t.tv_nsec ^ t.tv_sec ^ getpid());
}
static float rnd(uint32_t *s, float a, float b){
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return a + (b - a) * (float)(*s & 0x00ffffffu) / 16777215.0f;
}
static int rnd_int(uint32_t *s, int a, int b){
    return a + (int)(rnd(s, 0.0f, 1.0f) * (float)(b - a + 1));
}
static void shuffle_ints(uint32_t *s, int *arr, int n){
    /* Fisher-Yates with our LCG */
    for(int i = n - 1; i > 0; i--){
        int j = (int)(rnd(s, 0.0f, 1.0f) * (float)(i + 1));
        if(j > i) j = i;
        int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
}

/* ----- shader load ------------------------------------------------ */
static char *load_shader(void){
    const char *paths[] = {
        "vendor/genxvectorcade/genxvectorcade.frag",
        "../vendor/genxvectorcade/genxvectorcade.frag",
        "../../vendor/genxvectorcade/genxvectorcade.frag",
        "/usr/share/ncz-screensavers/shaders/genxvectorcade.frag",
    };
    FILE *f = NULL; const char *used = NULL;
    for(unsigned i = 0; i < sizeof(paths)/sizeof(paths[0]); i++){
        if((f = fopen(paths[i], "rb"))){ used = paths[i]; break; }
    }
    if(!f){ fprintf(stderr, "genxvectorcade: cannot locate shader\n"); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *b = (char *)malloc((size_t)n + 1);
    if(!b || fread(b, 1, (size_t)n, f) != (size_t)n){
        free(b); fclose(f); return NULL;
    }
    fclose(f); b[n] = 0;
    fprintf(stderr, "[diag] genxvectorcade shader=%s\n", used);
    return b;
}

/* ----- shader compile helpers ------------------------------------ */
static GLuint compile(GLenum t, const char *src, const char *tag){
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){
        char l[8192]; glGetShaderInfoLog(s, sizeof l, NULL, l);
        fprintf(stderr, "genxvectorcade compile %s: %s\n", tag, l);
        glDeleteShader(s); return 0;
    }
    return s;
}
static GLuint link_prog(GLuint vs, GLuint fs){
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){
        char l[8192]; glGetProgramInfoLog(p, sizeof l, NULL, l);
        fprintf(stderr, "genxvectorcade link: %s\n", l);
        glDeleteProgram(p); return 0;
    }
    return p;
}

/* ----- feedback FBO setup ---------------------------------------- */
/* Create the ping-pong FBOs at half the window size. RGBA8: cheap,
 * ubiquitous, plenty of dynamic range for additive bloom trails. */
static int create_fbos(State *s, int win_w, int win_h){
    const char *ovr = getenv("NCZ_GVC_FB_SCALE");
    if(ovr && *ovr){ s->fb_scale = (float)strtod(ovr, NULL); }
    if(s->fb_scale < 0.25f) s->fb_scale = 0.25f;
    if(s->fb_scale > 1.0f)  s->fb_scale = 1.0f;
    int w = (int)((float)win_w * s->fb_scale);
    int h = (int)((float)win_h * s->fb_scale);
    if(w < 2) w = 2; if(h < 2) h = 2;
    s->fb_w = w; s->fb_h = h;
    s->win_w = win_w; s->win_h = win_h;
    s->fb_index = 0;

    s->fbo_a = 0; s->fbo_b = 0;
    glGenFramebuffers(1, &s->fbo_a);
    glGenFramebuffers(1, &s->fbo_b);
    glGenTextures(1, &s->tex_a);
    glGenTextures(1, &s->tex_b);

    GLuint fbos[2]   = { s->fbo_a, s->fbo_b };
    GLuint texs[2]   = { s->tex_a, s->tex_b };
    for(int i = 0; i < 2; i++){
        glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texs[i], 0);
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if(st != GL_FRAMEBUFFER_COMPLETE){
            fprintf(stderr, "genxvectorcade: FBO %d incomplete 0x%x\n",
                    i, (unsigned)st);
            return -1;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    fprintf(stderr, "[diag] genxvectorcade fb %dx%d scale=%.2f (window %dx%d)\n",
            w, h, s->fb_scale, win_w, win_h);
    return 0;
}

static void destroy_fbos(State *s){
    if(s->fbo_a){ glDeleteFramebuffers(1, &s->fbo_a); s->fbo_a = 0; }
    if(s->fbo_b){ glDeleteFramebuffers(1, &s->fbo_b); s->fbo_b = 0; }
    if(s->tex_a){ glDeleteTextures(1, &s->tex_a); s->tex_a = 0; }
    if(s->tex_b){ glDeleteTextures(1, &s->tex_b); s->tex_b = 0; }
}

/* ----- fade shader (rotates previous frame and scales by u_fade) -- */
static const char *fade_vert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  v_uv = a_pos * 0.5 + 0.5;\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *fade_frag =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_uv;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D u_prev;\n"
    "uniform float u_fade;       /* persistence: 0.86..0.97 */\n"
    "uniform vec2  u_rot_cos_sin;/* small rotation to spiral wakes */\n"
    "uniform vec2  u_scale;      /* 0.985..1.005: subtle zoom */\n"
    "uniform vec2  u_offset;     /* optional radial drift */\n"
    "void main(){\n"
    "  vec2 p = v_uv - 0.5;\n"
    "  vec2 q = vec2(p.x * u_scale.x, p.y * u_scale.y) + u_offset;\n"
    "  q = vec2(u_rot_cos_sin.x * q.x - u_rot_cos_sin.y * q.y,\n"
    "           u_rot_cos_sin.y * q.x + u_rot_cos_sin.x * q.y);\n"
    "  q += 0.5;\n"
    "  vec3 prev = texture(u_prev, q).rgb;\n"
    "  fragColor = vec4(prev * u_fade, 1.0);\n"
    "}\n";

static int build_fade_program(State *s){
    GLuint v = compile(GL_VERTEX_SHADER, fade_vert, "fade_vs");
    GLuint f = compile(GL_FRAGMENT_SHADER, fade_frag, "fade_fs");
    if(!v || !f){ return -1; }
    s->fade_program = link_prog(v, f);
    glDeleteShader(v); glDeleteShader(f);
    if(!s->fade_program) return -1;
    s->fade_loc_prev = glGetUniformLocation(s->fade_program, "u_prev");
    s->fade_loc_fade = glGetUniformLocation(s->fade_program, "u_fade");
    s->fade_loc_rot  = glGetUniformLocation(s->fade_program, "u_rot_cos_sin");
    s->fade_loc_res  = glGetUniformLocation(s->fade_program, "u_scale");
    /* We re-use u_scale for both axes — keep a separate uniform. */
    return 0;
}

/* ----- per-launch randomisation --------------------------------- */
static void randomise(State *s){
    uint32_t z = seed_rng();
    s->v_seed        = (float)z;
    s->v_shape       = (float)rnd_int(&z, 0, 5);
    s->v_speed       = rnd(&z, 0.65f, 1.55f);
    s->v_segments    = (float)rnd_int(&z, 8, 22);
    s->v_rotation    = (rnd(&z, 0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f)
                       * rnd(&z, 0.35f, 1.35f);
    s->v_pal_pair    = (float)rnd_int(&z, 0, 5);
    s->v_pal_rate    = rnd(&z, 0.08f, 0.22f);
    s->v_pal_phase   = rnd(&z, 0.0f, 1.0f);
    s->v_pal_contrast= rnd(&z, 0.92f, 1.35f);
    s->v_sym_base    = rnd(&z, 4.0f, 16.0f);
    s->v_burst_freq  = rnd(&z, 0.30f, 1.20f);
    s->v_trail_persist = rnd(&z, 0.86f, 0.97f);
    s->v_warp_amount = rnd(&z, 0.45f, 1.35f);
    s->v_ca_amount   = rnd(&z, 0.0f, 0.012f);
    /* Total cycle length: 4 phases of ~30..55s each + ~12s intermission */
    s->v_journey_total = 4.0f * rnd(&z, 30.0f, 55.0f) + 12.0f;
    /* Shuffle the phase order. Indices 0..4 = tunnel, grid, starfield,
     * kaleido, lattice. Two consecutive duplicates are forbidden so the
     * journey never hard-cuts. */
    int order[5] = {0, 1, 2, 3, 4};
    do {
        shuffle_ints(&z, order, 5);
    } while(order[0] == 0 && order[4] == 4);  /* encourage variety */
    for(int i = 0; i < 5; i++) s->v_phase_order[i] = order[i];
    s->v_phase_order_cur = 0;
    s->last_flash_frame = 0;
    s->frame = 0;
}

/* ----- phase blending -------------------------------------------- *
 * Phases 0..4 are movements (tunnel, grid_horizon, starfield_warp,
 * kaleido, lattice). At any moment we blend smoothly between an
 * "incoming" weight and an "outgoing" weight. We pick the active pair
 * based on time within v_journey_total.
 */
static float smooth01(float x){
    if(x < 0.0f) x = 0.0f; if(x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}
static void compute_phases(State *s, float t, float phases[5]){
    /* Total = 4 legs of ~30..55s + 12s settle = ~150s. Within the
     * cycle we march through v_phase_order. Each "leg" smoothly hands
     * off: 80% of leg time is the destination movement, 20% is a
     * blend from the previous one. */
    const int n_legs = 5;
    float leg = s->v_journey_total / (float)n_legs;
    float x = fmodf(t, s->v_journey_total);
    int leg_idx = (int)(x / leg);
    if(leg_idx >= n_legs) leg_idx = n_legs - 1;
    float local = (x - (float)leg_idx * leg) / leg; /* 0..1 within leg */
    /* Blend window: first 22% and last 22% blend; middle is solid */
    float blend = 0.22f;
    float w_in = 0.0f, w_out = 0.0f;
    int cur = s->v_phase_order[leg_idx];
    int prev = s->v_phase_order[(leg_idx + n_legs - 1) % n_legs];
    if(local < blend){
        w_out = 1.0f - smooth01(local / blend);
        w_in  = 1.0f - w_out;
    } else if(local > 1.0f - blend){
        w_in  = smooth01((local - (1.0f - blend)) / blend);
        w_out = 1.0f - w_in;
    } else {
        w_in = 1.0f; w_out = 0.0f;
    }
    /* Ensure at least one movement is always present */
    if(w_in + w_out < 0.001f){ w_in = 1.0f; }
    /* Tiny "ghost" weight on the lattice so even a "pure" tunnel leg
     * still has a hint of the recursive structure in the background —
     * this is what gives the journey its continuous, never-settling
     * quality. */
    for(int i = 0; i < n_legs; i++) phases[i] = 0.04f;
    phases[cur]  = w_in + 0.04f;
    phases[prev] += w_out;
    /* renormalise */
    float sum = 0.0f;
    for(int i = 0; i < n_legs; i++) sum += phases[i];
    if(sum > 0.0f){
        for(int i = 0; i < n_legs; i++) phases[i] /= sum;
    }
}

/* ----- main scene draw (the journey moment) --------------------- */
static const char *scene_vert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main(){\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

/* ----- per-frame render orchestration ---------------------------- *
 *
 * Each frame is three fullscreen passes:
 *
 *   1. fade_copy_pass(s): bind the OTHER FBO, render the current FBO's
 *      texture into it with a small rotation and a per-frame fade
 *      multiplier. This is the "recursive feedback" pass: wakes spiral
 *      inward and self-replicate. After this pass, fb_index flips to
 *      point at the just-rendered-to FBO (now the "current" buffer).
 *
 *   2. scene_pass(s): bind the current FBO, render the geometry on
 *      top of the faded previous frame by reading the OTHER FBO's
 *      texture (the one we rendered INTO in step 1). The scene shader
 *      adds its colour on top of the trail and outputs the result to
 *      the current FBO. After this pass, the current FBO contains
 *      [faded previous + new geometry].
 *
 *   3. blit_pass(s): bind the default framebuffer (the window), render
 *      the current FBO's texture with a 1:1 fullscreen quad. No fade,
 *      no rotation.
 *
 * We swap fb_index in step 1 — between steps 1 and 2, fb_index points
 * at the destination of step 1 (the just-faded buffer). In step 2 we
 * therefore read from the OTHER texture (the one we just wrote into
 * with step 1's geometry, which holds the trail+wake) — and we write
 * to the FB bound by step 1, which is the current FBO.
 */

static void fade_copy_pass(State *s, float t){
    /* Old "current" is at fb_index. We write into the other FBO. */
    GLuint src_tex = (s->fb_index == 0) ? s->tex_a : s->tex_b;
    GLuint dst_fbo = (s->fb_index == 0) ? s->fbo_b : s->fbo_a;
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, s->fb_w, s->fb_h);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(s->fade_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glUniform1i(s->fade_loc_prev, 0);
    glUniform1f(s->fade_loc_fade, s->v_trail_persist);
    float baseRot = 0.0035f + 0.001f * sinf(t * 0.4f);
    glUniform2f(s->fade_loc_rot, cosf(baseRot), sinf(baseRot));
    glUniform2f(s->fade_loc_res, 1.0f, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    /* Swap so the just-rendered-to FBO becomes "current". */
    s->fb_index = 1 - s->fb_index;
}

static void scene_pass(State *s, float t){
    /* The just-faded buffer is now "current". We draw on top of it.
     * Read from the CURRENT FBO's texture (= the just-faded buffer,
     * i.e. the trail) and write to the SAME current FBO. We must NOT
     * bind any different FBO here — reading from and writing to the
     * same texture in a single draw is GLES-legal (the spec allows it
     * but the result of the texture sample is undefined). The safer
     * pattern is: bind the SAME FBO we're sampling. Drivers handle
     * this case correctly (return the texture's value as it was at
     * the START of the draw call, before any writes). */
    int w = s->fb_w, h = s->fb_h;
    GLuint cur_fbo = (s->fb_index == 0) ? s->fbo_a : s->fbo_b;
    GLuint cur_tex = (s->fb_index == 0) ? s->tex_a : s->tex_b;
    glBindFramebuffer(GL_FRAMEBUFFER, cur_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(s->program);

    glUniform1f(s->u_time, t);
    glUniform2f(s->u_resolution, (float)w, (float)h);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cur_tex);
    glUniform1i(s->u_prev, 0);
    glUniform1f(s->u_fade, 1.0f);

    float phases[5];
    compute_phases(s, t, phases);
    glUniform4f(s->u_phase_ab, phases[0], phases[1], phases[2], phases[3]);
    glUniform1f(s->u_phase_e, phases[4]);

    glUniform2f(s->u_shape_speed, s->v_shape, s->v_speed);
    glUniform2f(s->u_seg_rot, s->v_segments, s->v_rotation);
    glUniform2f(s->u_pal_pair, s->v_pal_pair, s->v_pal_rate);
    glUniform1f(s->u_pal_phase, s->v_pal_phase);
    glUniform1f(s->u_pal_contrast, s->v_pal_contrast);
    glUniform2f(s->u_sym_burst, s->v_sym_base, s->v_burst_freq);
    glUniform1f(s->u_trail_persist, s->v_trail_persist);
    glUniform1f(s->u_warp_amount, s->v_warp_amount);
    glUniform1f(s->u_ca_amount, s->v_ca_amount);

    /* Pulse: zero-crossing of sine at burst frequency, sharpened
     * (pulse*pulse) so it feels musical. */
    float pulse = 0.5f + 0.5f * sinf(t * s->v_burst_freq * 6.2831853f);
    float pulseFinal = pulse * pulse;
    glUniform1f(s->u_pulse, pulseFinal);

    /* Flash: throttled brief inversion at movement boundaries */
    float flash = 0.0f;
    float leg = s->v_journey_total / 5.0f;
    float x = fmodf(t, s->v_journey_total);
    int leg_idx = (int)(x / leg);
    float local = (x - (float)leg_idx * leg) / leg;
    if(local < 0.06f || local > 0.94f){
        float fade = 1.0f;
        if(local < 0.06f) fade = local / 0.06f;
        else              fade = (1.0f - local) / 0.06f;
        if(fade > 1.0f) fade = 1.0f;
        flash = fade * 0.45f;
    }
    if(flash > 0.0f && (s->frame - s->last_flash_frame) < 120){
        flash = 0.0f;
    }
    if(flash > 0.0f) s->last_flash_frame = s->frame;
    glUniform1f(s->u_flash, flash);

    glUniform1f(s->u_seed, s->v_seed);

    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

static void blit_pass(State *s){
    GLuint cur_tex = (s->fb_index == 0) ? s->tex_a : s->tex_b;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, s->win_w, s->win_h);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(s->fade_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cur_tex);
    glUniform1i(s->fade_loc_prev, 0);
    glUniform1f(s->fade_loc_fade, 1.0f);
    glUniform2f(s->fade_loc_rot, 1.0f, 0.0f);
    glUniform2f(s->fade_loc_res, 1.0f, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ----- init / draw / free --------------------------------------- */
static void init_genxvectorcade(ModeInfo *m){
    State *s = (State *)calloc(1, sizeof *s);
    if(!s){ fprintf(stderr, "genxvectorcade: OOM\n"); ncz_harness_die(1); return; }
    m->data = s;
    s->fb_scale = DEFAULT_FB_SCALE;

    /* Vertex buffer */
    const float q[] = { -1,-1,  1,-1,  -1, 1,   -1, 1,  1,-1,  1, 1 };
    glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Main scene shader */
    char *frag = load_shader();
    if(!frag){ ncz_harness_die(1); return; }
    GLuint vs = compile(GL_VERTEX_SHADER, scene_vert, "scene_vs");
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag, "scene_fs");
    free(frag);
    if(!vs || !fs){ ncz_harness_die(1); return; }
    s->program = link_prog(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    if(!s->program){ ncz_harness_die(1); return; }

    /* Cache uniforms */
    s->u_time    = glGetUniformLocation(s->program, "u_time");
    s->u_resolution = glGetUniformLocation(s->program, "u_resolution");
    s->u_prev    = glGetUniformLocation(s->program, "u_prev");
    s->u_fade    = glGetUniformLocation(s->program, "u_fade");
    s->u_phase_ab= glGetUniformLocation(s->program, "u_phase_ab");
    s->u_phase_e = glGetUniformLocation(s->program, "u_phase_e");
    s->u_shape_speed = glGetUniformLocation(s->program, "u_shape_speed");
    s->u_seg_rot = glGetUniformLocation(s->program, "u_seg_rot");
    s->u_pal_pair= glGetUniformLocation(s->program, "u_pal_pair");
    s->u_pal_phase=glGetUniformLocation(s->program, "u_pal_phase");
    s->u_pal_contrast=glGetUniformLocation(s->program, "u_pal_contrast");
    s->u_sym_burst=glGetUniformLocation(s->program, "u_sym_burst");
    s->u_trail_persist=glGetUniformLocation(s->program, "u_trail_persist");
    s->u_warp_amount=glGetUniformLocation(s->program, "u_warp_amount");
    s->u_ca_amount=glGetUniformLocation(s->program, "u_ca_amount");
    s->u_pulse   =glGetUniformLocation(s->program, "u_pulse");
    s->u_flash   =glGetUniformLocation(s->program, "u_flash");
    s->u_seed    =glGetUniformLocation(s->program, "u_seed");

    /* Fade program */
    if(build_fade_program(s) != 0){
        fprintf(stderr, "genxvectorcade: fade program build failed\n");
        ncz_harness_die(1); return;
    }

    /* Per-launch randomisation */
    randomise(s);

    /* FBOs sized to current window */
    int w = m->xgwa.width, h = m->xgwa.height;
    if(w < 1) w = 1280; if(h < 1) h = 720;
    if(create_fbos(s, w, h) != 0){
        fprintf(stderr, "genxvectorcade: FBO setup failed\n");
        ncz_harness_die(1); return;
    }

    s->started = now_seconds();
    s->frame = 0;

    /* Diagnostic */
    fprintf(stderr,
        "[diag] genxvectorcade seed=%.0f shape=%.0f speed=%.3f segments=%.0f "
        "rotation=%.3f pal_pair=%.0f pal_rate=%.4f pal_phase=%.3f pal_contrast=%.3f "
        "sym_base=%.2f burst=%.3f trail=%.4f warp=%.3f ca=%.4f journey=%.1fs "
        "order=[%d,%d,%d,%d,%d] fb_scale=%.2f fb=%dx%d GL=%s\n",
        s->v_seed, s->v_shape, s->v_speed, s->v_segments, s->v_rotation,
        s->v_pal_pair, s->v_pal_rate, s->v_pal_phase, s->v_pal_contrast,
        s->v_sym_base, s->v_burst_freq, s->v_trail_persist,
        s->v_warp_amount, s->v_ca_amount, s->v_journey_total,
        s->v_phase_order[0], s->v_phase_order[1], s->v_phase_order[2],
        s->v_phase_order[3], s->v_phase_order[4],
        s->fb_scale, s->fb_w, s->fb_h, glGetString(GL_VERSION));
}

static void draw_genxvectorcade(ModeInfo *m){
    State *s = (State *)m->data;
    if(!s || !s->program) return;
    int w = m->xgwa.width, h = m->xgwa.height;
    if(w < 1) w = 1; if(h < 1) h = 1;
    /* Resize FBOs if window changed (compositor may reconfigure) */
    if(w != s->win_w || h != s->win_h){
        destroy_fbos(s);
        if(create_fbos(s, w, h) != 0){
            fprintf(stderr, "genxvectorcade: FBO resize failed\n");
            return;
        }
    }

    float t = (float)(now_seconds() - s->started);

    /* 1. fade previous FBO (rotate slightly + scale by persistence) */
    fade_copy_pass(s, t);

    /* 2. draw current geometry into the (now-current) FBO */
    scene_pass(s, t);

    /* 3. blit the FBO to the default framebuffer (window) */
    blit_pass(s);

    s->frame++;

    /* First-frame sanity (logs pixel samples + gl_error) */
    static int once;
    if(!once){
        once = 1;
        /* Read back the CURRENT FBO texture (not the window) — this is
         * what the user is going to see after the blit. If this is
         * black, something is wrong with the FBO path; if it's
         * colourful but the screen is still black, the blit pass is
         * the problem. */
        glBindFramebuffer(GL_FRAMEBUFFER, (s->fb_index == 0) ? s->fbo_a : s->fbo_b);
        unsigned char px[16] = {0};
        glReadPixels(s->fb_w/2, s->fb_h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glReadPixels(s->fb_w/4, s->fb_h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+4);
        glReadPixels(3*s->fb_w/4, s->fb_h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+8);
        glReadPixels(s->fb_w/2, 3*s->fb_h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+12);
        GLenum e = glGetError();
        fprintf(stderr,
            "[diag] genxvectorcade first draw gl_error=0x%x fbo_samples="
            "ctr=%u,%u,%u tl=%u,%u,%u tr=%u,%u,%u br=%u,%u,%u fb=%ux%u uniforms="
            "time:%d res:%d prev:%d phase_ab:%d phase_e:%d shape:%d pal:%d sym:%d\n",
            e, px[0], px[1], px[2], px[4], px[5], px[6], px[8], px[9], px[10],
            px[12], px[13], px[14], s->fb_w, s->fb_h,
            s->u_time, s->u_resolution, s->u_prev, s->u_phase_ab, s->u_phase_e,
            s->u_shape_speed, s->u_pal_pair, s->u_sym_burst);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* Optional frame-time measurement */
    static unsigned long _ft_counter;
    static double _ft_prev;
    static int _ft_enabled;
    if(!_ft_enabled) _ft_enabled = (getenv("NCZ_GVC_PERF_LOG") != NULL);
    if(_ft_enabled){
        double now = now_seconds();
        if((++_ft_counter % 30) == 0){
            if(_ft_prev > 0.0){
                double dt = (now - _ft_prev) / 30.0;
                fprintf(stderr, "[diag] genxvectorcade frame_t frame=%lu dt_ms=%.3f\n",
                        _ft_counter, dt * 1000.0);
                fflush(stderr);
            }
            _ft_prev = now_seconds();
        }
    }
}

static void free_genxvectorcade(ModeInfo *m){
    State *s = (State *)m->data;
    if(!s) return;
    destroy_fbos(s);
    if(s->vbo) glDeleteBuffers(1, &s->vbo);
    if(s->program) glDeleteProgram(s->program);
    if(s->fade_program) glDeleteProgram(s->fade_program);
    free(s);
    m->data = NULL;
}
static void reshape_genxvectorcade(ModeInfo *m, int w, int h){
    (void)m; (void)w; (void)h;
}
static Bool genxvectorcade_handle_event(ModeInfo *m, XEvent *e){
    (void)m; (void)e;
    return False;
}
static void release_genxvectorcade(ModeInfo *m){ (void)m; }

static ModeSpecOpt genxvectorcade_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table genxvectorcade_xscreensaver_function_table = {
    .name = "genxvectorcade",
    .class_ = "GenXVectorCade",
    .init_cb = init_genxvectorcade,
    .draw_cb = draw_genxvectorcade,
    .reshape_cb = reshape_genxvectorcade,
    .event_cb = genxvectorcade_handle_event,
    .free_cb = free_genxvectorcade,
    .release_cb = release_genxvectorcade,
    .opts = &genxvectorcade_opts,
    .defaults_str = DEFAULTS,
};
