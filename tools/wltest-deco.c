/* wltest-deco.c — which windows draw their own title bar, and what their
 * title bar can ask.
 *
 * Firefox and GTK programs draw a title bar with window buttons of their
 * own, and never use xdg-decoration. The shell must not draw a second set
 * on top (docs/handoff/streams/C.md), so the compositor tells it which
 * toplevels draw their own (aurwl_win_csd) and hands over what their bar
 * asked for (move, minimise, maximise, restore) once.
 *
 * One process is the compositor (aurwl.c linked in, run through its public
 * API), the other a Wayland client that makes five toplevels the five ways
 * a toolkit can behave, then waits while the compositor looks at them:
 *
 *   plain     binds no xdg-decoration                  -> draws its own
 *   ssd       asks for nothing in particular           -> ours
 *   ssd2      asks for server-side                     -> ours
 *   own       asks for client-side                     -> its own, and is told so
 *   asks      no decoration; moves, minimises, maximises, un-maximises
 *
 *   sh tools/wltest-deco.sh        (Linux: libwayland-dev, libxkbcommon-dev)
 *
 * Exit status 0 only if every check holds.
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
#include <time.h>

#include <wayland-client.h>
#include "xdg-shell-client.h"
#include "xdg-decoration-client.h"

#include "../src/aurwl/aurwl.h"

enum { NW = 5 };
static const char *NAME[NW] = { "plain", "ssd", "ssd2", "own", "asks" };

struct cl {
    struct wl_compositor *comp;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct xdg_wm_base *wm;
    struct zxdg_decoration_manager_v1 *deco;
    struct wl_surface *surf[NW];
    struct xdg_surface *xs[NW];
    struct xdg_toplevel *tl[NW];
    struct zxdg_toplevel_decoration_v1 *dec[NW];
    int mode[NW];                 /* what the compositor told it: 1 client, 2 server, 0 nothing */
    int configured[NW];
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    struct cl *c = d;
    if (!strcmp(iface, "wl_compositor")) c->comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
    else if (!strcmp(iface, "wl_shm")) c->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "wl_seat")) c->seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
    else if (!strcmp(iface, "xdg_wm_base")) c->wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 2);
    else if (!strcmp(iface, "zxdg_decoration_manager_v1"))
        c->deco = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

struct tag { struct cl *c; int i; };
static struct tag TAGS[NW];

static void xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
    struct tag *t = d;
    xdg_surface_ack_configure(xs, serial);
    t->c->configured[t->i] = 1;
}
static const struct xdg_surface_listener xs_l = { xs_configure };
static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s)
{ (void)d; (void)t; (void)w; (void)h; (void)s; }
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; }
static void tl_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h) { (void)d; (void)t; (void)w; (void)h; }
static void tl_caps(void *d, struct xdg_toplevel *t, struct wl_array *a) { (void)d; (void)t; (void)a; }
static const struct xdg_toplevel_listener tl_l = { tl_configure, tl_close, tl_bounds, tl_caps };
static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };
static void dc_configure(void *d, struct zxdg_toplevel_decoration_v1 *z, uint32_t mode)
{ (void)z; struct tag *t = d; t->c->mode[t->i] = (int)mode; }
static const struct zxdg_toplevel_decoration_v1_listener dc_l = { dc_configure };

static void give_buffer(struct cl *c, int i)
{
    int w = 64, h = 64;
    size_t n = (size_t)w * h * 4;
    int fd = memfd_create("deco", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)n) < 0) { perror("memfd"); exit(98); }
    uint32_t *map = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { perror("mmap"); exit(98); }
    for (int k = 0; k < w * h; k++) map[k] = 0xFF336699u;
    struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, (int)n);
    close(fd);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    wl_surface_attach(c->surf[i], b, 0, 0);
    wl_surface_damage(c->surf[i], 0, 0, w, h);
    wl_surface_commit(c->surf[i]);
}

/* What the client saw, handed to the compositor side with the READY byte. */
typedef struct { int mode[NW]; } report;

