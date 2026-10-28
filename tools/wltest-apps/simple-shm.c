/* simple-shm.c -- the smallest honest Wayland client: one xdg toplevel
 * with a gradient in shared memory, redrawn on frame callbacks.
 *
 * tools/wlhostile.c asks a "well-behaved client" to prove the compositor
 * still serves after a hostile one, and it asks for weston-simple-shm.
 * Where Weston is not installed (a build container, a CI runner with only
 * libwayland-dev) this stands in for it under that name:
 *
 *   cc -o /usr/local/bin/weston-simple-shm tools/wltest-apps/simple-shm.c \
 *      src/aurwl/gen/xdg-shell-protocol.c -Isrc/aurwl/gen $(pkg-config --cflags --libs wayland-client)
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include "xdg-shell-client.h"

static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_surface *surf;
static struct wl_buffer *buf;
static uint32_t *map;
static int W = 320, H = 200, tick;
static int configured, running = 1;

static void draw(void)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            map[y * W + x] = 0xFF000000u | (uint32_t)((x + tick) * 255 / W) << 16
                           | (uint32_t)(y * 255 / H) << 8 | (uint32_t)((x ^ y) & 255);
}
static void frame(void *d, struct wl_callback *cb, uint32_t t);
static const struct wl_callback_listener frame_l = { frame };
static void frame(void *d, struct wl_callback *cb, uint32_t t)
{
    (void)d; (void)t;
    wl_callback_destroy(cb);
    tick += 3; draw();
    wl_surface_attach(surf, buf, 0, 0);
    wl_surface_damage(surf, 0, 0, W, H);
    wl_callback_add_listener(wl_surface_frame(surf), &frame_l, NULL);
    wl_surface_commit(surf);
}
static void reg_global(void *d, struct wl_registry *r, uint32_t n, const char *i, uint32_t v)
{
    (void)d;
    if (!strcmp(i, "wl_compositor")) comp = wl_registry_bind(r, n, &wl_compositor_interface, 1);
    else if (!strcmp(i, "wl_shm")) shm = wl_registry_bind(r, n, &wl_shm_interface, 1);
    else if (!strcmp(i, "xdg_wm_base")) wm = wl_registry_bind(r, n, &xdg_wm_base_interface, v < 2 ? v : 2);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };
static void xs_configure(void *d, struct xdg_surface *xs, uint32_t s)
{ (void)d; xdg_surface_ack_configure(xs, s); configured = 1; }
static const struct xdg_surface_listener xs_l = { xs_configure };
static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s)
{ (void)d; (void)t; (void)w; (void)h; (void)s; }
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; running = 0; }
static const struct xdg_toplevel_listener tl_l = { .configure = tl_configure, .close = tl_close };

int main(void)
{
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) return 1;
    wl_registry_add_listener(wl_display_get_registry(dpy), &reg_l, NULL);
    wl_display_roundtrip(dpy);
    if (!comp || !shm || !wm) return 2;
    surf = wl_compositor_create_surface(comp);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surf);
    xdg_surface_add_listener(xs, &xs_l, NULL);
    struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_l, NULL);
    xdg_toplevel_set_title(tl, "simple-shm");
    wl_surface_commit(surf);
    while (!configured && wl_display_dispatch(dpy) >= 0) {}
    int fd = memfd_create("simple-shm", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, W * H * 4) < 0) return 3;
    map = mmap(NULL, (size_t)W * H * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, W * H * 4);
    buf = wl_shm_pool_create_buffer(pool, 0, W, H, W * 4, WL_SHM_FORMAT_XRGB8888);
    frame(NULL, wl_surface_frame(surf), 0);
    while (running && wl_display_dispatch(dpy) >= 0) {}
    return 0;
}
