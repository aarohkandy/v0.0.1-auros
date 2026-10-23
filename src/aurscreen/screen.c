/* screen.c — what the one restart looks like
 *
 * docs/issues/v3/05 item 5.5. After AurOS Setup restarts the computer,
 * the staging environment runs for somewhere between ten minutes and an
 * hour, and until this existed what a person saw for all of it was the
 * kernel's console: "aurstage: drive    ok, SMART says healthy",
 * " 40% 50% 60%", and an occasional sentence in among them. It could be
 * read. It was not something anybody who had never seen a console
 * should be asked to sit in front of while their computer's only copy
 * of Windows is being shrunk.
 *
 * So this paints a screen (docs/DESIGN-DIRECTION.md §5 C P0-3, Light):
 * the drawn mark beside "AurOS"; where the install is, as a list of
 * steps with "Step N of M" and, once it can be measured, "About N
 * minutes left" under them; the sentence aurstage last said, which is
 * already written for her; and, from the moment the Windows drive is
 * touched until it is finished, "Do not turn the computer off" in a
 * band nobody can miss. No log lines and no sizes: a sentence that
 * carries a size stays on the console.
 *
 * Only when something has FAILED: a helper code in digits, and a
 * "Show details for a helper" button that shows the raw lines a tester
 * asks a photo of (the verdict= line first). A code on a screen where
 * nothing went wrong looks like an error, so it is never shown then.
 * The button reads the keyboard and the mouse, never grabs them, and is
 * not drawn at all on a machine where neither was found: a button that
 * does nothing when she presses it is worse than none.
 *
 * HOW IT IS FED, AND WHY IT CANNOT HURT THE INSTALL
 *
 * aurstage is PID 1 and the only program that writes to a disk. It
 * starts this with a pipe on its standard input and copies every line
 * it says into the pipe as well as onto the console (boot.c). The
 * write end is non-blocking and SIGPIPE is ignored, so a screen that
 * hangs, crashes or never starts costs aurstage nothing: the lines are
 * dropped and the console still has them. This program opens no disk,
 * reads no disk, and takes no decision; it only listens (to the pipe,
 * and to /dev/input, read-only).
 *
 * It lives outside src/aurstage on purpose. build/staging's write gate
 * says exactly one file in that directory may open a device writably;
 * a display device is not a disk, but a gate with an exception for
 * "devices that are not disks" is the exception nobody reviewed.
 *
 *   aurscreen                        paint to the first DRM device
 *   aurscreen --png F --size W H     read all of stdin, paint the last
 *                                    state to F (tools/screentest.sh)
 *   aurscreen --state                read all of stdin, print the state
 *   aurscreen --mode install|restore|dry   instead of /proc/cmdline
 *   aurscreen --fonts DIR            where ui.ttf / ui-bold.ttf are
 *   aurscreen --code VERDICT         print the helper code of a verdict
 *   aurscreen --said                 with --png/--state: also print every
 *                                    string the picture draws, "said=..."
 *
 * Off-screen only (--png, --state), stdin may also carry, for tests:
 *   aurscreen-at SECONDS     the clock, for "About N minutes left"
 *   aurscreen-press X Y      a pointer press at X,Y of the --size picture
 *   aurscreen-key            Enter
 * They are read by main(), never by feed(): aurstage cannot say them.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>

#include "../aurshell/kms.h"
#include "../aurshell/draw.h"
#include "../common/font.h"
#include "../common/png.h"

/* The Light tokens (DESIGN-DIRECTION §3). Hardcoded, like aursorry's:
 * nothing here may depend on a theme file the install has not written
 * yet. One green; no red, no blue, no gradients. */
#define PAPER 0xF6F2E9u
#define CARD  0xFCFAF5u
#define SOFT  0xECE6DAu
#define RULE  0x6E675Cu
#define INK   0x17181Au
#define INK2  0x3A3D42u
#define GO    0x0B5D4Fu
#define WARN  0x8A5A12u
#define WARNT 0xF7EBD2u

enum { M_INSTALL, M_RESTORE, M_DRY };

/* The seven steps, and the sentence of aurstage's that starts each.
 * Matched on a prefix of a sentence install.c already says, so a step
 * is never claimed before the code that does it has started. */
static const struct { const char *starts, *label; } STEP[] = {
    { "",                                            "Checking this computer" },
    { "Saving this computer's Windows startup",      "Saving the way back to Windows" },
    { "Making room on the Windows drive",            "Making room on the Windows drive" },
    { "Copying AurOS onto this computer",            "Copying AurOS" },
    { "Making this computer able to start AurOS",    "Setting up how it starts" },
    { "Checking that this computer works under AurOS","Checking AurOS works here" },
    { "Writing the new layout",                      "Finishing" },
};
#define NSTEP ((int)(sizeof STEP / sizeof STEP[0]))
#define RISKY_FROM 2   /* "Making room": the part that cannot be undone */
#define COPY_STEP  3   /* the long one; nothing after it is estimated before it */

/* The restore's steps. It speaks in detail lines; these start each. */
static const struct { const char *starts, *label; } RSTEP[] = {
    { "",                         "Checking this computer" },
    { "checking the saved copy",  "Checking the way back to Windows" },
    { "restoring ",               "Putting Windows back" },
};
#define NRSTEP ((int)(sizeof RSTEP / sizeof RSTEP[0]))

