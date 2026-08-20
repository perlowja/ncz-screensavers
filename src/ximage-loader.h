/*
 * Minimal ximage-loader bridge for vendored xscreensaver hacks.
 *
 * Upstream's ximage-loader.h declares image_data_to_ximage() and related X11
 * image helpers. Our compositor-driven build keeps the texture path live via
 * src/image_data_to_ximage.c, which decodes embedded PNG data into the shim's
 * XImage representation.
 */
#ifndef NCZ_XIMAGE_LOADER_H
#define NCZ_XIMAGE_LOADER_H

#include "xscreensaver_compat.h"

/* Forward declaration: file_to_ximage is implemented as a NULL stub
 * in xscreensaver_compat.c. Vendored hacks (mapscroller, lavalite,
 * timetunnel, gleidescope, pulsar, maze3d, extrusion, worldpieces,
 * glplanet, unknownpleasures) all check the return and skip texture
 * rendering when it fails. */
extern XImage *file_to_ximage(Display *dpy, Visual *visual,
                              const char *filename);

#endif /* NCZ_XIMAGE_LOADER_H */
