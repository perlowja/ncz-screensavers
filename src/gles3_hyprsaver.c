/* gles3_hyprsaver.c — generic GLES3 wrapper for the 35 hyprsaver GLSL
 *                     fragment shaders vendored at vendor/hyprsaver/.
 *
 * Designed to plug into the gles3_harness.c init_cb / draw_cb / free_cb
 * triple. meson sets:
 *
 *   -DSHADER_FILE=<basename>      which .frag to load (relative to
 *                                 vendor/hyprsaver/shaders/)
 *   -DHACK_PREFIX=<ident>         symbol stem (init_<ident> / draw_<ident>
 *                                 / free_<ident> /
 *                                 <ident>_xscreensaver_function_table)
 *
 * The same .c file compiles 35 times — one per shader — with different
 * -D values, producing 35 independent executables. See the foreach
 * loop in meson.build.
 *
 * -----------------------------------------------------------------------
 * Hyprsaver's shaders all target #version 320 es (GLES 3.2). Confirmed
 * via `grep -h '^#version' vendor/hyprsaver/shaders/*.frag | sort -u`:
 *
 *   #version 320 es
 *   precision mediump float;
 *
 * Uniforms (from `grep -h '^uniform' ... | sort -u`):
 *
 *   uniform float u_time;          seconds since init
 *   uniform vec2  u_resolution;    viewport size in pixels
 *   uniform vec2  u_mouse;         mouse position in pixels
 *   uniform int   u_frame;         frame counter
 *   uniform float u_alpha;         (optional, used by some shaders)
 *   uniform float u_speed_scale;   (optional)
 *   uniform float u_zoom_scale;    (optional)
 *
 * We feed ALL of them from gles3_harness via the GL program; shaders
 * that don't declare a given uniform will silently ignore the call.
 * u_mouse defaults to viewport center and is never animated (headless
 * context — the operator's instruction was don't invent fake motion).
 * -----------------------------------------------------------------------
 *
 * Shaders carry the same permissive MIT-ish license as hyprsaver itself,
 * which is preserved at vendor/hyprsaver-LICENSE.txt and credited in
 * PORTED.md alongside the notes below.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <GLES3/gl32.h>

#include "gles3_compat.h"
#include "xscreensaver_compat.h"

/* Resolved at compile time by -DSHADER_FILE=...frag / -DHACK_PREFIX=...
 * in meson.build's `hyprsaver_shaders` foreach. Defaults are picked so
 * the .c still compiles standalone for sanity-checking. */
#ifndef SHADER_FILE
#define SHADER_FILE "tunnel.frag"
#endif
#ifndef HACK_PREFIX
#define HACK_PREFIX hyprsaver_tunnel
#endif
#ifndef HACK_TABLE
#define HACK_TABLE hyprsaver_tunnel
#endif

/* HACK_PREFIX_str — the expanded token "hyprsaver_<shader>" with the
 * trailing underscore for clean symbol composition. We use a
 * two-stage token-paste (CAT_EXPAND expands its args before
 * pasting) to force the HACK_PREFIX macro to be expanded; without
 * it C99's preprocessor would emit the literal
 * "HACK_PREFIX_" because `##` suppresses macro expansion of its
 * operands.
 *
 * IMPORTANT C-preprocessor rule we rely on elsewhere: the lexer
 * does NOT do sub-token lookups. `init_HACK_PREFIX_STR` is one
 * identifier, and the preprocessor will only look THAT up as a
 * macro. So we ALSO define `init_HACK_PREFIX_STR`,
 * `draw_HACK_PREFIX_STR`, etc. as separate macros below — each
 * expands to the desired per-shader symbol name. */

/* Stringification of the per-shader prefix. The C preprocessor
 * suppresses macro expansion of the operand of `#`, so we indirect
 * through one extra macro layer — STR_EXPAND expands HACK_PREFIX
 * before STR sees it. */
#define STR(x) #x
#define STR_EXPAND(x) STR(x)
#define HACK_PREFIX_STR STR_EXPAND(HACK_PREFIX)

/* Per-shader function-name symbols. The function-table emit in
 * XSCREENSAVER_MODULE_2 uses `init_##PREFIX` (no underscore separator),
 * so the function names are `init_hyprsaver_<shader>` — NOT
 * `init_hyprsaver_<shader>_`. We define them as their own one-token
 * macros here because C preprocessor does NOT do sub-token lookups. */