#define NTAIL 12
typedef struct {
    int  mode;
    int  step;                  /* index into STEP or RSTEP             */
    int  pct;                   /* -1: nothing to measure               */
    double p_t0, p_t1;          /* when this step's first and last      */
    int  p0;                    /*   percentages came, and the first    */
    char para[4][480];          /* what aurstage last said, a paragraph
                                 * per entry, its lines joined          */
    int  npara;
    int  para_open;             /* 1: open; 2: open, not for her (a size) */
    int  after;                 /* a verdict was said: keep every paragraph */
    int  last_dot;              /* the last sentence line ended with "." */
    char tail[NTAIL][200];      /* the last raw lines, for a helper     */
    int  ntail;
    char verdict[96];           /* from "aurstage-report v1 verdict=..." */
    char report[200];           /* that whole line, kept for the photo   */
    int  stopped;               /* a refusal or failure was reported    */
    int  finished;              /* the restore put Windows back         */
    char warn[200];             /* the last WARNING:, the why, for a helper */
    int  handover;              /* the install finished                 */
    int  details;               /* the helper page is showing           */
    double page_at, press_at;   /* one press is one press (§3 Input)    */
    int  dirty;
} state;

static void push_tail(state *s, const char *l)
{
    if (s->ntail == NTAIL) {
        memmove(s->tail[0], s->tail[1], sizeof s->tail[0] * (NTAIL - 1));
        s->ntail--;
    }
    snprintf(s->tail[s->ntail++], sizeof s->tail[0], "%s", l);
}

/* "drive    ok" and "         a remedy": aurstage's two-column detail
 * lines. Everything else that starts with a capital is a sentence
 * written for the person in front of the computer. */
static int is_detail(const char *t)
{
    if (t[0] == ' ') return 1;
    int i = 0;
    while (t[i] >= 'a' && t[i] <= 'z') i++;
    return i > 0 && i <= 8 && t[i] == ' ' && t[i + 1] == ' ';
}

/* "61.2 GiB", "8 MB", "512 bytes": never on her screen (§6 rule 10). */
static int has_size(const char *t)
{
    static const char *const U[] = { "B", "KB", "KiB", "MB", "MiB", "GB", "GiB",
                                     "TB", "TiB", "bytes", NULL };
    for (const char *p = t; *p; p++) {
        if (*p < '0' || *p > '9') continue;
        const char *q = p;
        while ((*q >= '0' && *q <= '9') || *q == '.' || *q == ',') q++;
        if (*q == ' ') q++;
        for (int i = 0; U[i]; i++) {
            size_t n = strlen(U[i]);
            if (!strncmp(q, U[i], n) && !((q[n] >= 'a' && q[n] <= 'z') || (q[n] >= 'A' && q[n] <= 'Z')))
                return 1;
        }
        p = q - 1 > p ? q - 1 : p;
    }
    return 0;
}

/* aurstage's own words are being reworded (install.c's last two lines
 * of "what next" still say AurBridge and Start installing, and that is
 * stream A2's file). Her screen never says the old names, whatever
 * build of aurstage is feeding it.
 * ponytail: delete when install.c says AurOS Setup / Install AurOS. */
static void plain(char *dst, size_t n, const char *src)
{
    static const char *const W[][2] = { { "AurBridge", "AurOS Setup" },
                                        { "Start installing", "Install AurOS" } };
    snprintf(dst, n, "%s", src);
    for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
        char *p;
        while ((p = strstr(dst, W[i][0]))) {
            char rest[512];
            snprintf(rest, sizeof rest, "%s", p + strlen(W[i][0]));
            snprintf(p, n - (size_t)(p - dst), "%s%s", W[i][1], rest);
        }
    }
}

static void set_step(state *s, int i)
{
    s->step = i; s->pct = -1; s->p_t0 = -1;
}

