/* _POSIX_C_SOURCE for sigaction + clock_gettime, _DEFAULT_SOURCE for M_PI. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/*
 * gles3_compat.c — implementation of the GLES3-native compat layer
 *                  declared in gles3_compat.h. See that header for the
 *                  design rationale and API surface.
 *
 * Compile-time assumptions:
 *   - GLES 3.2 headers present (/usr/include/GLES3/gl32.h via
 *     glesv2.pc's Cflags).
 *   - GLES 3.2 runtime support on the GPU (verified on O6N's
 *     Mali-G720-Immortalis on 2026-08-20 with sky1.gpu=vendor).
 *
 * Build target is linked without libGL.so.1 / gl4es — the GLES3
 * harness binary links system libEGL + libGLESv2 directly.
 */

#include "gles3_compat.h"

/* `gllist.h` transitively pulls in `xlockmoreI.h` → `xscreensaver_compat.h`
 * → `gl4es_include/GL/gl.h`, which redefines GL_FALSE/GL_TRUE/GL_NONE/etc.
 * to the same values as `gl32.h`. Undef the GLES3 ones first so gl4es's
 * definitions win; we never link against libGL.so.1 in the GLES3 path,
 * so there is no real conflict — just two equal-value macros for the
 * same identifier. */
#ifdef GL_FALSE
#  undef GL_FALSE
#endif
#ifdef GL_TRUE
#  undef GL_TRUE
#endif
#ifdef GL_ZERO
#  undef GL_ZERO
#endif
#ifdef GL_ONE
#  undef GL_ONE
#endif
#ifdef GL_NONE
#  undef GL_NONE
#endif
#ifdef GL_NO_ERROR
#  undef GL_NO_ERROR
#endif

#include "gllist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ----------------------------------------------------------------------- */
/* GL1 constants not present in GLES3.2's headers                           */
/* ----------------------------------------------------------------------- */
/*
 * GLES3 dropped all the legacy fixed-function constants that the
 * xscreensaver hacks rely on. We define the ones our compat layer
 * routes through here, with their canonical GL1.x values. Add more
 * as future phases need them.
 */
#ifndef GL_FLAT
#define GL_FLAT                              0x1D00
#endif
#ifndef GL_SMOOTH
#define GL_SMOOTH                            0x1D01
#endif
#ifndef GL_QUADS
#define GL_QUADS                             0x0007
#endif
#ifndef GL_C3F_V3F
#define GL_C3F_V3F                           0x2A24
#endif
#ifndef GL_N3F_V3F
#define GL_N3F_V3F                           0x2A25
#endif
#ifndef GL_AMBIENT_AND_DIFFUSE
#define GL_AMBIENT_AND_DIFFUSE               0x1602
#endif
#ifndef GL_FRONT
#define GL_FRONT                             0x0404
#endif
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK                    0x0408
#endif
#ifndef GL_LINE_LOOP
#define GL_LINE_LOOP                         0x0002
#endif
#ifndef GL_TRIANGLE_FAN
#define GL_TRIANGLE_FAN                      0x0006
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP                    0x0005
#endif
#ifndef GL_LINE_SMOOTH
#define GL_LINE_SMOOTH                       0x0B20
#endif
#ifndef GL_LINE_SMOOTH_HINT
#define GL_LINE_SMOOTH_HINT                  0x0C52
#endif
#ifndef GL_NICEST
#define GL_NICEST                            0x1102
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING                          0x0B50
#endif
#ifndef GL_LIGHT0
#define GL_LIGHT0                            0x4000
#endif
#ifndef GL_POSITION
#define GL_POSITION                          0x1203
#endif
#ifndef GL_AMBIENT
#define GL_AMBIENT                           0x1200
#endif
#ifndef GL_DIFFUSE
#define GL_DIFFUSE                           0x1201
#endif
#ifndef GL_SPECULAR
#define GL_SPECULAR                          0x1202
#endif

/* ----------------------------------------------------------------------- */
/* Section 1 — GLES3 runtime singleton                                     */
/* ----------------------------------------------------------------------- */
/*
 * One shader program + a few global uniforms + scratch VBO for the
 * immediate-mode batch. Created on first use, freed by
 * ncz_gles3_runtime_fini().
 *
 * The scratch VBO is reused across every ncz_im_begin/end pair:
 * glBufferSubData updates the relevant region without reallocating.
 * For batches > SCRATCH_VERT_CAP we spill to an alloc path (rare;
 * companion's FULL_CUBE build does ~20 inline quads which is well
 * under cap).
 */

#define SCRATCH_VERT_CAP 4096

/* ----------------------------------------------------------------------- */
/* Global runtime state — declared at the top of the file so every         */
/* function below can reach it without forward declarations.               */
/* ----------------------------------------------------------------------- */

/* Mat-stack state. Two stacks: modelview (default) and projection. The
 * helpers in section 2 compose onto whichever is "active"; gluPerspective
 * / gluLookAt are the only things that flip to PROJECTION. */
typedef struct {
    nczMatStack proj_stack;
    nczMatStack model_stack;
    int         active;
} ncz_gl_state;

static ncz_gl_state g_ms = { .active = 0 };

/* Public POINTERS for direct stack access — for hacks that need to
 * push/pop a specific stack (boing's draw_scanlines flips to
 * PROJECTION temporarily for an ortho 2D overlay). Pointers stay in
 * sync with g_ms automatically because they alias the same memory. */
nczMatStack *g_proj_stack_ptr;   /* &g_ms.proj_stack */
nczMatStack *g_model_stack_ptr;  /* &g_ms.model_stack */

/* Immediate-mode state. Begin/vertex/normal/color/uv/end all operate on
 * this. The MVP is computed from g_ms.model_stack + g_ms.proj_stack at
 * flush time. */
typedef struct {
    GLenum  primitive;
    int     vertex_count;
    int     vertex_cap;
    float  *verts;
    float   scratch[SCRATCH_VERT_CAP * 12];
    float   cur_color[4];
    float   cur_normal[3];
    float   cur_uv[2];
    bool    recording;
    const nczGLListChain *rec_chain;
    bool    has_material;
    float   material[4];
    bool    lit;
    float   light_dir[3];
    float   light_color[3];
    float   light_ambient[3];
    float   line_width;
    bool    has_texture;
    GLuint  bound_tex;
    bool    use_flat;
} ncz_im_state;

static ncz_im_state g_im = { 0 };

