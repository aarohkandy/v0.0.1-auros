/* sessionenvtest.c — does the portal find the screen?
 *
 * A Flathub program asks for a file through xdg-desktop-portal, which
 * the user's systemd starts on the session bus. It inherits nothing
 * from aurshell; it knows where the screen is only if somebody told
 * the bus and systemd (dbus-update-activation-environment --systemd).
 * Nobody did, so "Open..." in a Flathub program opened nothing.
 *
 * run_publish_env() is that somebody. This puts a stand-in for
 * dbus-update-activation-environment first on PATH, which writes down
 * what it was told, and checks:
 *   - nothing is published while the shell is still on its private
 *     boot-time bus (the portal is not on that one);
 *   - once on logind's bus, exactly the things the portal needs,
 *     with --systemd, are published -- including the Flathub data dirs
 *     and Xwayland's DISPLAY;
 *   - and only once;
 *   - and, with a real dbus-daemon: a program the BUS starts (what the
 *     store's Open button and a portal do) finds DISPLAY and
 *     WAYLAND_DISPLAY in its environment. (sessionenvtest --real, run by
 *     this test when dbus-daemon is installed.)
 *
 *   cc -O2 -std=gnu11 -o /tmp/sessionenvtest tools/sessionenvtest.c \
 *      src/aurshell/run.c -I src/aurshell && /tmp/sessionenvtest
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
#include "run.h"

static int fails, checks;
static void ok(int c, const char *w) { checks++; if (!c) fails++; printf("  %-62s %s\n", w, c ? "ok" : "FAIL"); }

static char said[4096];
static int  read_said(const char *path)
{
    said[0] = 0;
    for (int i = 0; i < 200; i++) {               /* up to 2 s for the child */
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(said, 1, sizeof said - 1, f);
            said[n] = 0; fclose(f);
            if (n) return 1;
        }
        usleep(10000);
    }
    return 0;
}

static int standin(const char *xdisp)
{
    char root[64], bin[128], rt[128], rec[160], fake[200], sock[200];
    snprintf(root, sizeof root, "/tmp/sessionenvtest.%d%s", (int)getpid(), xdisp ? "" : "n");
    snprintf(bin, sizeof bin, "%s/bin", root);
    snprintf(rt, sizeof rt, "%s/run", root);
    snprintf(rec, sizeof rec, "%s/said.txt", root);
    if (mkdir(root, 0700) || mkdir(bin, 0700) || mkdir(rt, 0700)) { perror(root); return 2; }

    snprintf(fake, sizeof fake, "%s/dbus-update-activation-environment", bin);
    FILE *f = fopen(fake, "w");
    fprintf(f, "#!/bin/sh\nfor a in \"$@\"; do printf '%%s\\n' \"$a\"; done >> '%s'\n", rec);
    fclose(f);
    chmod(fake, 0755);

    char path[400];
    snprintf(path, sizeof path, "%s:/usr/bin:/bin", bin);
    setenv("PATH", path, 1);
    setenv("XDG_RUNTIME_DIR", rt, 1);
    setenv("XDG_DATA_DIRS",
           "/home/her/.local/share/flatpak/exports/share:/var/lib/flatpak/exports/share:/usr/share", 1);
    /* The private bus the unit's dbus-run-session made at boot. */
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/tmp/dbus-private-boot", 1);

    printf("does the portal find the screen?%s\n", xdisp ? "" : " (no Xwayland)");
    ok(run_publish_env("wayland-1", xdisp) == 0, "on the boot-time bus: nothing is published");
    usleep(200000);
    ok(access(rec, F_OK) != 0, "...and the helper was not started");

    /* logind makes /run/user/<uid>/bus; the shell adopts it. */
    snprintf(sock, sizeof sock, "%s/bus", rt);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%.100s", sock);
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); return 2; }
    ok(run_adopt_user_bus() == 1, "the shell moves to logind's bus");
    ok(run_publish_env("wayland-1", xdisp) == 1, "...and then publishes where the desktop is");
    ok(read_said(rec), "the helper ran");
    ok(strstr(said, "--systemd\n") != NULL, "with --systemd, so the user's systemd hears it too");
    ok(strstr(said, "WAYLAND_DISPLAY=wayland-1\n") != NULL, "WAYLAND_DISPLAY is the compositor's socket");
    if (xdisp) ok(strstr(said, "\nDISPLAY=:7\n") != NULL, "DISPLAY is Xwayland's, beside WAYLAND_DISPLAY (X programs the bus starts)");
    else       ok(strstr(said, "DISPLAY=:") == NULL && strstr(said, "\nDISPLAY=") == NULL, "with no Xwayland: no DISPLAY is published (a wrong one would be worse)");
    ok(strstr(said, "XDG_CURRENT_DESKTOP=AurOS\n") != NULL, "XDG_CURRENT_DESKTOP=AurOS (auros-portals.conf)");
    ok(strstr(said, "XDG_SESSION_TYPE=wayland\n") != NULL, "XDG_SESSION_TYPE=wayland");
    ok(strstr(said, "XDG_DATA_DIRS=/home/her/.local/share/flatpak/exports/share:"
                    "/var/lib/flatpak/exports/share:/usr/share\n") != NULL,
       "XDG_DATA_DIRS with Flathub's directories in it");
    for (int i = 0; i < 20; i++) { run_reap(); usleep(10000); }
    ok(run_publish_env("wayland-1", xdisp) == 0, "and only once");

    close(s);
    char rm[160]; snprintf(rm, sizeof rm, "rm -rf '%s'", root);
    if (system(rm) != 0) fprintf(stderr, "could not remove %s\n", root);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}

