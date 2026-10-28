/* stall-probe.c -- what a real program does when it is a moment behind.
 *
 *   stall-probe SECONDS PROCESS -- command...
 *
 * Runs the command on the real compositor. When its window has been up a
 * while, stops every process named PROCESS (SIGSTOP: a program that is busy
 * loading a page, or a machine that is slow), has the compositor
 * reconfigure the window a dozen times as a shell does when windows are
 * moved about, then lets the program go. A program that answers its
 * configures in order -- Firefox does -- is told something it was never
 * sent if the compositor has forgotten any of them. Prints the window list
 * once a second; the window is gone from it if the program was dropped.
 */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/aurwl/aurwl.h"

static uint32_t now_ms(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u); }

int main(int argc, char **argv)
{
    if (argc < 5 || strcmp(argv[3], "--")) { fprintf(stderr, "usage: stall-probe SECONDS PROCESS -- cmd...\n"); return 2; }
    int secs = atoi(argv[1]);
    const char *proc = argv[2];
    char cmd[4096] = {0};
    for (int i = 4; i < argc; i++) { strncat(cmd, argv[i], sizeof cmd - strlen(cmd) - 2); strcat(cmd, " "); }
    aurwl *c = aurwl_create(1280, 800, 60000);
    if (!c) return 1;
    const char *sh[] = { "/bin/sh", "-c", cmd, NULL };
    if (aurwl_spawn(c, sh) < 0) return 1;
    uint32_t t0 = now_ms(), last = 0, last_frame = 0, stall_at = 0;
    int phase = 0, k = 0;
    while (now_ms() - t0 < (uint32_t)secs * 1000u) {
        struct pollfd p = { aurwl_fd(c), POLLIN, 0 };
        poll(&p, 1, 8);
        aurwl_dispatch(c); aurwl_reap(c);
        uint32_t t = now_ms();
        if (t - last_frame >= 16) { aurwl_frame_done(c, t); last_frame = t; }
        int n = aurwl_window_count(c);
        aurwl_win *w = n > 0 ? aurwl_window_at(c, 0) : NULL;
        if (w && phase == 0 && t - t0 > (uint32_t)secs * 400u) {
            char kill[300]; snprintf(kill, sizeof kill, "pkill -STOP -x %s", proc);
            if (system(kill)) {}
            printf("stalled %s\n", proc); fflush(stdout);
            phase = 1; stall_at = t;
        } else if (phase == 1) {
            if (k < 12 && t - stall_at > (uint32_t)(80 * (k + 1))) {     /* a new size every 80 ms */
                aurwl_win_configure(w ? w : aurwl_window_at(c, 0), 700 + 20 * k, 500, 1, 0, 0);
                k++;
            } else if (k >= 12 && t - stall_at > 1500) {
                char go[300]; snprintf(go, sizeof go, "pkill -CONT -x %s", proc);
                if (system(go)) {}
                printf("let go of %s after %d configures\n", proc, k); fflush(stdout);
                phase = 2;
            }
        }
        if (t - last >= 1000) {
            last = t;
            printf("t=%2us windows=%d", (t - t0) / 1000, n);
            for (int i = 0; i < n; i++) {
                aurwl_win *x = aurwl_window_at(c, i);
                surface *s = aurwl_win_content(x);
                printf(" [\"%s\" %dx%d]", aurwl_win_title(x), s ? s->w : 0, s ? s->h : 0);
            }
            printf(" phase=%d\n", phase); fflush(stdout);
        }
    }
    aurwl_destroy(c);
    return 0;
}
