/* layouts/tiles.c — the "One thing at a time" archetype.
 *
 * Boot lands on a page of large labelled buttons. Press one and it grows
 * into the whole screen. A single Home control, always in the same place
 * along the bottom, brings the page back. Nothing overlaps, nothing
 * minimises, nothing is ever hidden behind anything else.
 *
 * The point: for someone whose only computer is a phone, "a desktop" is
 * a second thing to learn before they can do the first thing — windows
 * that move, stack, shrink to an icon and reappear somewhere else. This
 * archetype asks them to learn nothing: it is a phone home screen at
 * desk size, and it behaves the way their thumb already expects.
 *
 * The cost is on the tin in tiles.shell and is real: you cannot see two
 * things at once, and switching goes via Home. What it buys is a
 * guarantee no other archetype here can make — there is exactly ONE
 * control for moving around, it is always drawn, it is always in the
 * same pixels, and nothing can ever cover it. Every other way of getting
 * lost needs a second place for things to be. This has no second place.
 */
#include "../shell.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define OPEN_SECS   0.42f
#define CLOSE_SECS  0.34f

typedef struct {
    tween open;      /* 0 = the page of buttons, 1 = one thing, full screen */
    int   win;       /* the window being shown or shrinking, -1 for none    */
    int   page;      /* which page of buttons                               */
    int   hot;       /* app index under the pointer / key cursor, -1 none   */
    int   home_hot;  /* pointer is over the Home control                    */
    int   nav_hot;   /* 0 previous page, 1 next page, -1 neither            */
} tiles_priv;

static tiles_priv *P(shell_ctx *c) { return (tiles_priv *)c->priv; }

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerpf (float a, float b, float t) { return a + (b - a) * t; }

/* Font metrics with fallbacks, because a theme may name a font that did
 * not load and a NULL font must degrade to a plausible layout rather
 * than to nonsense. Ascent and descent only: every block in this file
 * is measured as the ink the glyphs occupy, never as line height, so
 * that a measurement and the baseline derived from it cannot disagree. */
static float fa (font *f, float d) { return f ? font_ascent(f)  : d; }
static float fdc(font *f, float d) { return f ? font_descent(f) : d; }

/* Blend two THEME colours channel-wise. Not a hardcoded colour: both
 * ends come from the theme, and only the point between them is
 * computed. Used where a label has to stay one crisp glyph while the
 * surface under it changes colour — drawing the same word twice in two
 * colours at half alpha gives two ghosts and no contrast. */
static uint32_t mix_rgb(uint32_t a, uint32_t b, float t)
{
    float k = clampf(t, 0.f, 1.f);
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float ca = (float)((a >> sh) & 0xFFu), cb = (float)((b >> sh) & 0xFFu);
        out |= ((uint32_t)(ca + (cb - ca) * k) & 0xFFu) << sh;
    }
    return out;
}

static rect rect_lerp(rect a, rect b, float t)
{
    rect r;
    r.x = (int)lerpf((float)a.x, (float)b.x, t);
    r.y = (int)lerpf((float)a.y, (float)b.y, t);
    r.w = (int)lerpf((float)a.w, (float)b.w, t);
    r.h = (int)lerpf((float)a.h, (float)b.h, t);
    return r;
}

/* Paint-only: used to let the page recede while something opens over it.
 * Deliberately NOT part of tile_rect — the grid is not hit-testable
 * during the transition, so a scale that only exists in the painter can
 * never put a click somewhere the user did not aim. */
static rect rect_scale(rect r, float k)
{
    rect o;
    o.w = (int)((float)r.w * k); o.h = (int)((float)r.h * k);
    o.x = r.x + (r.w - o.w) / 2; o.y = r.y + (r.h - o.h) / 2;
    return o;
}

/* ── the two permanent bands ─────────────────────────────────────────
 * Both are chrome, both are painted last, and the open thing's rect is
 * defined to stop short of them. That is the whole anti-dead-end
 * mechanism, expressed as arithmetic rather than as a promise: there is
 * no value of anything for which Home is off screen or covered. */
static int  strip_h  (shell_ctx *c) { return c->bar_h + 8; }
static int  homebar_h(shell_ctx *c) { return c->target_large ? 84 : 72; }
/* The question, at its display size, above the page of buttons (and, with
 * it, the page buttons). Up to 125% text; past that the words are big
 * enough on their own, the question goes and the page buttons move down
 * into the bar, so the tiles keep the room. */
#define HEAD_H_BASE 56
static float tk(const shell_ctx *c) { return c->text_scale > 1.f ? c->text_scale : 1.f; }
static int head_on(const shell_ctx *c) { return tk(c) <= 1.25f; }
static int head_h(const shell_ctx *c) { return head_on(c) ? HEAD_H_BASE : 0; }

static rect homebar_rect(shell_ctx *c, int w, int h)
{
    rect r = { 0, h - homebar_h(c), w, homebar_h(c) };
    return r;
}

static rect home_btn_rect(shell_ctx *c, int w, int h)
{
    int bh  = homebar_h(c);
    int hgt = bh - 16;                 /* 56 px: 48 is the floor */
    int wid = c->target_large ? 240 : 200;
    int max = w - c->margin * 4;
    if (wid > max) wid = max > 60 ? max : 60;
    /* OFF the horizontal centre, and constant.
     *
     * The archetype's promise is that Home is one target, one size, in
     * one place, in every state — and that promise says nothing about
     * the middle. It used to sit at (w - wid)/2, which was the fourth
     * separate decision on this one screen to be perfectly symmetric
     * about the vertical axis. Here it sits on the grid's own left
     * measure instead, so the one permanent control lines up with the
     * column of buttons above it. Aligned to something is composed;
     * centred on nothing is default. */
    int x = c->margin * 2;
    if (x + wid > w - c->margin) x = w - c->margin - wid;
    if (x < c->margin) x = c->margin;
    rect r = { x, h - bh + (bh - hgt) / 2, wid, hgt };
    return r;
}

