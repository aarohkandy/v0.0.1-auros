/* screen.c — what the one restart looks like
 *
 * docs/issues/v3/05 item 5.5, redone for docs/DESIGN-DIRECTION.md (C,
 * item 3: the staging screens) and docs/research/people-walkthrough-
 * 2026-10.md D-01 and D-03 to D-07.
 *
 * After AurSetup restarts the computer, the staging environment runs
 * for somewhere between a minute and an hour, and what a person sees
 * for all of it is this: where the install is, in her words; one
 * honest "about N minutes left" measured from how fast it is really
 * going; a bar when there is something to measure; and, from the moment
 * the Windows drive is touched until it is finished, "don't turn the
 * computer off" in a band nobody can miss.
 *
 * WHAT IT NEVER SHOWS, AND WHY. No log lines, no device names, no
 * sizes, no file systems, no program names but AurOS and Windows: a
 * line like "the Windows drive is now 8 MB" is true, is meant for a
 * technician, and reads as a disaster to the person it is shown to at
 * the one moment she is most afraid (D-03). Everything on this screen
 * is a sentence written for her here, in this file, by step -- aurstage's
 * own lines are only read to know WHICH step it is on and whether it has
 * stopped. The technical lines are still on the console and in the log;
 * a helper can see them on this screen too by adding `aurstage.details`
 * to the kernel line.
 *
 * When something has stopped, the screen says what happened to her task,
 * what state Windows is in, what happens next (this computer restarts
 * by itself, so nobody holds a power button down) and what to do to
 * try again -- and only then, small, a number to read to a helper: nine
 * digits in three groups, made from the machine's verdict, so the helper
 * can look it up (tools/helpercodes.py) and she never reads a word she
 * cannot say. A code on a screen where nothing went wrong looks like an
 * error, so there is none then.
 *
 * HOW IT IS FED, AND WHY IT CANNOT HURT THE INSTALL
 *
 * aurstage is PID 1 and the only program that writes to a disk. It
 * starts this with a pipe on its standard input and copies every line
 * it says into the pipe as well as onto the console (boot.c), plus
 * lines meant for this screen alone:
 *
 *   aurstage-progress N      a long step is N% done
 *   aurstage-mode gap|legacy which steps this install has
 *   aurstage-now WHERE       what state the disk is in when it stopped:
 *                            untouched | room | smaller | auros | windows
 *   aurstage-countdown N     N seconds until it restarts by itself
 *   aurstage-time N          (tests) set the clock
 *
 * The write end is non-blocking and SIGPIPE is ignored, so a screen
 * that hangs, crashes or never starts costs aurstage nothing: the lines
 * are dropped and the console still has them. This program opens no
 * disk, reads no disk, and takes no decision; it only listens.
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
 *   aurscreen --code VERDICT         print the helper number for it
 *   aurscreen --mode install|restore|dry   instead of /proc/cmdline
 *   aurscreen --details              show the technical lines too
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../aurshell/kms.h"
#include "../aurshell/draw.h"
#include "../common/font.h"
#include "../common/png.h"

/* The Light tokens of docs/DESIGN-DIRECTION.md section 3, hardcoded like
 * aursorry's: nothing here may depend on a theme file the install has
 * not written yet. */
#define PAPER  0xF6F2E9u
#define SOFT   0xECE6DAu
#define INK    0x17181Au
#define INK2   0x3A3D42u
#define RULE   0x6E675Cu
#define GO     0x0B5D4Fu
#define WARNC  0x8A5A12u
#define WARNT  0xF7EBD2u
#define TRACK  0xDDD6C8u

enum { M_INSTALL, M_RESTORE, M_DRY };
enum { W_NONE, W_UNTOUCHED, W_ROOM, W_SMALLER, W_AUROS, W_WINDOWS };