static void feed(state *s, char *line, double now)
{
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;

    if (!strncmp(line, "aurstage-progress ", 18)) {
        int p = atoi(line + 18);
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        if (s->p_t0 < 0) { s->p_t0 = now; s->p0 = p; }
        s->p_t1 = now;
        if (p != s->pct) { s->pct = p; s->dirty = 1; }
        return;
    }
    const char *t = line;
    if (!strncmp(t, "aurstage: ", 10)) t += 10;
    if (*t) push_tail(s, t);
    s->dirty = 1;

    const char *v = strstr(t, "aurstage-report v1 verdict=");
    if (v) {
        v += 27;
        size_t k = strcspn(v, " ");
        if (k >= sizeof s->verdict) k = sizeof s->verdict - 1;
        memcpy(s->verdict, v, k); s->verdict[k] = 0;
        /* Every restore-* verdict is a restore that stopped: nothing
         * saved, no usable copy, failed. "restored" is the one that
         * did not. Missing these left "Putting Windows back" on the
         * screen of a machine paused for good (found by review). */
        snprintf(s->report, sizeof s->report, "%s", v - 27);
        s->finished = !strcmp(s->verdict, "restored");
        int was = s->stopped;
        s->stopped = strstr(t, "record=refused") || strstr(t, "record=failed") ||
                     !strncmp(s->verdict, "no-", 3) || !strncmp(s->verdict, "restore-", 8) ||
                     strstr(s->verdict, "fail") || strstr(s->verdict, "refus");
        if (s->stopped && !was) s->page_at = now;
        /* What she reads from here is what aurstage says AFTER the
         * verdict: what happens next. The why is for a helper. */
        s->after = 1; s->npara = 0; s->para_open = 0;
        return;
    }
    if (strstr(t, "handing over to the system on")) { s->handover = 1; s->pct = -1; return; }

    if (!*t) { s->para_open = 0; s->last_dot = 0; return; }   /* a blank line ends one */

    /* What PID 1 says first is for a technician ("5 drivers loaded",
     * "this machine has 3 disks"). Until aurstage has a sentence for
     * her, the screen has its own. */
    if (!strcmp(t, "AurOS staging environment")) {
        snprintf(s->para[0], sizeof s->para[0], "%s",
                 s->mode == M_RESTORE ? "Getting ready to put Windows back."
                 : s->mode == M_INSTALL ? "Getting ready. Nothing on this computer has been changed yet."
                 : "Looking at this computer. Nothing will be changed.");
        s->npara = 1; s->para_open = 0;
        return;
    }
    /* The restore speaks in detail lines only; these are its steps. */
    if (s->mode == M_RESTORE && !s->after) {
        for (int i = NRSTEP - 1; i > 0; i--)
            if (!strncmp(t, RSTEP[i].starts, strlen(RSTEP[i].starts))) {
                if (i > s->step) set_step(s, i);
                snprintf(s->para[0], sizeof s->para[0], "%s", i == 1
                    ? "Checking the copy of how Windows started that was saved before AurOS went on."
                    : "Putting Windows and its drive back exactly as they were.");
                s->npara = 1; s->para_open = 0;
                return;
            }
    }
    if (!strncmp(t, "WARNING: ", 9)) {
        snprintf(s->warn, sizeof s->warn, "%s", t + 9);
        if (s->warn[0] >= 'a' && s->warn[0] <= 'z') s->warn[0] -= 32;
        return;
    }
    if (is_detail(t)) return;
    char fixed[480];
    plain(fixed, sizeof fixed, t);
    t = fixed;
    int cap = t[0] >= 'A' && t[0] <= 'Z';
    /* A new sentence on a new line starts a new paragraph even without
     * a blank line before it: what she reads is the latest sentence. */
    if (cap && s->last_dot) s->para_open = 0;
    size_t tl = strlen(t);
    s->last_dot = tl && t[tl - 1] == '.';
    if (s->para_open == 2) return;                  /* not for her: a size */
    if (!cap && !s->para_open) return;

    if (!s->para_open) {
        if (s->mode == M_INSTALL && !s->after)
            for (int i = 1; i < NSTEP; i++)
                if (!strncmp(t, STEP[i].starts, strlen(STEP[i].starts)) && i > s->step)
                    set_step(s, i);
        /* The sentence before stays up rather than a size going up. */
        if (has_size(t)) { s->para_open = 2; return; }
        if (!s->after) s->npara = 0;
        else s->ntail--;    /* on her page already; the helper's is for before */
        if (s->npara == 4) { s->para_open = 2; return; }
        snprintf(s->para[s->npara++], sizeof s->para[0], "%s", t);
        s->para_open = 1;
        return;
    }
    /* The next line of the same sentence: rewrapped to the screen, not
     * the console's 64 columns. */
    if (has_size(t)) { s->npara--; s->para_open = 2; return; }
    if (s->after) s->ntail--;
    char *p = s->para[s->npara - 1];
    size_t have = strlen(p);
    snprintf(p + have, sizeof s->para[0] - have, " %s", t);
}

/* ── the helper code ─────────────────────────────────────────────────
 *
 * Nine digits in groups of three, from the verdict alone, so the same
 * failure always reads the same over a telephone and `--code` turns a
 * verdict into its number. Digits 2 to 9 only: 0 and 1 are the two a
 * person reads as O and l (DESIGN-DIRECTION, "Considered and rejected").
 * 27 bits of FNV-1a; tools/screentest.sh proves every verdict aurstage
 * can say gets a different one. */
