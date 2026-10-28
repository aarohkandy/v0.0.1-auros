/* wltest-dialog.c -- a dialog opens in front of the program that asked it.
 *
 * F's real-Windows run (docs/handoff/streams/F.md, finding 9): VLC's first-run question, "Privacy and
 * Network Access", opened as a card half off the left edge BEHIND VLC's own window, its Continue
 * button at the edge, so she could not read it or answer it. The shell brought forward whichever
 * window it saw last, and a program that maps its dialog before (or beside) its main window gets the
 * main window last.
 *
 * Now the compositor says which window a toplevel is a dialog of (xdg_toplevel.set_parent, X11
 * WM_TRANSIENT_FOR: aurwl_win_transient_for), and session_sync brings the dialog forward, over the
 * window it belongs to, in whichever order the two map -- and goes back to that window when the
 * dialog is answered. This runs the real compositor, a real client, the real session_sync and the
 * real `present` of the rail and the taskbar, for each of three orders:
 *
 *   parent-first    the program maps, then asks its question
 *   dialog-first    the question maps, then the program (the order that buried it)
 *   answered        the question is closed: back to the program
 *
 *   sh tools/wltest-dialog.sh        (Linux: libwayland-dev, libxkbcommon-dev, libdbus-1-dev)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#include <wayland-client.h>
#include "xdg-shell-client.h"

#include "../src/aurwl/aurwl.h"
#include "../src/aurshell/shell.h"
#include "../src/aurshell/session.h"

static struct { struct wl_compositor *comp; struct wl_shm *shm; struct xdg_wm_base *wm; } G;

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    (void)d;
    if (!strcmp(iface, "wl_compositor")) G.comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
    else if (!strcmp(iface, "wl_shm")) G.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "xdg_wm_base")) G.wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 2);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };
static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };

typedef struct { struct wl_surface *surf; struct xdg_surface *xs; struct xdg_toplevel *tl; int configured; } win;

static void xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{ xdg_surface_ack_configure(xs, serial); ((win *)d)->configured = 1; }
static const struct xdg_surface_listener xs_l = { xs_configure };
static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s)
{ (void)d; (void)t; (void)w; (void)h; (void)s; }
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; }
static void tl_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h) { (void)d; (void)t; (void)w; (void)h; }
static void tl_caps(void *d, struct xdg_toplevel *t, struct wl_array *a) { (void)d; (void)t; (void)a; }
static const struct xdg_toplevel_listener tl_l = { tl_configure, tl_close, tl_bounds, tl_caps };

static void make(win *w, const char *title, struct xdg_toplevel *parent)
{
    memset(w, 0, sizeof *w);
    w->surf = wl_compositor_create_surface(G.comp);
    w->xs = xdg_wm_base_get_xdg_surface(G.wm, w->surf);
    xdg_surface_add_listener(w->xs, &xs_l, w);
    w->tl = xdg_surface_get_toplevel(w->xs);
    xdg_toplevel_add_listener(w->tl, &tl_l, w);
    xdg_toplevel_set_title(w->tl, title);
    if (parent) xdg_toplevel_set_parent(w->tl, parent);
    wl_surface_commit(w->surf);
}
static void map(struct wl_display *dpy, win *w)
{
    for (int i = 0; i < 20 && !w->configured; i++) wl_display_roundtrip(dpy);
    int px = 64;
    size_t n = (size_t)px * px * 4;
    int fd = memfd_create("dialog", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)n) < 0) { perror("memfd"); exit(98); }
    uint32_t *m = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int k = 0; k < px * px; k++) m[k] = 0xFF336699u;
    struct wl_shm_pool *pool = wl_shm_create_pool(G.shm, fd, (int)n);
    close(fd);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, px, px, px * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    wl_surface_attach(w->surf, b, 0, 0);
    wl_surface_damage(w->surf, 0, 0, px, px);
    wl_surface_commit(w->surf);
    wl_display_roundtrip(dpy);
}
static void unmap_and_destroy(struct wl_display *dpy, win *w)
{
    xdg_toplevel_destroy(w->tl);
    xdg_surface_destroy(w->xs);
    wl_surface_destroy(w->surf);
    wl_display_roundtrip(dpy);
}

static void say(int chan, int label)
{
    if (write(chan, &label, sizeof label) != (ssize_t)sizeof label) _exit(93);
    char go;
    if (read(chan, &go, 1) != 1) _exit(94);
}

enum { PARENT_FIRST = 1, DIALOG_FIRST = 2 };

static int client(const char *sock, int chan, int order)
{
    setenv("WAYLAND_DISPLAY", sock, 1);
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) return 90;
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_l, NULL);
    wl_display_roundtrip(dpy);
    if (!G.comp || !G.shm || !G.wm) return 91;
    xdg_wm_base_add_listener(G.wm, &wm_l, NULL);

    win main_w, dlg, other;
    make(&main_w, "main", NULL);
    make(&dlg, "dialog", main_w.tl);          /* the question is a dialog OF the program */
    if (order == PARENT_FIRST) {
        map(dpy, &main_w);
        make(&other, "other", NULL);          /* something else she opened since: it is the LAST window */
        map(dpy, &other);
        say(chan, 1);                         /* the program is up */
        map(dpy, &dlg);
        say(chan, 2);                         /* ...and has asked its question */
    } else {
        map(dpy, &dlg);                       /* the question first, as VLC's does */
        say(chan, 1);
        map(dpy, &main_w);
        say(chan, 2);                         /* ...then the program */
    }
    unmap_and_destroy(dpy, &dlg);             /* answered */
    say(chan, 3);
    wl_display_disconnect(dpy);
    return 0;
}

