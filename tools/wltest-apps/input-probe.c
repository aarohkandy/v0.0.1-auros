/* input-probe.c -- does input reach a real program (an X program, say)?
 *
 *   input-probe SECONDS KEYS -- command...
 *
 * Runs the command on the real compositor. When its window is up it gives
 * the window the keyboard, moves the pointer into it, clicks, and types KEYS
 * (letters, digits and space; '\n' is Enter) with real evdev codes through
 * aurwl_key(), as the shell does. Prints the window list (title, app id,
 * size, popup or not) once a second so the run shows what became of it.
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

static int evdev(char c)
{
    static const char *row1 = "qwertyuiop", *row2 = "asdfghjkl", *row3 = "zxcvbnm";
    const char *p;
    if ((p = strchr(row1, c)) && c) return 16 + (int)(p - row1);
    if ((p = strchr(row2, c)) && c) return 30 + (int)(p - row2);
    if ((p = strchr(row3, c)) && c) return 44 + (int)(p - row3);
    if (c >= '1' && c <= '9') return 2 + (c - '1');
    if (c == '0') return 11;
    if (c == ' ') return 57;
    if (c == '\n') return 28;
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 5 || strcmp(argv[3], "--")) { fprintf(stderr, "usage: input-probe SECONDS KEYS -- cmd...\n"); return 2; }
    int secs = atoi(argv[1]);
    const char *keys = argv[2];
    char cmd[4096] = {0};
    for (int i = 4; i < argc; i++) { strncat(cmd, argv[i], sizeof cmd - strlen(cmd) - 2); strcat(cmd, " "); }
    aurwl *c = aurwl_create(1280, 800, 60000);
    if (!c) return 1;
    const char *sh[] = { "/bin/sh", "-c", cmd, NULL };
    if (aurwl_spawn(c, sh) < 0) return 1;
    uint32_t t0 = now_ms(), last = 0, last_frame = 0, typed_at = 0;
    int phase = 0; size_t ki = 0;
    while (now_ms() - t0 < (uint32_t)secs * 1000u) {
        struct pollfd p = { aurwl_fd(c), POLLIN, 0 };
        poll(&p, 1, 8);
        aurwl_dispatch(c); aurwl_reap(c);
        uint32_t t = now_ms();
        if (t - last_frame >= 16) { aurwl_frame_done(c, t); last_frame = t; }
        int n = aurwl_window_count(c);
        aurwl_win *w = NULL;
        for (int i = 0; i < n; i++) { aurwl_win *x = aurwl_window_at(c, i); if (!aurwl_win_is_popup(x)) { w = x; break; } }
        if (w && phase == 0 && t - t0 > (uint32_t)secs * 400u) {        /* the window has had time: click in it */
            int ox, oy; aurwl_win_content_offset(w, &ox, &oy);
            aurwl_set_focus(c, w);
            aurwl_pointer_motion(c, w, ox + 30, oy + 30, t);
            aurwl_pointer_button(c, 0x110, 1, t + 5);
            aurwl_pointer_button(c, 0x110, 0, t + 10);
            phase = 1; typed_at = t + 300;
        } else if (phase == 1 && t >= typed_at) {
            if (keys[ki]) {
                int code;
                if (keys[ki] == '\\' && keys[ki + 1] == 'n') { code = 28; ki++; }       /* a typed \n is Enter */
                else code = evdev(keys[ki]);
                if (code > 0) { aurwl_key(c, (uint32_t)code, 1, t); aurwl_key(c, (uint32_t)code, 0, t + 10); }
                ki++; typed_at = t + 60;
            } else phase = 2;
        }
        if (t - last >= 1000) {
            last = t;
            printf("t=%2us windows=%d", (t - t0) / 1000, n);
            for (int i = 0; i < n; i++) {
                aurwl_win *x = aurwl_window_at(c, i);
                surface *s = aurwl_win_content(x);
                printf(" [\"%s\" %s %dx%d%s]", aurwl_win_title(x), aurwl_win_app_id(x), s ? s->w : 0, s ? s->h : 0,
                       aurwl_win_is_popup(x) ? " popup" : "");
            }
            printf(" phase=%d\n", phase); fflush(stdout);
        }
    }
    aurwl_destroy(c);
    return 0;
}
