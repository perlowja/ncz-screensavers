/* gles3_xshadertoy_smoke.c — HEADLESS smoke tester for the
 *                             xshadertoy-family shaders in
 *                             vendor/xshadertoy/glsl/.
 *
 * NOT the production driver. The production driver is
 * gles3_xshadertoy.c; it is loaded by gles3_harness.c which needs
 * a live Wayland compositor + wlr-layer-shell surface.
 *
 * This smoke tester is a separate, Wayland-free validation tool
 * that answers five questions for each shader listed on argv:
 *
 *   1. Does the shader's .glsl compile under GLES 3.0 here?
 *   2. Does it link?
 *   3. Does it produce non-fully-transparent, non-zero pixels at
 *      the requested frame indices? (catches "renders black"
 *      regressions that the transitions-smoke pattern catches)
 *   4. For two different iSeed values at the same frame, do the
 *      captured pixels DIFFER? (per-run variation check -- the
 *      acceptance criterion #2 from
 *      docs/superpowers/specs/2026-09-26-afterimage-design.md)
 *   5. Optional: a frame-time measurement, in ms, averaged over
 *      a configurable number of frames at the configured PBuffer
 *      resolution.
 *
 * The preamble/tail/seed logic is COPIED VERBATIM from
 * gles3_xshadertoy.c. If you change the preamble there, mirror
 * the change here or the smoke test will diverge from reality.
 *
 * Output:
 *   - Per (shader, seed, frame): <out-dir>/<name>_seed<seed>_frame<n>.png
 *   - Per-shader summary on stderr.
 *   - Frame-time line (if --measure).
 *
 * Usage:
 *   gles3_xshadertoy_smoke <shader_basename> <out_dir> [t1 t2 ...]
 *   gles3_xshadertoy_smoke --measure [--seed N] <shader_basename> <out_dir> [t1 ...]
 *   gles3_xshadertoy_smoke --width 1920 --height 1080 [--measure] <shader> <out_dir> [t1 ...]
 *
 * The shader directory defaults to vendor/xshadertoy/glsl/ relative
 * to the cwd; override with NCZ_SHADER_DIR env var.
 *
 * Dependencies: Mesa EGL with EGL_KHR_surfaceless_context, GLES 3.x
 * (PBuffer destination), libpng.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <png.h>

/* ------------------------------------------------------------------- */
/* Mirror of gles3_xshadertoy.c preamble/tail/seed. KEEP IN SYNC.      */
/* ------------------------------------------------------------------- */

static const char *vert_src =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *frag_preamble =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "\n"
    "out vec4 frag_color;\n"
    "\n"
    "uniform vec3  iResolution;\n"
    "uniform float iTime;\n"
    "uniform float iTimeDelta;\n"
    "uniform float iFrameRate;\n"
    "uniform int   iFrame;\n"
    "uniform vec4  iDate;\n"
    "uniform vec4  iMouse;\n"
    "uniform vec4  iSeed;\n"
    "\n"
    "uniform vec3  iChannelResolution[4];\n"
    "uniform float iChannelTime[4];\n"
    "\n"
    "uniform sampler2D iChannel0;\n"
    "uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2;\n"
    "uniform sampler2D iChannel3;\n";

static const char *frag_tail =
    "\nvoid main() {\n"
    "  vec4 col = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  mainImage(col, gl_FragCoord.xy);\n"
    "  frag_color = col;\n"
    "}\n";

/* xorshift32, identical to gles3_xshadertoy.c. */
static float
ncz_xorshift_u32(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (float)((double)x / 4294967296.0);
}

static void
ncz_make_iseed(float out[4], uint32_t base_seed) {
    uint32_t s = base_seed;
    if (s == 0) s = 0xA5A5A5A5u;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) (void)ncz_xorshift_u32(&s);
        out[i] = ncz_xorshift_u32(&s);
    }
}

/* ------------------------------------------------------------------- */
/* File I/O                                                             */
/* ------------------------------------------------------------------- */

static char *
read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static char *
build_frag_src(const char *body) {
    size_t pl = strlen(frag_preamble);
    size_t bl = strlen(body);
    size_t tl = strlen(frag_tail);
    char *out = (char *)malloc(pl + bl + tl + 4);
    if (!out) return NULL;
    memcpy(out, frag_preamble, pl);
    out[pl] = '\n';
    memcpy(out + pl + 1, body, bl);
    memcpy(out + pl + 1 + bl, frag_tail, tl + 1);
    return out;
}

