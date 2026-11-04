/* screenkmstest.c -- does the staging screen get the display back when it is taken away?
 *
 * aurstage loads the image's own drivers part way through an install, and a graphics driver among
 * them can replace the screen the firmware gave aurscreen: from then on every flip fails. The
 * screen used to ignore that and paint into nothing, so what she saw for the rest of the install
 * was the console's log lines (real Windows 11 run, 2026-11-04). It must close the dead display and
 * open the new one.
 *
 * This is src/aurscreen/screen.c with its main renamed, run in a child with a fake display (this
 * file stands in for src/aurshell/kms.c): the first flip fails, as it does when the device is gone;
 * the open after it succeeds, as it does when the new driver's device has appeared. The child runs
 * as the install does, reading a pipe that stays open, and is told to stop after a few seconds.
 * Prints "opens=N closes=N flips=N" and exits 0 only if the display was opened again.
 *
 *   gcc -std=gnu11 -Dmain=aurscreen_main -c -o screen.o src/aurscreen/screen.c
 *   gcc -std=gnu11 -o t tools/screenkmstest.c screen.o src/aurshell/draw.c src/common/font.c \
 *       src/common/png.c -lm && ./t
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/aurshell/kms.h"

static int opens, closes, flips;
static uint32_t px[1280 * 720];
static kms_display fake;

kms_display *kms_open(const char *card) { (void)card; opens++; return &fake; }
void kms_close(kms_display *d) { (void)d; closes++; }
surface *kms_back_surface(kms_display *d)
{
    static surface s;
    (void)d;
    s.px = px; s.w = 1280; s.h = 720; s.stride = 1280;
    return &s;
}
/* The first flip fails, as it does on a device that has gone; the rest work. */
int kms_flip(kms_display *d) { (void)d; return ++flips == 1 ? -1 : 0; }

int aurscreen_main(int argc, char **argv);

int main(void)
{
    int in[2];
    if (pipe(in) != 0) return 2;
    pid_t pid = fork();
    if (pid < 0) return 2;
    if (pid == 0) {
        dup2(in[0], 0);
        close(in[1]);
        char *argv[] = { "aurscreen", "--mode", "install", NULL };
        aurscreen_main(3, argv);
        printf("opens=%d closes=%d flips=%d\n", opens, closes, flips);
        fflush(stdout);
        _exit(opens >= 2 ? 0 : 1);
    }
    close(in[0]);
    sleep(4);
    kill(pid, SIGTERM);
    int st = 0;
    waitpid(pid, &st, 0);
    close(in[1]);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 2;
}
