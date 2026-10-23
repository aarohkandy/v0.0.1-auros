/* csdtest.c — one set of window buttons, never two.
 *
 * Firefox and the GTK programs draw their own title bar, with their own
 * minimise, maximise and close. The shell used to draw ITS title bar
 * above that, with its own three: two sets of window buttons, one above
 * the other (WL's finding, `docs/handoff/streams/C.md`). A window that
 * draws its own (win_entry.csd, from aurwl_win_csd) now gets none of the
 * shell's, in every look; and what its own bar asks for (to be moved, put
 * away, made big) is done by the look, so none of its buttons is dead.
 *
 * Also here: the X on a window of OURS asks the program to close, as
 * "Close this" does. It used to drop the slot and leave the program
 * running, and the next frame made the slot again.
 *
 *   cc -O2 -std=gnu11 -o /tmp/csdtest tools/csdtest.c src/aurshell/draw.c \
 *      src/aurshell/shellcommon.c src/aurshell/anim.c src/aurshell/foot.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      -I src/aurshell -I src/common -lm && /tmp/csdtest
 *   (the compositor half -- which window counts as drawing its own -- is
 *   tools/wltest-deco.sh)
 *
 * Run from the repository root (it reads the shells folder).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/aurshell/shell.h"
#include "../src/aurshell/foot.h"

static int fail = 0, checked = 0;
static void ok(const char *look, const char *what, int good)
{
    checked++;
    printf("    %-8s %-66s %s\n", look, what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

#define W 1366
#define H 768
#define WORDS "Words in the bar"
#define PAPER 0xFF12AB34u          /* what the program painted */

typedef struct { shell_ctx c; const shell_layout *L; surface *s; surface *app; shell_fonts f; } rig;

static void settle(rig *r)
{
    if (r->L->step) for (int i = 0; i < 80; i++) r->L->step(&r->c, 0.05f);
}

static void paint(rig *r)
{
    draw_track_reset();
    shell_text_log_n = 0;
    shell_text_log_on = 1;
    surface body = *r->s;
    body.h = r->c.screen_h;
    r->L->paint(&r->c, &body, &r->f, NULL);
    shell_text_log_on = 0;
}

/* Were `words` drawn in the strip of screen from `y0` to `y1` over the
 * program's frame? (The bar at the bottom lists the window by name too, so
 * "drawn anywhere" would blame the wrong thing.) */
static int said_between(const char *words, int x0, int x1, int y0, int y1)
{
    for (int i = 0; i < shell_text_log_n; i++) {
        const shell_text_rec *t = &shell_text_log[i];
        if (strcmp(t->s, words) || t->w <= 0) continue;
        if (t->x >= x0 && t->x < x1 && t->y >= y0 && t->y < y1) return 1;
    }
    return 0;
}

/* One program window with real pixels, `csd` or not, with a program
 * behind it (`wid`) or still starting (0). */
static void make(rig *r, const char *look, int csd, int wid)
{
    memset(&r->c, 0, sizeof r->c);
    theme_t t = {0};
    shell_theme_load(&r->c, &t);
    char shf[96];
    snprintf(shf, sizeof shf, "shells/%s.shell", look);
    if (shell_archetype_load(&r->c, shf) < 0) {
        fprintf(stderr, "cannot read %s -- run this from the repository root\n", shf);
        exit(2);
    }
    r->c.text_scale = 1.f;
    r->c.focus = r->c.hover = r->c.foot_hover = -1;
    r->c.mouse_x = r->c.mouse_y = -1;
    r->c.screen_w = W;
    r->c.screen_h = H - foot_height(&r->c);
    r->L = shell_layout_by_id(r->c.layout_id);
    if (!r->s) {
        r->s = surface_new(W, H);
        r->app = surface_new(900, 600);
        for (int i = 0; i < r->app->w * r->app->h; i++) r->app->px[i] = PAPER;
    }
    shell_fonts_free(&r->f);
    memset(&r->f, 0, sizeof r->f);
    shell_fonts_load(&r->f, &r->c);

    win_entry *e = &r->c.wins[0];
    memset(e, 0, sizeof *e);
    snprintf(e->title, sizeof e->title, "%s", WORDS);
    e->app = -1;
    e->content = r->app;
    e->wid = (uint32_t)wid;
    e->csd = csd;
    r->c.n_wins = 1;
    r->c.focus = 0;
    if (r->L->init) r->L->init(&r->c);
    if (r->L->present) r->L->present(&r->c, 0);     /* as session_sync does for a new window */
    settle(r);
    paint(r);
    /* A program draws at exactly the size the look set aside for it (that
     * is what session_sync's configure is for), so do the same: two passes. */
    for (int pass = 0; pass < 2; pass++) {
        rect a;
        if (!draw_track_slot(r->app, &a) || a.w <= 0 || a.h <= 0) break;
        if (a.w != r->app->w || a.h != r->app->h) {
            r->app = surface_new(a.w, a.h);
            for (int i = 0; i < a.w * a.h; i++) r->app->px[i] = PAPER;
            r->c.wins[0].content = r->app;
        }
        paint(r);
    }
}

