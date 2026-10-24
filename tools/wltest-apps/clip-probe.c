/* clip-probe.c -- does the clipboard cross between X programs and Wayland programs?
 *
 *   clip-probe OUTDIR [IMAGE.png]
 *
 * Runs on the real compositor, with real Xwayland and the real programs
 * xterm (an X window to hold the keyboard), xclip (an X client) and
 * wl-copy / wl-paste (Wayland clients). Like the shell, it gives the keyboard
 * to the window that has just appeared. Two crossings:
 *   A. a Wayland program copies "from-wayland"; the X window has the
 *      keyboard; xclip -o (X) reads it            -> OUTDIR/x-reads.txt
 *   B. an X program copies "from-x"; a Wayland window has the keyboard;
 *      wl-paste (Wayland) reads it                -> OUTDIR/wl-reads.txt
 * With an IMAGE the same two crossings are made with it (image/png):
 *   A'. wl-copy --type image/png, read by xclip -t image/png  -> OUTDIR/x-image.png
 *   B'. xclip -t image/png copies, wl-paste -t image/png reads -> OUTDIR/wl-image.png
 * Prints what it did; the files say what arrived.
 *
 * SINCE THE CLIPBOARD FOLLOWS THE KEYBOARD (src/aurwl/xclip.inc, review W01/W05/W09/W19) nothing crosses:
 * x-reads has no "from-wayland" (xclip says "target STRING not available"), wl-reads has no "from-x" (it is
 * the Wayland program's own copy), and the pictures do not arrive. xclip is an X client with no window, so it is never the X program the keyboard is in, and it
 * is refused in both directions (it cannot read the Wayland copy, and its copy does not reach the Wayland
 * program). This is the real-program proof of the refusal. The crossing itself, for an X program that has a
 * window with the keyboard (VLC to Firefox and back), is proven on the wire by tools/wltest-x.sh.
 */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/aurwl/aurwl.h"

static aurwl *C;
static uint32_t t0;
static uint32_t now_ms(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u); }

static void tick(int ms)               /* run the compositor like the shell does, for ms */
{
    uint32_t end = now_ms() + (uint32_t)ms, last = 0;
    while (now_ms() < end) {
        struct pollfd p = { aurwl_fd(C), POLLIN, 0 };
        poll(&p, 1, 8);
        aurwl_dispatch(C); aurwl_reap(C);
        uint32_t t = now_ms();
        if (t - last >= 16) { aurwl_frame_done(C, t); last = t; }
    }
}

static void run(const char *cmd) { const char *sh[] = { "/bin/sh", "-c", cmd, NULL }; if (aurwl_spawn(C, sh) < 0) printf("could not start: %s\n", cmd); }

#define MAXW 16
static uint32_t seen[MAXW]; static int n_seen;
static int is_seen(uint32_t id) { for (int i = 0; i < n_seen; i++) if (seen[i] == id) return 1; return 0; }

/* The window that has appeared since the last call, if any. */
static aurwl_win *fresh(void)
{
    for (int i = 0; i < aurwl_window_count(C); i++) {
        aurwl_win *w = aurwl_window_at(C, i);
        if (!aurwl_win_is_popup(w) && !is_seen(aurwl_win_id(w))) { if (n_seen < MAXW) seen[n_seen++] = aurwl_win_id(w); return w; }
    }
    return NULL;
}
static aurwl_win *wait_fresh(int ms)
{
    uint32_t end = now_ms() + (uint32_t)ms;
    while (now_ms() < end) { tick(100); aurwl_win *w = fresh(); if (w) return w; }
    return NULL;
}
static void list(const char *when)
{
    printf("[%s] windows:", when);
    for (int i = 0; i < aurwl_window_count(C); i++) { aurwl_win *w = aurwl_window_at(C, i); printf(" \"%s\"/%s%s", aurwl_win_title(w), aurwl_win_app_id(w), w == aurwl_focus(C) ? "*" : ""); }
    printf("\n"); fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/out";
    C = aurwl_create(1280, 800, 60000);
    if (!C) return 1;
    t0 = now_ms();
    char cmd[512];

    run("xterm -fa Monospace -T holder -e sleep 300");
    aurwl_win *xt = wait_fresh(20000);
    printf("xterm window: %s\n", xt ? "up" : "NEVER");
    if (!xt) return 2;
    tick(1500);

    /* A: a Wayland program copies; the X window has the keyboard. */
    run("printf from-wayland | wl-copy");
    aurwl_win *wl = wait_fresh(8000);
    printf("wl-copy window: %s\n", wl ? "up" : "none (it may need none)");
    if (wl) aurwl_set_focus(C, wl);
    tick(1500);                                  /* it asks to set the selection once it has the keyboard */
    list("after wl-copy");
    aurwl_set_focus(C, xt);
    tick(1500);
    list("X holds the keyboard");
    snprintf(cmd, sizeof cmd, "xclip -o -selection clipboard > %s/x-reads.txt 2>&1", out);
    run(cmd);
    tick(2500);

    /* B: an X program copies; a Wayland window has the keyboard. */
    run("printf from-x | xclip -i -selection clipboard");
    tick(1500);
    list("after xclip -i");
    snprintf(cmd, sizeof cmd, "wl-paste -n > %s/wl-reads.txt 2>&1", out);
    run(cmd);
    aurwl_win *wp = wait_fresh(8000);
    printf("wl-paste window: %s\n", wp ? "up" : "none");
    if (wp) aurwl_set_focus(C, wp);
    tick(3000);
    list("after the text");

    const char *img = argc > 2 ? argv[2] : NULL;
    if (img) {
        snprintf(cmd, sizeof cmd, "wl-copy --type image/png < %s", img);
        run(cmd);
        aurwl_win *wi = wait_fresh(8000);
        if (wi) aurwl_set_focus(C, wi);
        tick(2000);
        aurwl_set_focus(C, xt);
        tick(1500);
        snprintf(cmd, sizeof cmd, "xclip -o -selection clipboard -t image/png > %s/x-image.png 2>%s/x-image.err", out, out);
        run(cmd);
        tick(5000);
        snprintf(cmd, sizeof cmd, "xclip -i -selection clipboard -t image/png < %s", img);
        run(cmd);
        tick(2500);
        snprintf(cmd, sizeof cmd, "wl-paste -t image/png > %s/wl-image.png 2>%s/wl-image.err", out, out);
        run(cmd);
        aurwl_win *wq = wait_fresh(8000);
        if (wq) aurwl_set_focus(C, wq);
        tick(6000);
        list("after the image");
    }
    aurwl_destroy(C);
    return 0;
}
