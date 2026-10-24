/* wltest-xlife.c -- Xwayland's life, as the compositor runs it.
 *
 * Every other X test turns the launcher off (AUROS_NO_XWAYLAND) and builds the window manager by hand, so the
 * part that decides whether a second X program ever gets a window -- the socket aurwl holds, the lock file,
 * starting Xwayland on the first connection, collecting it, listening again -- was run by nothing: delete the
 * call that collects Xwayland and every scenario, mutant and fuzz run stayed green, while after the first X
 * program quit the second (VLC after Zoom) waited for ever at a socket nobody was listening on.
 *
 * Here the compositor runs for real with a pretend `Xwayland` first in PATH (a shell script that writes down
 * how it was started and then exits cleanly, crashes, or stays until killed, as a file says):
 *
 *   - the socket and the lock are made, the socket for this user alone (0700), the lock with our pid
 *   - a stale lock (its program dead) is taken over; a live one is not
 *   - the first connection starts Xwayland with the arguments and descriptors it needs and without a display
 *     of its own in its environment
 *   - when it ends, the next connection starts it again (a second X program after the first)
 *   - Xwayland that leaves cleanly, however soon, is never counted a failure; three that crash at start switch X
 *     off, say so, and take the socket away; it comes back by itself
 *
 *   sh tools/wltest-xlife.sh        (Linux: libwayland-dev, libxkbcommon-dev)
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../src/aurwl/aurwl.h"

static int fails, checked;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-84s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fails++;
}

static char bin[256], logd[256];
static aurwl *C;

/* The pretend Xwayland: this same program, started as `Xwayland` by a two-line script. It writes down how it was
 * started, takes every connection that is waiting on the socket it was handed (the way the real one does, or
 * the backlog fills), and then does what the mode file says: leave cleanly, crash, or stay until it is told to go. */
static int fake_xwayland(int argc, char **argv)
{
    const char *ld = getenv("LOGD");
    char path[300], buf[1024] = {0};
    int lfd = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-listenfd") && i + 1 < argc) lfd = atoi(argv[i + 1]);
        strncat(buf, argv[i], sizeof buf - strlen(buf) - 2); strcat(buf, " ");
    }
    snprintf(path, sizeof path, "%s/start.%d", ld, (int)getpid());
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "argv: %s\nWAYLAND_SOCKET=%s\nWAYLAND_DISPLAY=%s\nDISPLAY=%s\n", buf,
                getenv("WAYLAND_SOCKET") ? "set" : "unset", getenv("WAYLAND_DISPLAY") ? "set" : "unset", getenv("DISPLAY") ? "set" : "unset");
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/mode", ld);
    FILE *m = fopen(path, "r");
    char md[16] = {0};
    if (m) { if (fgets(md, sizeof md, m)) {} fclose(m); }
    if (lfd >= 0) fcntl(lfd, F_SETFL, fcntl(lfd, F_GETFL) | O_NONBLOCK);
    for (int i = 0; i < 300; i++) {                                   /* ~3 s at most; a signal ends it sooner */
        for (int k; lfd >= 0 && (k = accept(lfd, NULL, NULL)) >= 0; ) close(k);
        if (!strncmp(md, "clean", 5)) return 0;
        if (!strncmp(md, "crash", 5)) return 3;
        usleep(10000);
    }
    return 0;
}