static int slot_of(rig *r, rect *out)
{
    return draw_track_slot(r->app, out);
}

/* Press at `dy` from the top of the program's picture and drag. 1 if the
 * window followed the pointer. Where OUR title bar would be is above the
 * picture (negative dy); a program's own bar is inside it (positive). */
static int dragged(rig *r, int dy)
{
    rect a, b;
    if (!slot_of(r, &a)) return 0;
    int x = a.x + a.w / 2, y = a.y + dy;
    r->c.mouse_x = x; r->c.mouse_y = y; r->c.mouse_down = 1;
    if (r->L->click) r->L->click(&r->c, x, y);
    for (int i = 1; i <= 4; i++) {
        r->c.mouse_x = x + 15 * i; r->c.mouse_y = y + 10 * i;
        if (r->L->motion) r->L->motion(&r->c, r->c.mouse_x, r->c.mouse_y);
    }
    r->c.mouse_down = 0;
    if (r->L->motion) r->L->motion(&r->c, r->c.mouse_x, r->c.mouse_y);
    settle(r); paint(r);
    return slot_of(r, &b) && (b.x != a.x || b.y != a.y);
}

/* Press on our own title bar, shake the hand a little (nothing past DRAG_SLOP),
 * let go. 1 if the window stayed exactly where it was. */
static int held_still(rig *r, int dy)
{
    rect a, b;
    if (!slot_of(r, &a)) return 0;
    int x = a.x + a.w / 2, y = a.y + dy;
    r->c.mouse_x = x; r->c.mouse_y = y; r->c.mouse_down = 1;
    if (r->L->click) r->L->click(&r->c, x, y);
    static const int sh[][2] = { {3, 3}, {5, -4}, {-4, 5}, {2, -2} };
    for (unsigned i = 0; i < sizeof sh / sizeof sh[0]; i++) {
        r->c.mouse_x = x + sh[i][0]; r->c.mouse_y = y + sh[i][1];
        if (r->L->motion) r->L->motion(&r->c, r->c.mouse_x, r->c.mouse_y);
    }
    r->c.mouse_down = 0;
    if (r->L->motion) r->L->motion(&r->c, r->c.mouse_x, r->c.mouse_y);
    settle(r); paint(r);
    return slot_of(r, &b) && b.x == a.x && b.y == a.y;
}

static int window_buttons_of_ours(const char *look)
{
    return !strcmp(look, "taskbar") || !strcmp(look, "dock");
}