/* GLES3 runtime singleton: shader program + scratch VBO/VAO. */
typedef struct {
    GLuint  program;
    GLint   u_mvp;
    GLint   u_light_dir;
    GLint   u_light_color;
    GLint   u_ambient;
    GLint   u_has_material;
    GLint   u_material_color;
    GLint   u_has_texture;
    GLint   u_tex;
    GLint   u_use_flat;
    GLint   a_pos;
    GLint   a_normal;
    GLint   a_color;
    GLint   a_uv;
    GLuint  scratch_vbo;
    GLuint  scratch_vao;
    bool    initialized;
} ncz_runtime;

static ncz_runtime g_rt = { 0 };

/* Forward declarations so the runtime init can reach the matrix-stack
 * helpers below. */
void ncz_ms_init(void);
nczMatStack *ncz_ms_active(void);
void ncz_ms_set_active(int mode);

static const char *VERT_SHADER =
    "#version 320 es\n"
    "precision highp float;\n"
    "in vec3 a_pos;\n"
    "in vec3 a_normal;\n"
    "in vec4 a_color;\n"
    "in vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_light_color;\n"
    "uniform vec3 u_ambient;\n"
    "uniform vec4 u_material_color;\n"
    "uniform bool u_has_material;\n"
    "uniform bool u_has_texture;\n"
    "uniform bool u_use_flat;\n"
    "uniform sampler2D u_tex;\n"
    "flat out vec3 v_flat_normal;\n"
    "out vec3 v_normal;\n"
    "out vec4 v_color;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "  v_normal = a_normal;\n"
    "  v_flat_normal = a_normal;\n"
    "  vec4 base = u_has_material ? u_material_color : a_color;\n"
    "  v_color = vec4(base.rgb, base.a);\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char *FRAG_SHADER =
    "#version 320 es\n"
    "precision highp float;\n"
    "flat in vec3 v_flat_normal;\n"
    "in vec3 v_normal;\n"
    "in vec4 v_color;\n"
    "in vec2 v_uv;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_light_color;\n"
    "uniform vec3 u_ambient;\n"
    "uniform bool u_has_material;\n"
    "uniform bool u_has_texture;\n"
    "uniform bool u_use_flat;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  vec3 N = u_use_flat ? v_flat_normal : v_normal;\n"
    "  /* Defensive: zero-length normals normalize to NaN. Fall back to\n"
    "   * (0,0,1) so the diffuse term still has a defined direction. */\n"
    "  if (dot(N, N) < 1e-12) N = vec3(0.0, 0.0, 1.0);\n"
    "  N = normalize(N);\n"
    "  /* Use a sentinel fallback when u_light_dir is the (0,0,0) default\n"
    "   * (i.e. lighting not configured by the hack yet): pretend the\n"
    "   * light is straight overhead. normalize((0,0,0)) is NaN and would\n"
    "   * produce garbage output, which is the failure mode of the\n"
    "   * first boing pilot on 2026-08-20. */\n"
    "  vec3 Ldir = u_light_dir;\n"
    "  if (dot(Ldir, Ldir) < 1e-12) Ldir = vec3(0.0, 0.0, 1.0);\n"
    "  vec3 L = normalize(-Ldir);\n"
    "  float ndotl = max(dot(N, L), 0.0);\n"
    "  vec3 lit = v_color.rgb * (u_ambient + u_light_color * ndotl);\n"
    "  if (u_has_texture) {\n"
    "    vec4 t = texture(u_tex, v_uv);\n"
    "    frag = vec4(lit * t.rgb, v_color.a * t.a);\n"
    "  } else {\n"
    "    frag = vec4(lit, v_color.a);\n"
    "  }\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "gles3_compat: shader compile failed (%s):\n%s\n",
                type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static int compile_program(void) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, VERT_SHADER);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAG_SHADER);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return -1;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "gles3_compat: program link failed:\n%s\n", log);
        glDeleteProgram(p);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return -1;
    }
    glDetachShader(p, vs);
    glDetachShader(p, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    g_rt.program = p;
#define U(x) g_rt.u_##x = glGetUniformLocation(p, "u_" #x)
    U(mvp);
    U(light_dir);
    U(light_color);
    U(ambient);
    U(has_material);
    U(material_color);
    U(has_texture);
    U(tex);
    U(use_flat);
#undef U
#define A(x) g_rt.a_##x = glGetAttribLocation(p, "a_" #x)
    A(pos);
    A(normal);
    A(color);
    A(uv);
#undef A
    return 0;
}

