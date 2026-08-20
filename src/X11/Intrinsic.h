/* Minimal X11/Intrinsic.h shim for the ncz-screensavers build.
 *
 * xscreensaver's mapscroller.c uses the X Toolkit (Xt) API to spawn a
 * child process and watch its stdout pipe for new text content.
 * We don't have Xt installed and have no X11 display, so this shim
 * provides just enough typedefs and stub functions for mapscroller.c
 * to compile and link. The actual spawn-and-watch logic degrades
 * gracefully -- the loader_cb never fires -- so the screensaver runs
 * and just doesn't load any external text.
 *
 * Headers we mimic:
 *   <X11/Intrinsic.h> -- Widget, XtPointer, XtInputId, XtInputReadMask,
 *                         XtInputExceptMask, XtAppAddInput,
 *                         XtRemoveInput, XtDisplayToApplicationContext
 *
 * This file is reached via #include <X11/Intrinsic.h>. The vendored
 * hack guards the include with `#ifndef HAVE_COCOA`; we don't define
 * HAVE_COCOA so the include is taken, but the resulting types and
 * symbols are all satisfied by the stubs below.
 */
#ifndef _NCZ_INTRINSIC_SHIM_H
#define _NCZ_INTRINSIC_SHIM_H

#include "xscreensaver_compat.h"

typedef void *XtPointer;
typedef int    XtInputId;
typedef struct _XtAppContext *XtAppContext;

#define XtInputReadMask   0x01
#define XtInputExceptMask 0x02

XtAppContext XtDisplayToApplicationContext(Display *dpy);
XtInputId    XtAppAddInput(XtAppContext app,
                           int           source,
                           XtPointer     condition,
                           void        (*proc)(XtPointer, int *, XtInputId *),
                           XtPointer     closure);
void         XtRemoveInput(XtInputId id);

#endif /* _NCZ_INTRINSIC_SHIM_H */
/* ConnectionNumber(dpy) returns the fd for select()ing on the X11
 * connection. We don't have one, so just hand back 0 -- mapscroller
 * uses this only on the rare init-failure path, where the pipe-fd
 * cleanup loop never runs. */
#ifndef ConnectionNumber
# define ConnectionNumber(dpy) 0
#endif

/* XrmDatabase / XrmValue / XrmPutResource / XtDatabase. These are X
 * resource manager types. photopile.c only uses XrmPutResource in a
 * debug-mode codepath; we stub them and return success so the
 * conditionals compile and the runtime branch is taken. */
typedef struct _XrmHashBucketRec *XrmDatabase;
typedef struct {
    unsigned int size;
    void *addr;
} XrmValue;
#define XrmPutResource(db, name, type, value)  /* no-op */
#define XtDatabase(dpy) ((XrmDatabase) 0)