/* Previous page and Next page, at the right end of the heading row (the
 * page dots they replace were 30 px wide and 34 high). 0 = previous,
 * 1 = next. Pages wrap round: a button that is dead on the first or last
 * page is a button she presses twice to find out. They are up here, not
 * in the bar, because the bottom right is where a program's message
 * lands and a message must never cover a button. */
static rect nav_rect(shell_ctx *c, int w, int h, int which)
{
    int wn = 190, wp = 220;
    int x_next = w - c->margin - wn;
    if (!head_on(c)) {                         /* at large text: in the bar */
        int bh = homebar_h(c), hgt = bh - 16;
        int bw_n = (int)(9.f * 10.4f * tk(c)) + 64, bw_p = (int)(13.f * 10.4f * tk(c)) + 64;
        int xn = w - c->margin - bw_n;
        rect r = { which ? xn : xn - 12 - bw_p, h - bh + (bh - hgt) / 2,
                   which ? bw_n : bw_p, hgt };
        return r;
    }
    int hgt = HEAD_H_BASE - 4;                 /* 52 px */
    rect r = { which ? x_next : x_next - 12 - wp, strip_h(c) + c->margin,
               which ? wn : wp, hgt };
    return r;
}

/* The area a thing fills when it is open: everything that is not
 * permanent chrome. */
static rect full_rect(shell_ctx *c, int w, int h)
{
    int top = strip_h(c);
    int bot = h - homebar_h(c);
    rect r = { 0, top, w, bot - top > 1 ? bot - top : 1 };
    return r;
}

/* ── grid geometry, owned in one place ───────────────────────────────
 * Painting and hit-testing both come through grid_metrics()/tile_rect().
 * Deriving the two separately is how a UI ends up off by the width of a
 * shadow; here it would be worse than cosmetic, because the entire
 * promise of this archetype is that pressing a picture of a thing opens
 * that thing. */
/* th0 is row 0's height and th1 every other row's. They are different
 * numbers on purpose; see grid_metrics. */
typedef struct { int cols, rows, per, pages, tw, th0, th1, gap, x0, y0; } grid_m;

/* A broken grid, and the break is the point.
 *
 * What was here produced cols x rows identical near-squares — the
 * aspect was even capped at 5:4 so that no two could differ — with a
 * centred question above them. That is the forbidden composition
 * rendered to KMS, and it is also a weak answer to its own brief: a
 * page of buttons with no first button gives the eye nowhere to land.
 *
 * Now: slot 0 spans TWO columns, and row 0 is taller than the rows
 * under it (LEAD_H). So a page has one lead item and a run of smaller
 * ones, the way a contents page or a poster is set. Both facts flow
 * out of tile_rect, which paint AND hit-test both call, so the two
 * passes cannot disagree about where anything is.
 *
 * The cost of this is real and is paid in move_sel(): keyboard
 * navigation can no longer be (row, col) arithmetic over a uniform
 * grid, so it is a nearest-rect search instead. That is the honest
 * price of an uneven layout and it is contained to one function. */
#define LEAD_SPAN 2      /* columns the first item of a page occupies */
#define LEAD_H    1.34f  /* row 0's height, as a multiple of the rest */

static grid_m grid_metrics(shell_ctx *c, int w, int h)
{
    grid_m g;
    /* target_large is the .shell file saying this person aims with an
     * unsteady hand and reads with glasses on: fewer, bigger buttons. */
    g.cols = c->target_large ? 4 : 5;
    if (w < 1150) g.cols = c->target_large ? 3 : 4;
    if (w <  800) g.cols = 2;
    g.rows = (c->target_large || h < 720) ? 2 : 3;
    /* Row 0 gives up LEAD_SPAN cells to the lead item and gets one
     * back, so a page holds that many fewer than cols*rows. */
    g.per  = g.cols * g.rows - (LEAD_SPAN - 1);
    if (g.per < 1) g.per = 1;

    int n = c->n_apps > 0 ? c->n_apps : 1;
    g.pages = (n + g.per - 1) / g.per;

    g.gap = c->padding + 8;
    int left  = c->margin * 2;
    int right = w - c->margin * 2;
    int top   = strip_h(c) + c->margin + head_h(c);
    int bot   = h - homebar_h(c) - c->margin;

    int availw = (right - left) - g.gap * (g.cols - 1);
    int availh = (bot - top)    - g.gap * (g.rows - 1);
    if (availw < g.cols) availw = g.cols;
    if (availh < g.rows) availh = g.rows;

    g.tw = availw / g.cols;
    /* No aspect cap. A wide button is not a bug here — the lead item
     * is SUPPOSED to be a different shape from the rest, and clamping
     * every cell toward a square was precisely the rule that made an
     * uneven page impossible to express. */
    float unit = (float)availh / (LEAD_H + (float)(g.rows - 1));
    g.th0 = (int)(unit * LEAD_H);
    g.th1 = (int)unit;
    if (g.th0 < 48) g.th0 = 48;
    if (g.th1 < 48) g.th1 = 48;

    /* Left measure, top measure. The block used to be centred in BOTH
     * axes inside whatever was left over, which is how a page ends up
     * floating in the middle of nothing; the leftover space now falls
     * where a printed page leaves it, at the foot. */
    g.x0 = left;
    g.y0 = top;
    return g;
}

/* Slot -> cell, the one place the broken rhythm is expressed.
 * Returns the cell's column, row, column span, and the row heights are
 * read from g. Row 0 holds the lead (LEAD_SPAN wide) then cols -
 * LEAD_SPAN ordinary cells; every row after it is ordinary. */
