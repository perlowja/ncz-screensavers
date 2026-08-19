/*
 * Minimal grab-ximage bridge for vendored xscreensaver hacks.
 *
 * Upstream uses this API to asynchronously grab the X window contents into a
 * GL texture. The compositor port has no X server to capture, so the compat
 * shim supplies a deterministic fallback texture and preserves the callback
 * contract.
 */
#ifndef NCZ_GRAB_XIMAGE_H
#define NCZ_GRAB_XIMAGE_H

#include "xscreensaver_compat.h"

#endif /* NCZ_GRAB_XIMAGE_H */