static void helper_code(const char *verdict, char out[12])
{
    uint32_t h = 2166136261u;
    for (const char *p = verdict; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    int o = 0;
    for (int i = 0; i < 9; i++) {
        if (i && i % 3 == 0) out[o++] = ' ';
        out[o++] = (char)('2' + ((h >> (3 * (8 - i))) & 7u));
    }
    out[o] = 0;
}

/* "About N minutes left": only from a measured rate, and only from the
 * copy on, because before it the longest step is still to come and
 * nothing here knows how long it will take. A minute for each short
 * step after the current one.
 * ponytail: a fixed minute per short step; time them on real machines
 * and put the numbers here if the estimate is off by more than one. */
static int minutes_left(const state *s)
{
    if (s->mode != M_INSTALL || s->stopped || s->handover || s->step < COPY_STEP) return -1;
    double sec = 60.0 * (NSTEP - 1 - s->step), dt = s->p_t1 - s->p_t0;
    int dp = s->pct - s->p0;
    if (s->p_t0 >= 0 && s->pct >= 0 && dp >= 2 && dt >= 20)
        sec += dt * (100 - s->pct) / dp;
    else if (s->step == COPY_STEP)
        return -1;
    else
        sec += 60;
    int m = (int)ceil(sec / 60.0);
    return m < 1 ? 1 : m;
}

/* "Step N of M" and, once it can be measured, "About N minutes left". */
static void steps_said(const state *st, char a[32], char m[48])
{
    int n = st->mode == M_RESTORE ? NRSTEP : NSTEP, left = minutes_left(st);
    snprintf(a, 32, "Step %d of %d", st->step + 1, n);
    m[0] = 0;
    if (left > 0) snprintf(m, 48, "About %d minute%s left", left, left == 1 ? "" : "s");
}

/* ── painting ───────────────────────────────────────────────────── */

static const char *fontdir = "/usr/share/auros/fonts";

static font *pick(float px, int bold)
{
    char ui[300];
    snprintf(ui, sizeof ui, "%s/%s", fontdir, bold ? "ui-bold.ttf" : "ui.ttf");
    const char *t[] = { ui,
        bold ? "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
             : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    for (int i = 0; t[i]; i++) { font *f = font_load(t[i], px); if (f) return f; }
    return NULL;
}

/* A size and its line: §3 Type, scaled with the screen. Never below
 * 18 px: k is at least 1. */
typedef struct { font *f; int lh; } face;
typedef struct {
    int h; float k;
    face title, mark, body, bodyb, small, smallb, code;
} faces;

static face face_at(float px, float line, int bold, float k)
{
    face f = { pick(px * k, bold), (int)lroundf(line * k) };
    return f;
}

static float scale_of(int h)
{
    float k = (float)h / 600.f;
    return k < 1.f ? 1.f : k > 2.4f ? 2.4f : k;
}

static faces *faces_for(int h)
{
    static faces F;
    if (F.h == h) return &F;
    face *all[] = { &F.title, &F.mark, &F.body, &F.bodyb, &F.small, &F.smallb, &F.code };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
        if (all[i]->f) font_free(all[i]->f);
    F.h = h; F.k = scale_of(h);
    F.title  = face_at(32, 40, 1, F.k);
    F.mark   = face_at(22, 28, 1, F.k);
    F.body   = face_at(20, 30, 0, F.k);
    F.bodyb  = face_at(20, 30, 1, F.k);
    F.small  = face_at(18, 26, 0, F.k);
    F.smallb = face_at(18, 26, 1, F.k);
    F.code   = face_at(26, 34, 1, F.k);
    return &F;
}

/* tools/screentest.sh asks (--said) what a picture says: every string
 * drawn, once, whole. The pixels cannot be searched for words. */
static FILE *said;

/* One line of text in a box `lh` tall whose top is y. */
static void text_line(surface *s, face fc, int x, int y, const char *t, uint32_t c)
{
    if (!fc.f || !t || !*t) return;
    float asc = font_ascent(fc.f), desc = font_descent(fc.f);
    font_draw(fc.f, s->px, s->w, s->h, s->stride, (float)x,
              (float)y + ((float)fc.lh + asc - desc) / 2.f, t, c, 1.f);
}

static void text(surface *s, face fc, int x, int y, const char *t, uint32_t c)
{
    if (said && t && *t) fprintf(said, "said=%s\n", t);
    text_line(s, fc, x, y, t, c);
}

static int text_w(face fc, const char *t) { return fc.f ? (int)ceilf(font_text_width(fc.f, t)) : 0; }

/* Draws t wrapped to width w (s NULL: only measures) and returns the
 * height used. Nothing she reads is cut off with "...". */
static int wrap(surface *s, face fc, int x, int y, int w, const char *t, uint32_t c)
{
    if (said && s && t && *t) fprintf(said, "said=%s\n", t);
    if (!fc.f || !t) return 0;
    char line[512] = "", next[512];
    int used = 0;
    const char *p = t;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        int wl = (int)strcspn(p, " ");
        snprintf(next, sizeof next, "%s%s%.*s", line, line[0] ? " " : "", wl, p);
        if (line[0] && text_w(fc, next) > w) {
            if (s) text_line(s, fc, x, y + used, line, c);
            used += fc.lh; line[0] = 0;
            continue;
        }
        snprintf(line, sizeof line, "%s", next);
        p += wl;
    }
    if (line[0]) { if (s) text_line(s, fc, x, y + used, line, c); used += fc.lh; }
    return used;
}

/* The mark: a solid square turned 45 degrees, anti-aliased, the same
 * dozen lines as the installer's fill_diamond (wizard.c). r is half
 * its height. */
static void fill_diamond(surface *s, float cx, float cy, float r, uint32_t col)
{
    int x0 = (int)floorf(cx - r) - 1, x1 = (int)ceilf(cx + r) + 1;
    int y0 = (int)floorf(cy - r) - 1, y1 = (int)ceilf(cy + r) + 1;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float d = (fabsf((float)px + 0.5f - cx) + fabsf((float)py + 0.5f - cy) - r) * 0.70711f;
            float a = 0.5f - d;
            if (a > 0.f) draw_blend_px(s, px, py, col, a > 1.f ? 1.f : a);
        }
}

/* The column every screen state uses: one width, one left edge. */
typedef struct { float k; int in, L, W; } frame;
static frame frame_of(int w, int h)
{
    frame f;
    f.k = scale_of(h);
    f.in = (int)(24 * f.k);
    f.W = w - 2 * f.in;
    if (f.W > (int)(760 * f.k)) f.W = (int)(760 * f.k);
    f.L = (w - f.W) / 2;
    return f;
}

/* Where "Show details for a helper" is, and "Hide details" in the very
 * same place: bottom left, alone, a smaller secondary button. One
 * width for both labels, so what she pressed is still under her hand. */
static rect button_rect(int w, int h, faces *F)
{
    frame f = frame_of(w, h);
    int bw = text_w(F->smallb, "Show details for a helper") + (int)(48 * f.k);
    if (bw < (int)(320 * f.k)) bw = (int)(320 * f.k);
    int bh = (int)(48 * f.k);
    rect r = { f.L, h - f.in - bh, bw, bh };
    return r;
}

static int has_button(const state *st) { return st->stopped; }

static const char *title_of(const state *st)
{
    if (st->mode == M_RESTORE)
        return st->stopped ? "Windows was not put back"
             : st->finished ? "Windows is back" : "Putting Windows back";
    if (st->mode == M_DRY) return "Checking this computer";
    if (st->stopped) return "AurOS didn’t go on this time";
    if (st->handover) return "AurOS is installed";
    return "Installing AurOS";
}

