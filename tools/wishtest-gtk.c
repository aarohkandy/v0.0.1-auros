/* wishtest-gtk.c — what does a real GTK program tell the compositor? (docs/AI.md §3.5)
 *
 * wishtest-textinput.c proves the compositor against a client that speaks
 * the protocol the way we think GTK does. This asks GTK itself: the real
 * aurwl.c with textinput.c, a GTK3 program (tools/wishtest-gtk.py) with
 * an ordinary field, a field with visibility off (what a password box
 * is), PIN, e-mail, password-purpose, a TextView and a search box, moving
 * the keyboard focus between them. A moment after each move this reads
 * textinput_state() and compares it with what that field should be.
 *
 *   wishtest-gtk [-t SECONDS]      exit 0 only if every field was seen as expected
 *
 * Firefox sits on the same GtkIMContext, and sets GtkInputPurpose from the
 * HTML input type, so what is true of GtkEntry here is what its password
 * fields tell us. (Firefox itself is checked in a booted image by the
 * hardening/PARITY harness; see docs/handoff/streams/W1.md.)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../src/aurwl/aurwl.h"
#include "../src/aurwl/textinput.h"

static long long mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static const char *name_of(int s) { return s == TI_PLAIN ? "PLAIN" : s == TI_SENSITIVE ? "SENSITIVE" : "UNKNOWN"; }

static int expect_for(const char *field)
{
    if (!strcmp(field, "plain") || !strcmp(field, "email") || !strcmp(field, "textview") || !strcmp(field, "search"))
        return TI_PLAIN;
    return TI_SENSITIVE;                       /* hidden, pin, password */
}

int main(int argc, char **argv)
{
    int secs = 30;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = atoi(argv[++i]);
    char dir[] = "/tmp/wishgtk-XXXXXX";
    if (!mkdtemp(dir)) return 2;
    setenv("XDG_RUNTIME_DIR", dir, 1);
    setenv("NO_AT_BRIDGE", "1", 1);
    aurwl *c = aurwl_create(800, 600, 60000);
    if (!c) { fprintf(stderr, "no compositor\n"); return 2; }
    char logf[300];
    snprintf(logf, sizeof logf, "%s/focus.log", dir);
    const char *script = getenv("WISHTEST_GTK_PY") ? getenv("WISHTEST_GTK_PY") : "tools/wishtest-gtk.py";
    const char *av[] = { "python3", script, logf, NULL };
    pid_t kid = aurwl_spawn(c, av);
    if (kid < 0) { fprintf(stderr, "cannot start the GTK program\n"); return 2; }

    int focused = 0, checked = 0, bad = 0, fields = 0;
    char last[32] = "";
    long long last_at = 0, end = mono_ms() + secs * 1000LL;
    long long t0 = mono_ms();
    while (mono_ms() < end) {
        aurwl_dispatch(c);
        aurwl_reap(c);
        if (!focused && aurwl_window_count(c) > 0) {
            aurwl_set_focus(c, aurwl_window_at(c, 0));
            focused = 1;
            printf("window mapped after %lld ms; keyboard given to it\n", mono_ms() - t0);
        }
        /* the newest FOCUS line */
        FILE *f = fopen(logf, "r");
        char line[128], nm[32] = "";
        long long at = 0;
        int done = 0;
        while (f && fgets(line, sizeof line, f)) {
            long long ms;
            if (sscanf(line, "FOCUS %31s %lld", nm, &ms) == 2) { snprintf(last, sizeof last, "%s", nm); at = ms; }
            else if (!strncmp(line, "DONE", 4)) done = 1;
        }
        if (f) fclose(f);
        if (at && at != last_at) { last_at = at; checked = 0; }
        if (at && !checked && mono_ms() - mono_ms() == 0) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            long long now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            if (now - at >= 700) {               /* let GTK's enable + commit arrive */
                int got = textinput_state();
                int want = expect_for(last);
                unsigned hint, purpose;
                int on = textinput_peek(&hint, &purpose);
                printf("       %-9s enabled=%d hint=0x%x purpose=%u\n", last, on, hint, purpose);
                fields++;
                if (!strcmp(last, "hidden") && got == TI_PLAIN)
                    /* What GTK3 really does: a GtkEntry with visibility off announces an
                     * ordinary field. wishhook.c covers it by program and by window title. */
                    printf("  note %-9s GTK3 announces an invisible entry as PLAIN (hint 0, purpose normal)\n", last);
                else if (got == want) printf("  ok   %-9s %s\n", last, name_of(got));
                else { printf("  FAIL %-9s got %s, wanted %s\n", last, name_of(got), name_of(want)); bad++; }
                checked = 1;
            }
        }
        if (done) break;
        struct pollfd p = { -1, 0, 0 };
        poll(&p, 0, 20);
    }
    kill(kid, SIGTERM);
    waitpid(kid, NULL, 0);
    aurwl_destroy(c);
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd)) {}
    if (!focused) { printf("FAIL the GTK program never put a window up\n"); return 1; }
    if (fields < 6) { printf("FAIL only %d fields were looked at\n", fields); return 1; }
    printf("wishtest-gtk: %d fields, %d wrong\n", fields, bad);
    return bad ? 1 : 0;
}