static uint32_t now_ms(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u); }
static void pump(int ms)
{
    uint32_t end = now_ms() + (uint32_t)ms;
    do {
        struct pollfd p = { aurwl_fd(C), POLLIN, 0 };
        poll(&p, 1, 10);
        aurwl_dispatch(C);
        aurwl_reap(C);
    } while (now_ms() < end);
}
static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(text, f); fclose(f);
}
static void mode(const char *m) { char p[300]; snprintf(p, sizeof p, "%s/mode", logd); put(p, m); }
static int starts(void)
{
    int n = 0;
    DIR *d = opendir(logd);
    if (!d) return 0;
    for (struct dirent *e; (e = readdir(d)); ) if (!strncmp(e->d_name, "start.", 6)) n++;
    closedir(d);
    return n;
}
static int wait_starts(int want, int ms)
{
    for (int i = 0; i < ms / 10 && starts() < want; i++) pump(10);
    return starts() >= want;
}
static char *slurp(const char *path)
{
    static char buf[4096];
    buf[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return buf;
    size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    return buf;
}
/* What the pretend Xwayland wrote when it was started the nth time (0-based, in file-name order). */
static const char *start_log(int nth, pid_t *pid)
{
    static char path[300];
    DIR *d = opendir(logd);
    char names[16][64]; int n = 0;
    for (struct dirent *e; d && (e = readdir(d)); ) if (!strncmp(e->d_name, "start.", 6) && n < 16) snprintf(names[n++], 64, "%s", e->d_name);
    if (d) closedir(d);
    if (nth >= n) return "";
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (strcmp(names[j], names[i]) < 0) { char t[64]; memcpy(t, names[i], 64); memcpy(names[i], names[j], 64); memcpy(names[j], t, 64); }
    snprintf(path, sizeof path, "%s/%s", logd, names[nth]);
    if (pid) *pid = (pid_t)atoi(names[nth] + 6);
    return slurp(path);
}
static int connect_x(int n)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);          /* never blocks: a full backlog is a failed connection, not a hung test */
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "/tmp/.X11-unix/X%d", n);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    return fd;
}
static void clear_starts(void)
{
    DIR *d = opendir(logd);
    for (struct dirent *e; d && (e = readdir(d)); ) if (!strncmp(e->d_name, "start.", 6)) { char p[300]; snprintf(p, sizeof p, "%s/%s", logd, e->d_name); unlink(p); }
    if (d) closedir(d);
}
static int display_of(aurwl *c) { const char *d = aurwl_x_display(c); return d ? atoi(d + 1) : -1; }
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--fake-xwayland")) return fake_xwayland(argc, argv);
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(120);                                                         /* a test that hangs is a bug in the test or the compositor */
    char tmp[] = "/tmp/wltest-xlife-XXXXXX";
    if (!mkdtemp(tmp)) { perror("mkdtemp"); return 2; }
    snprintf(bin, sizeof bin, "%s/bin", tmp); snprintf(logd, sizeof logd, "%s/log", tmp);
    char run[300]; snprintf(run, sizeof run, "%s/run", tmp);
    mkdir(bin, 0700); mkdir(logd, 0700); mkdir(run, 0700);
    char script[300]; snprintf(script, sizeof script, "%s/Xwayland", bin);
    char self[300] = {0};
    if (readlink("/proc/self/exe", self, sizeof self - 1) < 0) { perror("readlink"); return 2; }
    char sh[700];
    snprintf(sh, sizeof sh, "#!/bin/sh\nexec \"%s\" --fake-xwayland \"$@\"\n", self);
    put(script, sh);
    chmod(script, 0755);
    setenv("LOGD", logd, 1);
    setenv("XDG_RUNTIME_DIR", run, 1);
    unsetenv("AUROS_NO_XWAYLAND");
    setenv("AUROS_X_RETRY_MS", "1500", 1);
    char path[1024];
    snprintf(path, sizeof path, "%s:%s", bin, getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
    setenv("PATH", path, 1);

    printf("\nthe socket and the lock\n");
    mode("hold");
    C = aurwl_create(1280, 720, 60000);
    if (!C) { printf("compositor did not start\n"); return 1; }
    int n = display_of(C);
    ok("X is on offer: a display number", n >= 0);
    char sock[64], lock[64];
    snprintf(sock, sizeof sock, "/tmp/.X11-unix/X%d", n); snprintf(lock, sizeof lock, "/tmp/.X%d-lock", n);
    struct stat st;
    ok("the socket is there and is a socket", stat(sock, &st) == 0 && S_ISSOCK(st.st_mode));
    ok("...for this user alone (mode 0700): no other user's program may read her screen's events", (st.st_mode & 0777) == 0700);
    ok("the lock names this process", atoi(slurp(lock)) == (int)getpid());
    ok("the lock cannot be written by others", stat(lock, &st) == 0 && (st.st_mode & 0222) == 0);

    printf("\nthe first X program starts Xwayland\n");
    clear_starts();
    int c1 = connect_x(n);
    ok("a program can connect to the socket", c1 >= 0);
    ok("Xwayland was started by that connection", wait_starts(1, 5000));
    pid_t xp = 0;
    const char *log = start_log(0, &xp);
    char want[64];
    snprintf(want, sizeof want, ":%d", n);
    ok("...for this display", strstr(log, want) != NULL);
    ok("...rootless, with our window manager's descriptor", strstr(log, "-rootless") && strstr(log, "-wm "));
    ok("...handed the listening socket", strstr(log, "-listenfd "));
    ok("...to end when its last program does, listening on no network", strstr(log, "-terminate") && strstr(log, "-nolisten tcp"));
    ok("...with the Wayland connection we made for it", strstr(log, "WAYLAND_SOCKET=set"));
    ok("...and no display of its own to find (it would connect to itself)", strstr(log, "WAYLAND_DISPLAY=unset") && strstr(log, "\nDISPLAY=unset"));
    pump(300);
    ok("a second connection does not start a second one", (c1 = connect_x(n)) >= 0 && (pump(300), starts() == 1));

    printf("\nthe next X program (VLC after Zoom)\n");
    mode("clean");
    if (xp > 0) kill(xp, SIGTERM);                              /* the last X program has gone: Xwayland ends */
    pump(500);
    if (c1 >= 0) close(c1);
    int c2 = connect_x(n);
    ok("the socket is listened to again after Xwayland ended", c2 >= 0 && wait_starts(2, 5000));
    pump(400);
    if (c2 >= 0) close(c2);

    printf("\nXwayland that ends cleanly is not a failure, however soon\n");
    for (int i = 0; i < 6; i++) {
        int before = starts();
        int cx = connect_x(n);
        if (cx < 0 || !wait_starts(before + 1, 5000)) break;
        pump(300);
        close(cx);
    }
    ok("six quick clean exits in a row: X is still on offer", aurwl_x_display(C) != NULL);
    ok("...and the socket is still there", exists(sock));
    {
        int before = starts();
        int cx = connect_x(n);
        ok("...and a seventh program still gets Xwayland", cx >= 0 && wait_starts(before + 1, 5000));
        pump(400);
        if (cx >= 0) close(cx);
    }

    printf("\nXwayland that crashes at start, three times\n");
    mode("crash");
    for (int i = 0; i < 3; i++) {
        int before = starts();
        int cx = connect_x(n);
        if (cx >= 0) { wait_starts(before + 1, 5000); pump(150); close(cx); }
    }
    ok("X is switched off (no program waits at a socket nobody serves)", aurwl_x_display(C) == NULL);
    ok("...the socket is gone", !exists(sock));
    ok("...and so is the lock", !exists(lock));
    mode("clean");
    for (int i = 0; i < 100 && aurwl_x_display(C) == NULL; i++) pump(20);
    ok("...and it comes back by itself", aurwl_x_display(C) != NULL);
    ok("...on the same display (programs that were told it keep working)", display_of(C) == n && exists(sock));
    {
        int before = starts();
        int cx = connect_x(n);
        ok("...and an X program gets Xwayland again", cx >= 0 && wait_starts(before + 1, 5000));
        pump(300);
        if (cx >= 0) close(cx);
    }

    printf("\nthe environment programs are started with\n");
    {
        char env[300]; snprintf(env, sizeof env, "%s/env", tmp);
        char cmd[512];
        snprintf(cmd, sizeof cmd, "echo \"$GDK_BACKEND|$QT_QPA_PLATFORM|$SDL_VIDEODRIVER|${CLUTTER_BACKEND-unset}|$DISPLAY\" > %s", env);
        const char *sh[] = { "/bin/sh", "-c", cmd, NULL };
        aurwl_spawn(C, sh);
        pump(500);
        char want[160];
        snprintf(want, sizeof want, "wayland,x11|wayland;xcb|wayland,x11|unset|:%d", n);
        ok("with X on offer a program may fall back to it (a Qt program from the archive has no Wayland plugin)", !strncmp(slurp(env), want, strlen(want)));
    }

    printf("\nthe lock\n");
    aurwl_destroy(C);
    ok("the socket and the lock are removed when the compositor ends", !exists(sock) && !exists(lock));
    pid_t gone = fork();                                                       /* a program that no longer exists: one that ended */
    if (gone == 0) _exit(0);
    waitpid(gone, NULL, 0);
    char buf[32]; snprintf(buf, sizeof buf, "%10d\n", (int)gone);
    put(lock, buf);
    C = aurwl_create(1280, 720, 60000);
    ok("a lock whose owner is gone is taken over (the same display)", display_of(C) == n);
    ok("...and now names this process", atoi(slurp(lock)) == (int)getpid());
    aurwl_destroy(C);
    snprintf(buf, sizeof buf, "%10d\n", 1);                                    /* init: alive */
    put(lock, buf);
    C = aurwl_create(1280, 720, 60000);
    ok("a lock whose owner is alive is left alone (another display)", display_of(C) >= 0 && display_of(C) != n);
    ok("...and its file is untouched", atoi(slurp(lock)) == 1);
    aurwl_destroy(C);
    unlink(lock);

    char cmd[512]; snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
    if (system(cmd)) {}
    printf("\n%d checks, %d failed\n", checked, fails);
    return fails ? 1 : 0;
}