/* A filled check mark, for a step that is done. */
static void check(surface *s, float cx, float cy, float r, uint32_t c)
{
    float w = r * 0.28f;
    draw_line(s, cx - r * 0.45f, cy + r * 0.02f, cx - r * 0.12f, cy + r * 0.36f, w, c, 1.f);
    draw_line(s, cx - r * 0.12f, cy + r * 0.36f, cx + r * 0.48f, cy - r * 0.34f, w, c, 1.f);
}

static void paint_steps(surface *s, const state *st, faces *F, frame f, int *py)
{
    int y = *py, n = st->mode == M_RESTORE ? NRSTEP : NSTEP;
    int all = st->handover || st->finished;
    for (int i = 0; i < n; i++) {
        const char *label = st->mode == M_RESTORE ? RSTEP[i].label : STEP[i].label;
        int done = i < st->step || all, now = i == st->step && !all;
        float r = 11.f * f.k, cx = (float)f.L + r, cy = (float)y + (float)F->body.lh / 2.f;
        if (done) {
            draw_circle(s, cx, cy, r, GO, 1.f);
            check(s, cx, cy, r, CARD);
        } else if (now) {
            draw_circle(s, cx, cy, r, GO, 1.f);
            draw_circle(s, cx, cy, r - 3.f * f.k, PAPER, 1.f);
            draw_circle(s, cx, cy, r - 6.f * f.k, GO, 1.f);
        } else {
            draw_circle(s, cx, cy, r, RULE, 1.f);
            draw_circle(s, cx, cy, r - 2.f * f.k, PAPER, 1.f);
        }
        int tx = f.L + (int)(2 * r + 14 * f.k);
        text(s, now ? F->bodyb : F->body, tx, y, label, now ? INK : INK2);
        y += F->body.lh;
        /* The bar belongs to the step it measures. */
        if (now && st->pct >= 0) {
            int bw = f.L + f.W - tx, bh = (int)(10 * f.k);
            if (bw > (int)(420 * f.k)) bw = (int)(420 * f.k);
            int by = y + (int)(2 * f.k);
            draw_round_rect(s, (rect){ tx, by, bw, bh }, corners_all(bh / 2.f), SOFT, 1.f);
            draw_round_rect_border(s, (rect){ tx, by, bw, bh }, corners_all(bh / 2.f), 1.f, RULE, 1.f);
            int fw = (int)((long)bw * st->pct / 100);
            if (fw >= bh)
                draw_round_rect(s, (rect){ tx, by, fw, bh }, corners_all(bh / 2.f), GO, 1.f);
            y += bh + (int)(10 * f.k);
        }
        y += (int)(4 * f.k);
    }
    *py = y;
}

static void paint_details(surface *s, const state *st, faces *F, frame f, int y, int bottom)
{
    y += wrap(s, F->bodyb, f.L, y, f.W, "This is for a helper. Pressing this did not change anything.", INK);
    y += (int)(16 * f.k);

    /* Everything a helper asks for, newest last; as much of the oldest
     * as fits is what gives way. */
    char code[12], l[8][240];
    int nl = 0;
    helper_code(st->verdict, code);
    snprintf(l[nl++], sizeof l[0], "Number: %s", code);
    int n = st->mode == M_RESTORE ? NRSTEP : NSTEP;
    snprintf(l[nl++], sizeof l[0], "Stopped at step %d of %d: %s", st->step + 1, n,
             st->mode == M_RESTORE ? RSTEP[st->step].label : STEP[st->step].label);
    if (st->warn[0]) snprintf(l[nl++], sizeof l[0], "Why: %s", st->warn);
    if (st->report[0]) snprintf(l[nl++], sizeof l[0], "%s", st->report);
    for (int i = 0; i < nl; i++) y += wrap(s, F->small, f.L, y, f.W, l[i], INK);
    y += (int)(12 * f.k);
    draw_hrule(s, f.L, y, f.W, 1, RULE, 1.f);
    y += (int)(12 * f.k);

    int first = st->ntail, room = bottom - y;
    while (first > 0 && room >= wrap(NULL, F->small, 0, 0, f.W, st->tail[first - 1], INK)) {
        room -= wrap(NULL, F->small, 0, 0, f.W, st->tail[first - 1], INK);
        first--;
    }
    for (int i = first; i < st->ntail; i++)
        y += wrap(s, F->small, f.L, y, f.W, st->tail[i], INK2);
}

/* The arrow she moves, black with a white edge (§3 Input), drawn only
 * once a mouse or a touch has moved it. */
static void paint_pointer(surface *s, int x, int y, float k)
{
    static const char *const A[] = {
        "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X",
        "X.......X", "X........X", "X.........X", "X......XXXXX", "X...X..X",
        "X..XX..X", "X.X  X..X", "XX   X..X", "X     X..X", "      X..X", "       XX",
    };
    int z = k < 1.5f ? 2 : 3;
    for (int r = 0; r < (int)(sizeof A / sizeof A[0]); r++)
        for (int c = 0; A[r][c]; c++)
            if (A[r][c] != ' ')
                draw_rect(s, (rect){ x + c * z, y + r * z, z, z }, A[r][c] == 'X' ? 0xFFFFFFu : INK, 1.f);
}

typedef struct { int x, y, seen, hover, keyed; } pointer;

/* THE RESTORE'S LAST ACT. restore_run (aurstage/install.c) ends with
 * reboot(RB_POWER_OFF): the computer switches itself off and she turns
 * it on again, which aurstage's own closing sentence says. The design
 * wants "The screen goes black, then Windows starts. About 2 minutes."
 * before a restart. Saying it while the machine powers off would leave
 * her waiting at a dark screen, so it is built in only once the restore
 * restarts instead: compile with -DRESTORE_RESTARTS=1 then. */
