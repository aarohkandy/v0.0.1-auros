/* wishinert.c — a build WITHOUT Wishes (decision D14: AUROS_WISHES is 0 in
 * the v0.1 image) must be inert. The panel's code is in the shell either way
 * (the shell is built from every file in src/aurshell), so this proves the
 * part that matters: it never looks for the engine, never wakes the main
 * loop, never sends a line, and no door opens.
 *
 * Compiled with no -DAUROS_WISHES at all, the same as the release shell.
 * tools/wishuitest.sh builds and runs it, and breaks it on purpose.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "../src/aurshell/wish.c"
#include "../src/aurshell/wishover.c"
#include "../src/aurshell/wishgallery.c"

static int checked, failed;
static void ok(const char *what, int good)
{
    checked++;
    if (!good) failed++;
    printf("  %-72s %s\n", what, good ? "ok" : "FAIL");
}

int main(void)
{
    printf("a build without Wishes\n");
    ok("the flag is off, as in the release image", AUROS_WISHES == 0);

    /* An engine that is listening: if the shell looked for it, it would connect. */
    char dir[] = "/tmp/wishinert.XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    setenv("XDG_RUNTIME_DIR", dir, 1);
    char path[300];
    snprintf(path, sizeof path, "%s/aurwish.sock", dir);
    int ls = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 4) < 0) { perror("listen"); return 2; }

    shell_ctx c;
    memset(&c, 0, sizeof c);
    theme_t t = {0};
    shell_theme_load(&c, &t);
    c.screen_w = 1366; c.screen_h = 700;
    for (int i = 0; i < 5; i++) { wish_step(&c); usleep(20000); }
    ok("it never looks for the engine, even with one listening", accept(ls, NULL, NULL) < 0 && K.fd < 0);
    ok("it has no socket for the main loop to wait on", wish_fd() < 0);
    ok("and no reason to wake the loop", wish_wait_ms() < 0);
    wish_input_seen();
    ok("keystrokes are not reported to anything", wish_step(&c) == 0 && !input_pending);
    wish_command(&c, WISH_CMD_TODAY);
    wish_command(&c, WISH_CMD_STOP_ALL);
    ok("Help's buttons, if any, open nothing and stop nothing", !c.wish_open && wishover_cards() == 0);
    shell_seed_apps(&c);
    ok("there is no Make a wish entry to press", shell_wish_app(&c) < 0);
    int has = 0;
    for (int i = 0; i < c.n_apps; i++) if (c.apps[i].role == APP_ROLE_WISH) has = 1;
    ok("the home has no Make a wish entry", !has);

    close(ls);
    unlink(path);
    rmdir(dir);
    printf("\n%d checks, %d failed\n", checked, failed);
    return failed ? 1 : 0;
}
