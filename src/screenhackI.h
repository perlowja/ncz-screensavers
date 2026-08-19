/* screenhackI.h — compatibility shim.
 *
 * Upstream's copy is xscreensaver internals: it includes <X11/Xlib.h>,
 * <X11/Xutil.h>, <GL/glu.h> and "xft.h". Vendoring it drags the real X11
 * headers into a build that deliberately replaces them, which shows up as
 * "conflicting types for Drawable" and as fatal errors for headers that are
 * not installed. Everything the GL hacks need from it is in the shim.
 */
#ifndef NCZ_SCREENHACKI_H
#define NCZ_SCREENHACKI_H
#include "xscreensaver_compat.h"
#endif
