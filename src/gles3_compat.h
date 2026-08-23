/* _POSIX_C_SOURCE for sigaction + clock_gettime, _DEFAULT_SOURCE for M_PI. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

/*
 * gles3_compat.h — the GLES3-native replacement for the gl4es-based shim
 *                  in xscreensaver_compat.h.
 *
 * Phase 1 of the full gl4es retirement (see GLES3-MIGRATION-PHASE1.md).
 *
 * ──────────────────────────────────────────────────────────────────────────
 * Why a separate compat layer (and not just the system GLES3 headers)
 * ──────────────────────────────────────────────────────────────────────────
 *
 * Vendored xscreensaver hacks were written against OpenGL 1.x / 2.x fixed
 * function: glBegin/glVertex3f/glMaterialfv/glLightfv/glPushMatrix/glRotatef.
 * GLES3 has NONE of those. They must be implemented in user code against
 * the GLES3 API (VBOs, VAOs, shaders, uniforms). This header provides:
 *
 *   1. Immediate-mode-style draw helpers (ncz_im_begin / ncz_im_vertex /
 *      ncz_im_normal / ncz_im_color / ncz_im_end). These are NOT a wrapper
 *      around a hidden GL1 emulator — they accumulate into a CPU-side
 *      vertex buffer and issue ONE real GLES3 glDrawArrays call per
 *      glEnd. This is exactly what gl4es does internally, except we
 *      control it directly and can match the actual GL1 semantics
 *      (current color, current normal, current texture coord, current
 *      material, current matrix).
 *
 *   2. A minimal fixed-function emulator: vertex color attribute,
 *      directional light (one, matching what xscreensaver hacks typically
 *      request via GL_LIGHT0), flat shading, optional 2D texture sampling
 *      with replace-or-modulate env. Implemented as a tiny GLSL ES 3.20
 *      shader pair that every helper routes through. NO display-list
 *      machinery is emulated at this layer (GLES3 has no display lists).
 *
 *   3. A matrix stack replacement (nczMat4 / nczMatStack). GLES3 has
 *      no matrix stack; the helpers in (1) project via the hack's
 *      model-view * projection matrix uploaded as uniforms. gluPerspective
 *      and gluLookAt are ported over from xscreensaver_compat.{h,c} and
 *      operate on this new stack.
 *
 *   4. A VBO-based replacement for the gllist.c pattern (renderList):
 *      nczGLList_upload takes a `struct gllist` chain (same layout as
 *      upstream) and uploads each node as a VBO with VAO bindings set up
 *      for the interleaved format. nczGLList_draw plays the chain.
 *      nczGLList_draw_wire turns quads/triangles into line loops for the
 *      wireframe case (matching the upstream wireframe branch).
 *
 *   5. A record-and-replay mechanism for glNewList / glEndList / glCallList,
 *      because several hacks (companion among them) compile a sequence
 *      of draw commands into a display list once and replay it per frame.
 *      The replay captures draw-call state (vertex count, primitive,
 *      current color/material/matrix) and plays it back as a sequence of
 *      real GLES3 draw calls against the live MVP uniform. There is NO
 *      glGenLists-equivalent GLES3 facility for this; we implement it
 *      directly.
 *
 * ──────────────────────────────────────────────────────────────────────────
 * Why this is alongside xscreensaver_compat.{h,c}, not replacing it
 * ──────────────────────────────────────────────────────────────────────────
 *
 * This is a Phase 1 task in a many-phase migration. The other 86 _demo
 * binaries still link against xscreensaver_compat.{h,c} and run via
 * gl4es. They MUST keep building and running unchanged while the
 * foundation work happens alongside. Deletion of the gl4es path is the
 * very last step (per the operator's explicit override of GRAEAE's
 * "dual-binary split" recommendation) and happens only after every
 * hack has been ported.
 *
 * LICENSING: this file is part of ncz-screensavers, GPL-2.0-or-later.
 * The GLSL ES 3.20 shaders below are derived from the public Khronos
 * fixed-function pipeline reference (MIT-style, see
 * https://www.khronos.org/opengl/wiki/Fixed_Pipeline).
 */