static void grid_cell(const grid_m *g, int slot, int *col, int *row, int *span)
{
    int first_row = g->cols - (LEAD_SPAN - 1);   /* cells in row 0 */
    if (slot < first_row) {
        *row = 0;
        *span = (slot == 0) ? LEAD_SPAN : 1;
        *col = (slot == 0) ? 0 : slot + (LEAD_SPAN - 1);
    } else {
        int k = slot - first_row;
        *row = 1 + k / g->cols;
        *col = k % g->cols;
        *span = 1;
    }
}

/* WHERE EACH PROGRAM SITS: the table's order, except that the first
 * page is shell_home_pick() for a page's worth -- which ends with "Get
 * more programs", always, in the last place on the page she lands on.
 * It used to be wherever the store ranked, which on a 1024-wide panel
 * was page two behind a 30-pixel page marker. Positions, not table
 * indices, are what pages and slots are made of; a program is still
 * known by its table index everywhere else in this file. */
static int display_pos(shell_ctx *c, int per, int app)
{
    int ord[SHELL_MAX_APPS];
    int n = shell_display_order(c, per, ord);
    for (int k = 0; k < n; k++) if (ord[k] == app) return k;
    return app;
}
static int display_app(shell_ctx *c, int per, int pos)
{
    int ord[SHELL_MAX_APPS];
    int n = shell_display_order(c, per, ord);
    return (pos >= 0 && pos < n) ? ord[pos] : -1;
}

static int page_of_app(shell_ctx *c, int app, int w, int h)
{
    grid_m g = grid_metrics(c, w, h);
    int p = display_pos(c, g.per, app) / g.per;
    return (p < 0) ? 0 : (p >= g.pages ? g.pages - 1 : p);
}

/* Keyed by GLOBAL app index rather than by slot, because that is the
 * only identity the rest of the layout cares about: the tile a thing
 * came from is the tile it shrinks back into, whatever page you are on
 * and whatever the screen got resized to in between. */
static rect tile_rect(shell_ctx *c, int w, int h, int app)
{
    grid_m g = grid_metrics(c, w, h);
    int pos  = display_pos(c, g.per, app);
    int page = pos / g.per;
    int slot = pos - page * g.per;
    int col, row, span;
    grid_cell(&g, slot, &col, &row, &span);

    /* Left-aligned, top-aligned. A short final page now runs off the
     * top-left like a column of type that has come to an end, instead
     * of being re-centred inside the full block — which was the third
     * centring decision in this one file and made two buttons on a
     * last page look like a dialog box. */
    rect r;
    r.x = g.x0 + col * (g.tw + g.gap);
    r.y = g.y0 + (row == 0 ? 0 : g.th0 + g.gap + (row - 1) * (g.th1 + g.gap));
    r.w = g.tw * span + g.gap * (span - 1);
    r.h = (row == 0) ? g.th0 : g.th1;
    return r;
}

/* Where a thing shrinks back to. A window with no app entry has no
 * button to return into, so it collapses to a point at the middle of the
 * grid rather than to (0,0), which would fly off the corner. */
static rect origin_rect(shell_ctx *c, int w, int h, int wi)
{
    if (wi >= 0 && wi < c->n_wins && c->wins[wi].app >= 0 && c->wins[wi].app < c->n_apps)
        return tile_rect(c, w, h, c->wins[wi].app);
    grid_m g = grid_metrics(c, w, h);
    rect r = { g.x0, g.y0, g.tw, g.th0 };
    return r;
}

static int app_window(shell_ctx *c, int app)
{
    for (int i = 0; i < c->n_wins; i++)
        if (c->wins[i].app == app) return i;
    return -1;
}

/* ── state changes ───────────────────────────────────────────────── */

static void open_app(shell_ctx *c, int app, int w, int h)
{
    tiles_priv *p = P(c);
    if (app < 0 || app >= c->n_apps) return;

    /* shell_launch() finds the running window or creates a slot AND
     * starts the program. This used to create the slot and stop, with a
     * comment saying the shell contract had no hook for starting
     * anything -- which was true when it was written and stayed in the
     * tree after the hook existed. Every button in this archetype
     * opened a rectangle with a name in it and nothing behind it. */
    int wi = shell_launch(c, app);
    if (wi < 0) return;
    p->win  = wi;
    p->page = page_of_app(c, app, w, h);
    c->focus = wi;
    /* EASE_OUT_CUBIC, not the theme's spring: a spring overshoots past
     * 1, and past 1 means a button drawn larger than the screen it is
     * growing into. Overshoot is charm everywhere except at the edges. */
    tween_to(&p->open, 1.f, OPEN_SECS, EASE_OUT_CUBIC);
}

static void go_home(shell_ctx *c)
{
    tiles_priv *p = P(c);
    p->hot = -1;
    if (p->win < 0) { p->page = 0; return; }   /* already home: first page */
    tween_to(&p->open, 0.f, CLOSE_SECS, EASE_IN_OUT_CUBIC);
    /* c->focus is left alone until the shrink lands: the thing you are
     * on is still the thing you are on until it is visibly back in its
     * button. l_step clears it. */
}

/* ── painting ────────────────────────────────────────────────────── */

/* A poster, not a badge.
 *
 * The composition this replaces was: halo disc, centred mark, centred
 * name, centred hint, stacked on the tile's own axis — the sixth
 * independent copy of one block in this product, and the reason six
 * genuinely different interaction models all read as the same generic
 * thing. Here the mark sits in the top-left corner and the words hang
 * off the bottom-left measure, so a tile is a piece of a page rather
 * than an app-store icon with a caption. The empty middle is the
 * composition, not a gap waiting to be filled.
 *
 * The lead item gets the display face at its largest size; the rest
 * get it one step down. That size jump IS the hierarchy — no colour,
 * no border weight, no shadow. */
