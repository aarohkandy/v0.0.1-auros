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
    int  check_wins;        /* how many windows were open before      */
    int  keyed;             /* she has moved the selection by keyboard */
} W = { .hover_act = -1, .sel = 0 };

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
       A_AGAIN, A_N };
/* Every button is a verb that names what happens (DESIGN-DIRECTION §4),
 * and the answers each carry one line saying where she lands. */
static const char *ACT_LABEL[A_N] = {
    "Yes, it all works", "Let me look first", "No, go back to Windows",
    "Start AurOS from now on", "Go back to Windows", "Not yet",
    "Bring my files across", "Not now", "Start using AurOS",
    "Open Your files", "Open Internet", "Restart now", "Restart later",
    "Try again",
};
static const char *ACT_LINE[A_N] = {
    "Switching on starts AurOS from now on.",
    "Nothing changes. This comes back next time.",
    "Switching on starts Windows again.",
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
    int  checks_y;                 /* top of the two check rows, or 0  */
    int  check_h;
    int  lines;                    /* 1: a line under each answer      */
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

/* ONE FUNCTION OWNS WHERE EVERYTHING IS: painting, pressing and
 * tools/targets.c all ask it. A column of answers when a row does not
 * fit, and the explanation gives way before an answer does -- the
 * reachable answer is worth more than the sentence above it. */
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
    int gap   = (int)(12.f * k); if (gap < 8) gap = 8;
    int wide  = (int)(32.f * k);            /* before the way out    */
    int line  = (int)(30.f * k);            /* a line under a button */

    g->head_y = (int)((float)sh * 0.12f + 40.f * k);
    g->sub_y  = g->head_y + (int)(44.f * k);
    g->body_y = g->sub_y + (int)(38.f * k);

    int which[A_N];
    int na = actions_for(v, which);
    g->n_acts = 0;

    /* The two things she checks, on the question page only, each with
     * the button that lets her check it. */
    const char *bl[3];
    int nbody = body_of(v->page, W.partial, bl) + (v->page == W_TROUBLE && W.note[0]);
    int y = nbody ? g->body_y + nbody * (int)(30.f * k) : g->body_y - (int)(6.f * k);
    if (v->page == W_ASK) {
        y = g->body_y - (int)(18.f * k);
        g->check_h = act_h + (int)(12.f * k);
        g->checks_y = y;
        y += 2 * g->check_h + (int)(28.f * k);
    }

    int bw[A_N], want = 0;
    for (int i = 0; i < na; i++) {
        bw[i] = (int)(48.f + 11.5f * k * (float)strlen(ACT_LABEL[which[i]]));
        if (bw[i] < 160) bw[i] = 160;
        want += bw[i];
    }
    if (na > 1) want += (na - 2) * gap + ((v->page == W_ASK) ? wide : gap);
    int has_lines = 0;
    for (int i = 0; i < na; i++) if (ACT_LINE[which[i]]) has_lines = 1;

    int column = na > 1 && want > g->cw;
    int need = column ? na * act_h + (na - 1) * gap : act_h + (has_lines ? line : 0);
    int bottom = sh - (int)(16.f * k);
    if (y + need > bottom && g->checks_y) {          /* checks go first */
        y = g->checks_y; g->checks_y = 0;
    }
    if (y + need > bottom) {                          /* then the lines */
        has_lines = 0;
        need = column ? na * act_h + (na - 1) * gap : act_h;
    }
    if (y + need > bottom) y = bottom - need;
    if (y < g->sub_y + (int)(12.f * k)) {
        /* Still no room: the sentence under the heading gives way too,
         * never an answer. */
        g->no_sub = 1;
        if (y < g->head_y + (int)(16.f * k)) y = g->head_y + (int)(16.f * k);
    }
    g->lines = has_lines && !column;
    g->body_max_y = (g->checks_y ? g->checks_y : y) - (int)(10.f * k);

    if (g->checks_y) {
        /* the two buttons first in the target list: they are on top */
        int b = (int)(48.f + 11.5f * k * (float)strlen(ACT_LABEL[A_FILES]));
        for (int i = 0; i < 2; i++) {
            g->acts[g->n_acts] = (rect){ g->gx + g->cw - b,
                                         g->checks_y + i * g->check_h, b, act_h };
            g->act[g->n_acts++] = i ? A_NET : A_FILES;
        }
    }
    int x = g->gx;
    for (int i = 0; i < na && g->n_acts < WEL_MAX_ACT + 2; i++) {
        rect r;
        if (column) r = (rect){ g->gx, y + i * (act_h + gap), g->cw, act_h };
        else {
            /* The way out sits at the far end, a wide gap away. */
            if (v->page == W_ASK && which[i] == A_NO) x = g->gx + g->cw - bw[i];
            r = (rect){ x, y, bw[i], act_h };
            x += bw[i] + gap;
        }
        g->acts[g->n_acts] = r;
        g->act[g->n_acts++] = which[i];
    }
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
    case W_ASK:       return "Check two things first.";
    case W_SURE_YES:  return "Switching this computer on will start AurOS. "
                             "Windows stays on the disk.";
    case W_SURE_NO:   return "Switching this computer on will start Windows "
                             "again. AurOS stays on the disk.";
    case W_WORKING:   return "";
    case W_CONFIRMED: return can_import
                          ? "Your documents and pictures are still in Windows. "
                            "AurOS can copy them here."
                          : "Windows is still on this disk.";
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
        lines[n++] = "AurOS is still on this disk. Nothing has been deleted.";
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
    if (!c->welcome_open) return;
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

    if (head)
        shell_text(s, head, (float)g.gx, (float)g.head_y, head_of(W.page), c->fg, 1.f);
    const char *sub = sub_of(W.page, W.can_import);
    if (bodyf && sub[0] && !g.no_sub)
        shell_text_elided(s, bodyf, (float)g.gx, (float)g.sub_y, (float)g.cw,
                          sub, c->subtle, 1.f);

    const char *lines[3];
    int nl = body_of(W.page, W.partial, lines);
    int y = g.body_y;
    for (int i = 0; i < nl && bodyf && y <= g.body_max_y; i++) {
        shell_text_elided(s, bodyf, (float)g.gx, (float)y, (float)g.cw,
                          lines[i], c->fg, 1.f);
        y += (int)(30.f * k);
    }
    if (W.page == W_TROUBLE && W.note[0] && bodyf && y <= g.body_max_y)
        shell_text_elided(s, bodyf, (float)g.gx, (float)y, (float)g.cw,
                          W.note, c->fg, 1.f);

    /* The two checks: a question on the left, its button on the right,
     * a rule under each. */
    if (g.checks_y && name) {
        static const char *Q[2] = { "Can you see your photos?",
                                    "Does the internet work?" };
        for (int i = 0; i < 2; i++) {
            int ry = g.checks_y + i * g.check_h;
            int bh = g.acts[i].h;
            shell_text(s, name, (float)g.gx, shell_baseline(name, (float)ry, (float)bh),
                       Q[i], c->fg, 1.f);
            draw_hrule(s, g.gx, ry + g.check_h - (int)(6.f * k), g.cw, 1, c->overlay, 1.f);
        }
    }

    for (int i = 0; i < g.n_acts; i++) {
        int id = g.act[i];
        int primary = id == A_YES || id == A_SURE_YES || id == A_SURE_NO ||
                      id == A_IMPORT || id == A_START || id == A_RESTART ||
                      id == A_AGAIN || (id == A_FILES && W.page == W_IMPORTED);
        paint_action(s, f, c, g.acts[i], ACT_LABEL[id],
                     W.hover_act == id || (W.keyed && W.sel == i), primary);
        if (g.lines && ACT_LINE[id] && f->tiny) {
            /* As wide as the room before the next button: a line she
             * needs is never cut short. */
            rect r = g.acts[i];
            float lx = (float)r.x, lw = (float)(g.gx + g.cw) - lx;
            if (i + 1 < g.n_acts && g.acts[i + 1].y == r.y)
                lw = (float)g.acts[i + 1].x - 12.f - lx;
            shell_text_elided(s, f->tiny, lx, (float)(r.y + r.h) + 24.f * k, lw,
                              ACT_LINE[id], c->subtle, 1.f);
        }
    }
}

/* ── pressing it ─────────────────────────────────────────────────── */

static int hit(rect r, int x, int y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

/* Her files, or the internet, opened so she can check -- the question
 * steps aside and comes back when she closes what it opened. */
static void check_with(shell_ctx *c, shell_icon icon)
{
    for (int i = 0; i < c->n_apps; i++)
        if (c->apps[i].icon == icon && c->apps[i].role == APP_ROLE_PROGRAM) {
            W.checking = 1;
            W.check_wins = c->n_wins;
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
    if (!W.keyed) W.sel = g.checks_y ? 2 : 0;
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