/* ------------------------------------------------------------------- */
/* PNG output (via libpng -- same pattern as gles3_transitions_smoke)  */
/* ------------------------------------------------------------------- */

static int
write_png(const char *path, const unsigned char *rgba,
          int w, int h, int flip_vertical) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "smoke: cannot open %s for writing\n", path);
        return -1;
    }
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) { fclose(f); return -1; }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_write_struct(&png_ptr, NULL); fclose(f); return -1; }
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(f);
        return -1;
    }
    png_init_io(png_ptr, f);
    png_set_IHDR(png_ptr, info_ptr, (png_uint_32)w, (png_uint_32)h,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    unsigned char *rows = (unsigned char *)malloc((size_t)w * 4 * h);
    if (!rows) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(f);
        return -1;
    }
    /* GL reads from bottom-left; PNG is top-down. Flip by default. */
    if (flip_vertical) {
        for (int y = 0; y < h; y++) {
            memcpy(rows + y * w * 4,
                   rgba + ((h - 1 - y) * w) * 4,
                   (size_t)w * 4);
        }
    } else {
        memcpy(rows, rgba, (size_t)w * h * 4);
    }
    for (int y = 0; y < h; y++) {
        png_write_row(png_ptr, rows + y * w * 4);
    }
    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    free(rows);
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------- */
/* Compile/link helpers                                                */
/* ------------------------------------------------------------------- */

