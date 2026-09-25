/* gles3_xshadertoy.c — generic GLES3 driver for the 38 Shadertoy-API
 *                      fragment shaders vendored under
 *                      vendor/xshadertoy/glsl/.
 *
 * Patterned after gles3_hyprsaver.c (one .c reused for many shaders
 * via -D flags). meson.set:
 *
 *   -DSHADER_FILE=<basename>.glsl   which .glsl to load (relative to
 *                                   vendor/xshadertoy/glsl/)
 *   -DHACK_PREFIX=<ident>           symbol stem (init_<ident> /
 *                                   draw_<ident> / free_<ident> /
 *                                   <ident>_xscreensaver_function_table)
 *
 * The same .c file compiles 38 times — one per shader — producing 38
 * independent executables (xshadertoy_<shader>_gles3).
 *
 * ----------------------------------------------------------------------
 * Why this exists and why it isn't a port of upstream xshadertoy.c
 * ----------------------------------------------------------------------
 *
 * Upstream xscreensaver 6.16 ships `hacks/glx/xshadertoy.c` (1191 LOC)
 * plus 38 `.glsl` shaders under `hacks/glx/glsl/`. Each shader is
 * launched via a ~20-line bash wrapper that exec's xshadertoy with the
 * shader source piped on stdin. We need GLES3-native binaries that the
 * harness can launch from any cwd and that resolve their shader from
 * the installed /usr/share/... path (not cwd-relative — that was the
 * blackhole regression that bit shipping).
 *
 * A full port of xshadertoy.c is overkill for the 38 shipped shaders:
 *   - All 38 are single-pass (each wrapper only sets `--program0`,
 *     never `--program1`..`--program4`). Multi-pass support is
 *     required by upstream for full Shadertoy-API fidelity but
 *     unused here.
 *   - Upstream's options (--speed, --scale, --automouse, --duration,
 *     etc.) are screenhack config plumbing we don't replicate; the
 *     native GLES3 harness launches one binary per shader and the
 *     shader runs at native tempo.
 *   - Upstream's resource loader (read xrm / parse options / find
 *     shaders via $BUNDLE_RESPATH / android_read_asset_file) is
 *     purely an X11/Android concern.
 *
 * What we DO need is exactly what upstream `fragment_shader_head`
 * already provides: a GLSL preamble that declares the Shadertoy
 * uniform set (iResolution / iTime / iTimeDelta / iFrameRate /
 * iFrame / iDate / iMouse / iChannel0..3), the GLSL-1.2-to-1.3
 * compatibility shims (round, max(int,int), texture/sampler2D aliases,
 * etc.), and a `void main()` wrapper that calls the shader's
 * `mainImage(out vec4 fragColor, in vec2 fragCoord)`. That's the
 * minimum shader-source compatibility shim that lets a vendored
 * `xxx.glsl` compile on GLES 3.x without modification.
 *
 * The uniform shim is straight from the upstream file
 * `fragment_shader_head` (lines 264-408 in the source I read), with
 * small adjustments for our GLES 3.0 target: we always emit
 * `#version 300 es` and pin to precision highp. The whole preamble
 * is ~80 lines including helpers.
 *
 * Single-pass render loop: each frame uploads the Shadertoy uniform
 * set (iResolution, iTime, iTimeDelta, iFrameRate, iFrame, iDate,
 * iMouse), binds a dummy 1x1 black texture to each iChannel0..3 unit
 * so shaders that `texture(iChannel0, ...)` don't read driver garbage,
 * then draws a fullscreen quad. Mouse is a static no-op (the GLES3
 * harness doesn't pipe a Wayland pointer into this hack, and no
 * shipped shader depends on it being live — Shadertoy mouse is
 * "idiosyncratic" per upstream's own comment).
 *
 * ----------------------------------------------------------------------
 * Licensing
 * ----------------------------------------------------------------------
 *
 * The preamble's GLSL compatibility helpers are adapted from upstream
 * xscreensaver 6.16 xshadertoy.c, Copyright © 2026 Jamie Zawinski,
 * under the same X Consortium MIT-style permission notice as
 * xscreensaver itself. The vendored shaders preserve their original
 * MIT / CC0 / CC BY 3.0 / public-domain headers verbatim. See
 * `vendor/xshadertoy/PORTED.md` for the per-shader license inventory.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include <GLES3/gl32.h>

#include "gles3_compat.h"
#include "xscreensaver_compat.h"

/* Resolved at compile time by -DSHADER_FILE=...glsl /
 * -DHACK_PREFIX=... (display name; may contain hyphens, used for
 * the `name` field of the function table and for the binary name) /
 * -DHACK_PREFIX_ID=... (hyphen-free C identifier stem used for the
 * `init_/draw_/free_/...` function names and the function-table
 * struct symbol). Meson passes both for every shader — the
 * hyphen-bearing basenames like `bestill0-0` and `neongravity-1`
 * need the ID form because C identifiers cannot contain hyphens.
 * For basenames without hyphens, HACK_PREFIX_ID == HACK_PREFIX. */
