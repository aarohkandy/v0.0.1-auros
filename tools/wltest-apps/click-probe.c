/* click-probe.c -- is a click on a real program's button delivered?
 *
 *   click-probe SECONDS OUT.png X,Y [X,Y ...] -- command...
 *
 * Runs the command on the real compositor. From SECONDS/3 on it clicks, in
 * order, each point (window content coordinates, as in OUT.png; the first
 * run with no point shows where things are): the pointer moves there in
 * two steps, rests 400 ms (hover), presses, 100 ms later releases, then
 * waits 2.5 s. Run the program with WAYLAND_DEBUG=client to see what it
 * was sent: wl_pointer.enter / motion / button(serial, time, button,
 * state) and the surface each went to. Writes the window as OUT.png at the
 * end, and prints what the compositor did: windows, popups, focus.
 */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/aurwl/aurwl.h"
#include "../../src/common/png.h"

static uint32_t now_ms(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u); }

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: click-probe SECS OUT.png [X,Y ...] -- cmd...\n"); return 2; }
    int secs = atoi(argv[1]);
    const char *out = argv[2];
    int px[32], py[32], np = 0, a = 3;
    for (; a < argc && strcmp(argv[a], "--"); a++)
        if (np < 32 && sscanf(argv[a], "%d,%d", &px[np], &py[np]) == 2) np++;
    if (a >= argc) return 2;
    char cmd[4096] = {0};
    for (a++; a < argc; a++) { strncat(cmd, argv[a], sizeof cmd - strlen(cmd) - 2); strcat(cmd, " "); }

    aurwl *c = aurwl_create(1280, 800, 60000);
    if (!c) return 1;
    const char *sh[] = { "/bin/sh", "-c", cmd, NULL };
    if (aurwl_spawn(c, sh) < 0) return 1;
    uint32_t t0 = now_ms(), last_frame = 0, last = 0, next = 0;
    int k = 0, stage = 0;
    while (now_ms() - t0 < (uint32_t)secs * 1000u) {
        struct pollfd p = { aurwl_fd(c), POLLIN, 0 };
        poll(&p, 1, 8);
        aurwl_dispatch(c); aurwl_reap(c);
        uint32_t t = now_ms(), el = t - t0;
        if (t - last_frame >= 16) { aurwl_frame_done(c, t); last_frame = t; }
        int n = aurwl_window_count(c);
        aurwl_win *w = NULL;
        for (int i = 0; i < n; i++) { aurwl_win *x = aurwl_window_at(c, i); if (!aurwl_win_is_popup(x)) { w = x; break; } }
        if (w && !aurwl_focus(c)) aurwl_set_focus(c, w);
        if (w && k < np && el > (uint32_t)secs * 333u && t >= next) {
            int ox, oy; aurwl_win_content_offset(w, &ox, &oy);
            int x = ox + px[k], y = oy + py[k];
            if (stage == 0)      { aurwl_pointer_motion(c, w, x - 6, y - 3, t); aurwl_pointer_motion(c, w, x, y, t + 8);
                                   stage = 1; next = t + 400; }
            else if (stage == 1) { aurwl_pointer_button(c, 0x110, 1, t);
                                   printf("t=%us pressed at %d,%d\n", el / 1000, px[k], py[k]); fflush(stdout);
                                   stage = 2; next = t + 100; }
            else                 { aurwl_pointer_button(c, 0x110, 0, t);
                                   printf("t=%us released\n", el / 1000); fflush(stdout);
                                   stage = 0; k++; next = t + 2500; }
        }
        if (t - last >= 2000) {
            last = t;
            printf("t=%2us windows=%d", el / 1000, n);
            for (int i = 0; i < n; i++) {
                aurwl_win *x = aurwl_window_at(c, i); surface *s = aurwl_win_content(x);
                printf(" [\"%s\" %dx%d%s]", aurwl_win_title(x), s ? s->w : 0, s ? s->h : 0, aurwl_win_is_popup(x) ? " popup" : "");
            }
            printf("\n"); fflush(stdout);
        }
    }
    int n = aurwl_window_count(c);
    for (int i = 0; i < n; i++) {
        aurwl_win *w = aurwl_window_at(c, i);
        if (aurwl_win_is_popup(w)) continue;
        surface *s = aurwl_win_content(w);
        if (!s) break;
        uint32_t *tight = malloc((size_t)s->w * s->h * 4);
        for (int y = 0; tight && y < s->h; y++)
            for (int x = 0; x < s->w; x++) tight[(size_t)y * s->w + x] = s->px[(size_t)y * s->stride + x] & 0xFFFFFFu;
        if (tight) png_write_rgb(out, tight, s->w, s->h);
        free(tight);
        printf("%s %dx%d\n", out, s->w, s->h);
        break;
    }
    aurwl_destroy(c);
    return 0;
}
