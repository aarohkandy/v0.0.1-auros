/* press.c — see press.h. */
#include <stdlib.h>
#include "press.h"

void press_screen(press_state *p, int64_t now_ms, unsigned sig)
{
    if (sig != p->sig) { p->sig = sig; p->sig_ms = now_ms; }
}

int press_accept(press_state *p, int64_t now_ms, int x, int y)
{
    if (now_ms - p->sig_ms < PRESS_AFTER_CHANGE_MS) return 0;
    if (p->have_last && now_ms - p->last_ms < PRESS_AGAIN_MS &&
        abs(x - p->lx) <= PRESS_SAME_PX && abs(y - p->ly) <= PRESS_SAME_PX)
        return 0;
    p->last_ms = now_ms; p->lx = x; p->ly = y; p->have_last = 1;
    return 1;
}

int press_filter(press_state *p, int down, int64_t now_ms, int x, int y)
{
    if (down) {
        if (press_accept(p, now_ms, x, y)) { p->dead = 0; return 1; }
        p->dead = 1;
        return 0;
    }
    if (p->dead) { p->dead = 0; return 0; }
    return 1;
}
