/* ncz_render.h - render-resolution policy shared by all GLES3 hacks.
 *
 * The harness renders into a Wayland buffer that may be smaller than the
 * surface; wp_viewport upscales it in the compositor, so a 4K panel can run a
 * shader at 1080p with no extra GPU pass. This file holds the pure math.
 */
#ifndef NCZ_RENDER_H
#define NCZ_RENDER_H

/* Effective render size: native size times scale (clamped 0.25..1), then
 * height clamped to max_h (0 = unlimited) preserving aspect ratio, rounded to
 * even sizes, never larger than native. Returns 1 when the result is smaller
 * than native in either dimension, else 0 (and rw/rh equal native). */
int ncz_render_size(int nw, int nh, double scale, int max_h, int *rw, int *rh);

/* Default max render height by GPU class, from GL_RENDERER and the native
 * height: Mali (Sky1) always caps at 1080; integrated Intel caps at 1080 when
 * the native height exceeds 1440; everything else is unlimited (0). */
int ncz_render_platform_cap(const char *gl_renderer, int native_h);

/* Next step of the adaptive ladder 1.0 -> 0.75 -> 0.5 (below 0.5 stays). */
double ncz_render_step_down(double scale);

#endif
