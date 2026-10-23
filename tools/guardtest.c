/* guardtest.c — can a page trap her, and is a stranger's program stopped?
 *
 * Drives src/aurshell/guard.c the way main.c does: every key through
 * guard_watch_key() first, clicks through foot_click() then
 * guard_click(), guard_step() once per pass. The sound is a stand-in
 * (power_muted/power_mute_set below), so the test can see it go off and
 * come back. Then main.c is read as text to make sure the hooks are
 * really there, in the right order -- the event loop cannot be linked.
 *
 * tools/scamtest/guardprove.sh breaks each protection in a copy of
 * guard.c or main.c, rebuilds this, and insists it fails.
 *
 *   cc -O2 -std=gnu11 -o /tmp/guardtest tools/guardtest.c \
 *      src/aurshell/guard.c src/aurshell/foot.c src/aurshell/draw.c \
 *      src/aurshell/shellcommon.c src/aurshell/anim.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      -I src/aurshell -I src/common $(pkg-config --cflags --libs dbus-1) -lm \
 *      -DGUARD_CONSENT_DIR='"/tmp/guardtest.d"' && /tmp/guardtest [main.c]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../src/aurshell/shell.h"
#include "../src/aurshell/foot.h"
#include "../src/aurshell/guard.h"

/* ── the sound, standing in for power.c ─────────────────────────── */
static int sound_muted, mute_calls;
int  power_muted(void) { return sound_muted; }
void power_mute_set(int m) { sound_muted = m; mute_calls++; }

static int fail = 0, checked = 0;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-62s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

#define ESC 1
#define RET 28
#define DEL 111
#define LCTRL 29
#define LALT 56
#define DOWN 108

static int64_t T = 100000;      /* the test's clock, in ms */

static void fresh(shell_ctx *c, int wins)
{
    /* Close whatever the last case left open, the way the host would
     * on its next pass, so guard.c's own state starts clean too. */
    c->guard_open = 0;
    guard_step(c);
    memset(c, 0, sizeof *c);
    theme_t t = {0};
    shell_theme_load(c, &t);
    c->text_scale = 1.f;
    c->foot_hover = -1;
    c->screen_w = 1024;
    c->screen_h = 600 - foot_height(c);
    c->allow_settings = 1;
    c->allow_network = 1;
    c->n_wins = wins;
    c->focus = wins ? 0 : -1;
    sound_muted = 0; mute_calls = 0;
    T += 10000;                 /* far from any earlier Escape */
}

/* A key down then up, as the host delivers it. Returns what the guard
 * said about the press; *up gets what it said about the release. */
static int tap(shell_ctx *c, int code, int *up)
{
    int d = guard_watch_key(c, code, 1, T);
    if (c->guard_open && !d) guard_key(c, code);   /* the panel chain */
    int u = guard_watch_key(c, code, 0, T + 40);
    if (up) *up = u;
    T += 300;
    return d;
}

static int press_button(shell_ctx *c, int id)
{
    rect b[GUARD_MAX_BUTTONS]; int w[GUARD_MAX_BUTTONS];
    int n = guard_buttons(c, c->screen_w, c->screen_h, b, w);
    for (int i = 0; i < n; i++)
        if (w[i] == id) {
            int x = b[i].x + b[i].w / 2, y = b[i].y + b[i].h / 2;
            if (!foot_click(c, x, y)) guard_click(c, x, y);
            guard_step(c);
            return 1;
        }
    return 0;
}

static int has_button(shell_ctx *c, int id)
{
    rect b[GUARD_MAX_BUTTONS]; int w[GUARD_MAX_BUTTONS];
    int n = guard_buttons(c, c->screen_w, c->screen_h, b, w);
    for (int i = 0; i < n; i++) if (w[i] == id) return 1;
    return 0;
}