#ifndef RESTORE_RESTARTS
#define RESTORE_RESTARTS 0
#endif
#define RESTART_SAYS "The screen goes black, then Windows starts. About 2 minutes."

/* A code on a screen where nothing went wrong looks like an error. */
static int shows_code(const state *st) { return st->stopped; }

/* The box at the foot: what she must not do, then what she will see.
 * From the first irreversible write to the end. Returns its lines;
 * l[0] is the one in bold. */
static int band(const state *st, const char *l[2])
{
    l[0] = l[1] = NULL;
    if (st->stopped) return 0;
    if (st->mode == M_RESTORE) {
        if (st->finished) { l[0] = RESTORE_RESTARTS ? RESTART_SAYS : NULL; return l[0] != NULL; }
        if (!RESTORE_RESTARTS) {
            l[0] = "Do not turn the computer off. It switches itself off when it is done.";
            return 1;
        }
        l[0] = "Do not turn the computer off. It restarts by itself when it is done.";
        l[1] = RESTART_SAYS;
        return 2;
    }
    if (st->mode == M_INSTALL && st->step >= RISKY_FROM && !st->handover) {
        l[0] = "Do not turn the computer off. AurOS starts by itself when it is done.";
        return 1;
    }
    return 0;
}

static void paint(surface *s, const state *st, const pointer *pt)
{
    faces *F = faces_for(s->h);
    frame f = frame_of(s->w, s->h);
    surface_fill(s, 0xFF000000u | PAPER);
    int y = f.in + (int)(8 * f.k);

    /* The wordmark: the mark, a 0.3 em gap, "AurOS". The mark's height
     * is the cap height of the word beside it. */
    float px = 22.f * f.k, cap = 0.72f * px;
    float mx = (float)f.L + cap / 2.f, my = (float)y + (float)F->mark.lh / 2.f;
    fill_diamond(s, mx, my, cap / 2.f, GO);
    text(s, F->mark, f.L + (int)(cap + 0.3f * px), y, "AurOS", INK);
    y += F->mark.lh + (int)(24 * f.k);

    rect b = button_rect(s->w, s->h, F);
    if (st->stopped && st->details) {
        paint_details(s, st, F, f, y, b.y - (int)(24 * f.k));
    } else {
        y += wrap(s, F->title, f.L, y, f.W, title_of(st), INK);
        y += (int)(8 * f.k);
        for (int i = 0; i < st->npara; i++)
            y += wrap(s, F->body, f.L, y, f.W, st->para[i], INK) + (int)(8 * f.k);
        y += (int)(16 * f.k);

        if (shows_code(st)) {
            /* The number, and why it is there: a group of its own. */
            y += (int)(8 * f.k);
            char code[12];
            helper_code(st->verdict, code);
            text(s, F->code, f.L, y, code, INK);
            y += F->code.lh;
            wrap(s, F->body, f.L, y, f.W, "If you need help, read this number to them.", INK);
        } else {
            if (st->mode != M_DRY) {
                paint_steps(s, st, F, f, &y);
                if (!st->handover && !st->finished) {
                    char a[32], m[48];
                    steps_said(st, a, m);
                    y += (int)(4 * f.k);
                    text(s, F->small, f.L, y, a, INK2);
                    if (m[0]) text(s, F->small, f.L + text_w(F->small, a) + (int)(32 * f.k), y, m, INK2);
                }
            }
            /* A box she cannot press, so no border: a tint and a bar,
             * the text in ink. */
            const char *bl[2];
            int nb = band(st, bl);
            if (nb) {
                int pad = (int)(14 * f.k), ic = (int)(24 * f.k), tw = f.W - 2 * pad - ic - pad;
                int th = wrap(NULL, F->bodyb, 0, 0, tw, bl[0], INK);
                int bh = th + (nb > 1 ? wrap(NULL, F->body, 0, 0, tw, bl[1], INK) : 0) + 2 * pad;
                int by = s->h - f.in - bh;
                draw_round_rect(s, (rect){ f.L, by, f.W, bh }, corners_all(8.f * f.k), WARNT, 1.f);
                draw_rect(s, (rect){ f.L, by, (int)(4 * f.k), bh }, WARN, 1.f);
                float cx = (float)(f.L + pad + ic / 2), cy = (float)(by + pad) + (float)F->bodyb.lh / 2.f;
                draw_circle(s, cx, cy, (float)ic / 2.f, WARN, 1.f);
                draw_rect(s, (rect){ (int)cx - (int)(1.5f * f.k), (int)(cy - 7 * f.k), (int)(3 * f.k), (int)(8 * f.k) }, WARNT, 1.f);
                draw_rect(s, (rect){ (int)cx - (int)(1.5f * f.k), (int)(cy + 3 * f.k), (int)(3 * f.k), (int)(3 * f.k) }, WARNT, 1.f);
                wrap(s, F->bodyb, f.L + pad + ic + pad, by + pad, tw, bl[0], INK);
                if (nb > 1) wrap(s, F->body, f.L + pad + ic + pad, by + pad + th, tw, bl[1], INK);
            }
        }
    }

    if (has_button(st) && pt) {
        uint32_t edge = pt->hover ? INK : RULE;
        draw_round_rect(s, b, corners_all(8.f * f.k), CARD, 1.f);
        draw_round_rect_border(s, b, corners_all(8.f * f.k), 2.f * f.k, edge, 1.f);
        if (pt->keyed) {
            int o = (int)(4 * f.k);
            draw_round_rect_border(s, (rect){ b.x - o, b.y - o, b.w + 2 * o, b.h + 2 * o },
                                   corners_all(11.f * f.k + (float)o), 3.f * f.k, INK, 1.f);
        }
        const char *lbl = st->details ? "Hide details" : "Show details for a helper";
        text(s, F->smallb, b.x + (b.w - text_w(F->smallb, lbl)) / 2, b.y + (b.h - F->smallb.lh) / 2, lbl, INK);
        if (pt->seen) paint_pointer(s, pt->x, pt->y, f.k);
    }
}