static GLuint
compile_shader(GLenum stage, const char *src,
               char *log_out, size_t log_cap) {
    GLuint s = glCreateShader(stage);
    if (!s) { snprintf(log_out, log_cap, "glCreateShader failed"); return 0; }
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei got = 0;
        glGetShaderInfoLog(s, sizeof log, &got, log);
        snprintf(log_out, log_cap, "%.*s",
                 (int)(log_cap - 8), log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint
link_program(GLuint vs, GLuint fs, char *log_out, size_t log_cap) {
    GLuint p = glCreateProgram();
    if (!p) { snprintf(log_out, log_cap, "glCreateProgram failed"); return 0; }
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei got = 0;
        glGetProgramInfoLog(p, sizeof log, &got, log);
        snprintf(log_out, log_cap, "%.*s",
                 (int)(log_cap - 8), log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ------------------------------------------------------------------- */
/* EGL setup -- surfaceless + PBuffer.                                 */
/* ------------------------------------------------------------------- */

static EGLDisplay egl_disp;
static EGLContext  egl_ctx;
static EGLSurface  egl_pbuf;
static int         dst_w = 256, dst_h = 256;

static int
init_egl(void) {
    egl_disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_disp == EGL_NO_DISPLAY) {
        fprintf(stderr, "smoke: eglGetDisplay failed\n");
        return -1;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl_disp, &major, &minor)) {
        fprintf(stderr, "smoke: eglInitialize failed\n");
        return -1;
    }
    fprintf(stderr, "[diag] EGL %d.%d vendor=%s\n", major, minor,
            eglQueryString(egl_disp, EGL_VENDOR));
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "smoke: eglBindAPI failed\n");
        return -1;
    }
    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num_cfg = 0;
    if (!eglChooseConfig(egl_disp, cfg_attr, &cfg, 1, &num_cfg) ||
        num_cfg < 1) {
        fprintf(stderr, "smoke: eglChooseConfig failed\n");
        return -1;
    }
    EGLint pbuf_attr[] = { EGL_WIDTH, dst_w, EGL_HEIGHT, dst_h, EGL_NONE };
    egl_pbuf = eglCreatePbufferSurface(egl_disp, cfg, pbuf_attr);
    if (egl_pbuf == EGL_NO_SURFACE) {
        fprintf(stderr, "smoke: eglCreatePbufferSurface failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    EGLint ctx_attr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
    };
    egl_ctx = eglCreateContext(egl_disp, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "smoke: eglCreateContext failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(egl_disp, egl_pbuf, egl_pbuf, egl_ctx)) {
        fprintf(stderr, "smoke: eglMakeCurrent failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    fprintf(stderr, "[diag] GL_VERSION=%s\n",
            (const char *)glGetString(GL_VERSION));
    fprintf(stderr, "[diag] GL_RENDERER=%s\n",
            (const char *)glGetString(GL_RENDERER));
    fprintf(stderr, "[diag] GLSL_VERSION=%s\n",
            (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
    return 0;
}

static void
teardown_egl(void) {
    if (egl_disp != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (egl_ctx != EGL_NO_CONTEXT)
            eglDestroyContext(egl_disp, egl_ctx);
        if (egl_pbuf != EGL_NO_SURFACE)
            eglDestroySurface(egl_disp, egl_pbuf);
        eglTerminate(egl_disp);
    }
}

/* ------------------------------------------------------------------- */
/* Per-shader state.                                                    */
/* ------------------------------------------------------------------- */

typedef struct {
    GLuint program;
    GLuint vbo;
    GLuint ichan_tex;
    GLint  loc_iresolution;
    GLint  loc_itime;
    GLint  loc_itimedelta;
    GLint  loc_ifps;
    GLint  loc_iframe;
    GLint  loc_idate;
    GLint  loc_imouse;
    GLint  loc_iseed;
    GLint  loc_ichan0;
    GLint  loc_ichan1;
    GLint  loc_ichan2;
    GLint  loc_ichan3;
} SState;

static int
init_shader(SState *ss, const char *body, char *fail_reason, size_t fr_cap) {
    memset(ss, 0, sizeof *ss);
    char *frag_src = build_frag_src(body);
    if (!frag_src) {
        snprintf(fail_reason, fr_cap, "build_frag_src returned NULL");
        return -1;
    }
    char log[1024] = "";
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src, log, sizeof log);
    if (!vs) {
        snprintf(fail_reason, fr_cap, "VS: %s", log);
        free(frag_src);
        return -1;
    }
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src, log, sizeof log);
    free(frag_src);
    if (!fs) {
        snprintf(fail_reason, fr_cap, "FS: %s", log);
        return -1;
    }
    ss->program = link_program(vs, fs, log, sizeof log);
    if (!ss->program) {
        snprintf(fail_reason, fr_cap, "LINK: %s", log);
        return -1;
    }
    ss->loc_iresolution = glGetUniformLocation(ss->program, "iResolution");
    ss->loc_itime       = glGetUniformLocation(ss->program, "iTime");
    ss->loc_itimedelta  = glGetUniformLocation(ss->program, "iTimeDelta");
    ss->loc_ifps        = glGetUniformLocation(ss->program, "iFrameRate");
    ss->loc_iframe      = glGetUniformLocation(ss->program, "iFrame");
    ss->loc_idate       = glGetUniformLocation(ss->program, "iDate");
    ss->loc_imouse      = glGetUniformLocation(ss->program, "iMouse");
    ss->loc_iseed       = glGetUniformLocation(ss->program, "iSeed");
    ss->loc_ichan0      = glGetUniformLocation(ss->program, "iChannel0");
    ss->loc_ichan1      = glGetUniformLocation(ss->program, "iChannel1");
    ss->loc_ichan2      = glGetUniformLocation(ss->program, "iChannel2");
    ss->loc_ichan3      = glGetUniformLocation(ss->program, "iChannel3");

    static const float quad[] = {
        -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
        -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
    };
    glGenBuffers(1, &ss->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, ss->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenTextures(1, &ss->ichan_tex);
    glBindTexture(GL_TEXTURE_2D, ss->ichan_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    static const unsigned char black[4] = {0, 0, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, black);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

static void
render_one(SState *ss, const float iseed[4], int frame_idx, float itime,
           int w, int h, unsigned char *rgba_out) {
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(ss->program);

    float zero4[4] = {0.f, 0.f, 0.f, 0.f};
    float date_v[4] = {2026.f, 9.f, 26.f, 0.f};

    glUniform3f(ss->loc_iresolution, (float)w, (float)h, 1.f);
    glUniform1f(ss->loc_itime, itime);
    glUniform1f(ss->loc_itimedelta, 1.f / 60.f);
    glUniform1f(ss->loc_ifps, 60.f);
    glUniform1i(ss->loc_iframe, frame_idx);
    glUniform4fv(ss->loc_idate, 1, date_v);
    glUniform4fv(ss->loc_imouse, 1, zero4);
    glUniform4fv(ss->loc_iseed, 1, iseed);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ss->ichan_tex);
    glUniform1i(ss->loc_ichan0, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ss->ichan_tex);
    glUniform1i(ss->loc_ichan1, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ss->ichan_tex);
    glUniform1i(ss->loc_ichan2, 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, ss->ichan_tex);
    glUniform1i(ss->loc_ichan3, 3);

    glBindBuffer(GL_ARRAY_BUFFER, ss->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    /* Read back. PNG write will flip vertically. */
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba_out);
}

/* ------------------------------------------------------------------- */
/* Tiny helpers: FNV-1a hash, "is mostly black" detector.              */
/* ------------------------------------------------------------------- */

static uint32_t
fnv1a(const unsigned char *p, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

static int
is_mostly_black(const unsigned char *rgba, int w, int h) {
    /* Sample every 16th pixel. If fewer than 1% of samples have any
     * channel > 32 (out of 255), call it black. The 32 threshold
     * is high enough to ignore the standard luminance-floor pixels
     * many of these shaders add to avoid crushing to absolute
     * black; this catches real "renders black" regressions. */
    long total = 0, lit = 0;
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const unsigned char *p = rgba + (y * w + x) * 4;
            total++;
            if (p[0] > 32 || p[1] > 32 || p[2] > 32) lit++;
        }
    }
    if (total == 0) return 1;
    return (lit * 100 / total) < 1;
}

static double
now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------- */
/* main                                                                 */
/* ------------------------------------------------------------------- */

int
main(int argc, char **argv) {
    int do_measure = 0;
    uint32_t explicit_seed = 0;
    int seed_given = 0;
    int next_arg = 1;
    while (next_arg < argc && argv[next_arg][0] == '-') {
        if (strcmp(argv[next_arg], "--measure") == 0) do_measure = 1;
        else if (strcmp(argv[next_arg], "--seed") == 0 &&
                 next_arg + 1 < argc) {
            explicit_seed = (uint32_t)strtoul(argv[next_arg + 1], NULL, 0);
            seed_given = 1;
            next_arg++;
        } else if (strcmp(argv[next_arg], "--width") == 0 &&
                 next_arg + 1 < argc) {
            dst_w = atoi(argv[next_arg + 1]);
            next_arg++;
        } else if (strcmp(argv[next_arg], "--height") == 0 &&
                 next_arg + 1 < argc) {
            dst_h = atoi(argv[next_arg + 1]);
            next_arg++;
        }
        next_arg++;
    }
    /* Required positional: shader basename, out_dir. */
    if (argc - next_arg < 2) {
        fprintf(stderr,
                "usage: gles3_xshadertoy_smoke [--measure] [--seed N] "
                "[--width W --height H] "
                "<shader_basename> <out_dir> [t1 t2 ...]\n");
        return 2;
    }
    const char *shader_name = argv[next_arg];
    const char *out_dir     = argv[next_arg + 1];
    next_arg += 2;

    /* Frame indices to dump at. Default: {60}. */
    int n_frames = (argc - next_arg);
    int *frames = NULL;
    if (n_frames > 0) {
        frames = (int *)malloc(sizeof(int) * (size_t)n_frames);
        if (!frames) {
            fprintf(stderr, "smoke: out of memory\n");
            return 1;
        }
        for (int i = 0; i < n_frames; i++) frames[i] = atoi(argv[next_arg + i]);
    } else {
        n_frames = 1;
        frames = (int *)malloc(sizeof(int));
        frames[0] = 60;
    }

    if (init_egl() != 0) { free(frames); return 1; }

    /* Ensure out_dir exists. */
    struct stat st;
    if (stat(out_dir, &st) != 0) {
        if (mkdir(out_dir, 0755) != 0) {
            fprintf(stderr, "smoke: cannot create %s\n", out_dir);
            free(frames);
            teardown_egl();
            return 1;
        }
    }

    const char *shader_dir = getenv("NCZ_SHADER_DIR");
    if (!shader_dir || !*shader_dir) shader_dir = "vendor/xshadertoy/glsl/";

    char path[1024];
    snprintf(path, sizeof path, "%s%s.glsl", shader_dir, shader_name);
    fprintf(stderr, "\n=== %s ===\n", shader_name);
    size_t blen = 0;
    char *body = read_file(path, &blen);
    if (!body) {
        fprintf(stderr, "%s: cannot read %s\n", shader_name, path);
        free(frames);
        teardown_egl();
        return 1;
    }
    SState ss;
    char fail_reason[256] = "";
    if (init_shader(&ss, body, fail_reason, sizeof fail_reason) != 0) {
        fprintf(stderr, "%s: init failed: %s\n", shader_name, fail_reason);
        free(body); free(frames);
        teardown_egl();
        return 1;
    }
    free(body);

    /* Render with three different iSeed values at each requested
     * frame index. */
    uint32_t seeds[3];
    if (seed_given) {
        seeds[0] = explicit_seed;
        seeds[1] = explicit_seed ^ 0xDEADBEEFu;
        seeds[2] = explicit_seed ^ 0x12345678u;
    } else {
        seeds[0] = 0xA1B2C3D4u;
        seeds[1] = 0xB2C3D4E5u;
        seeds[2] = 0xC3D4E5F6u;
    }

    size_t pix_bytes = (size_t)dst_w * dst_h * 4;
    unsigned char *bufs[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        bufs[k] = (unsigned char *)malloc(pix_bytes);
        if (!bufs[k]) continue;
    }
    int rendered = 0, all_varied = 1;

    for (int fi = 0; fi < n_frames; fi++) {
        int frame_idx = frames[fi];
        float itime = (float)frame_idx / 60.f;

        for (int k = 0; k < 3; k++) {
            if (!bufs[k]) continue;
            float iseed[4];
            ncz_make_iseed(iseed, seeds[k]);
            render_one(&ss, iseed, frame_idx, itime,
                       dst_w, dst_h, bufs[k]);
            char outpath[1024];
            snprintf(outpath, sizeof outpath,
                     "%s/%s_seed%08x_frame%04d.png",
                     out_dir, shader_name, seeds[k], frame_idx);
            if (write_png(outpath, bufs[k], dst_w, dst_h, 1) == 0) {
                uint32_t h1 = fnv1a(bufs[k], pix_bytes);
                int black = is_mostly_black(bufs[k], dst_w, dst_h);
                fprintf(stderr,
                        "  %s seed=%08x frame=%d hash=%08x nonblack=%s -> %s\n",
                        shader_name, seeds[k], frame_idx, h1,
                        black ? "no" : "yes",
                        black ? "FAIL" : "OK");
                if (!black) rendered++;
            }
        }

        /* Per-run variation at this frame: hash(buf[0]) must differ
         * from hash(buf[1]) AND from hash(buf[2]). */
        if (bufs[0] && bufs[1] && bufs[2]) {
            uint32_t h0 = fnv1a(bufs[0], pix_bytes);
            uint32_t h1 = fnv1a(bufs[1], pix_bytes);
            uint32_t h2 = fnv1a(bufs[2], pix_bytes);
            int diff01 = (h0 != h1);
            int diff02 = (h0 != h2);
            int diff12 = (h1 != h2);
            int frame_diff = diff01 && diff02 && diff12;
            fprintf(stderr,
                    "  %s frame=%d per-run-variation: "
                    "hash01=%s hash02=%s hash12=%s -> %s\n",
                    shader_name, frame_idx,
                    diff01 ? "diff" : "SAME",
                    diff02 ? "diff" : "SAME",
                    diff12 ? "diff" : "SAME",
                    frame_diff ? "PASS" : "FAIL");
            if (!frame_diff) all_varied = 0;
        }
    }

    /* Optional: measure frame time over N draws at fixed seed. */
    if (do_measure && bufs[0]) {
        int n_warm = 5, n_meas = 60;
        float iseed[4];
        ncz_make_iseed(iseed, seeds[0]);
        for (int i = 0; i < n_warm; i++) {
            render_one(&ss, iseed, i, (float)i / 60.f,
                       dst_w, dst_h, bufs[0]);
        }
        double t0 = now_sec();
        for (int i = 0; i < n_meas; i++) {
            render_one(&ss, iseed, i, (float)i / 60.f,
                       dst_w, dst_h, bufs[0]);
        }
        double t1 = now_sec();
        double ms = (t1 - t0) * 1000.0 / (double)n_meas;
        fprintf(stderr,
                "  %s measure: avg_frame_ms=%.3f fps=%.1f "
                "(%dx%d, %d frames)\n",
                shader_name, ms, 1000.0 / ms, dst_w, dst_h, n_meas);
    }
    for (int k = 0; k < 3; k++) free(bufs[k]);
    free(frames);
    glDeleteProgram(ss.program);
    glDeleteBuffers(1, &ss.vbo);
    glDeleteTextures(1, &ss.ichan_tex);

    fprintf(stderr,
            "\n[diag] summary: %s rendered=%d per_run_varied=%s\n",
            shader_name, rendered, all_varied ? "yes" : "no");

    teardown_egl();
    return (rendered > 0 && all_varied) ? 0 : 1;
}
