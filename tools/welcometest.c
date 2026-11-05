/* welcometest.c — the question, and what pressing each answer asks for.
 *
 * The welcome panel is the only thing in this product that asks
 * whether AurOS works, and the only place a person can say no. It
 * draws, so tools/targets.c measures it; what THAT cannot see is the
 * thing this file is about: which word each button writes, which
 * screen follows which answer, and -- the one that matters most --
 * that nothing is written at all unless something was pressed.
 *
 * It runs against a scratch directory rather than /run/auros, which is
 * what WELCOME_RUN and WELCOME_STATE are for. Nothing here is root and
 * nothing here touches an EFI variable: the panel's whole privilege is
 * writing one word into a directory its own user owns.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "../src/aurshell/shell.h"
#include "../src/aurshell/foot.h"
#include "../src/aurshell/welcome.h"

/* The network, as net.c would say it (welcome.c asks for it when net.c is linked; here it is
 * this). Not static: it is the symbol welcome.c's weak reference finds. */
static int net_up = 1;
int net_online(void) { return net_up; }

static int fail = 0, checked = 0;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-58s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

static void wipe(void)
{
    unlink(WELCOME_RUN "/answer");
    unlink(WELCOME_ANSWER "/result");
    unlink(WELCOME_ANSWER "/import.log");
    unlink(WELCOME_ANSWER "/stick.log");
    unlink(WELCOME_RUN "/sticks.txt");
    unlink(WELCOME_RUN "/ferry");
    unlink(WELCOME_STATE);
    unlink(WELCOME_FOUND);
}

/* The answer directory, when the fixture puts it inside WELCOME_RUN --
 * which tools/README.md's build line does. It is the root side's, not
 * something the panel made, and it used to be counted as the panel's
 * second file. */
static int is_answer_dir(const char *name)
{
    const char *ans = strrchr(WELCOME_ANSWER, '/');
    return ans && !strncmp(WELCOME_ANSWER, WELCOME_RUN "/", strlen(WELCOME_RUN) + 1)
               && !strcmp(name, ans + 1);
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(text, f);
    fclose(f);
}

static int slurp(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t k = fread(out, 1, n - 1, f);
    fclose(f);
    out[k] = 0;
    return (int)k;
}

static void machine(shell_ctx *c)
{
    memset(c, 0, sizeof *c);
    theme_t t; memset(&t, 0, sizeof t);
    shell_theme_load(c, &t);
    c->text_scale = 1.f;
    c->screen_w = 1366;
    c->screen_h = 768 - foot_height(c);
}

/* Press the action whose label is `want`, wherever the layout put it.
 * Pressing by LABEL rather than by index: the whole point of the
 * column fallback is that the buttons move, and a test that pressed
 * "the second one" would follow them silently into the wrong answer. */
static int press(shell_ctx *c, int page, int can_import, const char *want)
{
    welcome_set_page(page);
    welcome_view v = { page, can_import };
    rect b[16];
    int n = welcome_targets(c, c->screen_w, c->screen_h, &v, b, 16);
    /* welcome_targets gives rectangles, not labels, so the press is
     * driven through the same click path the shell uses and the answer
     * is read from what it asked for. Each rectangle is tried in turn
     * and the one that produced the expected word is the answer. */
    for (int i = 0; i < n; i++) {
        unlink(WELCOME_RUN "/answer");
        welcome_set_page(page);
        c->welcome_open = 1;
        welcome_click(c, b[i].x + b[i].w / 2, b[i].y + b[i].h / 2);
        char got[64];
        if (slurp(WELCOME_RUN "/answer", got, sizeof got) > 0 &&
            !strcmp(got, want))
            return 1;
        if (!want[0] && !c->welcome_open) return 1;   /* a close button */
    }
    return 0;
}

/* Is `words` among the words the panel paints, on the page it is on? (shell_text_log: what
 * windowspagetest reads Settings by.) A line may be wrapped, so ask for a phrase short enough to
 * sit on one line of a 1366 px screen. */
static shell_fonts F_;
static surface *SURF_;
static int says(shell_ctx *c, const char *words)
{
    c->welcome_open = 1;
    shell_text_log_n = 0; shell_text_log_on = 1;
    welcome_paint(c, SURF_, &F_);
    shell_text_log_on = 0;
    for (int i = 0; i < shell_text_log_n; i++)
        if (strstr(shell_text_log[i].s, words) && shell_text_log[i].w > 0) return 1;
    return 0;
}
static int says_page(shell_ctx *c, int page, const char *words)
{
    welcome_set_page(page);
    return says(c, words);
}