static void paint_tile(shell_ctx *c, surface *s, shell_fonts *f, int app,
                       rect t, float alpha, int lead)
{
    tiles_priv *p = P(c);
    int hot = (p->hot == app);
    int run = app_window(c, app) >= 0;
    /* The store's mark in the signal colour, as on every home: the one
     * button that leads somewhere new. */
    uint32_t ink = c->apps[app].role == APP_ROLE_STORE ? c->accent : c->apps[app].tint;
    uint32_t paper = hot ? c->surface_hi : c->surface_c;
    corners cr = corners_all((float)c->radius);
    int pad = c->padding;

    draw_round_rect(s, t, cr, paper, alpha);
    /* Hover is an inset bar down the left edge, the cheapest possible
     * anti-glassmorphism primitive and the one GOV.UK uses for exactly
     * this job. No shadow, no glow, no second ring. */
    if (hot) draw_rect(s, (rect){ t.x, t.y, 4, t.h }, c->accent, alpha);
    draw_frame(s, t, 1, hot ? c->muted : c->overlay, alpha);

    /* The lead item's mark is nearly twice the others'. Together with
     * the display face two steps up, that is the whole of the
     * hierarchy on this page: no colour, no border weight, no glow. */
    /* Running. A solid square of the signal colour in the corner —
     * this is one of only two things on the whole page allowed to be
     * red, and it means "this is already going". The dot-in-a-halo it
     * replaces said the same thing twice at two alphas. */
    if (run)
        draw_rect(s, (rect){ t.x + t.w - pad - 11, t.y + pad, 11, 11 },
                  c->accent, alpha * 0.95f);

    float ha = fa(f->small, 11.f), hd = fdc(f->small, 4.f);

    /* One composition for every tile, the lead included: the mark in the
     * corner, the words at the foot. The lead is the same shape, wider,
     * with its name a step larger (DESIGN-DIRECTION C P1-14). */
    float isz = clampf((float)t.h * 0.25f, 30.f, lead ? 72.f : 56.f);
    shell_icon_draw(s, c->apps[app].icon,
                    (float)(t.x + pad) + isz * 0.5f,
                    (float)(t.y + pad) + isz * 0.5f,
                    isz, ink, paper, alpha * 0.98f);

    font *nf = lead ? f->huge : f->big;
    float room = (float)t.w - (float)pad * 2.f;
    if (shell_text_w(nf, c->apps[app].name) > room) nf = f->big;
    if (shell_text_w(nf, c->apps[app].name) > room) nf = f->dmid;
    if (shell_text_w(nf, c->apps[app].name) > room) nf = f->mid;

    float la = fa(nf, 22.f), ld = fdc(nf, 8.f);
    /* Anchored to the FOOT of the tile and measured in ink, so the
     * baseline the hint is drawn at and the space reserved for it are
     * the same number by construction. */
    float hint_b = (float)(t.y + t.h - pad) - hd;
    float name_b = hint_b - ha - 9.f - ld;
    int   hint   = (name_b - la > (float)(t.y + pad) + isz + 10.f);
    if (!hint) name_b = (float)(t.y + t.h - pad) - ld;

    float tw = (float)(t.w - 2 * pad);
    shell_text_elided(s, nf, (float)(t.x + pad), name_b, tw, c->apps[app].name,
                      c->fg_hi, alpha * 0.98f);
    if (hint && c->apps[app].hint[0] && shell_text_w(f->small, c->apps[app].hint) <= tw)
        shell_text(s, f->small, (float)(t.x + pad), hint_b,
                   c->apps[app].hint, c->subtle, alpha * 0.98f);
}

/* The growing thing. One composition, two sets of words: the button's
 * own label fades out over the first third of the move, the app's own
 * title fades in over the last third, and the icon scales continuously
 * across the whole of it. That continuity IS the explanation — the user
 * watches the button they pressed become the screen, so they never have
 * to be told where this came from or where the page went. */
