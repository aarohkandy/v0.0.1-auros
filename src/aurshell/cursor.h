/* cursor.h — the pointer's pictures.
 *
 * Drawn by us because there is no compositor to do it, and drawn LARGE:
 * the pointer is how she knows what she can click. A hand over a link and
 * an I-beam over a text box are the two things that tell her so without a
 * word; a pointer that is always the same arrow, or too small to find, is
 * the fastest way to make someone believe the machine has frozen.
 *
 * Two kinds of picture:
 *   glyphs    our own, black with a white outline, whole-number scaled so
 *             the edges stay sharp, ~30 px at normal text and 60 at 200%
 *             (DESIGN-DIRECTION §3, Input). Used for the arrow and for
 *             every shape a program asks for BY NAME (wp_cursor_shape_v1).
 *   images    the picture a program sets itself (wl_pointer.set_cursor):
 *             Firefox and every GTK3/Qt5 program load their cursor theme
 *             and hand us the result. Drawn at the program's pixels, scaled
 *             up or down so that the cursor canvas is as large as ours.
 */
#ifndef AUROS_CURSOR_H
#define AUROS_CURSOR_H

#include "draw.h"

enum { CUR_ARROW, CUR_HAND, CUR_IBEAM, CUR_CROSS, CUR_WAIT, CUR_NO, CUR_MOVE,
       CUR_EW, CUR_NS, CUR_NESW, CUR_NWSE, CUR_COUNT };

/* How many screen pixels one glyph pixel is, from the text-size setting:
 * 2 at normal, 3 at 150%, 4 at 200% and over. */
int  cursor_scale(float text_scale);

/* Which glyph a wp_cursor_shape_device_v1 shape number is. Every number a
 * protocol-valid client can send (1..34) has one; anything else is the arrow. */
int  cursor_class_of_shape(int shape);

/* Size of a glyph at scale `sc`, and where its hotspot is in it. */
void cursor_glyph_size(int cls, int sc, int *w, int *h, int *hot_x, int *hot_y);

/* The glyph with its hotspot at (x,y). Clipped to `s`. */
void cursor_paint_glyph(surface *s, int cls, int x, int y, int sc);

/* A program's own picture (premultiplied ARGB, as wl_shm gives it) with its
 * hotspot at (x,y). The picture's longer side is scaled to 24*sc pixels
 * (48 at normal text) unless it is already larger than 96: a theme cursor
 * is then as big as ours whatever size the program thought it was asked
 * for, and a big custom picture is left alone. Within 6% of a size is not
 * rescaled at all: a resample costs sharpness and gains nothing. */
void cursor_paint_image(surface *s, const surface *img, int hot_x, int hot_y, int x, int y, int sc);

/* The scale `cursor_paint_image` would draw an image of this size at, x1000
 * (1000 = unscaled). Exposed for the test. */
int  cursor_image_scale_milli(int w, int h, int sc);

#endif