/* ── the steps ──────────────────────────────────────────────────────
 *
 * `starts` is the sentence of aurstage's that starts the step, matched
 * on a prefix of a sentence install.c already says (tools/screentest.sh
 * checks each is still there, word for word: reword one there and the
 * screen would sit on the step before it for the rest of the install).
 * `label` and `body` are ours. Neither carries a number or a unit.
 * `legacy` / `gap`: which installs have the step; `xl` / `xg`: how many
 * seconds a typical machine takes in it, which only seeds the estimate
 * until the screen has measured the machine in front of it. */
static const struct {
    const char *starts, *label, *body;
    int legacy, gap, xl, xg;
} STEP[] = {
    { "", "Checking this computer",
      "Looking at this computer. Nothing has been changed yet.", 1, 1, 25, 25 },
    { "Checking the copy of AurOS that Windows wrote", "Checking AurOS",
      "Checking the copy of AurOS that Windows put beside itself.", 0, 1, 0, 180 },
    { "Saving this computer's Windows startup", "Saving the way back to Windows",
      "Saving what Windows needs to start, so it can always be put back.", 1, 1, 60, 45 },
    { "Making room on the Windows drive", "Making room for AurOS",
      "Making room for AurOS. Your Windows files are not touched.", 1, 0, 600, 0 },
    { "Copying AurOS onto this computer", "Copying AurOS",
      "Putting AurOS on this computer.", 1, 0, 420, 0 },
    { "Making this computer able to start AurOS", "Setting up how it starts",
      "Setting up how this computer starts AurOS.", 1, 0, 45, 0 },
    { "Checking that this computer works under AurOS", "Checking AurOS works here",
      "Checking that AurOS works on this computer.", 1, 1, 45, 45 },
    { "Writing the new layout", "Finishing",
      "Finishing. Nearly there.", 1, 1, 30, 40 },
};
#define NSTEP ((int)(sizeof STEP / sizeof STEP[0]))
#define RISKY_LEGACY 3     /* "Making room": the part that cannot be undone */
#define RISKY_GAP    3     /* "Setting up": the first write on this computer */

#define NTAIL 5
typedef struct {
    int  mode;
    int  gap;                   /* the fast way: Windows made the room   */
    int  step;                  /* index into STEP                       */
    int  pct;                   /* -1: nothing to measure                */
    char para[4][200];          /* her sentences                         */
    int  npara;
    int  stopped;               /* a refusal or failure was reported     */
    int  finished;              /* the restore put Windows back          */
    int  handover;              /* the install finished                  */
    int  where;                 /* W_*: what state the disk is in        */
    int  countdown;             /* seconds to the automatic restart, -1  */
    char verdict[96];
    char warn[200];             /* the last WARNING:                     */
    /* the clock, in seconds (the tests set it) */
    double now, t0, step_t0, pct_t0, last_move;
    double step_took[NSTEP];
    int  got_clock;
    /* for a helper */
    int  details;
    char tail[NTAIL][160];
    int  ntail;
    char report[160];
    int  dirty;
} state;

static double wall(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void push_tail(state *s, const char *l)
{
    if (s->ntail == NTAIL) {
        memmove(s->tail[0], s->tail[1], sizeof s->tail[0] * (NTAIL - 1));
        s->ntail--;
    }
    snprintf(s->tail[s->ntail++], sizeof s->tail[0], "%s", l);
}

/* The helper number: FNV-1a over the verdict, nine decimal digits. Not
 * a secret and not a checksum: a short name for the sentence that is on
 * the console, which a helper turns back with tools/helpercodes.py. */
static unsigned code_of(const char *verdict)
{
    unsigned long long h = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)verdict; *p; p++) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    return (unsigned)(h % 1000000000ull);
}

static void code_text(const char *verdict, char *out, size_t n)
{
    unsigned c = code_of(verdict);
    snprintf(out, n, "%03u %03u %03u", c / 1000000, (c / 1000) % 1000, c % 1000);
}

/* A sentence is for her only if it has nothing a technician wrote in
 * it: no device, no unit, no filesystem. Anything else is replaced by a
 * plain one for the step. */
