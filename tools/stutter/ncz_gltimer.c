/*
 * LD_PRELOAD frame-pacing / GL-call timing shim for stutter analysis.
 *
 *   gcc -O2 -shared -fPIC -o ncz_gltimer.so ncz_gltimer.c -ldl
 *   LD_PRELOAD=./ncz_gltimer.so NCZ_GLTIMER_OUT=/path/tag <hack>
 *
 * Writes <tag>.frames ("t_ms interval_ms swap_call_ms" per eglSwapBuffers)
 * and <tag>.events (every timed call slower than NCZ_GLTIMER_SLOW_MS, default
 * 8). Analyze with frame-stats.py. Needs no GL/EGL headers on purpose so it
 * builds on hosts without -dev packages.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef unsigned EGLBoolean;
typedef unsigned GLuint, GLenum;
typedef int GLint, GLsizei;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static double t0 = -1, last_swap = -1, slow_ms = 8.0;
static FILE *fr, *ev;
static unsigned long nframes;

static void flush_all(void)
{
    if (fr) fflush(fr);
    if (ev) fflush(ev);
}

static void onsig(int n)
{
    (void)n;
    _exit(0); /* streams are line-buffered: nothing to flush, and fflush is not async-signal-safe */
}

__attribute__((constructor)) static void init(void)
{
    const char *p = getenv("NCZ_GLTIMER_OUT"), *s = getenv("NCZ_GLTIMER_SLOW_MS");
    char path[512];
    if (s && atof(s) >= 0.5) slow_ms = atof(s);
    if (p) {
        snprintf(path, sizeof path, "%s.frames", p);
        fr = fopen(path, "w");
        snprintf(path, sizeof path, "%s.events", p);
        ev = fopen(path, "w");
        if (fr) setvbuf(fr, NULL, _IOLBF, 0);
        if (ev) setvbuf(ev, NULL, _IOLBF, 0);
    }
    signal(SIGINT, onsig);
    signal(SIGTERM, onsig);
    atexit(flush_all);
}

static void slow(const char *name, double t, double d)
{
    if (d > slow_ms && ev)
        fprintf(ev, "SLOW %s t=%.1f dur=%.2f\n", name, t - (t0 < 0 ? t : t0), d);
}

EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface s)
{
    static EGLBoolean (*real)(EGLDisplay, EGLSurface);
    if (!real) real = dlsym(RTLD_NEXT, "eglSwapBuffers");
    double a = now_ms();
    if (t0 < 0) t0 = a;
    EGLBoolean r = real(d, s);
    double b = now_ms();
    if (last_swap >= 0 && fr) {
        fprintf(fr, "%.2f %.3f %.3f\n", a - t0, a - last_swap, b - a);
        nframes++;
    }
    last_swap = a;
    slow("eglSwapBuffers", a, b - a);
    return r;
}

#define TIMED(ret, name, params, args)                                     \
    ret name params                                                        \
    {                                                                      \
        static ret (*real) params;                                         \
        if (!real) real = dlsym(RTLD_NEXT, #name);                         \
        double a = now_ms();                                               \
        ret r = real args;                                                 \
        slow(#name, a, now_ms() - a);                                      \
        return r;                                                          \
    }
#define TIMED_V(name, params, args)                                        \
    void name params                                                       \
    {                                                                      \
        static void (*real) params;                                        \
        if (!real) real = dlsym(RTLD_NEXT, #name);                         \
        double a = now_ms();                                               \
        real args;                                                         \
        slow(#name, a, now_ms() - a);                                      \
    }

TIMED_V(glCompileShader, (GLuint s), (s))
TIMED_V(glLinkProgram, (GLuint p), (p))
TIMED_V(glFinish, (void), ())
TIMED_V(glReadPixels,
        (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p),
        (x, y, w, h, f, t, p))
TIMED_V(glTexImage2D,
        (GLenum tg, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f,
         GLenum t, const void *p),
        (tg, l, i, w, h, b, f, t, p))
TIMED_V(glTexSubImage2D,
        (GLenum tg, GLint l, GLint xo, GLint yo, GLsizei w, GLsizei h,
         GLenum f, GLenum t, const void *p),
        (tg, l, xo, yo, w, h, f, t, p))
