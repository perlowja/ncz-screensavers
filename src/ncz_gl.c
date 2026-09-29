/* ncz_gl.c - EGL config choice and diagnostic helpers (see ncz_gl.h). */
#include "ncz_gl.h"
#include <stdio.h>

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
    if (!eglChooseConfig(dpy, cfg_attr, NULL, 0, &num) || num < 1)
        return NULL;
    EGLConfig cfg;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &num) || num < 1)
        return NULL;
    return cfg;
}

int ncz_diag_periodic_samples(void) {
    static int v = -1;
    if (v < 0) {
        const char *d = getenv("NCZ_DIAG_FRAMEBUFFER");
        v = (d && *d && strcmp(d, "0") != 0) ? 1 : 0;
    }
    return v;
}
