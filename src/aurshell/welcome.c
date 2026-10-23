/* welcome.c — see welcome.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "welcome.h"
#include "draw.h"

#define NOTE_MAX  200

static struct {
    int  page;
    int  can_import;
    int  asked;             /* a word is in flight                   */
    char wanted[16];        /* WHICH word. See welcome_step.         */
    int  partial;           /* the import finished with things left  */
    int  hover_act;
    int  sel;               /* which action the keyboard is on       */
    char note[NOTE_MAX];    /* what went wrong, in the answer's words */
    int  checking;          /* she went to check; back when it closes */
    int  check_panel;       /* ...a panel (Wi-Fi), not a program      */
    int  check_wins;        /* how many windows were open before      */
    int  keyed;             /* she has moved the selection by keyboard */
} W = { .hover_act = -1, .sel = 0 };

/* Is this computer on a network? net.c answers from the kernel's routing
 * table; a harness that does not link net.c has no opinion, which here
 * means "online" -- the check then simply offers the internet. */
extern int net_online(void) __attribute__((weak));
static int offline_now_(void) { return net_online && net_online() == 0; }

/* ── reading the two files this panel is allowed to read ─────────── */

static int slurp(const char *path, char *buf, size_t n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t k = read(fd, buf, n - 1);
    close(fd);
    if (k < 0) return -1;
    buf[k] = 0;
    return (int)k;
}