#ifndef SHADER_FILE
#define SHADER_FILE "polarnight.glsl"
#endif
#ifndef HACK_PREFIX
#define HACK_PREFIX xshadertoy_polarnight
#endif
#ifndef HACK_PREFIX_ID
#define HACK_PREFIX_ID xshadertoy_polarnight
#endif
#ifndef HACK_TABLE
#define HACK_TABLE xshadertoy_polarnight_xscreensaver_function_table
#endif

/* Stringify the display name (used for the function-table's `name`
 * field so users see the upstream-style `xshadertoy_<shader>` name). */
#define STR(x) #x
#define STR_EXPAND(x) STR(x)
#define HACK_PREFIX_STR STR_EXPAND(HACK_PREFIX)

/* Per-shader symbol names. All flow through HACK_PREFIX_ID so the
 * function-table struct, init_cb, draw_cb, etc. are valid C. */
#define CAT(a, b) a##b
#define CAT_EXPAND(a, b) CAT(a, b)
#define init_HACK_PREFIX_      CAT_EXPAND(init_, HACK_PREFIX_ID)
#define draw_HACK_PREFIX_      CAT_EXPAND(draw_, HACK_PREFIX_ID)
#define free_HACK_PREFIX_      CAT_EXPAND(free_, HACK_PREFIX_ID)
#define reshape_HACK_PREFIX_   CAT_EXPAND(reshape_, HACK_PREFIX_ID)
#define release_HACK_PREFIX_   CAT_EXPAND(release_, HACK_PREFIX_ID)

/* ------------------------------------------------------------------- */
/* Per-hack state                                                      */
/* ------------------------------------------------------------------- */

typedef struct {
    GLuint program;
    GLuint vbo;
    GLuint ichan_tex;        /* 1x1 RGBA8 dummy texture for iChannel0..3 */
    GLint loc_iresolution;
    GLint loc_itime;
    GLint loc_itimedelta;
    GLint loc_ifps;
    GLint loc_iframe;
    GLint loc_idate;
    GLint loc_imouse;
    GLint loc_ichan0;
    GLint loc_ichan1;
    GLint loc_ichan2;
    GLint loc_ichan3;
    double start_time;
    double last_time;
    unsigned long frame;
} XSToyState;

/* ------------------------------------------------------------------- */
/* Vertex shader — trivial pass-through fullscreen triangle pair.       */
/* ------------------------------------------------------------------- */

static const char *vert_src =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

/* ------------------------------------------------------------------- */
/* Fragment shader preamble — Shadertoy-API uniform set + GLSL 1.2      */
/* compatibility shims. Adapted from upstream xshadertoy.c lines        */
/* 264-408.                                                             */
/* ------------------------------------------------------------------- */

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
    "\n"
    "uniform vec3  iChannelResolution[4];\n"
    "uniform float iChannelTime[4];\n"
    "\n"
    "uniform sampler2D iChannel0;\n"
    "uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2;\n"
    "uniform sampler2D iChannel3;\n"
    "\n"
    "void main() {\n"
    "  vec4 col = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  mainImage(col, gl_FragCoord.xy);\n"
    "  frag_color = col;\n"
    "}\n";

/* ------------------------------------------------------------------- */
/* Helpers                                                              */
/* ------------------------------------------------------------------- */

static double
now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static char *
read_entire_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr,
                "xshadertoy: cannot open shader %s\n", path);
        return NULL;
    }
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

