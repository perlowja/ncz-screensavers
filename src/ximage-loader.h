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

#endif /* NCZ_XIMAGE_LOADER_H */
