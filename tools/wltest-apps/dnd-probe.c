/* dnd-probe.c -- a real program's drag and drop, through the real compositor.
 *
 *   dnd-probe SECONDS OUT.png X1,Y1 X2,Y2 -- command...
 *
 * Runs the command (a file manager, say) on the compositor, gives its
 * window the keyboard, and at SECONDS/2 drags from window point (X1,Y1) to
 * (X2,Y2) in small steps with the left button held, then lets go. Points
 * are in the picture OUT.png (the window's content, which is also written
 * at the end), so the first run with 0,0 0,0 shows where things are.
 * Prints what the compositor saw: window size, whether a drag started
 * (the drag icon asks for one), and any protocol trouble.
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
    int a = 1;
    if (argc < 7) { fprintf(stderr, "usage: dnd-probe SECS OUT.png X1,Y1 X2,Y2 -- cmd...\n"); return 2; }
    int secs = atoi(argv[a++]);
    const char *out = argv[a++];
    int x1, y1, x2, y2;
    if (sscanf(argv[a++], "%d,%d", &x1, &y1) != 2 || sscanf(argv[a++], "%d,%d", &x2, &y2) != 2) return 2;
    if (strcmp(argv[a++], "--")) return 2;
    char cmd[4096] = {0};
    for (; a < argc; a++) { strncat(cmd, argv[a], sizeof cmd - strlen(cmd) - 2); strcat(cmd, " "); }

    aurwl *c = aurwl_create(1280, 800, 60000);
    if (!c) return 1;
    const char *sh[] = { "/bin/sh", "-c", cmd, NULL };
    if (aurwl_spawn(c, sh) < 0) return 1;
    uint32_t t0 = now_ms(), last_frame = 0, step_at = 0;
    int phase = 0, step = 0, saw_icon = 0;
    const int STEPS = 30;
    while (now_ms() - t0 < (uint32_t)secs * 1000u) {
        struct pollfd p = { aurwl_fd(c), POLLIN, 0 };
        poll(&p, 1, 8);
        aurwl_dispatch(c);
        aurwl_reap(c);
        uint32_t t = now_ms();
        if (t - last_frame >= 16) { aurwl_frame_done(c, t); last_frame = t; }
        aurwl_win *w = aurwl_window_count(c) ? aurwl_window_at(c, 0) : NULL;
        if (w && !aurwl_focus(c)) aurwl_set_focus(c, w);
        if (!w || (x1 == 0 && y1 == 0)) continue;
        int ox, oy; aurwl_win_content_offset(w, &ox, &oy);
        uint32_t el = t - t0;
        if (phase == 0 && el > (uint32_t)secs * 500u) {      /* press */
            aurwl_pointer_motion(c, w, ox + x1, oy + y1, t);
            aurwl_pointer_motion(c, w, ox + x1 + 1, oy + y1, t + 5);
            aurwl_pointer_button(c, 0x110, 1, t + 10);
            phase = 1; step_at = t + 200;
            printf("t=%us pressed at %d,%d\n", el / 1000, x1, y1); fflush(stdout);
        } else if (phase == 1 && t >= step_at) {             /* drag in steps */
            step++;
            int x = x1 + (x2 - x1) * step / STEPS, y = y1 + (y2 - y1) * step / STEPS;
            aurwl_pointer_motion(c, w, ox + x, oy + y, t);
            if (aurwl_drag_icon(c, NULL, NULL)) saw_icon = 1;
            step_at = t + 40;
            if (step >= STEPS) { phase = 2; step_at = t + 600; printf("t=%us at the target\n", el / 1000); fflush(stdout); }
        } else if (phase == 2 && t >= step_at) {             /* let go */
            aurwl_pointer_button(c, 0x110, 0, t);
            phase = 3;
            printf("released at %d,%d (drag icon seen: %d)\n", x2, y2, saw_icon);
            fflush(stdout);
        }
    }
    int n = aurwl_window_count(c);
    printf("windows: %d\n", n);
    if (n) {
        aurwl_win *w = aurwl_window_at(c, 0);
        surface *s = aurwl_win_content(w);
        if (s) {
            /* the view's rows are `stride` apart, not `w`: pack them */
            uint32_t *tight = malloc((size_t)s->w * s->h * 4);
            for (int y = 0; tight && y < s->h; y++)
                for (int x = 0; x < s->w; x++) tight[(size_t)y * s->w + x] = s->px[(size_t)y * s->stride + x] & 0xFFFFFFu;
            if (tight) png_write_rgb(out, tight, s->w, s->h);
            free(tight);
            printf("%s %dx%d\n", out, s->w, s->h);
        }
    }
    aurwl_destroy(c);
    return 0;
}
