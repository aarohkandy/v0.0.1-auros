/* presstest.c — one press is one press (src/aurshell/press.c).
 *
 * Her habit is to double-click. The second press must not land on what
 * the first one put on the screen, and must not press the same control
 * twice. Run from the repository root:
 *
 *   cc -O2 -std=gnu11 -o /tmp/presstest tools/presstest.c src/aurshell/press.c \
 *      && /tmp/presstest
 *
 * It also reads src/aurshell/main.c to check the shell's loop really
 * asks (a gate nobody calls protects nobody).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/aurshell/press.h"

static int fail, checked;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-66s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

static int source_has(const char *needle)
{
    FILE *f = fopen("src/aurshell/main.c", "r");
    if (!f) { perror("src/aurshell/main.c"); exit(2); }
    static char buf[400000];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, needle) != NULL;
}

int main(void)
{
    press_state p = {0};
    printf("\none press is one press\n\n");

    press_screen(&p, 10000, 1);              /* she has been here a while */
    press_screen(&p, 20000, 1);
    ok("an ordinary press goes through", press_accept(&p, 20000, 100, 100));
    ok("the same control again 300 ms later is ignored",
       !press_accept(&p, 20300, 104, 98));
    ok("...and still 100 ms after that (the ignored one did not restart it)",
       !press_accept(&p, 20400, 100, 100));
    ok("500 ms after the one that counted, it is a new press",
       press_accept(&p, 20500, 100, 100));
    ok("another control 100 ms later is a different press",
       press_accept(&p, 20600, 600, 400));

    printf("\n  and when the screen changes under her\n");
    press_screen(&p, 30000, 2);              /* a page opened */
    ok("a press 200 ms after a page opened is ignored",
       !press_accept(&p, 30200, 500, 300));
    ok("...also one 699 ms after", !press_accept(&p, 30699, 500, 300));
    ok("700 ms after, it counts", press_accept(&p, 30700, 500, 300));
    press_screen(&p, 40000, 2);              /* still the same page */
    ok("a screen that did not change starts no dead time",
       press_accept(&p, 40001, 500, 300));
    press_screen(&p, 50000, 3);
    press_screen(&p, 50400, 4);              /* changed again */
    ok("each change starts it again",
       !press_accept(&p, 51000, 700, 500) && press_accept(&p, 51100, 700, 500));

    printf("\n  an ignored press is ignored whole\n");
    press_state q = {0};
    press_screen(&q, 1000, 1);
    press_screen(&q, 5000, 2);
    ok("down inside the dead time is dropped", !press_filter(&q, 1, 5100, 10, 10));
    ok("...and so is its release", !press_filter(&q, 0, 5180, 10, 10));
    ok("the next press after is let through", press_filter(&q, 1, 6000, 10, 10));
    ok("...and its release too", press_filter(&q, 0, 6050, 10, 10));
    ok("a release with no press behind it is let through (a drag ends)",
       press_filter(&q, 0, 7000, 10, 10));

    printf("\n  and the shell asks\n");
    ok("main.c tells it what screen she is on", source_has("press_screen(&pr"));
    ok("main.c filters mouse buttons through it", source_has("press_filter(&pr"));
    ok("main.c filters touchpad taps through it", source_has("press_accept(&pr"));

    printf("\n");
    if (fail) { printf("%d of %d wrong.\n", fail, checked); return 1; }
    printf("%d checks: a double-click is one press, and nothing lands on what\n"
           "it has just opened.\n", checked);
    return 0;
}