/* value of `key=` in a key=value blob, or "" */
static void field(const char *blob, const char *key, char *out, size_t n)
{
    out[0] = 0;
    size_t kl = strlen(key);
    for (const char *p = blob; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t i = 0;
            while (v[i] && v[i] != '\n' && i + 1 < n) { out[i] = v[i]; i++; }
            out[i] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

/* What aurfirst last said about this machine. */
static void read_state(char *phase, size_t n)
{
    char blob[1024];
    phase[0] = 0;
    if (slurp(WELCOME_STATE, blob, sizeof blob) < 0) return;
    field(blob, "phase", phase, n);
}

static int windows_worth_importing(void)
{
    struct stat st;
    /* ferry-detect writes JSON. An empty object is "nothing found",
     * and an absent file is a machine where the scan never ran -- both
     * mean there is nothing to offer, and offering it anyway is a
     * button that fails when pressed, which this product keeps
     * deciding is worse than no button. */
    if (stat(WELCOME_FOUND, &st) != 0) return 0;
    if (st.st_size < 16) return 0;
    char blob[4096];
    if (slurp(WELCOME_FOUND, blob, sizeof blob) < 0) return 0;
    return strstr(blob, "\"device\"") != NULL || strstr(blob, "\"windows\"") != NULL;
}

/* ── asking for something ────────────────────────────────────────── */

static int ask_for(const char *word)
{
    snprintf(W.wanted, sizeof W.wanted, "%s", word);
    W.asked = 1;
    W.note[0] = 0;
    char path[512];
    snprintf(path, sizeof path, "%s/answer", WELCOME_RUN);
    /* O_EXCL: two presses in the same second must not make two
     * requests, and a request already sitting there is one the root
     * side has not picked up yet.
     *
     * AND `W.asked` IS SET EITHER WAY. It used to be set only on the
     * path that created the file, so a leftover request -- from a
     * previous session, or from an answering service that was killed
     * -- made this return "in flight" while nothing ever polled for
     * the answer. The panel then sat on "One moment" for ever, which
     * is the one screen with no way out of it. */
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0 && errno == EEXIST) return 0;
    if (fd < 0) { W.asked = 0; return -1; }
    ssize_t k = write(fd, word, strlen(word));
    close(fd);
    if (k != (ssize_t)strlen(word)) { W.asked = 0; return -1; }
    return 0;
}

/* ── the state machine ───────────────────────────────────────────── */

int welcome_init(shell_ctx *c)
{
    char phase[32];
    read_state(phase, sizeof phase);
    W.can_import = windows_worth_importing();
    W.page = W_ASK;
    W.sel = 0;
    /* ONLY WHILE THE QUESTION IS OPEN. A machine that answered it
     * yesterday, or was never converted from anything, sees nothing:
     * a desktop that greets you every morning is a desktop you stop
     * reading. */
    c->welcome_open = !strcmp(phase, "asking");
    return c->welcome_open;
}

int welcome_wait_ms(void) { return W.asked ? 200 : -1; }

int welcome_step(shell_ctx *c)
{
    /* She pressed "Open Your files" or "Open Internet" to check. When
     * she has closed what that opened, the question is back where she
     * left it: one answer away, not tomorrow. */
    if (W.checking && !c->welcome_open && c->n_wins <= W.check_wins &&
        !c->want_launch && !SHELL_PANEL_OPEN(c)) {
        W.checking = 0;
        c->welcome_open = 1;
        return 1;
    }
    if (!c->welcome_open) return 0;
    int dirty = 0;
    if (!W.asked) return dirty;

    char path[512];
    snprintf(path, sizeof path, "%s/result", WELCOME_ANSWER);
    char blob[1024];
    if (slurp(path, blob, sizeof blob) < 0) return dirty;

    char req[32], res[32], note[NOTE_MAX];
    field(blob, "request", req, sizeof req);
    field(blob, "result",  res, sizeof res);
    field(blob, "note",    note, sizeof note);
    /* A result file with no result line is one that was being written
     * while we read it. answer.sh writes it whole and renames, so this
     * should not happen -- and "should not happen" is why it is
     * checked rather than assumed. */
    if (!res[0]) return dirty;
    /* AND IT HAS TO BE AN ANSWER TO THE QUESTION THIS PANEL ASKED.
     *
     * The panel did not used to remember what it asked for, so an
     * answer to something else -- a result left over from a previous
     * session, or one a second request produced -- was acted on. A
     * confirm whose outcome nobody knows would show "Your files are
     * here. Open Files to see them." The file is left alone rather
     * than consumed: it is not ours to remove. */
    if (strcmp(req, W.wanted) != 0) return dirty;

    W.asked = 0;
    W.partial = 0;
    snprintf(W.note, sizeof W.note, "%s", note);

    if (!strcmp(res, "ok") || !strcmp(res, "partial")) {
        W.partial = !strcmp(res, "partial");
        if (!strcmp(req, "confirm"))      W.page = W_CONFIRMED;
        else if (!strcmp(req, "decline")) W.page = W_DECLINED;
        else if (!strcmp(req, "import"))  W.page = W_IMPORTED;
        else                              W.page = W_TROUBLE;
    } else {
        /* refused, failed, or a word this panel does not know. All of
         * them are "that did not work", and the note says which -- a
         * result nothing recognised must not leave the page where it
         * was, because W_WORKING has no way out. */
        W.page = W_TROUBLE;
    }
    W.sel = 0;
    return 1;
}

/* ── where everything is ─────────────────────────────────────────── */

enum { A_YES, A_LATER, A_NO, A_SURE_YES, A_SURE_NO, A_NOT_YET,
       A_IMPORT, A_SKIP, A_START, A_FILES, A_NET, A_RESTART, A_RESTART_LATER,
       A_AGAIN, A_WIFI, A_BACKQ, A_N };
/* Every button is a verb that names what happens (DESIGN-DIRECTION §4),
 * and the answers each carry one line saying where she lands. */
static const char *ACT_LABEL[A_N] = {
    "Yes, it all works", "Let me look first", "No, go back to Windows",
    "Start AurOS from now on", "Go back to Windows", "Not yet",
    "Bring my files across", "Not now", "Start using AurOS",
    "Open Your files", "Open Internet", "Restart now", "Restart later",
    "Try again", "Open Wi-Fi", "Back to the question",
};
/* The one sentence for each moment (docs/handoff/streams/K.md, "One
 * truth about going back"), checked against what aurfirst does. */
#define SAY_YES "AurOS starts every time from now on. Windows and its files stay on this computer."
#define SAY_NO  "Windows starts the next time this computer starts. AurOS stays on this computer, with anything you saved in it. Nothing is deleted."
static const char *ACT_LINE[A_N] = {
    SAY_YES,
    "Nothing changes. AurOS asks again the next time it starts.",
    SAY_NO,
    NULL, NULL, "Back to the question. Nothing changes.",
    NULL, "You can do this later from Settings.", NULL,
    NULL, NULL, NULL, "AurOS stays open until you switch off.",
    NULL,
};

#define WEL_MAX_ACT 4
typedef struct {
    int  gx, cw;
    int  head_y, sub_y, body_y;
    int  body_max_y;               /* last baseline the body may use   */
    int  check_y;                  /* top of the one check row, or 0   */
    int  rows;                     /* 1: each answer has its line beside it */
    int  line_px, line_max;        /* its line's height and most lines */
    int  no_sub;                   /* 1: no room for the sentence      */
    rect acts[WEL_MAX_ACT + 2]; int act[WEL_MAX_ACT + 2]; int n_acts;
} welcome_geom;

static int clampi_(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static welcome_view view_now(void)
{
    welcome_view v = { W.page, W.can_import };
    return v;
}

/* The answers on a page, in order. Primary first. */
static int actions_for(const welcome_view *v, int *which)
{
    int n = 0;
    switch (v->page) {
    case W_ASK:      which[n++] = A_YES; which[n++] = A_LATER; which[n++] = A_NO; break;
    case W_SURE_YES: which[n++] = A_SURE_YES; which[n++] = A_NOT_YET; break;
    case W_SURE_NO:  which[n++] = A_SURE_NO;  which[n++] = A_NOT_YET; break;
    case W_WORKING:  break;
    case W_CONFIRMED:
        if (v->can_import) { which[n++] = A_IMPORT; which[n++] = A_SKIP; }
        else which[n++] = A_START;
        break;
    case W_IMPORTING: break;
    case W_IMPORTED: which[n++] = A_FILES; which[n++] = A_START; break;
    case W_DECLINED: which[n++] = A_RESTART; which[n++] = A_RESTART_LATER; break;
    case W_TROUBLE:  which[n++] = A_AGAIN; which[n++] = A_LATER; break;
    }
    return n;
}

static int body_of(int page, int partial, const char *lines[3]);
static const char *sub_of(int page, int can_import);

/* How many lines of 18 px a sentence needs in `w` pixels: an estimate
 * on purpose (geometry has no fonts), a little generous so that what is
 * reserved is never less than what is drawn. */
static int lines_for(const char *t, int w, float k)
{
    int cpl = (int)((float)w / (9.4f * k)); if (cpl < 8) cpl = 8;
    int n = ((int)strlen(t) + cpl - 1) / cpl;
    return n < 1 ? 1 : n;
}

/* ONE FUNCTION OWNS WHERE EVERYTHING IS: painting, pressing and
 * tools/targets.c all ask it.
 *
 * The question page is a list of choices: a button, and BESIDE it, in
 * 18 px ink, what pressing it does. (It was three narrow columns with
 * the sentences at 12 px and cut off: the one screen that decides for
 * good, and the sentences that say which button keeps AurOS were the
 * smallest, greyest text on it -- people-walkthrough F-07.) The check
 * gives way first when the screen is short, then the sentences, then
 * the one under the heading: never an answer. */
static void welcome_layout(const shell_ctx *c, int sw, int sh,
                           const welcome_view *v, welcome_geom *g)
{
    memset(g, 0, sizeof *g);
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;

    g->gx = clampi_(sw / 12, 24, 120);
    g->cw = sw - 2 * g->gx;
    if (g->cw > 900) g->cw = 900;
    if (g->cw < 120) { g->gx = 8; g->cw = sw - 16; }

    int act_h = (int)(34.f + 16.f * k); if (act_h < 48) act_h = 48;
    int gap   = (int)(16.f * k); if (gap < 12) gap = 12;
    int wide  = (int)(32.f * k);            /* before the way out    */
    g->line_px = (int)(24.f * k);

    g->head_y = (int)((float)sh * 0.12f + 40.f * k);
    g->sub_y  = g->head_y + (int)(44.f * k);
    int per_line = (int)((float)g->cw / (8.f * k)); if (per_line < 10) per_line = 10;
    const char *sub0 = sub_of(v->page, v->can_import);
    int sub_lines = sub0[0] ? 1 + (int)strlen(sub0) / per_line : 1;
    if (sub_lines > 3) sub_lines = 3;
    g->body_y = g->sub_y + (int)(8.f * k) + sub_lines * (int)(30.f * k);

    int which[A_N];
    int na = actions_for(v, which);
    g->n_acts = 0;

    const char *bl[3];
    int nb = body_of(v->page, W.partial, bl), nbody = 0;
    for (int i = 0; i < nb; i++) nbody += lines_for(bl[i], g->cw, k);
    if (v->page == W_TROUBLE && W.note[0]) nbody += lines_for(W.note, g->cw, k);
    int y = nbody ? g->body_y + nbody * (int)(30.f * k) : g->body_y - (int)(6.f * k);

    /* The one check, on the question page only, with its button. */
    int check_h = act_h + (int)(12.f * k);
    if (v->page == W_ASK) {
        y = g->body_y - (int)(14.f * k);
        g->check_y = y;
        y += check_h + (int)(24.f * k);
    }

    /* Each answer: its button, and what it does beside it. */
    int bw = 0, has_lines = 0;
    for (int i = 0; i < na; i++) {
        int w = (int)(48.f + 11.5f * k * (float)strlen(ACT_LABEL[which[i]]));
        if (w < 160) w = 160;
        if (w > bw) bw = w;
        if (ACT_LINE[which[i]]) has_lines = 1;
    }
    if (bw > g->cw / 2 && has_lines) bw = g->cw / 2;
    int line_w = g->cw - bw - 24;
    int bottom = sh - (int)(16.f * k);

    for (int attempt = 0; attempt < 4; attempt++) {
        g->rows = has_lines && line_w >= 160;
        int total = 0;
        for (int i = 0; i < na; i++) {
            int rh = act_h;
            if (g->rows && ACT_LINE[which[i]]) {
                int ln = lines_for(ACT_LINE[which[i]], line_w, k);
                if (ln > 4) ln = 4;
                int lh = ln * g->line_px + 6;
                if (lh > rh) rh = lh;
            }
            total += rh + (i ? ((v->page == W_ASK && which[i] == A_NO) ? wide : gap) : 0);
        }
        if (!g->rows) {            /* plain buttons, wrapping onto more rows if they must */
            int xx = 0, nrow = 1;
            for (int i = 0; i < na; i++) {
                int w = (int)(48.f + 11.5f * k * (float)strlen(ACT_LABEL[which[i]]));
                if (w < 160) w = 160;
                if (xx + w > g->cw && xx) { xx = 0; nrow++; }
                xx += w + gap;
            }
            total = nrow * act_h + (nrow - 1) * gap;
        }
        if (y + total <= bottom) break;
        if (attempt == 0 && g->check_y) {                 /* the check goes first */
            y = g->check_y; g->check_y = 0;
        } else if (attempt == 1) {                        /* then the sentences  */
            has_lines = 0;
        } else if (attempt == 2) {                        /* then the sentence under the heading */
            g->no_sub = 1;
            y = g->head_y + (int)(16.f * k);
        }
    }
    g->line_max = 4;
    /* Whatever is left over, the answers are on the screen: they move up
     * before they go off it. */
    {
        int total = 0;
        for (int i = 0; i < na; i++) {
            int rh = act_h;
            if (g->rows && ACT_LINE[which[i]]) {
                int ln = lines_for(ACT_LINE[which[i]], line_w, k);
                if (ln > 4) ln = 4;
                if (ln * g->line_px + 6 > rh) rh = ln * g->line_px + 6;
            }
            total += rh + (i ? ((v->page == W_ASK && which[i] == A_NO && g->rows) ? wide : gap) : 0);
        }
        if (!g->rows) {
            int xx = 0, nrow = 1;
            for (int i = 0; i < na; i++) {
                int w = (int)(48.f + 11.5f * k * (float)strlen(ACT_LABEL[which[i]]));
                if (w < 160) w = 160;
                if (xx + w > g->cw && xx) { xx = 0; nrow++; }
                xx += w + gap;
            }
            total = nrow * act_h + (nrow - 1) * gap;
        }
        if (y + total > bottom) y = bottom - total;
        if (y < 0) y = 0;
    }
    g->body_max_y = (g->check_y ? g->check_y : y) - (int)(10.f * k);

    if (g->check_y) {
        int b = (int)(48.f + 11.5f * k * (float)strlen(ACT_LABEL[A_NET]));
        g->acts[g->n_acts] = (rect){ g->gx + g->cw - b, g->check_y, b, act_h };
        g->act[g->n_acts++] = A_NET;                    /* A_WIFI when offline; same place */
    }
    int x = g->gx, yy = y;
    for (int i = 0; i < na && g->n_acts < WEL_MAX_ACT + 2; i++) {
        rect r;
        int rh = act_h;
        if (g->rows) {
            if (ACT_LINE[which[i]]) {
                int ln = lines_for(ACT_LINE[which[i]], line_w, k);
                if (ln > 4) ln = 4;
                int lh = ln * g->line_px + 6;
                if (lh > rh) rh = lh;
            }
            if (i) yy += (v->page == W_ASK && which[i] == A_NO) ? wide : gap;
            r = (rect){ g->gx, yy, bw, act_h };
            yy += rh;
        } else {
            int w = (int)(48.f + 11.5f * k * (float)strlen(ACT_LABEL[which[i]]));
            if (w < 160) w = 160;
            if (x + w > g->gx + g->cw) { x = g->gx; yy += act_h + gap; }
            r = (rect){ x, yy, w, act_h };
            x += w + gap;
        }
        g->acts[g->n_acts] = r;
        g->act[g->n_acts++] = which[i];
    }
}

/* While she is checking something, the question steps aside and this
 * chip stays: the way back is always on screen (E-03). */
static rect back_chip(const shell_ctx *c, int sw)
{
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
    int w = (int)(340.f * k), h = (int)(52.f * k);
    if (w > sw - 24) w = sw - 24;
    return (rect){ (sw - w) / 2, 6, w, h };
}

int welcome_targets(const shell_ctx *c, int sw, int sh,
                    const welcome_view *v, rect *out, int max)
{
    welcome_geom g;
    welcome_layout(c, sw, sh, v, &g);
    int n = 0;
    for (int i = 0; i < g.n_acts && n < max; i++) out[n++] = g.acts[i];
    return n;
}

void welcome_set_page(int page)
{ if (page >= 0 && page < W_PAGE_N) { W.page = page; W.sel = 0; W.keyed = 0; } }
int  welcome_page(void) { return W.page; }

/* ── what it says ────────────────────────────────────────────────── */

static const char *head_of(int page)
{
    switch (page) {
    case W_ASK:       return "Does everything work?";
    case W_SURE_YES:  return "Start AurOS from now on?";
    case W_SURE_NO:   return "Go back to Windows?";
    case W_WORKING:   return "Saving your answer.";
    case W_CONFIRMED: return "AurOS starts from now on";
    case W_IMPORTING: return "Bringing your files across";
    case W_IMPORTED:  return "Your files are here";
    case W_DECLINED:  return "Windows starts next time";
    default:          return "That did not work";
    }
}

static const char *sub_of(int page, int can_import)
{
    switch (page) {
    case W_ASK:       return "Check one thing first.";
    case W_SURE_YES:  return SAY_YES;
    case W_SURE_NO:   return SAY_NO;
    case W_WORKING:   return "";
    case W_CONFIRMED: return can_import
                          ? "Your documents and pictures are still in Windows. "
                            "AurOS can copy them here."
                          : "Windows and its files stay on this computer.";
    case W_IMPORTING: return "Your files are copied. Nothing in Windows changes.";
    case W_IMPORTED:  return "Open Your files to see them.";
    case W_DECLINED:  return "Press Restart now to go back to Windows.";
    default:
        /* IT USED TO SAY "Nothing has been changed." It cannot know
         * that: confirm can write BootOrder and then fail its own
         * read-back. The note under this line is what actually
         * happened, and it comes from the thing that did it. */
        return "AurOS could not finish that.";
    }
}

static int body_of(int page, int partial, const char *lines[3])
{
    int n = 0;
    switch (page) {
    case W_IMPORTING:
        lines[n++] = "Leave the computer on until this finishes.";
        break;
    case W_IMPORTED:
        if (partial) lines[n++] = "Some things could not be brought across.";
        break;
    case W_DECLINED:
        lines[n++] = "AurOS is still on this computer, but Windows starts. "
                     "To use AurOS again, or to remove it, ask someone who "
                     "knows computers.";
        break;
    default: break;
    }
    return n;
}

static void paint_action(surface *s, shell_fonts *f, const shell_ctx *c,
                         rect r, const char *label, int hot, int primary)
{
    /* What can be pressed looks pressable: the go fill, or a 2 px rule.
     * Hover only darkens the rule (DESIGN-DIRECTION §3). */
    corners rr = corners_all(8.f);
    if (primary) draw_round_rect(s, r, rr, c->accent, 1.f);
    else {
        draw_round_rect(s, r, rr, c->surface_c, 1.f);
        draw_round_rect_border(s, r, rr, 2.f, hot ? c->fg : c->overlay, 1.f);
    }
    if (primary && hot)
        draw_round_rect_border(s, (rect){ r.x - 3, r.y - 3, r.w + 6, r.h + 6 },
                               corners_all(11.f), 2.f, c->fg, 1.f);
    font *ft = f->mid ? f->mid : f->small;
    if (ft)
        shell_text_centred(s, ft, (float)r.x + r.w / 2.f,
                           shell_baseline(ft, (float)r.y, (float)r.h),
                           label, primary ? c->bg : c->fg, 1.f);
}

void welcome_paint(shell_ctx *c, surface *s, shell_fonts *f)
{
    /* Checking something: only the way back is drawn. */
    if (!c->welcome_open) {
        if (W.checking) {
            rect b = back_chip(c, s->w);
            corners rr = corners_all(8.f);
            draw_round_rect(s, b, rr, c->surface_c, 1.f);
            draw_round_rect_border(s, b, rr, 2.f, c->fg, 1.f);
            font *ft = f->mid ? f->mid : f->small;
            if (ft) {
                float tw = shell_text_w(ft, ACT_LABEL[A_BACKQ]);
                float x0 = (float)b.x + ((float)b.w - tw - 36.f) * 0.5f;
                shell_icon_draw(s, ICON_BACK, x0 + 11.f, (float)b.y + (float)b.h * 0.5f,
                                24.f, c->fg_hi, c->surface_c, 1.f);
                shell_text(s, ft, x0 + 36.f, shell_baseline(ft, (float)b.y, (float)b.h),
                           ACT_LABEL[A_BACKQ], c->fg_hi, 1.f);
            }
        }
        return;
    }
    int sw = s->w, sh = c->screen_h;
    if (sh <= 0 || sh > s->h) sh = s->h;

    welcome_view v = view_now();
    welcome_geom g;
    welcome_layout(c, sw, sh, &v, &g);
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;

    /* Paper, opaque: a question, not a veil over the desktop. */
    draw_rect(s, (rect){ 0, 0, sw, sh }, c->bg, 1.f);

    font *head  = f->huge ? f->huge : (f->big ? f->big : f->mid);
    font *bodyf = f->small ? f->small : f->mid;
    font *name  = f->mid ? f->mid : f->small;
    font *lf    = f->tiny ? f->tiny : bodyf;

    if (head)
        shell_text(s, head, (float)g.gx, (float)g.head_y, head_of(W.page), c->fg, 1.f);
    const char *sub = sub_of(W.page, W.can_import);
    float lh = 30.f * k;
    if (bodyf && sub[0] && !g.no_sub)
        shell_text_wrap(s, bodyf, (float)g.gx, (float)g.sub_y, (float)g.cw, lh,
                        sub, c->subtle, 1.f, 3);

    const char *lines[3];
    int nl = body_of(W.page, W.partial, lines);
    int y = g.body_y;
    for (int i = 0; i < nl && bodyf && y <= g.body_max_y; i++)
        y += (int)lh * shell_text_wrap(s, bodyf, (float)g.gx, (float)y, (float)g.cw,
                                       lh, lines[i], c->fg, 1.f, 3);
    if (W.page == W_TROUBLE && W.note[0] && bodyf && y <= g.body_max_y)
        shell_text_wrap(s, bodyf, (float)g.gx, (float)y, (float)g.cw, lh,
                        W.note, c->fg, 1.f, 3);

    /* The check: a question on the left, its button on the right, a rule
     * under it. Offline it says so, and the button opens Wi-Fi. */
    if (g.check_y && name) {
        int offline = offline_now_();
        const char *q = offline ? "Not connected to the internet yet."
                                : "Does the internet work?";
        int bh = g.acts[0].h;
        shell_text(s, name, (float)g.gx,
                   shell_baseline(name, (float)g.check_y, (float)bh), q, c->fg, 1.f);
        draw_hrule(s, g.gx, g.check_y + bh + (int)(8.f * k), g.cw, 1, c->overlay, 1.f);
    }

    int offline = g.check_y ? offline_now_() : 0;
    for (int i = 0; i < g.n_acts; i++) {
        int id = g.act[i];
        int primary = id == A_YES || id == A_SURE_YES || id == A_SURE_NO ||
                      id == A_IMPORT || id == A_START || id == A_RESTART ||
                      id == A_AGAIN || (id == A_FILES && W.page == W_IMPORTED);
        const char *label = (id == A_NET && offline) ? ACT_LABEL[A_WIFI] : ACT_LABEL[id];
        paint_action(s, f, c, g.acts[i], label,
                     W.hover_act == id || (W.keyed && W.sel == i), primary);
        /* What pressing it does, beside it, in 18 px ink-2 and never cut. */
        if (g.rows && ACT_LINE[id] && lf) {
            rect r = g.acts[i];
            float lx = (float)(r.x + r.w) + 24.f;
            shell_text_wrap(s, lf, lx, (float)r.y + font_ascent(lf) + 4.f,
                            (float)(g.gx + g.cw) - lx, (float)g.line_px,
                            ACT_LINE[id], c->subtle, 1.f, g.line_max);
        }
    }
}

/* ── pressing it ─────────────────────────────────────────────────── */

static int hit(rect r, int x, int y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

/* Her files, or the internet, opened so she can check -- the question
 * steps aside, a chip says how to come back, and it returns by itself
 * when she has closed what she opened. */
static void check_with(shell_ctx *c, shell_icon icon)
{
    for (int i = 0; i < c->n_apps; i++)
        if (c->apps[i].icon == icon && c->apps[i].role == APP_ROLE_PROGRAM) {
            W.checking = 1;
            W.check_wins = c->n_wins;
            W.check_panel = 0;
            c->want_launch = i + 1;
            c->welcome_open = 0;
            return;
        }
}

static int do_action(shell_ctx *c, int id)
{
    const char *trouble = "AurOS could not reach the part of itself that changes this.";
    switch (id) {
    case A_YES: W.page = W_SURE_YES; W.sel = 0; return 1;
    case A_NO:  W.page = W_SURE_NO;  W.sel = 0; return 1;
    case A_NOT_YET: W.page = W_ASK;  W.sel = 0; return 1;
    case A_SURE_YES:
    case A_SURE_NO:
        if (ask_for(id == A_SURE_YES ? "confirm" : "decline") == 0) {
            W.page = W_WORKING; W.sel = 0;
        } else {
            W.page = W_TROUBLE; snprintf(W.note, sizeof W.note, "%s", trouble);
        }
        return 1;
    case A_IMPORT:
        if (ask_for("import") == 0) { W.page = W_IMPORTING; W.sel = 0; }
        else { W.page = W_TROUBLE;
               snprintf(W.note, sizeof W.note,
                        "AurOS could not start bringing your files across."); }
        return 1;
    case A_AGAIN:
        W.page = W_ASK; W.sel = 0; W.note[0] = 0;
        return 1;
    case A_FILES:
        check_with(c, ICON_FILES);
        if (W.page == W_IMPORTED) W.checking = 0;   /* done: not coming back */
        return 1;
    case A_NET:
        /* Offline, "Open Internet" would show a browser error page and
         * nowhere to go: open the Wi-Fi page instead (E-02). */
        if (offline_now_()) {
            shell_open_net(c);
            W.checking = 1; W.check_panel = 1; W.check_wins = c->n_wins;
            return 1;
        }
        check_with(c, ICON_GLOBE);
        return 1;
    case A_RESTART:
        c->want_power_off = 2;          /* the band's own restart */
        c->welcome_open = 0;
        return 1;
    case A_LATER: case A_SKIP: case A_START: case A_RESTART_LATER:
        c->welcome_open = 0;
        return 1;
    }
    return 0;
}

int welcome_click(shell_ctx *c, int x, int y)
{
    if (!c->welcome_open && W.checking) {
        rect b = back_chip(c, c->screen_w);
        if (hit(b, x, y)) {
            W.checking = 0;
            c->welcome_open = 1;
            W.page = W_ASK;
            return 1;
        }
        return 0;
    }
    if (!c->welcome_open) return 0;
    int sw = c->screen_w, sh = c->screen_h;
    welcome_view v = view_now();
    welcome_geom g;
    welcome_layout(c, sw, sh, &v, &g);
    for (int i = 0; i < g.n_acts; i++)
        if (hit(g.acts[i], x, y)) return do_action(c, g.act[i]);
    /* A PRESS ANYWHERE ELSE DOES NOTHING, and is still consumed. This
     * is a question, not a notification: pressing the background to
     * make it go away is exactly the reflex that would answer it by
     * accident. */
    return 1;
}

void welcome_motion(shell_ctx *c, int x, int y)
{
    if (!c->welcome_open) return;
    welcome_view v = view_now();
    welcome_geom g;
    welcome_layout(c, c->screen_w, c->screen_h, &v, &g);
    W.hover_act = -1;
    for (int i = 0; i < g.n_acts; i++)
        if (hit(g.acts[i], x, y)) { W.hover_act = g.act[i]; break; }
}

int welcome_key(shell_ctx *c, int k)
{
    if (!c->welcome_open) return 0;
    welcome_view v = view_now();
    welcome_geom g;
    welcome_layout(c, c->screen_w, c->screen_h, &v, &g);
    if (!g.n_acts) return 1;            /* nothing to press yet */

    /* evdev: 105 left, 106 right, 103 up, 108 down, 28 enter, 57 space,
     * 1 escape. Arrows in both axes because the actions are a row on a
     * wide screen and a column on a narrow one, and she does not know
     * which. */
    /* Until she moves it, the selection is the first ANSWER, not the
     * first button (on the question page those are the two checks). */
    if (!W.keyed) W.sel = g.check_y ? 1 : 0;
    switch (k) {
    case 105: case 103:
        W.keyed = 1;
        W.sel = (W.sel + g.n_acts - 1) % g.n_acts; return 1;
    case 106: case 108:
        W.keyed = 1;
        W.sel = (W.sel + 1) % g.n_acts; return 1;
    case 28: case 57:
        do_action(c, g.act[clampi_(W.sel, 0, g.n_acts - 1)]); return 1;
    case 1:
        /* ESCAPE IS "LET ME LOOK FIRST", NEVER AN ANSWER -- and on a
         * confirming page it is "Not yet". */
        if (W.page == W_SURE_YES || W.page == W_SURE_NO) { W.page = W_ASK; W.sel = 0; }
        else c->welcome_open = 0;
        return 1;
    }
    return 1;                           /* the panel eats the rest */
}