int ncz_gles3_runtime_init(void) {
    if (g_rt.initialized) return 0;
    memset(&g_rt, 0, sizeof g_rt);
    if (compile_program() < 0) return -1;
    fprintf(stderr, "[diag] gles3_compat: shader program %u compiled\n", g_rt.program);

    /* Scratch VBO + VAO for the immediate-mode batch. The VBO is sized
     * for the worst-case batch; we use glBufferData with GL_DYNAMIC_DRAW
     * and only glBufferSubData the actually-used range per glEnd. */
    glGenBuffers(1, &g_rt.scratch_vbo);
    glGenVertexArrays(1, &g_rt.scratch_vao);
    glBindVertexArray(g_rt.scratch_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_rt.scratch_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(SCRATCH_VERT_CAP * 12 * sizeof(float)),
                 NULL, GL_DYNAMIC_DRAW);

    /* Per-vertex layout: pos(3) + normal(3) + color(4) + uv(2) = 12 floats.
     * stride = 12 * sizeof(float) = 48. */
    GLsizei stride = 12 * (GLsizei)sizeof(float);
    glVertexAttribPointer(g_rt.a_pos,    3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(g_rt.a_pos);
    glVertexAttribPointer(g_rt.a_normal, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(g_rt.a_normal);
    glVertexAttribPointer(g_rt.a_color,  4, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(g_rt.a_color);
    glVertexAttribPointer(g_rt.a_uv,     2, GL_FLOAT, GL_FALSE, stride, (void *)(10 * sizeof(float)));
    glEnableVertexAttribArray(g_rt.a_uv);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Sensible GLES3 defaults. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);   /* xscreensaver hacks set this themselves */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    ncz_ms_init();
    g_im.cur_color[0] = 1.0f;
    g_im.cur_color[1] = 1.0f;
    g_im.cur_color[2] = 1.0f;
    g_im.cur_color[3] = 1.0f;
    g_im.material[0] = 1.0f;
    g_im.material[1] = 1.0f;
    g_im.material[2] = 1.0f;
    g_im.material[3] = 1.0f;
    g_im.light_ambient[0] = 0.2f;
    g_im.light_ambient[1] = 0.2f;
    g_im.light_ambient[2] = 0.2f;
    g_im.light_color[0] = 1.0f;
    g_im.light_color[1] = 1.0f;
    g_im.light_color[2] = 1.0f;

    g_rt.initialized = true;
    return 0;
}

void ncz_gles3_runtime_fini(void) {
    if (!g_rt.initialized) return;
    if (g_rt.scratch_vbo) glDeleteBuffers(1, &g_rt.scratch_vbo);
    if (g_rt.scratch_vao) glDeleteVertexArrays(1, &g_rt.scratch_vao);
    if (g_rt.program)    glDeleteProgram(g_rt.program);
    memset(&g_rt, 0, sizeof g_rt);
}

GLuint ncz_shader_program(void) {
    return g_rt.program;
}

/* ----------------------------------------------------------------------- */
/* Section 2 — Matrix stack                                                */
/* ----------------------------------------------------------------------- */
/*
 * Two stacks: modelview (default) and projection. ncz_mat_stack_perspective
 * / ncz_mat_stack_lookAt write into the projection stack; all others
 * compose into the modelview stack.
 *
 * MVP is computed on demand by ncz_mat_stack_mvp (projection * modelview).
 */

/* Public accessor for use by the GL1-routing stub layer (when/if we
 * add it in a later phase). The model stack is the default target of
 * every gl* helper except ncz_mat_stack_perspective and
 * ncz_mat_stack_lookAt, which compose onto the projection stack. */
nczMatStack *ncz_ms_active(void) {
    return g_ms.active ? &g_ms.proj_stack : &g_ms.model_stack;
}

void ncz_ms_set_active(int mode) {
    g_ms.active = mode;
}

/* Initialize both stacks. Called from ncz_gles3_runtime_init. */
void ncz_ms_init(void) {
    ncz_mat_stack_init(&g_ms.model_stack);
    ncz_mat_stack_init(&g_ms.proj_stack);
    /* Point the public aliases at the live storage. Ported hacks
     * reference g_proj_stack_ptr / g_model_stack_ptr to push/pop a
     * specific stack without going through ncz_ms_active(). */
    g_proj_stack_ptr  = &g_ms.proj_stack;
    g_model_stack_ptr = &g_ms.model_stack;
}

void ncz_mat4_identity(nczMat4 m) {
    m[0]=1; m[1]=0; m[2]=0; m[3]=0;
    m[4]=0; m[5]=1; m[6]=0; m[7]=0;
    m[8]=0; m[9]=0; m[10]=1; m[11]=0;
    m[12]=0; m[13]=0; m[14]=0; m[15]=1;
}

void ncz_mat4_multiply(nczMat4 out, const nczMat4 a, const nczMat4 b) {
    nczMat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r[i*4+j] = a[i*4+0]*b[0*4+j] +
                       a[i*4+1]*b[1*4+j] +
                       a[i*4+2]*b[2*4+j] +
                       a[i*4+3]*b[3*4+j];
        }
    }
    memcpy(out, r, sizeof r);
}

void ncz_mat4_translate(nczMat4 m, float x, float y, float z) {
    nczMat4 t;
    ncz_mat4_identity(t);
    t[12] = x; t[13] = y; t[14] = z;
    nczMat4 r;
    ncz_mat4_multiply(r, m, t);
    memcpy(m, r, sizeof r);
}

void ncz_mat4_scale(nczMat4 m, float sx, float sy, float sz) {
    nczMat4 s;
    ncz_mat4_identity(s);
    s[0]=sx; s[5]=sy; s[10]=sz;
    nczMat4 r;
    ncz_mat4_multiply(r, m, s);
    memcpy(m, r, sizeof r);
}

void ncz_mat4_rotate(nczMat4 m, float deg, float ax, float ay, float az) {
    /* Rodrigues' rotation formula; matches OpenGL convention.
     * Pre-multiply by a rotation matrix. */
    float rad = deg * (float)(M_PI / 180.0);
    float c = cosf(rad), s = sinf(rad), t = 1.0f - c;
    float len = sqrtf(ax*ax + ay*ay + az*az);
    if (len < 1e-6f) return;
    float x = ax / len, y = ay / len, z = az / len;
    nczMat4 r;
    ncz_mat4_identity(r);
    r[0]=t*x*x + c;     r[1]=t*x*y + s*z; r[2]=t*x*z - s*y; r[3]=0;
    r[4]=t*x*y - s*z;   r[5]=t*y*y + c;   r[6]=t*y*z + s*x; r[7]=0;
    r[8]=t*x*z + s*y;   r[9]=t*y*z - s*x; r[10]=t*z*z + c;  r[11]=0;
    r[12]=0; r[13]=0; r[14]=0; r[15]=1;
    nczMat4 out;
    ncz_mat4_multiply(out, m, r);
    memcpy(m, out, sizeof out);
}

void ncz_mat4_perspective(nczMat4 m, float fovy_deg, float aspect,
                          float znear, float zfar) {
    float f = 1.0f / tanf(fovy_deg * (float)(M_PI / 360.0));
    nczMat4 r;
    memset(r, 0, sizeof r);
    r[0] = f / aspect;
    r[5] = f;
    r[10] = (zfar + znear) / (znear - zfar);
    r[11] = -1.0f;
    r[14] = (2.0f * zfar * znear) / (znear - zfar);
    memcpy(m, r, sizeof r);
}

/* Standard column-major orthographic projection. glOrtho equivalent.
 *
 * Maps (left, right) to (-1, 1) in NDC x; (bottom, top) to (-1, 1) in NDC y;
 * (znear, zfar) to (-1, 1) in NDC z. The vendor boing.c uses glOrtho
 * for the scanlines 2D overlay; without this helper, that path used a
 * broken lookAt-based projection that clipped most scanlines off-screen.
 *
 * This is a 2D-only projection. For 3D scene rendering use perspective. */
void ncz_mat4_ortho(nczMat4 m,
                    float left, float right,
                    float bottom, float top,
                    float znear, float zfar) {
    nczMat4 r;
    memset(r, 0, sizeof r);
    r[0]  = 2.0f / (right - left);
    r[5]  = 2.0f / (top - bottom);
    r[10] = -2.0f / (zfar - znear);
    r[12] = -(right + left) / (right - left);
    r[13] = -(top + bottom) / (top - bottom);
    r[14] = -(zfar + znear) / (zfar - znear);
    r[15] = 1.0f;
    memcpy(m, r, sizeof r);
}

void ncz_mat4_lookAt(nczMat4 m,
                     float ex, float ey, float ez,
                     float cx, float cy, float cz,
                     float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float rlf = 1.0f / sqrtf(fx*fx + fy*fy + fz*fz);
    fx *= rlf; fy *= rlf; fz *= rlf;
    float sx = fy*uz - fz*uy;
    float sy = fz*ux - fx*uz;
    float sz = fx*uy - fy*ux;
    float rls = 1.0f / sqrtf(sx*sx + sy*sy + sz*sz);
    sx *= rls; sy *= rls; sz *= rls;
    float ux2 = sy*fz - sz*fy;
    float uy2 = sz*fx - sx*fz;
    float uz2 = sx*fy - sy*fx;
    nczMat4 r = {
        sx,    ux2,   -fx,   0.0f,
        sy,    uy2,   -fy,   0.0f,
        sz,    uz2,   -fz,   0.0f,
        0.0f,  0.0f,  0.0f,  1.0f,
    };
    ncz_mat4_multiply(m, r, (nczMat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -ex,-ey,-ez,1,
    });
}

