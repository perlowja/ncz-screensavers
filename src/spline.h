/*
 * Copyright (c) 1987, 1988, 1989 Stanford University
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representation about the suitability of this software
 * for any purpose is made.  It is provided "as is" without express or
 * implied warranty.
 *
 * STANFORD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 * IN NO EVENT SHALL STANABLE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* This code came with the InterViews distribution, and was translated
   from C++ to C by Matthieu Devin <devin@lucid.com> some time in 1992.
 */

/* ncz-screensavers shim: pull XPoint and XRectangle from the compat layer
 * (real Xlib is not present, but the geometry types are vendored into
 * xscreensaver_compat.h). The vendored implementations of the spline
 * functions live in /tmp/xscreensaver-6.15/utils/spline.c; jigsaw uses
 * spline.c unmodified. */

#ifndef _SPLINE_H_
#define _SPLINE_H_

#include "xscreensaver_compat.h"

typedef struct _spline
{
  /* input */
  unsigned int	n_controls;
  double*	control_x;
  double*	control_y;

  /* output */
  unsigned int		n_points;
  XPoint*	points;
  unsigned int		allocated_points;
} spline;

spline* make_spline (unsigned int size);
void compute_spline (spline* s);
void compute_closed_spline (spline* s);
void just_fill_spline (spline* s);
void append_spline_points (spline* s1, spline* s2);
void spline_bounding_box (spline* s, XRectangle* rectangle_out);
void free_spline(spline *s);

#endif /* _SPLINE_H_ */