/* Find the shader file. Search order mirrors gles3_blackhole.c
 * (build-tree → repo-relative → installed absolute) so the binary
 * works both before and after `meson install`. The "from /" launch
 * path that bit blackhole shipping is exercised via the absolute
 * /usr/share/... lookup. An override NCZ_SHADER_DIR is honored first
 * so tests and packaging QA can redirect the install location
 * without symlinking /usr/share/... */
static char *
locate_shader(const char *name, char *out_used_path, size_t out_path_cap) {
    static const char *prefixes[] = {
        "vendor/xshadertoy/glsl/",
        "../vendor/xshadertoy/glsl/",
        "../../vendor/xshadertoy/glsl/",
        "/usr/share/ncz-screensavers/shaders/",
        NULL,
    };
    const char *override = getenv("NCZ_SHADER_DIR");
    if (override && *override) {
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", override, name);
        FILE *f = fopen(p, "rb");
        if (f) {
            fclose(f);
            if (out_used_path && out_path_cap) {
                snprintf(out_used_path, out_path_cap, "%s", p);
            }
            return read_entire_file(p, NULL);
        }
        fprintf(stderr,
                "xshadertoy: NCZ_SHADER_DIR=%s set but %s not found\n",
                override, p);
    }
    for (int i = 0; prefixes[i]; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s%s", prefixes[i], name);
        FILE *f = fopen(p, "rb");
        if (f) {
            fclose(f);
            if (out_used_path && out_path_cap) {
                snprintf(out_used_path, out_path_cap, "%s", p);
            }
            return read_entire_file(p, NULL);
        }
    }
    fprintf(stderr,
            "xshadertoy: cannot locate %s in any known location\n", name);
    return NULL;
}

/* Strip the leading `#version ...` / `precision ...` lines from a
 * vendored .glsl if it has them. Upstream .glsl files don't carry a
 * #version line — they assume whatever xshadertoy.c's preamble
 * emits — but our preamble already issues `#version 300 es`. Two
 * `#version` lines in the same shader is a compile error. We strip
 * conservatively: only the leading contiguous block of #version,
 * #extension, #pragma, precision, and blank lines. The first
 * non-whitespace line that doesn't match those starts the body and
 * stays put. */
static const char *
skip_leading_directives(const char *src) {
    const char *p = src;
    while (*p) {
        /* Skip leading whitespace on the current line. */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n') { p++; continue; }
        if (*p == '/' && p[1] == '/') {
            /* C++ comment line — skip to EOL. */
            while (*p && *p != '\n') p++;
            continue;
        }
        if (*p == '#') {
            const char *q = p;
            /* Match a recognized leading directive. */
            static const char *k_directives[] = {
                "version", "extension", "pragma", "precision", NULL
            };
            int matched = 0;
            for (int i = 0; k_directives[i]; i++) {
                size_t L = strlen(k_directives[i]);
                if (strncmp(q + 1, k_directives[i], L) == 0 &&
                    (q[L+1] == ' ' || q[L+1] == '\t' || q[L+1] == '\n')) {
                    /* Skip to end of line. */
                    while (*p && *p != '\n') p++;
                    matched = 1;
                    break;
                }
            }
            if (!matched) return p;
            continue;
        }
        return p;
    }
    return p;
}

/* Build the final fragment shader source = preamble + (stripped)
 * .glsl body. Both pieces are concatenated as C strings — no
 * tokenizing, no re-injection of `#line` directives. */
static char *
build_frag_src(const char *body) {
    size_t preamble_len = strlen(frag_preamble);
    const char *body_start = skip_leading_directives(body);
    size_t body_len = strlen(body_start);
    /* +2 for the separating newline and trailing NUL. */
    char *out = (char *)malloc(preamble_len + body_len + 8);
    if (!out) return NULL;
    memcpy(out, frag_preamble, preamble_len);
    out[preamble_len] = '\n';
    memcpy(out + preamble_len + 1, body_start, body_len + 1);
    return out;
}