static int client(const char *sock, int chan)
{
    setenv("WAYLAND_DISPLAY", sock, 1);
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "client: no display\n"); return 90; }
    struct cl c; memset(&c, 0, sizeof c);
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_l, &c);
    wl_display_roundtrip(dpy);
    if (!c.comp || !c.shm || !c.wm || !c.seat) { fprintf(stderr, "client: missing globals\n"); return 91; }
    if (!c.deco) { fprintf(stderr, "client: no xdg-decoration global\n"); return 92; }
    xdg_wm_base_add_listener(c.wm, &wm_l, NULL);

    for (int i = 0; i < NW; i++) {
        TAGS[i].c = &c; TAGS[i].i = i;
        c.surf[i] = wl_compositor_create_surface(c.comp);
        c.xs[i] = xdg_wm_base_get_xdg_surface(c.wm, c.surf[i]);
        xdg_surface_add_listener(c.xs[i], &xs_l, &TAGS[i]);
        c.tl[i] = xdg_surface_get_toplevel(c.xs[i]);
        xdg_toplevel_add_listener(c.tl[i], &tl_l, &TAGS[i]);
        xdg_toplevel_set_title(c.tl[i], NAME[i]);
        if (i == 1 || i == 2 || i == 3) {
            c.dec[i] = zxdg_decoration_manager_v1_get_toplevel_decoration(c.deco, c.tl[i]);
            zxdg_toplevel_decoration_v1_add_listener(c.dec[i], &dc_l, &TAGS[i]);
            if (i == 2) zxdg_toplevel_decoration_v1_set_mode(c.dec[i], ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
            if (i == 3) zxdg_toplevel_decoration_v1_set_mode(c.dec[i], ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        }
        wl_surface_commit(c.surf[i]);
    }
    for (int tries = 0; tries < 20; tries++) {
        wl_display_roundtrip(dpy);
        int all = 1;
        for (int i = 0; i < NW; i++) all &= c.configured[i];
        if (all) break;
    }
    for (int i = 0; i < NW; i++) give_buffer(&c, i);
    wl_display_roundtrip(dpy);

    /* The program's own title bar: drag, minimise, maximise. */
    xdg_toplevel_move(c.tl[4], c.seat, 1);
    xdg_toplevel_set_minimized(c.tl[4]);
    xdg_toplevel_set_maximized(c.tl[4]);
    /* ...and one that says restore, on a window that did not say anything else. */
    xdg_toplevel_unset_maximized(c.tl[0]);
    wl_display_roundtrip(dpy);
    wl_display_roundtrip(dpy);

    report rp;
    for (int i = 0; i < NW; i++) rp.mode[i] = c.mode[i];
    if (write(chan, &rp, sizeof rp) != (ssize_t)sizeof rp) return 93;
    char go;
    if (read(chan, &go, 1) != 1) return 94;       /* hold the windows until the compositor has looked */
    wl_display_disconnect(dpy);
    return 0;
}

static int fails, checked;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-72s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fails++;
}

static aurwl_win *by_title(aurwl *c, const char *t)
{
    int n = aurwl_window_count(c);
    for (int i = 0; i < n; i++) {
        aurwl_win *w = aurwl_window_at(c, i);
        if (w && !strcmp(aurwl_win_title(w), t)) return w;
    }
    return NULL;
}

static uint32_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

int main(void)
{
    if (!getenv("XDG_RUNTIME_DIR")) { fprintf(stderr, "wltest-deco: XDG_RUNTIME_DIR must be set\n"); return 2; }
    aurwl *c = aurwl_create(1280, 720, 60000);
    if (!c) { fprintf(stderr, "wltest-deco: compositor did not start\n"); return 1; }
    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) { perror("socketpair"); return 1; }
    pid_t kid = fork();
    if (kid < 0) { perror("fork"); return 1; }
    if (kid == 0) { close(sp[0]); _exit(client(aurwl_socket(c), sp[1])); }
    close(sp[1]);

    report rp; size_t got = 0;
    uint32_t t0 = now_ms();
    while (got < sizeof rp && now_ms() - t0 < 15000) {
        struct pollfd p[2] = { { aurwl_fd(c), POLLIN, 0 }, { sp[0], POLLIN, 0 } };
        poll(p, 2, 20);
        aurwl_dispatch(c);
        aurwl_reap(c);
        if (p[1].revents & POLLIN) {
            ssize_t k = read(sp[0], (char *)&rp + got, sizeof rp - got);
            if (k <= 0) break;
            got += (size_t)k;
        }
    }
    if (got < sizeof rp) {
        printf("wltest-deco: the client never finished\n");
        kill(kid, 9); waitpid(kid, NULL, 0);
        return 1;
    }
    aurwl_dispatch(c);

    printf("\nwhich windows draw their own title bar\n");
    aurwl_win *w[NW];
    for (int i = 0; i < NW; i++) w[i] = by_title(c, NAME[i]);
    for (int i = 0; i < NW; i++) ok(NAME[i], w[i] != NULL);
    if (!w[0] || !w[1] || !w[2] || !w[3] || !w[4]) { fails++; goto done; }

    ok("no xdg-decoration object: it draws its own (Firefox, GTK)", aurwl_win_csd(w[0]) == 1);
    ok("an xdg-decoration object, no preference: the shell's bar", aurwl_win_csd(w[1]) == 0);
    ok("asked for server-side: the shell's bar", aurwl_win_csd(w[2]) == 0);
    ok("asked for client-side: it draws its own", aurwl_win_csd(w[3]) == 1);
    ok("...and was told so (it must not guess)", rp.mode[3] == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    ok("...while the others were told server-side",
       rp.mode[1] == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE &&
       rp.mode[2] == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    ok("a popup or nothing at all is not a window that draws its own", aurwl_win_csd(NULL) == 0);

    printf("\nwhat its own title bar asked for\n");
    unsigned rq = aurwl_win_take_requests(w[4]);
    ok("move was asked for", (rq & AURWL_REQ_MOVE) != 0);
    ok("minimise was asked for", (rq & AURWL_REQ_MINIMIZE) != 0);
    ok("maximise was asked for", (rq & AURWL_REQ_MAXIMIZE) != 0);
    ok("restore was not (nobody asked)", (rq & AURWL_REQ_RESTORE) == 0);
    ok("they are handed over once", aurwl_win_take_requests(w[4]) == 0);
    ok("restore, asked on another window, is that window's",
       (aurwl_win_take_requests(w[0]) & AURWL_REQ_RESTORE) != 0);
    ok("a window that asked nothing has nothing to take", aurwl_win_take_requests(w[1]) == 0);

done:;
    char go = 1;
    if (write(sp[0], &go, 1) < 0) perror("write");
    waitpid(kid, NULL, 0);
    aurwl_destroy(c);
    printf("\n%d checks, %d failed\n", checked, fails);
    return fails ? 1 : 0;
}