/* ── her presses ─────────────────────────────────────────────────── */

/* One press is one press: nothing for 700 ms after the page changed,
 * and a second press within 500 ms of the last is the same press. */
static void press(state *st, double now)
{
    double last = st->press_at;
    st->press_at = now;
    if (!has_button(st) || now - st->page_at < 0.7 || now - last < 0.5) return;
    st->details = !st->details;
    st->page_at = now;
    st->dirty = 1;
}

static int inside(rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static void click(state *st, int w, int h, int x, int y, double now)
{
    if (has_button(st) && inside(button_rect(w, h, faces_for(h)), x, y)) press(st, now);
}

/* /dev/input, read-only and never grabbed. Opened only once something
 * has failed, which is the only time there is anything to press. */
#define NIN 16
typedef struct {
    int n, fd[NIN];
    struct input_absinfo ax[NIN], ay[NIN];
    int abs[NIN];
    char name[NIN][16];
    double scanned;
} inputs;

static int bit(const unsigned long *b, int i)
{ return (int)((b[i / (8 * sizeof *b)] >> (i % (8 * sizeof *b))) & 1); }

static void input_scan(inputs *in)
{
    DIR *d = opendir("/dev/input");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && in->n < NIN) {
        if (strncmp(e->d_name, "event", 5)) continue;
        if (strlen(e->d_name) >= sizeof in->name[0]) continue;
        int dup = 0;
        for (int i = 0; i < in->n; i++) dup |= !strcmp(in->name[i], e->d_name);
        if (dup) continue;
        char p[16 + sizeof in->name[0]];
        snprintf(p, sizeof p, "/dev/input/%.15s", e->d_name);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long keys[KEY_MAX / (8 * sizeof(unsigned long)) + 1] = { 0 };
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 ||
            !(bit(keys, KEY_ENTER) || bit(keys, BTN_LEFT) || bit(keys, BTN_TOUCH))) {
            close(fd);
            continue;
        }
        int i = in->n++;
        in->fd[i] = fd;
        snprintf(in->name[i], sizeof in->name[0], "%s", e->d_name);
        in->abs[i] = ioctl(fd, EVIOCGABS(ABS_X), &in->ax[i]) == 0 &&
                     ioctl(fd, EVIOCGABS(ABS_Y), &in->ay[i]) == 0 &&
                     in->ax[i].maximum > in->ax[i].minimum && in->ay[i].maximum > in->ay[i].minimum;
    }
    closedir(d);
}

static void input_read(inputs *in, int i, state *st, pointer *pt, int w, int h, double now)
{
    struct input_event ev[64];
    ssize_t n = read(in->fd[i], ev, sizeof ev);
    if (n < 0 && errno != EAGAIN && errno != EINTR) {          /* unplugged */
        close(in->fd[i]);
        in->n--;
        in->fd[i] = in->fd[in->n]; in->ax[i] = in->ax[in->n];
        in->ay[i] = in->ay[in->n]; in->abs[i] = in->abs[in->n];
        memcpy(in->name[i], in->name[in->n], sizeof in->name[0]);
        return;
    }
    for (ssize_t j = 0; n > 0 && j < n / (ssize_t)sizeof ev[0]; j++) {
        const struct input_event *e = &ev[j];
        if (e->type == EV_REL && (e->code == REL_X || e->code == REL_Y)) {
            int *c = e->code == REL_X ? &pt->x : &pt->y, lim = e->code == REL_X ? w : h;
            *c += e->value;
            if (*c < 0) *c = 0;
            if (*c > lim - 1) *c = lim - 1;
            pt->seen = 1; pt->keyed = 0;
        } else if (e->type == EV_ABS && in->abs[i] && (e->code == ABS_X || e->code == ABS_Y)) {
            const struct input_absinfo *a = e->code == ABS_X ? &in->ax[i] : &in->ay[i];
            int lim = e->code == ABS_X ? w : h;
            long v = (long)(e->value - a->minimum) * (lim - 1) / (a->maximum - a->minimum);
            *(e->code == ABS_X ? &pt->x : &pt->y) = (int)(v < 0 ? 0 : v > lim - 1 ? lim - 1 : v);
            pt->seen = 1; pt->keyed = 0;
        } else if (e->type == EV_KEY && e->value == 1) {
            if (e->code == BTN_LEFT || e->code == BTN_TOUCH) click(st, w, h, pt->x, pt->y, now);
            else if (e->code == KEY_ENTER || e->code == KEY_KPENTER || e->code == KEY_SPACE) {
                pt->keyed = 1; press(st, now);
            }
        }
        st->dirty = 1;
    }
    int hv = has_button(st) && inside(button_rect(w, h, faces_for(h)), pt->x, pt->y) && pt->seen;
    if (hv != pt->hover) { pt->hover = hv; st->dirty = 1; }
}

static int mode_from_cmdline(void)
{
    char b[4096] = "";
    int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { ssize_t n = read(fd, b, sizeof b - 1); if (n > 0) b[n] = 0; close(fd); }
    if (strstr(b, "aurstage.restore")) return M_RESTORE;
    if (strstr(b, "aurstage.install")) return M_INSTALL;
    return M_DRY;
}