/* The same, on a real bus: a program the BUS starts (the store's Open
 * button, whatever a portal launches) is started by dbus-daemon with the
 * activation environment, not the shell's. A throwaway session bus, a
 * service that writes down the DISPLAY and WAYLAND_DISPLAY it was started
 * with, the real dbus-update-activation-environment, and a method call
 * that activates it. Exit 77: this machine cannot run it (not a failure). */
static int real(void)
{
    char root[80], rt[120], conf[140], svc[180], probe[140], out[140], cmd[600];
    snprintf(root, sizeof root, "/tmp/sessionenvtest-real.%d", (int)getpid());
    snprintf(rt, sizeof rt, "%s/run", root);
    snprintf(conf, sizeof conf, "%s/bus.conf", root);
    snprintf(probe, sizeof probe, "%s/probe.sh", root);
    snprintf(out, sizeof out, "%s/probed.txt", root);
    if (mkdir(root, 0700) || mkdir(rt, 0700)) { perror(root); return 2; }
    snprintf(svc, sizeof svc, "%s/services", root);
    mkdir(svc, 0700);
    snprintf(svc, sizeof svc, "%s/services/org.auros.EnvProbe.service", root);
    FILE *f = fopen(conf, "w");
    fprintf(f, "<busconfig><type>session</type><auth>EXTERNAL</auth><listen>unix:path=%s/bus</listen>"
               "<servicedir>%s/services</servicedir><policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
               "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n", rt, root);
    fclose(f);
    f = fopen(svc, "w");
    fprintf(f, "[D-BUS Service]\nName=org.auros.EnvProbe\nExec=/bin/sh %s\n", probe);
    fclose(f);
    f = fopen(probe, "w");
    fprintf(f, "#!/bin/sh\nprintf 'DISPLAY=%%s\\nWAYLAND_DISPLAY=%%s\\n' \"$DISPLAY\" \"$WAYLAND_DISPLAY\" > '%s.tmp' && mv '%s.tmp' '%s'\nsleep 2\n",
            out, out, out);
    fclose(f);

    unsetenv("DISPLAY"); unsetenv("WAYLAND_DISPLAY");
    pid_t d = fork();
    if (d == 0) {
        if (!freopen("/dev/null", "w", stderr)) _exit(127);
        execlp("dbus-daemon", "dbus-daemon", "--nofork", "--config-file", conf, (char *)NULL);
        _exit(127);
    }
    char sock[140]; snprintf(sock, sizeof sock, "%s/bus", rt);
    int up = 0;
    for (int i = 0; i < 100 && !up; i++) { up = access(sock, F_OK) == 0; if (!up) usleep(30000); }
    int rc = 0;
    if (!up) { printf("  (dbus-daemon would not start here)\n"); rc = 77; goto done; }
    if (system("command -v dbus-update-activation-environment >/dev/null 2>&1") != 0) {
        printf("  (no dbus-update-activation-environment here)\n"); rc = 77; goto done;
    }

    snprintf(cmd, sizeof cmd, "unix:path=%s", sock);
    setenv("XDG_RUNTIME_DIR", rt, 1);
    setenv("DBUS_SESSION_BUS_ADDRESS", cmd, 1);
    printf("does a program the bus starts find the screens?\n");
    ok(run_publish_env("wayland-1", ":7") == 1, "the shell publishes on a real session bus");
    sleep(1);                      /* the helper's first act is the activation environment */
    snprintf(cmd, sizeof cmd,
             "timeout 8 dbus-send --session --dest=org.auros.EnvProbe /org/auros/EnvProbe "
             "org.auros.EnvProbe.Ping >/dev/null 2>&1 &");
    if (system(cmd) != 0) { perror("dbus-send"); rc = 2; goto done; }
    ok(read_said(out), "the bus started the program");
    ok(strstr(said, "DISPLAY=:7\n") && !strstr(said, "DISPLAY=\n"), "...and it found Xwayland (DISPLAY)");
    ok(strstr(said, "WAYLAND_DISPLAY=wayland-1\n") != NULL, "...and the desktop (WAYLAND_DISPLAY)");
    printf("\n%d checks, %d failed\n", checks, fails);
    rc = fails ? 1 : 0;
done:
    if (d > 0) { kill(d, SIGTERM); waitpid(d, NULL, 0); }
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", root);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--real")) return real();
    if (argc > 1 && !strcmp(argv[1], "--nox"))  return standin(NULL);
    int rc = standin(":7");
    fflush(stdout);
    /* Each of the next two is a fresh process: run_publish_env speaks once. */
    char self[300];
    snprintf(self, sizeof self, "%s --nox", argv[0]);
    if (system(self) != 0) rc = 1;
    fflush(stdout);
    if (system("command -v dbus-daemon >/dev/null 2>&1") == 0) {
        snprintf(self, sizeof self, "%s --real", argv[0]);
        int st = system(self);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 77) printf("\n(the real-bus part could not run on this machine)\n");
        else if (st != 0) rc = 1;
    } else printf("\n(no dbus-daemon here: the real-bus part is left to CI)\n");
    return rc;
}