void ncz_mat_stack_init(nczMatStack *s) {
    s->top = 0;
    s->mode = 0;
    ncz_mat4_identity(s->m[0]);
}

void ncz_mat_stack_load_identity(nczMatStack *s) {
    ncz_mat4_identity(s->m[s->top]);
}

void ncz_mat_stack_push(nczMatStack *s) {
    if (s->top + 1 >= (int)(sizeof(s->m) / sizeof(s->m[0]))) {
        fprintf(stderr, "gles3_compat: matstack overflow\n");
        return;
    }
    memcpy(s->m[s->top+1], s->m[s->top], sizeof s->m[0]);
    s->top++;
}

void ncz_mat_stack_pop(nczMatStack *s) {
    if (s->top == 0) {
        fprintf(stderr, "gles3_compat: matstack underflow\n");
        return;
    }
    s->top--;
}

void ncz_mat_stack_translate(nczMatStack *s, float x, float y, float z) {
    ncz_mat4_translate(s->m[s->top], x, y, z);
}

void ncz_mat_stack_scale(nczMatStack *s, float sx, float sy, float sz) {
    ncz_mat4_scale(s->m[s->top], sx, sy, sz);
}

void ncz_mat_stack_rotate(nczMatStack *s, float deg, float ax, float ay, float az) {
    ncz_mat4_rotate(s->m[s->top], deg, ax, ay, az);
}

void ncz_mat_stack_perspective(nczMatStack *s, float fovy_deg, float aspect,
                                float znear, float zfar) {
    ncz_mat4_perspective(s->m[s->top], fovy_deg, aspect, znear, zfar);
}

void ncz_mat_stack_lookAt(nczMatStack *s,
                          float ex, float ey, float ez,
                          float cx, float cy, float cz,
                          float ux, float uy, float uz) {
    ncz_mat4_lookAt(s->m[s->top], ex, ey, ez, cx, cy, cz, ux, uy, uz);
}

void ncz_mat_stack_ortho(nczMatStack *s,
                          float left, float right,
                          float bottom, float top,
                          float znear, float zfar) {
    ncz_mat4_ortho(s->m[s->top], left, right, bottom, top, znear, zfar);
}

const float *ncz_mat_stack_modelview(nczMatStack *s) {
    return s->m[s->top];
}

const float *ncz_mat_stack_projection(nczMatStack *s) {
    return s->m[s->top];
}

const float *ncz_mat_stack_mvp(nczMatStack *s, nczMat4 out) {
    ncz_mat4_multiply(out, g_ms.proj_stack.m[g_ms.proj_stack.top],
                          g_ms.model_stack.m[g_ms.model_stack.top]);
    return out;
}

/* ----------------------------------------------------------------------- */
/* Section 3 — Immediate-mode batch accumulator                            */
/* ----------------------------------------------------------------------- */
/*
 * Accumulator state. Begin/vertex/normal/color/uv/end all operate on
 * this. When recording mode is on (set by ncz_im_set_recording), end
 * does NOT issue a draw — it routes to the recorder. When recording is
 * off, end does the upload + draw.
 *
 * Vertex storage: pos(3) + normal(3) + color(4) + uv(2) = 12 floats,
 * 48 bytes per vertex.
 */

void ncz_im_clear_color(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
}

void ncz_im_clear(int mask) {
    glClear(mask);
}

void ncz_im_viewport(int x, int y, int w, int h) {
    glViewport(x, y, w, h);
}

void ncz_im_enable(GLenum cap) {
    glEnable(cap);
}

void ncz_im_disable(GLenum cap) {
    glDisable(cap);
}

void ncz_im_front_face(GLenum mode) {
    glFrontFace(mode);
}

void ncz_im_blend_func(GLenum sfactor, GLenum dfactor) {
    glBlendFunc(sfactor, dfactor);
}

void ncz_im_line_width(float w) {
    g_im.line_width = w;
    glLineWidth(w);
}

void ncz_im_shade_model(GLenum mode) {
    /* GL_FLAT or GL_SMOOTH. We honor this on the next draw by
     * toggling u_use_flat in the shader. */
    g_im.use_flat = (mode == GL_FLAT);
}

void ncz_im_material(GLenum face, GLenum pname, const float *value) {
    (void)face;
    if (pname == GL_AMBIENT_AND_DIFFUSE) {
        memcpy(g_im.material, value, sizeof g_im.material);
        g_im.has_material = true;
    }
    /* Other pnames (GL_SPECULAR, GL_EMISSION, GL_SHININESS) are
     * ignored in this initial pass. xscreensaver hacks that use them
     * are deferred — listed in GLES3-MIGRATION-PHASE1.md as
     * foundation changes for Phase 2+. */
}

void ncz_im_light_position(int light, const float *xyz_w) {
    (void)light;
    /* Position is a 4-vector. The hack's convention is w=0 (directional
     * light "at infinity"). For w=1 (point light at xyz) we'd need to
     * re-derive the direction per-vertex, but every xscreensaver hack
     * in this repo uses w=0 — confirmed by spot-check. */
    if (xyz_w[3] == 0.0f) {
        g_im.light_dir[0] = xyz_w[0];
        g_im.light_dir[1] = xyz_w[1];
        g_im.light_dir[2] = xyz_w[2];
    } else {
        /* Treat as point light at origin, direction along negative
         * of the position. Adequate for the rare case. */
        float len = sqrtf(xyz_w[0]*xyz_w[0] + xyz_w[1]*xyz_w[1] + xyz_w[2]*xyz_w[2]);
        if (len > 1e-6f) {
            g_im.light_dir[0] = -xyz_w[0] / len;
            g_im.light_dir[1] = -xyz_w[1] / len;
            g_im.light_dir[2] = -xyz_w[2] / len;
        }
    }
    g_im.lit = true;
}

void ncz_im_light_ambient(int light, const float *rgba) {
    (void)light;
    g_im.light_ambient[0] = rgba[0];
    g_im.light_ambient[1] = rgba[1];
    g_im.light_ambient[2] = rgba[2];
}