#define CAT(a, b) a##b
#define CAT_EXPAND(a, b) CAT(a, b)
#define init_HACK_PREFIX_STR_    CAT_EXPAND(init_, HACK_PREFIX)
#define draw_HACK_PREFIX_STR_    CAT_EXPAND(draw_, HACK_PREFIX)
#define free_HACK_PREFIX_STR_    CAT_EXPAND(free_, HACK_PREFIX)
#define reshape_HACK_PREFIX_STR_ CAT_EXPAND(reshape_, HACK_PREFIX)
#define release_HACK_PREFIX_STR_ CAT_EXPAND(release_, HACK_PREFIX)

/* ------------------------------------------------------------------- */
/* Per-hack state                                                      */
/* ------------------------------------------------------------------- */

typedef struct {
    GLuint        program;
    GLuint        vao;
    GLuint        vbo;

    /* Uniform locations (cached once at init). A shader that doesn't
     * declare a given uniform will return -1 from glGetUniformLocation
     * — we still try to update it; GL silently no-ops. */
    GLint         loc_u_time;
    GLint         loc_u_resolution;
    GLint         loc_u_mouse;
    GLint         loc_u_frame;
    GLint         loc_u_alpha;
    GLint         loc_u_speed_scale;
    GLint         loc_u_zoom_scale;

    double        start_time;
    unsigned long frame;
    /* Cached viewport size — taken from ModeInfo (filled in by the
     * harness on configure/reshape). */
    int           width;
    int           height;
} HyprsaverState;

/* ------------------------------------------------------------------- */
/* Vertex shader — trivial pass-through that emits a fullscreen quad.  */
/* ------------------------------------------------------------------- */
static const char *vert_src =
    "#version 320 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

/* ------------------------------------------------------------------- */
/* Trivial pass-through vertex shader + the .frag loaded at init.       */
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
                "hyprsaver: cannot open shader %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static GLuint