static int for_her(const char *t)
{
    static const char *const BAD_BITS[] = {
        "/dev", "vd", "sd", "nvme", "MiB", "GiB", "MB", "GB", "KB", "sector",
        "LBA", "NTFS", "vfat", "ext4", "GPT", "ntfs", "initramfs", "kernel",
        "BitLocker", "SMART", "aurstage", "0x", "partition", "table", "root",
        "--", NULL };
    for (int i = 0; BAD_BITS[i]; i++)
        if (strstr(t, BAD_BITS[i])) return 0;
    for (const char *p = t; *p; p++)
        if (*p >= '0' && *p <= '9') return 0;       /* never print a number */
    return 1;
}

static const char *plain_for(const state *s)
{
    if (s->mode == M_RESTORE) return "Windows could not be put back this time.";
    if (!strncmp(s->verdict, "no-", 3))
        return "AurOS could not find what it needs on this computer.";
    return "Something stopped AurOS from going on this computer.";
}

static int in_list(const state *s, int i)
{ return s->gap ? STEP[i].gap : STEP[i].legacy; }

static int shown_steps(const state *s)
{ int n = 0; for (int i = 0; i < NSTEP; i++) n += in_list(s, i); return n; }

static int position(const state *s)
{ int n = 0; for (int i = 0; i <= s->step && i < NSTEP; i++) n += in_list(s, i); return n; }

static int risky_from(const state *s) { return s->gap ? RISKY_GAP : RISKY_LEGACY; }

/* ── how long is left ─────────────────────────────────────────────────
 *
 * Measured, not promised. The steps already done say how fast THIS
 * computer is compared with a typical one (the ratio of what they took to
 * what a typical one takes, kept between a half and six); the step
 * under way says how long it has left from how far it has got and how
 * long that took; the steps after it are typical, times the ratio.
 * Returns seconds, or -1 when there is nothing honest to say yet. */
static double eta(const state *s)
{
    if (s->stopped || s->handover || s->finished) return -1;
    if (s->mode != M_INSTALL) return -1;
    double took = 0, typ = 0;
    for (int i = 0; i < s->step && i < NSTEP; i++) {
        if (!in_list(s, i) || s->step_took[i] <= 0) continue;
        took += s->step_took[i];
        typ += s->gap ? STEP[i].xg : STEP[i].xl;
    }
    double ratio = typ >= 20 ? took / typ : 1.0;
    if (ratio < 0.5) ratio = 0.5;
    if (ratio > 6) ratio = 6;
    double here = s->now - s->step_t0;
    double x = (s->gap ? STEP[s->step].xg : STEP[s->step].xl) * ratio;
    double left_here = x - here;
    if (s->pct >= 3 && here >= 15)
        left_here = here * (100.0 - s->pct) / s->pct;      /* measured */
    if (left_here < 0) left_here = 0;
    double after = 0;
    for (int i = s->step + 1; i < NSTEP; i++)
        if (in_list(s, i)) after += (s->gap ? STEP[i].xg : STEP[i].xl) * ratio;
    double total = left_here + after;
    if (s->now - s->t0 < 8) return -1;                      /* too soon */
    return total;
}

static void eta_text(double sec, char *out, size_t n)
{
    if (sec < 0) { out[0] = 0; return; }
    int m = (int)((sec + 59.0) / 60.0);
    if (sec < 50) snprintf(out, n, "Less than a minute left.");
    else if (m <= 1) snprintf(out, n, "About a minute left.");
    else snprintf(out, n, "About %d minutes left.", m);
}

/* ── reading what aurstage says ────────────────────────────────────── */

static void set_para(state *s, const char *a, const char *b, const char *c, const char *d)
{
    const char *l[4] = { a, b, c, d };
    s->npara = 0;
    for (int i = 0; i < 4; i++)
        if (l[i] && *l[i]) snprintf(s->para[s->npara++], sizeof s->para[0], "%s", l[i]);
}

/* What she is told, from where we are. Rebuilt whenever anything
 * changes, from state alone: nothing is carried over from a line. */
