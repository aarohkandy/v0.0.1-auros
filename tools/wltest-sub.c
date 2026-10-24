/* wltest-sub.c — does the window the shell paints contain its subsurfaces?
 *
 * Firefox draws its page into a wl_subsurface. aurwl kept that surface's
 * pixels and never put them anywhere, so Firefox opened as an empty
 * frame on a booted AurOS (PARITY, build 39906221767). Chromium and
 * Electron video, GTK4 popups and Qt all draw through subsurfaces too.
 *
 * This is the test that cannot be fooled by "a window mapped": every
 * scenario builds a tree of surfaces whose pixels are known, asks the
 * compositor for the window's content -- exactly the call the shell makes
 * -- and compares it BYTE FOR BYTE with a picture painted by a plain
 * reference model written here, with no code shared with the compositor.
 * Input goes through the same door the shell uses (aurwl_pointer_motion
 * with a point in the window), and the client reports where it was told
 * the pointer is.
 *
 * Compositor and client are one program in two processes: the parent is
 * the compositor (aurwl.c linked in, run through its public API), the
 * child is a Wayland client that runs one scenario. They talk over a
 * socketpair of their own, so the child can say "I have committed
 * everything; here is what the window must look like now" at the exact
 * moments a synchronized subsurface is supposed to be showing the old
 * frame.
 *
 *   wltest-sub              run every scenario
 *   wltest-sub N | NAME     run one
 *   wltest-sub -l           list them
 *
 * Exit status 0 only if every scenario passed.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
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
#include "viewporter-client.h"
#include "text-input-client.h"
#include "idle-inhibit-client.h"
#include "cursor-shape-client.h"

#include "../src/aurwl/aurwl.h"
#include "../src/aurwl/textinput.h"

/* Stream W1's text-input-v3 half is linked in by wltest-sub.sh; where it
 * is not, the scenario that needs it says so and passes. */
int textinput_state(void) __attribute__((weak));

/* ── pictures and the reference model ─────────────────────────────── */

typedef struct { int w, h; uint32_t *px; } img;

static img img_new(int w, int h)
{
    img m = { w, h, calloc((size_t)w * h, 4) };
    if (!m.px) abort();
    return m;
}
static void img_free(img *m) { free(m->px); m->px = NULL; }
static img img_solid(int w, int h, uint32_t argb)
{
    img m = img_new(w, h);
    for (int i = 0; i < w * h; i++) m.px[i] = argb;
    return m;
}

/* Premultiplied source-over, one channel at a time, written the plain
 * way (the compositor does the same sum with a shift trick). */
static uint32_t ref_over(uint32_t d, uint32_t s)
{
    uint32_t sa = s >> 24, out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t dv = (d >> sh) & 255, sv = (s >> sh) & 255;
        uint32_t v = (dv * (255 - sa) + 127) / 255 + sv;
        out |= (v > 255 ? 255 : v) << sh;
    }
    return out;
}

/* Paint `s` onto `m` with its corner at (x,y), clipped to `m`. */
static void img_over(img *m, int x, int y, const img *s)
{
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            int dx = x + i, dy = y + j;
            if (dx < 0 || dy < 0 || dx >= m->w || dy >= m->h) continue;
            m->px[dy * m->w + dx] = ref_over(m->px[dy * m->w + dx], s->px[j * s->w + i]);
        }
}
/* The same, with the source first scaled to dw x dh (nearest, sampled at
 * the pixel's centre) from the part of it at (sx,sy) sw x sh. */
static void img_over_scaled(img *m, int x, int y, int dw, int dh,
                            const img *s, int sx, int sy, int sw, int sh)
{
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            int dx = x + i, dy = y + j;
            if (dx < 0 || dy < 0 || dx >= m->w || dy >= m->h) continue;
            int u = sx + (int)(((2L * i + 1) * sw) / (2L * dw));
            int v = sy + (int)(((2L * j + 1) * sh) / (2L * dh));
            m->px[dy * m->w + dx] = ref_over(m->px[dy * m->w + dx], s->px[v * s->w + u]);
        }
}
static void img_rect(img *m, int x, int y, int w, int h, uint32_t argb)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int dx = x + i, dy = y + j;
            if (dx >= 0 && dy >= 0 && dx < m->w && dy < m->h) m->px[dy * m->w + dx] = argb;
        }
}

/* ── the line between the two processes ───────────────────────────── */

enum { M_EXPECT = 1, M_MOVE, M_BUTTON, M_FOCUS, M_FRAME, M_RESIZE, M_ASK, M_TI, M_IDLE, M_TIME, M_KEY, M_CURSOR, M_LEAVE, M_CONF, M_FOCUSPOP, M_FOCUSIS, M_END };
typedef struct { int kind, a, b, c, d; } msg;

static int xfer(int fd, void *buf, size_t n, int wr)
{
    char *p = buf;
    while (n) {
        ssize_t k = wr ? write(fd, p, n) : read(fd, p, n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return -1;
        p += k; n -= (size_t)k;
    }
    return 0;
}

/* ── the compositor side ──────────────────────────────────────────── */

static int fails_here;          /* failures reported in the current scenario */

static aurwl_win *first_window(aurwl *c)
{
    int n = aurwl_window_count(c);
    for (int i = 0; i < n; i++) {
        aurwl_win *w = aurwl_window_at(c, i);
        if (w && !aurwl_win_is_popup(w)) return w;
    }
    return NULL;
}

/* Compare what the shell would be handed with what it must be handed. */
static int check_content(aurwl *c, const img *want, const char *label)
{
    aurwl_win *w = first_window(c);
    if (!w) { printf("      %s: no window is mapped\n", label); return 1; }
    surface *s = aurwl_win_content(w);
    if (!s) { printf("      %s: the window has no content\n", label); return 1; }
    if (s->w != want->w || s->h != want->h) {
        printf("      %s: content is %dx%d, expected %dx%d\n", label, s->w, s->h, want->w, want->h);
        return 1;
    }
    int bad = 0, fx = -1, fy = -1;
    uint32_t got = 0, exp = 0;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint32_t g = s->px[(size_t)y * s->stride + x], e = want->px[y * want->w + x];
            if (g != e) { if (!bad) { fx = x; fy = y; got = g; exp = e; } bad++; }
        }
    if (bad)
        printf("      %s: %d of %d pixels differ; first at (%d,%d): got %08x, expected %08x\n",
               label, bad, s->w * s->h, fx, fy, got, exp);
    return bad ? 1 : 0;
}

static uint32_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* Run the compositor until the child ends, serving its requests. */
static int serve(aurwl *c, int chan, pid_t kid, int *child_result)
{
    uint32_t t0 = now_ms();
    for (;;) {
        struct pollfd p[2] = {
            { .fd = aurwl_fd(c), .events = POLLIN },
            { .fd = chan,        .events = POLLIN },
        };
        poll(p, 2, 20);
        aurwl_dispatch(c);
        aurwl_reap(c);
        if (now_ms() - t0 > 20000) { printf("      timed out\n"); return 1; }
        if (!(p[1].revents & (POLLIN | POLLHUP))) {
            int st;
            if (waitpid(kid, &st, WNOHANG) == kid) {
                *child_result = WIFEXITED(st) ? WEXITSTATUS(st) : 99;
                return 0;
            }
            continue;
        }
        msg m;
        if (xfer(chan, &m, sizeof m, 0) < 0) {            /* the child hung up */
            int st; waitpid(kid, &st, 0);
            *child_result = WIFEXITED(st) ? WEXITSTATUS(st) : 99;
            return 0;
        }
        uint8_t reply = 0;
        aurwl_win *w = first_window(c);
        switch (m.kind) {
        case M_EXPECT: {
            img want = img_new(m.a, m.b);
            if (xfer(chan, want.px, (size_t)m.a * m.b * 4, 0) < 0) return 1;
            char label[32]; snprintf(label, sizeof label, "step %d", m.c);
            if (check_content(c, &want, label)) { fails_here++; reply = 1; }
            img_free(&want);
            break;
        }
        case M_MOVE:   aurwl_pointer_motion(c, w, m.a, m.b, now_ms()); break;
        case M_BUTTON: aurwl_pointer_button(c, (uint32_t)m.a, m.b, now_ms()); break;
        case M_FOCUS:  aurwl_set_focus(c, w); break;
        case M_FOCUSPOP: {        /* "the shell was pressed inside a menu": the keyboard is asked to go to it */
            for (int i = 0, n = aurwl_window_count(c); i < n; i++) {
                aurwl_win *x = aurwl_window_at(c, i);
                if (x && aurwl_win_is_popup(x)) { aurwl_set_focus(c, x); break; }
            }
            break;
        }
        case M_FOCUSIS: {         /* "who has the keyboard?" a: 0 nobody, 1 the first window, 2 a popup */
            aurwl_win *f = aurwl_focus(c);
            int is = !f ? 0 : aurwl_win_is_popup(f) ? 2 : (f == w ? 1 : 3);
            reply = (uint8_t)(is == m.a ? 0 : 1);
            if (reply) { printf("      the keyboard is with %s, expected %s\n", is == 0 ? "nobody" : is == 1 ? "the window" : is == 2 ? "a popup" : "another window",
                                m.a == 0 ? "nobody" : m.a == 1 ? "the window" : "a popup"); fails_here++; }
            break;
        }
        case M_KEY:    aurwl_key(c, (uint32_t)m.a, m.b, now_ms()); break;
        case M_LEAVE:  aurwl_pointer_motion(c, NULL, 0, 0, now_ms()); break;   /* off every window */
        case M_CURSOR: {  /* "what is the pointer asked to look like?" a=kind b,c=hotspot d=shape; IMAGE: w,h, pixels follow */
            int dim[2] = { 0, 0 };
            uint32_t *px = NULL;
            if (m.a == AURWL_CURSOR_IMAGE) {
                if (xfer(chan, dim, sizeof dim, 0) < 0) return 1;
                px = malloc((size_t)dim[0] * dim[1] * 4);
                if (!px || xfer(chan, px, (size_t)dim[0] * dim[1] * 4, 0) < 0) return 1;
            }
            surface *im = NULL; int hx = 0, hy = 0, sh = 0;
            int kind = aurwl_cursor(c, &im, &hx, &hy, &sh);
            int ok = kind == m.a;
            if (ok && kind == AURWL_CURSOR_SHAPE) ok = sh == m.d;
            if (ok && kind == AURWL_CURSOR_IMAGE) {
                ok = im && im->w == dim[0] && im->h == dim[1] && hx == m.b && hy == m.c;
                for (int y = 0; ok && y < dim[1]; y++)
                    for (int x = 0; x < dim[0]; x++)
                        if (im->px[(size_t)y * im->stride + x] != px[y * dim[0] + x]) { ok = 0; break; }
            }
            if (!ok) {
                printf("      pointer picture: kind %d (hot %d,%d shape %d%s), expected kind %d (hot %d,%d shape %d)\n",
                       kind, hx, hy, sh, im ? "" : ", no image", m.a, m.b, m.c, m.d);
                fails_here++; reply = 1;
            }
            free(px);
            break;
        }
        case M_FRAME:  aurwl_frame_done(c, now_ms()); break;
        case M_RESIZE: if (w) aurwl_win_configure(w, m.a, m.b, 1, 0, 0); break;
        case M_CONF: {    /* "configure the a-th window, as the shell does every frame": size b x c, activated d */
            int idx = 0;
            for (int i = 0, n = aurwl_window_count(c); i < n; i++) {
                aurwl_win *x = aurwl_window_at(c, i);
                if (x && !aurwl_win_is_popup(x) && idx++ == m.a) { aurwl_win_configure(x, m.b, m.c, m.d, 0, 0); break; }
            }
            break;
        }
        case M_ASK: {     /* "what size does the window think it is?" */
            int lw = 0, lh = 0;
            if (w) aurwl_win_logical_size(w, &lw, &lh);
            reply = (uint8_t)(lw == m.a && lh == m.b ? 0 : 1);
            if (reply) { printf("      logical size is %dx%d, expected %dx%d\n", lw, lh, m.a, m.b); fails_here++; }
            break;
        }
        case M_TI: {      /* "what does the compositor think the field is?" */
            int st = textinput_state ? textinput_state() : m.a;
            reply = (uint8_t)(st == m.a ? 0 : 1);
            if (reply) { printf("      text input state is %d, expected %d\n", st, m.a); fails_here++; }
            break;
        }
        case M_IDLE: {    /* "is the screen allowed to go dark?" (m.a: 1 = inhibited) */
            int got = aurwl_idle_inhibited(c);
            reply = (uint8_t)(got == m.a ? 0 : 1);
            if (reply) { printf("      idle inhibited is %d, expected %d\n", got, m.a); fails_here++; }
            break;
        }
        case M_TIME: {    /* "how long does painting this window take?" (limit m.a ms) */
            uint32_t t0 = now_ms();
            if (w) (void)aurwl_win_content(w);
            uint32_t took = now_ms() - t0;
            reply = (uint8_t)(took <= (uint32_t)m.a ? 0 : 1);
            if (reply) { printf("      painting the window took %u ms (limit %d)\n", took, m.a); fails_here++; }
            break;
        }
        case M_END: *child_result = m.a; (void)xfer(chan, &reply, 1, 1);
                    { int st; waitpid(kid, &st, 0); } return 0;
        }
        if (xfer(chan, &reply, 1, 1) < 0) return 1;
    }
}