void ncz_im_light_diffuse(int light, const float *rgba) {
    (void)light;
    g_im.light_color[0] = rgba[0];
    g_im.light_color[1] = rgba[1];
    g_im.light_color[2] = rgba[2];
}

void ncz_im_light_specular(int light, const float *rgba) {
    /* Specular is set up in the shader pipeline but ignored in the
     * current shader. Phong/Blinn-Phong specular is a Phase 2+
     * foundation change. */
    (void)light; (void)rgba;
}

void ncz_im_bind_texture(GLenum target, GLuint tex) {
    (void)target;
    g_im.bound_tex = tex;
    g_im.has_texture = true;
}

void ncz_im_set_recording(bool on) {
    g_im.recording = on;
}

void ncz_im_set_chain_for_recording(const nczGLListChain *chain) {
    g_im.rec_chain = chain;
}

void ncz_im_begin(GLenum primitive) {
    g_im.primitive = primitive;
    g_im.vertex_count = 0;
    if (!g_im.verts) {
        g_im.verts = g_im.scratch;
        g_im.vertex_cap = SCRATCH_VERT_CAP;
    }
}

static float *im_push_vertex(float x, float y, float z) {
    if (g_im.vertex_count >= g_im.vertex_cap) {
        /* Spill to heap. Rare. */
        int newcap = g_im.vertex_cap * 2;
        float *nh;
        if (g_im.verts == g_im.scratch) {
            nh = (float *)malloc(newcap * 12 * sizeof(float));
            memcpy(nh, g_im.scratch, g_im.vertex_cap * 12 * sizeof(float));
        } else {
            nh = (float *)realloc(g_im.verts, newcap * 12 * sizeof(float));
        }
        g_im.verts = nh;
        g_im.vertex_cap = newcap;
    }
    float *v = g_im.verts + g_im.vertex_count * 12;
    v[0] = x; v[1] = y; v[2] = z;
    v[3] = g_im.cur_normal[0];
    v[4] = g_im.cur_normal[1];
    v[5] = g_im.cur_normal[2];
    v[6] = g_im.cur_color[0];
    v[7] = g_im.cur_color[1];
    v[8] = g_im.cur_color[2];
    v[9] = g_im.cur_color[3];
    v[10] = g_im.cur_uv[0];
    v[11] = g_im.cur_uv[1];
    g_im.vertex_count++;
    return v;
}

void ncz_im_vertex3f(float x, float y, float z) {
    im_push_vertex(x, y, z);
}

void ncz_im_normal3f(float nx, float ny, float nz) {
    g_im.cur_normal[0] = nx;
    g_im.cur_normal[1] = ny;
    g_im.cur_normal[2] = nz;
}

void ncz_im_color3f(float r, float g, float b) {
    g_im.cur_color[0] = r;
    g_im.cur_color[1] = g;
    g_im.cur_color[2] = b;
    g_im.cur_color[3] = 1.0f;
}

void ncz_im_color3fv(const float *rgb) {
    g_im.cur_color[0] = rgb[0];
    g_im.cur_color[1] = rgb[1];
    g_im.cur_color[2] = rgb[2];
    g_im.cur_color[3] = 1.0f;
}

void ncz_im_color4f(float r, float g, float b, float a) {
    g_im.cur_color[0] = r;
    g_im.cur_color[1] = g;
    g_im.cur_color[2] = b;
    g_im.cur_color[3] = a;
}

void ncz_im_color4fv(const float *rgba) {
    memcpy(g_im.cur_color, rgba, sizeof g_im.cur_color);
}

void ncz_im_tex_coord2f(float u, float v) {
    g_im.cur_uv[0] = u;
    g_im.cur_uv[1] = v;
}

/* Convert the accumulated vertices into draw calls for the current
 * primitive. GL_QUADS becomes 6 indices per quad; GL_TRIANGLES is
 * passed through; GL_LINES is passed through; everything else aborts
 * (should not be hit by any vendored hack in the survey). */
