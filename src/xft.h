/* xscreensaver, Copyright © 2014-2022 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

/* Minimal xft.h shim for the ncz-screensavers compositor build.
 *
 * Upstream's xft.h is the real Xft compatibility header — when neither
 * HAVE_XFT nor HAVE_COCOA/HAVE_ANDROID is defined, its tail-branch pulls
 * in <X11/Xlib.h>. That collides with the shim's opaque Display/Window/
 * Drawable/Pixmap typedefs in xscreensaver_compat.h:
 *
 *     /usr/include/X11/X.h:102:13: error: conflicting types for 'Pixmap';
 *     have 'XID' {aka 'long unsigned int'}
 *
 * That single conflict blocks every vendored hack that #include's
 * "texfont.h" (which #include's "xft.h") — i.e. dnalogo, geodesicgears,
 * gibson, glsnake, juggler3d, mapscroller, molecule, pinion, splitflap,
 * tangram, winduprobot, fliptext, skulloop, spheremonics, unicrud.
 *
 * We do not compile texfont.c / xft.c at all. The vendored hacks never
 * call XftFontOpenXlfd / XftDrawStringUtf8 / etc. directly; they only
 * pass through the texture_font_data wrapper in texfont.h, whose
 * load_texture_font / print_texture_string / print_texture_label /
 * free_texture_font we stub in xscreensaver_compat.c (returning NULL /
 * no-op, so the hacks skip the text-rendering path).
 *
 * Therefore we need just enough here to make texfont.h and the vendored
 * hack .c files accept the XCharStruct type and the conversion macros.
 * XftFont, XftColor, XftDraw, XRenderColor, XGlyphInfo, FcChar8 etc.
 * stay as forward declarations so that IF something in the future needs
 * to touch them, the compiler won't reject the identifier outright.
 */

#ifndef __XSCREENSAVER_XFT_H__
#define __XSCREENSAVER_XFT_H__

/* The XGlyphInfo field names and values are, of course, arbitrarily
   different from XCharStruct for no sensible reason.  These macros
   translate between them.
 */
# define XGlyphInfo_to_XCharStruct(G,C) do {		\
    (C).lbearing  =  -(G).x;				\
    (C).rbearing  =   (G).width - (G).x;		\
    (C).ascent    =   (G).y;				\
    (C).descent   =   (G).height - (G).y;		\
    (C).width     =   (G).xOff;				\
} while (0)

# define XCharStruct_to_XGlyphInfo(C,G) do {		\
    (G).x         =  -(C).lbearing;			\
    (G).y         =   (C).ascent;			\
    (G).xOff      =   (C).width;			\
    (G).yOff      =   0;				\
    (G).width     =   (C).rbearing - (C).lbearing;	\
    (G).height    =   (C).ascent   + (C).descent;	\
} while (0)

/* Xutf8TextExtents returns a bounding box in an XRectangle, which
   conveniently interprets everything in the opposite direction
   from XGlyphInfo!
 */
# define XCharStruct_to_XmbRectangle(C,R) do {		\
    (R).x         =   (C).lbearing;			\
    (R).y         =  -(C).ascent;			\
    (R).width     =   (C).rbearing - (C).lbearing;	\
    (R).height    =   (C).ascent   + (C).descent;	\
} while (0)

# define XmbRectangle_to_XCharStruct(R,C,ADV) do {	\
    (C).lbearing  =   (R).x;				\
    (C).rbearing  =   (R).width + (R).x;		\
    (C).ascent    =  -(R).y;				\
    (C).descent   =   (R).height + (R).y;		\
    (C).width     =   (ADV);				\
} while (0)


#ifndef _Xconst
# define _Xconst const
#endif

/* XCharStruct — the only Xft-derived type the vendored GL hacks actually
 * touch. They declare it as a local variable and pass it to print_texture_*
 * / texture_string_metrics. We carry it as a complete struct so
 * sizeof(XCharStruct) is meaningful. Field semantics match Xlib:
 *   lbearing / rbearing: distance from origin to left/right edge of ink
 *   width:               distance from origin to next origin (== advance)
 *   ascent / descent:    vertical extents above/below the baseline
 */
typedef struct {
    short           lbearing;
    short           rbearing;
    short           width;
    short           ascent;
    short           descent;
    unsigned short  attributes;
} XCharStruct;

/* Forward declarations for the rest. Vendored hacks do not touch these
 * directly; texfont.c, which would, is not compiled by us. If any
 * future hack does reach for one, the linker will tell us — and the
 * shape can be filled in here or stubbed in xscreensaver_compat.c. */
typedef struct _XGlyphInfo {
    unsigned short width, height;
    short x, y;
    short xOff, yOff;
} XGlyphInfo;

typedef struct _XFontStruct {
    /* unused -- declared as a type by xft.h but never dereferenced here. */
    int _placeholder;
} XFontStruct;

typedef struct _XftFont {
    XFontStruct *xfont;
    char *name;
    int ascent;
    int descent;
    int height;
} XftFont;
typedef struct _XftColor   XftColor;
typedef struct _XftDraw    XftDraw;
typedef struct _XftPattern XftPattern;
typedef struct {
    unsigned short red, green, blue, alpha;
} XRenderColor;

typedef unsigned char FcChar8;
typedef struct _FcPattern    FcPattern;

#endif /* __XSCREENSAVER_XFT_H__ */
/* Function stubs. Vendored hacks (xftwrap.c, texfont.c) reference these.
 * Our build never actually calls into real Xft; the stub implementations
 * in xscreensaver_compat.c return NULL/no-op so the hacks compile and
 * link, and the rendering paths that depend on real Xft silently no-op.
 *
 * These declarations were removed from upstream's vendored copy because
 * we expected to avoid compiling texfont.c / xftwrap.c at all. We have
 * since added them back as stubs for hacks like photopile that need
 * xftwrap.c's word-wrap helpers. */
XftFont *XftFontOpenXlfd(Display *dpy, int screen, const char *xlfd);
XftFont *XftFontOpenName(Display *dpy, int screen, const char *name);
void     XftFontClose(Display *dpy, XftFont *font);
Bool     XftColorAllocName(Display *dpy, void *visual, Colormap cmap,
                           const char *name, XftColor *result);
Bool     XftColorAllocValue(Display *dpy, void *visual, Colormap cmap,
                            const XRenderColor *color, XftColor *result);
void     XftColorFree(Display *dpy, void *visual, Colormap cmap,
                      XftColor *color);
XftDraw *XftDrawCreate(Display *dpy, void *drawable, void *visual,
                       Colormap colormap);
Display *XftDrawDisplay(XftDraw *draw);
void     XftDrawDestroy(XftDraw *draw);
void     XftTextExtentsUtf8(Display *dpy, XftFont *pub,
                            const unsigned char *string, int len,
                            XGlyphInfo *extents);
void     XftDrawStringUtf8(XftDraw *draw, const XftColor *color,
                           XftFont *pub, int x, int y,
                           const unsigned char *string, int len);