/* ── 1. the keys no page can have ───────────────────────────────── */
static void keys(void)
{
    printf("  a page cannot keep her: Escape and Ctrl+Alt+Delete\n");
    shell_ctx c; memset(&c, 0, sizeof c);

    fresh(&c, 1);
    int up = 0;
    int a = tap(&c, ESC, NULL), b = tap(&c, ESC, NULL), d = tap(&c, ESC, &up);
    ok("Escape three times opens 'Get me out of this page'",
       c.guard_open && guard_mode() == GUARD_OUT);
    ok("...the first two still reach the page (it may need them)", !a && !b);
    ok("...the third, and its release, reach nothing else", d && up);

    fresh(&c, 1);
    for (int i = 0; i < 3; i++) { guard_watch_key(&c, ESC, 1, T); guard_watch_key(&c, ESC, 0, T + 40); T += 1500; }
    ok("three slow Escapes (one every 1.5 s) are just Escapes", !c.guard_open);

    fresh(&c, 0);
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    ok("at Home, with nothing open, Escape is left alone", !c.guard_open);

    fresh(&c, 1);
    foot_open_only(&c, &c.help_open);
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    ok("Escape in another panel belongs to that panel", !c.guard_open);

    fresh(&c, 0);
    guard_watch_key(&c, LCTRL, 1, T); guard_watch_key(&c, LALT, 1, T + 10);
    d = guard_watch_key(&c, DEL, 1, T + 20);
    up = guard_watch_key(&c, DEL, 0, T + 60);
    guard_watch_key(&c, LALT, 0, T + 70); guard_watch_key(&c, LCTRL, 0, T + 80);
    ok("Ctrl+Alt+Delete opens it, anywhere", c.guard_open && guard_mode() == GUARD_OUT);
    ok("...and Delete goes nowhere else", d && up);

    fresh(&c, 1);
    guard_watch_key(&c, LCTRL, 1, T);
    d = guard_watch_key(&c, DEL, 1, T + 20);
    guard_watch_key(&c, DEL, 0, T + 40); guard_watch_key(&c, LCTRL, 0, T + 50);
    ok("Ctrl+Delete alone is an ordinary key", !c.guard_open && !d);

    fresh(&c, 1);
    c.kiosk = 1;
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    ok("a kiosk has none of this (it has no way to close anything)", !c.guard_open);
}

/* ── 2. the card itself ─────────────────────────────────────────── */
static void way_out(void)
{
    printf("\n  the way out: quiet at once, then out\n");
    shell_ctx c; memset(&c, 0, sizeof c);

    fresh(&c, 1);
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    guard_step(&c);
    ok("the sound goes off the moment it opens", sound_muted == 1);
    ok("it offers 'Close this page' and 'Go back to it'",
       has_button(&c, G_CLOSE) && has_button(&c, G_BACK));
    press_button(&c, G_CLOSE);
    ok("'Close this page' stops the program now, not politely",
       c.want_close_win == 2 && !c.guard_open);
    ok("...and the sound comes back as it was", sound_muted == 0);

    fresh(&c, 1);
    sound_muted = 1;                       /* she had it off already */
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    guard_step(&c);
    press_button(&c, G_BACK);
    ok("'Go back to it' closes nothing", c.want_close_win == 0 && !c.guard_open);
    ok("sound she had turned off herself stays off", sound_muted == 1);

    fresh(&c, 1);
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    guard_step(&c);
    foot_open_only(&c, &c.settings_open);  /* the band, pressed instead */
    guard_step(&c);
    ok("closed by the band instead: the sound still comes back",
       sound_muted == 0 && !c.guard_open && c.settings_open);

    /* A hand still pressing Escape must not close it again at once. */
    fresh(&c, 1);
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    T -= 250;                              /* a fourth, 50 ms later */
    tap(&c, ESC, NULL);
    ok("a fourth Escape straight after does not close it", c.guard_open);
    T += 1500;
    tap(&c, ESC, NULL);
    ok("Escape later means 'Go back' -- and closes nothing",
       !c.guard_open && c.want_close_win == 0);

    fresh(&c, 1);
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    T += 1500;
    tap(&c, RET, NULL);
    ok("Return with nothing chosen does nothing", c.guard_open && c.want_close_win == 0);
    tap(&c, DOWN, NULL); tap(&c, RET, NULL);
    ok("Down, Return: the first choice, as a click would", c.want_close_win == 2);

    fresh(&c, 1);
    tap(&c, ESC, NULL); tap(&c, ESC, NULL); tap(&c, ESC, NULL);
    int leaked = 0;
    /* Every key but the four that act (Escape, Return, keypad Enter,
     * Space): those are tested above, and one of them would close it. */
    for (int k = 2; k < 200; k++)
        if (k != ESC && k != RET && k != 96 && k != 57 && !guard_key(&c, k)) leaked++;
    ok("while it is up, no key reaches the page behind it", leaked == 0 && c.guard_open);
    ok("...and SHELL_PANEL_OPEN knows it is up", SHELL_PANEL_OPEN(&c));

    /* Help: the one place a frightened person presses. */
    fresh(&c, 1);
    foot_open_only(&c, &c.help_open);
    rect r;
    int there = foot_help_out(&c, c.screen_w, c.screen_h + foot_height(&c), &r);
    ok("Help shows 'Get me out of this page' while a program is open", there);
    if (there) {
        foot_click(&c, r.x + r.w / 2, r.y + r.h / 2);
        guard_step(&c);
    }
    ok("...pressing it opens the way out, quietly",
       c.guard_open && !c.help_open && guard_mode() == GUARD_OUT && sound_muted == 1);
    rect pages;
    if (there && foot_help_button(&c, c.screen_w, c.screen_h + foot_height(&c), &pages))
        ok("...above 'Show me how to do things', not on top of it",
           pages.y >= r.y + r.h);
    fresh(&c, 0);
    foot_open_only(&c, &c.help_open);
    ok("with nothing open, Help does not offer it",
       !foot_help_out(&c, c.screen_w, c.screen_h + foot_height(&c), &r));

    fresh(&c, 0);
    guard_watch_key(&c, LCTRL, 1, T); guard_watch_key(&c, LALT, 1, T);
    guard_watch_key(&c, DEL, 1, T);
    ok("with nothing open, it offers only 'OK'",
       has_button(&c, G_OK) && !has_button(&c, G_CLOSE));
}