static void im_flush_as_draw(void) {
    if (g_im.vertex_count == 0) return;

    /* Drain any stale errors so our check below reports only fresh ones. */
    while (glGetError() != GL_NO_ERROR) {}

    /* Bind scratch VAO + VBO. */
    glBindVertexArray(g_rt.scratch_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_rt.scratch_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(g_im.vertex_count * 12 * sizeof(float)),
                    g_im.verts);

    GLenum prim;
    GLsizei count = g_im.vertex_count;
    GLuint quad_ibo = 0;
    if (g_im.primitive == GL_QUADS) {
        /* Convert each quad into two triangles via an index buffer.
         * Build it in a stack-resident array if the batch fits; heap
         * otherwise. */
        int nquads = g_im.vertex_count / 4;
        GLsizei nidx = nquads * 6;
        GLuint  stack_idx[1024];
        GLuint *idx = stack_idx;
        GLuint *heap_idx = NULL;
        if ((size_t)nidx > sizeof(stack_idx) / sizeof(stack_idx[0])) {
            heap_idx = (GLuint *)malloc(nidx * sizeof(GLuint));
            idx = heap_idx;
        }
        for (int q = 0; q < nquads; q++) {
            GLuint v = (GLuint)(q * 4);
            idx[q*6 + 0] = v + 0;
            idx[q*6 + 1] = v + 1;
            idx[q*6 + 2] = v + 2;
            idx[q*6 + 3] = v + 0;
            idx[q*6 + 4] = v + 2;
            idx[q*6 + 5] = v + 3;
        }
        glGenBuffers(1, &quad_ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     (GLsizeiptr)(nidx * sizeof(GLuint)), idx, GL_STREAM_DRAW);
        free(heap_idx);
        prim = GL_TRIANGLES;
        count = nidx;
    } else if (g_im.primitive == GL_TRIANGLES ||
               g_im.primitive == GL_LINES ||
               g_im.primitive == GL_LINE_LOOP ||
               g_im.primitive == GL_TRIANGLE_FAN ||
               g_im.primitive == GL_TRIANGLE_STRIP) {
        prim = g_im.primitive;
    } else {
        fprintf(stderr, "gles3_compat: unhandled primitive 0x%x\n",
                (unsigned)g_im.primitive);
        return;
    }

    /* Bind shader + upload uniforms. */
    glUseProgram(g_rt.program);
    nczMat4 mvp;
    ncz_mat_stack_mvp(g_model_stack_ptr, mvp);
    if (g_rt.u_mvp >= 0)        glUniformMatrix4fv(g_rt.u_mvp, 1, GL_FALSE, mvp);
    if (g_rt.u_light_dir >= 0)  glUniform3fv(g_rt.u_light_dir, 1, g_im.light_dir);
    if (g_rt.u_light_color >= 0) glUniform3fv(g_rt.u_light_color, 1, g_im.light_color);
    if (g_rt.u_ambient >= 0)    glUniform3fv(g_rt.u_ambient, 1, g_im.light_ambient);
    if (g_rt.u_has_material >= 0) glUniform1i(g_rt.u_has_material, g_im.has_material ? 1 : 0);
    if (g_rt.u_material_color >= 0) glUniform4fv(g_rt.u_material_color, 1, g_im.material);
    if (g_rt.u_has_texture >= 0) glUniform1i(g_rt.u_has_texture, g_im.has_texture ? 1 : 0);
    if (g_im.has_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_im.bound_tex);
        if (g_rt.u_tex >= 0) glUniform1i(g_rt.u_tex, 0);
    }
    if (g_rt.u_use_flat >= 0) glUniform1i(g_rt.u_use_flat, g_im.use_flat ? 1 : 0);

    /* THE DRAW CALL — must be issued while the program is bound and the
     * VAO + IBO (if any) are bound. */
    if (quad_ibo) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad_ibo);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, NULL);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &quad_ibo);
    } else {
        glDrawArrays(prim, 0, count);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void ncz_im_end(void) {
    if (g_im.recording) {
        return;
    }
    im_flush_as_draw();
    /* Free any heap-spilled vertex storage back to scratch. */
    if (g_im.verts && g_im.verts != g_im.scratch) {
        free(g_im.verts);
        g_im.verts = NULL;
        g_im.vertex_cap = 0;
    }
}

/* ----------------------------------------------------------------------- */
/* Section 4 — gllist VBO conversion                                       */
/* ----------------------------------------------------------------------- */
/*
 * Walk a struct gllist chain. For each node, upload its interleaved
 * float array into a VBO. Build a VAO with the correct attribute
 * layout for either C3F_V3F or N3F_V3F, and pre-build index buffers
 * for the wireframe + quad-to-tri cases.
 */

int nczGLList_upload(const struct gllist *list, nczGLListChain *out) {
    memset(out, 0, sizeof *out);
    if (!list) return 0;

    int n = 0;
    for (const struct gllist *l = list; l; l = l->next) n++;
    if (n == 0) return 0;
    out->nodes = (nczGLListNode *)calloc((size_t)n, sizeof(*out->nodes));
    out->count = n;

    int idx = 0;
    for (const struct gllist *l = list; l; l = l->next, idx++) {
        nczGLListNode *node = &out->nodes[idx];
        node->primitive = l->primitive;
        node->count = l->points;

        int floats_per_vertex = 0;
        if (l->format == GL_C3F_V3F) floats_per_vertex = 6;
        else if (l->format == GL_N3F_V3F) floats_per_vertex = 6;
        else {
            fprintf(stderr, "gles3_compat: gllist format 0x%x not supported\n",
                    (unsigned)l->format);
            return -1;
        }

        /* Upload the interleaved data as-is. We'll bind different
         * attribute offsets in the VAO depending on whether the
         * normal or color attribute comes first. */
        GLsizeiptr bytes = (GLsizeiptr)(l->points * floats_per_vertex * sizeof(float));
        glGenBuffers(1, &node->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, node->vbo);
        glBufferData(GL_ARRAY_BUFFER, bytes, l->data, GL_STATIC_DRAW);

        glGenVertexArrays(1, &node->vao);
        glBindVertexArray(node->vao);
        glBindBuffer(GL_ARRAY_BUFFER, node->vbo);
        GLsizei stride = (GLsizei)(floats_per_vertex * sizeof(float));
        if (l->format == GL_N3F_V3F) {
            glVertexAttribPointer(g_rt.a_normal, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
            glEnableVertexAttribArray(g_rt.a_normal);
            glVertexAttribPointer(g_rt.a_pos,    3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
            glEnableVertexAttribArray(g_rt.a_pos);
        } else { /* GL_C3F_V3F */
            glVertexAttribPointer(g_rt.a_color,  3, GL_FLOAT, GL_FALSE, stride, (void *)0);
            glEnableVertexAttribArray(g_rt.a_color);
            glVertexAttribPointer(g_rt.a_pos,    3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
            glEnableVertexAttribArray(g_rt.a_pos);
        }
        /* Disable the unused attrs so they don't read garbage. */
        if (l->format == GL_N3F_V3F) glDisableVertexAttribArray(g_rt.a_color);
        else                          glDisableVertexAttribArray(g_rt.a_normal);
        /* Color is uploaded as vec4 in the shader, but we only have
         * 3 components; the shader-side attribute will read garbage
         * for the 4th. Bind a 1.0 alpha by setting alpha = 1
         * unconditionally when the format is C3F_V3F: we use a small
         * one-element VBO carrying a constant 4th component. For
         * simplicity in Phase 1 we accept alpha = 1.0 for C3F_V3F
         * data — every xscreensaver gllist we surveyed is fully
         * opaque anyway. */
        glVertexAttrib4f(g_rt.a_color, 1.0f, 1.0f, 1.0f, 1.0f);
        /* UV same trick — default to (0,0) when no texture. */
        glVertexAttrib2f(g_rt.a_uv, 0.0f, 0.0f);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        /* Pre-build the wireframe index buffer. */
        if (l->primitive == GL_QUADS || l->primitive == GL_TRIANGLES) {
            int verts_per = (l->primitive == GL_QUADS) ? 4 : 3;
            int groups = l->points / verts_per;
            GLsizei nidx = groups * verts_per * 2;  /* each edge = 2 indices */
            GLuint *idx = (GLuint *)malloc(nidx * sizeof(GLuint));
            for (int g = 0; g < groups; g++) {
                GLuint base = (GLuint)(g * verts_per);
                for (int v = 0; v < verts_per; v++) {
                    idx[g * verts_per * 2 + v * 2 + 0] = base + v;
                    idx[g * verts_per * 2 + v * 2 + 1] = base + ((v + 1) % verts_per);
                }
            }
            glGenBuffers(1, &node->wire_ibo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, node->wire_ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(nidx * sizeof(GLuint)),
                         idx, GL_STATIC_DRAW);
            free(idx);
            node->wire_icount = nidx;
        }

        /* Pre-build the quad-to-triangle index buffer. */
        if (l->primitive == GL_QUADS) {
            int nquads = l->points / 4;
            GLsizei nidx = nquads * 6;
            GLuint *idx = (GLuint *)malloc(nidx * sizeof(GLuint));
            for (int q = 0; q < nquads; q++) {
                GLuint v = (GLuint)(q * 4);
                idx[q*6 + 0] = v + 0;
                idx[q*6 + 1] = v + 1;
                idx[q*6 + 2] = v + 2;
                idx[q*6 + 3] = v + 0;
                idx[q*6 + 4] = v + 2;
                idx[q*6 + 5] = v + 3;
            }
            glGenBuffers(1, &node->tri_ibo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, node->tri_ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(nidx * sizeof(GLuint)),
                         idx, GL_STATIC_DRAW);
            free(idx);
            node->tri_icount = nidx;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    return 0;
}

void nczGLList_free(nczGLListChain *chain) {
    if (!chain || !chain->nodes) return;
    for (int i = 0; i < chain->count; i++) {
        nczGLListNode *n = &chain->nodes[i];
        if (n->vbo)      glDeleteBuffers(1, &n->vbo);
        if (n->vao)      glDeleteVertexArrays(1, &n->vao);
        if (n->wire_ibo) glDeleteBuffers(1, &n->wire_ibo);
        if (n->tri_ibo)  glDeleteBuffers(1, &n->tri_ibo);
    }
    free(chain->nodes);
    memset(chain, 0, sizeof *chain);
}

static void gllist_draw_with_prim(nczGLListNode *n, GLenum prim, GLuint ibo, GLsizei icount) {
    glBindVertexArray(n->vao);
    if (ibo) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glDrawElements(prim, icount, GL_UNSIGNED_INT, NULL);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    } else {
        glDrawArrays(prim, 0, n->count);
    }
    glBindVertexArray(0);
}

void nczGLList_draw(const nczGLListChain *chain) {
    if (!chain || !chain->nodes) return;

    /* Bind shader + upload uniforms ONCE per call (the per-vertex
     * color/material varies via the vertex array). */
    glUseProgram(g_rt.program);
    nczMat4 mvp;
    ncz_mat_stack_mvp(&g_ms.model_stack, mvp);
    glUniformMatrix4fv(g_rt.u_mvp, 1, GL_FALSE, mvp);
    glUniform3fv(g_rt.u_light_dir, 1, g_im.light_dir);
    glUniform3fv(g_rt.u_light_color, 1, g_im.light_color);
    glUniform3fv(g_rt.u_ambient, 1, g_im.light_ambient);
    glUniform1i(g_rt.u_has_material, g_im.has_material ? 1 : 0);
    glUniform4fv(g_rt.u_material_color, 1, g_im.material);
    glUniform1i(g_rt.u_has_texture, g_im.has_texture ? 1 : 0);
    if (g_im.has_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_im.bound_tex);
        glUniform1i(g_rt.u_tex, 0);
    }
    glUniform1i(g_rt.u_use_flat, g_im.use_flat ? 1 : 0);

    for (int i = 0; i < chain->count; i++) {
        nczGLListNode *n = &chain->nodes[i];
        if (n->primitive == GL_QUADS) {
            gllist_draw_with_prim(n, GL_TRIANGLES, n->tri_ibo, n->tri_icount);
        } else if (n->primitive == GL_LINES || n->primitive == GL_POINTS) {
            gllist_draw_with_prim(n, n->primitive, 0, 0);
        } else if (n->primitive == GL_TRIANGLES) {
            gllist_draw_with_prim(n, GL_TRIANGLES, 0, 0);
        } else {
            fprintf(stderr, "gles3_compat: gllist primitive 0x%x not handled\n",
                    (unsigned)n->primitive);
        }
    }
    glUseProgram(0);
}

void nczGLList_draw_wire(const nczGLListChain *chain) {
    if (!chain || !chain->nodes) return;

    glUseProgram(g_rt.program);
    nczMat4 mvp;
    ncz_mat_stack_mvp(&g_ms.model_stack, mvp);
    glUniformMatrix4fv(g_rt.u_mvp, 1, GL_FALSE, mvp);
    glUniform3fv(g_rt.u_light_dir, 1, g_im.light_dir);
    glUniform3fv(g_rt.u_light_color, 1, g_im.light_color);
    glUniform3fv(g_rt.u_ambient, 1, g_im.light_ambient);
    glUniform1i(g_rt.u_has_material, g_im.has_material ? 1 : 0);
    glUniform4fv(g_rt.u_material_color, 1, g_im.material);
    glUniform1i(g_rt.u_has_texture, g_im.has_texture ? 1 : 0);
    if (g_im.has_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_im.bound_tex);
        glUniform1i(g_rt.u_tex, 0);
    }
    glUniform1i(g_rt.u_use_flat, g_im.use_flat ? 1 : 0);
    glLineWidth(g_im.line_width > 0 ? g_im.line_width : 1.0f);

    for (int i = 0; i < chain->count; i++) {
        nczGLListNode *n = &chain->nodes[i];
        if (n->primitive == GL_LINES || n->primitive == GL_POINTS) {
            gllist_draw_with_prim(n, n->primitive, 0, 0);
        } else if (n->wire_ibo) {
            gllist_draw_with_prim(n, GL_LINES, n->wire_ibo, n->wire_icount);
        } else {
            gllist_draw_with_prim(n, GL_LINE_LOOP, 0, n->count);
        }
    }
    glUseProgram(0);
}

/* ----------------------------------------------------------------------- */
/* Section 5 — Display-list recorder stubs                                 */
/* ----------------------------------------------------------------------- */
/*
 * The companion pilot does NOT need these (it builds the FULL_CUBE
 * list inline, then calls glCallList — for the GLES3 port we
 * restructure the call into two helper invocations: render the inline
 * build_face quads via ncz_im_*, then nczGLList_draw the three
 * uploaded gllist chains). The recorder is provided for completeness
 * and is exercised by later phases; boing and companion don't use it.
 *
 * For Phase 1 we ship it as a thin recorder that captures chain
 * pointers and inline quad/triangle draws, with a fixed-size op
 * buffer. This is the minimum needed to satisfy the API surface
 * declared in gles3_compat.h; the FULL implementation (handling
 * inline glBegin/.../glEnd while recording) is Phase 2+.
 */

#define NCZ_DL_MAX_OPS 1024

int ncz_dl_new(nczDL *dl) {
    if (!dl) return -1;
    if (!dl->ops) {
        dl->ops = (nczDL_Rec *)calloc(NCZ_DL_MAX_OPS, sizeof(*dl->ops));
        if (!dl->ops) return -1;
        dl->max_ops = NCZ_DL_MAX_OPS;
    }
    dl->n_ops = 0;
    return 0;
}

void ncz_dl_end(nczDL *dl) {
    (void)dl;
    /* No-op in Phase 1 — see file-level comment. */
}

int ncz_dl_draw_chain(nczDL *dl, const nczGLListChain *chain) {
    if (!dl || !chain) return -1;
    if (dl->n_ops >= dl->max_ops) return -1;
    nczDL_Rec *r = &dl->ops[dl->n_ops++];
    r->chain = chain;
    r->vcount = 0;
    memcpy(r->color, g_im.cur_color, sizeof r->color);
    memcpy(r->material_ambdiff, g_im.material, sizeof r->material_ambdiff);
    r->has_material = g_im.has_material;
    r->lit = g_im.lit;
    return 0;
}

int ncz_dl_draw_quad(nczDL *dl,
                     const float *p0, const float *p1,
                     const float *p2, const float *p3,
                     const float *normal) {
    if (!dl) return -1;
    if (dl->n_ops >= dl->max_ops) return -1;
    nczDL_Rec *r = &dl->ops[dl->n_ops++];
    r->chain = NULL;
    r->vcount = 4;
    float *v = r->verts;
    const float *pts[4] = { p0, p1, p2, p3 };
    for (int i = 0; i < 4; i++) {
        v[i*9 + 0] = pts[i][0];
        v[i*9 + 1] = pts[i][1];
        v[i*9 + 2] = pts[i][2];
        v[i*9 + 3] = normal[0];
        v[i*9 + 4] = normal[1];
        v[i*9 + 5] = normal[2];
        v[i*9 + 6] = g_im.cur_color[0];
        v[i*9 + 7] = g_im.cur_color[1];
        v[i*9 + 8] = g_im.cur_color[2];
    }
    memcpy(r->color, g_im.cur_color, sizeof r->color);
    memcpy(r->material_ambdiff, g_im.material, sizeof r->material_ambdiff);
    r->has_material = g_im.has_material;
    r->lit = g_im.lit;
    return 0;
}

int ncz_dl_draw_triangle(nczDL *dl,
                         const float *p0, const float *p1, const float *p2,
                         const float *normal) {
    if (!dl) return -1;
    if (dl->n_ops >= dl->max_ops) return -1;
    nczDL_Rec *r = &dl->ops[dl->n_ops++];
    r->chain = NULL;
    r->vcount = 3;
    float *v = r->verts;
    const float *pts[3] = { p0, p1, p2 };
    for (int i = 0; i < 3; i++) {
        v[i*9 + 0] = pts[i][0];
        v[i*9 + 1] = pts[i][1];
        v[i*9 + 2] = pts[i][2];
        v[i*9 + 3] = normal[0];
        v[i*9 + 4] = normal[1];
        v[i*9 + 5] = normal[2];
        v[i*9 + 6] = g_im.cur_color[0];
        v[i*9 + 7] = g_im.cur_color[1];
        v[i*9 + 8] = g_im.cur_color[2];
    }
    memcpy(r->color, g_im.cur_color, sizeof r->color);
    memcpy(r->material_ambdiff, g_im.material, sizeof r->material_ambdiff);
    r->has_material = g_im.has_material;
    r->lit = g_im.lit;
    return 0;
}

void ncz_dl_call(const nczDL *dl) {
    if (!dl || dl->n_ops == 0) return;
    /* Phase 1 stub: iterate ops; for chain ops, replay via gllist
     * draw; for inline ops, push into the scratch VBO and draw. */
    for (int i = 0; i < dl->n_ops; i++) {
        const nczDL_Rec *r = &dl->ops[i];
        if (r->chain) {
            /* Color/material state is the snapshot at recording time,
             * but the LIVE color/material state is what the renderer
             * actually wants to use. The hack calls glMaterialfv
             * right before glCallList, so we honor the live state. */
            nczGLList_draw(r->chain);
        } else if (r->vcount == 4) {
            /* Inline quad. Push into scratch + draw with index expansion. */
            ncz_im_begin(GL_QUADS);
            for (int v = 0; v < 4; v++) {
                ncz_im_normal3f(r->verts[v*9+3], r->verts[v*9+4], r->verts[v*9+5]);
                ncz_im_color3f(r->verts[v*9+6], r->verts[v*9+7], r->verts[v*9+8]);
                ncz_im_vertex3f(r->verts[v*9+0], r->verts[v*9+1], r->verts[v*9+2]);
            }
            ncz_im_end();
        } else if (r->vcount == 3) {
            ncz_im_begin(GL_TRIANGLES);
            for (int v = 0; v < 3; v++) {
                ncz_im_normal3f(r->verts[v*9+3], r->verts[v*9+4], r->verts[v*9+5]);
                ncz_im_color3f(r->verts[v*9+6], r->verts[v*9+7], r->verts[v*9+8]);
                ncz_im_vertex3f(r->verts[v*9+0], r->verts[v*9+1], r->verts[v*9+2]);
            }
            ncz_im_end();
        }
    }
}

void ncz_dl_free(nczDL *dl) {
    if (!dl) return;
    free(dl->ops);
    memset(dl, 0, sizeof *dl);
}

/* ----------------------------------------------------------------------- */
/* Section 6 — EGL context wrapper                                         */
/* ----------------------------------------------------------------------- */
/*
 * Phase 1 keeps the EGL surface+context owned by the harness binary
 * (gles3_harness.c); gles3_compat.c only exposes helpers for the
 * MakeCurrent and SwapBuffers bridge. The handle returned here is
 * trivially small — it exists so future phases can attach per-context
 * state without changing the API surface.
 */

struct ncz_gles3_ctx {
    EGLDisplay dpy;
    EGLContext ctx;
};

ncz_gles3_ctx *ncz_gles3_init(void *wl_display, EGLConfig config) {
    (void)wl_display;
    ncz_gles3_ctx *c = (ncz_gles3_ctx *)calloc(1, sizeof *c);
    if (!c) return NULL;
    /* The harness creates the EGLDisplay + EGLSurface + EGLContext
     * itself; the helpers below just record the context pointer for
     * the bridge. */
    c->dpy = EGL_NO_DISPLAY;
    c->ctx = EGL_NO_CONTEXT;
    return c;
}

void ncz_gles3_destroy(ncz_gles3_ctx *ctx) {
    free(ctx);
}

bool ncz_gles3_make_current(ncz_gles3_ctx *ctx, EGLSurface surface) {
    (void)ctx;
    return eglMakeCurrent(eglGetCurrentDisplay(), surface, surface,
                          eglGetCurrentContext()) == EGL_TRUE;
}

bool ncz_gles3_swap(ncz_gles3_ctx *ctx, EGLSurface surface) {
    (void)ctx;
    return eglSwapBuffers(eglGetCurrentDisplay(), surface) == EGL_TRUE;
}

EGLConfig ncz_gles3_choose_config(EGLDisplay dpy) {
    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE,
    };
    EGLint num = 0;
    if (!eglChooseConfig(dpy, cfg_attr, NULL, 0, &num) || num < 1) {
        return NULL;
    }
    EGLConfig cfg;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &num) || num < 1) return NULL;
    return cfg;
}
