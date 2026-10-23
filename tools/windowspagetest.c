/* windowspagetest.c — "Put Windows back", and the way to keep her files first.
 *
 * Settings has a page for going back to Windows. It could not save her files
 * in Windows first on every computer (a locked drive, too many files, no
 * room), refused, and told her in one line to "save them to a USB stick
 * first" -- with no button for that. Now the page has the row, and a
 * refusal with a reason (answer.sh's why=) opens the screen that says why
 * and offers the stick (welcome.c).
 *
 * Driven by its words, as she does: Settings, "Put Windows back", the rows.
 * The root side is a directory this test writes into.
 *
 *   cc -O2 -std=gnu11 -o /tmp/windowspagetest tools/windowspagetest.c \
 *      src/aurshell/settings.c src/aurshell/power.c src/aurshell/run.c \
 *      src/aurshell/foot.c src/aurshell/welcome.c src/aurshell/draw.c \
 *      src/aurshell/shellcommon.c src/aurshell/anim.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      -I src/aurshell -I src/common -lm \
 *      -DWELCOME_RUN='"/tmp/windowspagetest.d"' \
 *      -DWELCOME_ANSWER='"/tmp/windowspagetest.d/answer.d"' \
 *      -DWELCOME_STATE='"/tmp/windowspagetest.d/first.state"' \
 *      -DWELCOME_FERRY='"/tmp/windowspagetest.d/ferry"' && /tmp/windowspagetest
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/aurshell/shell.h"
#include "../src/aurshell/settings.h"
#include "../src/aurshell/welcome.h"
#include "../src/aurshell/foot.h"

static int fails, checks;
static void ok(int c, const char *w) { checks++; if (!c) fails++; printf("  %-72s %s\n", w, c ? "ok" : "FAIL"); }

static shell_ctx C;
static shell_fonts F;
static surface *SURF;

static void paint(void)
{
    shell_text_log_n = 0; shell_text_log_on = 1;
    settings_paint(&C, SURF, &F);
    shell_text_log_on = 0;
}
static int shown(const char *words)
{
    paint();
    for (int i = 0; i < shell_text_log_n; i++)
        if (!strcmp(shell_text_log[i].s, words) && shell_text_log[i].w > 0) return 1;
    return 0;
}
static int shown_start(const char *prefix)
{
    paint();
    for (int i = 0; i < shell_text_log_n; i++)
        if (!strncmp(shell_text_log[i].s, prefix, strlen(prefix)) && shell_text_log[i].w > 0) return 1;
    return 0;
}
static int press(const char *words)
{
    paint();
    for (int i = 0; i < shell_text_log_n; i++)
        if (!strcmp(shell_text_log[i].s, words)) {
            int x = shell_text_log[i].x + shell_text_log[i].w / 2;
            int y = shell_text_log[i].y + shell_text_log[i].h / 2;
            settings_motion(&C, x, y);
            return settings_click(&C, x, y);
        }
    return 0;
}
static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w"); if (!f) { perror(path); exit(2); }
    fputs(text, f); fclose(f);
}
static int slurp(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r"); if (!f) return 0;
    size_t k = fread(out, 1, n - 1, f); fclose(f); out[k] = 0; return (int)k;
}

int main(void)
{
    mkdir(WELCOME_RUN, 0700);
    mkdir(WELCOME_ANSWER, 0755);
    unlink(WELCOME_RUN "/answer");
    unlink(WELCOME_ANSWER "/result");
    put(WELCOME_STATE, "phase=done\nconverted=yes\n");
    put(WELCOME_RUN "/sticks.txt", "");
    put(WELCOME_RUN "/ferry", "#!/bin/sh\ncat " WELCOME_RUN "/sticks.txt 2>/dev/null\n");
    chmod(WELCOME_RUN "/ferry", 0755);

    theme_t t = {0};
    shell_theme_load(&C, &t);
    C.allow_settings = C.allow_theme_change = C.allow_network = 1;
    C.text_scale = 1.f; C.foot_hover = -1;
    snprintf(C.layout_id, sizeof C.layout_id, "rail");
    C.screen_w = 1280; C.screen_h = 800 - foot_height(&C);
    shell_fonts_load(&F, &C);
    SURF = surface_new(1280, 800);

    printf("Put Windows back: her files first\n");
    C.settings_open = 1;
    settings_opened(&C);
    ok(press("Put Windows back"), "\"Put Windows back\" opens its page");
    ok(shown("Save my files to a USB stick first"), "the page has \"Save my files to a USB stick first\"");
    ok(shown("Keep AurOS") && shown("Start Windows this once") && shown("Remove AurOS and put Windows back"),
       "...beside the three it already had");

    char got[96];
    ok(press("Save my files to a USB stick first"), "she presses it");
    ok(C.welcome_open == 1 && welcome_page() == W_STICK_PLUG,
       "the screen that asks for a stick opens (none is plugged in)");
    ok(!slurp(WELCOME_RUN "/answer", got, sizeof got), "...and nothing was asked of the root side: nothing is removed");
    C.welcome_open = 0;

    /* The refusal. Pressing "Remove AurOS ..." twice (the second after she has had time
     * to read what the first changed) asks for putback; the root side says no, with a reason. */
    ok(press("Remove AurOS and put Windows back"), "the first press only arms it");
    usleep(1700 * 1000);
    ok(press("Press again to restart and remove AurOS"), "the second asks");
    ok(slurp(WELCOME_RUN "/answer", got, sizeof got) && !strcmp(got, "putback"), "...for putback, and only that");
    ok(settings_step(&C) == 0 && !C.welcome_open, "nothing happens until the root side answers");
    put(WELCOME_ANSWER "/result",
        "request=putback\nresult=failed\ncode=4\nwhy=toomany\nnote=your files are more than AurOS can set aside to hand to Windows\n");
    ok(settings_step(&C) == 1, "its refusal reaches the page");
    ok(C.welcome_open == 1 && welcome_page() == W_BLOCKED,
       "...as the screen that says why and offers the stick (not one line of small words)");
    C.welcome_open = 0;
    paint();
    ok(shown_start("Nothing has changed: your files are more than AurOS"),
       "...and the page itself still says nothing has changed");

    /* A refusal with no reason this screen knows stays a line on the page. */
    unlink(WELCOME_RUN "/answer"); unlink(WELCOME_ANSWER "/result");
    ok(press("Remove AurOS and put Windows back"), "again: arm it");
    usleep(1700 * 1000);
    ok(press("Press again to restart and remove AurOS"), "...and ask");
    put(WELCOME_ANSWER "/result", "request=putback\nresult=failed\nnote=the files that put Windows back are not on this computer any more\n");
    settings_step(&C);
    ok(!C.welcome_open, "a refusal for another reason does not open the stick screen");

    unlink(WELCOME_RUN "/answer"); unlink(WELCOME_ANSWER "/result");
    unlink(WELCOME_RUN "/ferry"); unlink(WELCOME_RUN "/sticks.txt"); unlink(WELCOME_STATE);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