static void paint_open(shell_ctx *c, surface *s, shell_fonts *f,
                       rect a, float rad, int wi, float t)
{
    const win_entry *win = &c->wins[wi];
    int app = (win->app >= 0 && win->app < c->n_apps) ? win->app : -1;
    uint32_t ink = (app >= 0) ? c->apps[app].tint : c->fg;
    shell_icon ic = (app >= 0) ? c->apps[app].icon : ICON_WINDOW;
    corners cr = corners_all(rad);
    int pad = c->padding;

    draw_round_rect(s, a, cr, c->surface_c, 1.f);
    if (t < 0.995f) {
        draw_frame(s, a, 1, c->overlay, 1.f - t * 0.4f);
        draw_hrule(s, a.x, a.y, a.w, 3, c->accent, 1.f - t);
    }

    if (win->content)
        draw_content_fit(s, win->content, a, cr, t);

    /* Text does not scale, so it cannot ride the move the way the mark
     * does: it leaves early and arrives late, and for the fifth of a
     * second in between only the mark is on the card. */
    float ta = clampf(1.f - t * 2.5f, 0.f, 1.f);        /* the button's words  */
    float tb = clampf(t * 2.5f - 1.5f, 0.f, 1.f);       /* the app's own words */

    float x   = (float)(a.x + pad);
    float isz = lerpf(clampf((float)a.h * 0.24f, 26.f, 78.f),
                      clampf((float)a.h * 0.11f, 30.f, 96.f), t);

    /* Both compositions are anchored to the SAME left measure, so the
     * mark travels straight down the page instead of sliding sideways
     * into a centre it never had. */
    float Ta = fa(f->huge, 32.f),  Td = fdc(f->huge, 12.f);
    float Sa = fa(f->mid, 14.f),   Sd = fdc(f->mid, 5.f);
    float ha = fa(f->small, 11.f), hd = fdc(f->small, 4.f);
    float la = fa(f->huge, 32.f),  ld = fdc(f->huge, 12.f);

    float icyA = (float)(a.y + pad) + isz * 0.5f;
    float icyB = (float)a.y + (float)a.h * 0.34f;
    float icy  = lerpf(icyA, icyB, t);
    float ia   = win->content ? (1.f - t) : 1.f;
    shell_icon_draw(s, ic, x + isz * 0.5f, icy, isz, ink, c->surface_c, ia * 0.98f);

    if (ta > 0.f && app >= 0) {
        float hint_b = (float)(a.y + a.h - pad) - hd;
        float name_b = hint_b - ha - 9.f - ld;
        shell_text(s, f->huge, x, name_b, c->apps[app].name, c->fg_hi, ta * 0.98f);
        shell_text(s, f->small, x, hint_b, c->apps[app].hint, c->subtle, ta * 0.9f);
        (void)la;
    }
    if (tb > 0.f && !win->content) {
        float ly = icy + isz * 0.5f + 34.f + Ta;
        shell_text(s, f->huge, x, ly, win->title, c->fg_hi, tb * 0.98f);
        draw_hrule(s, (int)x, (int)(ly + Td + 18.f),
                   (int)((float)(a.w - pad * 2) * 0.24f), 2, c->fg_hi, tb * 0.9f);
        float sy = ly + Td + 18.f + 22.f + Sa;
        shell_text(s, f->mid, x, sy,
                   win->subtitle[0] ? win->subtitle : "Opening…", c->subtle, tb * 0.9f);
        /* Only ever shown on the placeholder, i.e. exactly when there is
         * nothing here yet and the user is most likely to wonder whether
         * they have broken something. */
        shell_text(s, f->small, x, sy + Sd + 13.f + ha,
                   "Press Home below to come back.", c->muted, tb * 0.95f);
    }
}

/* Previous page and Next page: outlined buttons with drawn chevrons, and
 * where she is ("Page 1 of 3") between them and Home. */
static void paint_nav(shell_ctx *c, surface *s, shell_fonts *f,
                      int w, int h, int pages, float alpha)
{
    if (pages < 2 || alpha <= 0.01f) return;
    tiles_priv *p = P(c);
    corners rr = corners_all(8.f);
    for (int i = 0; i < 2; i++) {
        rect r = nav_rect(c, w, h, i);
        int hot = (p->nav_hot == i);
        draw_round_rect(s, r, rr, hot ? c->surface_hi : c->surface_c, alpha);
        draw_round_rect_border(s, r, rr, 2.f, hot ? c->fg : c->overlay, alpha);
        font *ft = f->mid ? f->mid : f->small;
        const char *lab = i ? "Next page" : "Previous page";
        float tw = shell_text_w(ft, lab);
        float cy = (float)r.y + (float)r.h * 0.5f;
        float total = tw + 14.f + 22.f;
        float x0 = (float)r.x + ((float)r.w - total) * 0.5f;
        if (i) {
            shell_text(s, ft, x0, shell_baseline(ft, (float)r.y, (float)r.h), lab, c->fg_hi, alpha);
            shell_icon_draw(s, ICON_FORWARD, x0 + tw + 14.f + 11.f, cy, 24.f, c->fg_hi, c->surface_c, alpha);
        } else {
            shell_icon_draw(s, ICON_BACK, x0 + 11.f, cy, 24.f, c->fg_hi, c->surface_c, alpha);
            shell_text(s, ft, x0 + 22.f + 14.f, shell_baseline(ft, (float)r.y, (float)r.h), lab, c->fg_hi, alpha);
        }
    }
    char lab[40];
    snprintf(lab, sizeof lab, "Page %d of %d", p->page + 1, pages);
    rect hb = home_btn_rect(c, w, h), pv = nav_rect(c, w, h, 0);
    float lx = (float)(hb.x + hb.w) + 24.f;
    /* Where she is, when there is room for it beside the buttons. */
    if (pv.y < hb.y || lx + shell_text_w(f->small, lab) < (float)pv.x - 12.f)
        shell_text(s, f->small, lx, shell_baseline(f->small, (float)hb.y, (float)hb.h),
                   lab, c->subtle, alpha);
}

static void paint_strip(shell_ctx *c, surface *s, shell_fonts *f, int w, float t)
{
    tiles_priv *p = P(c);
    int bh = strip_h(c);
    rect r = { 0, 0, w, bh };

    draw_rect(s, r, c->bg_alt, 1.f);
    draw_hrule(s, 0, bh, w, 1, c->overlay, 1.f);
    float brand_end = shell_wordmark(c, s, f, (float)c->margin, (float)bh * 0.5f);

    if (c->show_clock) {
        char hm[32], dt[48];
        shell_clock(hm, sizeof hm, dt, sizeof dt);
        float rx = (float)(w - c->margin);
        shell_text(s, f->mid, rx - shell_text_w(f->mid, hm),
                   shell_baseline(f->mid, 0.f, (float)bh), hm, c->fg_hi, 1.f);
        rx -= shell_text_w(f->mid, hm) + 13.f;
        draw_vrule(s, (int)rx, 9, bh - 18, 1, c->overlay, 1.f);
        rx -= 13.f;
        shell_text(s, f->small, rx - shell_text_w(f->small, dt),
                   shell_baseline(f->small, 0.f, (float)bh), dt, c->subtle, 0.9f);
        int cx = (int)(rx - shell_text_w(f->small, dt)) - 8;   /* opens Messages */
        c->clock_r = (rect){ cx, 0, w - cx, bh };
    }

    /* One slot that says where you are, and it crossfades in place.
     * The question and the answer occupying the same pixels is the
     * point: there is one "where am I", not a title bar that appears
     * and a greeting that disappears. Nothing here is clickable — this
     * is NOT a taskbar.
     *
     * It sits on the grid's left measure, not on w*0.5. Centring it
     * put a question mark on the screen's axis directly above a grid
     * that was itself centred in both directions; two centred things
     * stacked is not a composition, it is a default. */
    /* After the brand, separated by a rule: a masthead reads left to
     * right as two facts, not as one string that happens to wrap. */
    float gx = brand_end + 18.f;
    if (p->win >= 0) draw_vrule(s, (int)gx, 9, bh - 18, 1, c->overlay, 1.f);
    gx += 18.f;
    float na = clampf(t * 2.5f - 1.5f, 0.f, 1.f);
    if (na > 0.f && p->win >= 0) {
        const win_entry *win = &c->wins[p->win];
        uint32_t ink = (win->app >= 0) ? c->apps[win->app].tint : c->fg;
        shell_icon ic = (win->app >= 0) ? c->apps[win->app].icon : ICON_WINDOW;
        shell_icon_draw(s, ic, gx + 8.f, (float)bh * 0.5f, 16.f, ink, c->bg_alt, na * 0.95f);
        shell_text(s, f->mid, gx + 24.f, shell_baseline(f->mid, 0.f, (float)bh),
                   win->title, c->fg_hi, na * 0.97f);
    }
}

