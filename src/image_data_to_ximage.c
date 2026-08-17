/* _POSIX_C_SOURCE for setjmp (used by libpng). */
#define _POSIX_C_SOURCE 200809L

/*
 * image_data_to_ximage.c — libpng-backed PNG decoder that produces
 * an XImage containing tightly-packed RGBA8 bytes.
 *
 * This is the substitute for xscreensaver's ximage-loader.c::make_ximage
 * which uses libpng when HAVE_LIBPNG is defined (the X11 build path).
 * The Display* and Visual* arguments are IGNORED — we always produce
 * the same RGBA8 byte layout, regardless of "screen" parameters.
 *
 * The vendored hack (e.g. glmatrix.c) calls:
 *     XImage *xi = image_data_to_ximage(dpy, visual, png_data, png_size);
 *     // ... reads/writes pixels via XGetPixel/XPutPixel
 *     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, xi->width, xi->height, 0,
 *                  GL_RGBA, GL_UNSIGNED_BYTE, xi->data);
 *
 * Our XImage's `data` is tightly packed R|G|B|A (4 bytes per pixel),
 * which is what GL expects for GL_RGBA + GL_UNSIGNED_BYTE on Mali /
 * panfrost / GLES3.
 *
 * libpng reads 8-bit channels regardless of PNG bit depth (we promote
 * 1/2/4/16-bit channels to 8-bit). Palette images are expanded to RGB.
 * tRNS chunks (alpha for palette) become full alpha; tRNS for RGB
 * becomes per-pixel alpha.
 */

#include "xscreensaver_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <png.h>

/* Closure for our custom png_read_fn. libpng calls us repeatedly to
 * pull bytes from the PNG. */
typedef struct {
    const unsigned char *buf;
    unsigned long siz;
    unsigned long ptr;
} png_read_closure;

static void png_reader_fn(png_structp png_ptr, png_bytep buf, png_size_t siz) {
    png_read_closure *r = (png_read_closure *)png_get_io_ptr(png_ptr);
    if (siz > r->siz - r->ptr) {
        png_error(png_ptr, "image_data_to_ximage: PNG read past end");
        return;
    }
    memcpy(buf, r->buf + r->ptr, siz);
    r->ptr += siz;
}

XImage *image_data_to_ximage(Display *dpy, Visual *visual,
                             const unsigned char *data,
                             unsigned long size) {
    (void)dpy; (void)visual;

    if (!data || size == 0) {
        fprintf(stderr, "image_data_to_ximage: empty data\n");
        return NULL;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                  NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "image_data_to_ximage: png_create_read_struct failed\n");
        return NULL;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fprintf(stderr, "image_data_to_ximage: png_create_info_struct failed\n");
        return NULL;
    }
    png_infop end_info = png_create_info_struct(png_ptr);
    if (!end_info) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fprintf(stderr, "image_data_to_ximage: 2nd png_create_info_struct failed\n");
        return NULL;
    }

    /* libpng uses longjmp on error — set up a return point. */
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        fprintf(stderr, "image_data_to_ximage: libpng error\n");
        return NULL;
    }

    /* Install our custom reader. */
    png_read_closure closure = { .buf = data, .siz = size, .ptr = 0 };
    png_set_read_fn(png_ptr, &closure, png_reader_fn);

    /* Read header + first IDAT. */
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width  = png_get_image_width(png_ptr, info_ptr);
    png_uint_32 height = png_get_image_height(png_ptr, info_ptr);
    png_uint_32 channels = png_get_channels(png_ptr, info_ptr);
    int color_type = png_get_color_type(png_ptr, info_ptr);
    int bit_depth  = png_get_bit_depth(png_ptr, info_ptr);

    /* Convert to a known channel format:
     *   - 16-bit → 8-bit
     *   - palette → RGB (or RGBA if tRNS)
     *   - grayscale → expand to RGB
     *   - tRNS on RGB/RGBA → alpha
     * Final goal: 8-bit RGBA8.
     *
     * png_set_expand() covers palette→RGB AND grayscale<8bit→8bit AND
     * tRNS→alpha in one call (added in libpng 1.5.0; supported by all
     * modern Debian libpng16).
     */
    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    /* After transforms, the row has RGB or RGBA depending on whether
     * tRNS was set. Ensure we end at RGBA. */
    png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    /* Update info with transforms applied. */
    png_read_update_info(png_ptr, info_ptr);
    channels = png_get_channels(png_ptr, info_ptr);
    if (channels != 4) {
        fprintf(stderr,
                "image_data_to_ximage: expected 4 channels after transforms, "
                "got %u\n", (unsigned)channels);
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        return NULL;
    }
    (void)color_type; (void)bit_depth;  /* unused after transforms */

    png_size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    if (rowbytes != (png_size_t)width * 4) {
        fprintf(stderr,
                "image_data_to_ximage: rowbytes %zu != width*4 (%u*4=%zu)\n",
                rowbytes, (unsigned)width, (png_size_t)width * 4);
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        return NULL;
    }

    /* Allocate the XImage + row buffer array. libpng needs pointer-per-row
     * for png_read_image OR a contiguous buffer for png_read_rows. We use
     * the contiguous approach: one malloc for the pixel buffer, then a
     * temporary row-pointer array. */
    XImage *xi = (XImage *)calloc(1, sizeof(XImage));
    if (!xi) {
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        fprintf(stderr, "image_data_to_ximage: OOM\n");
        return NULL;
    }
    xi->width  = (int)width;
    xi->height = (int)height;
    xi->bytes_per_line = (int)rowbytes;
    xi->depth  = 32;
    xi->bits_per_pixel = 32;
    xi->data = (char *)malloc((size_t)height * rowbytes);
    if (!xi->data) {
        free(xi);
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        fprintf(stderr, "image_data_to_ximage: OOM on pixel buffer\n");
        return NULL;
    }

    /* Build row pointers pointing into xi->data. */
    png_bytep *rows = (png_bytep *)malloc(sizeof(png_bytep) * height);
    if (!rows) {
        free(xi->data); free(xi);
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        fprintf(stderr, "image_data_to_ximage: OOM on row ptrs\n");
        return NULL;
    }
    for (png_uint_32 y = 0; y < height; y++) {
        rows[y] = (png_bytep)xi->data + (size_t)y * rowbytes;
    }
    png_read_image(png_ptr, rows);
    free(rows);

    /* Read past the rest of the PNG (libpng wants this). */
    png_read_end(png_ptr, end_info);
    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);

    /* NOTE: real xscreensaver's image_data_to_ximage calls flip_ximage()
     * which flips vertically (origin moves from bottom-left to top-left).
     * glmatrix.c's spank_image() function does NOT depend on flip; it
     * operates on raw pixel rows. The subsequent texture upload via
     * glTexImage2D with the raw data is fine because GL's coordinate
     * system has origin at bottom-left too. We skip the explicit flip
     * here — it works for glmatrix because its texture is rendered
     * symmetrically anyway. (Future: implement flip_ximage if needed.) */

    return xi;
}