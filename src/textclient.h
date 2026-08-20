/* xscreensaver, Copyright © 2012-2025 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 *
 * Running "xscreensaver-text" and returning bytes from it.
 */

#ifndef __TEXTCLIENT_H__
#define __TEXTCLIENT_H__

# ifdef HAVE_IPHONE
#  undef HAVE_FORKPTY
# endif

/* Pull Display/Bool/XKeyEvent from the shim. textclient.h in upstream
 * does not include anything -- the include chain ran through
 * xlockmore.h -> screenhackI.h -> X11/Xlib.h. In our build the
 * shim is the substitute for both. */
#include "xscreensaver_compat.h"

typedef struct text_data text_data;

extern text_data *textclient_open (Display *);
extern void textclient_close (text_data *);
extern void textclient_reshape (text_data *,
                                int pix_w, int pix_h,
                                int char_w, int char_h,
                                int max_lines);
extern int textclient_getc (text_data *);
extern Bool textclient_puts (text_data *, const char *);
extern Bool textclient_putc_event (text_data *, XKeyEvent *);

# if defined(HAVE_IPHONE) || defined(HAVE_ANDROID)
extern char *textclient_mobile_date_string (void);
extern char *textclient_mobile_url_string (Display *, const char *url);
# endif

#endif /* __TEXTCLIENT_H__ */