/* ── the client side ──────────────────────────────────────────────── */

struct pev {                      /* what the pointer last told us */
    struct wl_surface *in;        /* surface it is on now, or NULL */
    int enters, leaves, motions, buttons;
    int ex, ey;                   /* last enter */
    int mx, my;                   /* last motion */
};
struct cl {
    struct wl_display *dpy;
    struct wl_compositor *comp;
    struct wl_subcompositor *sub;
    struct wl_shm *shm;
    struct xdg_wm_base *wm;
    struct wp_viewporter *vper;
    struct wl_seat *seat;
    struct wl_pointer *ptr;
    struct zwp_text_input_manager_v3 *timgr;
    struct zwp_idle_inhibit_manager_v1 *inhmgr;
    struct wp_cursor_shape_manager_v1 *csmgr;
    uint32_t enter_serial;        /* of the last wl_pointer.enter */
    struct wl_data_device_manager *ddm;
    struct wl_data_device *ddev;
    struct {                      /* what the drag target side was told */
        struct wl_data_offer *offer;
        struct wl_surface *in;
        int enters, leaves, motions, drops;
        int ex, ey, mx, my;
        uint32_t serial;
        uint32_t offer_action;
        char mime[64];
    } dnd;
    struct {                      /* what the drag source side was told */
        char target[64];
        uint32_t action;
        int dropped, finished, cancelled;
    } dsrc;
    uint32_t btn_serial;
    struct wl_surface *ti_in;     /* where text input was last entered */
    int ti_enters;
    int chan;
    int step;
    int failed;
    struct pev pe;
    int frames;                   /* frame callbacks fired */
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    struct cl *c = d;
    if (!strcmp(iface, "wl_compositor"))         c->comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
    else if (!strcmp(iface, "wl_subcompositor")) c->sub  = wl_registry_bind(r, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(iface, "wl_shm"))           c->shm  = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "xdg_wm_base"))      c->wm   = wl_registry_bind(r, name, &xdg_wm_base_interface, 2);
    else if (!strcmp(iface, "wp_viewporter"))    c->vper = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
    else if (!strcmp(iface, "wl_seat"))          c->seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
    else if (!strcmp(iface, "wp_cursor_shape_manager_v1")) c->csmgr = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
    else if (!strcmp(iface, "wl_data_device_manager")) c->ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, 3);
    else if (!strcmp(iface, "zwp_idle_inhibit_manager_v1")) c->inhmgr = wl_registry_bind(r, name, &zwp_idle_inhibit_manager_v1_interface, 1);
    else if (!strcmp(iface, "zwp_text_input_manager_v3")) c->timgr = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

static void p_enter(void *d, struct wl_pointer *p, uint32_t ser, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{ (void)p; struct cl *c = d; c->enter_serial = ser; c->pe.in = s; c->pe.enters++; c->pe.ex = wl_fixed_to_int(x); c->pe.ey = wl_fixed_to_int(y); }
static void p_leave(void *d, struct wl_pointer *p, uint32_t ser, struct wl_surface *s)
{ (void)p; (void)ser; (void)s; struct cl *c = d; c->pe.in = NULL; c->pe.leaves++; }
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{ (void)p; (void)t; struct cl *c = d; c->pe.motions++; c->pe.mx = wl_fixed_to_int(x); c->pe.my = wl_fixed_to_int(y); }
static void p_button(void *d, struct wl_pointer *p, uint32_t ser, uint32_t t, uint32_t b, uint32_t st)
{ (void)p; (void)t; (void)b; struct cl *c = d; c->pe.buttons++; if (st) c->btn_serial = ser; }
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{ (void)d; (void)p; (void)t; (void)a; (void)v; }
static void p_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d; (void)p; (void)s; }
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d; (void)p; (void)a; (void)v; }
static const struct wl_pointer_listener ptr_l = {
    .enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button,
    .axis = p_axis, .frame = p_frame, .axis_source = p_axis_source,
    .axis_stop = p_axis_stop, .axis_discrete = p_axis_discrete,
};

static void buf_release(void *d, struct wl_buffer *b) { (void)b; ++*(int *)d; }
static const struct wl_buffer_listener buf_l = { buf_release };

static void frame_done(void *d, struct wl_callback *cb, uint32_t t)
{ (void)t; ((struct cl *)d)->frames++; wl_callback_destroy(cb); }
static const struct wl_callback_listener frame_l = { frame_done };

/* A shared-memory picture the client keeps a mapping of, so a scenario
 * can change a few pixels and say only those changed. */
typedef struct { struct wl_buffer *buf; uint32_t *map; int w, h; int *rel; } shmbuf;   /* rel: releases seen */

static shmbuf mkbuf(struct cl *c, const img *m, uint32_t fmt)
{
    size_t n = (size_t)m->w * m->h * 4;
    int fd = memfd_create("wltest-sub", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)n) < 0) { perror("memfd"); exit(98); }
    uint32_t *map = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { perror("mmap"); exit(98); }
    memcpy(map, m->px, n);
    struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, (int)n);
    close(fd);
    shmbuf b = { wl_shm_pool_create_buffer(pool, 0, m->w, m->h, m->w * 4, fmt), map, m->w, m->h,
                 calloc(1, sizeof(int)) };
    wl_shm_pool_destroy(pool);
    wl_buffer_add_listener(b.buf, &buf_l, b.rel);
    (void)c;
    return b;
}

/* Say what the window must look like right now, once the compositor has
 * certainly seen everything sent so far. */
static void expect(struct cl *c, const img *want)
{
    wl_display_roundtrip(c->dpy);
    msg m = { M_EXPECT, want->w, want->h, ++c->step, 0 };
    uint8_t reply = 1;
    if (xfer(c->chan, &m, sizeof m, 1) < 0 ||
        xfer(c->chan, want->px, (size_t)want->w * want->h * 4, 1) < 0 ||
        xfer(c->chan, &reply, 1, 0) < 0) exit(97);
    if (reply) c->failed = 1;
}
static void ask(struct cl *c, int kind, int a, int b)
{
    wl_display_roundtrip(c->dpy);
    msg m = { kind, a, b, 0, 0 };
    uint8_t reply = 1;
    if (xfer(c->chan, &m, sizeof m, 1) < 0 || xfer(c->chan, &reply, 1, 0) < 0) exit(97);
    if (reply && kind == M_ASK) c->failed = 1;
    wl_display_roundtrip(c->dpy);       /* and take what the compositor answered */
}
/* Tell the compositor something and hear that it is done -- WITHOUT
 * reading what it has sent to this client. A program that is busy. */
static void poke(struct cl *c, int kind, int a, int b, int cc, int d)
{
    msg m = { kind, a, b, cc, d };
    uint8_t reply = 1;
    if (xfer(c->chan, &m, sizeof m, 1) < 0 || xfer(c->chan, &reply, 1, 0) < 0) exit(97);
}
static void check(struct cl *c, int ok, const char *what)
{
    if (ok) return;
    printf("      %s\n", what);
    c->failed = 1;
}

/* ── a toplevel, the way every toolkit makes one ──────────────────── */

typedef struct {
    struct wl_surface *s;
    struct xdg_surface *xs;
    struct xdg_toplevel *tl;
    int configured;
    int count;              /* configures received */
    uint32_t serial;
} top;

static void xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{ top *t = d; xdg_surface_ack_configure(xs, serial); t->serial = serial; t->configured = 1; t->count++; }
static const struct xdg_surface_listener xs_l = { xs_configure };
static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s)
{ (void)d; (void)t; (void)w; (void)h; (void)s; }
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; }
static const struct xdg_toplevel_listener tl_l = { .configure = tl_configure, .close = tl_close };

/* Map a toplevel carrying `m`. The root is the surface everything else
 * is stacked on. */
static top *mktop(struct cl *c, const img *m, uint32_t fmt)
{
    top *t = calloc(1, sizeof *t);      /* the listeners keep pointing at it */
    t->s  = wl_compositor_create_surface(c->comp);
    t->xs = xdg_wm_base_get_xdg_surface(c->wm, t->s);
    xdg_surface_add_listener(t->xs, &xs_l, t);
    t->tl = xdg_surface_get_toplevel(t->xs);
    xdg_toplevel_add_listener(t->tl, &tl_l, t);
    wl_surface_commit(t->s);
    for (int i = 0; i < 20 && !t->configured; i++) wl_display_roundtrip(c->dpy);
    if (!t->configured) { printf("      no configure arrived\n"); exit(96); }
    shmbuf b = mkbuf(c, m, fmt);
    wl_surface_attach(t->s, b.buf, 0, 0);
    wl_surface_damage_buffer(t->s, 0, 0, m->w, m->h);
    wl_surface_commit(t->s);
    wl_display_roundtrip(c->dpy);
    return t;
}

/* A subsurface of `parent`, with `m` attached and committed. Left in
 * the protocol's default synchronized mode unless `desync`. */
typedef struct { struct wl_surface *s; struct wl_subsurface *ss; shmbuf b; } sub;

static sub mksub(struct cl *c, struct wl_surface *parent, int x, int y,
                 const img *m, uint32_t fmt, int desync)
{
    sub u = {0};
    u.s  = wl_compositor_create_surface(c->comp);
    u.ss = wl_subcompositor_get_subsurface(c->sub, u.s, parent);
    if (desync) wl_subsurface_set_desync(u.ss);
    wl_subsurface_set_position(u.ss, x, y);
    if (m) {
        u.b = mkbuf(c, m, fmt);
        wl_surface_attach(u.s, u.b.buf, 0, 0);
        wl_surface_damage_buffer(u.s, 0, 0, m->w, m->h);
        wl_surface_commit(u.s);
    }
    return u;
}
static void show(struct cl *c, sub *u, const img *m, uint32_t fmt)     /* new frame */
{
    u->b = mkbuf(c, m, fmt);
    wl_surface_attach(u->s, u->b.buf, 0, 0);
    wl_surface_damage_buffer(u->s, 0, 0, m->w, m->h);
    wl_surface_commit(u->s);
}