static void compose(state *s)
{
    /* "restart by itself in 31 seconds": the one number she can use. */
    char again[96] = "", cd[96] = "";
    if (s->countdown >= 0)
        snprintf(cd, sizeof cd, s->countdown == 1
                 ? "This computer will restart by itself in 1 second."
                 : "This computer will restart by itself in %d seconds.", s->countdown);
    (void)again;
    if (s->mode == M_RESTORE) {
        if (s->stopped)
            set_para(s, s->warn[0] && for_her(s->warn) ? s->warn : plain_for(s),
                     "Nothing on this computer has been lost.",
                     s->countdown >= 0 ? cd : NULL,
                     NULL);
        else if (s->finished)
            set_para(s, "Windows is back, with every file in it.",
                     "The screen goes black, then Windows starts. About two minutes.", NULL, NULL);
        else
            set_para(s, "Putting Windows back exactly as it was.",
                     "Your files are not touched.", NULL, NULL);
        return;
    }
    if (s->mode == M_DRY) {
        set_para(s, "Looking at this computer. Nothing will be changed.", NULL, NULL, NULL);
        return;
    }
    if (s->stopped) {
        const char *what = s->warn[0] && for_her(s->warn) ? s->warn : plain_for(s);
        const char *state_line =
            s->where == W_UNTOUCHED ? "Nothing on this computer has been changed."
          : s->where == W_ROOM || s->where == W_SMALLER
              ? "Your Windows files are safe. Windows will start as usual."
          : s->where == W_AUROS ? "AurOS is on this computer. It will start now."
          : s->where == W_WINDOWS ? "AurOS is on this computer, and Windows will start first."
          : "Nothing on this computer has been lost.";
        const char *next = s->countdown >= 0 ? cd : NULL;
        const char *again = (s->where == W_AUROS || s->where == W_WINDOWS) ? NULL
            : "To try again, open AurOS Setup in Windows and press Install AurOS.";
        set_para(s, what, state_line, next, again);
        return;
    }
    if (s->handover) {
        set_para(s, "AurOS is installed. Starting it now.", NULL, NULL, NULL);
        return;
    }
    set_para(s, STEP[s->step].body, NULL, NULL, NULL);
}

static void feed(state *s, char *line)
{
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;

    if (!strncmp(line, "aurstage-time ", 14)) {
        s->now = atof(line + 14); s->got_clock = 1;
        if (s->t0 == 0 && s->now > 0) { s->t0 = s->step_t0 = s->pct_t0 = s->last_move = s->now; }
        s->dirty = 1;
        return;
    }
    if (!strncmp(line, "aurstage-progress ", 18)) {
        int p = atoi(line + 18);
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        if (p != s->pct) { s->pct = p; s->last_move = s->now; s->dirty = 1; compose(s); }
        return;
    }
    if (!strncmp(line, "aurstage-mode ", 14)) {
        s->gap = !strcmp(line + 14, "gap");
        s->dirty = 1; compose(s);
        return;
    }
    if (!strncmp(line, "aurstage-now ", 13)) {
        const char *w = line + 13;
        s->where = !strcmp(w, "untouched") ? W_UNTOUCHED : !strcmp(w, "room") ? W_ROOM
                 : !strcmp(w, "smaller") ? W_SMALLER : !strcmp(w, "auros") ? W_AUROS
                 : !strcmp(w, "windows") ? W_WINDOWS : W_NONE;
        s->dirty = 1; compose(s);
        return;
    }
    if (!strncmp(line, "aurstage-countdown ", 19)) {
        s->countdown = atoi(line + 19);
        s->dirty = 1; compose(s);
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
        snprintf(s->report, sizeof s->report, "%s", v - 27);
        /* Every restore-* verdict is a restore that stopped: nothing
         * saved, no usable copy, failed. "restored" is the one that
         * did not. */
        s->finished = !strcmp(s->verdict, "restored");
        s->stopped = strstr(t, "record=refused") || strstr(t, "record=failed") ||
                     !strncmp(s->verdict, "no-", 3) || !strncmp(s->verdict, "restore-", 8) ||
                     strstr(s->verdict, "fail") || strstr(s->verdict, "refus");
        if (s->stopped && s->where == W_NONE && s->mode == M_INSTALL)
            s->where = strstr(t, "record=refused") ? W_UNTOUCHED : W_SMALLER;
        compose(s);
        return;
    }
    if (strstr(t, "handing over to the system on")) {
        s->handover = 1; s->pct = -1; compose(s); return;
    }
    if (!*t) return;

    if (!strcmp(t, "AurOS staging environment")) {
        set_para(s, s->mode == M_RESTORE ? "Getting ready to put Windows back. About a minute."
                  : s->mode == M_INSTALL ? "AurOS is starting. About one minute. Please don't press anything."
                  : "Looking at this computer. Nothing will be changed.",
                 NULL, NULL, NULL);
        return;
    }
    if (!strncmp(t, "WARNING: ", 9)) {
        snprintf(s->warn, sizeof s->warn, "%s", t + 9);
        if (s->warn[0] >= 'a' && s->warn[0] <= 'z') s->warn[0] -= 32;
        if (s->stopped) compose(s);
        return;
    }
    /* A step starts when aurstage says its sentence. */
    for (int i = 1; i < NSTEP; i++)
        if (STEP[i].starts[0] && !strncmp(t, STEP[i].starts, strlen(STEP[i].starts)) &&
            i > s->step && in_list(s, i)) {
            s->step_took[s->step] = s->now - s->step_t0;
            s->step = i; s->pct = -1;
            s->step_t0 = s->last_move = s->now;
            compose(s);
            return;
        }
    if (s->mode == M_RESTORE && !s->stopped && !s->finished &&
        (!strncmp(t, "checking the saved copy", 23) || !strncmp(t, "restoring ", 10)))
        compose(s);
}

