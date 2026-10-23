/* osd.c — see osd.h. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "osd.h"
#include "notify.h"
#include "draw.h"

static osd_kind cur = OSD_NONE;
static int      cur_val;
static char     cur_line[160];
static rect     last_box;

void osd_show(osd_kind kind, int value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    cur = kind;
    cur_val = value;
}

void osd_say(const char *line)
{
    if (!line || !*line) return;
    snprintf(cur_line, sizeof cur_line, "%s", line);
    cur = OSD_SAID;
}

int  osd_visible(void) { return cur != OSD_NONE; }
void osd_press(void)   { cur = OSD_NONE; }
rect osd_box(void)     { return cur != OSD_NONE ? last_box : (rect){ 0, 0, 0, 0 }; }

void osd_paint(shell_ctx *c, surface *s, shell_fonts *f)
{
    last_box = (rect){ 0, 0, 0, 0 };
    if (cur == OSD_NONE) return;
    font *ft = f->mid ? f->mid : f->small;
    if (!ft) return;

    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
    char text[176];
    if (cur == OSD_MUTED)           snprintf(text, sizeof text, "Sound off");
    else if (cur == OSD_VOLUME)     snprintf(text, sizeof text, "Sound %d%%", cur_val);
    else if (cur == OSD_BRIGHTNESS) snprintf(text, sizeof text, "Screen %d%%", cur_val);
    else                            snprintf(text, sizeof text, "%s", cur_line);
    /* The number AND a bar: a bar alone cannot be compared with the one
     * she saw a moment ago, a number alone does not show "how much". */
    int barw = (cur == OSD_VOLUME || cur == OSD_BRIGHTNESS) ? (int)(120.f * k) : 0;
    int pad = (int)(12.f * k); if (pad < 12) pad = 12;
    int m = c->margin > 8 ? c->margin : 8;

    /* The strip: the look's own, left of the clock -- or, with the
     * clock down in the bar, the space just above the bar. */
    rect ck = c->clock_r;
    int has = ck.w > 0 && ck.h > 0;
    int low = has && ck.y > c->screen_h / 2;
    int sh  = has ? ck.h : (c->bar_h > 0 ? c->bar_h : 40);
    int right = !has ? s->w - m : low ? ck.x + ck.w - m : ck.x - 8;
    float room = (float)(right - m - 2 * pad - (barw ? barw + pad : 0));
    /* Above the bar it takes a corner, and the programs list opens at
     * the other end of it: so no more than 40% of the width there (a
     * long sentence wraps), and up to 70% in a strip, which is empty
     * between the name at one end and the clock at the other. */
    float cap = (float)s->w * (low ? 0.40f : 0.70f) - (float)(2 * pad);
    if (room > cap) room = cap;
    float lh = font_line_height(ft);
    int lines = notify_wrap(NULL, ft, 0, 0, room, lh, 4, text, 0, 0);
    if (lines < 1) lines = 1;
    float tw = lines > 1 ? room : shell_text_w(ft, text);
    int w = (int)tw + 2 * pad + (barw ? barw + pad : 0);
    int h = sh - 8;
    if (lines > 1 || h < (int)lh + 8) h = lines * (int)lh + 2 * pad;
    int x = right - w;
    int y = !has ? 4 : low ? ck.y - 8 - h : ck.y + 4;
    if (y < 0) y = 0;

    last_box = (rect){ x, y, w, h };
    draw_round_rect(s, last_box, corners_all(8.f), c->surface_c, 1.f);
    draw_round_rect_border(s, last_box, corners_all(8.f), 1.f, c->overlay, 1.f);
    float by = lines > 1 ? (float)(y + pad) + font_ascent(ft)
                         : shell_baseline(ft, (float)y, (float)h);
    notify_wrap(s, ft, (float)(x + pad), by, room, lh, 4, text, c->fg_hi, 1.f);
    if (barw) {
        int bh = (int)(10.f * k); if (bh < 8) bh = 8;
        rect trough = { x + w - pad - barw, y + (h - bh) / 2, barw, bh };
        draw_rect(s, trough, c->bg_alt, 1.f);
        draw_frame(s, trough, 1, c->overlay, 1.f);
        rect fill = trough;
        fill.w = trough.w * cur_val / 100;
        if (fill.w > 0) draw_rect(s, fill, c->fg_hi, 1.f);
    }
}