#ifndef NCZ_GLES3_COMPAT_H
#define NCZ_GLES3_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* OpenGL ES 3.2 headers — provides GLint, GLfloat, GLenum, GLuint,
 * GL_TRIANGLES, GL_UNSIGNED_INT, glGenBuffers, etc.  These headers are
 * present on the build host (/usr/include/GLES3/gl32.h, included via
 * glesv2.pc's Cflags) and on O6N at the same path. The Mali-G720 GPU on
 * O6N reports GL_VERSION "OpenGL ES 3.2 v1.r53p0-..." on the vendor
 * blob, GLES 3.2 also on Panthor — confirmed by gles3-probe on
 * 2026-08-20 with sky1.gpu=vendor. */
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>

/* EGL — for context setup in the GLES3 harness binary. */
#include <EGL/egl.h>
#include <EGL/eglext.h>

/* ----------------------------------------------------------------------- */
/* Section 1 — EGL/GLES3 context setup (the GLES3 harness)                  */
/* ----------------------------------------------------------------------- */
/*
 * ncz_gles3_init — bring up an EGL/GLES3 context against the host's
 * Wayland surface. Returns an opaque handle the harness stores; the
 * draw/replay helpers below use the per-thread state set up here.
 *
 * ncz_gles3_choose_config — pick a default EGLConfig matching the
 * RGBA8+depth16 shape that xscreensaver hacks expect. The caller can
 * override, but every screensaver binary in this repo wants the same
 * defaults, so the helper is what they should reach for.
 *
 * ncz_gles3_make_current / ncz_gles3_swap — bridge between the helpers
 * and the host's EGL surface. The harness's frame callback calls swap
 * after invoking the hack's draw_cb, matching glXSwapBuffers.
 */

typedef struct ncz_gles3_ctx ncz_gles3_ctx;

ncz_gles3_ctx *ncz_gles3_init(void *wl_display, EGLConfig config);
void           ncz_gles3_destroy(ncz_gles3_ctx *ctx);
bool           ncz_gles3_make_current(ncz_gles3_ctx *ctx, EGLSurface surface);
bool           ncz_gles3_swap(ncz_gles3_ctx *ctx, EGLSurface surface);

EGLConfig ncz_gles3_choose_config(EGLDisplay dpy);

/* ----------------------------------------------------------------------- */
/* Section 2 — Matrix stack                                                */
/* ----------------------------------------------------------------------- */
/*
 * GLES3 has no matrix stack. Hacks call glMatrixMode / glLoadIdentity /
 * glRotatef / glTranslatef / glScalef / gluPerspective / gluLookAt /
 * glPushMatrix / glPopMatrix hundreds of times per frame. The replacement
 * is a small 4x4 column-major matrix type (matching OpenGL convention)
 * and a tiny stack per mode (MODELVIEW and PROJECTION).
 *
 * nczMat4 is a row-major 4x4 stored flat. The shader uploads it as
 * column-major via the uniform "u_mvp" — we transpose on upload. This
 * keeps the math readable while staying compatible with GLSL.
 *
 * Stack depth 32 is what xlockmore-style hacks typically reach
 * (glPushMatrix / glPopMatrix nesting in companion, gltext, starwars,
 * etc. is <20 deep everywhere).
 */

typedef float nczMat4[16];

typedef struct {
    nczMat4 m[32];
    int     top;
    int     mode;   /* 0 = MODELVIEW (current mode is irrelevant; we just
                            track which matrix the helpers compose onto) */
} nczMatStack;

void ncz_mat4_identity(nczMat4 m);
void ncz_mat4_multiply(nczMat4 out, const nczMat4 a, const nczMat4 b);
void ncz_mat4_translate(nczMat4 m, float x, float y, float z);
void ncz_mat4_scale(nczMat4 m, float sx, float sy, float sz);
void ncz_mat4_rotate(nczMat4 m, float deg, float ax, float ay, float az);
void ncz_mat4_perspective(nczMat4 m, float fovy_deg, float aspect,
                          float znear, float zfar);
void ncz_mat4_ortho(nczMat4 m,
                    float left, float right,
                    float bottom, float top,
                    float znear, float zfar);
void ncz_mat4_frustum(nczMat4 m,
                      float left, float right,
                      float bottom, float top,
                      float znear, float zfar);
void ncz_mat4_lookAt(nczMat4 m,
                     float ex, float ey, float ez,
                     float cx, float cy, float cz,
                     float ux, float uy, float uz);

void ncz_mat_stack_init(nczMatStack *s);
void ncz_mat_stack_load_identity(nczMatStack *s);
void ncz_mat_stack_push(nczMatStack *s);
void ncz_mat_stack_pop(nczMatStack *s);
void ncz_mat_stack_translate(nczMatStack *s, float x, float y, float z);
void ncz_mat_stack_scale(nczMatStack *s, float sx, float sy, float sz);
void ncz_mat_stack_rotate(nczMatStack *s, float deg, float ax, float ay, float az);
void ncz_mat_stack_perspective(nczMatStack *s, float fovy_deg, float aspect,
                                float znear, float zfar);
void ncz_mat_stack_lookAt(nczMatStack *s,
                          float ex, float ey, float ez,
                          float cx, float cy, float cz,
                          float ux, float uy, float uz);
void ncz_mat_stack_ortho(nczMatStack *s,
                          float left, float right,
                          float bottom, float top,
                          float znear, float zfar);
/* Copy the top MODELVIEW * PROJECTION into `out` and return a pointer
 * to the projection matrix (for gluProject). */
const float *ncz_mat_stack_mvp(nczMatStack *s, nczMat4 out);
const float *ncz_mat_stack_modelview(nczMatStack *s);
const float *ncz_mat_stack_projection(nczMatStack *s);

/* Direct access to the two stacks — for hacks that need to push/pop
 * one of them explicitly (boing's draw_scanlines temporarily flips
 * to PROJECTION for the ortho 2D overlay). These are pointers into
 * the live storage in gles3_compat.c; pushes/pops propagate. */
extern nczMatStack *g_proj_stack_ptr;
extern nczMatStack *g_model_stack_ptr;

/* ----------------------------------------------------------------------- */
/* Section 3 — Immediate-mode-style draw helpers                           */
/* ----------------------------------------------------------------------- */
/*
 * These emulate glBegin(GL_TRIANGLES | GL_QUADS | GL_LINES | ...) /
 * glVertex3f / glNormal3f / glColor4f / glEnd. They DO NOT issue any
 * GL calls until glEnd. On glEnd, the accumulated primitive is drawn
 * with one glDrawArrays.
 *
 * For the common case (small batches of <256 vertices) the CPU cost is
 * negligible and the savings over per-vertex GL calls are large.
 *
 * For the quads case, glEnd emits two triangles per quad via a tiny
 * index expansion (4 vertices -> 6 indices). All of the
 * GL_QUADS-using hacks in this repo (boing, companion, glschool,
 * glforestfire, etc.) need exactly this.
 *
 * State tracked across begin/end pairs:
 *   - current color (glColor3f/glColor4f/glMaterialfv)
 *   - current normal (glNormal3f)
 *   - current texture coord (glTexCoord2f)
 *   - current line width (glLineWidth)
 *   - current texture binding (glBindTexture)
 *
 * This is enough to faithfully replay every xscreensaver GL1 call we
 * have surveyed in this repo. Anything that doesn't fit is a Phase-2+
 * foundation change — flagged in the migration doc.
 *
 * Vertex format chosen: position(3) + normal(3) + color(4) + uv(2)
 * = 12 floats = 48 bytes per vertex, packed. Color carries alpha (4
 * components) even when glColor3f is used (alpha = 1.0).
 */

void ncz_im_begin(GLenum primitive);   /* GL_TRIANGLES / GL_QUADS / etc. */
void ncz_im_vertex3f(float x, float y, float z);
void ncz_im_normal3f(float nx, float ny, float nz);
void ncz_im_color3f(float r, float g, float b);
void ncz_im_color3fv(const float *rgb);
void ncz_im_color4f(float r, float g, float b, float a);
void ncz_im_color4fv(const float *rgba);
void ncz_im_tex_coord2f(float u, float v);
void ncz_im_end(void);

/* Stack-managed state setters. The "current" color/normal/uv/etc.
 * persist until the next setter call OR until the shader is told to
 * use material-derived color instead (glMaterialfv). */
void ncz_im_line_width(float w);

void ncz_im_clear_color(float r, float g, float b, float a);
void ncz_im_clear(int mask);                /* GL_COLOR_BUFFER_BIT etc. */
void ncz_im_viewport(int x, int y, int w, int h);
void ncz_im_enable(GLenum cap);             /* GL_DEPTH_TEST / GL_CULL_FACE / ... */
void ncz_im_disable(GLenum cap);

/* FrontFace — boing's sphere is wound CW (because the vertex order is
 * what we'd normally call "CCW seen from inside"), and uses
 * glFrontFace(GL_CW) to compensate. Forwarded to the real GLES3
 * glFrontFace. */
void ncz_im_front_face(GLenum mode);       /* GL_CW / GL_CCW */

/* Material — when set, the current color comes from the material
 * ambient+diffuse rather than the explicit glColor call. This is the
 * xscreensaver convention. */
void ncz_im_material(GLenum face, GLenum pname,
                     const float *value);  /* face = GL_FRONT/GL_FRONT_AND_BACK,
                                              pname = GL_AMBIENT_AND_DIFFUSE */
void ncz_im_shade_model(GLenum mode);       /* GL_FLAT / GL_SMOOTH */

/* Lighting. We support ONE directional light (matching what ~every
 * xscreensaver GL hack configures via glLight0). The hack's own
 * glLightfv(GL_LIGHT0, GL_POSITION, ...) and glLightfv(...,
 * GL_AMBIENT/DIFFUSE/SPECULAR, ...) set the active light's
 * parameters; glEnable(GL_LIGHTING) / glEnable(GL_LIGHT0) flips
 * shading to lit mode. */
void ncz_im_light_position(int light, const float *xyz_w);
void ncz_im_light_ambient (int light, const float *rgba);
void ncz_im_light_diffuse (int light, const float *rgba);
void ncz_im_light_specular(int light, const float *rgba);

/* Texture binding. The hack loads the image via image_data_to_ximage
 * (unchanged), then calls glTexImage2D to upload. ncz_im_bind_texture
 * tells the helpers which sampler to use on the next draw. */
void ncz_im_bind_texture(GLenum target, GLuint tex);

/* State — the GLES3 shader is uniform-driven, so we read these per
 * glEnd. Setter is exposed for callers that want to override the
 * default depth-test / blend state without going through ncz_im_enable. */
void ncz_im_blend_func(GLenum sfactor, GLenum dfactor);

/* ----------------------------------------------------------------------- */
/* Section 4 — VBO-based replacement for xscreensaver's gllist.c            */
/* ----------------------------------------------------------------------- */
/*
 * xscreensaver's gllist.c stores baked vertex data in static const
 * float arrays, then plays them with glInterleavedArrays + glDrawArrays
 * via renderList(list, wire_p). GLES3 has glInterleavedArrays (via
 * ext), but it is deprecated and the cleanest path is to upload each
 * node's interleaved data into a VBO once and replay it with a
 * properly configured VAO.
 *
 * struct gllist — kept identical to upstream's gllist.h:
 *     GLenum format;       // GL_C3F_V3F, GL_N3F_V3F
 *     GLenum primitive;    // GL_TRIANGLES, GL_QUADS, GL_LINES, ...
 *     int    points;       // number of vertices
 *     const void *data;    // interleaved float array, never mutated
 *     struct gllist *next;
 *
 * We include the upstream gllist.h verbatim so that the vendored
 * companion_disc.c / companion_heart.c / companion_quad.c (and any
 * other *_model.c the future phases will port) compile without
 * editing their data declarations.
 *
 * nczGLList_upload walks a gllist chain, creating one VBO+VAO per node.
 * nczGLList_draw plays the chain. nczGLList_draw_wire plays quads /
 * triangles as line loops for the wireframe case (matching upstream's
 * "do it the hard way" branch).
 *
 * The format mapping:
 *   GL_C3F_V3F  -> 6 floats per vertex, position (0..2), color (3..5)
 *   GL_N3F_V3F  -> 6 floats per vertex, normal (0..2), position (3..5)
 *
 * Note: the interleaved layouts we accept are the SAME two formats
 * upstream uses. The vendor models (companion_*, handsy_*, headroom_*,
 * etc.) are all N3F_V3F per spot-checks in the existing data files
 * already in src/. We confirm by inspection in the data — first six
 * floats of companion_quad.c are position(3)+normal(3), in the
 * N3F_V3F order. companion_disc and companion_heart likewise.
 */

/*
 * struct gllist — kept identical to upstream's gllist.h (vendored in
 * src/gllist.h). We forward-declare here to avoid pulling in the
 * legacy xlockmoreI.h → xscreensaver_compat.h → gl4es_include/GL/gl.h
 * header chain. Files that USE nczGLList_* (and need the full struct
 * definition) should #include "gllist.h" themselves before this header.
 */
struct gllist;

/* An uploaded gllist chain. One entry per `struct gllist` node. */
typedef struct nczGLListNode_tag {
    GLuint  vbo;
    GLuint  vao;
    GLenum  primitive;     /* GL_TRIANGLES / GL_QUADS / GL_LINES */
    GLsizei count;         /* vertex count */
    /* For the wireframe case, an index buffer that turns quads into
     * line loops (4 -> 8 indices) and triangles into line loops
     * (3 -> 6 indices). 0 when the wireframe path is unused. */
    GLuint  wire_ibo;
    GLsizei wire_icount;
    /* For quads, an index buffer that expands 4 -> 6 indices for two
     * triangles. 0 when not needed. */
    GLuint  tri_ibo;
    GLsizei tri_icount;
} nczGLListNode;

typedef struct nczGLListChain {
    int             count;
    nczGLListNode  *nodes;
} nczGLListChain;

/* Build the chain: parse the gllist, allocate VBOs+VAOs+IBOs, upload.
 * Returns 0 on success, -1 on failure (with a logged message). The
 * chain must be freed with nczGLList_free. */
int  nczGLList_upload(const struct gllist *list, nczGLListChain *out);
void nczGLList_free(nczGLListChain *chain);

/* Replay the chain — colored, lit if lighting is on, with the current
 * ncz_im state (color/material/etc.) applied per draw. */
void nczGLList_draw(const nczGLListChain *chain);
void nczGLList_draw_wire(const nczGLListChain *chain);

/* ----------------------------------------------------------------------- */
/* Section 5 — Display-list recording (glNewList/glEndList/glCallList)     */
/* ----------------------------------------------------------------------- */
/*
 * Several hacks (companion among them) compile a sequence of draw
 * commands into a display list once and replay it per frame via
 * glCallList. GLES3 has no display lists.
 *
 * The replacement is a small command recorder. Each glNewList / glEndList
 * bracket records a list of "draw this gllist chain" and "draw this
 * inline sequence of triangles/quads" commands plus the matrix state
 * at the time of recording. glCallList replays them as a sequence of
 * native GLES3 draw calls.
 *
 * The companion hack specifically uses this for the "FULL_CUBE" list,
 * which is the whole companion cube assembled from build_face (a
 * mix of inline glBegin/glEnd immediate-mode quads) + the three
 * uploaded gllist chains (quad, disc, heart). The replay has to
 * preserve the per-cube material color the caller set with
 * glMaterialfv before the glCallList.
 *
 * Recording is implemented as a struct nczDLNode list of draw ops,
 * each op holding a snapshot of the ncz_im color/material state at
 * the time it was recorded. Replay is just walking the list and
 * issuing the corresponding nczGLList_draw / inline draw.
 *
 * Maximum recorded ops: 1024 (well over what companion needs; if a
 * future hack needs more, the limit is one struct-array size bump).
 */

typedef enum {
    NCZ_DL_OP_GLLIST,    /* draw a previously-uploaded gllist chain */
    NCZ_DL_OP_INLINE,    /* inline immediate-mode batch (primitive +
                            up to 16 verts × 12 floats each) */
} nczDL_Op;

typedef struct {
    /* For GLLIST: pointer to the chain (uploaded at init, stable ptr).
     * For INLINE: NULL. */
    const nczGLListChain *chain;
    /* Inline-vertex storage: up to 16 vertices in the standard 12-
     * float per-vertex format (pos3 + normal3 + color4 + uv2).
     * Plenty for what the legacy hacks emit in one glBegin/glEnd. */
    GLenum  primitive;          /* GL_TRIANGLES / GL_QUADS / GL_LINES / etc. */
    float   verts[16 * 12];     /* 12 floats per vertex */
    int     vcount;
    /* Color/material snapshot at recording time. */
    float   color[4];
    float   material_ambdiff[4];
    bool    has_material;
    bool    lit;
} nczDL_Rec;

typedef struct {
    int       max_ops;
    int       n_ops;
    nczDL_Rec *ops;
} nczDL;

/* Begin recording a new list. Clears any previously recorded ops. */
int  ncz_dl_new(nczDL *dl);
void ncz_dl_end(nczDL *dl);

/* Record ops. ncz_dl_draw_chain records a gllist-chain draw;
 * ncz_dl_draw_inline records an inline immediate-mode batch (a whole
 * glBegin/glEnd pair captured by the recording-mode hooks in
 * ncz_im_begin / vertex / color / end — see ncz_im_set_recording). */
int  ncz_dl_draw_chain(nczDL *dl, const nczGLListChain *chain);
int  ncz_dl_draw_inline(nczDL *dl, GLenum primitive,
                        const float *verts, int vcount);

/* Replay: walks ops and emits the GLES3 draws. */
void ncz_dl_call(const nczDL *dl);

/* Free the recorded ops (the gllist chains are owned by the caller). */
void ncz_dl_free(nczDL *dl);

/* Recording-mode toggle. While recording, ncz_im_begin/end accumulate
 * into a per-call scratch buffer instead of drawing, and the recorder
 * decides whether to record the result as a GLLIST op (if a chain
 * was set with ncz_dl_set_chain_for_recording) or as an INLINE op. */
void ncz_im_set_recording(bool on);
void ncz_im_set_chain_for_recording(const nczGLListChain *chain);

/* ----------------------------------------------------------------------- */
/* Section 6 — Shaders                                                     */
/* ----------------------------------------------------------------------- */
/*
 * A single, small vertex+fragment shader pair covers the xscreensaver
 * GL1 usage footprint surveyed in this repo:
 *
 *   - per-vertex position + normal + color (RGBA) + texcoord (UV)
 *   - per-draw uniforms: MVP matrix, light direction+color, ambient,
 *     has-texture flag, base-color (material), tex sampler binding
 *   - GL_FLAT vs GL_SMOOTH shading (driven by `flat` qualifier)
 *   - GL_DEPTH_TEST, GL_CULL_FACE, GL_BLEND on/off
 *
 * No multi-texturing, no per-fragment lighting beyond a single diffuse
 * term, no normal mapping, no shadows, no fog. Those are explicitly
 * listed as Phase 2+ foundation changes in GLES3-MIGRATION-PHASE1.md.
 */

GLuint ncz_shader_program(void);    /* lazy-compiled singleton */

/* ----------------------------------------------------------------------- */
/* Section 7 — Init / teardown                                             */
/* ----------------------------------------------------------------------- */
/*
 * ncz_gles3_runtime_init — compile the shader, build the VAO/VBO
 * resources used by ncz_im_begin/.../end. Called once per process by
 * the GLES3 harness before the first init_<hack>.
 *
 * ncz_gles3_runtime_fini — frees the shader and any held GL objects.
 */

int  ncz_gles3_runtime_init(void);
void ncz_gles3_runtime_fini(void);

#endif /* NCZ_GLES3_COMPAT_H */