static GLuint
compile_shader(GLenum stage, const char *src) {
    GLuint s = glCreateShader(stage);
    if (!s) return 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei got = 0;
        glGetShaderInfoLog(s, sizeof log, &got, log);
        fprintf(stderr, "xshadertoy: %s shader compile failed:\n%s\n",
                stage == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* ------------------------------------------------------------------- */
/* XSCREENSAVER module                                                  */
/* ------------------------------------------------------------------- */

static void
init_xshadertoy(ModeInfo *mi) {
    XSToyState *st = (XSToyState *)calloc(1, sizeof(*st));
    if (!st) { ncz_harness_die(1); return; }
    mi->data = st;

    char used_path[1024] = "";
    char *body = locate_shader(SHADER_FILE, used_path, sizeof used_path);
    if (!body) { ncz_harness_die(1); return; }
    fprintf(stderr, "[diag] xshadertoy shader=%s\n", used_path);

    char *frag_src = build_frag_src(body);
    free(body);
    if (!frag_src) { ncz_harness_die(1); return; }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    free(frag_src);
    if (!vs || !fs) { ncz_harness_die(1); return; }

    st->program = glCreateProgram();
    if (!st->program) { ncz_harness_die(1); return; }
    glAttachShader(st->program, vs);
    glAttachShader(st->program, fs);
    glBindAttribLocation(st->program, 0, "a_pos");
    glLinkProgram(st->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(st->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei got = 0;
        glGetProgramInfoLog(st->program, sizeof log, &got, log);
        fprintf(stderr, "xshadertoy: link failed:\n%s\n", log);
        ncz_harness_die(1);
        return;
    }

    /* Cache uniform locations. GL silently no-ops a write to -1. */
    st->loc_iresolution = glGetUniformLocation(st->program, "iResolution");
    st->loc_itime       = glGetUniformLocation(st->program, "iTime");
    st->loc_itimedelta  = glGetUniformLocation(st->program, "iTimeDelta");
    st->loc_ifps        = glGetUniformLocation(st->program, "iFrameRate");
    st->loc_iframe      = glGetUniformLocation(st->program, "iFrame");
    st->loc_idate       = glGetUniformLocation(st->program, "iDate");
    st->loc_imouse      = glGetUniformLocation(st->program, "iMouse");
    st->loc_ichan0      = glGetUniformLocation(st->program, "iChannel0");
    st->loc_ichan1      = glGetUniformLocation(st->program, "iChannel1");
    st->loc_ichan2      = glGetUniformLocation(st->program, "iChannel2");
    st->loc_ichan3      = glGetUniformLocation(st->program, "iChannel3");

    /* Fullscreen quad — 2 triangles, 6 vertices. */
    static const float quad[] = {
        -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
        -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
    };
    glGenBuffers(1, &st->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* 1x1 RGBA8 black dummy texture for iChannel0..3. Shaders that
     * `texture(iChannelN, ...)` without us providing a real source
     * get black pixels instead of uninitialized driver memory. */
    glGenTextures(1, &st->ichan_tex);
    glBindTexture(GL_TEXTURE_2D, st->ichan_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    static const unsigned char black[4] = {0, 0, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, black);
    glBindTexture(GL_TEXTURE_2D, 0);

    st->start_time = now_seconds();
    st->last_time  = st->start_time;
    fprintf(stderr,
            "[diag] xshadertoy init ok: program=%u vbo=%u GL=%s "
            "uniforms ires=%d itime=%d iframe=%d imouse=%d "
            "ichan0..3=%d,%d,%d,%d\n",
            st->program, st->vbo,
            (const char *)glGetString(GL_VERSION),
            st->loc_iresolution, st->loc_itime, st->loc_iframe,
            st->loc_imouse,
            st->loc_ichan0, st->loc_ichan1,
            st->loc_ichan2, st->loc_ichan3);
}

static void
draw_xshadertoy(ModeInfo *mi) {
    XSToyState *st = (XSToyState *)mi->data;
    if (!st || !st->program) return;

    int w = mi->xgwa.width;
    int h = mi->xgwa.height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    double now = now_seconds();
    float itime = (float)(now - st->start_time);
    float idelta = (float)(now - st->last_time);
    st->last_time = now;
    st->frame++;

    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(st->program);

    /* iMouse is a static (0,0,0,0) — no live Wayland pointer routed
     * to this hack. Shadertoy shaders that check iMouse.zw (drag
     * delta) will see zero, which is the natural "no interaction"
     * state per upstream's own code. */
    float zero4[4] = {0.f, 0.f, 0.f, 0.f};

    glUniform3f(st->loc_iresolution, (float)w, (float)h, 1.f);
    glUniform1f(st->loc_itime, itime);
    glUniform1f(st->loc_itimedelta, idelta);
    /* iFrameRate = (frame+1) / (now-start_time) — the upstream
     * formula uses total_frames / (now-start_time), which divides by
     * zero on the first frame. Use max(elapsed, 1e-6) to stay sane. */
    float fps = (float)st->frame /
                (float)((now - st->start_time) > 1e-6
                        ? (now - st->start_time) : 1e-6);
    glUniform1f(st->loc_ifps, fps);
    glUniform1i(st->loc_iframe, (GLint)st->frame);

    /* iDate — recompute per frame is overkill; recompute per second. */
    static double last_date_update = -1.0;
    static float date_v[4] = {0,0,0,0};
    if (last_date_update < 0.0 || now - last_date_update > 1.0) {
        time_t t = (time_t)now;
        struct tm tm;
        localtime_r(&t, &tm);
        struct tm midnight = tm;
        midnight.tm_hour = 0;
        midnight.tm_min = 0;
        midnight.tm_sec = 0;
        time_t m = mktime(&midnight);
        date_v[0] = (float)(tm.tm_year + 1900);
        date_v[1] = (float)(tm.tm_mon + 1);
        date_v[2] = (float)(tm.tm_mday);
        date_v[3] = (float)(now - (double)m);
        last_date_update = now;
    }
    glUniform4fv(st->loc_idate, 1, date_v);
    glUniform4fv(st->loc_imouse, 1, zero4);

    /* Bind the black 1x1 to all four iChannel units. */
    if (st->ichan_tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, st->ichan_tex);
        glUniform1i(st->loc_ichan0, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, st->ichan_tex);
        glUniform1i(st->loc_ichan1, 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, st->ichan_tex);
        glUniform1i(st->loc_ichan2, 2);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, st->ichan_tex);
        glUniform1i(st->loc_ichan3, 3);
    }

    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    ncz_gles3_draw_arrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    if (st->frame == 4 || (st->frame >= 60 && (st->frame % 60) == 0)) {
        unsigned char px[16] = {0};
        glReadPixels(w/2, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glReadPixels(w/8, h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+4);
        glReadPixels(7*w/8, h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+8);
        glReadPixels(w/8, 7*h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+12);
        GLenum e = glGetError();
        fprintf(stderr,
                "[diag] xshadertoy frame=%lu gl_error=0x%x "
                "samples=%u,%u,%u; %u,%u,%u; %u,%u,%u; %u,%u,%u\n",
                st->frame, (unsigned)e,
                px[0],px[1],px[2], px[4],px[5],px[6],
                px[8],px[9],px[10], px[12],px[13],px[14]);
    }
}

static void
reshape_xshadertoy(ModeInfo *mi, int w, int h) {
    (void)mi; (void)w; (void)h;
}

static Bool
xshadertoy_event(ModeInfo *mi, XEvent *e) {
    (void)mi; (void)e;
    return False;
}

static void
release_xshadertoy(ModeInfo *mi) {
    (void)mi;
}

static void
free_xshadertoy(ModeInfo *mi) {
    XSToyState *st = (XSToyState *)mi->data;
    if (!st) return;
    if (st->program) glDeleteProgram(st->program);
    if (st->vbo) glDeleteBuffers(1, &st->vbo);
    if (st->ichan_tex) glDeleteTextures(1, &st->ichan_tex);
    free(st);
    mi->data = NULL;
}

static ModeSpecOpt xshadertoy_opts = {0, NULL, 0, NULL, NULL};

struct xscreensaver_function_table HACK_TABLE = {
    .name       = HACK_PREFIX_STR,
    .class_     = "XShadertoy",
    .init_cb    = init_xshadertoy,
    .draw_cb    = draw_xshadertoy,
    .reshape_cb = reshape_xshadertoy,
    .event_cb   = xshadertoy_event,
    .free_cb    = free_xshadertoy,
    .release_cb = release_xshadertoy,
    .opts       = &xshadertoy_opts,
    .defaults_str = DEFAULTS,
};