static void paint_homebar(shell_ctx *c, surface *s, shell_fonts *f, int w, int h, float t)
{
    tiles_priv *p = P(c);
    rect bar = homebar_rect(c, w, h);

    draw_rect(s, bar, c->bg_alt, 1.f);
    /* A 2px rule closes the foot of the page, the way a rule closes a
     * printed footer. Heavier than the hairlines inside the page, so
     * the permanent chrome reads as a different order of thing. */
    draw_hrule(s, 0, bar.y, w, 2, c->overlay, 1.f);

    rect b = home_btn_rect(c, w, h);
    corners cr = corners_all((float)c->radius);
    int hot = p->home_hot;

    /* Quiet when you are already here, filled and loud when you are
     * not — but never absent, never moved and never a different size.
     * The user learns one target once.
     *
     * The state change runs at twice the speed of the move, on purpose:
     * halfway through, this control is the one thing on screen that
     * must not be a half-blended nothing-colour. */
    float k = clampf(t * 2.f, 0.f, 1.f);
    draw_round_rect(s, b, cr, hot ? c->surface_hi : c->surface_c, 1.f - k);
    draw_frame(s, b, 2, hot ? c->accent : c->fg, 1.f - k);
    if (k > 0.005f) draw_round_rect(s, b, cr, c->accent, k);

    /* The word, not a glyph. The shared mark set has no house and no
     * grid for "home", and inventing one here would fork the set six
     * ways — which is exactly what shellcommon.c exists to prevent. It
     * is also the better call on its own merits: a mark that needs
     * interpreting is a label that failed, and this is the one control
     * that cannot afford to be interpreted.
     *
     * On the accent fill the label is drawn in the background colour.
     * That is the one colour a theme guarantees contrasts with its
     * accent, so this stays readable in a warm-paper theme and in a
     * dark one without a single hardcoded value. */
    font *hf = c->target_large ? f->big : f->dmid;
    float by = shell_baseline(hf, (float)b.y, (float)b.h);
    shell_text(s, hf, (float)(b.x + c->padding), by, "Home",
               mix_rgb(c->fg_hi, c->bg, k), 1.f);
}

static void l_paint(shell_ctx *c, surface *s, shell_fonts *f, const surface *wall)
{
    tiles_priv *p = P(c);
    int w = s->w, h = s->h;

    /* Remember what we actually drew. l_click gets screen coordinates
     * but no surface, and guessing a size there would put every hit
     * rect in the wrong place on any display that is not the one the
     * guess was written for. */
    c->screen_w = w; c->screen_h = h;

    if (wall) {
        for (int y = 0; y < s->h && y < wall->h; y++)
            memcpy(s->px + (size_t)y * s->stride, wall->px + (size_t)y * wall->stride,
                   (size_t)(s->w < wall->w ? s->w : wall->w) * sizeof *s->px);
    } else surface_fill(s, 0xFF000000u | c->bg);

    float t = clampf(p->open.value, 0.f, 1.f);
    grid_m g = grid_metrics(c, w, h);

    if (p->page >= g.pages) p->page = g.pages - 1;
    if (p->page < 0) p->page = 0;
    /* While something is open, the page under it is the page that thing
     * lives on — so closing always puts you back where you pressed. */
    if (p->win >= 0 && c->wins[p->win].app >= 0)
        p->page = page_of_app(c, c->wins[p->win].app, w, h);

    if (t < 0.999f) {
        int first = p->page * g.per;
        int last  = first + g.per;
        if (last > c->n_apps) last = c->n_apps;
        for (int k = first; k < last; k++) {
            int i = display_app(c, g.per, k);
            if (i < 0) continue;
            if (t > 0.f && p->win >= 0 && c->wins[p->win].app == i)
                continue;                       /* this one IS the growing card */
            rect r = tile_rect(c, w, h, i);
            if (t > 0.001f) r = rect_scale(r, 1.f - 0.07f * t);
            paint_tile(c, s, f, i, r, 1.f - t, (k - first) == 0);
        }
        /* The question, at the display size. It is the page's one
         * instruction, so it is the biggest thing on it. */
        if (f->huge && head_on(c))
            shell_text(s, f->huge, (float)(c->margin * 2),
                       (float)(strip_h(c) + c->margin) + font_ascent(f->huge) * 0.92f,
                       "What would you like to do?", c->fg_hi, (1.f - t) * 0.98f);
    }

    if (p->win >= 0)
        paint_open(c, s, f,
                   rect_lerp(origin_rect(c, w, h, p->win), full_rect(c, w, h), t),
                   (float)c->radius * (1.f - t), p->win, t);

    /* Chrome last, always. Nothing the user opens can paint over the one
     * way back. */
    paint_strip(c, s, f, w, t);
    paint_homebar(c, s, f, w, h, t);
    /* The page label sits in the bar, the buttons in the heading row. */
    if (t < 0.999f) paint_nav(c, s, f, w, h, g.pages, 1.f - t);
}