int main(void)
{
    rig r; memset(&r, 0, sizeof r);
    static const char *LOOKS[] = { "taskbar", "dock", "rail", "tiles" };

    printf("  a program that draws its own title bar gets none of ours\n");
    for (int k = 0; k < 4; k++) {
        const char *look = LOOKS[k];
        rect ours, its;
        make(&r, look, 0, 7);
        int have_ours = slot_of(&r, &ours);
        int ours_says = have_ours && said_between(WORDS, ours.x, ours.x + ours.w, ours.y - 70, ours.y + 4);
        make(&r, look, 1, 7);
        int have_its = slot_of(&r, &its);
        ok(look, "its own pixels are on screen", have_its);
        if (!strcmp(look, "tiles")) {
            /* One thing at a time never had a bar: the page is the program. */
            ok(look, "(no bar of ours to take away: the page is the program)",
               have_its && ours.w == its.w && ours.h == its.h);
        } else {
            ok(look, "a window of ours says its name in a bar (the control)",
               have_ours && ours_says);
            ok(look, "...and a program that has its own bar gets no second name over it",
               !said_between(WORDS, its.x, its.x + its.w, its.y - 70, its.y + 70));
            ok(look, "...so its picture is taller than inside our bar",
               have_ours && have_its && its.h > ours.h);
        }
        if (window_buttons_of_ours(look)) {
            make(&r, look, 0, 7);
            ok(look, "a window of ours is dragged by its title bar (the control)", dragged(&r, -18));
            make(&r, look, 0, 7);
            ok(look, "a wobble of the hand between press and release is a press, not a drag (8 px)",
               held_still(&r, -18));
            make(&r, look, 1, 7);
            ok(look, "a program's own bar is not ours to drag: the program asks (below)",
               !dragged(&r, 12));
        }
        make(&r, look, 1, 7);
        /* Where it is painted, the first pixel is the program's own. */
        paint(&r);
        slot_of(&r, &its);
        uint32_t px = r.s->px[(size_t)(its.y + 6) * r.s->stride + (its.x + its.w / 2)];
        ok(look, "the top of the frame is the program's picture, not our paint",
           (px & 0xFFFFFFu) == (PAPER & 0xFFFFFFu));
    }

    printf("  what its own title bar asks for is done by the look\n");
    {   /* Like Windows: move, maximise, restore, minimise. */
        make(&r, "taskbar", 1, 7);
        rect a, b;
        slot_of(&r, &a);
        r.c.mouse_x = a.x + 30; r.c.mouse_y = a.y + 12; r.c.mouse_down = 1;
        r.L->request(&r.c, 0, WREQ_MOVE);
        r.L->motion(&r.c, a.x + 30 + 70, a.y + 12 + 50);
        r.c.mouse_down = 0;
        r.L->motion(&r.c, a.x + 100, a.y + 62);
        settle(&r); paint(&r); slot_of(&r, &b);
        if (!(b.x == a.x + 70 && b.y == a.y + 50))
            printf("      (from %d,%d to %d,%d; wanted %d,%d)\n", a.x, a.y, b.x, b.y, a.x + 70, a.y + 50);
        ok("taskbar", "its title bar's drag moves the window", b.x == a.x + 70 && b.y == a.y + 50);
        make(&r, "taskbar", 1, 7);
        slot_of(&r, &a);
        r.c.mouse_x = a.x + 30; r.c.mouse_y = a.y + 12; r.c.mouse_down = 1;
        r.L->request(&r.c, 0, WREQ_MOVE);
        r.L->motion(&r.c, a.x + 30 + 3, a.y + 12 + 2);
        r.c.mouse_down = 0;
        r.L->motion(&r.c, a.x + 33, a.y + 14);
        settle(&r); paint(&r); slot_of(&r, &b);
        ok("taskbar", "...from the first pixel: the program already waited out its own threshold",
           b.x == a.x + 3 && b.y == a.y + 2);

        ok("taskbar", "it is not maximised to begin with", r.L->maxed(&r.c, 0) == 0);
        r.L->request(&r.c, 0, WREQ_MAXIMIZE);
        settle(&r); paint(&r); slot_of(&r, &b);
        ok("taskbar", "its maximise button makes it fill the screen",
           r.L->maxed(&r.c, 0) == 1 && b.w == r.c.screen_w);
        r.L->request(&r.c, 0, WREQ_MAXIMIZE);
        ok("taskbar", "(asking again does not undo it)", r.L->maxed(&r.c, 0) == 1);
        r.L->request(&r.c, 0, WREQ_RESTORE);
        settle(&r); paint(&r); slot_of(&r, &b);
        ok("taskbar", "its restore button brings it back", r.L->maxed(&r.c, 0) == 0 && b.w < r.c.screen_w);
        r.L->request(&r.c, 0, WREQ_MINIMIZE);
        ok("taskbar", "its minimise button puts it on the bar", r.c.wins[0].minimised == 1);

        make(&r, "taskbar", 1, 7);
        slot_of(&r, &a);
        r.c.mouse_x = a.x + 30; r.c.mouse_y = a.y + 12; r.c.mouse_down = 0;
        r.L->request(&r.c, 0, WREQ_MOVE);
        r.L->motion(&r.c, a.x + 100, a.y + 62);
        settle(&r); paint(&r); slot_of(&r, &b);
        ok("taskbar", "a drag asked for with no button held goes nowhere", b.x == a.x && b.y == a.y);
    }
    {   /* Like a Mac: move and minimise. */
        make(&r, "dock", 1, 7);
        rect a, b;
        slot_of(&r, &a);
        r.c.mouse_x = a.x + 30; r.c.mouse_y = a.y + 12; r.c.mouse_down = 1;
        r.L->request(&r.c, 0, WREQ_MOVE);
        r.L->motion(&r.c, a.x + 30 + 70, a.y + 12 + 50);
        r.c.mouse_down = 0;
        r.L->motion(&r.c, a.x + 100, a.y + 62);
        settle(&r); paint(&r); slot_of(&r, &b);
        ok("dock", "its title bar's drag moves the window", b.x == a.x + 70 && b.y == a.y + 50);
        make(&r, "dock", 1, 7);
        slot_of(&r, &a);
        r.c.mouse_x = a.x + 30; r.c.mouse_y = a.y + 12; r.c.mouse_down = 1;
        r.L->request(&r.c, 0, WREQ_MOVE);
        r.L->motion(&r.c, a.x + 30 + 3, a.y + 12 + 2);
        r.c.mouse_down = 0;
        r.L->motion(&r.c, a.x + 33, a.y + 14);
        settle(&r); paint(&r); slot_of(&r, &b);
        ok("dock", "...from the first pixel: the program already waited out its own threshold",
           b.x == a.x + 3 && b.y == a.y + 2);
        r.L->request(&r.c, 0, WREQ_MINIMIZE);
        ok("dock", "its minimise button puts it in the dock", r.c.wins[0].minimised == 1);

        /* A program that asks to fill the space (Firefox, on a small screen) gets it. */
        make(&r, "dock", 1, 7);
        slot_of(&r, &a);
        r.L->request(&r.c, 0, WREQ_MAXIMIZE);
        settle(&r); paint(&r); slot_of(&r, &b);
        ok("dock", "a program that asks to fill the space is given all of it",
           r.L->maxed(&r.c, 0) == 1 && b.w == r.c.screen_w && b.h > a.h);
        r.L->request(&r.c, 0, WREQ_RESTORE);
        settle(&r); paint(&r); slot_of(&r, &b);
        ok("dock", "...and gives it back where it was",
           r.L->maxed(&r.c, 0) == 0 && b.x == a.x && b.y == a.y && b.w == a.w && b.h == a.h);
        /* Two windows; the first goes away: the second keeps ITS state, not the first's. */
        make(&r, "dock", 1, 7);
        r.c.wins[1] = r.c.wins[0]; r.c.wins[1].wid = 8; r.c.n_wins = 2;
        r.L->request(&r.c, 1, WREQ_MAXIMIZE);
        r.L->removed(&r.c, 0);
        r.c.wins[0] = r.c.wins[1]; r.c.n_wins = 1;
        ok("dock", "when a window goes, the next one keeps its own maximised state",
           r.L->maxed(&r.c, 0) == 1);
    }
    for (int k = 2; k < 4; k++) {   /* the two looks with no window buttons: minimise is Home */
        make(&r, LOOKS[k], 1, 7);
        ok(LOOKS[k], "it is the thing on show", r.c.focus == 0);
        r.L->request(&r.c, 0, WREQ_MINIMIZE);
        settle(&r);
        ok(LOOKS[k], "its minimise button goes Home", r.c.focus == -1);
    }

    printf("  the X on a window of ours asks the program\n");
    for (int k = 0; k < 2; k++) {
        const char *look = LOOKS[k];
        if (!window_buttons_of_ours(look)) continue;
        /* Press along the top row from the right edge in: the first
         * thing that happens is the X (it is the rightmost control). */
        for (int wid = 7; wid >= 0; wid -= 7) {
            make(&r, look, 0, wid);
            rect a; slot_of(&r, &a);
            int th = 36;
            int fx = -1;
            for (int x = a.x + a.w - 2; x > a.x + a.w - 120 && fx < 0; x--) {
                int y = a.y - th / 2;
                r.c.mouse_x = x; r.c.mouse_y = y; r.c.mouse_down = 1;
                r.L->click(&r.c, x, y);
                r.c.mouse_down = 0;
                r.L->motion(&r.c, x, y);
                if (r.c.want_close_win || r.c.n_wins == 0) fx = x;
            }
            if (wid) {
                ok(look, "with a program behind it, the X asks it to close",
                   fx >= 0 && r.c.want_close_win == 1);
                ok(look, "...and the window is still there until the program goes",
                   r.c.n_wins == 1);
            } else {
                ok(look, "with nothing behind it yet, the X drops the empty window",
                   fx >= 0 && r.c.n_wins == 0 && !r.c.want_close_win);
            }
        }
    }

    printf("\n%d checks, %d failed\n", checked, fail);
    return fail ? 1 : 0;
}
