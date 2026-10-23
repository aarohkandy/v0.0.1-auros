/* foot.c — see foot.h. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "foot.h"
#include "draw.h"
#include "anim.h"

/* ── the fixed look ─────────────────────────────────────────────────
 *
 * Deliberately not from the theme. See foot.h. These two are a 13.9:1
 * pair, which is well past the 4.5:1 text floor with room for a theme
 * author to have done something unexpected around them. */
#define FOOT_BG     0x14171Bu
#define FOOT_INK    0xECEFF2u
#define FOOT_DIM    0x9BA3ABu
#define FOOT_RULE   0x6E7680u
#define FOOT_HOT    0x23272Du

/* The BUTTONS are at least 48px on their shorter side (DESIGN-DIRECTION
 * §3; docs/EASY.md rule 4 says 44), so the band has to be tall enough to
 * hold one with room to breathe. */
#define FOOT_TARGET 48
#define FOOT_PAD     6
#define FOOT_MIN_H  (FOOT_TARGET + 2 * FOOT_PAD)
/* And the words in it are never smaller than this, whatever she has
 * chosen, because this is the control she reaches for when everything
 * else has become unreadable. */
#define FOOT_MIN_PX 18.f

/* The range she can choose within. Below 0.8 the shell's own furniture
 * stops fitting; above 2.0 a 1024x600 panel holds almost nothing, and
 * "almost nothing, very large" is a different product. */
#define SCALE_MIN   0.80f
#define SCALE_MAX   2.00f
#define SCALE_STEP  0.15f

enum { B_HELP, B_NET, B_SETTINGS, B_CLOSE, B_SMALLER, B_BIGGER,
       B_POWER, B_N };

/* One name per thing, the name on the button (DESIGN-DIRECTION §4). */
static const char *LABEL[B_N] = { "Help", "Wi-Fi", "Settings", "Close this",
                                  "Smaller text", "Bigger text", "Turn off" };
static const shell_icon ICON[B_N] = { ICON_HELP, ICON_WIFI, ICON_GEAR, ICON_CLOSE,
                                      ICON_SMALLER, ICON_BIGGER, ICON_POWER };

/* ── where the buttons are ──────────────────────────────────────────
 *
 * ONE function, called by painting, by hit-testing and by
 * tools/targets.c, per docs/SHELLS.md. Two groups: Help, Wi-Fi,
 * Settings and Close this from the left corner; Turn off, Bigger text,
 * a wide gap, Smaller text from the right one. Each item is an outlined
 * 48 px-or-more box, icon above its label. When both groups do not fit
 * on one row (big text, or a small screen) the left group takes the top
 * row and the right group the bottom one. A control that moves between
 * visits is one she has to find again: Close this is last on the left so
 * the others never move.
 */
#define FOOT_GAP 8
#define FOOT_BIG_GAP 24     /* Bigger text and Smaller text: never neighbours */

static float foot_k(const shell_ctx *c)
{ return c->text_scale < 1.f ? 1.f : (c->text_scale > 2.f ? 2.f : c->text_scale); }

/* The width of one item: its label at the type size, and room. An
 * estimate on purpose -- geometry is a pure function of the policy and
 * the sizes, so the harness can ask it everything without a font -- and
 * a generous one, so a real label is never cut. */
static int item_w(const shell_ctx *c, int which)
{
    float k = foot_k(c);
    int w = (int)(10.4f * k * (float)strlen(LABEL[which])) + (int)(28.f * k);
    return w < 96 ? 96 : w;
}
/* Icon above label up to 150% text. Past that the words are what she
 * needs and the band is already two rows: the icon goes, so the band
 * does not eat the screen (at 200% on 1024x600 it would take a third). */
static int with_icons(const shell_ctx *c) { return foot_k(c) <= 1.5f; }
static int item_h(const shell_ctx *c)
{
    float k = foot_k(c);
    int h = with_icons(c) ? (int)(40.f + 14.f * k) : (int)(28.f * k);
    return h < FOOT_TARGET ? FOOT_TARGET : h;
}

/* Which buttons this machine has at all. A build that forbids changing
 * settings has no size controls; a kiosk has neither a way to turn the
 * machine off nor a network of its own to choose, since an
 * administrator sets a kiosk's network and a public terminal whose
 * users can point it at any nearby wifi is a different product. A build
 * that forbids changing settings does not get a button that refuses: it
 * gets no button. Close this only when there is something to close, and
 * last in the left group so that the buttons before it never move. */
