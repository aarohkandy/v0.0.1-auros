/* cursor-probe.c -- what picture does a real program give the pointer?
 *
 *   cursor-probe SECONDS X1,Y1 X2,Y2 ... -- command...
 *
 * Runs the command on the real compositor, gives its window the keyboard,
 * and visits each window point in turn (SECONDS/N each), printing what
 * aurwl_cursor() says once a second: kind, picture size, hotspot, shape.
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
    int a = 1, n = 0, px[16], py[16];
    if (argc < 5) return 2;
    int secs = atoi(argv[a++]);
    for (; a < argc && strcmp(argv[a], "--"); a++)
        if (n < 16 && sscanf(argv[a], "%d,%d", &px[n], &py[n]) == 2) n++;
    if (a >= argc - 1 || !n) return 2;
    char cmd[4096] = {0};
    for (a++; a < argc; a++) { strncat(cmd, argv[a], sizeof cmd - strlen(cmd) - 2); strcat(cmd, " "); }
    aurwl *c = aurwl_create(1280, 800, 60000);
    if (!c) return 1;
    const char *sh[] = { "/bin/sh", "-c", cmd, NULL };
    if (aurwl_spawn(c, sh) < 0) return 1;
    uint32_t t0 = now_ms(), last = 0, last_frame = 0;
    while (now_ms() - t0 < (uint32_t)secs * 1000u) {
        struct pollfd p = { aurwl_fd(c), POLLIN, 0 };
        poll(&p, 1, 8);
        aurwl_dispatch(c); aurwl_reap(c);
        uint32_t t = now_ms();
        if (t - last_frame >= 16) { aurwl_frame_done(c, t); last_frame = t; }
        aurwl_win *w = aurwl_window_count(c) ? aurwl_window_at(c, 0) : NULL;
        if (w && !aurwl_focus(c)) aurwl_set_focus(c, w);
        if (!w) continue;
        /* start after the window has had time to draw, then step through the points */
        uint32_t el = t - t0, start = (uint32_t)secs * 1000u / 3;
        if (el < start) continue;
        int i = (int)((el - start) * (uint32_t)n / ((uint32_t)secs * 1000u - start + 1));
        if (i >= n) i = n - 1;
        int ox, oy; aurwl_win_content_offset(w, &ox, &oy);
        aurwl_pointer_motion(c, w, ox + px[i], oy + py[i], t);
        if (t - last >= 1000) {
            last = t;
            surface *im; int hx, hy, sh2;
            int k = aurwl_cursor(c, &im, &hx, &hy, &sh2);
            printf("t=%2us at %d,%d: kind=%d", el / 1000, px[i], py[i], k);
            if (k == AURWL_CURSOR_IMAGE) printf(" image %dx%d hot %d,%d", im->w, im->h, hx, hy);
            if (k == AURWL_CURSOR_SHAPE) printf(" shape %d", sh2);
            printf("\n"); fflush(stdout);
        }
    }
    aurwl_destroy(c);
    return 0;
}