static double mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static volatile sig_atomic_t quit;
static void on_term(int sig) { (void)sig; quit = 1; }

int main(int argc, char **argv)
{
    const char *png = NULL;
    int pw = 1280, ph = 720, dump = 0, mode = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--png") && i + 1 < argc) png = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) { pw = atoi(argv[++i]); ph = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--state")) dump = 1;
        else if (!strcmp(argv[i], "--said")) said = stdout;
        else if (!strcmp(argv[i], "--fonts") && i + 1 < argc) fontdir = argv[++i];
        else if (!strcmp(argv[i], "--code") && i + 1 < argc) {
            char c[12]; helper_code(argv[++i], c); printf("%s\n", c); return 0;
        }
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            mode = !strcmp(m, "restore") ? M_RESTORE : !strcmp(m, "install") ? M_INSTALL : M_DRY;
        }
    }
    state st;
    memset(&st, 0, sizeof st);
    st.pct = -1; st.p_t0 = -1; st.page_at = st.press_at = -1e9;
    st.mode = mode >= 0 ? mode : mode_from_cmdline();
    pointer pt = { 0, 0, 0, 0, 0 };

    /* Off-screen: everything on stdin, then one picture or one report. */
    if (png || dump || said) {
        if (pw < 16 || ph < 16 || pw > 8192 || ph > 8192) return 1;
        char line[1024];
        double now = 0;
        pt.x = pw / 2; pt.y = ph / 2;
        while (fgets(line, sizeof line, stdin)) {
            int x, y;
            if (!strncmp(line, "aurscreen-at ", 13)) now = atof(line + 13);
            else if (sscanf(line, "aurscreen-press %d %d", &x, &y) == 2) {
                pt.x = x; pt.y = y; pt.seen = 1; pt.keyed = 0;
                click(&st, pw, ph, x, y, now);
            } else if (!strncmp(line, "aurscreen-key", 13)) { pt.keyed = 1; press(&st, now); }
            else feed(&st, line, now);
        }
        pt.hover = pt.seen && has_button(&st) && inside(button_rect(pw, ph, faces_for(ph)), pt.x, pt.y);
        if (dump) {
            char code[12] = "-";
            if (shows_code(&st)) helper_code(st.verdict, code);
            printf("mode=%d step=%d label=%s pct=%d stopped=%d handover=%d finished=%d verdict=%s "
                   "left=%d details=%d code=%s\n",
                   st.mode, st.step, st.mode == M_RESTORE ? RSTEP[st.step].label : STEP[st.step].label,
                   st.pct, st.stopped, st.handover, st.finished, st.verdict[0] ? st.verdict : "-",
                   minutes_left(&st), st.details, code);
            for (int i = 0; i < st.npara; i++) printf("para=%s\n", st.para[i]);
            if (st.warn[0]) printf("why=%s\n", st.warn);
            const char *bl[2];
            int nb = band(&st, bl);
            for (int i = 0; i < nb; i++) printf("band=%s\n", bl[i]);
        }
        if (png || said) {
            surface *o = surface_new(pw, ph);
            if (!o) return 1;
            paint(o, &st, &pt);
            int rc = png ? png_write_rgb(png, o->px, o->w, o->h) : 0;
            surface_free(o);
            if (rc < 0) { fprintf(stderr, "aurscreen: cannot write %s\n", png); return 1; }
        }
        return 0;
    }

    signal(SIGTERM, on_term);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    kms_display *d = kms_open(NULL);
    if (!d) return 0;               /* no display: the console has it all */
    pt.x = d->width / 2; pt.y = d->height / 2;

    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    char buf[8192]; size_t have = 0;
    inputs in = { 0 };
    in.scanned = -1e9;
    st.dirty = 1;
    int open_in = 1;
    while (!quit) {
        if (st.dirty) {
            surface *s = kms_back_surface(d);
            /* No input found, no button: nothing she can press is drawn. */
            if (s) { paint(s, &st, in.n ? &pt : NULL); kms_flip(d); }
            st.dirty = 0;
        }
        double now = mono();
        if (has_button(&st) && now - in.scanned >= 2.0) {   /* plugged in later, or loaded later */
            int had = in.n;
            input_scan(&in);
            in.scanned = now;
            if (in.n != had) st.dirty = 1;
        }
        struct pollfd p[1 + NIN];
        p[0] = (struct pollfd){ open_in ? 0 : -1, POLLIN, 0 };
        for (int i = 0; i < in.n; i++) p[1 + i] = (struct pollfd){ in.fd[i], POLLIN, 0 };
        if (poll(p, (nfds_t)(1 + in.n), 500) <= 0) continue;
        now = mono();
        for (int i = in.n - 1; i >= 0; i--)
            if (p[1 + i].revents) input_read(&in, i, &st, &pt, d->width, d->height, now);
        if (!(p[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        ssize_t n = read(0, buf + have, sizeof buf - 1 - have);
        if (n == 0) { open_in = 0; continue; }   /* aurstage is gone; keep the last picture */
        if (n < 0) { if (errno != EAGAIN && errno != EINTR) open_in = 0; continue; }
        have += (size_t)n;
        buf[have] = 0;
        char *a = buf, *nl;
        while ((nl = strchr(a, '\n'))) {
            *nl = 0;
            feed(&st, a, now);
            a = nl + 1;
        }
        have = strlen(a);
        memmove(buf, a, have + 1);
        if (have >= sizeof buf - 2) have = 0;    /* a line that long is noise */
    }
    kms_close(d);
    return 0;
}