static void foot_which(const shell_ctx *c, int left[FOOT_MAX], int *nl,
                       int right[FOOT_MAX], int *nr)
{
    *nl = *nr = 0;
    left[(*nl)++] = B_HELP;
    if (!c->kiosk && c->allow_network) left[(*nl)++] = B_NET;
    if (!c->kiosk && c->allow_settings) left[(*nl)++] = B_SETTINGS;
    if (!c->kiosk && c->n_wins > 0) left[(*nl)++] = B_CLOSE;
    if (!c->kiosk) right[(*nr)++] = B_POWER;
    if (c->allow_settings) { right[(*nr)++] = B_BIGGER; right[(*nr)++] = B_SMALLER; }
}

static int group_w(const shell_ctx *c, const int *g, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++) {
        w += item_w(c, g[i]);
        if (i) w += (g[i] == B_SMALLER) ? (int)(FOOT_BIG_GAP * foot_k(c)) : FOOT_GAP;
    }
    return w;
}

/* One row or two, for a panel this wide. */
static int foot_rows(const shell_ctx *c, int sw)
{
    int left[FOOT_MAX], right[FOOT_MAX], nl, nr;
    foot_which(c, left, &nl, right, &nr);
    if (nl + nr == 0) return 0;
    int need = group_w(c, left, nl) + group_w(c, right, nr) +
               ((nl && nr) ? 2 * FOOT_GAP : 0) + 2 * FOOT_PAD;
    return (nr && need > sw) ? 2 : 1;
}

static int foot_band_h(const shell_ctx *c, int rows)
{
    return rows * item_h(c) + (rows + 1) * FOOT_PAD;
}

int foot_buttons(const shell_ctx *c, int sw, int sh, rect *out, int *which)
{
    if (c->no_foot) return 0;
    int left[FOOT_MAX], right[FOOT_MAX], nl, nr;
    foot_which(c, left, &nl, right, &nr);
    int rows = foot_rows(c, sw);
    if (!rows) return 0;
    int ih = item_h(c), h = foot_band_h(c, rows), top = sh - h;
    int y0 = top + FOOT_PAD;
    int y1 = rows == 2 ? y0 + ih + FOOT_PAD : y0;

    int n = 0;
    /* The left group starts flush in the corner. A corner cannot be
     * overshot by a hand that is not steady. */
    int x = FOOT_PAD;
    for (int i = 0; i < nl; i++) {
        int w = item_w(c, left[i]);
        out[n] = (rect){ x, y0, w, ih };
        which[n++] = left[i];
        x += w + FOOT_GAP;
    }
    /* The right group runs inward from the other corner. */
    x = sw - FOOT_PAD;
    for (int i = 0; i < nr; i++) {
        int w = item_w(c, right[i]);
        x -= w;
        out[n] = (rect){ x, y1, w, ih };
        which[n++] = right[i];
        x -= (right[i] == B_BIGGER) ? (int)(FOOT_BIG_GAP * foot_k(c)) : FOOT_GAP;
    }
    return n;
}

/* ── the three things she can do to the machine ─────────────────── */

enum { P_OFF = 1, P_RESTART = 2, P_SLEEP = 3, P_BACK = 4 };

static const struct { int id; const char *label, *note; } POWER[] = {
    { P_OFF,     "Turn it off",
                 "Everything closes. It will not come on by itself." },
    { P_RESTART, "Start it again",
                 "It goes off and comes straight back on." },
    { P_SLEEP,   "Let it sleep",
                 "The screen goes dark. Press a key to wake it." },
    { P_BACK,    "Never mind",
                 "Go back to what you were doing." },
};
#define N_POWER ((int)(sizeof POWER / sizeof POWER[0]))

/* Can this computer sleep at all? The kernel says so in one file, and
 * offering a person a button for something their machine cannot do is
 * the failure this product keeps finding in itself. */
static int can_sleep(void)
{
    FILE *f = fopen("/sys/power/state", "r");
    if (!f) return 0;
    char buf[128] = {0};
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return 0; }
    fclose(f);
    return strstr(buf, "mem") != NULL || strstr(buf, "freeze") != NULL;
}