/* A stand-in for `ferry stick list`: prints whatever is in sticks.txt. */
static void fake_ferry(const char *list)
{
    put(WELCOME_RUN "/sticks.txt", list);
    put(WELCOME_RUN "/ferry", "#!/bin/sh\ncat " WELCOME_RUN "/sticks.txt 2>/dev/null\n");
    chmod(WELCOME_RUN "/ferry", 0755);
}

/* What was asked: the request file's text, or "" if nothing was. */
static const char *asked_now(void)
{
    static char got[96];
    got[0] = 0;
    slurp(WELCOME_RUN "/answer", got, sizeof got);
    return got;
}

/* Press the button at index `i` of this page's targets. */
static int press_nth(shell_ctx *c, int page, int i)
{
    welcome_set_page(page);
    welcome_view v = { page, 0 };
    rect b[16];
    int n = welcome_targets(c, c->screen_w, c->screen_h, &v, b, 16);
    if (i >= n) return -1;
    c->welcome_open = 1;
    unlink(WELCOME_RUN "/answer");
    welcome_click(c, b[i].x + b[i].w / 2, b[i].y + b[i].h / 2);
    return n;
}

int main(void)
{
    mkdir(WELCOME_RUN, 0700);
    /* The root side's directory, which the answers are put into. The
     * build line in tools/README.md places it inside WELCOME_RUN; a
     * test that needs it and does not make it died on its first
     * put() with "No such file or directory". */
    mkdir(WELCOME_ANSWER, 0755);
    wipe();

    printf("\nThe question, and what each answer asks for\n\n");

    shell_ctx c; machine(&c);

    printf("  when it appears at all\n");
    welcome_init(&c);
    ok("a machine with no state file is not asked anything",
       c.welcome_open == 0);
    put(WELCOME_STATE, "phase=done\nconverted=yes\n");
    welcome_init(&c);
    ok("a machine that already answered is not asked again",
       c.welcome_open == 0);
    put(WELCOME_STATE, "phase=declined\n");
    welcome_init(&c);
    ok("nor one that said no", c.welcome_open == 0);
    put(WELCOME_STATE, "phase=not-converted\n");
    welcome_init(&c);
    ok("nor one AurOS was installed onto directly",
       c.welcome_open == 0);
    put(WELCOME_STATE, "phase=asking\n");
    welcome_init(&c);
    ok("and one that is waiting IS asked", c.welcome_open == 1);
    ok("...on the question, not halfway through it",
       welcome_page() == W_ASK);

    printf("\n  and what nothing has asked for\n");
    struct stat st;
    ok("opening it writes no request at all",
       stat(WELCOME_RUN "/answer", &st) != 0);

    /* YES AND NO EACH ASK TWICE. The question page used to write the
     * answer on the first press -- and her habit is to double-click
     * (docs/EASY.md), which pressed it before she had read it. */
    printf("\n  the question page decides nothing by itself\n");
    {
        welcome_set_page(W_ASK);
        welcome_view v = { W_ASK, 0 };
        rect b[16];
        int n = welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        int wrote = 0, to_yes = 0, to_no = 0;
        for (int i = 0; i < n; i++) {
            unlink(WELCOME_RUN "/answer");
            welcome_set_page(W_ASK);
            c.welcome_open = 1;
            welcome_click(&c, b[i].x + b[i].w / 2, b[i].y + b[i].h / 2);
            if (stat(WELCOME_RUN "/answer", &st) == 0) wrote = 1;
            if (welcome_page() == W_SURE_YES) to_yes = 1;
            if (welcome_page() == W_SURE_NO) to_no = 1;
        }
        ok("no button on the question writes an answer", n >= 3 && !wrote);
        ok("the question has the one check and the three answers", n == 4);
        ok("\"Yes, it all works\" asks again, on its own page", to_yes);
        ok("\"No, go back to Windows\" asks again, on its own page", to_no);
        c.want_launch = 0; c.welcome_open = 1;
    }

    /* E-03: she went to check something, so the question steps aside and
     * a chip stays to say how to come back. */
    printf("\n  and checking something\n");
    {
        welcome_set_page(W_ASK);
        c.welcome_open = 1;
        welcome_view v = { W_ASK, 0 };
        rect b[16];
        welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        c.n_apps = 0;
        /* the check button is first; with no program to open it does nothing,
         * so make one the way the shell would */
        c.n_apps = 1;
        memset(&c.apps[0], 0, sizeof c.apps[0]);
        c.apps[0].icon = ICON_GLOBE;
        welcome_click(&c, b[0].x + b[0].w / 2, b[0].y + b[0].h / 2);
        ok("pressing the check opens it and puts the question aside",
           c.welcome_open == 0 && c.want_launch == 1);
        ok("...and the way back is a press away, in the same place every time",
           welcome_click(&c, c.screen_w / 2, 20) == 1 && c.welcome_open == 1);
        ok("...which brings the question back at the first page", welcome_page() == W_ASK);
        c.n_apps = 0; c.want_launch = 0;
    }

    printf("\n  the three answers\n");
    ok("\"Yes, it all works\" asks for confirm",
       press(&c, W_SURE_YES, 0, "confirm"));
    ok("...and then says so on screen", welcome_page() == W_WORKING);
    ok("\"No, go back to Windows\" asks for decline",
       press(&c, W_SURE_NO, 0, "decline"));
    /* The import is offered only when firstboot.sh found a Windows to
     * read, so the fixture has to have found one. This is what
     * ferry-detect writes. */
    put(WELCOME_FOUND,
        "{\"device\":\"/dev/sda2\",\"windows\":\"10\",\"clean\":true}\n");
    put(WELCOME_STATE, "phase=asking\n");
    welcome_init(&c);
    ok("\"Bring my files across\" asks for import",
       press(&c, W_CONFIRMED, 1, "import"));

    /* A machine with no Windows worth reading must not be offered an
     * import: a button that fails when pressed is worse than no
     * button, which is the rule this product keeps relearning. */
    unlink(WELCOME_FOUND);
    put(WELCOME_STATE, "phase=asking\n");
    welcome_init(&c);
    welcome_set_page(W_CONFIRMED);
    {
        welcome_view v = { W_CONFIRMED, 0 };
        rect b[16];
        int n = welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        int asked = 0;
        for (int i = 0; i < n; i++) {
            unlink(WELCOME_RUN "/answer");
            welcome_set_page(W_CONFIRMED);
            c.welcome_open = 1;
            welcome_click(&c, b[i].x + b[i].w / 2, b[i].y + b[i].h / 2);
            char got[64];
            if (slurp(WELCOME_RUN "/answer", got, sizeof got) > 0) asked = 1;
        }
        ok("with no Windows on the disk, nothing offers to import from it",
           n > 0 && !asked);
    }

    printf("\n  letting go without answering\n");
    welcome_set_page(W_ASK);
    c.welcome_open = 1;
    unlink(WELCOME_RUN "/answer");
    welcome_key(&c, 1);                  /* escape */
    ok("escape closes it", c.welcome_open == 0);
    ok("...and answers nothing", stat(WELCOME_RUN "/answer", &st) != 0);
    welcome_set_page(W_ASK);
    c.welcome_open = 1;
    welcome_click(&c, 4, 4);             /* the background */
    ok("pressing the background does nothing", c.welcome_open == 1);
    ok("...and asks for nothing", stat(WELCOME_RUN "/answer", &st) != 0);
    ok("...and is still consumed, so it cannot reach what is behind it",
       welcome_click(&c, 4, 4) == 1);

    /* A DIFFERENT BUTTON, and this check could not fail until it was.
     *
     * It pressed b[0], which for this page IS "Yes, it all works" --
     * so the assertion held whether the in-flight request had been
     * preserved or overwritten with an identical "confirm". A review
     * changed ask_for's O_EXCL to O_TRUNC, so a second press really
     * did replace the request, and this still printed ok. Pressing
     * "No, go back to Windows" and requiring "confirm" to survive is
     * the same check with something to catch. */
    printf("\n  and one request at a time\n");
    unlink(WELCOME_RUN "/answer");
    press(&c, W_SURE_YES, 0, "confirm");
    welcome_set_page(W_SURE_NO);
    c.welcome_open = 1;
    {
        welcome_view v = { W_SURE_NO, 0 };
        rect b[16];
        int n = welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        int last = 0;                      /* "Go back to Windows" */
        welcome_click(&c, b[last].x + b[last].w / 2,
                      b[last].y + b[last].h / 2);
        char got[64];
        int k = slurp(WELCOME_RUN "/answer", got, sizeof got);
        ok("a second press while one is in flight does not replace it",
           n >= 2 && k > 0 && !strcmp(got, "confirm"));
    }

    printf("\n  reading the answer back\n");
    unlink(WELCOME_RUN "/answer");
    press(&c, W_SURE_YES, 0, "confirm");
    c.welcome_open = 1;
    ok("nothing moves while there is no answer yet",
       welcome_step(&c) == 0 && welcome_page() == W_WORKING);
    put(WELCOME_ANSWER "/result", "request=confirm\nresult=ok\n");
    ok("an answer moves it on", welcome_step(&c) == 1);
    ok("...to the screen that offers to bring her files",
       welcome_page() == W_CONFIRMED);

    unlink(WELCOME_RUN "/answer");
    press(&c, W_SURE_NO, 0, "decline");
    c.welcome_open = 1;
    put(WELCOME_ANSWER "/result", "request=decline\nresult=ok\n");
    welcome_step(&c);
    ok("saying no reaches the screen that says Windows is next",
       welcome_page() == W_DECLINED);

    unlink(WELCOME_RUN "/answer");
    press(&c, W_SURE_YES, 0, "confirm");
    c.welcome_open = 1;
    put(WELCOME_ANSWER "/result",
        "request=confirm\nresult=failed\nnote=this computer would not take it\n");
    welcome_step(&c);
    ok("a refusal reaches the screen that says so, not the one that lies",
       welcome_page() == W_TROUBLE);

    /* A result file caught halfway through being written has no
     * result line. answer.sh writes it whole and renames, so this is
     * the belt to that braces -- and the failure it prevents is the
     * panel saying "done" because it read an empty file. */
    unlink(WELCOME_RUN "/answer");
    press(&c, W_SURE_YES, 0, "confirm");
    c.welcome_open = 1;
    put(WELCOME_ANSWER "/result", "request=confirm\n");
    ok("a half-written answer is not an answer",
       welcome_step(&c) == 0 && welcome_page() == W_WORKING);

    /* THE WHOLE OF ITS PRIVILEGE, checked rather than described. A
     * panel that quietly wrote somewhere else would be a panel whose
     * comment about never needing to be root had stopped being true. */
    printf("\n  and the panel never needs to be root\n");
    {
        unlink(WELCOME_RUN "/answer");
        unlink(WELCOME_ANSWER "/result");
        DIR *d = opendir(WELCOME_RUN);
        int before = 0;
        struct dirent *e;
        while (d && (e = readdir(d)))
            if (e->d_name[0] != '.' && !is_answer_dir(e->d_name)) before++;
        if (d) closedir(d);
        press(&c, W_SURE_YES, 0, "confirm");
        welcome_step(&c);
        d = opendir(WELCOME_RUN);
        int after = 0, only_answer = 1;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.' || is_answer_dir(e->d_name)) continue;
            after++;
            if (strcmp(e->d_name, "answer") && strcmp(e->d_name, "first.state") &&
                strcmp(e->d_name, "found.json"))
                only_answer = 0;
        }
        if (d) closedir(d);
        ok("one press leaves one new file behind, and it is the request",
           after == before + 1 && only_answer);
    }

    /* AN ANSWER TO ANOTHER QUESTION IS NOT AN ANSWER. The panel did
     * not used to remember what it asked, so a result left over from
     * a previous session -- or one a second request produced -- was
     * acted on, and a confirm of unknown outcome showed "Your files
     * are here. Open Files to see them." */
    printf("\n  and an answer to something it did not ask\n");
    unlink(WELCOME_RUN "/answer");
    press(&c, W_SURE_YES, 0, "confirm");
    c.welcome_open = 1;
    put(WELCOME_ANSWER "/result", "request=import\nresult=ok\n");
    ok("a result for another request is left alone",
       welcome_step(&c) == 0 && welcome_page() == W_WORKING);
    put(WELCOME_ANSWER "/result", "request=confirm\nresult=zzz\n");
    welcome_step(&c);
    ok("and a result nothing recognises is trouble, not silence",
       welcome_page() == W_TROUBLE);

    /* ── whose files? (docs/FERRY.md: result=choose, users=Bob,Harold) ── */
    printf("\n  whose files, when Windows has more than one person\n");
    {
        unlink(WELCOME_RUN "/answer");
        put(WELCOME_FOUND, "{\"device\":\"/dev/sda2\",\"windows\":\"10\",\"clean\":true}\n");
        put(WELCOME_STATE, "phase=asking\n");
        welcome_init(&c);
        press(&c, W_CONFIRMED, 1, "import");           /* she pressed Bring my files across */
        c.welcome_open = 1;
        put(WELCOME_ANSWER "/result",
            "request=import\nresult=choose\ncode=6\nusers=Bob,Harold\nnote=more than one person uses Windows\n");
        ok("several people and no way to tell: the answer is a question",
           welcome_step(&c) == 1 && welcome_page() == W_WHOSE);
        welcome_view v = { W_WHOSE, 0 };
        rect b[16];
        int n = welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        ok("...one button for each person, and \"Not now\"", n == 3);
        usleep(30000);       /* a person takes longer than the file system's clock ticks (4 ms) */
        press_nth(&c, W_WHOSE, 1);
        ok("pressing the second person asks for import:Harold, by name",
           !strcmp(asked_now(), "import:Harold"));
        ok("...and goes to the screen that says it is being brought", welcome_page() == W_IMPORTING);
        /* The question's own answer is still on disk (request=import, choose): it
         * is not the answer to the name she just pressed. */
        ok("...and the old answer, still on disk, is not mistaken for the new one",
           welcome_step(&c) == 0 && welcome_page() == W_IMPORTING);
        usleep(30000);
        unlink(WELCOME_RUN "/answer");
        put(WELCOME_ANSWER "/import.log", "Looking at the Windows part of this computer.\nCopying your Pictures: 1,200 of 5,000 files. About 4 minutes left.\n");
        welcome_step(&c);
        put(WELCOME_ANSWER "/result", "request=import\nresult=ok\nfiles=5000\n");
        ok("the answer to import:Harold is read as the answer to import",
           welcome_step(&c) == 1 && welcome_page() == W_IMPORTED);

        /* Her name first, and no name the root side's door would refuse. */
        welcome_test_people("Bob,Harold,Mary Ann");
        setenv("USER", "Harold", 1);
        welcome_test_people("Bob,Harold,Mary Ann");
        welcome_view v2 = { W_WHOSE, 0 };
        n = welcome_targets(&c, c.screen_w, c.screen_h, &v2, b, 16);
        ok("three people and \"Not now\"", n == 4);
        press_nth(&c, W_WHOSE, 0);
        ok("the account's own name is first", !strcmp(asked_now(), "import:Harold"));
        welcome_test_people("Bob,../etc,a:b,-x,Mary Ann,.hidden,Harold.");
        n = welcome_targets(&c, c.screen_w, c.screen_h, &v2, b, 16);
        ok("a name that could be read as a path or an option is never offered (Bob, Mary Ann)", n == 3);
        unlink(WELCOME_RUN "/answer");
    }

    /* ── nothing to bring ─────────────────────────────────────────── */
    printf("\n  and when there was nothing to bring\n");
    {
        unlink(WELCOME_RUN "/answer");
        press(&c, W_CONFIRMED, 1, "import");
        c.welcome_open = 1;
        put(WELCOME_ANSWER "/result", "request=import\nresult=empty\nfiles=0\nnote=there were no files\n");
        ok("an empty Windows is its own screen, not an error",
           welcome_step(&c) == 1 && welcome_page() == W_EMPTY);
        welcome_view v = { W_EMPTY, 0 };
        rect b[16];
        int n = welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        ok("...with one way on", n == 1);
        c.welcome_open = 1;
        unlink(WELCOME_RUN "/answer");
        welcome_click(&c, b[0].x + b[0].w / 2, b[0].y + b[0].h / 2);
        ok("...which closes it (nothing is asked for)", c.welcome_open == 0 && !asked_now()[0]);
    }

    /* ── Save my files to a USB stick ─────────────────────────────── */
    printf("\n  saving her files on a USB stick\n");
    {
        fake_ferry("");
        c.welcome_open = 0;
        welcome_open_stick(&c);
        ok("with no stick in, it says to plug one in", c.welcome_open == 1 && welcome_page() == W_STICK_PLUG);
        fake_ferry("/dev/sdb1\tSanDisk (16 GB)\t16000000000\texfat\n/dev/sdc1\tBACKUP (500 GB)\t500000000000\tntfs\n");
        welcome_view v = { W_STICK_PLUG, 0 };
        rect b[16];
        welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        c.welcome_open = 1;
        welcome_click(&c, b[0].x + b[0].w / 2, b[0].y + b[0].h / 2);       /* "Look again" */
        ok("\"Look again\" finds the stick that was plugged in", welcome_page() == W_STICK_PICK);
        welcome_view v2 = { W_STICK_PICK, 0 };
        int n = welcome_targets(&c, c.screen_w, c.screen_h, &v2, b, 16);
        ok("...one button for each stick, and \"Not now\"", n == 3);
        press_nth(&c, W_STICK_PICK, 1);
        ok("pressing the second stick asks to save on that one", !strcmp(asked_now(), "stick:/dev/sdc1"));
        ok("...and goes to the screen that says it is being saved", welcome_page() == W_STICK_SAVING);
        unlink(WELCOME_RUN "/answer");
        put(WELCOME_ANSWER "/stick.log", "Getting ready to save your files on the USB stick.\nSaving your Pictures: 200 of 5,000 files. About 3 minutes left.\n");
        ok("the screen follows what the copy says", welcome_step(&c) == 1);
        put(WELCOME_ANSWER "/result", "request=stick\nresult=ok\nfiles=5000\n");
        ok("saved: the screen that says it is safe to take the stick out",
           welcome_step(&c) == 1 && welcome_page() == W_STICK_DONE);

        press_nth(&c, W_STICK_PICK, 0);
        put(WELCOME_ANSWER "/result", "request=stick\nresult=partial\ncode=2\nfiles=40\n");
        welcome_step(&c);
        ok("some not saved: still the done screen (and it says so)", welcome_page() == W_STICK_DONE);
        press_nth(&c, W_STICK_PICK, 0);
        put(WELCOME_ANSWER "/result", "request=stick\nresult=failed\ncode=4\nwhy=noroom\nnote=there is not enough room on the USB stick\n");
        welcome_step(&c);
        ok("no room on the stick: the screen that says so", welcome_page() == W_TROUBLE);
        unlink(WELCOME_RUN "/answer");
        welcome_view vt = { W_TROUBLE, 0 };
        n = welcome_targets(&c, c.screen_w, c.screen_h, &vt, b, 16);
        c.welcome_open = 1;
        welcome_click(&c, b[0].x + b[0].w / 2, b[0].y + b[0].h / 2);         /* Try again */
        ok("\"Try again\" starts the stick screens over, not the first question", welcome_page() == W_STICK_PICK);
        press_nth(&c, W_STICK_PICK, 0);
        put(WELCOME_ANSWER "/result", "request=stick\nresult=empty\nfiles=0\n");
        welcome_step(&c);
        ok("nothing of hers to save: its own calm screen", welcome_page() == W_EMPTY);
        unlink(WELCOME_RUN "/answer");

        /* Letting go. */
        welcome_open_stick(&c);
        welcome_key(&c, 1);
        ok("escape lets go of it, and nothing was asked for", c.welcome_open == 0 && !asked_now()[0]);
    }

    /* ── Put Windows back could not save her files first ───────────── */
    printf("\n  when Put Windows back cannot save her files in Windows\n");
    {
        fake_ferry("");
        static const char *WHY[] = { "toomany", "locked", "noroom", "dirty", "nodrive", "some" };
        for (int i = 0; i < 6; i++) {
            c.welcome_open = 0;
            welcome_open_blocked(&c, WHY[i]);
            welcome_view v = { W_BLOCKED, 0 };
            rect b[16];
            int n = welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
            char what[80];
            snprintf(what, sizeof what, "why=%s: a screen with two ways on", WHY[i]);
            ok(what, c.welcome_open == 1 && welcome_page() == W_BLOCKED && n == 2);
        }
        c.welcome_open = 0;
        welcome_open_blocked(&c, "toomany");
        welcome_view v = { W_BLOCKED, 0 };
        rect b[16];
        welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        welcome_click(&c, b[0].x + b[0].w / 2, b[0].y + b[0].h / 2);
        ok("\"Save my files to a USB stick first\" goes to the stick screens, and removes nothing",
           welcome_page() == W_STICK_PLUG && !asked_now()[0]);
        c.welcome_open = 0;
        welcome_open_blocked(&c, "locked");
        welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
        welcome_click(&c, b[1].x + b[1].w / 2, b[1].y + b[1].h / 2);
        ok("\"Keep AurOS\" closes it and changes nothing", c.welcome_open == 0 && !asked_now()[0]);
        c.welcome_open = 0;
        welcome_open_blocked(&c, "noroom");
        welcome_key(&c, 1);
        ok("escape is \"Keep AurOS\", never an answer", c.welcome_open == 0 && !asked_now()[0]);
    }

    /* WHAT THE PAGES SAY, before she presses, in words that match what the code does. */
    printf("\n  what the pages tell her\n");
    {
        shell_fonts_load(&F_, &c);
        SURF_ = surface_new(c.screen_w, 768);
        ok("\"Yes\" says it can be undone, and where",
           says_page(&c, W_SURE_YES, "press Settings, then Put Windows back"));
        ok("\"No\" says, before it is pressed, that she cannot start AurOS again herself",
           says_page(&c, W_SURE_NO, "AurOS will not start again by itself") &&
           says(&c, "ask someone who knows computers"));
        ok("after \"Yes\" she is told where help is",
           says_page(&c, W_CONFIRMED, "press Help at the bottom left"));
        /* a note from the root side is a clause: it is shown as a sentence */
        unlink(WELCOME_RUN "/answer");
        press(&c, W_SURE_YES, 0, "confirm");
        c.welcome_open = 1;
        put(WELCOME_ANSWER "/result",
            "request=confirm\nresult=failed\nnote=this computer would not let AurOS change what it starts\n");
        welcome_step(&c);
        ok("a failure's reason starts with a capital and ends with a full stop",
           welcome_page() == W_TROUBLE &&
           says(&c, "This computer would not let AurOS change what it starts."));
    }


    /* THE SECOND PAGE EARNS ITS PLACE, AND "NOT YET" SAYS WHAT IS WAITING. Yes asked "Start AurOS
     * from now on?" and said, in the same words as the line beside Yes, what Yes does; Not yet
     * went back to a page that looked as if nothing had happened. */
    printf("\n  the second page, and Not yet\n");
    {
        put(WELCOME_STATE, "phase=asking\n");
        welcome_init(&c);
        ok("beside Yes, the question says what Yes does",
           says_page(&c, W_ASK, "AurOS starts every time from now on."));
        ok("the second page is the last step, not that sentence again",
           says_page(&c, W_SURE_YES, "This is the last step") && !says(&c, "AurOS starts every time from now on"));
        ok("...and it still says Windows and her files stay, and the way back",
           says(&c, "Windows and its files are meant to stay on this computer.") &&
           says(&c, "press Settings, then Put Windows back"));
        ok("the first question, before anything, is just \"Check one thing first.\"",
           says_page(&c, W_ASK, "Check one thing first.") && !says(&c, "Nothing has changed yet"));
        unlink(WELCOME_RUN "/answer");
        press_nth(&c, W_SURE_YES, 1);                  /* Not yet */
        ok("\"Not yet\" goes back to the question and asks for nothing",
           welcome_page() == W_ASK && !asked_now()[0]);
        ok("...which now says nothing has changed, and that it asks again until she answers",
           says(&c, "Nothing has changed yet. AurOS asks this again until you answer.") &&
           !says(&c, "Check one thing first."));
        ok("Escape on the second page says the same", ({
               welcome_set_page(W_ASK); welcome_init(&c);          /* a fresh start */
               welcome_set_page(W_SURE_YES); c.welcome_open = 1;
               welcome_key(&c, 1);
               welcome_page() == W_ASK && says(&c, "Nothing has changed yet");
           }));
        press_nth(&c, W_ASK, 1);                       /* Yes again */
        ok("pressing Yes again is a new question (the sentence goes)",
           welcome_page() == W_SURE_YES);
        welcome_init(&c);
        ok("and the next start asks it afresh",
           says_page(&c, W_ASK, "Check one thing first."));
    }

    /* NOT CONNECTED YET, ON A COMPUTER THAT HAS JUST STARTED, IS NOT TRUE: a cable takes up to
     * a minute to be given an address, and she was told, and sent to Wi-Fi. */
    printf("\n  the internet check\n");
    {
        put(WELCOME_STATE, "phase=asking\n");
        welcome_init(&c);
        net_up = 0; welcome_test_uptime(10);
        ok("just started, no network yet: it says it is looking",
           says_page(&c, W_ASK, "Checking the internet. One moment.") && !says(&c, "Not connected"));
        ok("...and the button is the Wi-Fi one, which is where help is", says(&c, "Open Wi-Fi"));
        welcome_set_page(W_ASK); c.welcome_open = 1;
        welcome_step(&c);
        ok("...and the panel asks to be woken every second while it looks", welcome_wait_ms() == 1000);
        ok("...and nothing is repainted until it knows more", welcome_step(&c) == 0);
        welcome_test_uptime(200);
        ok("after a minute and a half it says it is not connected, and the screen follows",
           welcome_step(&c) == 1 && says(&c, "Not connected to the internet yet.") && says(&c, "Open Wi-Fi"));
        ok("...and the panel stops waking", welcome_wait_ms() == -1);
        net_up = 1;
        ok("a network that arrives is noticed", welcome_step(&c) == 1 &&
           says(&c, "Does the internet work?") && says(&c, "Open Internet"));
        net_up = 0; welcome_test_uptime(10);
        {   /* pressing the check while it looks opens the Wi-Fi page, not a browser's error page */
            welcome_set_page(W_ASK); c.welcome_open = 1;
            c.n_apps = 1; memset(&c.apps[0], 0, sizeof c.apps[0]); c.apps[0].icon = ICON_GLOBE;
            c.want_launch = 0; c.net_open = 0;
            welcome_view v = { W_ASK, 0 };
            rect b[16];
            welcome_targets(&c, c.screen_w, c.screen_h, &v, b, 16);
            welcome_click(&c, b[0].x + b[0].w / 2, b[0].y + b[0].h / 2);
            ok("pressing it while it looks opens Wi-Fi, not a browser", c.net_open == 1 && c.want_launch == 0);
            c.net_open = 0; c.n_apps = 0; c.welcome_open = 1;
        }
        net_up = 1; welcome_test_uptime(-1);
    }

    /* "NOT NOW ... YOU CAN DO THIS LATER FROM SETTINGS" has to be true: Settings offers it. */
    printf("\n  bringing her files across, from Settings\n");
    {
        put(WELCOME_FOUND, "{\"device\":\"/dev/sda2\",\"windows\":\"10\",\"clean\":true}\n");
        put(WELCOME_STATE, "phase=asking\nconverted=yes\n");
        ok("not offered while the first question is open (the root side would refuse it)", !welcome_can_bring());
        put(WELCOME_STATE, "phase=done\nconverted=yes\n");
        ok("offered once she has said yes, and there is a Windows to read", welcome_can_bring());
        put(WELCOME_STATE, "phase=declined\nconverted=yes\n");
        ok("not on a computer that said no", !welcome_can_bring());
        put(WELCOME_STATE, "phase=done\nconverted=yes\n");
        unlink(WELCOME_FOUND);
        ok("not when there is no Windows on the disk", !welcome_can_bring());
        put(WELCOME_FOUND, "{\"device\":\"/dev/sda2\",\"windows\":\"10\",\"clean\":true}\n");

        welcome_init(&c);
        unlink(WELCOME_RUN "/answer"); unlink(WELCOME_ANSWER "/result");
        welcome_open_bring(&c);
        ok("it opens on its own screen", c.welcome_open == 1 && welcome_page() == W_BRING);
        ok("...and asks nothing until she presses", !asked_now()[0]);
        ok("...which says what it does", says(&c, "Bring my files across") &&
           says(&c, "Your documents and pictures are still in Windows.") && says(&c, "Nothing in Windows changes."));
        ok("\"Bring my files across\" asks for import, the way the first start does",
           press_nth(&c, W_BRING, 0) > 0 && !strcmp(asked_now(), "import") && welcome_page() == W_IMPORTING);
        put(WELCOME_ANSWER "/result", "request=import\nresult=empty\nfiles=0\n");
        welcome_step(&c);
        ok("nothing to bring is a calm screen that says so, in these words, not the USB stick's",
           welcome_page() == W_EMPTY && says(&c, "No files to bring across") && !says(&c, "No files to save"));
        c.welcome_open = 1;
        press_nth(&c, W_EMPTY, 0);
        ok("...and Done closes it", c.welcome_open == 0);
        unlink(WELCOME_RUN "/answer"); unlink(WELCOME_ANSWER "/result");
        welcome_open_bring(&c);
        press_nth(&c, W_BRING, 0);
        put(WELCOME_ANSWER "/result", "request=import\nresult=refused\nnote=AurOS has not been confirmed yet\n");
        welcome_step(&c);
        ok("a refusal is trouble, in her words", welcome_page() == W_TROUBLE && says(&c, "AurOS has not been confirmed yet."));
        unlink(WELCOME_RUN "/answer");
        press_nth(&c, W_TROUBLE, 0);                     /* Try again */
        ok("\"Try again\" is this question again, not the USB stick's", welcome_page() == W_BRING);
        unlink(WELCOME_RUN "/answer"); unlink(WELCOME_ANSWER "/result");
        welcome_open_bring(&c);
        press_nth(&c, W_BRING, 0);
        put(WELCOME_ANSWER "/result", "request=import\nresult=ok\nfiles=12\n");
        welcome_step(&c);
        ok("when they are here, it says so and offers to open them, and Done (not \"Start using AurOS\")",
           welcome_page() == W_IMPORTED && says(&c, "Your files are here") && says(&c, "Open Your files") &&
           says(&c, "Done") && !says(&c, "Start using AurOS"));
        unlink(WELCOME_RUN "/answer"); unlink(WELCOME_ANSWER "/result");
        welcome_open_bring(&c);
        welcome_key(&c, 1);
        ok("Escape and \"Not now\" leave it, asking for nothing",
           c.welcome_open == 0 && !asked_now()[0]);
        welcome_open_bring(&c);
        press_nth(&c, W_BRING, 1);
        ok("...Not now closes it", c.welcome_open == 0 && !asked_now()[0]);
        welcome_open_stick(&c);
        ok("the USB stick's pages are not the bring page afterwards", welcome_page() != W_BRING);
        c.welcome_open = 0;
    }

    wipe();
    printf("\n");
    if (fail) {
        printf("%d of %d wrong. This is the only place anybody can say\n",
               fail, checked);
        printf("whether AurOS works, or say no to it.\n");
        return 1;
    }
    printf("%d checks: it appears only while the question is open, each\n",
           checked);
    printf("button asks for one word, and closing it answers nothing.\n");
    return 0;
}
