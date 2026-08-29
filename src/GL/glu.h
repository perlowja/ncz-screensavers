/* Minimal GL/glu.h for the GLES3-native (NCZ_GLES3_BUILD) path.
 *
 * The vendored xscreensaver jigsaw.c unconditionally includes <GL/glu.h>.
 * GLES3 has no GLU, libglu1-mesa-dev is not installed on the build host, and
 * linking the real libGLU would pull in libGL.so.1 and break the
 * libEGL.so.1 + libGLESv2.so.2-only contract these ports are held to.
 *
 * What this header covers:
 *   - The GLUtesselator / _GLUfuncptr / GLU_TESS_* surface jigsaw references
 *     inside its HAVE_TESS block. jigsaw_gles3 is built without tessellation,
 *     so nothing in that family is referenced at link time; the declarations
 *     exist only so the file parses.
 *   - gluPerspective / gluLookAt, which jigsaw genuinely calls at reshape.
 *     These resolve to the real implementations in xscreensaver_compat.c --
 *     the same ones the gl4es path uses, built on glMultMatrixf and
 *     glTranslated, both of which gles3_compat.c provides for GLES3.
 *
 * Scope: local include path only, for hacks that need it. If libGLU ever
 * appears on a build host, hacks that legitimately want it should get the
 * real header rather than this one.
 */
#ifndef NCZ_GLES3_GLU_STUB_H
#define NCZ_GLES3_GLU_STUB_H

typedef struct GLUtesselator GLUtesselator;
typedef void (*_GLUfuncptr)(void);

#define GLU_TESS_BEGIN       100100
#define GLU_TESS_VERTEX_DATA 100105
#define GLU_TESS_END         100102
#define GLU_TESS_COMBINE     100105
#define GLU_TESS_ERROR       100103

#ifdef __cplusplus
extern "C" {
#endif

GLUtesselator *gluNewTess(void);
void           gluDeleteTess(GLUtesselator *tess);
void           gluTessCallback(GLUtesselator *tess, GLenum which, _GLUfuncptr fn);
void           gluTessBeginPolygon(GLUtesselator *tess, void *data);
void           gluTessBeginContour(GLUtesselator *tess);
void           gluTessVertex(GLUtesselator *tess, GLdouble *coords, void *data);
void           gluTessEndContour(GLUtesselator *tess);
void           gluTessEndPolygon(GLUtesselator *tess);

const GLubyte *gluErrorString(GLenum error);

void gluPerspective(GLdouble fovy, GLdouble aspect,
                    GLdouble zNear, GLdouble zFar);
void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
               GLdouble cx, GLdouble cy, GLdouble cz,
               GLdouble ux, GLdouble uy, GLdouble uz);

#ifdef __cplusplus
}
#endif

#endif /* NCZ_GLES3_GLU_STUB_H */