/* ── 3. a stranger's program ────────────────────────────────────── */
static int consent_age(const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", GUARD_CONSENT_DIR, name);
    struct stat st;
    if (lstat(p, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (int)(time(NULL) - st.st_mtime);
}

static void remote(void)
{
    printf("\n  before a remote-control program goes on\n");
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", GUARD_CONSENT_DIR,
             GUARD_CONSENT_DIR);
    if (system(cmd) != 0) { ok("make the test's answer folder", 0); return; }

    shell_ctx c; memset(&c, 0, sizeof c);
    fresh(&c, 1);
    guard_remote(&c, "anydesk");
    guard_step(&c);
    ok("polkit stopped it: she is asked", c.guard_open && guard_mode() == GUARD_REMOTE);
    ok("...without turning her sound off", sound_muted == 0 && mute_calls == 0);
    ok("the safe answer comes first", has_button(&c, G_NO) && has_button(&c, G_YES));
    T += 1500;
    tap(&c, RET, NULL);
    ok("Return with nothing chosen says nothing", c.guard_open && consent_age("anydesk") < 0);
    tap(&c, ESC, NULL);
    ok("Escape is 'Don't install it', never yes",
       !c.guard_open && consent_age("anydesk") < 0);
    ok("...and she is told nothing was installed",
       strstr(c.say, "Nothing was installed") != NULL);

    fresh(&c, 1);
    guard_remote(&c, "anydesk");
    tap(&c, DOWN, NULL); tap(&c, RET, NULL);
    ok("Down, Return lands on the safe answer", !c.guard_open && consent_age("anydesk") < 0);

    fresh(&c, 1);
    guard_remote(&c, "anydesk");
    press_button(&c, G_NO);
    ok("'Don't install it' writes nothing", !c.guard_open && consent_age("anydesk") < 0);

    fresh(&c, 1);
    guard_remote(&c, "anydesk");
    press_button(&c, G_YES);
    int age = consent_age("anydesk");
    ok("'I called someone I know' writes her answer, just now", age >= 0 && age < 5);
    ok("...for that program only", consent_age("teamviewer") < 0);
    ok("...and tells her to try again", c.guard_open && guard_mode() == GUARD_REMOTE_OK &&
                                        has_button(&c, G_OK));
    press_button(&c, G_OK);
    ok("'OK' closes it", !c.guard_open);

    static const char *BAD[] = { "../etc", "AnyDesk", "any desk", "a", "",
                                 "abcdefghijklmnopqrstuvwxyz0123456789", NULL };
    int opened = 0;
    for (int i = 0; BAD[i]; i++) {
        fresh(&c, 1);
        guard_remote(&c, BAD[i]);
        if (c.guard_open) opened++;
    }
    ok("a name that is not plain letters and digits is ignored", opened == 0);
    fresh(&c, 1);
    c.kiosk = 1;
    guard_remote(&c, "anydesk");
    ok("not on a kiosk (it cannot install anything)", !c.guard_open);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", GUARD_CONSENT_DIR);
    if (system(cmd) != 0) { }
}

/* ── 4. big enough to hit, on the screen, at every size ─────────── */
static void sizes(void)
{
    printf("\n  every button is 44 px or more and on the screen\n");
    static const int RES[][2] = { {1024, 600}, {1366, 768}, {1920, 1080} };
    static const float SC[] = { 0.8f, 1.f, 1.45f, 2.f };
    int bad = 0, n_all = 0;
    for (int r = 0; r < 3; r++)
        for (int s = 0; s < 4; s++)
            for (int mode = 0; mode < 4; mode++) {
                shell_ctx c; memset(&c, 0, sizeof c);
                fresh(&c, mode == 3 ? 0 : 1);
                c.text_scale = SC[s];
                c.screen_w = RES[r][0];
                c.screen_h = RES[r][1] - foot_height(&c);
                if (mode == 1) guard_remote(&c, "anydesk");
                else { guard_watch_key(&c, LCTRL, 1, T); guard_watch_key(&c, LALT, 1, T);
                       guard_watch_key(&c, DEL, 1, T); }
                rect b[GUARD_MAX_BUTTONS]; int w[GUARD_MAX_BUTTONS];
                int n = guard_buttons(&c, c.screen_w, c.screen_h, b, w);
                for (int i = 0; i < n; i++, n_all++)
                    if (b[i].w < 44 || b[i].h < 44 || b[i].x < 0 || b[i].y < 0 ||
                        b[i].x + b[i].w > c.screen_w || b[i].y + b[i].h > c.screen_h) {
                        printf("      button %d at %dx%d text %.0f%%: %dx%d at %d,%d\n",
                               i, RES[r][0], RES[r][1], (double)(SC[s] * 100.f),
                               b[i].w, b[i].h, b[i].x, b[i].y);
                        bad++;
                    }
                c.guard_open = 0;
                foot_open_only(&c, &c.help_open);
                rect o;
                if (mode != 3 && foot_help_out(&c, c.screen_w, c.screen_h + foot_height(&c), &o)) {
                    n_all++;
                    if (o.w < 44 || o.h < 44 || o.y + o.h > c.screen_h) bad++;
                }
            }
    char w[96];
    snprintf(w, sizeof w, "%d buttons at 3 screens x 4 text sizes", n_all);
    ok(w, bad == 0 && n_all > 0);
}

/* ── 5. main.c really calls it, in the right places ─────────────── */
static char *slurp(const char *p)
{
    FILE *f = fopen(p, "r");
    if (!f) return NULL;
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static void wired(const char *main_c)
{
    printf("\n  main.c calls it where it must (%s)\n", main_c);
    char *s = slurp(main_c);
    if (!s) { ok("main.c can be read", 0); return; }
    char *watch = strstr(s, "guard_watch_key(&c, ev.code, ev.value");
    char *media = strstr(s, "media_key(&c, ev.code)) {");
    char *sess  = strstr(s, "consumed = session_key(&c, ev.code");
    ok("every key meets guard_watch_key before any program or the volume keys",
       watch && media && sess && watch < media && watch < sess);
    ok("the card answers the keyboard first when it is up",
       strstr(s, "if      (c.guard_open)    guard_key(&c, ev.code);") != NULL);
    char *gp = strstr(s, "guard_paint(&c, fb, &f);");
    char *wp = strstr(s, "welcome_paint(&c, fb, &f);");
    char *fp = strstr(s, "foot_paint(&c, fb, &f);");
    ok("painted over the welcome question and under the band",
       gp && wp && fp && wp < gp && gp < fp);
    int clicks = 0;
    for (char *p = s; (p = strstr(p, "guard_click(&c, c.mouse_x, c.mouse_y)")); p++) {
        char *w = strstr(p, "welcome_click(&c, c.mouse_x, c.mouse_y)");
        if (w && w - p < 200) clicks++;
    }
    ok("asked before the welcome question, on the mouse AND the touchpad", clicks == 2);
    ok("guard_pump and guard_step run every pass",
       strstr(s, "if (guard_pump(&c)) dirty = 1;") && strstr(s, "if (guard_step(&c)) dirty = 1;"));
    char *force = strstr(s, "int force = (c.want_close_win == 2);");
    char *kill  = force ? strstr(force, "if (force) {\n                        aurwl_win_kill(win);") : NULL;
    ok("'Close this page' (want_close_win 2) stops it without asking", force && kill);
    ok("what the card says afterwards is shown", strstr(s, "osd_say(c.say)") != NULL);
}

int main(int argc, char **argv)
{
    printf("\nCan a page trap her, and is a stranger's program stopped?\n\n");
    keys();
    way_out();
    remote();
    sizes();
    wired(argc > 1 ? argv[1] : "src/aurshell/main.c");
    printf("\n");
    if (fail) { printf("guardtest: %d of %d FAIL\n", fail, checked); return 1; }
    printf("guardtest: %d checks ok\n", checked);
    return 0;
}