static int fails, checked;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-86s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fails++;
}

static uint32_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static int get_label(aurwl *c, int chan, int want)
{
    int label = 0;
    size_t got = 0;
    uint32_t t0 = now_ms();
    while (got < sizeof label && now_ms() - t0 < 15000) {
        struct pollfd p[2] = { { aurwl_fd(c), POLLIN, 0 }, { chan, POLLIN, 0 } };
        poll(p, 2, 20);
        aurwl_dispatch(c);
        aurwl_reap(c);
        if (p[1].revents & POLLIN) {
            ssize_t k = read(chan, (char *)&label + got, sizeof label - got);
            if (k <= 0) return 0;
            got += (size_t)k;
        }
    }
    aurwl_dispatch(c);
    return got == sizeof label && label == want;
}

static const char *front(const shell_ctx *sc)
{
    return sc->focus >= 0 && sc->focus < sc->n_wins ? sc->wins[sc->focus].title : "(Home)";
}

static void sync_a_while(aurwl *c, shell_ctx *sc, const shell_layout *L)
{
    for (int i = 0; i < 8; i++) { aurwl_dispatch(c); session_sync(sc, L); if (L->step) L->step(sc, 0.05f); aurwl_frame_done(c, now_ms()); }
}

static void run(const char *look, int order)
{
    char nm[160];
    aurwl *c = aurwl_create(1280, 720, 60000);
    if (!c) { printf("wltest-dialog: compositor did not start\n"); fails++; return; }
    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) { perror("socketpair"); fails++; return; }
    pid_t kid = fork();
    if (kid == 0) { close(sp[0]); _exit(client(aurwl_socket(c), sp[1], order)); }
    close(sp[1]);

    static shell_ctx sc;
    memset(&sc, 0, sizeof sc);
    sc.wl = c;
    sc.screen_w = 1280; sc.screen_h = 680;
    char shf[96]; snprintf(shf, sizeof shf, "shells/%s.shell", look);
    if (shell_archetype_load(&sc, shf) < 0) { printf("cannot read %s (run from the repository root)\n", shf); exit(2); }
    sc.screen_w = 1280; sc.screen_h = 680;
    const shell_layout *L = shell_layout_by_id(sc.layout_id);
    sc.focus = -1;
    if (L->init) L->init(&sc);

    const char *what = order == PARENT_FIRST ? "the program, then its question" : "the question, then the program";
    if (!get_label(c, sp[0], 1)) { printf("wltest-dialog: step 1 never came\n"); fails++; goto done; }
    sync_a_while(c, &sc, L);
    { char go = 1; if (write(sp[0], &go, 1) < 0) perror("write"); }
    if (!get_label(c, sp[0], 2)) { printf("wltest-dialog: step 2 never came\n"); fails++; goto done; }
    sync_a_while(c, &sc, L);
    snprintf(nm, sizeof nm, "%s, %s: they are all windows of hers", look, what);
    int want_n = order == PARENT_FIRST ? 3 : 2;        /* (parent-first has an unrelated window as well) */
    ok(nm, sc.n_wins == want_n);
    snprintf(nm, sizeof nm, "%s, %s: the question is in front (front: %s)", look, what, front(&sc));
    ok(nm, sc.n_wins == want_n && !strcmp(front(&sc), "dialog"));
    { int d = -1; for (int i = 0; i < sc.n_wins; i++) if (!strcmp(sc.wins[i].title, "dialog")) d = i;
      int m = -1; for (int i = 0; i < sc.n_wins; i++) if (!strcmp(sc.wins[i].title, "main")) m = i;
      snprintf(nm, sizeof nm, "%s, %s: the shell knows it belongs to the program", look, what);
      ok(nm, d >= 0 && m >= 0 && sc.wins[d].dialog_of == sc.wins[m].wid && sc.wins[m].dialog_of == 0); }
    { char go = 1; if (write(sp[0], &go, 1) < 0) perror("write"); }

    if (!get_label(c, sp[0], 3)) { printf("wltest-dialog: step 3 never came\n"); fails++; goto done; }
    sync_a_while(c, &sc, L);
    snprintf(nm, sizeof nm, "%s, %s: answered, she is back in the program (front: %s)", look, what, front(&sc));
    ok(nm, sc.n_wins == want_n - 1 && !strcmp(front(&sc), "main"));
    { char go = 1; if (write(sp[0], &go, 1) < 0) perror("write"); }
done:
    close(sp[0]);
    waitpid(kid, NULL, 0);
    aurwl_destroy(c);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    if (!getenv("XDG_RUNTIME_DIR")) { fprintf(stderr, "wltest-dialog: XDG_RUNTIME_DIR must be set\n"); return 2; }
    printf("a dialog opens in front of the program that asked it\n");
    static const char *LOOKS[] = { "rail", "taskbar", "dock", "tiles" };
    for (size_t i = 0; i < sizeof LOOKS / sizeof *LOOKS; i++) {
        run(LOOKS[i], PARENT_FIRST);
        run(LOOKS[i], DIALOG_FIRST);
    }
    printf("\n%d checks, %d failed\n", checked, fails);
    return fails ? 1 : 0;
}