#define XRGB WL_SHM_FORMAT_XRGB8888
#define ARGB WL_SHM_FORMAT_ARGB8888
#define RED   0xFFFF0000u
#define GREEN 0xFF00FF00u
#define BLUE  0xFF0000FFu
#define GREY  0xFF808080u
#define WHITE 0xFFFFFFFFu
#define BLACK 0xFF000000u

/* ── scenarios ────────────────────────────────────────────────────── */

/* 1. No subsurface at all: the baseline. If this fails the harness is
 * what is broken. */
static void sc_plain(struct cl *c)
{
    img r = img_solid(64, 48, RED);
    top *t = mktop(c, &r, XRGB);
    (void)t;
    expect(c, &r);
}

/* 2. The case that opened Firefox blank: a page in a subsurface. An
 * independent (desynchronized) child shows as soon as it is committed;
 * where it sits is the parent's state, so it is at (0,0) until the
 * PARENT commits. */
static void sc_page(struct cl *c)
{
    img root = img_solid(80, 60, GREY), page = img_solid(40, 30, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub p = mksub(c, t->s, 12, 9, &page, XRGB, 1);
    img want = img_solid(80, 60, GREY);
    img_over(&want, 0, 0, &page);                    /* shown, not yet moved */
    expect(c, &want);
    wl_surface_commit(t->s);                          /* the parent commits: it moves */
    want = img_solid(80, 60, GREY);
    img_over(&want, 12, 9, &page);
    expect(c, &want);
    (void)p;
}

/* 3. Synchronized (the default): nothing of the child shows until the
 * parent commits; two commits before that are one frame, the newer, and
 * the older buffer is handed back unseen. */
static void sc_sync(struct cl *c)
{
    img root = img_solid(50, 40, GREY), a = img_solid(20, 10, GREEN), b = img_solid(20, 10, BLUE);
    top *t = mktop(c, &root, XRGB);
    sub s = mksub(c, t->s, 5, 5, &a, XRGB, 0);        /* sync, committed: waits */
    shmbuf first = s.b;
    expect(c, &root);
    show(c, &s, &b, XRGB);                           /* a newer frame, still waiting */
    expect(c, &root);
    check(c, *first.rel == 1, "the superseded frame was not released to its client (exactly once)");
    wl_surface_commit(t->s);
    img want = img_solid(50, 40, GREY);
    img_over(&want, 5, 5, &b);
    expect(c, &want);
    check(c, *first.rel == 1 && *s.b.rel == 1, "a frame was released more or less than once");
}

/* 4. The cache is a snapshot taken AT the commit. Attaching again
 * afterwards, without committing, changes nothing the parent applies. */
static void sc_snapshot(struct cl *c)
{
    img root = img_solid(40, 30, GREY), a = img_solid(10, 10, GREEN), b = img_solid(10, 10, BLUE);
    top *t = mktop(c, &root, XRGB);
    sub s = mksub(c, t->s, 0, 0, &a, XRGB, 0);        /* green committed */
    shmbuf ga = s.b;
    shmbuf nb = mkbuf(c, &b, XRGB);
    wl_surface_attach(s.s, nb.buf, 0, 0);            /* blue attached, NOT committed */
    wl_surface_commit(t->s);
    img want = img_solid(40, 30, GREY);
    img_over(&want, 0, 0, &a);
    expect(c, &want);                                /* green, not blue */
    wl_surface_commit(s.s);                          /* now commit the blue ... */
    wl_surface_commit(t->s);                          /* ... and let the parent apply it */
    want = img_solid(40, 30, GREY);
    img_over(&want, 0, 0, &b);
    expect(c, &want);
    /* a commit that carries no new buffer applies nothing, and must not
     * hand a buffer back twice */
    wl_surface_commit(s.s);
    wl_surface_commit(t->s);
    expect(c, &want);
    check(c, *ga.rel == 1 && *nb.rel == 1, "a frame was released more or less than once");
}

/* 5. Trees. Everything synchronized: only the root's commit shows any of
 * it, and it shows all of it. */
static void sc_nested_sync(struct cl *c)
{
    img root = img_solid(60, 40, GREY), a = img_solid(30, 20, GREEN), b = img_solid(10, 6, RED);
    top *t = mktop(c, &root, XRGB);
    sub A = mksub(c, t->s, 4, 4, &a, XRGB, 0);
    sub B = mksub(c, A.s, 2, 3, &b, XRGB, 0);
    wl_surface_commit(B.s);                          /* nested: waits for A */
    wl_surface_commit(A.s);                          /* waits for the root */
    expect(c, &root);
    wl_surface_commit(t->s);
    img want = img_solid(60, 40, GREY);
    img_over(&want, 4, 4, &a);
    img_over(&want, 6, 7, &b);                       /* (4+2, 4+3) */
    expect(c, &want);
}

/* 6. A desynchronized child of a synchronized parent is still held: it
 * behaves as synchronized because its parent does ("effectively"). */
static void sc_effective_sync(struct cl *c)
{
    img root = img_solid(60, 40, GREY), a = img_solid(30, 20, GREEN), b = img_solid(10, 6, RED);
    top *t = mktop(c, &root, XRGB);
    sub A = mksub(c, t->s, 4, 4, &a, XRGB, 0);        /* sync */
    wl_surface_commit(t->s);                         /* A is on screen */
    img base = img_solid(60, 40, GREY);
    img_over(&base, 4, 4, &a);
    expect(c, &base);
    sub B = mksub(c, A.s, 2, 3, NULL, XRGB, 1);      /* desync under a sync parent */
    show(c, &B, &b, XRGB);                           /* held: A is sync */
    expect(c, &base);
    wl_surface_commit(A.s);                          /* A itself waits for the root */
    expect(c, &base);
    wl_surface_commit(t->s);                         /* now A's state, and B with it */
    img want = base;
    want.px = malloc(60 * 40 * 4); memcpy(want.px, base.px, 60 * 40 * 4);
    img_over(&want, 6, 7, &b);
    expect(c, &want);
}

/* 7. Switching to desynchronized applies what was being held. And only
 * when nothing above is still holding it. */
static void sc_set_desync(struct cl *c)
{
    img root = img_solid(60, 40, GREY), a = img_solid(30, 20, GREEN), b = img_solid(10, 6, RED);
    top *t = mktop(c, &root, XRGB);
    sub A = mksub(c, t->s, 0, 0, &a, XRGB, 0);        /* sync, committed, held */
    sub B = mksub(c, A.s, 0, 0, &b, XRGB, 0);        /* sync, held */
    expect(c, &root);
    wl_subsurface_set_desync(B.ss);                  /* A still holds B */
    expect(c, &root);
    wl_subsurface_set_desync(A.ss);                  /* A is free: A and B both land */
    img want = img_solid(60, 40, GREY);
    img_over(&want, 0, 0, &a);
    img_over(&want, 0, 0, &b);
    expect(c, &want);
}

/* 8. Stacking. The parent is IN the stack: a child below it shows only
 * through its transparent parts; order changes land with the parent. */
static void sc_zorder(struct cl *c)
{
    img root = img_solid(60, 40, BLUE);
    img_rect(&root, 20, 10, 20, 20, 0x00000000);     /* a hole in the parent */
    img low = img_solid(60, 40, RED), hi = img_solid(10, 10, GREEN), mid = img_solid(14, 14, WHITE);
    top *t = mktop(c, &root, ARGB);
    sub L = mksub(c, t->s, 0, 0, &low, XRGB, 1);
    sub H = mksub(c, t->s, 0, 0, &hi, XRGB, 1);
    sub M = mksub(c, t->s, 3, 3, &mid, XRGB, 1);
    wl_subsurface_place_below(L.ss, t->s);            /* pending: the parent has not committed */
    /* before the parent commits, the order is creation order: L, H, M all above it */
    img want = img_solid(60, 40, BLUE);
    img_rect(&want, 20, 10, 20, 20, 0x00000000);
    img_over(&want, 0, 0, &low); img_over(&want, 0, 0, &hi); img_over(&want, 0, 0, &mid);
    expect(c, &want);
    wl_surface_commit(t->s);                          /* L moves under the parent */
    want = img_solid(60, 40, RED);
    img rt = img_solid(60, 40, BLUE); img_rect(&rt, 20, 10, 20, 20, 0x00000000);
    img_over(&want, 0, 0, &rt);                      /* parent over the red */
    img_over(&want, 0, 0, &hi); img_over(&want, 3, 3, &mid);
    expect(c, &want);
    wl_subsurface_place_above(H.ss, M.s);           /* green over white */
    wl_surface_commit(t->s);
    want = img_solid(60, 40, RED);
    img_over(&want, 0, 0, &rt);
    img_over(&want, 3, 3, &mid); img_over(&want, 0, 0, &hi);
    expect(c, &want);
    wl_subsurface_place_below(H.ss, M.s);           /* and back */
    wl_surface_commit(t->s);
    want = img_solid(60, 40, RED);
    img_over(&want, 0, 0, &rt);
    img_over(&want, 0, 0, &hi); img_over(&want, 3, 3, &mid);
    expect(c, &want);
    wl_subsurface_place_above(M.ss, t->s);          /* M right above the parent */
    wl_subsurface_place_below(H.ss, t->s);          /* H right below the parent: over L only */
    wl_surface_commit(t->s);
    want = img_solid(60, 40, RED);
    img_over(&want, 0, 0, &hi);                      /* L, H, parent, M */
    img_over(&want, 0, 0, &rt);
    img_over(&want, 3, 3, &mid);
    expect(c, &want);
}

/* 9. Translucent pixels blend. Premultiplied, so the colour is already
 * scaled by the alpha. */
static void sc_alpha(struct cl *c)
{
    img root = img_solid(30, 20, GREY);
    img veil = img_solid(20, 10, 0x80800000u);       /* 50% red, premultiplied */
    img_rect(&veil, 10, 0, 10, 10, 0x40002040u);     /* 25% of something else */
    top *t = mktop(c, &root, XRGB);
    mksub(c, t->s, 5, 5, &veil, ARGB, 1);
    wl_surface_commit(t->s);
    img want = img_solid(30, 20, GREY);
    img_over(&want, 5, 5, &veil);
    expect(c, &want);
}

/* 10. A subsurface can lie outside its parent, or half outside. The
 * window is what the parent's buffer covers; the rest is cut off. */
static void sc_clip(struct cl *c)
{
    img root = img_solid(40, 30, GREY), big = img_solid(30, 30, GREEN);
    img_rect(&big, 0, 0, 5, 5, RED);
    top *t = mktop(c, &root, XRGB);
    mksub(c, t->s, 25, 20, &big, XRGB, 1);            /* hangs off the corner */
    mksub(c, t->s, -3, -3, &big, XRGB, 1);            /* hangs off the other */
    wl_surface_commit(t->s);
    img want = img_solid(40, 30, GREY);
    img_over(&want, 25, 20, &big);
    img_over(&want, -3, -3, &big);
    expect(c, &want);
}

/* 11. A viewport scales a subsurface to its destination; a source
 * rectangle crops first. */
static void sc_viewport(struct cl *c)
{
    img root = img_solid(40, 30, GREY);
    img four = img_new(2, 2);
    four.px[0] = RED; four.px[1] = GREEN; four.px[2] = BLUE; four.px[3] = WHITE;
    top *t = mktop(c, &root, XRGB);
    sub s = mksub(c, t->s, 4, 4, &four, XRGB, 1);
    struct wp_viewport *vp = wp_viewporter_get_viewport(c->vper, s.s);
    wp_viewport_set_destination(vp, 8, 8);
    wl_surface_commit(s.s);
    wl_surface_commit(t->s);
    img want = img_solid(40, 30, GREY);
    img_over_scaled(&want, 4, 4, 8, 8, &four, 0, 0, 2, 2);
    expect(c, &want);

    img grid = img_new(4, 4);
    for (int i = 0; i < 16; i++) grid.px[i] = 0xFF000000u | (uint32_t)(i * 15) << 16 | (uint32_t)(255 - i * 15);
    sub g = mksub(c, t->s, 20, 10, &grid, XRGB, 1);
    struct wp_viewport *gv = wp_viewporter_get_viewport(c->vper, g.s);
    wp_viewport_set_source(gv, wl_fixed_from_int(1), wl_fixed_from_int(1), wl_fixed_from_int(2), wl_fixed_from_int(2));
    wp_viewport_set_destination(gv, 6, 6);
    wl_surface_commit(g.s);
    wl_surface_commit(t->s);
    img_over_scaled(&want, 20, 10, 6, 6, &grid, 1, 1, 2, 2);
    expect(c, &want);
}

/* 12. mpv's way of making a window: a one-pixel toplevel stretched by a
 * viewport to the window's size, with the picture in a subsurface. The
 * window is the size the VIEWPORT says, not one pixel. */
static void sc_stretched_root(struct cl *c)
{
    img one = img_solid(1, 1, BLACK), vid = img_solid(16, 8, RED);
    top *t = mktop(c, &one, XRGB);
    struct wp_viewport *vp = wp_viewporter_get_viewport(c->vper, t->s);
    wp_viewport_set_destination(vp, 40, 30);
    mksub(c, t->s, 5, 6, &vid, XRGB, 1);
    wl_surface_commit(t->s);
    img want = img_solid(40, 30, BLACK);
    img_over(&want, 5, 6, &vid);
    expect(c, &want);
    ask(c, M_ASK, 40, 30);
}

/* 13a. A window with a shadow AND a viewport. GTK4 sets a viewport
 * destination the size of its whole buffer and the window geometry inside
 * it; the shell maps the pointer over the window it draws, which is the
 * geometry. The size aurwl reports must be the window's, or every click
 * lands in the wrong place (the store's Install button never worked). */
static void sc_csd_viewport(struct cl *c)
{
    img root = img_solid(80, 60, GREY), page = img_solid(30, 20, GREEN);
    top *t = mktop(c, &root, XRGB);
    mksub(c, t->s, 12, 9, &page, XRGB, 1);
    struct wp_viewport *vp = wp_viewporter_get_viewport(c->vper, t->s);
    wp_viewport_set_destination(vp, 80, 60);
    xdg_surface_set_window_geometry(t->xs, 10, 5, 60, 45);
    wl_surface_commit(t->s);
    img full = img_solid(80, 60, GREY);
    img_over(&full, 12, 9, &page);
    img want = img_new(60, 45);
    for (int y = 0; y < 45; y++) for (int x = 0; x < 60; x++) want.px[y * 60 + x] = full.px[(y + 5) * 80 + x + 10];
    expect(c, &want);
    ask(c, M_ASK, 60, 45);                           /* the window, not the 80x60 surface */
}

/* 13. Damage: only the damaged part of a frame is taken from the client.
 * The client rewrites two areas of its buffer but reports one, so the
 * other must still show the old pixels. */
static void sc_damage(struct cl *c)
{
    img root = img_solid(50, 40, GREY), page = img_solid(32, 32, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub p = mksub(c, t->s, 8, 4, &page, XRGB, 1);
    wl_surface_commit(t->s);
    img first = img_solid(50, 40, GREY);
    img_over(&first, 8, 4, &page);
    expect(c, &first);                               /* the window is painted, nothing stale */
    for (int y = 4; y < 8; y++) for (int x = 4; x < 8; x++) p.b.map[y * 32 + x] = RED;      /* damaged */
    for (int y = 20; y < 24; y++) for (int x = 20; x < 24; x++) p.b.map[y * 32 + x] = BLUE; /* not reported */
    wl_surface_attach(p.s, p.b.buf, 0, 0);
    wl_surface_damage_buffer(p.s, 4, 4, 4, 4);
    wl_surface_commit(p.s);
    img want = img_solid(50, 40, GREY);
    img shown = img_solid(32, 32, GREEN);
    img_rect(&shown, 4, 4, 4, 4, RED);
    img_over(&want, 8, 4, &shown);
    expect(c, &want);
}

/* 14. Going away. A child with no buffer is not there; a destroyed one is
 * gone at once, without waiting for the parent to commit. */
static void sc_unmap(struct cl *c)
{
    img root = img_solid(40, 30, GREY), a = img_solid(10, 10, GREEN), keep = img_solid(6, 6, BLUE);
    top *t = mktop(c, &root, XRGB);
    sub s = mksub(c, t->s, 2, 2, &a, XRGB, 1);
    sub k = mksub(c, t->s, 30, 20, &keep, XRGB, 1);   /* stays, so the window stays flattened */
    wl_surface_commit(t->s);
    img base = img_solid(40, 30, GREY);
    img_over(&base, 30, 20, &keep);
    img want = img_solid(40, 30, GREY);
    img_over(&want, 2, 2, &a);
    img_over(&want, 30, 20, &keep);
    expect(c, &want);
    wl_surface_attach(s.s, NULL, 0, 0);              /* unmap */
    wl_surface_commit(s.s);
    expect(c, &base);
    show(c, &s, &a, XRGB);                           /* and back */
    expect(c, &want);
    wl_subsurface_destroy(s.ss);                     /* no parent commit */
    expect(c, &base);
    (void)k;
}

/* 15. Where the pointer is. The topmost subsurface that takes input at
 * the point is told, in its own coordinates; the one with an empty input
 * region is invisible to the pointer. */
static void sc_pointer(struct cl *c)
{
    img root = img_solid(100, 80, GREY), page = img_solid(60, 40, GREEN), veil = img_solid(20, 20, 0x40000040u);
    top *t = mktop(c, &root, XRGB);
    sub P = mksub(c, t->s, 10, 10, &page, XRGB, 1);
    sub V = mksub(c, t->s, 20, 20, &veil, ARGB, 1);
    struct wl_region *none = wl_compositor_create_region(c->comp);
    wl_surface_set_input_region(V.s, none);          /* empty: clicks fall through */
    wl_surface_commit(V.s);
    wl_surface_commit(t->s);
    ask(c, M_FOCUS, 0, 0);
    wl_display_roundtrip(c->dpy);

    ask(c, M_MOVE, 15, 15);                          /* on the page */
    check(c, c->pe.in == P.s, "pointer over the page did not enter the page");
    check(c, c->pe.ex == 5 && c->pe.ey == 5, "page was not told (5,5)");

    ask(c, M_MOVE, 25, 25);                          /* the veil is over it, but takes no input */
    check(c, c->pe.in == P.s, "an input-transparent surface took the pointer");
    check(c, c->pe.mx == 15 && c->pe.my == 15, "motion over the veil was not (15,15) in the page");

    ask(c, M_MOVE, 5, 5);                            /* off the page, on the frame */
    check(c, c->pe.in == t->s, "pointer on the frame did not enter the toplevel");
    check(c, c->pe.leaves == 1, "the page was not told the pointer left");
    check(c, c->pe.ex == 5 && c->pe.ey == 5, "toplevel was not told (5,5)");

    ask(c, M_MOVE, 69, 49);                          /* the page's last pixel ... */
    check(c, c->pe.in == P.s && c->pe.ex == 59 && c->pe.ey == 39, "the page's last pixel is not (59,39)");
    ask(c, M_MOVE, 70, 49);                          /* ... and the one beyond it */
    check(c, c->pe.in == t->s, "the page's right edge is not exclusive");
    ask(c, M_MOVE, 69, 50);
    check(c, c->pe.in == t->s, "the page's bottom edge is not exclusive");

    ask(c, M_MOVE, 15, 15);
    check(c, c->pe.in == P.s, "back on the page");
    /* a button goes to the surface the pointer is on */
    int before = c->pe.buttons;
    ask(c, M_BUTTON, 0x110, 1);
    check(c, c->pe.buttons == before + 1, "button press did not arrive");
    /* implicit grab: pulled off the page with the button down, motion
     * still goes to the page -- in the page's coordinates, so negative */
    ask(c, M_MOVE, 5, 5);
    check(c, c->pe.in == P.s, "dragging out of the page ended the grab");
    check(c, c->pe.mx == -5 && c->pe.my == -5, "grabbed motion is not in the page's coordinates");
    ask(c, M_BUTTON, 0x110, 0);
    ask(c, M_MOVE, 6, 6);
    check(c, c->pe.in == t->s, "after the release the pointer did not move to the toplevel");
}

/* 16. Trees measure from their own parent: the point is walked down
 * through each position in turn. */
static void sc_pointer_nested(struct cl *c)
{
    img root = img_solid(100, 80, GREY), a = img_solid(50, 40, GREEN), b = img_solid(10, 10, RED);
    top *t = mktop(c, &root, XRGB);
    sub A = mksub(c, t->s, 10, 10, &a, XRGB, 1);
    sub B = mksub(c, A.s, 5, 5, &b, XRGB, 1);
    wl_surface_commit(A.s);
    wl_surface_commit(t->s);
    ask(c, M_FOCUS, 0, 0);
    ask(c, M_MOVE, 16, 17);
    check(c, c->pe.in == B.s, "nested child did not take the pointer");
    check(c, c->pe.ex == 1 && c->pe.ey == 2, "nested child was not told (1,2)");
    ask(c, M_MOVE, 40, 40);
    check(c, c->pe.in == A.s && c->pe.ex == 30 && c->pe.ey == 30, "the middle surface was not told (30,30)");
}

/* 17. A subsurface moved by the parent's commit moves under the pointer
 * too: it is where it is DRAWN. */
static void sc_pointer_moves(struct cl *c)
{
    img root = img_solid(60, 40, GREY), a = img_solid(10, 10, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub A = mksub(c, t->s, 0, 0, &a, XRGB, 1);
    wl_surface_commit(t->s);
    ask(c, M_FOCUS, 0, 0);
    ask(c, M_MOVE, 35, 20);
    check(c, c->pe.in == t->s, "frame expected at (35,20)");
    wl_subsurface_set_position(A.ss, 30, 15);
    ask(c, M_MOVE, 35, 20);
    check(c, c->pe.in == t->s, "child moved before its parent committed");
    wl_surface_commit(t->s);
    ask(c, M_MOVE, 36, 21);
    check(c, c->pe.in == A.s && c->pe.ex == 6 && c->pe.ey == 6, "child did not take the pointer after moving");
}

/* 18. Frame callbacks asked of a subsurface are answered, or a toolkit
 * that paces its video on them draws one frame and stops. */
static void sc_frame(struct cl *c)
{
    img root = img_solid(30, 20, GREY), a = img_solid(10, 10, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub A = mksub(c, t->s, 0, 0, &a, XRGB, 1);
    struct wl_callback *cb = wl_surface_frame(A.s);
    wl_callback_add_listener(cb, &frame_l, c);
    wl_surface_commit(A.s);
    ask(c, M_FRAME, 0, 0);
    check(c, c->frames == 1, "the subsurface's frame callback never fired");
}

/* 19. The window geometry still crops the flattened window, and offsets
 * the pointer by the same amount. */
static void sc_geometry(struct cl *c)
{
    img root = img_solid(80, 60, GREY), page = img_solid(30, 20, GREEN);
    top *t = mktop(c, &root, XRGB);
    mksub(c, t->s, 12, 9, &page, XRGB, 1);
    xdg_surface_set_window_geometry(t->xs, 10, 5, 60, 45);
    wl_surface_commit(t->s);
    img full = img_solid(80, 60, GREY);
    img_over(&full, 12, 9, &page);
    img want = img_new(60, 45);
    for (int y = 0; y < 45; y++) for (int x = 0; x < 60; x++) want.px[y * 60 + x] = full.px[(y + 5) * 80 + x + 10];
    expect(c, &want);
}

/* 20. Wreckage. Parents and children are destroyed in every order and
 * the compositor must come out of it still serving: a buffer is
 * destroyed while parked in a cache, a parent goes before its child. */
static void sc_wreck(struct cl *c)
{
    img root = img_solid(40, 30, GREY), a = img_solid(10, 10, GREEN);
    top *t = mktop(c, &root, XRGB);
    img next = img_solid(10, 10, BLUE), shown = img_solid(40, 30, GREY);
    img_over(&shown, 0, 0, &a);
    sub s = mksub(c, t->s, 0, 0, &a, XRGB, 0);
    wl_surface_commit(t->s);
    expect(c, &shown);                               /* a frame on screen */
    show(c, &s, &next, XRGB);                        /* its successor, held in the cache ... */
    wl_buffer_destroy(s.b.buf);                      /* ... and the buffer is destroyed */
    wl_surface_commit(t->s);
    expect(c, &shown);                               /* that frame is lost; the old one stays */
    sub s2 = mksub(c, t->s, 0, 0, &a, XRGB, 1);
    wl_surface_commit(t->s);
    wl_surface_destroy(t->s);                         /* the parent goes first */
    wl_surface_commit(s2.s);
    wl_surface_attach(s2.s, NULL, 0, 0);
    wl_surface_commit(s2.s);
    wl_display_roundtrip(c->dpy);
    check(c, wl_display_get_error(c->dpy) == 0, "the compositor disconnected us");
}

/* 21. Text input follows the keyboard, and the keyboard is on the toplevel
 * -- as in every compositor -- even though the thing being typed into is
 * a subsurface (Firefox's page). Wishes (docs/AI.md 3.5) must know when
 * that field is a password, so the declaration made by a program whose
 * page is a subsurface has to arrive, and the pointer wandering over the
 * page must not disturb it. */
static void ti_enter(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s)
{ (void)t; struct cl *c = d; c->ti_in = s; c->ti_enters++; }
static void ti_leave(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s)
{ (void)t; (void)s; struct cl *c = d; c->ti_in = NULL; }
static void ti_preedit(void *d, struct zwp_text_input_v3 *t, const char *s, int32_t a, int32_t b)
{ (void)d; (void)t; (void)s; (void)a; (void)b; }
static void ti_commit_string(void *d, struct zwp_text_input_v3 *t, const char *s)
{ (void)d; (void)t; (void)s; }
static void ti_delete(void *d, struct zwp_text_input_v3 *t, uint32_t a, uint32_t b)
{ (void)d; (void)t; (void)a; (void)b; }
static void ti_done(void *d, struct zwp_text_input_v3 *t, uint32_t s) { (void)d; (void)t; (void)s; }
static const struct zwp_text_input_v3_listener ti_l = {
    ti_enter, ti_leave, ti_preedit, ti_commit_string, ti_delete, ti_done,
};

static void sc_textinput(struct cl *c)
{
    if (!c->timgr) { printf("      (text-input-v3 is not linked in: skipped)\n"); return; }
    img root = img_solid(100, 80, GREY), page = img_solid(60, 40, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub P = mksub(c, t->s, 10, 10, &page, XRGB, 1);
    wl_surface_commit(t->s);
    struct zwp_text_input_v3 *ti = zwp_text_input_manager_v3_get_text_input(c->timgr, c->seat);
    zwp_text_input_v3_add_listener(ti, &ti_l, c);
    ask(c, M_FOCUS, 0, 0);
    wl_display_roundtrip(c->dpy);
    check(c, c->ti_enters == 1, "text input was not entered when the window got the keyboard");
    check(c, c->ti_in == t->s, "text input was entered on something other than the toplevel");

    ask(c, M_MOVE, 15, 15);                          /* the pointer is over the page */
    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_content_type(ti, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                                       ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD);
    zwp_text_input_v3_commit(ti);
    wl_display_roundtrip(c->dpy);
    ask(c, M_TI, TI_SENSITIVE, 0);
    ask(c, M_MOVE, 5, 5);                            /* and off it: nothing changes */
    ask(c, M_TI, TI_SENSITIVE, 0);

    zwp_text_input_v3_enable(ti);                    /* a different field */
    zwp_text_input_v3_set_content_type(ti, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                                       ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    zwp_text_input_v3_commit(ti);
    wl_display_roundtrip(c->dpy);
    ask(c, M_TI, TI_PLAIN, 0);
    (void)P;
}

/* 22. A film keeps the screen lit -- but only while the window it is
 * playing in is on screen. Firefox puts the inhibitor on the toplevel;
 * mpv on its video subsurface; either must count. */
static void sc_idle(struct cl *c)
{
    if (!c->inhmgr) { printf("      no idle-inhibit global\n"); c->failed = 1; return; }
    img root = img_solid(40, 30, GREY), vid = img_solid(10, 10, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub v = mksub(c, t->s, 5, 5, &vid, XRGB, 1);
    wl_surface_commit(t->s);
    ask(c, M_IDLE, 0, 0);                            /* nobody has asked yet */
    struct zwp_idle_inhibitor_v1 *on_window = zwp_idle_inhibit_manager_v1_create_inhibitor(c->inhmgr, t->s);
    ask(c, M_IDLE, 1, 0);                            /* the toplevel's */
    zwp_idle_inhibitor_v1_destroy(on_window);
    ask(c, M_IDLE, 0, 0);
    struct zwp_idle_inhibitor_v1 *on_video = zwp_idle_inhibit_manager_v1_create_inhibitor(c->inhmgr, v.s);
    ask(c, M_IDLE, 1, 0);                            /* a subsurface's counts too */
    wl_surface_attach(v.s, NULL, 0, 0);              /* the video goes away ... */
    wl_surface_commit(v.s);
    ask(c, M_IDLE, 0, 0);                            /* ... so does the reason */
    show(c, &v, &vid, XRGB);
    ask(c, M_IDLE, 1, 0);
    wl_surface_attach(t->s, NULL, 0, 0);             /* the whole window is hidden */
    wl_surface_commit(t->s);
    ask(c, M_IDLE, 0, 0);
    (void)on_video;
}

/* 23. A program cannot make painting its window cost what it likes. Three
 * thousand subsurfaces, each one pixel stretched by a viewport over the
 * whole window, is three thousand full-window passes. */
static void sc_budget(struct cl *c)
{
    img root = img_solid(1000, 900, GREY), px = img_solid(1, 1, RED);
    top *t = mktop(c, &root, XRGB);
    for (int i = 0; i < 3000; i++) {
        sub s = mksub(c, t->s, 0, 0, &px, XRGB, 1);
        struct wp_viewport *vp = wp_viewporter_get_viewport(c->vper, s.s);
        wp_viewport_set_destination(vp, 1000, 900);
        wl_surface_commit(s.s);
        if (i % 500 == 499) wl_display_roundtrip(c->dpy);
    }
    wl_surface_commit(t->s);
    wl_display_roundtrip(c->dpy);
    ask(c, M_TIME, 1500, 0);
}

/* 24. Drag and drop. The pointer belongs to the drag; the surface under
 * it -- a subsurface, in coordinates of its own -- is told enter, motion,
 * leave and drop; the two ends settle the action between them; the data
 * goes through the pipe; the source hears dropped and finished. A drag
 * nobody accepts, one that is refused, and one that is escaped from all
 * end in `cancelled`. */
static void dd_offer_mime(void *d, struct wl_data_offer *o, const char *m)
{ struct cl *c = d; (void)o; if (!c->dnd.mime[0]) snprintf(c->dnd.mime, sizeof c->dnd.mime, "%s", m); }
static void dd_offer_srcact(void *d, struct wl_data_offer *o, uint32_t a) { (void)d; (void)o; (void)a; }
static void dd_offer_action(void *d, struct wl_data_offer *o, uint32_t a)
{ (void)o; ((struct cl *)d)->dnd.offer_action = a; }
static const struct wl_data_offer_listener dd_offer_l = { dd_offer_mime, dd_offer_srcact, dd_offer_action };

static void dd_data_offer(void *d, struct wl_data_device *dev, struct wl_data_offer *o)
{ struct cl *c = d; (void)dev; c->dnd.mime[0] = 0; wl_data_offer_add_listener(o, &dd_offer_l, c); }
static void dd_enter(void *d, struct wl_data_device *dev, uint32_t ser, struct wl_surface *s,
                     wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{
    struct cl *c = d; (void)dev;
    c->dnd.enters++; c->dnd.in = s; c->dnd.serial = ser; c->dnd.offer = o;
    c->dnd.ex = wl_fixed_to_int(x); c->dnd.ey = wl_fixed_to_int(y);
    c->dnd.offer_action = 0;
}
static void dd_leave(void *d, struct wl_data_device *dev)
{
    struct cl *c = d; (void)dev;
    c->dnd.leaves++; c->dnd.in = NULL;
    if (c->dnd.offer) { wl_data_offer_destroy(c->dnd.offer); c->dnd.offer = NULL; }
}
static void dd_motion(void *d, struct wl_data_device *dev, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{ struct cl *c = d; (void)dev; (void)t; c->dnd.motions++; c->dnd.mx = wl_fixed_to_int(x); c->dnd.my = wl_fixed_to_int(y); }
static void dd_drop(void *d, struct wl_data_device *dev) { (void)dev; ((struct cl *)d)->dnd.drops++; }
static void dd_selection(void *d, struct wl_data_device *dev, struct wl_data_offer *o)
{ (void)d; (void)dev; if (o) wl_data_offer_destroy(o); }
static const struct wl_data_device_listener dd_l = { dd_data_offer, dd_enter, dd_leave, dd_motion, dd_drop, dd_selection };

static void ds_target(void *d, struct wl_data_source *s, const char *m)
{ struct cl *c = d; (void)s; snprintf(c->dsrc.target, sizeof c->dsrc.target, "%s", m ? m : ""); }
static void ds_send(void *d, struct wl_data_source *s, const char *m, int32_t fd)
{ (void)d; (void)s; (void)m; if (write(fd, "hello", 5) < 0) {} close(fd); }
static void ds_cancelled(void *d, struct wl_data_source *s) { (void)s; ((struct cl *)d)->dsrc.cancelled++; }
static void ds_dropped(void *d, struct wl_data_source *s) { (void)s; ((struct cl *)d)->dsrc.dropped++; }
static void ds_finished(void *d, struct wl_data_source *s) { (void)s; ((struct cl *)d)->dsrc.finished++; }
static void ds_action(void *d, struct wl_data_source *s, uint32_t a) { (void)s; ((struct cl *)d)->dsrc.action = a; }
static const struct wl_data_source_listener ds_l = { ds_target, ds_send, ds_cancelled, ds_dropped, ds_finished, ds_action };

#define ACT_COPY WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY
#define ACT_MOVE WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE

/* Press on the page and start dragging from it. */
static struct wl_data_source *dd_begin(struct cl *c, struct wl_surface *origin, int x, int y, uint32_t serial_delta)
{
    ask(c, M_MOVE, x, y);
    ask(c, M_BUTTON, 0x110, 1);
    struct wl_data_source *src = wl_data_device_manager_create_data_source(c->ddm);
    wl_data_source_add_listener(src, &ds_l, c);
    wl_data_source_offer(src, "text/plain");
    wl_data_source_set_actions(src, ACT_COPY | ACT_MOVE);
    memset(&c->dsrc, 0, sizeof c->dsrc);
    c->dnd.enters = c->dnd.leaves = c->dnd.motions = c->dnd.drops = 0;
    wl_data_device_start_drag(c->ddev, src, origin, NULL, c->btn_serial + serial_delta);
    wl_display_roundtrip(c->dpy);
    return src;
}

static void sc_dnd(struct cl *c)
{
    if (!c->ddm || !c->seat) { printf("      no data device manager\n"); c->failed = 1; return; }
    c->ddev = wl_data_device_manager_get_data_device(c->ddm, c->seat);
    wl_data_device_add_listener(c->ddev, &dd_l, c);
    img root = img_solid(100, 80, GREY), page = img_solid(60, 40, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub P = mksub(c, t->s, 10, 10, &page, XRGB, 1);
    wl_surface_commit(t->s);
    ask(c, M_FOCUS, 0, 0);

    /* a drag that quotes the wrong serial is refused */
    wl_display_roundtrip(c->dpy);
    struct wl_data_source *bad = dd_begin(c, P.s, 15, 15, 1);
    check(c, c->dsrc.cancelled == 1 && c->dnd.enters == 0, "a drag with a serial nobody sent was not refused");
    (void)bad;
    ask(c, M_BUTTON, 0x110, 0);

    /* a real one, started from the page */
    struct wl_data_source *src = dd_begin(c, P.s, 15, 15, 0);
    check(c, c->dsrc.cancelled == 0, "a drag that should run was cancelled");
    check(c, c->pe.in == NULL, "the origin was not told the pointer left when the drag began");
    check(c, c->dnd.enters == 1 && c->dnd.in == P.s && c->dnd.ex == 5 && c->dnd.ey == 5,
          "the surface under the pointer was not entered at once, in its own coordinates");
    check(c, !strcmp(c->dnd.mime, "text/plain"), "the offer did not list the source's type");
    wl_data_offer_accept(c->dnd.offer, c->dnd.serial, "text/plain");
    wl_data_offer_set_actions(c->dnd.offer, ACT_COPY | ACT_MOVE, ACT_MOVE);
    wl_display_roundtrip(c->dpy);
    check(c, !strcmp(c->dsrc.target, "text/plain"), "the source was not told what the target accepts");
    check(c, c->dsrc.action == ACT_MOVE && c->dnd.offer_action == ACT_MOVE, "the action was not settled as move");

    ask(c, M_MOVE, 20, 20);
    check(c, c->dnd.motions >= 1 && c->dnd.mx == 10 && c->dnd.my == 10, "drag motion was not (10,10) in the page");
    int moved_enters = c->dnd.enters;
    ask(c, M_MOVE, 5, 5);                            /* the frame around the page */
    check(c, c->dnd.leaves == 1 && c->dnd.enters == moved_enters + 1 && c->dnd.in == t->s,
          "leaving the page for the frame was not leave + enter on the toplevel");
    check(c, c->dsrc.target[0] == 0, "the source was still told something was accepted after the target changed");
    ask(c, M_MOVE, 30, 30);                          /* and back on the page */
    check(c, c->dnd.in == P.s && c->dnd.ex == 20 && c->dnd.ey == 20, "back on the page: not entered at (20,20)");
    wl_data_offer_accept(c->dnd.offer, c->dnd.serial, "text/plain");
    wl_data_offer_set_actions(c->dnd.offer, ACT_COPY, ACT_COPY);
    wl_display_roundtrip(c->dpy);
    check(c, c->dsrc.action == ACT_COPY, "the action was not re-settled as copy");

    int pm = c->pe.motions, pb = c->pe.buttons;
    ask(c, M_MOVE, 31, 31);
    check(c, c->pe.motions == pm, "the pointer reached a program while a drag was running");
    ask(c, M_BUTTON, 0x110, 0);                      /* let go: drop */
    check(c, c->pe.buttons == pb, "the release reached the program instead of ending the drag");
    check(c, c->dnd.drops == 1 && c->dsrc.dropped == 1, "the drop was not delivered to both ends");
    int fds[2];
    if (pipe(fds) == 0) {
        wl_data_offer_receive(c->dnd.offer, "text/plain", fds[1]);
        close(fds[1]);
        wl_display_roundtrip(c->dpy);
        char buf[16] = {0};
        ssize_t n = read(fds[0], buf, sizeof buf - 1);
        close(fds[0]);
        check(c, n == 5 && !strcmp(buf, "hello"), "the data did not come through the pipe");
    }
    check(c, c->dsrc.finished == 0, "the source was told finished before the target said so");
    wl_data_offer_finish(c->dnd.offer);
    wl_display_roundtrip(c->dpy);
    check(c, c->dsrc.finished == 1, "the source was not told the drag finished");
    wl_data_offer_destroy(c->dnd.offer); c->dnd.offer = NULL;
    ask(c, M_MOVE, 32, 32);
    check(c, c->pe.in == P.s, "the pointer was not handed back to the page after the drop");
    wl_data_source_destroy(src);

    /* nobody accepts: cancelled, and the target is told it left */
    src = dd_begin(c, P.s, 33, 33, 0);
    ask(c, M_MOVE, 34, 34);
    ask(c, M_BUTTON, 0x110, 0);
    check(c, c->dsrc.cancelled == 1 && c->dnd.drops == 0, "a drag nobody accepted was dropped instead of cancelled");
    check(c, c->dnd.leaves >= 1, "the target was not told the drag left");
    wl_data_source_destroy(src);

    /* Escape */
    ask(c, M_MOVE, 35, 35);
    src = dd_begin(c, P.s, 35, 35, 0);
    ask(c, M_KEY, 1, 1);
    check(c, c->dsrc.cancelled == 1, "Escape did not cancel the drag");
    ask(c, M_KEY, 1, 0);
    ask(c, M_BUTTON, 0x110, 0);
    ask(c, M_MOVE, 36, 36);
    check(c, c->pe.in == P.s, "after Escape the pointer was not given back");
    wl_data_source_destroy(src);
}

/* 32. A drop whose target never says finish used to block every later drag, for every program, for ever. */
static void sc_dnd_hung(struct cl *c)
{
    if (!c->ddm || !c->seat) { printf("      no data device manager\n"); c->failed = 1; return; }
    c->ddev = wl_data_device_manager_get_data_device(c->ddm, c->seat);
    wl_data_device_add_listener(c->ddev, &dd_l, c);
    img root = img_solid(100, 80, GREY), page = img_solid(60, 40, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub P = mksub(c, t->s, 10, 10, &page, XRGB, 1);
    wl_surface_commit(t->s);
    ask(c, M_FOCUS, 0, 0);
    wl_display_roundtrip(c->dpy);

    struct wl_data_source *src = dd_begin(c, P.s, 15, 15, 0);
    wl_data_offer_accept(c->dnd.offer, c->dnd.serial, "text/plain");
    wl_data_offer_set_actions(c->dnd.offer, ACT_COPY, ACT_COPY);
    wl_display_roundtrip(c->dpy);
    ask(c, M_BUTTON, 0x110, 0);                      /* the drop */
    check(c, c->dnd.drops == 1, "setup: the drop was not delivered");
    int cancelled = c->dsrc.cancelled;
    usleep(500 * 1000);                              /* the target never says finish */
    ask(c, M_MOVE, 20, 20);
    check(c, c->dsrc.cancelled == cancelled + 1, "the source was not told when the target never finished");
    wl_data_source_destroy(src);
    struct wl_data_source *again = dd_begin(c, P.s, 21, 21, 0);             /* somebody else's drag */
    check(c, c->dsrc.cancelled == 0 && c->dnd.enters == 1, "a drag was refused because an earlier drop was never finished");   /* (dd_begin counts afresh) */
    wl_data_source_destroy(again);
}

/* 25. The pointer's picture. A program under the pointer may set a picture
 * of its own (wl_pointer.set_cursor: pixels, a hotspot, attach offsets move
 * it), hide the pointer (null surface), or name a shape
 * (wp_cursor_shape_v1) for the shell to draw. Only the program the pointer
 * is on, quoting its latest enter, may; and the picture is gone as soon as
 * the pointer enters or leaves anything. What the shell is handed is the
 * program's own pixels, byte for byte. */
static void ask_cursor(struct cl *c, int kind, int hx, int hy, int shape, const img *im)
{
    wl_display_roundtrip(c->dpy);
    msg m = { M_CURSOR, kind, hx, hy, shape };
    uint8_t reply = 1;
    int ok = xfer(c->chan, &m, sizeof m, 1) == 0;
    if (ok && kind == AURWL_CURSOR_IMAGE) {
        int dim[2] = { im->w, im->h };
        ok = xfer(c->chan, dim, sizeof dim, 1) == 0 &&
             xfer(c->chan, im->px, (size_t)im->w * im->h * 4, 1) == 0;
    }
    if (!ok || xfer(c->chan, &reply, 1, 0) < 0) exit(97);
    if (reply) c->failed = 1;
}

static img pointer_img(int w, int h, uint32_t seed)
{
    img m = img_new(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t a = (seed + (uint32_t)(x * 40 + y * 17)) & 255;       /* premultiplied: colour <= alpha */
            m.px[y * w + x] = (a << 24) | ((a * (uint32_t)((x * 70) & 255) / 255) << 16) |
                              ((a * (uint32_t)((y * 90) & 255) / 255) << 8) | (a / 3);
        }
    return m;
}

static void sc_cursor(struct cl *c)
{
    if (!c->ptr) { printf("      no pointer\n"); c->failed = 1; return; }
    img root = img_solid(100, 80, GREY), page = img_solid(60, 40, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub P = mksub(c, t->s, 10, 10, &page, XRGB, 1);
    wl_surface_commit(t->s);
    ask(c, M_FOCUS, 0, 0);
    ask_cursor(c, AURWL_CURSOR_DEFAULT, 0, 0, 0, NULL);          /* nobody is under the pointer */
    ask(c, M_MOVE, 15, 15);                                      /* on the page */
    ask_cursor(c, AURWL_CURSOR_DEFAULT, 0, 0, 0, NULL);          /* entered, nothing asked: the shell's arrow */

    img a = pointer_img(6, 5, 90), b = pointer_img(6, 5, 200);
    struct wl_surface *cs = wl_compositor_create_surface(c->comp);
    wl_pointer_set_cursor(c->ptr, c->enter_serial, cs, 2, 1);    /* a surface with no picture yet */
    wl_surface_commit(cs);
    ask_cursor(c, AURWL_CURSOR_DEFAULT, 0, 0, 0, NULL);          /* ... is not a hidden pointer */
    shmbuf ba = mkbuf(c, &a, ARGB);
    wl_surface_attach(cs, ba.buf, 0, 0);
    wl_surface_damage(cs, 0, 0, 6, 5);
    wl_surface_commit(cs);
    ask_cursor(c, AURWL_CURSOR_IMAGE, 2, 1, 0, &a);              /* the program's own pixels, hotspot (2,1) */

    shmbuf bb = mkbuf(c, &b, ARGB);                              /* the next frame of an animated cursor */
    wl_surface_attach(cs, bb.buf, 0, 0);
    wl_surface_damage(cs, 0, 0, 6, 5);
    wl_surface_commit(cs);
    ask_cursor(c, AURWL_CURSOR_IMAGE, 2, 1, 0, &b);

    shmbuf bc = mkbuf(c, &a, ARGB);                              /* attached 1 left and 2 up: the hotspot moves the other way */
    wl_surface_attach(cs, bc.buf, -1, -2);
    wl_surface_damage(cs, 0, 0, 6, 5);
    wl_surface_commit(cs);
    ask_cursor(c, AURWL_CURSOR_IMAGE, 3, 3, 0, &a);

    wl_pointer_set_cursor(c->ptr, c->enter_serial - 1, NULL, 0, 0);    /* a serial that is not the enter's: ignored */
    ask_cursor(c, AURWL_CURSOR_IMAGE, 3, 3, 0, &a);
    wl_pointer_set_cursor(c->ptr, c->enter_serial, NULL, 0, 0);        /* hide it */
    ask_cursor(c, AURWL_CURSOR_HIDDEN, 0, 0, 0, NULL);
    wl_pointer_set_cursor(c->ptr, c->enter_serial, cs, 1, 1);
    ask_cursor(c, AURWL_CURSOR_IMAGE, 1, 1, 0, &a);

    ask(c, M_MOVE, 5, 5);                                        /* onto the frame: a new enter, the picture is gone */
    ask_cursor(c, AURWL_CURSOR_DEFAULT, 0, 0, 0, NULL);
    wl_pointer_set_cursor(c->ptr, c->enter_serial - 1, cs, 0, 0);      /* the OLD enter's serial: ignored */
    ask_cursor(c, AURWL_CURSOR_DEFAULT, 0, 0, 0, NULL);
    wl_pointer_set_cursor(c->ptr, c->enter_serial, cs, 0, 0);
    ask_cursor(c, AURWL_CURSOR_IMAGE, 0, 0, 0, &a);

    ask(c, M_LEAVE, 0, 0);                                       /* off every window (the shell passes none) */
    ask_cursor(c, AURWL_CURSOR_DEFAULT, 0, 0, 0, NULL);
    (void)P;
}

/* 26. A program that is a moment behind while windows are moved about.
 * Two toplevels from one client, each with a page in a subsurface, and the
 * compositor reconfigures them over and over (the shell does, every frame
 * a window's size or focus differs) before the client has read any of it.
 * It then answers every one, in order, as Firefox and GTK do. Every
 * serial was sent, so none may be refused: this disconnected Firefox with
 * "no configure with serial N was sent" when a second window opened.
 * An answer that names a serial never sent is refused all the same --
 * long after the oldest ones have been forgotten. */
static void slow_storm(struct cl *c, top **pa)
{
    img root = img_solid(80, 60, GREY), page = img_solid(40, 30, GREEN);
    top *a = mktop(c, &root, XRGB), *b = mktop(c, &root, XRGB);
    sub pa_ = mksub(c, a->s, 12, 9, &page, XRGB, 1), pb = mksub(c, b->s, 12, 9, &page, XRGB, 1);
    wl_surface_commit(a->s); wl_surface_commit(b->s);
    int a0 = a->count, b0 = b->count;
    for (int k = 0; k < 12; k++) {                  /* a dozen each, interleaved: a second of Firefox loading a page */
        poke(c, M_CONF, 0, 300 + k, 200, 1);
        poke(c, M_CONF, 1, 310 + k, 210, 1);
    }
    wl_display_roundtrip(c->dpy);                    /* now it reads, and answers each one in turn */
    check(c, a->count - a0 >= 12 && b->count - b0 >= 12, "the client was not sent a configure for every size");
    check(c, wl_display_get_error(c->dpy) == 0, "a configure the client answered in order was refused (12 waiting)");
    for (int k = 0; k < 150; k++) {                  /* and far more than the compositor remembers */
        poke(c, M_CONF, 0, 400 + k, 200, 1);
        poke(c, M_CONF, 1, 420 + k, 210, 1);
    }
    wl_display_roundtrip(c->dpy);
    check(c, a->count - a0 >= 150 && b->count - b0 >= 150, "the client was not sent 150 configures");
    check(c, wl_display_get_error(c->dpy) == 0, "a configure the client answered in order was refused (150 waiting)");
    img want = img_solid(80, 60, GREY);
    img_over(&want, 12, 9, &page);
    expect(c, &want);                               /* and the pages are still on both windows */
    (void)pa_; (void)pb;
    *pa = a;
}
static void wrong_serial(struct cl *c, top *a, uint32_t serial, const char *what)
{
    xdg_surface_ack_configure(a->xs, serial);
    wl_display_roundtrip(c->dpy);
    const struct wl_interface *iface = NULL; uint32_t id = 0;
    uint32_t code = wl_display_get_protocol_error(c->dpy, &iface, &id);
    check(c, iface == &xdg_surface_interface && code == XDG_SURFACE_ERROR_INVALID_SERIAL, what);
}
static void sc_slow_client(struct cl *c)
{
    top *a; slow_storm(c, &a);
    wrong_serial(c, a, 0, "an answer from before anything was sent was accepted (after the oldest were forgotten)");
}
/* 29. A menu is never given the keyboard. Pressing inside one used to move the keyboard to it, and when it
 * closed the keyboard was NOBODY's: every key was dropped until she clicked a window again. */
typedef struct { struct wl_surface *s; struct xdg_surface *xs; struct xdg_popup *pp; int configured; } popup;
static void pop_xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{ popup *p = d; xdg_surface_ack_configure(xs, serial); p->configured = 1; }
static const struct xdg_surface_listener pop_xs_l = { pop_xs_configure };
static void pop_configure(void *d, struct xdg_popup *p, int32_t x, int32_t y, int32_t w, int32_t h)
{ (void)d; (void)p; (void)x; (void)y; (void)w; (void)h; }
static void pop_done(void *d, struct xdg_popup *p) { (void)d; (void)p; }
static const struct xdg_popup_listener pop_l = { .configure = pop_configure, .popup_done = pop_done };
static popup *mkpopup(struct cl *c, top *parent, const img *m)
{
    popup *p = calloc(1, sizeof *p);
    p->s = wl_compositor_create_surface(c->comp);
    p->xs = xdg_wm_base_get_xdg_surface(c->wm, p->s);
    xdg_surface_add_listener(p->xs, &pop_xs_l, p);
    struct xdg_positioner *pos = xdg_wm_base_create_positioner(c->wm);
    xdg_positioner_set_size(pos, m->w, m->h);
    xdg_positioner_set_anchor_rect(pos, 2, 2, 1, 1);
    p->pp = xdg_surface_get_popup(p->xs, parent->xs, pos);
    xdg_popup_add_listener(p->pp, &pop_l, p);
    xdg_positioner_destroy(pos);
    wl_surface_commit(p->s);
    for (int i = 0; i < 20 && !p->configured; i++) wl_display_roundtrip(c->dpy);
    shmbuf b = mkbuf(c, m, XRGB);
    wl_surface_attach(p->s, b.buf, 0, 0);
    wl_surface_damage_buffer(p->s, 0, 0, m->w, m->h);
    wl_surface_commit(p->s);
    wl_display_roundtrip(c->dpy);
    return p;
}
static void sc_popup_focus(struct cl *c)
{
    img root = img_solid(80, 60, GREY), menu = img_solid(30, 20, GREEN);
    top *t = mktop(c, &root, XRGB);
    ask(c, M_FOCUS, 0, 0);
    ask(c, M_FOCUSIS, 1, 0);
    popup *p = mkpopup(c, t, &menu);
    ask(c, M_FOCUSPOP, 0, 0);                        /* the press inside the menu */
    ask(c, M_FOCUSIS, 1, 0);                         /* the window still has the keyboard */
    xdg_popup_destroy(p->pp);                        /* the item was chosen: the menu goes */
    xdg_surface_destroy(p->xs);
    wl_surface_destroy(p->s);
    wl_display_roundtrip(c->dpy);
    ask(c, M_FOCUSIS, 1, 0);                         /* ...and typing still works (focus was NULL here, once) */
}

/* 30. No buffer may be bigger than the machine will hold. Each side was bounded and the product was not: a
 * 16384 x 16384 buffer from a sparse pool is a gigabyte of store, copied whole on the first commit. The
 * window keeps what it had, and a big buffer that is allowed still works. */
static void sc_huge_buffer(struct cl *c)
{
    img small = img_solid(40, 30, RED);
    top *t = mktop(c, &small, XRGB);
    const int W = 8192, H = 5120;                       /* 42 million pixels: over the limit (33 million) */
    size_t stride = (size_t)W * 4, n = stride * (size_t)H;
    int fd = memfd_create("wltest-sub-huge", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)n) < 0) { perror("memfd"); exit(98); }
    struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, (int)n);
    close(fd);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, W, H, (int)stride, XRGB);
    wl_shm_pool_destroy(pool);
    wl_surface_attach(t->s, b, 0, 0);
    wl_surface_damage_buffer(t->s, 0, 0, 16, 16);
    wl_surface_commit(t->s);
    expect(c, &small);                                   /* refused: what it had is still what it shows */
    wl_buffer_destroy(b);
    img big = img_solid(1200, 800, GREEN);              /* a big one that is allowed */
    shmbuf ok = mkbuf(c, &big, XRGB);
    wl_surface_attach(t->s, ok.buf, 0, 0);
    wl_surface_damage_buffer(t->s, 0, 0, 1200, 800);
    wl_surface_commit(t->s);
    expect(c, &big);
}

/* 31. She starts a selection, drags off every window and lets go there. The program must hear the button come
 * up (it was told "leave" with the button held and never "up": the selection kept growing with the pointer). */
static void sc_release_outside(struct cl *c)
{
    img root = img_solid(100, 80, GREY);
    top *t = mktop(c, &root, XRGB);
    ask(c, M_FOCUS, 0, 0);
    ask(c, M_MOVE, 30, 30);
    int b0 = c->pe.buttons;
    ask(c, M_BUTTON, 0x110, 1);
    check(c, c->pe.buttons == b0 + 1, "setup: the press did not arrive");
    ask(c, M_LEAVE, 0, 0);                           /* the pointer is off every window, the button still down */
    check(c, c->pe.leaves == 0, "the program was told the pointer left while its button was held");
    ask(c, M_BUTTON, 0x110, 0);                      /* ...and she lets go out there */
    check(c, c->pe.buttons == b0 + 2, "the release was never delivered: the program still thinks the button is down");
    check(c, c->pe.leaves == 1, "the program was not told the pointer left once the button was up");
    ask(c, M_MOVE, 40, 40);                          /* she comes back: a fresh enter, and nothing is held */
    check(c, c->pe.in == t->s, "the pointer did not come back into the window");
    int bb = c->pe.buttons;
    ask(c, M_BUTTON, 0x110, 1);
    ask(c, M_BUTTON, 0x110, 0);
    check(c, c->pe.buttons == bb + 2, "a click after that did not arrive as a press and a release");
}

/* 33. A program may have only so many windows at once (the shell walks the window list on every pointer movement
 * and every frame, and each step of it walks the surface list). */
static void sc_window_cap(struct cl *c)
{
    img root = img_solid(20, 20, GREY);
    top *keep = mktop(c, &root, XRGB);
    (void)keep;
    for (int i = 1; i < 128; i++) {                  /* 127 more: 128 in all is allowed */
        struct wl_surface *s = wl_compositor_create_surface(c->comp);
        struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(c->wm, s);
        xdg_surface_get_toplevel(xs);
    }
    wl_display_roundtrip(c->dpy);
    check(c, wl_display_get_error(c->dpy) == 0, "128 windows from one program were refused");
    struct wl_surface *s = wl_compositor_create_surface(c->comp);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(c->wm, s);
    xdg_surface_get_toplevel(xs);                    /* the 129th */
    wl_display_roundtrip(c->dpy);
    const struct wl_interface *iface = NULL; uint32_t id = 0;
    uint32_t code = wl_display_get_protocol_error(c->dpy, &iface, &id);
    check(c, iface == &xdg_surface_interface && code == XDG_WM_BASE_ERROR_ROLE, "a 129th window from one program was allowed");
}

static void sc_slow_future(struct cl *c)
{
    top *a; slow_storm(c, &a);
    wrong_serial(c, a, a->serial + 1, "an answer to a configure not yet sent was accepted (after the oldest were forgotten)");
}

/* 27. The serial must be this window's own, and one that was sent. */
static void sc_serial_foreign(struct cl *c)
{
    img root = img_solid(80, 60, GREY);
    top *a = mktop(c, &root, XRGB), *b = mktop(c, &root, XRGB);
    poke(c, M_CONF, 0, 321, 200, 1); poke(c, M_CONF, 1, 322, 200, 1);
    wl_display_roundtrip(c->dpy);
    check(c, a->serial != b->serial && wl_display_get_error(c->dpy) == 0, "setup: two windows, two serials");
    top *older = a->serial < b->serial ? a : b, *newer = older == a ? b : a;
    wrong_serial(c, newer, older->serial, "one window's serial was accepted for another");   /* a real serial, someone else's */
}
static void sc_serial_invented(struct cl *c)
{
    img root = img_solid(80, 60, GREY);
    top *a = mktop(c, &root, XRGB);
    poke(c, M_CONF, 0, 321, 200, 1);
    wl_display_roundtrip(c->dpy);
    check(c, wl_display_get_error(c->dpy) == 0, "setup: the window was configured");
    wrong_serial(c, a, a->serial + 1, "an answer to a configure that was never sent was accepted");   /* just past the newest */
}

static void sc_cursor_shape(struct cl *c)
{
    if (!c->csmgr || !c->ptr) { printf("      no cursor shape global\n"); c->failed = 1; return; }
    img root = img_solid(100, 80, GREY), page = img_solid(60, 40, GREEN);
    top *t = mktop(c, &root, XRGB);
    sub P = mksub(c, t->s, 10, 10, &page, XRGB, 1);
    wl_surface_commit(t->s);
    ask(c, M_FOCUS, 0, 0);
    struct wp_cursor_shape_device_v1 *dev = wp_cursor_shape_manager_v1_get_pointer(c->csmgr, c->ptr);
    ask(c, M_MOVE, 15, 15);
    ask_cursor(c, AURWL_CURSOR_DEFAULT, 0, 0, 0, NULL);
    wp_cursor_shape_device_v1_set_shape(dev, c->enter_serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
    ask_cursor(c, AURWL_CURSOR_SHAPE, 0, 0, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER, NULL);
    wp_cursor_shape_device_v1_set_shape(dev, c->enter_serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
    ask_cursor(c, AURWL_CURSOR_SHAPE, 0, 0, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT, NULL);
    wp_cursor_shape_device_v1_set_shape(dev, c->enter_serial + 1, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT);  /* wrong serial */
    ask_cursor(c, AURWL_CURSOR_SHAPE, 0, 0, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT, NULL);
    ask(c, M_MOVE, 5, 5);                                        /* another enter: gone */
    ask_cursor(c, AURWL_CURSOR_DEFAULT, 0, 0, 0, NULL);
    wp_cursor_shape_device_v1_set_shape(dev, c->enter_serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE);
    ask_cursor(c, AURWL_CURSOR_SHAPE, 0, 0, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE, NULL);

    /* ANOTHER program cannot change it, even knowing the serial */
    struct wl_display *d2 = wl_display_connect(NULL);
    if (d2) {
        struct cl o = { .dpy = d2, .chan = -1 };
        struct wl_registry *r2 = wl_display_get_registry(d2);
        wl_registry_add_listener(r2, &reg_l, &o);
        wl_display_roundtrip(d2);
        if (o.seat && o.csmgr && o.comp) {
            struct wl_pointer *p2 = wl_seat_get_pointer(o.seat);
            struct wp_cursor_shape_device_v1 *dev2 = wp_cursor_shape_manager_v1_get_pointer(o.csmgr, p2);
            wp_cursor_shape_device_v1_set_shape(dev2, c->enter_serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT);
            wl_pointer_set_cursor(p2, c->enter_serial, NULL, 0, 0);
            wl_display_roundtrip(d2);
            wl_display_roundtrip(c->dpy);
            ask_cursor(c, AURWL_CURSOR_SHAPE, 0, 0, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE, NULL);
        } else { printf("      the second program found no globals\n"); c->failed = 1; }
    }

    /* a shape that is not one is a protocol error, and ends that program only */
    wp_cursor_shape_device_v1_set_shape(dev, c->enter_serial, 99);
    wl_display_roundtrip(c->dpy);
    uint32_t code = 0; const struct wl_interface *iface = NULL; uint32_t id = 0;
    int err = wl_display_get_protocol_error(c->dpy, &iface, &id);
    code = (uint32_t)err;
    check(c, err == WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE && iface == &wp_cursor_shape_device_v1_interface,
          "a shape that does not exist was not refused with invalid_shape");
    (void)code; (void)P;
}

static const struct { const char *name; void (*fn)(struct cl *); } SC[] = {
    { "plain",            sc_plain },
    { "page",             sc_page },
    { "sync",             sc_sync },
    { "snapshot",         sc_snapshot },
    { "nested-sync",      sc_nested_sync },
    { "effective-sync",   sc_effective_sync },
    { "set-desync",       sc_set_desync },
    { "zorder",           sc_zorder },
    { "alpha",            sc_alpha },
    { "clip",             sc_clip },
    { "viewport",         sc_viewport },
    { "stretched-root",   sc_stretched_root },
    { "csd-viewport",     sc_csd_viewport },
    { "damage",           sc_damage },
    { "unmap",            sc_unmap },
    { "pointer",          sc_pointer },
    { "pointer-nested",   sc_pointer_nested },
    { "pointer-moves",    sc_pointer_moves },
    { "frame",            sc_frame },
    { "geometry",         sc_geometry },
    { "wreck",            sc_wreck },
    { "textinput",        sc_textinput },
    { "idle",             sc_idle },
    { "budget",           sc_budget },
    { "dnd",              sc_dnd },
    { "cursor",           sc_cursor },
    { "cursor-shape",     sc_cursor_shape },
    { "slow-client",      sc_slow_client },
    { "slow-future",      sc_slow_future },
    { "popup-focus",      sc_popup_focus },
    { "huge-buffer",      sc_huge_buffer },
    { "release-outside",  sc_release_outside },
    { "dnd-hung",         sc_dnd_hung },
    { "window-cap",       sc_window_cap },
    { "serial-foreign",   sc_serial_foreign },
    { "serial-invented",  sc_serial_invented },
};
#define NSC ((int)(sizeof SC / sizeof SC[0]))

static int be_client(int which, int chan)
{
    /* The child leaves with _exit(), which does not flush stdio: what a
     * failing check says would be lost whenever stdout is a pipe. */
    setvbuf(stdout, NULL, _IONBF, 0);
    struct cl c = { .chan = chan };
    c.dpy = wl_display_connect(NULL);
    if (!c.dpy) { printf("      cannot connect\n"); return 95; }
    struct wl_registry *reg = wl_display_get_registry(c.dpy);
    wl_registry_add_listener(reg, &reg_l, &c);
    wl_display_roundtrip(c.dpy);
    if (!c.comp || !c.sub || !c.shm || !c.wm) { printf("      missing a global\n"); return 94; }
    if (c.seat) { c.ptr = wl_seat_get_pointer(c.seat); wl_pointer_add_listener(c.ptr, &ptr_l, &c); }
    wl_display_roundtrip(c.dpy);
    SC[which].fn(&c);
    wl_display_roundtrip(c.dpy);
    msg m = { M_END, c.failed, 0, 0, 0 };
    uint8_t reply;
    if (xfer(chan, &m, sizeof m, 1) == 0) (void)xfer(chan, &reply, 1, 0);
    return c.failed ? 1 : 0;
}

static int run_one(int which)
{
    char dir[] = "/tmp/wltest-sub-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    setenv("XDG_RUNTIME_DIR", dir, 1);
    aurwl *c = aurwl_create(1280, 720, 60000);
    if (!c) { printf("  %-18s  compositor did not start\n", SC[which].name); return 1; }
    setenv("WAYLAND_DISPLAY", aurwl_socket(c), 1);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) { perror("socketpair"); return 1; }
    fflush(stdout);
    pid_t kid = fork();
    if (kid < 0) { perror("fork"); return 1; }
    if (kid == 0) {
        close(sv[0]);
        _exit(be_client(which, sv[1]));
    }
    close(sv[1]);
    fails_here = 0;
    printf("  %-18s\n", SC[which].name);
    int cres = 0;
    int bad = serve(c, sv[0], kid, &cres);
    close(sv[0]);
    aurwl_destroy(c);
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd)) {}
    int ok = !bad && !fails_here && cres == 0;
    printf("  %-18s  %s\n", SC[which].name, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "-l")) {
        for (int i = 0; i < NSC; i++) printf("%2d %s\n", i, SC[i].name);
        return 0;
    }
    int only = -1;
    if (argc > 1) {
        for (int i = 0; i < NSC; i++) if (!strcmp(argv[1], SC[i].name)) only = i;
        if (only < 0 && argv[1][0] >= '0' && argv[1][0] <= '9') only = atoi(argv[1]);
        if (only < 0 || only >= NSC) { fprintf(stderr, "no such scenario: %s\n", argv[1]); return 2; }
    }
    signal(SIGPIPE, SIG_IGN);
    setenv("AUROS_NO_XWAYLAND", "1", 1);        /* these scenarios are about Wayland; no X sockets in /tmp */
    setenv("AUROS_DRAG_FINISH_MS", "300", 1);   /* a drop is given up on after this long (the real one: 10 s) */
    int failed = 0, ran = 0;
    for (int i = 0; i < NSC; i++) {
        if (only >= 0 && i != only) continue;
        failed += run_one(i);
        ran++;
    }
    printf("\n%s: %d of %d scenarios passed\n", failed ? "FAIL" : "PASS", ran - failed, ran);
    return failed ? 1 : 0;
}
