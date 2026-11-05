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
 *      -DWELCOME_FERRY='"/tmp/windowspagetest.d/ferry"' \
 *      -DWELCOME_FOUND='"/tmp/windowspagetest.d/found.json"' && /tmp/windowspagetest
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

/* A program started, as the shell's spawn hook would be told. */
static char spawned[400];
static int spawn_hook(shell_ctx *c, const char *blob, int n)
{
    (void)c; (void)n;
    snprintf(spawned, sizeof spawned, "%s", blob);
    return 0;
}

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
/* Is `part` inside any word the panel painted? (A sentence wraps over several lines.) */
static int shown_has(const char *part)
{
    paint();
    for (int i = 0; i < shell_text_log_n; i++)
        if (strstr(shell_text_log[i].s, part) && shell_text_log[i].w > 0) return 1;
    return 0;
}
/* Press at the left end or the right end of the painted words `words`: 0 left, 1 right. */
static int press_end(const char *words, int right)
{
    paint();
    for (int i = 0; i < shell_text_log_n; i++)
        if (!strcmp(shell_text_log[i].s, words)) {
            int x = right ? shell_text_log[i].x + shell_text_log[i].w - 2 : shell_text_log[i].x + 2;
            int y = shell_text_log[i].y + shell_text_log[i].h / 2;
            settings_motion(&C, x, y);
            return settings_click(&C, x, y);
        }
    return -1;
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

    printf("Settings: the size of the words\n");
    C.settings_open = 1;
    settings_opened(&C);
    ok(shown("100%"), "at the size it came at, the words row says 100%");
    ok(!shown("17%"), "...not where the handle sits between smallest and largest (that read 17%)");
    C.text_scale = 1.5f;
    ok(shown("150%"), "bigger words say 150%");
    C.text_scale = 1.f;

    /* Two things the first start and the help pages promise are in Settings. "Not now" on
     * "Bring my files across" said "You can do this later from Settings", and Windows' disk is
     * hidden from the file manager (81-auros-windows-disk.rules), so the files were out of
     * reach; and printing.html had no way to add a printer. */
    printf("Settings: Printers, and Bring my files across\n");
    {
        char tool[400];
        snprintf(tool, sizeof tool, "%s/printers", WELCOME_RUN);
        put(tool, "#!/bin/sh\nexit 0\n");
        chmod(tool, 0755);
        setenv("AUROS_PRINTER_TOOL", tool, 1);
        put(WELCOME_FOUND, "{\"device\":\"/dev/sda2\",\"windows\":\"10\",\"clean\":true}\n");
        C.spawn = spawn_hook;
        C.settings_open = 1;
        settings_opened(&C);
        /* (the list may have more rows than fit: the page's own "Show more" brings the rest) */
        ok(shown("Printers") || (press("Show more") && shown("Printers")), "the first page has a Printers row");
        settings_opened(&C);
        ok(shown("Bring my files across") || (press("Show more") && shown("Bring my files across")),
           "...and a \"Bring my files across\" row, the one \"Not now\" promised");
        spawned[0] = 0;
        settings_opened(&C);
        if (!shown("Printers")) press("Show more");
        ok(press("Printers"), "she presses Printers");
        ok(!strcmp(spawned, tool), "...which starts the printer tool, by its own name");
        ok(C.settings_open == 0, "...and Settings steps aside so that its window is what she sees");
        C.settings_open = 1;
        settings_opened(&C);
        if (!shown("Bring my files across")) press("Show more");
        unlink(WELCOME_RUN "/answer");
        C.welcome_open = 0;
        ok(press("Bring my files across"), "she presses Bring my files across");
        ok(C.welcome_open == 1 && welcome_page() == W_BRING,
           "...the screen that offers to copy them opens");
        char asked[96];
        ok(!slurp(WELCOME_RUN "/answer", asked, sizeof asked), "...and nothing is asked until she presses its button");
        C.welcome_open = 0;

        /* and not offered where it would not work */
        C.settings_open = 1;
        put(WELCOME_STATE, "phase=asking\nconverted=yes\n");
        settings_opened(&C);
        ok(!shown("Bring my files across") && !(press("Show more") && shown("Bring my files across")),
           "while the first question is still open there is no such row (the root side would refuse it)");
        put(WELCOME_STATE, "phase=done\nconverted=yes\n");
        unlink(WELCOME_FOUND);
        settings_opened(&C);
        ok(!shown("Bring my files across") && !(press("Show more") && shown("Bring my files across")),
           "on a computer with no Windows to read there is no such row");
        put(WELCOME_FOUND, "{\"device\":\"/dev/sda2\",\"windows\":\"10\",\"clean\":true}\n");
        setenv("AUROS_PRINTER_TOOL", WELCOME_RUN "/no-such-tool", 1);
        settings_opened(&C);
        ok(!shown("Printers") && !(press("Show more") && shown("Printers")),
           "with no printer tool installed there is no Printers row (a row that does nothing is worse than none)");
        setenv("AUROS_PRINTER_TOOL", tool, 1);
        snprintf(C.allowed_apps, sizeof C.allowed_apps, "%s", "firefox");
        settings_opened(&C);
        ok(!shown("Printers") && !(press("Show more") && shown("Printers")),
           "where only some programs are allowed, no Printers row either");
        C.allowed_apps[0] = 0;
        settings_opened(&C);
        C.settings_open = 1;
        settings_opened(&C);
    }

    /* BIG WORDS ON THE SMALLEST PANEL. The buttons at the foot of Settings were all one width, and
     * at the biggest words "Close Settings" was drawn past its box into "Show more"; and at 150%
     * the page that says what removing AurOS loses was cut to one line, "Your documents, pict". */
    printf("Settings with big words on the smallest panel\n");
    {
        surface *small = surface_new(1024, 600);
        surface *was = SURF;
        float was_scale = C.text_scale;
        int was_w = C.screen_w, was_h = C.screen_h;
        C.screen_w = 1024;
        SURF = small;
        C.text_scale = 2.0f;
        C.screen_h = 600 - foot_height(&C);
        shell_fonts_load(&F, &C);
        C.settings_open = 1;
        settings_opened(&C);
        int l = press_end("Close Settings", 0);
        ok(l == 1 && C.settings_open == 0, "at 200%, the left end of \"Close Settings\" closes Settings");
        C.settings_open = 1; settings_opened(&C);
        int r = press_end("Close Settings", 1);
        ok(r == 1 && C.settings_open == 0, "...and so does the right end of its words (the box is as wide as they are)");
        C.settings_open = 1; settings_opened(&C);
        press_end("Show more", 1);
        ok(C.settings_open == 1, "...and the right end of \"Show more\" is Show more, not Close");

        C.text_scale = 1.5f;
        C.screen_h = 600 - foot_height(&C);
        shell_fonts_load(&F, &C);
        C.settings_open = 1; settings_opened(&C);
        for (int i = 0; i < 12 && !shown("Put Windows back"); i++) press("Show more");
        ok(press("Put Windows back"), "at 150%, she opens Put Windows back");
        /* (the text log keeps 72 letters of a line: the last sentence starts in the second line) */
        ok(shown_has("If they cannot be"), "...and its sentence goes on past the first line, to what happens if they cannot be");

        SURF = was;
        C.text_scale = was_scale; C.screen_w = was_w; C.screen_h = was_h;
        shell_fonts_load(&F, &C);
        surface_free(small);
        C.settings_open = 1; settings_opened(&C);
    }

    printf("Put Windows back: her files first\n");
    ok(press("Put Windows back"), "\"Put Windows back\" opens its page");
    ok(shown("Save my files to a USB stick first"), "the page has \"Save my files to a USB stick first\"");
    ok(shown("Keep AurOS") && shown("Start Windows this once") && shown("Remove AurOS and put Windows back"),
       "...beside the three it already had");

    /* What removing AurOS does with her files. Both kinds of install copy her folders into
     * Windows first (answer.sh back_files) and refuse, removing nothing, when they cannot; the
     * page used to say they "go with" AurOS. */
    /* (a row's line is CHOICE_LEN - 1 characters at most: the long form is on the page) */
    ok(shown("Your files are copied into Windows first."),
       "the row that removes AurOS says her files are copied into Windows first");
    ok(shown_start("This computer restarts and removes AurOS."), "...under a page that says what it does");
    ok(shown("Your files are copied onto a stick. Nothing is removed."),
       "the USB stick row says in full that nothing is removed (it was cut off after \"Not\")");
    paint();
    int gone = 0;
    for (int i = 0; i < shell_text_log_n; i++)
        if (strstr(shell_text_log[i].s, "goes with it")) gone = 1;
    ok(!gone, "...and nowhere does the page say her files go with AurOS");

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