int foot_power_open(const shell_ctx *c) { return c && c->power_open; }

int foot_power_buttons(const shell_ctx *c, int sw, int sh,
                       rect *out, int *which)
{
    if (!c || c->kiosk) return 0;
    int body = sh - foot_height(c);
    if (body <= 0) body = sh;
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;

    int n = 0;
    int ids[FOOT_POWER_MAX];
    for (int i = 0; i < N_POWER && n < FOOT_POWER_MAX; i++) {
        if (POWER[i].id == P_SLEEP && !can_sleep()) continue;
        ids[n++] = POWER[i].id;
    }

    /* One under another, big, down the left measure. A column rather
     * than a row because these are not equivalent choices to be
     * scanned -- they are read one at a time, and the last of them is
     * the way out. */
    int gx = sw / 12; if (gx < 20) gx = 20;
    int w = sw - 2 * gx; if (w > (int)(560.f * k)) w = (int)(560.f * k);
    int h = (int)(46.f + 20.f * k); if (h < FOOT_TARGET) h = FOOT_TARGET;
    int gap = (int)(10.f * k); if (gap < 8) gap = 8;

    int total = n * h + (n - 1) * gap;
    int top = (body - total) / 2 + (int)(20.f * k);
    if (top < (int)(70.f * k)) top = (int)(70.f * k);
    if (top + total > body) top = body - total;
    if (top < 0) top = 0;

    for (int i = 0; i < n; i++) {
        out[i] = (rect){ gx, top + i * (h + gap), w, h };
        which[i] = ids[i];
    }
    return n;
}

int foot_height(const shell_ctx *c)
{
    if (!c || c->no_foot) return 0;
    int rows = foot_rows(c, c->screen_w > 0 ? c->screen_w : 1024);
    return rows ? foot_band_h(c, rows) : 0;
}

int foot_help_open(const shell_ctx *c) { return c && c->help_open; }

/* ── "Show me how to do things" ─────────────────────────────────────
 *
 * The panel explains the band; the pages show everything else, step by
 * step. First on the panel, because it is the thing she came for. */
#define HOVER_PAGES (-200)       /* foot_hover; the power choices use -10x */
#define HOVER_OUT   (-201)

/* "Get me out of this page" (src/aurshell/guard.c), above it whenever a
 * program is open: a frightened person presses Help (docs/EASY.md rule
 * 7), and getting away from a page that shouts at her comes before
 * reading how to do things. Same size, same place, every look. */
int foot_help_out(const shell_ctx *c, int sw, int sh, rect *out)
{
    if (!c || c->kiosk || c->n_wins <= 0) return 0;
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
    int top = sh - foot_height(c);
    int gx = sw / 12; if (gx < 20) gx = 20;
    int w = (int)(440.f * k); if (w > sw - 2 * gx) w = sw - 2 * gx;
    int h = (int)(56.f * k);  if (h < FOOT_TARGET) h = FOOT_TARGET;
    *out = (rect){ gx, top / 12, w, h };
    return 1;
}

int foot_help_button(const shell_ctx *c, int sw, int sh, rect *out)
{
    if (!c || c->kiosk) return 0;     /* a kiosk has no way to close it */
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
    int top = sh - foot_height(c);
    int gx = sw / 12; if (gx < 20) gx = 20;
    int w = (int)(440.f * k); if (w > sw - 2 * gx) w = sw - 2 * gx;
    int h = (int)(56.f * k);  if (h < FOOT_TARGET) h = FOOT_TARGET;
    rect o;
    int y = top / 12;
    if (foot_help_out(c, sw, sh, &o)) y = o.y + o.h + (int)(12.f * k);
    *out = (rect){ gx, y, w, h };
    return 1;
}