/* ── input ───────────────────────────────────────────────────────── */

static int inside(rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int l_click(shell_ctx *c, int x, int y)
{
    tiles_priv *p = P(c);
    int w = c->screen_w, h = c->screen_h;
    float t = clampf(p->open.value, 0.f, 1.f);

    /* Home is tested first and wins every ambiguity, by construction. */
    if (inside(home_btn_rect(c, w, h), x, y)) { go_home(c); return 1; }
    if (inside(homebar_rect(c, w, h), x, y))  return 1;   /* chrome swallows the rest */

    /* Mid-transition nothing is where it will be, so no click is
     * honoured: half-delivered presses are how a user ends up somewhere
     * they did not choose. */
    if (t > 0.001f && t < 0.999f) return 1;

    /* Open and settled: the screen belongs to the client. Not consumed,
     * so the compositor routes it there. */
    if (t >= 0.999f) return 0;

    grid_m g = grid_metrics(c, w, h);
    if (g.pages > 1)
        for (int i = 0; i < 2; i++)
            if (inside(nav_rect(c, w, h, i), x, y)) {
                p->page = (p->page + (i ? 1 : g.pages - 1)) % g.pages;   /* wraps round */
                p->hot = -1;
                return 1;
            }

    int first = p->page * g.per, last = first + g.per;
    if (last > c->n_apps) last = c->n_apps;
    for (int k = first; k < last; k++) {
        int i = display_app(c, g.per, k);
        if (i >= 0 && inside(tile_rect(c, w, h, i), x, y)) { open_app(c, i, w, h); return 1; }
    }

    return 0;
}

static void l_motion(shell_ctx *c, int x, int y)
{
    tiles_priv *p = P(c);
    int w = c->screen_w, h = c->screen_h;
    c->mouse_x = x; c->mouse_y = y;

    p->home_hot = inside(home_btn_rect(c, w, h), x, y);
    p->nav_hot = inside(nav_rect(c, w, h, 0), x, y) ? 0 : inside(nav_rect(c, w, h, 1), x, y) ? 1 : -1;
    p->hot = -1;
    if (clampf(p->open.value, 0.f, 1.f) > 0.001f) return;

    grid_m g = grid_metrics(c, w, h);
    int first = p->page * g.per, last = first + g.per;
    if (last > c->n_apps) last = c->n_apps;
    for (int k = first; k < last; k++) {
        int i = display_app(c, g.per, k);
        if (i >= 0 && inside(tile_rect(c, w, h, i), x, y)) { p->hot = i; return; }
    }
}

/* The keyboard is a bonus here, not the model (keyboard_optional=yes),
 * so it drives the same single highlight the pointer does rather than
 * growing a second notion of "selected". */
/* Keyboard navigation over a grid that is no longer a grid.
 *
 * This used to be (row, col) arithmetic over g.cols, which is exactly
 * the thing that stops working the moment one cell spans two columns
 * and one row is taller than the others. So it is a nearest-rect
 * search instead: from the current cell's centre, take the closest
 * cell that actually lies in the direction asked for, weighting
 * off-axis distance double so that "right" prefers the thing beside
 * you over the thing diagonally below.
 *
 * It reads the SAME tile_rect the painter and the hit-test read, so
 * whatever composition that function returns, the arrows follow it.
 * That is the price of an uneven layout, paid once, here. */
static void move_sel(shell_ctx *c, int dcol, int drow)
{
    tiles_priv *p = P(c);
    grid_m g = grid_metrics(c, c->screen_w, c->screen_h);
    if (c->n_apps <= 0) return;

    if (p->hot < 0) {
        int k0 = p->page * g.per;
        if (k0 >= c->n_apps) k0 = c->n_apps - 1;
        p->hot = display_app(c, g.per, k0);
        return;
    }

    int first = p->page * g.per, last = first + g.per;
    if (last > c->n_apps) last = c->n_apps;

    rect cur = tile_rect(c, c->screen_w, c->screen_h, p->hot);
    float cx = (float)cur.x + (float)cur.w * 0.5f;
    float cy = (float)cur.y + (float)cur.h * 0.5f;

    int best = -1;
    float bestd = 0.f;
    for (int k = first; k < last; k++) {
        int i = display_app(c, g.per, k);
        if (i < 0 || i == p->hot) continue;
        rect r = tile_rect(c, c->screen_w, c->screen_h, i);
        float dx = (float)r.x + (float)r.w * 0.5f - cx;
        float dy = (float)r.y + (float)r.h * 0.5f - cy;
        if (dcol > 0 && dx <=  1.f) continue;
        if (dcol < 0 && dx >= -1.f) continue;
        if (drow > 0 && dy <=  1.f) continue;
        if (drow < 0 && dy >= -1.f) continue;
        float along = dcol ? fabsf(dx) : fabsf(dy);
        float off   = dcol ? fabsf(dy) : fabsf(dx);
        float d = along + off * 2.f;
        if (best < 0 || d < bestd) { best = i; bestd = d; }
    }
    if (best >= 0) { p->hot = best; return; }

    /* Nothing that way on this page. Left and right step the page, as
     * they always did; up and down stop, because a page break is a
     * horizontal idea here and pretending otherwise teleports you. */
    if (dcol > 0 && p->page + 1 < g.pages) {
        p->page++;
        p->hot = display_app(c, g.per, p->page * g.per);
    } else if (dcol < 0 && p->page > 0) {
        p->page--;
        int l = p->page * g.per + g.per;
        if (l > c->n_apps) l = c->n_apps;
        p->hot = display_app(c, g.per, l - 1);
    }
    if (p->hot >= c->n_apps) p->hot = c->n_apps - 1;
    if (p->hot < 0) p->hot = 0;
}

static void l_key(shell_ctx *c, int k)
{
    tiles_priv *p = P(c);
    float t = clampf(p->open.value, 0.f, 1.f);
    switch (k) {
        case 1:                                    /* KEY_ESC   */
        case 102: go_home(c); break;               /* KEY_HOME  */
        case 28:                                   /* KEY_ENTER */
            if (t <= 0.001f && p->hot >= 0) open_app(c, p->hot, c->screen_w, c->screen_h);
            break;
        /* Arrows never switch between open things: that would be a second
         * way to move around, and the tradeoff this archetype signed up
         * for is that switching goes via Home. */
        case 105: if (t <= 0.001f) move_sel(c, -1,  0); break;   /* LEFT  */
        case 106: if (t <= 0.001f) move_sel(c, +1,  0); break;   /* RIGHT */
        case 103: if (t <= 0.001f) move_sel(c,  0, -1); break;   /* UP    */
        case 108: if (t <= 0.001f) move_sel(c,  0, +1); break;   /* DOWN  */
        default: break;
    }
}

static int l_step(shell_ctx *c, float dt)
{
    tiles_priv *p = P(c);
    int moving = tween_step(&p->open, dt);
    if (!moving && p->open.value <= 0.001f && p->win >= 0) {
        p->win = -1;
        c->focus = -1;      /* back on the page of buttons, nothing is "on" */
    }
    return moving;
}

static void l_init(shell_ctx *c)
{
    static tiles_priv priv;
    memset(&priv, 0, sizeof priv);
    priv.hot = -1;
    priv.nav_hot = -1;
    priv.win = -1;
    tween_set(&priv.open, 0.f);
    c->priv = &priv;

    /* Boot lands on the page of buttons. The one exception is a session
     * that already has something focused — a restored session, or the
     * preview harness — which is shown straight away with no animation,
     * because a transition nobody asked for explains nothing. */
    if (c->focus >= 0 && c->focus < c->n_wins) {
        priv.win = c->focus;
        if (c->wins[priv.win].app >= 0)
            priv.page = page_of_app(c, c->wins[priv.win].app, c->screen_w, c->screen_h);
        tween_set(&priv.open, 1.f);
    } else {
        c->focus = -1;
    }
}

/* A window that arrived by itself -- the store's "Open" button, the
 * "is ready" card, a link from another program -- is the thing she is
 * waiting for, and one thing at a time means it fills the screen.
 * Without this the page went on showing whatever it showed (the store,
 * or the buttons) and the program she asked for opened out of sight. */
static void l_present(shell_ctx *c, int win)
{
    tiles_priv *p = P(c);
    if (win < 0 || win >= c->n_wins) return;
    c->focus = win;
    if (p->win == win) { tween_to(&p->open, 1.f, OPEN_SECS, EASE_OUT_CUBIC); return; }
    int replacing = p->win >= 0 && p->open.value > 0.5f;
    p->win = win;
    if (c->wins[win].app >= 0)
        p->page = page_of_app(c, c->wins[win].app, c->screen_w, c->screen_h);
    /* One full-screen thing replacing another has nothing to grow from. */
    if (replacing) tween_set(&p->open, 1.f);
    else tween_to(&p->open, 1.f, OPEN_SECS, EASE_OUT_CUBIC);
}

static void l_fini(shell_ctx *c) { c->priv = NULL; }

/* The program is already the whole screen and has no bar of ours: the
 * one thing its own bar can ask that means anything is "put me away",
 * which is Home. */
static void l_request(shell_ctx *c, int win, unsigned rq)
{
    if ((rq & WREQ_MINIMIZE) && P(c)->win == win) go_home(c);
}

/* Home, the page buttons, every tile on the page; or, with a program open, the program. */
static int l_targets(shell_ctx *c, shell_target *o, int max)
{
    tiles_priv *p = P(c);
    int w = c->screen_w, h = c->screen_h, n = 0;
#define ADD(R, WHAT, KIND) do { rect r_ = (R); if (r_.w > 0 && r_.h > 0 && n < max) { \
        o[n].r = r_; o[n].what = (WHAT); o[n].kind = (KIND); n++; } } while (0)
    ADD(home_btn_rect(c, w, h), "the Home button", TARGET_PRESS);
    if (clampf(p->open.value, 0.f, 1.f) >= 0.999f) {
        ADD(full_rect(c, w, h), "a window", TARGET_WINDOW);
        return n;
    }
    grid_m g = grid_metrics(c, w, h);
    if (g.pages > 1)
        for (int i = 0; i < 2; i++)
            ADD(nav_rect(c, w, h, i), i ? "the Next page button" : "the Previous page button", TARGET_PRESS);
    int first = p->page * g.per, last = first + g.per;
    if (last > c->n_apps) last = c->n_apps;
    for (int k = first; k < last; k++) {
        int i = display_app(c, g.per, k);
        if (i >= 0) ADD(tile_rect(c, w, h, i), "a tile", TARGET_PRESS);
    }
#undef ADD
    return n;
}

const shell_layout layout_tiles = {
    .id = "tiles",
    .request = l_request,
    .init = l_init,
    .paint = l_paint,
    .click = l_click,
    .motion = l_motion,
    .key = l_key,
    .step = l_step,
    .present = l_present,
    .fini = l_fini,
    .targets = l_targets,
};
