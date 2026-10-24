/* primarytest.c -- one primary action per panel (EASY: one obvious next step).
 *
 * Every panel's button painter adds one to shell_primary_n when it draws THE button (the filled,
 * lit one). A panel with two of them has two answers to "what do I press?". This paints Help (with a
 * program open and the help pages there, so both its buttons show), Settings on every page she can
 * reach, the Wi-Fi panel and the Bluetooth panel, in the states a computer with no radio is in, and
 * holds each paint to one at most. (The first-run screens are journeytest's: "no panel draws two
 * primary buttons".)
 *
 *   cc -O2 -std=gnu11 -o /tmp/primarytest tools/primarytest.c \
 *      -DFOOT_HELP_PAGES='"/tmp/primarytest.index.html"' \
 *      src/aurshell/settings.c src/aurshell/net.c src/aurshell/bt.c src/aurshell/power.c \
 *      src/aurshell/run.c src/aurshell/foot.c src/aurshell/welcome.c src/aurshell/draw.c \
 *      src/aurshell/shellcommon.c src/aurshell/anim.c src/aurshell/layouts/[a-z]*.c \
 *      src/common/theme.c src/common/font.c -I src/aurshell -I src/common -lm
 *   (repository root; `net` and `bt` may want -ldbus-1 flags as in tools/README.md)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/aurshell/shell.h"
#include "../src/aurshell/settings.h"
#include "../src/aurshell/net.h"
#include "../src/aurshell/bt.h"
#include "../src/aurshell/foot.h"

static int fails, checks;
static void ok(int good, const char *w) { checks++; if (!good) fails++; printf("  %-76s %s\n", w, good ? "ok" : "FAIL"); }

static shell_ctx C;
static shell_fonts F;
static surface *SURF;

static void reset_view(void) { shell_text_log_n = 0; shell_text_log_on = 1; shell_primary_n = 0; }
static void end_view(void)   { shell_text_log_on = 0; }
static int press(const char *words)
{
    reset_view(); settings_paint(&C, SURF, &F); end_view();
    for (int i = 0; i < shell_text_log_n; i++)
        if (!strcmp(shell_text_log[i].s, words)) {
            int x = shell_text_log[i].x + shell_text_log[i].w / 2, y = shell_text_log[i].y + shell_text_log[i].h / 2;
            settings_motion(&C, x, y);
            return settings_click(&C, x, y);
        }
    return 0;
}

int main(void)
{
    FILE *h = fopen("/tmp/primarytest.index.html", "w"); if (h) { fputs("<p>help</p>\n", h); fclose(h); }
    static const struct { int w, h; } SIZES[] = { { 1024, 600 }, { 1366, 768 } };
    static const float SCALES[] = { 1.0f, 1.5f };
    char home[64]; snprintf(home, sizeof home, "/tmp/primarytest.%d", (int)getpid()); mkdir(home, 0700); setenv("HOME", home, 1);
    for (size_t sz = 0; sz < 2; sz++)
        for (size_t k = 0; k < 2; k++) {
            memset(&C, 0, sizeof C);
            theme_t t = {0};
            shell_theme_load(&C, &t);
            C.allow_settings = C.allow_theme_change = C.allow_network = 1;
            C.text_scale = SCALES[k]; C.foot_hover = -1;
            snprintf(C.layout_id, sizeof C.layout_id, "rail");
            C.screen_w = SIZES[sz].w; C.screen_h = SIZES[sz].h - foot_height(&C);
            shell_fonts_free(&F); memset(&F, 0, sizeof F);
            shell_fonts_load(&F, &C);
            if (SURF) surface_free(SURF);
            SURF = surface_new(SIZES[sz].w, SIZES[sz].h);
            char at[64]; snprintf(at, sizeof at, "%dx%d text %.0f%%", SIZES[sz].w, SIZES[sz].h, (double)(SCALES[k] * 100.f));
            char w[160];

            /* Help, with a program open (the way out of a page) and the pages there (show me how). */
            C.n_wins = 1; snprintf(C.wins[0].title, sizeof C.wins[0].title, "A page"); C.wins[0].app = -1;
            C.help_open = 1;
            reset_view(); foot_paint(&C, SURF, &F); end_view();
            snprintf(w, sizeof w, "%s: Help draws one lit button (both of its buttons are there)", at);
            ok(shell_primary_n == 1, w);
            C.help_open = 0; C.n_wins = 0;

            /* Settings: the first page, then every row that opens a page. */
            C.settings_open = 1; settings_opened(&C);
            reset_view(); settings_paint(&C, SURF, &F); end_view();
            snprintf(w, sizeof w, "%s: Settings draws at most one lit button", at);
            ok(shell_primary_n <= 1, w);
            static const char *ROWS[] = { "Colors", "How this computer works", "The time and date", "Headphones and mice", "Put Windows back" };
            for (size_t r = 0; r < sizeof ROWS / sizeof *ROWS; r++) {
                if (!press(ROWS[r])) continue;                       /* a row this machine has not */
                reset_view(); settings_paint(&C, SURF, &F); end_view();
                snprintf(w, sizeof w, "%s: Settings > %s draws at most one lit button", at, ROWS[r]);
                ok(shell_primary_n <= 1, w);
                C.settings_open = 0; C.settings_open = 1; settings_opened(&C);
            }
            C.settings_open = 0;

            /* Wi-Fi and Bluetooth, on a computer with no radio and none of nmcli's answers. */
            C.net_open = 1; net_opened(&C);
            reset_view(); net_paint(&C, SURF, &F); end_view();
            snprintf(w, sizeof w, "%s: Wi-Fi draws at most one lit button", at);
            ok(shell_primary_n <= 1, w);
            C.net_open = 0; net_closed(&C);
            C.bt_open = 1; bt_opened(&C);
            reset_view(); bt_paint(&C, SURF, &F); end_view();
            snprintf(w, sizeof w, "%s: Bluetooth draws at most one lit button", at);
            ok(shell_primary_n <= 1, w);
            C.bt_open = 0;
        }
    char rm[120]; snprintf(rm, sizeof rm, "rm -rf '%s'", home);
    if (system(rm) != 0) fprintf(stderr, "could not remove %s\n", home);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