compile_shader(GLenum stage, const char *src, const char *tag) {
    GLuint s = glCreateShader(stage);
    if (!s) {
        fprintf(stderr, "hyprsaver: glCreateShader(%u) failed for %s\n",
                (unsigned)stage, tag);
        return 0;
    }
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        char *log = (char *)malloc((size_t)(len > 0 ? len : 1));
        if (log) {
            glGetShaderInfoLog(s, len, NULL, log);
            fprintf(stderr,
                    "hyprsaver: %s compile failed (%s):\n%s\n",
                    tag, "shader", log);
            free(log);
        }
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint
link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    if (!p) return 0;
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        char *log = (char *)malloc((size_t)(len > 0 ? len : 1));
        if (log) {
            glGetProgramInfoLog(p, len, NULL, log);
            fprintf(stderr, "hyprsaver: program link failed:\n%s\n", log);
            free(log);
        }
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ------------------------------------------------------------------- */
/* init_/draw_/free_                                                  */
/* ------------------------------------------------------------------- */

static void
hyprsaver_init(ModeInfo *mi) {
    HyprsaverState *st = (HyprsaverState *)calloc(1, sizeof(*st));
    if (!st) {
        fprintf(stderr, "hyprsaver: OOM at init\n");
        exit(1);
    }
    mi->data = st;
    st->start_time = now_seconds();
    st->frame = 0;

    /* Resolve shader path. The vendor tree lives at vendor/hyprsaver/shaders/
     * relative to the source root. We run from build/ — but the
     * cwd is whatever the compositor invocation set. Try a few roots
     * so this works both in `ninja -C builddir` dev runs and from a
     * package install. */
    char path[1024];
    const char *candidates[] = {
        "vendor/hyprsaver/shaders/" SHADER_FILE,
        "../vendor/hyprsaver/shaders/" SHADER_FILE,
        "../../vendor/hyprsaver/shaders/" SHADER_FILE,
        "/usr/share/ncz-screensavers/shaders/" SHADER_FILE,
    };
    FILE *probe = NULL;
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        probe = fopen(candidates[i], "rb");
        if (probe) { fclose(probe); snprintf(path, sizeof(path), "%s", candidates[i]); break; }
    }
    if (!probe) {
        fprintf(stderr,
                "hyprsaver[%s]: cannot locate vendor/hyprsaver/shaders/%s\n",
                HACK_PREFIX_STR, SHADER_FILE);
        exit(1);
    }

    size_t frag_len = 0;
    char *frag_src = read_entire_file(path, &frag_len);
    if (!frag_src) exit(1);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src, "vertex");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src, "fragment");
    free(frag_src);
    if (!vs || !fs) exit(1);
    st->program = link_program(vs, fs);
    /* Shaders can be deleted now; they're attached to the program. */
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!st->program) exit(1);

    glGenVertexArrays(1, &st->vao);
    glBindVertexArray(st->vao);
    glGenBuffers(1, &st->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    /* Fullscreen NDC quad — 2 triangles covering [-1, 1] x [-1, 1]. */
    const float verts[] = {
        -1.f, -1.f,
         1.f, -1.f,
        -1.f,  1.f,
        -1.f,  1.f,
         1.f, -1.f,
         1.f,  1.f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    /* Cache uniform locations — a shader that doesn't declare a given
     * uniform returns -1 and we silently skip the per-frame update. */
    st->loc_u_time         = glGetUniformLocation(st->program, "u_time");
    st->loc_u_resolution   = glGetUniformLocation(st->program, "u_resolution");
    st->loc_u_mouse        = glGetUniformLocation(st->program, "u_mouse");
    st->loc_u_frame        = glGetUniformLocation(st->program, "u_frame");
    st->loc_u_alpha        = glGetUniformLocation(st->program, "u_alpha");
    st->loc_u_speed_scale  = glGetUniformLocation(st->program, "u_speed_scale");
    st->loc_u_zoom_scale   = glGetUniformLocation(st->program, "u_zoom_scale");

    fprintf(stderr,
            "[diag] hyprsaver[%s] init: GL_VERSION=%s, frag=%s, locs=time:%d res:%d mouse:%d frame:%d alpha:%d speed:%d zoom:%d\n",
            HACK_PREFIX_STR,
            (const char *)glGetString(GL_VERSION),
            SHADER_FILE,
            st->loc_u_time, st->loc_u_resolution, st->loc_u_mouse,
            st->loc_u_frame, st->loc_u_alpha, st->loc_u_speed_scale,
            st->loc_u_zoom_scale);
}

static void
hyprsaver_draw(ModeInfo *mi) {
    HyprsaverState *st = (HyprsaverState *)mi->data;
    if (!st || !st->program) return;

    int w = mi->xgwa.width;
    int h = mi->xgwa.height;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    st->width = w;
    st->height = h;

    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(st->program);
    if (st->loc_u_time        >= 0) glUniform1f(st->loc_u_time,        (float)(now_seconds() - st->start_time));
    if (st->loc_u_resolution  >= 0) glUniform2f(st->loc_u_resolution,  (float)w, (float)h);
    if (st->loc_u_mouse       >= 0) glUniform2f(st->loc_u_mouse,       (float)w * 0.5f, (float)h * 0.5f);
    if (st->loc_u_frame       >= 0) glUniform1i(st->loc_u_frame,       (int)st->frame);
    /* Scaling uniforms — fixed defaults; the operator's brief says
     * "headless/screensaver context has no real value" for these. */
    if (st->loc_u_alpha       >= 0) glUniform1f(st->loc_u_alpha,       1.0f);
    if (st->loc_u_speed_scale >= 0) glUniform1f(st->loc_u_speed_scale, 1.0f);
    if (st->loc_u_zoom_scale  >= 0) glUniform1f(st->loc_u_zoom_scale,  1.0f);

    glBindVertexArray(st->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    st->frame++;
    if ((st->frame % 60) == 0) {
        fprintf(stderr,
                "[diag] hyprsaver[%s] frame=%lu t=%.2fs\n",
                HACK_PREFIX_STR, st->frame,
                now_seconds() - st->start_time);
    }
}

static void
hyprsaver_free(ModeInfo *mi) {
    HyprsaverState *st = (HyprsaverState *)mi->data;
    if (!st) return;
    if (st->vbo) glDeleteBuffers(1, &st->vbo);
    if (st->vao) glDeleteVertexArrays(1, &st->vao);
    if (st->program) glDeleteProgram(st->program);
    free(st);
    mi->data = NULL;
}

static void
hyprsaver_reshape(ModeInfo *mi, int w, int h) {
    /* The viewport size is grabbed fresh each draw from mi->xgwa —
     * no per-reshape work needed for a fullscreen-quad shader. */
    (void)mi; (void)w; (void)h;
}

static Bool
hyprsaver_event(ModeInfo *mi, XEvent *e) {
    (void)mi; (void)e;
    return False;
}

/* The harness expects init_<HACK_PREFIX> etc. We generate the right
 * symbols by stringifying HACK_PREFIX with two layers of macro
 * expansion so the per-binary symbol stem matches what meson declared
 * in -DHACK_PREFIX=hyprsaver_<name>. */
/* HACK_PREFIX_STR + PASTE_EXPAND are defined earlier (after the
 * HACK_PREFIX/#endif block) so the .c body below can use them
 * everywhere, not only after this comment. */

/* Initialize the per-binary symbols. We use a run-time indirection so
 * the .c compiles into 35 different executables with one -D per row. */

/* Public linkage so the xscreensaver_function_table can reference
 * these symbols from the harness. */
void init_HACK_PREFIX_STR_(ModeInfo *mi)    { hyprsaver_init(mi); }
void draw_HACK_PREFIX_STR_(ModeInfo *mi)    { hyprsaver_draw(mi); }
void free_HACK_PREFIX_STR_(ModeInfo *mi)    { hyprsaver_free(mi); }
void reshape_HACK_PREFIX_STR_(ModeInfo *mi, int w, int h)
                                          { hyprsaver_reshape(mi, w, h); }
/* Event-handler symbol has its own convention — XSCREENSAVER_MODULE_2
 * emits `PREFIX##_handle_event` (NOT `handle_##PREFIX`). With
 * PREFIX=HACK_PREFIX that produces the literal identifier
 * `HACK_PREFIX_handle_event`, which the preprocessor then looks up
 * as a macro. We define that macro to expand to the desired
 * per-shader symbol name `<hyprsaver_<shader>>_handle_event`.
 * Function body is just a thin wrapper around the static
 * `hyprsaver_event()` helper so the actual logic stays in one place.
 * Public linkage is required because the table global references it
 * from outside this translation unit. */
/* Event-handler symbol has its own convention — XSCREENSAVER_MODULE_2
 * emits `PREFIX##_handle_event` (NOT `handle_##PREFIX`). With
 * PREFIX=HACK_PREFIX that becomes the literal identifier
 * `HACK_PREFIX_handle_event`, which the preprocessor then looks up
 * as a macro. Define the macro FIRST so the function name below
 * expands to the right per-shader symbol; the XSCREENSAVER_MODULE_2
 * table emit at the bottom of the file references the same name.
 * The function body is a thin wrapper around the static
 * `hyprsaver_event()` helper so the actual logic stays in one place.
 * Public linkage is required because the table global references
 * it from outside this translation unit. */
#define HACK_PREFIX_handle_event CAT_EXPAND(HACK_PREFIX, _handle_event)
Bool HACK_PREFIX_handle_event(ModeInfo *mi, XEvent *e) { return hyprsaver_event(mi, e); }

/* Stubs the XSCREENSAVER_MODULE_2 macro expects to exist for every
 * PREFIX. The hyprsaver shaders don't carry their own option tables
 * (they're pure GL frag-shader file loads with all behavior driven by
 * uniforms), so release_<prefix> is a no-op and <prefix>_opts is a
 * single-element array terminated by an empty descriptor. These are
 * weak — if a future shader wants real options it can override by
 * providing its own. */
void release_HACK_PREFIX_STR_(ModeInfo *mi) { (void)mi; }
/* Per-shader opts symbol. The XSCREENSAVER_MODULE_2 macro emits
 * `&PREFIX##_opts` for the .opts field. We pass HACK_PREFIX as
 * the PREFIX arg, so the table references `&HACK_PREFIX_opts`,
 * which we also define as a macro alias for the expanded token
 * `hyprsaver_<shader>_opts`.
 *
 * The shape of the symbol is a `ModeSpecOpt` struct (a single
 * value, not an array). Initialized to all zeros so the table's
 * numopts/numvars/opts/vars fields stay 0/NULL — the harness
 * never reads them. */
#define HACK_PREFIX_opts CAT_EXPAND(HACK_PREFIX, _opts)
static ModeSpecOpt HACK_PREFIX_opts = { 0, NULL, 0, NULL, NULL };

/* Module-table emit. We bypass xscreensaver_compat.h's
 * XSCREENSAVER_MODULE_2 macro here because it bakes in the
 * `_xscreensaver_function_table` suffix, which our harness sets
 * HACK_TABLE to. We emit the struct directly with
 * HACK_TABLE as the symbol name — that matches what
 * `extern struct xscreensaver_function_table HACK_TABLE;` in
 * gles3_harness.c expects (where HACK_TABLE is -D'd to
 * `<shader>_xscreensaver_function_table`). */
struct xscreensaver_function_table HACK_TABLE = {
    .name        = HACK_PREFIX_STR,
    .class_      = "Hyprsaver",
    .init_cb     = init_HACK_PREFIX_STR_,
    .draw_cb     = draw_HACK_PREFIX_STR_,
    .reshape_cb  = reshape_HACK_PREFIX_STR_,
    .event_cb    = HACK_PREFIX_handle_event,
    .free_cb     = free_HACK_PREFIX_STR_,
    .release_cb  = release_HACK_PREFIX_STR_,
    .opts        = &HACK_PREFIX_opts,
    .defaults_str = DEFAULTS,
};