static int out_button_at(const shell_ctx *c, int x, int y)
{
    rect r;
    return foot_help_out(c, c->screen_w, c->screen_h + foot_height(c), &r) &&
           x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int help_pages_here(void) { return access(FOOT_HELP_PAGES, R_OK) == 0; }

static int help_button_at(const shell_ctx *c, int x, int y)
{
    rect r;
    return help_pages_here() &&
           foot_help_button(c, c->screen_w, c->screen_h + foot_height(c), &r) &&
           x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void open_help_pages(shell_ctx *c)
{
    c->help_open = 0;
    if (!c->spawn || !help_pages_here()) return;
    /* A NEW window, because a new window is what the shell brings to
     * the front. A page handed to a browser that is already open
     * becomes a tab in a window she may not be looking at. */
    static const char ARGV[] =
        "sh\0-c\0"
        "command -v firefox >/dev/null 2>&1 && exec firefox --new-window \"$0\"; "
        "exec xdg-open \"$0\"\0"
        "file://" FOOT_HELP_PAGES;
    c->spawn(c, ARGV, 4);
}

/* ── her chosen size, kept where she can own it ─────────────────────
 *
 * Her own settings file, not the system theme: changing how big the
 * words are must not need a password, and must survive a restart. */
static void scale_path(char *out, size_t n)
{
    const char *home = getenv("HOME");
    if (!home || !*home) home = "/tmp";
    snprintf(out, n, "%s/.config/auros", home);
    mkdir(out, 0755);
    snprintf(out, n, "%s/.config/auros/text-scale", home);
}

float foot_load_text_scale(void)
{
    char p[512]; scale_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return 1.f;
    float v = 1.f;
    if (fscanf(f, "%f", &v) != 1) v = 1.f;
    fclose(f);
    if (v < SCALE_MIN) v = SCALE_MIN;
    if (v > SCALE_MAX) v = SCALE_MAX;
    return v;
}

void foot_save_text_scale(float scale)
{
    char p[512]; scale_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;                      /* not being able to remember
                                          * is not a reason to refuse */
    fprintf(f, "%.2f\n", (double)scale);
    fclose(f);
}

/* ── painting ───────────────────────────────────────────────────────*/

/* An outlined box on the fixed colours: a 2 px rule, radius 8, the
 * icon above its label. Hover changes only the rule. */
static void paint_button(surface *s, shell_fonts *f, rect r, shell_icon ic,
                         const char *label, int hot, int icon)
{
    corners rr = corners_all(8.f);
    draw_round_rect(s, r, rr, hot ? FOOT_HOT : FOOT_BG, 1.f);
    draw_round_rect_border(s, r, rr, 2.f, hot ? FOOT_INK : FOOT_DIM, 1.f);
    font *ft = f->label ? f->label : (f->mid ? f->mid : f->small);
    float by;
    if (icon) {
        float isz = (float)r.h * 0.40f;
        float icy = (float)r.y + (float)r.h * 0.30f;
        shell_icon_draw(s, ic, (float)r.x + (float)r.w * 0.5f, icy, isz, FOOT_INK, FOOT_BG, 1.f);
        if (!ft) return;
        by = (float)r.y + (float)r.h - 8.f - font_descent(ft);
    } else {
        if (!ft) return;
        by = shell_baseline(ft, (float)r.y, (float)r.h);
    }
    float tw = shell_text_w(ft, label);
    if (tw <= (float)r.w - 10.f)
        shell_text(s, ft, (float)r.x + ((float)r.w - tw) * 0.5f, by, label, FOOT_INK, 1.f);
    else
        shell_text_elided(s, ft, (float)r.x + 5.f, by, (float)r.w - 10.f, label, FOOT_INK, 1.f);
}

void foot_paint(shell_ctx *c, surface *s, shell_fonts *f)
{
    int h = foot_height(c);
    if (h <= 0) return;
    int top = s->h - h;

    /* The help panel sits above the archetype and below the band, so
     * the way out of it is never covered by it. */
    if (c->help_open) {
        rect all = { 0, 0, s->w, top };
        draw_rect(s, all, FOOT_BG, 0.94f);

        font *big = f->big ? f->big : f->mid;
        font *body = f->small ? f->small : f->mid;
        int x = s->w / 12, y = top / 6;
        rect hb;
        if (foot_help_out(c, s->w, s->h, &hb)) {
            float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
            draw_rect(s, hb, FOOT_INK, 1.f);
            if (c->foot_hover == HOVER_OUT)
                draw_frame(s, (rect){ hb.x - 4, hb.y - 4, hb.w + 8, hb.h + 8 },
                           2, FOOT_INK, 1.f);
            font *nm = f->mid ? f->mid : f->small;
            if (nm)
                shell_text_elided(s, nm, (float)hb.x + 18.f,
                                  shell_baseline(nm, (float)hb.y, (float)hb.h),
                                  (float)hb.w - 36.f, "Get me out of this page",
                                  FOOT_BG, 1.f);
            x = hb.x;
            y = hb.y + hb.h + (int)(28.f * k);
        }
        if (help_pages_here() && foot_help_button(c, s->w, s->h, &hb)) {
            /* Filled, the one lit thing on the panel: it is what she
             * came for. Hover rings it rather than recolouring it. */
            float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
            draw_rect(s, hb, FOOT_INK, 1.f);
            if (c->foot_hover == HOVER_PAGES)
                draw_frame(s, (rect){ hb.x - 4, hb.y - 4, hb.w + 8, hb.h + 8 },
                           2, FOOT_INK, 1.f);
            font *nm = f->mid ? f->mid : f->small;
            if (nm)
                shell_text_elided(s, nm, (float)hb.x + 18.f,
                                  shell_baseline(nm, (float)hb.y, (float)hb.h),
                                  (float)hb.w - 36.f, "Show me how to do things",
                                  FOOT_BG, 1.f);
            x = hb.x;
            y = hb.y + hb.h + (int)(28.f * k);
        }
        if (big) {
            shell_text(s, big, (float)x, (float)y + font_ascent(big),
                       "Where you are", FOOT_INK, 1.f);
            y += (int)(font_line_height(big) * 1.6f);
        }
        if (body) {
            float lh = font_line_height(body) * 1.45f;
            static const char *LINES[] = {
                "This bar at the bottom is always here. It never goes away.",
                "",
                "Bigger text and Smaller text change the size of the words.",
                "If the words have become too big to use, press Smaller text.",
                "",
                "Wi-Fi shows the networks in range, so you can join yours.",
                "",
                "Settings has the brightness, the sound and the clock.",
                "",
                "Close this shuts whatever you are looking at.",
                "",
                "Turn off shuts the computer down properly.",
                "",
                "Press Help again to close this.",
                NULL
            };
            for (int i = 0; LINES[i]; i++) {
                if (LINES[i][0])
                    shell_text(s, body, (float)x, (float)y + font_ascent(body),
                               LINES[i], FOOT_INK, 0.92f);
                y += (int)lh;
            }
        }
    }

    /* The three choices, above the archetype and below the band --
     * the way out of them is never covered by them. */
    if (c->power_open) {
        rect all = { 0, 0, s->w, top };
        draw_rect(s, all, FOOT_BG, 0.96f);

        font *big = f->big ? f->big : f->mid;
        font *nm  = f->mid ? f->mid : f->small;
        font *sm  = f->tiny ? f->tiny : f->small;
        float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
        int gx = s->w / 12; if (gx < 20) gx = 20;
        if (big)
            shell_text(s, big, (float)gx, (float)(40.f * k),
                       "What should this computer do?", FOOT_INK, 1.f);

        rect pb[FOOT_POWER_MAX]; int pw[FOOT_POWER_MAX];
        int pn = foot_power_buttons(c, s->w, s->h, pb, pw);
        for (int i = 0; i < pn; i++) {
            int hot = (c->foot_hover == -100 - pw[i]) ||
                      (c->power_sel  == pw[i]);
            draw_rect(s, pb[i], hot ? FOOT_HOT : FOOT_BG, 1.f);
            draw_frame(s, pb[i], 1, FOOT_RULE, hot ? 1.f : 0.6f);
            const char *label = "", *note = "";
            for (int j = 0; j < N_POWER; j++)
                if (POWER[j].id == pw[i]) { label = POWER[j].label;
                                            note = POWER[j].note; }
            if (nm)
                shell_text(s, nm, (float)pb[i].x + 16.f,
                           (float)pb[i].y + (float)(14.f * k) + font_ascent(nm),
                           label, FOOT_INK, 1.f);
            if (sm)
                shell_text_elided(s, sm, (float)pb[i].x + 16.f,
                                  (float)pb[i].y + pb[i].h - (float)(12.f * k),
                                  (float)pb[i].w - 32.f, note, FOOT_DIM, 0.9f);
        }
    }

    rect band = { 0, top, s->w, h };
    draw_rect(s, band, FOOT_BG, 1.f);
    draw_hrule(s, 0, top, s->w, 1, FOOT_RULE, 1.f);

    rect r[B_N]; int which[B_N];
    int n = foot_buttons(c, s->w, s->h, r, which);
    for (int i = 0; i < n; i++)
        paint_button(s, f, r[i], ICON[which[i]], LABEL[which[i]],
                     c->foot_hover == which[i], with_icons(c));
}

/* ── input ──────────────────────────────────────────────────────────*/

void foot_motion(shell_ctx *c, int x, int y)
{
    c->foot_hover = -1;
    if (c->power_open && y < c->screen_h) {
        rect pb[FOOT_POWER_MAX]; int pw[FOOT_POWER_MAX];
        int pn = foot_power_buttons(c, c->screen_w,
                                    c->screen_h + foot_height(c), pb, pw);
        for (int i = 0; i < pn; i++)
            if (x >= pb[i].x && x < pb[i].x + pb[i].w &&
                y >= pb[i].y && y < pb[i].y + pb[i].h) {
                c->foot_hover = -100 - pw[i];
                c->power_sel  = pw[i];     /* the two agree, always */
                return;
            }
        /* The pointer is on this screen but not on a choice. Whatever
         * the arrow keys last picked is still picked: this function
         * runs after EVERY input event, so without this the highlight
         * made with the keyboard would be erased by the next keypress
         * -- including the Return meant to act on it. */
        if (c->power_sel) c->foot_hover = -100 - c->power_sel;
        return;
    }
    if (c->help_open && out_button_at(c, x, y)) { c->foot_hover = HOVER_OUT; return; }
    if (c->help_open && help_button_at(c, x, y)) { c->foot_hover = HOVER_PAGES; return; }
    rect r[B_N]; int which[B_N];
    int n = foot_buttons(c, c->screen_w, c->screen_h + foot_height(c), r, which);
    for (int i = 0; i < n; i++)
        if (x >= r[i].x && x < r[i].x + r[i].w &&
            y >= r[i].y && y < r[i].y + r[i].h) { c->foot_hover = which[i]; return; }
}

/* Toggle one panel and close every other.
 *
 * Each case used to clear the others by name, and one of them -- the
 * headphones panel, which is opened from inside Settings rather than
 * from the band -- was not on any of those lists. So: Settings, then
 * "Headphones and mice", then Settings again, and BOTH were open. The
 * headphones panel was what she could see; every press went to the
 * invisible Settings panel underneath, because painting goes back to
 * front and hit-testing goes front to back and the two had been given
 * different ideas about what was showing.
 *
 * One function. A panel that is not in it cannot be forgotten by it. */
void foot_open_only(shell_ctx *c, int *flag)
{
    int want = !*flag;
    c->help_open = c->net_open = c->settings_open = 0;
    c->bt_open = c->power_open = 0;
    /* AND THE WELCOME QUESTION, which nothing on the band opens and
     * which therefore never reaches this function by the front door.
     * It still has to go: it is painted over the other panels, so a
     * Settings opened underneath it would take clicks that land on
     * something she cannot see. Closing it answers nothing and it is
     * back the next time the machine is switched on -- which is what
     * "Let me look first" does, and it is the right meaning for
     * "she pressed Settings" too. */
    c->welcome_open = 0;
    c->guard_open = 0;
    c->wish_open = 0;          /* Wishes: a panel like the others     */
    c->power_sel = 0;          /* nothing is chosen until she chooses */
    *flag = want;
}

/* The band and its two overlays, from the keyboard.
 *
 * The three power choices had no keyboard at all: no way to pick one,
 * and -- worse -- no way to DISMISS the question, on a screen that
 * covers everything she was doing. docs/EASY.md rule 6 says every
 * state has a way out. Escape is that way out here, and it means the
 * same thing the last button means: never mind.
 *
 * Returns 1 if the key was ours. While either overlay is up that is
 * every key, because the overlay is modal and a key that fell past it
 * would reach an application she cannot see. */
int foot_key(shell_ctx *c, int k)
{
    if (!c) return 0;

    if (c->power_open) {
        rect pb[FOOT_POWER_MAX]; int pw[FOOT_POWER_MAX];
        int pn = foot_power_buttons(c, c->screen_w,
                                    c->screen_h + foot_height(c), pb, pw);
        if (pn <= 0) { c->power_open = 0; c->power_sel = 0; return 1; }

        int sel = -1;
        for (int i = 0; i < pn; i++) if (pw[i] == c->power_sel) sel = i;

        switch (k) {
        case 1:                                     /* Escape          */
            c->power_open = 0; c->power_sel = 0;
            return 1;
        case 108: case 15:                          /* Down, Tab       */
            sel = (sel < 0) ? 0 : (sel + 1) % pn;
            break;
        case 103:                                   /* Up              */
            sel = (sel <= 0) ? pn - 1 : sel - 1;
            break;
        case 28: case 96: case 57:                  /* Return, KP, Sp  */
            /* Nothing picked yet means nothing happens. A blind Return
             * that turned the machine off would be the worst key in
             * this program. */
            if (sel < 0) return 1;
            if (pw[sel] == P_BACK) c->power_open = 0;
            else { c->want_power_off = pw[sel]; c->power_open = 0; }
            c->power_sel = 0;
            return 1;
        default:
            return 1;                               /* modal          */
        }
        c->power_sel  = pw[sel];
        c->foot_hover = -100 - pw[sel];
        return 1;
    }

    /* Any key closes help, for the same reason any click does: asking
     * her to find one particular small button again, to get rid of the
     * thing that exists to help her, would be a joke at her expense. */
    if (c->help_open) {
        /* Return is the panel's one button, "Show me how". */
        if (k == 28 || k == 96) open_help_pages(c);
        c->help_open = 0;
        return 1;
    }
    return 0;
}

int foot_click(shell_ctx *c, int x, int y)
{
    int h = foot_height(c);
    if (h <= 0) return 0;
    int full_h = c->screen_h + h;

    rect r[B_N]; int which[B_N];
    int n = foot_buttons(c, c->screen_w, full_h, r, which);
    for (int i = 0; i < n; i++) {
        if (x < r[i].x || x >= r[i].x + r[i].w ||
            y < r[i].y || y >= r[i].y + r[i].h) continue;
        switch (which[i]) {
        case B_HELP:     foot_open_only(c, &c->help_open);     return 1;
        case B_NET:      foot_open_only(c, &c->net_open);      return 1;
        case B_SETTINGS: foot_open_only(c, &c->settings_open); return 1;
        case B_SMALLER:
            c->text_scale -= SCALE_STEP;
            if (c->text_scale < SCALE_MIN) c->text_scale = SCALE_MIN;
            c->text_changed = 1;
            foot_save_text_scale(c->text_scale);
            return 1;
        case B_BIGGER:
            c->text_scale += SCALE_STEP;
            if (c->text_scale > SCALE_MAX) c->text_scale = SCALE_MAX;
            c->text_changed = 1;
            foot_save_text_scale(c->text_scale);
            return 1;
        case B_CLOSE:
            c->want_close_win = 1;
            return 1;
        case B_POWER:
            /* It used to turn the machine off, here, on one press of a
             * button that is always on screen. No confirmation, no way
             * back, and no way to do the other two things a person
             * wants from that corner of a computer. */
            foot_open_only(c, &c->power_open);
            return 1;
        }
    }

    if (c->power_open && y < c->screen_h) {
        rect pb[FOOT_POWER_MAX]; int pw[FOOT_POWER_MAX];
        int pn = foot_power_buttons(c, c->screen_w, full_h, pb, pw);
        for (int i = 0; i < pn; i++) {
            if (x < pb[i].x || x >= pb[i].x + pb[i].w ||
                y < pb[i].y || y >= pb[i].y + pb[i].h) continue;
            if (pw[i] == P_BACK) c->power_open = 0;
            else { c->want_power_off = pw[i]; c->power_open = 0; }
            c->power_sel = 0;
            return 1;
        }
        /* Inside the question but not on an answer: swallowed. A press
         * that fell through would reach the desktop underneath, and on
         * THIS screen the thing underneath is whatever she was doing
         * before she thought about turning the machine off. */
        return 1;
    }

    /* A press anywhere on the help panel closes it. Asking her to find
     * the same small button again to get rid of the thing that exists
     * to help her would be a joke at her expense. */
    if (c->help_open && y < c->screen_h) {
        if (out_button_at(c, x, y)) { foot_open_only(c, &c->guard_open); return 1; }
        if (help_button_at(c, x, y)) open_help_pages(c);
        c->help_open = 0;
        return 1;
    }

    /* Inside the band but not on a button: swallowed, so a near-miss
     * does not fall through and press whatever the archetype has at the
     * bottom of its screen. */
    return y >= full_h - h;
}
