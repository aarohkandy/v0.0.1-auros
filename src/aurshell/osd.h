/* osd.h — the line that answers a key she just pressed.
 *
 * Volume and brightness are the two controls on a laptop that are
 * already physical: they are printed on the keyboard and people press
 * them without being taught. What they have never worked without is
 * feedback. A brightness key that changes the screen tells you what it
 * did; a volume key in a silent room tells you nothing at all, and the
 * person presses it eleven more times. The same line says where a
 * picture of the screen was saved, and what could not be done.
 *
 * NOTHING VANISHES (docs/DESIGN-DIRECTION.md §5 C6). It used to go by
 * itself after 1.4 seconds, or 3.2 for a sentence -- the one place in
 * the product that broke docs/EASY.md rule 5 on purpose, and a
 * sentence taken away from a slow reader halfway through. Now it stays
 * until her next press, and that press is NOT taken: it still reaches
 * what she pressed. So it is never in the way, and it is never gone
 * before she has read it.
 *
 * It sits in a strip that covers nothing: inside the look's own strip,
 * just left of the clock, where there is only empty strip; or, where
 * the clock is down in the bar ("Like Windows"), directly above the
 * bar at the right, a space the card keeps clear for it (notify.c).
 */
#ifndef AUROS_OSD_H
#define AUROS_OSD_H

#include "shell.h"

/* What it is showing. */
typedef enum { OSD_NONE, OSD_VOLUME, OSD_BRIGHTNESS, OSD_MUTED,
               /* One line, no bar: where a picture was saved, what
                * could not be done. */
               OSD_SAID } osd_kind;

/* Show one. `value` is 0..100. */
void osd_show(osd_kind kind, int value);
void osd_say(const char *line);

/* True while one is on screen. */
int  osd_visible(void);

/* Her next press, pointer or key. The message goes; the press is not
 * taken -- the host carries on delivering it to whatever she pressed. */
void osd_press(void);

/* Where it was last painted, screen pixels; w 0 when nothing is up. */
rect osd_box(void);

/* Painted last, in its strip; it never reaches the band. */
void osd_paint(shell_ctx *c, surface *s, shell_fonts *f);

#endif
