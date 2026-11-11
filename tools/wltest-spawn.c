/* wltest-spawn.c -- what a program the shell starts is given, and what the shell can tell about it.
 *
 *   cwd      The shell is a system service with no working directory of its own, so every program it
 *            started began in `/`: "Your files" showed bin, boot, dev, etc, and a Save dialog opened
 *            there. aurwl_spawn() now starts the program in her home folder ($HOME).
 *   proc     session_proc() answers for a program the way the "Starting..." card needs: still running
 *            (it, or something it started in its own group), gone and well (a launcher that handed the
 *            job on: Firefox, soffice, flatpak run), or gone and failed. A launcher that exits 0 and
 *            leaves a child behind is still running. Only the last of those may be told "did not start".
 *
 * The real compositor, the real fork and the real kill(); no window is needed.
 *
 *   sh tools/wltest-spawn.sh     (Linux: libwayland-dev, libxkbcommon-dev, libdbus-1-dev)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/aurwl/aurwl.h"
#include "../src/aurshell/shell.h"
#include "../src/aurshell/session.h"

static int fails, checks;
static void check(const char *what, int ok, const char *got)
{
    checks++;
    if (!ok) { fails++; printf("  FAIL %s (%s)\n", what, got); }
    else printf("  ok   %s\n", what);
}

static aurwl *C;
static shell_ctx SC;

static void nap(int ms) { struct timespec t = { 0, ms * 1000000L }; nanosleep(&t, NULL); }

/* Run `sh -c script` through the shell's own spawn hook; the pid. */
static int start(const char *script)
{
    char blob[256] = {0};
    size_t n = 0;
    const char *argv[] = { "/bin/sh", "-c", script };
    for (int i = 0; i < 3; i++) { size_t l = strlen(argv[i]) + 1; memcpy(blob + n, argv[i], l); n += l; }
    return session_spawn(&SC, blob, 3);
}

/* The state once it has settled (a program that is running stays running). */
static int state_of(int pid, int want)
{
    int st = PROC_UNKNOWN;
    for (int i = 0; i < 300; i++) {
        aurwl_reap(C);
        st = session_proc(&SC, pid);
        if (st == want) return st;
        nap(10);
    }
    return st;
}

static const char *name(int st)
{ return st == PROC_RUNNING ? "running" : st == PROC_HANDED_OFF ? "handed off" : st == PROC_FAILED ? "failed" : "unknown"; }

int main(void)
{
    char dir[PATH_MAX], real[PATH_MAX], out[PATH_MAX + 16], cmd[2 * PATH_MAX + 64], got[PATH_MAX] = "";
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(dir, sizeof dir, "%s/wltest-spawn-home.%d", tmp, (int)getpid());
    if (mkdir(dir, 0700) < 0) { perror(dir); return 2; }
    if (!realpath(dir, real)) { perror("realpath"); return 2; }

    C = aurwl_create(1280, 720, 60000);
    if (!C) { printf("wltest-spawn: compositor did not start\n"); return 1; }
    SC.wl = C;

    /* ── the folder a program starts in ── */
    if (chdir("/") != 0) return 2;                 /* the shell's own: a service's, `/` */
    setenv("HOME", real, 1);
    snprintf(out, sizeof out, "%s/cwd", real);
    snprintf(cmd, sizeof cmd, "pwd -P > '%s'", out);
    int pid = start(cmd);
    check("a program is started", pid > 0, "no pid");
    for (int i = 0; i < 300 && access(out, F_OK) != 0; i++) { aurwl_reap(C); nap(10); }
    nap(50);
    FILE *f = fopen(out, "r");
    if (f) { if (fgets(got, sizeof got, f)) got[strcspn(got, "\n")] = 0; fclose(f); }
    check("it starts in her home folder, not in /", !strcmp(got, real), got);
    char here[PATH_MAX] = "";
    if (getcwd(here, sizeof here)) {}
    check("...and the shell itself stays where it was", !strcmp(here, "/"), here);

    /* ── what became of it ── */
    pid = start("exit 3");
    int st = state_of(pid, PROC_FAILED);
    check("it exits 3 with no window: failed", st == PROC_FAILED, name(st));

    pid = start("exit 0");
    st = state_of(pid, PROC_HANDED_OFF);
    check("it exits 0 at once (a launcher that handed off): not a failure", st == PROC_HANDED_OFF, name(st));

    pid = start("sleep 30");
    nap(100);
    st = state_of(pid, PROC_RUNNING);
    check("it is running: running", st == PROC_RUNNING, name(st));
    kill(-pid, SIGKILL);
    st = state_of(pid, PROC_FAILED);
    check("it is killed: failed", st == PROC_FAILED, name(st));

    pid = start("sleep 30 & exit 0");
    for (int i = 0; i < 50; i++) { aurwl_reap(C); nap(10); }      /* the wrapper is long gone */
    st = session_proc(&SC, pid);
    check("the wrapper exits 0 and leaves its child running: still running", st == PROC_RUNNING, name(st));
    int code = aurwl_kid_exit(C, pid);
    check("...and the wrapper's own exit is on record as 0", code == 0, code < 0 ? "not recorded" : "other");
    kill(-pid, SIGKILL);
    st = state_of(pid, PROC_HANDED_OFF);
    check("the child goes too: handed off, not failed", st == PROC_HANDED_OFF, name(st));

    aurwl_destroy(C);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd)) {}
    printf("wltest-spawn: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
