/* press.h — one press is one press.
 *
 * Her habit is the one Windows taught her: double-click it. Here a
 * double-click is two presses, and the second lands on whatever the
 * first one just put on the screen -- on "Settings" it presses a row of
 * the page that opened; on "Next" it presses the button where "Next"
 * was. DESIGN-DIRECTION §3 (Input): ignore every press for 700 ms after
 * the screen she is looking at changes, and a second press on the same
 * control within 500 ms. The press that is ignored is ignored whole,
 * its release included.
 *
 * Pure: a clock and a few numbers in, an answer out, so tools/presstest.c
 * can check it without a screen or a mouse.
 */
#ifndef AUROS_PRESS_H
#define AUROS_PRESS_H
#include <stdint.h>

#define PRESS_AFTER_CHANGE_MS 700
#define PRESS_AGAIN_MS        500
#define PRESS_SAME_PX         24     /* "the same control", near enough */

typedef struct {
    unsigned sig;        /* what the screen was last time it was asked */
    int64_t  sig_ms;     /* when it last changed                       */
    int64_t  last_ms;    /* the last press that was let through        */
    int      lx, ly;
    int      have_last;
    int      dead;       /* the press now down was ignored             */
} press_state;

/* Every pass of the loop: what screen is she on (any number that
 * differs when a panel, page or question differs)? */
void press_screen(press_state *p, int64_t now_ms, unsigned sig);

/* A press went down at x,y. 1: let it through. 0: ignore it. */
int  press_accept(press_state *p, int64_t now_ms, int x, int y);

/* A button event, down (1) or up (0). 1: handle it. 0: drop it. */
int  press_filter(press_state *p, int down, int64_t now_ms, int x, int y);
#endif