/* ── painting ───────────────────────────────────────────────────── */

/* The UI face (build/staging copies it in as ui.ttf / ui-bold.ttf), then
 * DejaVu as the fallback. FONT_DIR is only for tools/screenshots-staging.sh,
 * which renders with the shipping face on a computer that has no image. */
#ifndef FONT_DIR
#define FONT_DIR "/usr/share/auros/fonts"
#endif
static font *pick(float px, int bold)
{
    static const char *R[] = { FONT_DIR "/ui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    static const char *B[] = { FONT_DIR "/ui-bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    const char **t = bold ? B : R;
    for (int i = 0; t[i]; i++) { font *f = font_load(t[i], px); if (f) return f; }
    return NULL;
}

static void text(surface *s, font *f, int x, int y, const char *t, uint32_t c, float a)
{
    if (f && t && *t)
        font_draw(f, s->px, s->w, s->h, s->stride, (float)x, (float)y + font_ascent(f), t, c, a);
}

/* Draw `t` wrapped to `width`; returns the y below it. */
static int wrapped(surface *s, font *f, int x, int y, int width, const char *t,
                   uint32_t c, float lead)
{
    if (!f) return y + 24;
    char line[260];
    const char *p = t;
    int lh = (int)(font_line_height(f) * lead);
    while (*p) {
        size_t n = 0, last_sp = 0;
        line[0] = 0;
        while (p[n] && n < sizeof line - 1) {
            line[n] = p[n]; line[n + 1] = 0;
            if (p[n] == ' ') last_sp = n;
            if (font_text_width(f, line) > (float)width && last_sp) { n = last_sp; break; }
            n++;
        }
        if (!p[n] || n >= sizeof line - 1) n = strlen(p) < sizeof line - 1 ? strlen(p) : sizeof line - 1;
        memcpy(line, p, n); line[n] = 0;
        text(s, f, x, y, line, c, 1.f);
        y += lh;
        p += n;
        while (*p == ' ') p++;
    }
    return y;
}

static const char *title_of(const state *st)
{
    if (st->mode == M_RESTORE)
        return st->stopped ? "Windows was not put back this time"
             : st->finished ? "Windows is back" : "Putting Windows back";
    if (st->mode == M_DRY) return "Looking at this computer";
    if (st->stopped) return "AurOS didn't go on this time";
    if (st->handover) return "AurOS is installed";
    return "Installing AurOS";
}

static void paint(surface *s, const state *st)
{
    float k = (float)s->h / 720.f;
    if (k < 1.0f) k = 1.0f;          /* never below the 18 px floor of EASY */
    if (k > 2.4f) k = 2.4f;
    font *h1 = pick(40.f * k, 1), *body = pick(22.f * k, 0),
         *small = pick(19.f * k, 0), *mono = pick(14.f * k, 0),
         *code = pick(26.f * k, 1);

    surface_fill(s, 0xFF000000u | PAPER);
    int L = (int)(s->w * 0.09f), W = s->w - 2 * L;
    int y = (int)(s->h * 0.09f);

    /* the mark: the same diamond as the website and the installer */
    float cx = (float)L + 14.f * k, cy = (float)y + 14.f * k, r = 13.f * k;
    for (int dy = -(int)r; dy <= (int)r; dy++) {
        int half = (int)(r - (dy < 0 ? -dy : dy));
        draw_rect(s, (rect){ (int)cx - half, (int)cy + dy, 2 * half + 1, 1 }, GO, 1.f);
    }
    text(s, small, L + (int)(40 * k), y + (int)(4 * k), "AurOS", INK2, 1.f);
    y += (int)(58 * k);

    text(s, h1, L, y, title_of(st), INK, 1.f);
    y += h1 ? (int)(font_line_height(h1) * 1.3f) : 44;

    /* her sentences */
    y += (int)(8 * k);
    for (int i = 0; i < st->npara; i++) {
        y = wrapped(s, body, L, y, W, st->para[i], i == 0 ? INK : INK2, 1.3f);
        if (i == 0 && st->npara > 1) y += (int)(8 * k);
    }
    y += (int)(14 * k);

    /* the bar, and how long is left */
    double e = eta(st);
    char et[64];
    eta_text(e, et, sizeof et);
    int long_step = st->mode == M_INSTALL && !st->stopped && !st->handover &&
                    (st->step == 3 || st->step == 4 || st->step == 1);
    if (st->pct >= 0 && !st->stopped) {
        int bh = (int)(10 * k);
        draw_round_rect(s, (rect){ L, y, W, bh }, corners_all(bh / 2.f), TRACK, 1.f);
        int fw = (int)((long)W * st->pct / 100);
        if (fw > 0)
            draw_round_rect(s, (rect){ L, y, fw, bh }, corners_all(bh / 2.f), GO, 1.f);
        y += bh + (int)(14 * k);
    }
    if (st->mode == M_INSTALL && !st->stopped && !st->handover) {
        char step_t[64];
        snprintf(step_t, sizeof step_t, "Step %d of %d%s%s", position(st), shown_steps(st),
                 et[0] ? ". " : "", et);
        text(s, small, L, y, step_t, INK, 1.f);
        y += small ? (int)(font_line_height(small) * 1.4f) : 24;
        if (long_step || (st->now - st->last_move) > 40) {
            text(s, small, L, y, "It can look still. Nothing is wrong.", INK2, 1.f);
            y += small ? (int)(font_line_height(small) * 1.4f) : 24;
        }
    }
    y += (int)(14 * k);

    /* the steps, for an install: done, now, to come */
    if (st->mode == M_INSTALL && !st->stopped) {
        for (int i = 0; i < NSTEP; i++) {
            if (!in_list(st, i)) continue;
            int done = i < st->step || st->handover;
            int now = i == st->step && !st->handover;
            uint32_t c = done ? GO : now ? INK : INK2;
            float dx = (float)L + 8.f * k, dy = (float)y + 12.f * k;
            if (done) draw_circle(s, dx, dy, 7.f * k, GO, 1.f);
            else if (now) { draw_circle(s, dx, dy, 7.f * k, INK, 1.f);
                            draw_circle(s, dx, dy, 3.5f * k, PAPER, 1.f); }
            else draw_circle(s, dx, dy, 4.f * k, RULE, 1.f);
            text(s, small, L + (int)(30 * k), y + (int)(2 * k), STEP[i].label, c, 1.f);
            y += small ? (int)(font_line_height(small) * 1.5f) : 24;
        }
    }

    /* the number to read to a helper: only when something stopped */
    int foot = s->h - (int)(30 * k);
    if (st->details) foot -= (NTAIL + 1) * (mono ? (int)font_line_height(mono) : 16);
    if (st->stopped && st->verdict[0]) {
        char c[32];
        code_text(st->verdict, c, sizeof c);
        int ch = code ? (int)font_line_height(code) : 30;
        int by = foot - ch - (small ? (int)font_line_height(small) : 20) - (int)(34 * k);
        text(s, small, L, by, "If you need help, read this number to them.", INK2, 1.f);
        text(s, code, L, by + (small ? (int)(font_line_height(small) * 1.3f) : 24), c, INK, 1.f);
        foot = by - (int)(18 * k);
    }

    /* don't turn it off: from the first irreversible write to the end */
    int risky = st->mode == M_RESTORE ? !st->stopped && !st->finished && st->verdict[0] == 0
              : st->mode == M_INSTALL && st->step >= risky_from(st) && !st->stopped && !st->handover;
    if (risky) {
        int bh = (int)(54 * k), by = foot - bh;
        draw_round_rect(s, (rect){ L, by, W, bh }, corners_all(8.f * k), WARNT, 1.f);
        draw_rect(s, (rect){ L, by, (int)(5 * k), bh }, WARNC, 1.f);
        text(s, body, L + (int)(22 * k), by + (bh - (body ? (int)font_line_height(body) : 20)) / 2,
             st->mode == M_RESTORE
                 ? "Please don't turn the computer off until this screen says it is done."
                 : "Please don't turn the computer off. It will restart by itself.", INK, 1.f);
    }

    /* the technical lines, only for a helper (aurstage.details) */
    if (st->details) {
        int fy = s->h - (int)(24 * k) - (NTAIL + 1) * (mono ? (int)font_line_height(mono) : 16);
        draw_hrule(s, L, fy - (int)(8 * k), W, 1, RULE, 0.6f);
        if (st->report[0]) { text(s, mono, L, fy, st->report, INK, 1.f);
                             fy += mono ? (int)font_line_height(mono) : 16; }
        for (int i = 0; i < st->ntail; i++) {
            text(s, mono, L, fy, st->tail[i], INK2, 0.9f);
            fy += mono ? (int)font_line_height(mono) : 16;
        }
    }

    if (h1) font_free(h1);
    if (body) font_free(body);
    if (small) font_free(small);
    if (mono) font_free(mono);
    if (code) font_free(code);
}

static int mode_from_cmdline(int *details)
{
    char b[4096] = "";
    int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { ssize_t n = read(fd, b, sizeof b - 1); if (n > 0) b[n] = 0; close(fd); }
    if (details) *details = strstr(b, "aurstage.details") != NULL;
    if (strstr(b, "aurstage.restore")) return M_RESTORE;
    if (strstr(b, "aurstage.install")) return M_INSTALL;
    return M_DRY;
}

static volatile sig_atomic_t quit;
static void on_term(int sig) { (void)sig; quit = 1; }

static void init_state(state *st, int mode, int details)
{
    memset(st, 0, sizeof *st);
    st->pct = -1;
    st->countdown = -1;
    st->mode = mode;
    st->details = details;
    compose(st);
    /* Before aurstage has said a word, this is what she is told: the
     * minutes of black before the screen came up are over, and the next
     * few are not a crash (D-01). */
    if (mode == M_INSTALL)
        set_para(st, "AurOS is starting. About one minute. Please don't press anything.", NULL, NULL, NULL);
    else if (mode == M_RESTORE)
        set_para(st, "Getting ready to put Windows back. About a minute.", NULL, NULL, NULL);
}

int main(int argc, char **argv)
{
    const char *png = NULL;
    int pw = 1280, ph = 720, dump = 0, mode = -1, details = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--png") && i + 1 < argc) png = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) { pw = atoi(argv[++i]); ph = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--state")) dump = 1;
        else if (!strcmp(argv[i], "--details")) details = 1;
        else if (!strcmp(argv[i], "--code") && i + 1 < argc) {
            char c[32]; code_text(argv[++i], c, sizeof c); puts(c); return 0;
        }
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            mode = !strcmp(m, "restore") ? M_RESTORE : !strcmp(m, "install") ? M_INSTALL : M_DRY;
        }
    }
    int cmd_details = 0;
    int cmd_mode = mode_from_cmdline(&cmd_details);
    state st;
    init_state(&st, mode >= 0 ? mode : cmd_mode, details >= 0 ? details : cmd_details);

    /* Off-screen: everything on stdin, then one picture or one report. */
    if (png || dump) {
        char line[1024];
        while (fgets(line, sizeof line, stdin)) feed(&st, line);
        if (dump) {
            double e = eta(&st);
            char c[32] = "-";
            if (st.stopped && st.verdict[0]) {
                code_text(st.verdict, c, sizeof c);
                for (char *q = c; *q; q++) if (*q == ' ') *q = '_';   /* one field */
            }
            printf("mode=%d gap=%d step=%d position=%d of=%d label=%s pct=%d stopped=%d "
                   "handover=%d finished=%d verdict=%s eta=%d where=%d code=%s countdown=%d\n",
                   st.mode, st.gap, st.step, position(&st), shown_steps(&st), STEP[st.step].label,
                   st.pct, st.stopped, st.handover, st.finished,
                   st.verdict[0] ? st.verdict : "-", e < 0 ? -1 : (int)e, st.where, c, st.countdown);
            for (int i = 0; i < st.npara; i++) printf("para=%s\n", st.para[i]);
        }
        if (png) {
            surface *o = surface_new(pw, ph);
            if (!o) return 1;
            paint(o, &st);
            int rc = png_write_rgb(png, o->px, o->w, o->h);
            surface_free(o);
            if (rc < 0) { fprintf(stderr, "aurscreen: cannot write %s\n", png); return 1; }
        }
        return 0;
    }

    signal(SIGTERM, on_term);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    char buf[8192]; size_t have = 0;
    int open_in = 1;
    kms_display *d = NULL;
    double next_open = 0;
    st.now = st.t0 = st.step_t0 = st.pct_t0 = st.last_move = wall();
    st.dirty = 1;
    /* THE DISPLAY MAY NOT BE THERE YET. The screen is started as early as
     * aurstage can (D-01) -- before it loads the graphics drivers -- so a
     * computer whose picture comes from a driver the kernel has to load
     * has no display for the first seconds. Ask again, and keep listening
     * meanwhile: nothing it is told is lost. */
    while (!quit) {
        st.now = wall();
        if (!d && st.now >= next_open) {
            d = kms_open(NULL);
            if (!d) next_open = st.now + 0.5;
            else st.dirty = 1;
        }
        if (d && (st.dirty || st.mode == M_INSTALL)) {
            surface *s = kms_back_surface(d);
            if (s) { paint(s, &st); kms_flip(d); }
            st.dirty = 0;
        }
        struct pollfd p = { 0, POLLIN, 0 };
        if (!open_in || poll(&p, 1, 1000) <= 0) {
            if (!open_in) { struct timespec t = { 1, 0 }; nanosleep(&t, NULL); }
            continue;
        }
        ssize_t n = read(0, buf + have, sizeof buf - 1 - have);
        if (n == 0) { open_in = 0; continue; }   /* aurstage is gone; keep the last picture */
        if (n < 0) { if (errno != EAGAIN && errno != EINTR) open_in = 0; continue; }
        have += (size_t)n;
        buf[have] = 0;
        char *a = buf, *nl;
        while ((nl = strchr(a, '\n'))) {
            *nl = 0;
            feed(&st, a);
            a = nl + 1;
        }
        have = strlen(a);
        memmove(buf, a, have + 1);
        if (have >= sizeof buf - 2) have = 0;    /* a line that long is noise */
    }
    if (d) kms_close(d);
    return 0;
}
