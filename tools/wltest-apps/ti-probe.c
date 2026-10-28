/* ti-probe.c -- what does the compositor believe the focused field is?
 *
 *   ti-probe SECONDS [TAB_AT ...] -- command...
 *
 * Each TAB_AT is a second at which Tab is pressed (to move between fields).
 *
 * The real compositor (aurwl.c + textinput.c) with a real program on it,
 * keyboard focus given to the first window as the shell does. Every second
 * it prints what textinput_state() says (0 unknown, 1 plain, 2 sensitive) and
 * the raw hint/purpose. Used to prove that a password field in Firefox, whose
 * page is a subsurface, is seen as one -- Wishes (docs/AI.md 3.5) depend on it.
 */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/aurwl/aurwl.h"
#include "../../src/aurwl/textinput.h"

static uint32_t now_ms(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u); }

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: ti-probe SECONDS [TAB_AT...] -- cmd...\n"); return 2; }
    int secs = atoi(argv[1]), tabs[16], ntabs = 0, a = 2;
    for (; a < argc && strcmp(argv[a], "--"); a++) if (ntabs < 16) tabs[ntabs++] = atoi(argv[a]);
    if (a >= argc - 1) { fprintf(stderr, "no command\n"); return 2; }
    char cmd[4096] = {0};
    for (int i = a + 1; i < argc; i++) { strncat(cmd, argv[i], sizeof cmd - strlen(cmd) - 2); strcat(cmd, " "); }
    aurwl *c = aurwl_create(1280, 800, 60000);
    if (!c) return 1;
    const char *sh[] = { "/bin/sh", "-c", cmd, NULL };
    if (aurwl_spawn(c, sh) < 0) return 1;
    uint32_t t0 = now_ms(), last = 0, last_frame = 0;
    while (now_ms() - t0 < (uint32_t)secs * 1000u) {
        struct pollfd p = { aurwl_fd(c), POLLIN, 0 };
        poll(&p, 1, 16);
        aurwl_dispatch(c);
        aurwl_reap(c);
        uint32_t t = now_ms();
        if (t - last_frame >= 16) { aurwl_frame_done(c, t); last_frame = t; }
        if (aurwl_window_count(c) > 0 && !aurwl_focus(c)) aurwl_set_focus(c, aurwl_window_at(c, 0));
        if (t - last >= 1000) {
            last = t;
            for (int k = 0; k < ntabs; k++)
                if ((t - t0) / 1000 == (uint32_t)tabs[k]) {
                    aurwl_key(c, 15, 1, t); aurwl_key(c, 15, 0, t + 20);
                    printf("-- Tab\n");
                }
            unsigned hint = 0, purpose = 0;
            int en = textinput_peek(&hint, &purpose);
            printf("t=%2us windows=%d focus=%d state=%d enabled=%d hint=0x%x purpose=%u\n",
                   (t - t0) / 1000, aurwl_window_count(c), aurwl_focus(c) != NULL,
                   textinput_state(), en, hint, purpose);
            fflush(stdout);
        }
    }
    aurwl_destroy(c);
    return 0;
}
