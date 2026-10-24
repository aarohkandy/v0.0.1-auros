/* wltest-x.c — the X window manager in aurwl (src/aurwl/xwm.inc), against a
 * pretend X server.
 *
 * The real thing is Xwayland: an X server that is a Wayland client of the
 * compositor, with a second connection (-wm) on which the compositor is the
 * X window manager. This test plays BOTH halves of Xwayland by hand, so every
 * byte the window manager writes and every message it is sent is under the
 * test's control and can be checked:
 *
 *   - a pretend X server on the -wm connection: it answers the connection
 *     setup, InternAtom, GetProperty, QueryTree and GetWindowAttributes from
 *     a table, records every request the window manager makes (opcode and
 *     body), and sends events (CreateNotify, MapRequest, ConfigureRequest,
 *     PropertyNotify, ClientMessage, ...);
 *   - a pretend Xwayland on the Wayland side: a child process that makes
 *     wl_surfaces with known pixels on request, as Xwayland does for each X
 *     window, and reports their ids (the WL_SURFACE_ID the pretend server
 *     then sends the window manager).
 *
 * What is proved: the window manager takes over the screen (event mask on
 * the root, the EWMH check window); reads a window's title (Latin-1 and
 * UTF-8), class, hints and protocols before mapping it; maps it at (0,0);
 * turns it into one window of the shell, with the right title, app id and
 * pixels, only when its surface is known; honours size hints when the shell
 * resizes it and never asks twice for the same size; closes it politely (or
 * kills it when it cannot be asked); gives it the keyboard and takes the
 * keyboard away; answers a client's own resize and move requests; shows an
 * override-redirect window (a menu) as a popup of the window in use; follows
 * title changes; passes fullscreen, maximize and minimise requests to the
 * shell; forgets a destroyed window; and survives an X server that lies about
 * lengths, sequence numbers, strings, counts and window ids.
 *
 *   wltest-x            run every scenario
 *   wltest-x N | NAME   run one
 *   wltest-x -l         list them
 */
#define _GNU_SOURCE
/* The compositor, whole (so the test can reach its statics). tools/wltest-sub-mutants.py
 * points AURWL_SRC at a broken copy; a copy's xwm.inc is the one beside it. */
#ifndef AURWL_SRC
#define AURWL_SRC "../src/aurwl/aurwl.c"
#endif
#include AURWL_SRC

#include <poll.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <wayland-client.h>
#include <execinfo.h>

/* ── a pretend X server ──────────────────────────────────────────── */
#define FX_ROOT   0x2a
#define FX_BASE   0x00400000u
#define FX_MASK   0x001fffffu

typedef struct { uint8_t op, d1; uint16_t seq; size_t len; uint8_t body[512]; uint32_t dlen, hash; } req;   /* dlen/hash: a ChangeProperty's data, all of it */
typedef struct { uint32_t win, atom, type; int format; size_t n_bytes; uint8_t data[512]; } prop;

static struct {
    int      fd;
    uint8_t  in[(17u << 20) + 4096]; size_t in_n;      /* a 16 MiB clipboard request fits */
    int      setup_done;
    uint16_t seq;
    req      reqs[8192]; int n_reqs;
    uint32_t atoms[64];                      /* by index into XA_NAMES; 1000 + index */
    prop     props[64]; int n_props;
    int      garble;                         /* answer replies wrongly: 1 length, 2 sequence */
    int      mute;                           /* answer nothing */
    uint32_t stack[64]; int n_stack;         /* X's stacking order, bottom to top: created on top, moved only by ConfigureWindow */
} X;

static char dyn_names[16][64]; static int n_dyn;
static uint32_t dyn_atom(const char *name)
{
    for (int i = 0; i < n_dyn; i++) if (!strcmp(dyn_names[i], name)) return 3000u + (uint32_t)i;
    if (n_dyn >= 16) { fprintf(stderr, "wltest-x: too many dynamic atoms\n"); abort(); }
    snprintf(dyn_names[n_dyn], sizeof dyn_names[0], "%s", name);
    return 3000u + (uint32_t)n_dyn++;
}
static const char *dyn_name(uint32_t a) { return a >= 3000 && a - 3000 < (uint32_t)n_dyn ? dyn_names[a - 3000] : NULL; }

static uint32_t atom_id(const char *name)
{
    for (int i = 0; i < XA_COUNT; i++) if (!strcmp(name, XA_NAMES[i])) return 1000u + (uint32_t)i;
    if (!strcmp(name, "STRING")) return XP_STRING;
    if (!strcmp(name, "ATOM")) return XP_ATOM;
    if (!strcmp(name, "WINDOW")) return XP_WINDOW;
    if (!strcmp(name, "WM_NAME")) return XP_WM_NAME;
    if (!strcmp(name, "WM_CLASS")) return XP_WM_CLASS;
    if (!strcmp(name, "WM_HINTS")) return XP_WM_HINTS;
    if (!strcmp(name, "WM_NORMAL_HINTS")) return XP_WM_NORMAL_HINTS;
    if (!strcmp(name, "WM_TRANSIENT_FOR")) return XP_WM_TRANSIENT_FOR;
    return dyn_atom(name);                                   /* a mime type, say: made up on first use */
}

static void pump(int ms);

/* One thread plays both ends, so a write that would block has to let the
 * other end run: otherwise 300 small events fill the socket (each costs far
 * more than its 32 bytes in kernel memory) and the test waits for itself. */
static void fx_send(const void *b, size_t n)
{
    const uint8_t *p = b;
    while (n) {
        ssize_t k = write(X.fd, p, n);
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && errno == EAGAIN) { pump(1); continue; }
        if (k <= 0) return;
        p += k; n -= (size_t)k;
    }
}

static void fx_reply(uint8_t byte1, uint16_t seq, const void *extra, size_t extra_len, const uint8_t head[24])
{
    /* head: 24 bytes from offset 8 */
    size_t padded = (extra_len + 3) & ~(size_t)3;
    uint8_t *buf = calloc(1, 32 + padded);
    buf[0] = 1; buf[1] = byte1;
    xw16(buf + 2, X.garble == 2 ? (uint16_t)(seq + 7) : seq);
    xw32(buf + 4, X.garble == 1 ? 0x7fffff00u : (uint32_t)(padded / 4));
    if (head) memcpy(buf + 8, head, 24);
    if (extra_len) memcpy(buf + 32, extra, extra_len);
    fx_send(buf, 32 + padded);
    free(buf);
}

static prop *fx_find_prop(uint32_t win, uint32_t atom)
{
    for (int i = 0; i < X.n_props; i++) if (X.props[i].win == win && X.props[i].atom == atom) return &X.props[i];
    return NULL;
}
static void fx_setprop(uint32_t win, const char *name, const char *type, int format, const void *data, size_t n_bytes)
{
    prop *p = fx_find_prop(win, atom_id(name));
    if (!p) p = &X.props[X.n_props++];
    p->win = win; p->atom = atom_id(name); p->type = atom_id(type); p->format = format;
    p->n_bytes = n_bytes; memcpy(p->data, data, n_bytes);
}

static void fx_setup_reply(void)
{
    uint8_t b[8 + 4 * 32] = {0};
    size_t o = 0;
    b[0] = 1; xw16(b + 2, 11);
    uint8_t *body = b + 8;
    xw32(body + 0, 12000000); xw32(body + 4, FX_BASE); xw32(body + 8, FX_MASK); xw32(body + 12, 256);
    xw16(body + 16, 4); xw16(body + 18, 65535); body[20] = 1; body[21] = 1;
    memcpy(body + 32, "fake", 4);
    o = 32 + 4;
    body[o] = 24; body[o + 1] = 32; body[o + 2] = 32; o += 8;               /* one pixmap format */
    xw32(body + o, FX_ROOT); xw32(body + o + 4, 0x20); xw32(body + o + 8, 0xffffff);
    xw16(body + o + 20, 1280); xw16(body + o + 22, 800);
    xw32(body + o + 32, 0x21); body[o + 38] = 24;
    o += 40;
    xw16(b + 6, (uint16_t)(o / 4));
    fx_send(b, 8 + o);
}

/* Properties the window manager's own window carries for the clipboard, and an X program that owns CLIPBOARD. */
static struct { uint32_t win, atom, type; int format; uint8_t *data; size_t n; int used; } SP[4];
static struct {
    uint32_t targets[16]; int n_targets;
    uint8_t *text; size_t text_n;                    /* UTF8_STRING / STRING answer */
    uint8_t *img; size_t img_n; uint32_t img_atom;   /* an image, by its mime name */
    int incr;                                        /* send the image in chunks */
    int incr_active, incr_done; size_t incr_off; uint32_t incr_win, incr_prop;
} XO;
static void ev32(uint8_t type, const void *fields);
static void sp_set(uint32_t win, uint32_t atom, uint32_t type, int format, const void *data, size_t n)
{
    int i = 0;
    for (; i < 4; i++) if (SP[i].used && SP[i].win == win && SP[i].atom == atom) break;
    if (i == 4) for (i = 0; i < 4 && SP[i].used; i++) ;
    if (i == 4) abort();
    free(SP[i].data);
    SP[i].used = 1; SP[i].win = win; SP[i].atom = atom; SP[i].type = type; SP[i].format = format;
    SP[i].data = malloc(n + 1); if (n) memcpy(SP[i].data, data, n); SP[i].n = n;
}
static void fx_sel_notify(uint32_t requestor, uint32_t selection, uint32_t target, uint32_t prop)
{
    uint8_t f[28] = {0};
    xw32(f + 4, requestor); xw32(f + 8, selection); xw32(f + 12, target); xw32(f + 16, prop);
    ev32(31, f);
}
static void fx_prop_notify(uint32_t win, uint32_t atom, int state)
{
    uint8_t f[28] = {0};
    xw32(f, win); xw32(f + 4, atom); f[12] = (uint8_t)state;
    ev32(28, f);
}
/* The window manager asked the owner for `target`. */
static void fx_convert(uint32_t requestor, uint32_t selection, uint32_t target, uint32_t prop)
{
    if (target == atom_id("TARGETS")) {
        if (XO.n_targets < 0) { fx_sel_notify(requestor, selection, target, 0); return; }    /* an owner that will not say */
        sp_set(requestor, prop, XP_ATOM, 32, XO.targets, 4 * (size_t)XO.n_targets);
        fx_sel_notify(requestor, selection, target, prop);
    } else if ((target == atom_id("UTF8_STRING") || target == XP_STRING) && XO.text) {
        sp_set(requestor, prop, target, 8, XO.text, XO.text_n);
        fx_sel_notify(requestor, selection, target, prop);
    } else if (target == XO.img_atom && XO.img) {
        if (XO.incr) {
            uint32_t sz = (uint32_t)XO.img_n;
            sp_set(requestor, prop, atom_id("INCR"), 32, &sz, 4);
            XO.incr_active = 1; XO.incr_done = 0; XO.incr_off = 0; XO.incr_win = requestor; XO.incr_prop = prop;
        } else sp_set(requestor, prop, target, 8, XO.img, XO.img_n);
        fx_sel_notify(requestor, selection, target, prop);
    } else fx_sel_notify(requestor, selection, target, 0);
}
/* The window manager read (and so deleted) a property of its window: the owner's INCR goes on. */
static void fx_after_delete(uint32_t win, uint32_t atom)
{
    if (!XO.incr_active || win != XO.incr_win || atom != XO.incr_prop) return;
    if (XO.incr_off < XO.img_n) {
        size_t n = XO.img_n - XO.incr_off < 100000 ? XO.img_n - XO.incr_off : 100000;
        sp_set(win, atom, XO.img_atom, 8, XO.img + XO.incr_off, n);
        XO.incr_off += n;
        fx_prop_notify(win, atom, 0);
    } else if (!XO.incr_done) {
        sp_set(win, atom, XO.img_atom, 8, "", 0);              /* the empty chunk: that was all of it */
        XO.incr_done = 1;
        fx_prop_notify(win, atom, 0);
    } else XO.incr_active = 0;
}

/* One request arrived: remember it, and answer it if it asks a question. */
static void fx_request(const uint8_t *r, size_t len)
{
    if (X.n_reqs >= 8192) return;
    req *q = &X.reqs[X.n_reqs++];
    q->op = r[0]; q->d1 = r[1]; q->seq = ++X.seq; q->len = len - 4;
    memcpy(q->body, r + 4, q->len < sizeof q->body ? q->len : sizeof q->body);
    if (r[0] == 18 && q->len >= 20) {                     /* ChangeProperty: the whole of the data, summed */
        size_t dl = (size_t)xr32(r + 4 + 16) * (size_t)(r[4 + 12] / 8);
        if (dl > q->len - 20) dl = q->len - 20;
        q->dlen = (uint32_t)dl;
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < dl; i++) h = (h ^ r[4 + 20 + i]) * 16777619u;
        q->hash = h;
    }
    if (X.mute) return;
    const uint8_t *b = r + 4;
    uint8_t head[24] = {0};
    switch (r[0]) {
    case 16: {                                            /* InternAtom */
        uint16_t n = xr16(b);
        char name[64] = {0};
        memcpy(name, b + 4, n < 63 ? n : 63);
        xw32(head, atom_id(name));
        fx_reply(0, q->seq, NULL, 0, head);
        break;
    }
    case 12: {                                            /* ConfigureWindow: a restack moves the window in the pretend X stack */
        uint16_t mask = xr16(b + 4);
        if (mask & XC_STACK) {
            unsigned idx = (unsigned)__builtin_popcount(mask & (XC_X | XC_Y | XC_W | XC_H | XC_BORDER | XC_SIBLING));
            uint32_t mode = xr32(b + 8 + 4 * idx), win = xr32(b);
            int at = -1;
            for (int i = 0; i < X.n_stack; i++) if (X.stack[i] == win) at = i;
            if (at >= 0) {
                for (int i = at; i + 1 < X.n_stack; i++) X.stack[i] = X.stack[i + 1];
                if (mode == 0) X.stack[X.n_stack - 1] = win;                          /* Above */
                else { for (int i = X.n_stack - 1; i > 0; i--) X.stack[i] = X.stack[i - 1]; X.stack[0] = win; }   /* Below */
            }
        }
        break;
    }
    case 22:                                              /* SetSelectionOwner: XFixes tells every listener who owns it now */
        if (xr32(b + 4) == atom_id("CLIPBOARD")) {
            uint8_t f[28] = {0};
            xw32(f, FX_ROOT); xw32(f + 4, xr32(b)); xw32(f + 8, xr32(b + 4));
            ev32(87, f);
        }
        break;
    case 17: {                                            /* GetAtomName */
        const char *nm = dyn_name(xr32(b));
        if (!nm) { fx_reply(0, q->seq, NULL, 0, head); break; }
        xw16(head, (uint16_t)strlen(nm));
        fx_reply(0, q->seq, nm, strlen(nm), head);
        break;
    }
    case 24:                                              /* ConvertSelection: we are the owner */
        fx_convert(xr32(b), xr32(b + 4), xr32(b + 8), xr32(b + 12));
        break;
    case 20: {                                            /* GetProperty */
        uint32_t win = xr32(b), atom = xr32(b + 4);
        for (int i = 0; i < 4; i++) if (SP[i].used && SP[i].win == win && SP[i].atom == atom) {
            size_t unit = (size_t)(SP[i].format / 8);
            size_t off = (size_t)xr32(b + 12) * 4, lim = (size_t)xr32(b + 16) * 4;       /* as the protocol says: from here, at most this much */
            if (off > SP[i].n) off = SP[i].n;
            size_t send = SP[i].n - off < lim ? SP[i].n - off : lim;
            send -= send % unit;
            xw32(head, SP[i].type);
            xw32(head + 4, (uint32_t)(SP[i].n - off - send));                           /* bytes after */
            xw32(head + 8, (uint32_t)(send / unit));
            fx_reply((uint8_t)SP[i].format, q->seq, SP[i].data + off, send, head);
            if (r[1] && SP[i].n - off - send == 0) { SP[i].used = 0; fx_after_delete(win, atom); }   /* deleted only when all of it was read */
            return;
        }
        prop *p = fx_find_prop(win, atom);
        if (!p) { fx_reply(0, q->seq, NULL, 0, head); break; }
        xw32(head, p->type);
        xw32(head + 4, 0);
        xw32(head + 8, (uint32_t)(p->n_bytes / (size_t)(p->format / 8)));
        fx_reply((uint8_t)p->format, q->seq, p->data, p->n_bytes, head);
        break;
    }
    case 98: {                                            /* QueryExtension: Composite 140, BIG-REQUESTS 141, XFIXES 142 (events from 87) */
        uint16_t n = xr16(b);
        char name[32] = {0};
        memcpy(name, b + 4, n < 31 ? n : 31);
        head[0] = 1;
        head[1] = !strcmp(name, "BIG-REQUESTS") ? 141 : !strcmp(name, "XFIXES") ? 142 : 140;
        head[2] = !strcmp(name, "XFIXES") ? 87 : 0;
        fx_reply(0, q->seq, NULL, 0, head);
        break;
    }
    case 141:                                             /* BIG-REQUESTS Enable: the most a request may carry */
        xw32(head, 4u * 1024 * 1024);
        fx_reply(0, q->seq, NULL, 0, head);
        break;
    case 142:                                             /* XFIXES: QueryVersion answers; SelectSelectionInput is noted */
        if (r[1] == 0) { xw32(head, 5); xw32(head + 4, 0); fx_reply(0, q->seq, NULL, 0, head); }
        break;
    case 15: {                                            /* QueryTree: no children */
        xw32(head, FX_ROOT); xw32(head + 4, 0);
        fx_reply(0, q->seq, NULL, 0, head);
        break;
    }
    case 3: {                                             /* GetWindowAttributes */
        uint8_t full[32 + 12] = {0};
        full[0] = 1; xw16(full + 2, q->seq); xw32(full + 4, 3);
        full[26] = 0; full[27] = 0;
        fx_send(full, sizeof full);
        break;
    }
    default: break;
    }
}

static void fx_read_inner(void);
/* Not re-entrant: answering a request can have to wait for the window manager to read (pump), which must not start reading requests again. */
static void fx_read(void)
{
    static int busy;
    if (busy) return;
    busy = 1; fx_read_inner(); busy = 0;
}
static void fx_read_inner(void)
{
    ssize_t n = read(X.fd, X.in + X.in_n, sizeof X.in - X.in_n);
    if (n <= 0) return;
    X.in_n += (size_t)n;
    size_t off = 0;
    for (;;) {
        if (!X.setup_done) {
            if (X.in_n - off < 12) break;
            off += 12; X.setup_done = 1;
            fx_setup_reply();
            continue;
        }
        if (X.in_n - off < 4) break;
        size_t len = 4 * (size_t)xr16(X.in + off + 2);
        if (len == 0) {                                   /* BIG-REQUESTS: the real length follows, counting itself */
            if (X.in_n - off < 8) break;
            len = 4 * (size_t)xr32(X.in + off + 4);
            if (len < 8 || X.in_n - off < len) break;
            uint8_t *rr = X.in + off + 4;                  /* rebuild a plain header just before the body */
            rr[0] = X.in[off]; rr[1] = X.in[off + 1];
            fx_request(rr, len - 4);
            off += len;
            continue;
        }
        if (len < 4 || X.in_n - off < len) break;
        fx_request(X.in + off, len);
        off += len;
    }
    memmove(X.in, X.in + off, X.in_n - off);
    X.in_n -= off;
}

/* events the pretend server sends */
static void ev32(uint8_t type, const void *fields)
{
    uint8_t e[32] = {0};
    e[0] = type;
    if (fields) memcpy(e + 4, fields, 28);
    fx_send(e, 32);
}
static void ev_create(uint32_t win, int x, int y, int w, int h, int or_)
{
    uint8_t f[28] = {0};
    xw32(f, FX_ROOT); xw32(f + 4, win);
    xw16(f + 8, (uint16_t)x); xw16(f + 10, (uint16_t)y); xw16(f + 12, (uint16_t)w); xw16(f + 14, (uint16_t)h);
    f[18] = (uint8_t)or_;
    if (X.n_stack < 64) X.stack[X.n_stack++] = win;
    ev32(16, f);
}
/* Which window Xwayland would hand a click to, where every window covers the same place: the top one. */
static uint32_t fx_top(void) { return X.n_stack ? X.stack[X.n_stack - 1] : 0; }
static void ev_map_request(uint32_t win) { uint8_t f[28] = {0}; xw32(f, FX_ROOT); xw32(f + 4, win); ev32(20, f); }
static void ev_map_notify(uint32_t win, int or_) { uint8_t f[28] = {0}; xw32(f, win); xw32(f + 4, win); f[8] = (uint8_t)or_; ev32(19, f); }
static void ev_unmap(uint32_t win) { uint8_t f[28] = {0}; xw32(f, FX_ROOT); xw32(f + 4, win); ev32(18, f); }
static void ev_destroy(uint32_t win) { uint8_t f[28] = {0}; xw32(f, FX_ROOT); xw32(f + 4, win); ev32(17, f); }
static void ev_configure_request(uint32_t win, int x, int y, int w, int h, unsigned mask)
{
    uint8_t f[28] = {0};
    xw32(f, FX_ROOT); xw32(f + 4, win);
    xw16(f + 12, (uint16_t)x); xw16(f + 14, (uint16_t)y); xw16(f + 16, (uint16_t)w); xw16(f + 18, (uint16_t)h);
    xw16(f + 22, (uint16_t)mask);
    ev32(23, f);
}
static void ev_property(uint32_t win, uint32_t atom) { uint8_t f[28] = {0}; xw32(f, win); xw32(f + 4, atom); ev32(28, f); }
static void ev_client(uint32_t win, uint32_t type, uint32_t d0, uint32_t d1, uint32_t d2)
{
    uint8_t e[32] = {0};
    e[0] = 33; e[1] = 32;
    xw32(e + 4, win); xw32(e + 8, type); xw32(e + 12, d0); xw32(e + 16, d1); xw32(e + 20, d2);
    fx_send(e, 32);
}

/* ── the compositor, and a pretend Xwayland on the Wayland side ───── */
static aurwl *C;
static pid_t  kid;
static int    cmd_fd = -1, res_fd = -1;
static int    fails;

static void pump(int ms)
{
    struct pollfd p[3] = {
        { .fd = aurwl_fd(C), .events = POLLIN },
        { .fd = X.fd,        .events = POLLIN },
        { .fd = res_fd,      .events = POLLIN },
    };
    poll(p, 3, ms);
    aurwl_dispatch(C);
    if (p[1].revents & POLLIN) fx_read();
}
#define SETTLE() do { for (int _i = 0; _i < 8; _i++) pump(15); } while (0)
#define WAIT(cond) do { for (int _i = 0; _i < 400 && !(cond); _i++) pump(10); } while (0)

static int check(int ok, const char *fmt, ...)
{
    if (ok) return 1;
    va_list ap; va_start(ap, fmt);
    printf("      ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fails++;
    return 0;
}

static req *last_req(uint8_t op, uint32_t first_word, int from)
{
    for (int i = X.n_reqs - 1; i >= from; i--)
        if (X.reqs[i].op == op && (first_word == 0 || xr32(X.reqs[i].body) == first_word)) return &X.reqs[i];
    return NULL;
}
/* ConfigureWindow requests that ask for a size (not a restack). */
static req *last_size(uint32_t win, int from)
{
    for (int i = X.n_reqs - 1; i >= from; i--)
        if (X.reqs[i].op == 12 && xr32(X.reqs[i].body) == win && (xr16(X.reqs[i].body + 4) & (XC_W | XC_H))) return &X.reqs[i];
    return NULL;
}
static int count_size(uint32_t win, int from)
{
    int n = 0;
    for (int i = from; i < X.n_reqs; i++)
        if (X.reqs[i].op == 12 && xr32(X.reqs[i].body) == win && (xr16(X.reqs[i].body + 4) & (XC_W | XC_H))) n++;
    return n;
}
static int count_req(uint8_t op, uint32_t first_word, int from)
{
    int n = 0;
    for (int i = from; i < X.n_reqs; i++)
        if (X.reqs[i].op == op && (first_word == 0 || xr32(X.reqs[i].body) == first_word)) n++;
    return n;
}

/* The Wayland child: makes surfaces on command. */
struct kw { struct wl_display *d; struct wl_compositor *comp; struct wl_subcompositor *sub; struct wl_shm *shm; struct wl_surface *surf[16]; int n; };
static void kreg(void *data, struct wl_registry *r, uint32_t name, const char *i, uint32_t v)
{
    struct kw *k = data; (void)v;
    if (!strcmp(i, "wl_compositor")) k->comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(i, "wl_shm")) k->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(i, "wl_subcompositor")) k->sub = wl_registry_bind(r, name, &wl_subcompositor_interface, 1);
}
static void kreg_rm(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener kreg_l = { kreg, kreg_rm };

static int be_xwayland(int sock, int cmd, int res)
{
    struct kw k = {0};
    k.d = wl_display_connect_to_fd(sock);
    if (!k.d) return 1;
    wl_registry_add_listener(wl_display_get_registry(k.d), &kreg_l, &k);
    wl_display_roundtrip(k.d);
    if (!k.comp || !k.shm || !k.sub) return 2;
    char line[64];
    for (;;) {
        size_t n = 0;
        while (n < sizeof line - 1) { char ch; if (read(cmd, &ch, 1) != 1) return 0; if (ch == '\n') break; line[n++] = ch; }
        line[n] = 0;
        if (line[0] == 'S') {                          /* S <argb>: a surface with a 40x30 picture */
            uint32_t argb = (uint32_t)strtoul(line + 2, NULL, 16);
            struct wl_surface *s = wl_compositor_create_surface(k.comp);
            int fd = memfd_create("x", MFD_CLOEXEC);
            if (fd < 0 || ftruncate(fd, 40 * 30 * 4) < 0) return 3;
            uint32_t *px = mmap(NULL, 40 * 30 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            for (int i = 0; i < 40 * 30; i++) px[i] = argb;
            struct wl_shm_pool *pool = wl_shm_create_pool(k.shm, fd, 40 * 30 * 4);
            struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, 40, 30, 160, WL_SHM_FORMAT_XRGB8888);
            wl_surface_attach(s, b, 0, 0);
            wl_surface_damage(s, 0, 0, 40, 30);
            wl_surface_commit(s);
            wl_display_roundtrip(k.d);
            k.surf[k.n++ % 16] = s;
            int id = (int)wl_proxy_get_id((struct wl_proxy *)s);
            char out[32]; int l = snprintf(out, sizeof out, "%d\n", id);
            if (write(res, out, (size_t)l) < 0) return 4;
        } else if (line[0] == 'P') {                   /* P <argb>: like S, but NOT sent yet: the id is known, the surface is not */
            uint32_t argb = (uint32_t)strtoul(line + 2, NULL, 16);
            struct wl_surface *s = wl_compositor_create_surface(k.comp);
            int fd = memfd_create("x", MFD_CLOEXEC);
            if (fd < 0 || ftruncate(fd, 40 * 30 * 4) < 0) return 3;
            uint32_t *px = mmap(NULL, 40 * 30 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            for (int i = 0; i < 40 * 30; i++) px[i] = argb;
            struct wl_shm_pool *pool = wl_shm_create_pool(k.shm, fd, 40 * 30 * 4);
            struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, 40, 30, 160, WL_SHM_FORMAT_XRGB8888);
            wl_surface_attach(s, b, 0, 0);
            wl_surface_damage(s, 0, 0, 40, 30);
            wl_surface_commit(s);
            k.surf[k.n++ % 16] = s;
            int id = (int)wl_proxy_get_id((struct wl_proxy *)s);
            char out[32]; int l = snprintf(out, sizeof out, "%d\n", id);
            if (write(res, out, (size_t)l) < 0) return 4;
        } else if (line[0] == 'U') {                   /* U <index>: a new surface that is a subsurface of surface <index> (a role of its own) */
            int i = atoi(line + 2);
            struct wl_surface *s = wl_compositor_create_surface(k.comp);
            if (i >= 0 && i < 16 && k.surf[i]) wl_subcompositor_get_subsurface(k.sub, s, k.surf[i]);
            wl_display_roundtrip(k.d);
            k.surf[k.n++ % 16] = s;
            int id = (int)wl_proxy_get_id((struct wl_proxy *)s);
            char out[32]; int l = snprintf(out, sizeof out, "%d\n", id);
            if (write(res, out, (size_t)l) < 0) return 4;
        } else if (line[0] == 'F') {                   /* F: now send it */
            wl_display_roundtrip(k.d);
            if (write(res, "0\n", 2) < 0) return 4;
        } else if (line[0] == 'D') {                   /* D <index>: destroy it */
            int i = atoi(line + 2);
            if (i >= 0 && i < 16 && k.surf[i]) { wl_surface_destroy(k.surf[i]); k.surf[i] = NULL; }
            wl_display_roundtrip(k.d);
            if (write(res, "0\n", 2) < 0) return 4;
        }
    }
}

static int ask_surface(uint32_t argb)
{
    char line[32]; int l = snprintf(line, sizeof line, "S %x\n", argb);
    if (write(cmd_fd, line, (size_t)l) < 0) return -1;
    char buf[32] = {0}; size_t n = 0;
    for (int i = 0; i < 500 && n < sizeof buf - 1; i++) {
        pump(10);
        struct pollfd p = { .fd = res_fd, .events = POLLIN };
        if (poll(&p, 1, 0) > 0) { char ch; if (read(res_fd, &ch, 1) == 1) { if (ch == '\n') return atoi(buf); buf[n++] = ch; } }
    }
    return -1;
}
static int ask_child(const char *line)
{
    if (write(cmd_fd, line, strlen(line)) < 0) return -1;
    char buf[32] = {0}; size_t n = 0;
    for (int i = 0; i < 500 && n < sizeof buf - 1; i++) {
        pump(10);
        struct pollfd p = { .fd = res_fd, .events = POLLIN };
        if (poll(&p, 1, 0) > 0) { char ch; if (read(res_fd, &ch, 1) == 1) { if (ch == '\n') return atoi(buf); buf[n++] = ch; } }
    }
    return -1;
}

static int ask_child_idx_sub(int idx)
{
    char line[32]; snprintf(line, sizeof line, "U %d\n", idx);
    return ask_child(line);
}

static void start(void)
{
    memset(&X, 0, sizeof X);
    setenv("AUROS_NO_XWAYLAND", "1", 1);
    char dir[] = "/tmp/wltest-x-XXXXXX";
    if (!mkdtemp(dir)) abort();
    setenv("XDG_RUNTIME_DIR", dir, 1);
    C = aurwl_create(1280, 800, 60000);
    if (!C) abort();
    int xs[2], ws[2], cp[2], rp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xs) || socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ws) ||
        pipe2(cp, O_CLOEXEC) || pipe2(rp, O_CLOEXEC)) abort();
    fflush(stdout);
    kid = fork();
    if (kid == 0) { close(xs[0]); close(xs[1]); close(ws[0]); close(cp[1]); close(rp[0]); _exit(be_xwayland(ws[1], cp[0], rp[1])); }
    close(ws[1]); close(cp[0]); close(rp[1]);
    cmd_fd = cp[1]; res_fd = rp[0];
    struct wl_client *xc = wl_client_create(C->display, ws[0]);
    X.fd = xs[1];
    fcntl(X.fd, F_SETFL, fcntl(X.fd, F_GETFL) | O_NONBLOCK);
    C->x.client = xc;
    C->x.wm = xwm_create(C, xs[0], xc);
    wl_list_init(&C->x.gone.link);
    for (int i = 0; i < XA_COUNT; i++) X.atoms[i] = 1000u + (uint32_t)i;
    WAIT(C->x.wm && C->x.wm->state == 2 && count_req(113, 0, 0) >= 0 && last_req(15, 0, 0));
    SETTLE();
}
static void finish(void)
{
    close(cmd_fd);
    xwm_destroy(C->x.wm); C->x.wm = NULL;
    C->x.client = NULL;
    close(X.fd);
    int st; waitpid(kid, &st, 0);
    aurwl_destroy(C);
}

/* A window the way an X client makes one: created, asks to be mapped,
 * its properties read, mapped, its surface made and told about. Returns the
 * surface index (the nth surface this child has made). */
static int surf_n;
static int show_window(uint32_t win, uint32_t argb, const char *title, const char *cls, int or_)
{
    int base = X.n_reqs;
    if (title) {
        fx_setprop(win, "WM_NAME", "STRING", 8, title, strlen(title));
    }
    if (cls) {                                    /* "instance|Class": two NUL-terminated strings */
        char buf[64]; size_t n = strlen(cls);
        memcpy(buf, cls, n + 1);
        for (size_t i = 0; i < n; i++) if (buf[i] == '|') buf[i] = 0;
        fx_setprop(win, "WM_CLASS", "STRING", 8, buf, n + 1);
    }
    ev_create(win, 30, 40, 40, 30, or_);
    if (!or_) ev_map_request(win);
    WAIT(or_ || last_req(8, win, base));
    ev_map_notify(win, or_);
    int id = ask_surface(argb);
    ev_client(win, atom_id("WL_SURFACE_ID"), (uint32_t)id, 0, 0);
    SETTLE();
    return surf_n++;
}

static xwin *xw(uint32_t id) { return C->x.wm ? xwin_find(C->x.wm, id) : NULL; }
static aurwl_win *first_win(void)
{
    for (int i = 0; i < aurwl_window_count(C); i++) { aurwl_win *w = aurwl_window_at(C, i); if (!aurwl_win_is_popup(w)) return w; }
    return NULL;
}

/* ── scenarios ───────────────────────────────────────────────────── */

static void sc_takeover(void)
{
    start();
    xwm *m = C->x.wm;
    check(m && m->state == 2, "the window manager did not get through the handshake (state %d)", m ? m->state : -1);
    check(m && m->root == FX_ROOT && m->root_w == 1280 && m->root_h == 800, "root window or screen size not read");
    check(count_req(16, 0, 0) == XA_COUNT, "expected %d InternAtom requests, saw %d", XA_COUNT, count_req(16, 0, 0));
    req *sel = last_req(2, FX_ROOT, 0);
    check(sel && xr32(sel->body + 4) == 0x800 &&
          xr32(sel->body + 8) == (XM_SUBSTRUCTURE_REDIRECT | XM_SUBSTRUCTURE_NOTIFY | XM_PROPERTY),
          "the root window's event mask is not redirect|notify|property");
    req *chk = last_req(18, FX_ROOT, 0);
    check(chk != NULL, "no property was set on the root window");
    int ewmh = 0;
    for (int i = 0; i < X.n_reqs; i++)
        if (X.reqs[i].op == 18 && xr32(X.reqs[i].body) == FX_ROOT && xr32(X.reqs[i].body + 4) == atom_id("_NET_SUPPORTING_WM_CHECK")) ewmh = 1;
    check(ewmh, "_NET_SUPPORTING_WM_CHECK was not set on the root window");
    check(last_req(1, 0, 0) != NULL, "the check window was not created");
    req *comp = last_req(140, 0, 0);
    check(comp && comp->d1 == 2 && xr32(comp->body) == FX_ROOT && comp->body[4] == 1,
          "the root's subwindows were not redirected for manual compositing (Xwayland draws no window otherwise)");
    req *own = last_req(22, 0, 0);
    check(own && xr32(own->body) != 0 && xr32(own->body + 4) == atom_id("WM_S0"),
          "WM_S0 was not claimed (Xwayland serves no X program until it is)");
    check(last_req(15, FX_ROOT, 0) != NULL, "windows already there were not looked for (QueryTree)");
    finish();
}

static void sc_map(void)
{
    start();
    uint32_t win = 0x400001;
    /* what the client put on its window before asking to be shown */
    fx_setprop(win, "_NET_WM_NAME", "UTF8_STRING", 8, "H\xc3\xa9llo \xe2\x98\x83", 10);  /* "Héllo ☃" */
    fx_setprop(win, "WM_PROTOCOLS", "ATOM", 32, (uint32_t[]){ atom_id("WM_DELETE_WINDOW") }, 4);
    uint32_t hints[18] = { 16 | 64, 0, 0, 0, 0, 200, 100, 0, 0, 7, 9, 0, 0, 0, 0, 0, 0, 0 };   /* min 200x100, inc 7x9 */
    fx_setprop(win, "WM_NORMAL_HINTS", "WM_NORMAL_HINTS", 32, hints, sizeof hints);
    int base = X.n_reqs;
    int idx = show_window(win, 0xFF336699, "old name", "vlc|Vlc", 0);
    (void)idx;
    check(count_req(20, win, base) == XW_NPROPS, "expected %d GetProperty for the window, saw %d", XW_NPROPS, count_req(20, win, base));
    req *mp = last_req(8, win, base);
    check(mp != NULL, "the window was never mapped");
    int at = -1;
    for (int i = base; i < X.n_reqs; i++) if (X.reqs[i].op == 8 && xr32(X.reqs[i].body) == win) at = i;
    req *cfg = last_req(12, win, base);
    check(cfg && xr16(cfg->body + 4) == (XC_X | XC_Y) && xr32(cfg->body + 8) == 0 && xr32(cfg->body + 12) == 0,
          "the window was not placed at (0,0) before it was mapped");
    int cfg_at = -1;
    for (int i = base; i < X.n_reqs; i++) if (X.reqs[i].op == 12 && xr32(X.reqs[i].body) == win) cfg_at = i;
    check(cfg_at >= 0 && cfg_at < at, "it was mapped before it was placed");
    req *ws = NULL;
    for (int i = base; i < X.n_reqs; i++)
        if (X.reqs[i].op == 18 && xr32(X.reqs[i].body) == win && xr32(X.reqs[i].body + 4) == atom_id("WM_STATE")) ws = &X.reqs[i];
    check(ws && xr32(ws->body + 20) == 1, "WM_STATE was not set to Normal");

    aurwl_win *w = first_win();
    check(w != NULL, "the mapped window is not in the shell's window list");
    if (w) {
        check(!strcmp(aurwl_win_title(w), "H\xc3\xa9llo \xe2\x98\x83"), "title is \"%s\"", aurwl_win_title(w));
        check(!strcmp(aurwl_win_app_id(w), "Vlc"), "app id is \"%s\" (the class, not the instance)", aurwl_win_app_id(w));
        check(w->role == ROLE_X11, "its role is not X11");
        surface *s = aurwl_win_content(w);
        check(s && s->w == 40 && s->h == 30 && s->px[0] == 0xFF336699 && s->px[29 * s->stride + 39] == 0xFF336699,
              "its pixels are not the client's (byte for byte)");
        check(!aurwl_win_is_popup(w), "an ordinary window reported as a popup");
    }
    finish();
}

static void sc_not_listed_until_known(void)
{
    start();
    uint32_t win = 0x400010;
    int idx = ask_surface(0xFF00FF00);                    /* a surface exists ... */
    SETTLE();
    check(aurwl_window_count(C) == 0, "a surface nobody has said is a window was listed");
    ev_create(win, 0, 0, 40, 30, 0);
    ev_map_request(win);
    WAIT(last_req(8, win, 0));
    ev_client(win, atom_id("WL_SURFACE_ID"), (uint32_t)idx, 0, 0);
    SETTLE();
    check(aurwl_window_count(C) == 1, "a window with a known surface was not listed (%d)", aurwl_window_count(C));
    /* a lie about which surface */
    uint32_t win2 = 0x400011;
    ev_create(win2, 0, 0, 40, 30, 0);
    ev_map_request(win2);
    ev_client(win2, atom_id("WL_SURFACE_ID"), 0x7fffff, 0, 0);          /* no such object */
    ev_client(win2, atom_id("WL_SURFACE_ID"), 1, 0, 0);                  /* wl_display: not a surface */
    ev_client(win2, atom_id("WL_SURFACE_ID"), (uint32_t)idx, 0, 0);      /* already another window's: moves it */
    SETTLE();
    check(aurwl_window_count(C) <= 1, "a surface became two windows");
    finish();
}

/* The window's message about its surface and the surface itself come on
 * different sockets; the message may arrive first. */
static void sc_surface_late(void)
{
    start();
    uint32_t win = 0x400018;
    ev_create(win, 0, 0, 40, 30, 0);
    ev_map_request(win);
    WAIT(last_req(8, win, 0));
    ev_map_notify(win, 0);
    int id = ask_child("P ff445566\n");                  /* the surface's number is known; it has not been sent */
    ev_client(win, atom_id("WL_SURFACE_ID"), (uint32_t)id, 0, 0);
    SETTLE();
    check(aurwl_window_count(C) == 0, "listed before its surface existed");
    ask_child("F\n");                                    /* the surface arrives */
    SETTLE();
    check(aurwl_window_count(C) == 1, "a surface that arrived after its window's message was never paired (%d)", aurwl_window_count(C));
    aurwl_win *w = first_win();
    surface *s = w ? aurwl_win_content(w) : NULL;
    check(s && s->px[0] == 0xFF445566, "its pixels are not the client's");
    finish();
}

static void sc_configure(void)
{
    start();
    uint32_t win = 0x400020;
    uint32_t hints[18] = { 16 | 32 | 64, 0, 0, 0, 0, 200, 100, 900, 700, 10, 10, 0, 0, 0, 0, 0, 0, 0 };
    fx_setprop(win, "WM_NORMAL_HINTS", "WM_NORMAL_HINTS", 32, hints, sizeof hints);
    show_window(win, 0xFF000000, "w", "a|A", 0);
    aurwl_win *w = first_win();
    check(w != NULL, "no window");
    if (!w) { finish(); return; }
    int base = X.n_reqs;
    aurwl_win_configure(w, 640, 480, 1, 0, 0);
    SETTLE();
    req *c1 = last_size(win, base);
    check(c1 && xr16(c1->body + 4) == (XC_W | XC_H), "no resize was asked for");
    /* 640 and 480, in steps of 10 from the 200x100 minimum: exact */
    check(c1 && xr32(c1->body + 8) == 640 && xr32(c1->body + 12) == 480, "asked for %u x %u, wanted 640 x 480",
          c1 ? xr32(c1->body + 8) : 0, c1 ? xr32(c1->body + 12) : 0);
    int n = count_size(win, base);
    aurwl_win_configure(w, 640, 480, 1, 0, 0);
    aurwl_win_configure(w, 640, 480, 0, 0, 0);
    SETTLE();
    check(count_size(win, base) == n, "the same size was asked for twice");
    aurwl_win_configure(w, 645, 487, 1, 0, 0);               /* off the 10-pixel steps: rounded down to 640 x 480 */
    SETTLE();
    check(count_size(win, base) == n, "a size that rounds to the same was asked for again");
    aurwl_win_configure(w, 50, 20, 1, 0, 0);                  /* below the minimum */
    SETTLE();
    c1 = last_size(win, base);
    check(c1 && xr32(c1->body + 8) == 200 && xr32(c1->body + 12) == 100, "below the minimum was not raised to it");
    aurwl_win_configure(w, 5000, 5000, 1, 0, 0);              /* above the maximum */
    SETTLE();
    c1 = last_size(win, base);
    check(c1 && xr32(c1->body + 8) == 900 && xr32(c1->body + 12) == 700, "above the maximum was not lowered to it");
    /* a program with a minimum and no steps: the rounding does not hide a missing clamp */
    uint32_t win2 = 0x400021;
    uint32_t hints2[18] = { 16, 0, 0, 0, 0, 300, 200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    fx_setprop(win2, "WM_NORMAL_HINTS", "WM_NORMAL_HINTS", 32, hints2, sizeof hints2);
    show_window(win2, 0xFF000000, "w2", "b|B", 0);
    aurwl_win *w2 = NULL;
    for (int i = 0; i < aurwl_window_count(C); i++) if (aurwl_window_at(C, i)->xw && aurwl_window_at(C, i)->xw->id == win2) w2 = aurwl_window_at(C, i);
    check(w2 != NULL, "the second window is not listed");
    if (w2) {
        int b2 = X.n_reqs;
        aurwl_win_configure(w2, 50, 20, 1, 0, 0);
        SETTLE();
        req *c2 = last_size(win2, b2);
        check(c2 && xr32(c2->body + 8) == 300 && xr32(c2->body + 12) == 200, "below the minimum (no steps) was not raised to it");
    }
    finish();
}

static void sc_close(void)
{
    start();
    uint32_t win = 0x400030, win2 = 0x400031;
    fx_setprop(win, "WM_PROTOCOLS", "ATOM", 32, (uint32_t[]){ atom_id("WM_DELETE_WINDOW"), atom_id("WM_TAKE_FOCUS") }, 8);
    show_window(win, 0xFF111111, "polite", "p|P", 0);
    aurwl_win *w = first_win();
    int base = X.n_reqs;
    if (w) aurwl_win_close(w);
    SETTLE();
    req *se = last_req(25, win, base);
    check(se && xr32(se->body + 8 + 8) == atom_id("WM_PROTOCOLS") && xr32(se->body + 8 + 12) == atom_id("WM_DELETE_WINDOW"),
          "closing a window that can be asked did not send WM_DELETE_WINDOW");
    check(count_req(113, 0, base) == 0, "a window that can be asked was killed");
    /* one that cannot be asked */
    show_window(win2, 0xFF222222, "rude", "r|R", 0);
    aurwl_win *w2 = NULL;
    for (int i = 0; i < aurwl_window_count(C); i++) if (aurwl_window_at(C, i)->xw && aurwl_window_at(C, i)->xw->id == win2) w2 = aurwl_window_at(C, i);
    base = X.n_reqs;
    if (w2) aurwl_win_close(w2);
    SETTLE();
    check(last_req(113, win2, base) != NULL, "closing a window that cannot be asked did not kill its client");
    base = X.n_reqs;
    if (w) aurwl_win_kill(w);
    SETTLE();
    check(last_req(113, win, base) != NULL, "kill did not kill the X client");
    finish();
}

static void sc_focus(void)
{
    start();
    uint32_t win = 0x400040;
    fx_setprop(win, "WM_PROTOCOLS", "ATOM", 32, (uint32_t[]){ atom_id("WM_TAKE_FOCUS") }, 4);
    show_window(win, 0xFF123456, "focus me", "f|F", 0);
    aurwl_win *w = first_win();
    int base = X.n_reqs;
    if (w) aurwl_set_focus(C, w);
    SETTLE();
    req *sf = last_req(42, 0, base);
    check(sf && xr32(sf->body) == win, "the X input focus was not given to the window");
    req *se = last_req(25, win, base);
    check(se && xr32(se->body + 8 + 12) == atom_id("WM_TAKE_FOCUS"), "WM_TAKE_FOCUS was not sent to a window that asked for it");
    int active = 0;
    for (int i = base; i < X.n_reqs; i++)
        if (X.reqs[i].op == 18 && xr32(X.reqs[i].body + 4) == atom_id("_NET_ACTIVE_WINDOW") && xr32(X.reqs[i].body + 20) == win) active = 1;
    check(active, "_NET_ACTIVE_WINDOW was not set");
    base = X.n_reqs;
    aurwl_set_focus(C, NULL);
    SETTLE();
    sf = last_req(42, 0, base);
    check(sf && xr32(sf->body) == 0, "taking the keyboard away did not take the X focus away");
    finish();
}

static void sc_client_configure(void)
{
    start();
    uint32_t win = 0x400050;
    show_window(win, 0xFF010203, "mover", "m|M", 0);
    int base = X.n_reqs;
    ev_configure_request(win, 50, 60, 300, 200, XC_X | XC_Y | XC_W | XC_H);
    SETTLE();
    req *c = last_req(12, win, base);
    check(c && xr16(c->body + 4) == (XC_X | XC_Y | XC_W | XC_H) &&
          xr32(c->body + 8) == 0 && xr32(c->body + 12) == 0 && xr32(c->body + 16) == 300 && xr32(c->body + 20) == 200,
          "a client's resize was not granted at (0,0)");
    /* the X server's notify of the change; then the same request again: nothing changes, so ICCCM wants a synthetic notify */
    uint8_t f[28] = {0};
    xw32(f, win); xw32(f + 4, win); xw16(f + 12, 0); xw16(f + 14, 0); xw16(f + 16, 300); xw16(f + 18, 200);
    ev32(22, f);
    base = X.n_reqs;
    ev_configure_request(win, 0, 0, 300, 200, XC_W | XC_H);
    SETTLE();
    req *se = last_req(25, win, base);
    check(se && se->body[8] == 22, "a request that changes nothing was not answered with a synthetic ConfigureNotify");
    /* absurd sizes are clamped, not passed to the server */
    base = X.n_reqs;
    ev_configure_request(win, 0, 0, 65535, 65535, XC_W | XC_H);
    SETTLE();
    c = last_req(12, win, base);
    check(c && xr32(c->body + 16) <= XW_SIZE_MAX && xr32(c->body + 20) <= XW_SIZE_MAX, "an absurd size was passed on");
    /* a window we do not manage: as asked */
    base = X.n_reqs;
    ev_configure_request(0x500000, 5, 6, 7, 8, XC_X | XC_Y | XC_W | XC_H);
    SETTLE();
    c = last_req(12, 0x500000, base);
    check(c && xr32(c->body + 8) == 5 && xr32(c->body + 16) == 7, "a window nobody manages was not configured as asked");
    finish();
}

static void sc_menu(void)
{
    start();
    uint32_t win = 0x400060, menu = 0x400061;
    show_window(win, 0xFF404040, "app", "a|A", 0);
    aurwl_win *app = first_win();
    if (app) aurwl_set_focus(C, app);
    SETTLE();
    int base = X.n_reqs;
    show_window(menu, 0xFFFFFFFF, NULL, NULL, 1);            /* override-redirect: positions itself */
    uint8_t f[28] = {0};                                      /* and the server says where */
    xw32(f, menu); xw32(f + 4, menu); xw16(f + 12, 25); xw16(f + 14, 35); xw16(f + 16, 40); xw16(f + 18, 30); f[22] = 1;
    ev32(22, f);
    SETTLE();
    check(count_req(8, menu, base) == 0 && count_req(20, menu, base) == 0, "an override-redirect window was managed");
    aurwl_win *mw = NULL;
    for (int i = 0; i < aurwl_window_count(C); i++) if (aurwl_window_at(C, i)->xw && aurwl_window_at(C, i)->xw->id == menu) mw = aurwl_window_at(C, i);
    check(mw != NULL, "the menu is not in the window list");
    if (mw) {
        check(aurwl_win_is_popup(mw), "the menu is not a popup");
        check(aurwl_win_parent(mw) == app, "the menu's parent is not the window in use");
        int ox = -1, oy = -1;
        aurwl_win_popup_offset(mw, &ox, &oy);
        check(ox == 25 && oy == 35, "popup offset is (%d,%d), expected (25,35): where it put itself", ox, oy);
    }
    finish();
}

static void sc_destroy(void)
{
    start();
    uint32_t win = 0x400070;
    show_window(win, 0xFF777777, "doomed", "d|D", 0);
    check(aurwl_window_count(C) == 1, "no window");
    ev_destroy(win);
    SETTLE();
    check(xw(win) == NULL, "a destroyed window is still tracked");
    check(aurwl_window_count(C) == 0, "a destroyed X window is still listed");
    finish();
}

static void sc_unmap_remap(void)
{
    start();
    uint32_t win = 0x400075;
    show_window(win, 0xFF777700, "again", "a|A", 0);
    int base = X.n_reqs;
    ev_unmap(win);
    SETTLE();
    int ws0 = 0;
    for (int i = base; i < X.n_reqs; i++)
        if (X.reqs[i].op == 18 && xr32(X.reqs[i].body + 4) == atom_id("WM_STATE") && xr32(X.reqs[i].body + 20) == 0) ws0 = 1;
    check(ws0, "WM_STATE was not set to Withdrawn when the window unmapped");
    base = X.n_reqs;
    ev_map_request(win);                                      /* shown again */
    SETTLE();
    check(last_req(8, win, base) != NULL, "a window asking to be shown again was not mapped");
    check(count_req(20, win, base) == 0, "properties were read again for a window we know");
    finish();
}

static void sc_title_change(void)
{
    start();
    uint32_t win = 0x400080;
    show_window(win, 0xFF808080, "first", "t|T", 0);
    aurwl_win *w = first_win();
    check(w && !strcmp(aurwl_win_title(w), "first"), "title is not \"first\"");
    fx_setprop(win, "WM_NAME", "STRING", 8, "caf\xe9", 4);       /* Latin-1 */
    ev_property(win, XP_WM_NAME);
    SETTLE();
    check(w && !strcmp(aurwl_win_title(w), "caf\xc3\xa9"), "Latin-1 title not converted to UTF-8 (\"%s\")", w ? aurwl_win_title(w) : "");
    fx_setprop(win, "WM_NAME", "STRING", 8, "a\x01" "b\x7f" "c", 5);      /* control characters */
    ev_property(win, XP_WM_NAME);
    SETTLE();
    check(w && !strcmp(aurwl_win_title(w), "a b c"), "control characters in a title were shown (\"%s\")", w ? aurwl_win_title(w) : "");
    fx_setprop(win, "_NET_WM_NAME", "UTF8_STRING", 8, "bad \xff\xfe utf8 \xc3", 14);
    ev_property(win, atom_id("_NET_WM_NAME"));
    SETTLE();
    int ok = 1;
    if (w) for (const unsigned char *p = (const unsigned char *)aurwl_win_title(w); *p; p++) if (*p >= 0x80) {
        int n = (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0) { ok = 0; break; }
        for (int k = 1; k <= n; k++) if ((p[k] & 0xC0) != 0x80) { ok = 0; break; }
        if (!ok) break;
        p += n;
    }
    check(ok, "invalid UTF-8 in a title was passed on (\"%s\")", w ? aurwl_win_title(w) : "");
    static const struct { const char *in; const char *out; const char *why; } T[] = {
        { "x\xc0\x80y",         "x??y",   "an overlong NUL" },
        { "x\xe0\x80\xafy",     "x???y",  "an overlong slash" },
        { "x\xed\xa0\x80y",     "x???y",  "a surrogate" },
        { "x\xf4\x90\x80\x80y", "x????y", "a code point past U+10FFFF" },
        { "x\xc2\x85y",         "x??y",   "a C1 control" },
        { "x\xc3\xa9\xe2\x98\x83y", "x\xc3\xa9\xe2\x98\x83y", "valid text" },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        fx_setprop(win, "_NET_WM_NAME", "UTF8_STRING", 8, T[i].in, strlen(T[i].in));
        ev_property(win, atom_id("_NET_WM_NAME"));
        SETTLE();
        check(w && !strcmp(aurwl_win_title(w), T[i].out), "%s in a title: got \"%s\"", T[i].why, w ? aurwl_win_title(w) : "");
    }
    finish();
}

static void sc_requests(void)
{
    start();
    uint32_t win = 0x400090;
    show_window(win, 0xFF909090, "states", "s|S", 0);
    aurwl_win *w = first_win();
    check(w != NULL, "no window");
    if (!w) { finish(); return; }
    aurwl_win_take_requests(w);
    int base = X.n_reqs;
    ev_client(win, atom_id("_NET_WM_STATE"), 1, atom_id("_NET_WM_STATE_FULLSCREEN"), 0);     /* add fullscreen */
    SETTLE();
    check(aurwl_win_take_requests(w) & AURWL_REQ_FULLSCREEN, "a fullscreen request did not reach the shell");
    int set = 0;
    for (int i = base; i < X.n_reqs; i++)
        if (X.reqs[i].op == 18 && xr32(X.reqs[i].body + 4) == atom_id("_NET_WM_STATE") && xr32(X.reqs[i].body + 16) == 1) set = 1;
    check(set, "_NET_WM_STATE was not set to fullscreen");
    ev_client(win, atom_id("_NET_WM_STATE"), 0, atom_id("_NET_WM_STATE_FULLSCREEN"), 0);     /* remove */
    SETTLE();
    check(aurwl_win_take_requests(w) & AURWL_REQ_UNFULLSCREEN, "leaving fullscreen did not reach the shell");
    ev_client(win, atom_id("_NET_WM_STATE"), 2, atom_id("_NET_WM_STATE_MAXIMIZED_VERT"), atom_id("_NET_WM_STATE_MAXIMIZED_HORZ"));
    SETTLE();
    check(aurwl_win_take_requests(w) & AURWL_REQ_MAXIMIZE, "a maximize request did not reach the shell");
    ev_client(win, atom_id("WM_CHANGE_STATE"), 3, 0, 0);
    SETTLE();
    check(aurwl_win_take_requests(w) & AURWL_REQ_MINIMIZE, "a minimise request did not reach the shell");
    /* the shell goes fullscreen on its own: the property follows */
    base = X.n_reqs;
    aurwl_win_configure(w, 800, 600, 1, 0, 1);
    SETTLE();
    set = 0;
    for (int i = base; i < X.n_reqs; i++)
        if (X.reqs[i].op == 18 && xr32(X.reqs[i].body + 4) == atom_id("_NET_WM_STATE") && xr32(X.reqs[i].body + 16) == 1) set = 1;
    check(set, "the shell's own fullscreen was not written to _NET_WM_STATE");
    finish();
}

static void sc_hostile(void)
{
    start();
    uint32_t win = 0x4000a0;
    show_window(win, 0xFFA0A0A0, "calm", "c|C", 0);
    /* a flood of windows: bounded */
    for (uint32_t i = 0; i < 600; i++) ev_create(0x600000 + i, 0, 0, 10, 10, i & 1);
    SETTLE();
    check(C->x.wm && C->x.wm->n_wins <= XW_MAX_WINS, "window table grew past its bound (%d)", C->x.wm ? C->x.wm->n_wins : -1);
    /* garbage client messages and properties */
    ev_client(win, atom_id("WL_SURFACE_ID"), 0xffffffffu, 0, 0);
    ev_client(0xdeadbeef, atom_id("_NET_WM_STATE"), 1, 0, 0);
    ev_client(win, atom_id("_NET_WM_STATE"), 99, 0xffffffff, 0xfffffffe);
    ev_client(win, 0xfffffff0u, 1, 2, 3);
    ev_property(0xdeadbeef, XP_WM_NAME);
    for (int i = 0; i < 40; i++) { uint8_t junk[32]; for (int k = 0; k < 32; k++) junk[k] = (uint8_t)(i * 31 + k * 7); junk[0] = (uint8_t)(2 + i % 60); if (junk[0] == 35) junk[0] = 36; fx_send(junk, 32); }
    SETTLE();
    check(C->x.wm && !C->x.wm->dead, "the window manager gave up on ordinary junk");
    /* a property that claims to be enormous or odd */
    fx_setprop(win, "WM_NORMAL_HINTS", "WM_NORMAL_HINTS", 32, (uint32_t[]){ 0xffffffffu, 1, 2, 3, 4, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0, 0, 0xffffffffu, 0xffffffffu }, 60);
    ev_property(win, XP_WM_NORMAL_HINTS);
    fx_setprop(win, "WM_CLASS", "STRING", 8, "\0\0\0\0\0\0", 6);
    ev_property(win, XP_WM_CLASS);
    SETTLE();
    aurwl_win *w = first_win();
    if (w) { aurwl_win_configure(w, 640, 480, 1, 0, 0); SETTLE(); }
    check(C->x.wm && !C->x.wm->dead, "hints that claim the moon killed the window manager");
    /* a reply that claims to be larger than any reply can be: the connection is dropped, the shell survives */
    X.garble = 1;
    ev_property(win, XP_WM_NAME);
    fx_setprop(win, "WM_NAME", "STRING", 8, "x", 1);
    SETTLE();
    SETTLE();
    check(C->x.wm == NULL || C->x.wm->dead, "an impossible reply length was believed");
    check(aurwl_window_count(C) >= 0, "(still serving)");
    X.garble = 0;
    finish();
}

static void sc_wrong_sequence(void)
{
    start();
    uint32_t win = 0x4000b0;
    X.garble = 2;                                              /* every reply has the wrong sequence number */
    ev_create(win, 0, 0, 10, 10, 0);
    ev_map_request(win);
    SETTLE(); SETTLE();
    check(C->x.wm == NULL || C->x.wm->dead, "a reply that answers no request we made was believed");
    X.garble = 0;
    finish();
}

/* A reply to a request that was never made. */
/* A frameless window (Zoom, an Electron splash) says so; the shell then draws no title bar over its own. */
static void sc_frameless(void)
{
    start();
    uint32_t win = 0x4000f0;
    uint32_t motif[5] = { 2, 0, 0, 0, 0 };                    /* MWM_HINTS_DECORATIONS set, decorations none */
    fx_setprop(win, "_MOTIF_WM_HINTS", "_MOTIF_WM_HINTS", 32, motif, sizeof motif);
    show_window(win, 0xFF204060, "frameless", "z|Z", 0);
    aurwl_win *w = first_win();
    check(w && aurwl_win_csd(w), "a window that asked for no decorations was given a title bar");
    uint32_t motif2[5] = { 2, 0, 1, 0, 0 };                   /* decorations: yes */
    fx_setprop(win, "_MOTIF_WM_HINTS", "_MOTIF_WM_HINTS", 32, motif2, sizeof motif2);
    ev_property(win, atom_id("_MOTIF_WM_HINTS"));
    SETTLE();
    check(w && !aurwl_win_csd(w), "a window that asked for decorations again still has none");
    uint32_t motif3[5] = { 1, 0, 0, 0, 0 };                   /* flags that do not mention decorations: no opinion */
    fx_setprop(win, "_MOTIF_WM_HINTS", "_MOTIF_WM_HINTS", 32, motif3, sizeof motif3);
    ev_property(win, atom_id("_MOTIF_WM_HINTS"));
    SETTLE();
    check(w && !aurwl_win_csd(w), "hints that do not mention decorations took them away");
    finish();
}

static void sc_unsolicited(void)
{
    start();
    show_window(0x4000d0, 0xFFC0C0C0, "calm", "c|C", 0);
    check(C->x.wm && !C->x.wm->dead && C->x.wm->pend_n == 0, "setup: the window manager is idle");
    uint8_t r[32] = {0};
    r[0] = 1; xw16(r + 2, 0x1234);
    fx_send(r, sizeof r);
    SETTLE();
    check(C->x.wm == NULL || C->x.wm->dead, "a reply to a request nobody made was believed");
    finish();
}

/* A window cannot take a surface that is already something else. */
static void sc_role_kept(void)
{
    start();
    uint32_t win = 0x4000e0;
    int idx = show_window(win, 0xFF123456, "real", "r|R", 0);
    int sub = ask_child_idx_sub(idx);                       /* a subsurface of the window's surface */
    check(sub > 0, "setup: no subsurface");
    uint32_t win2 = 0x4000e1;
    ev_create(win2, 0, 0, 10, 10, 0);
    ev_map_request(win2);
    SETTLE();
    ev_client(win2, atom_id("WL_SURFACE_ID"), (uint32_t)sub, 0, 0);
    SETTLE();
    xwin *x2 = xw(win2);
    check(x2 && x2->win == NULL, "a window took a surface that is a subsurface");
    int n_sub = 0;
    aurwl_win *s;
    wl_list_for_each(s, &C->surfaces, link) if (s->role == ROLE_SUBSURFACE) n_sub++;
    check(n_sub == 1, "the subsurface lost its role (%d left)", n_sub);
    finish();
}

static void sc_x_goes_away(void)
{
    start();
    uint32_t win = 0x4000c0;
    show_window(win, 0xFFC0C0C0, "x", "x|X", 0);
    check(aurwl_window_count(C) == 1, "no window");
    close(X.fd);                                               /* Xwayland dies */
    X.fd = open("/dev/null", O_RDWR);
    WAIT(C->x.wm->dead);
    check(C->x.wm->dead, "a vanished X server was not noticed");
    /* the surface is still there (its client connection lives on) but it is an X window of an X server that is gone */
    SETTLE();
    aurwl_win *w = first_win();
    if (w) aurwl_win_configure(w, 100, 100, 1, 0, 0);       /* nothing to say it to: must not crash */
    if (w) aurwl_win_close(w);
    aurwl_set_focus(C, w);
    /* ...and its process is collected: everything of it goes, so the next X program starts a fresh one */
    C->x.pid = 4242;
    xw_reaped(C, 4242);
    check(C->x.pid == 0 && C->x.wm == NULL && C->x.client == NULL, "the process was collected and its window manager (or connection) is still here");
    check(aurwl_window_count(C) == 0, "its windows are still listed after it went (%d)", aurwl_window_count(C));
    finish();
}

#include "wltest-x-clip.inc"

static aurwl_win *win_of_x(uint32_t id)
{
    for (int i = 0; i < aurwl_window_count(C); i++) { aurwl_win *w = aurwl_window_at(C, i); if (w->xw && w->xw->id == id) return w; }
    return NULL;
}

/* Two windows of one program (a player and its dialog) sit in the same place; Xwayland gives a click to the top
 * one, so the one she points at or types into must be the top one. */
static void sc_restack(void)
{
    start();
    uint32_t m1 = 0x400100, d1 = 0x400101;
    show_window(m1, 0xFF112233, "main", "v|V", 0);
    show_window(d1, 0xFF445566, "dialog", "v|V", 0);
    aurwl_win *wm = win_of_x(m1), *wd = win_of_x(d1);
    check(wm && wd, "setup: both windows are listed");
    check(fx_top() == d1, "setup: the later window is on top");
    if (!wm || !wd) { finish(); return; }
    aurwl_set_focus(C, wm); SETTLE();
    check(fx_top() == m1, "giving a window the keyboard did not put it on top of the X stack");
    aurwl_set_focus(C, wd); SETTLE();
    check(fx_top() == d1, "the dialog was not put on top when it got the keyboard");
    aurwl_win_configure(wm, 300, 200, 1, 0, 0); SETTLE();                  /* the shell says: this is the one in use */
    check(fx_top() == m1, "a window the shell calls active was not put on top");
    aurwl_set_focus(C, NULL); aurwl_set_focus(C, wd); SETTLE();
    check(fx_top() == d1, "setup: the dialog is on top again");
    aurwl_pointer_motion(C, wm, 5, 5, 100); SETTLE();                       /* the pointer goes into the player's window */
    check(fx_top() == m1, "the pointer entering a window did not put it on top (the click would go to the dialog)");
    finish();
}

/* A menu is shown while the program that owns it is the one in use, and never takes the keyboard. A program
 * that is not in use cannot paint over everything or take her clicks (it may be any size, anywhere, even
 * invisible: nothing manages an override-redirect window). */
static void sc_menu_bound(void)
{
    start();
    uint32_t a = 0x400200, b = 0x600001, ma = 0x400201, mb = 0x600002;     /* two programs: two X resource-id bases */
    show_window(a, 0xFF204060, "prog a", "a|A", 0);
    show_window(b, 0xFF206040, "prog b", "b|B", 0);
    aurwl_win *wa = win_of_x(a), *wb = win_of_x(b);
    check(wa && wb, "setup: both programs have a window");
    if (!wa || !wb) { finish(); return; }
    aurwl_set_focus(C, wa); SETTLE();
    show_window(mb, 0xFFFFFFFF, NULL, NULL, 1);               /* program b's menu while a is in use */
    check(win_of_x(mb) == NULL, "another program's override-redirect window is shown over the one in use");
    show_window(ma, 0xFFFFFFFF, NULL, NULL, 1);               /* program a's menu */
    aurwl_win *wma = win_of_x(ma);
    check(wma && aurwl_win_is_popup(wma) && aurwl_win_parent(wma) == wa, "the menu of the program in use is not its popup");
    aurwl_set_focus(C, wb); SETTLE();
    check(win_of_x(ma) == NULL, "a menu stays shown after its program stopped being the one in use");
    check(win_of_x(mb) != NULL, "a menu is not shown when its program becomes the one in use");
    aurwl_set_focus(C, NULL); SETTLE();
    check(win_of_x(ma) == NULL && win_of_x(mb) == NULL, "menus are shown with nothing (or a Wayland window) in use");
    aurwl_set_focus(C, wb); SETTLE();
    aurwl_win *wmb = win_of_x(mb);
    aurwl_set_focus(C, wmb); SETTLE();                         /* what pressing inside the menu does */
    check(aurwl_focus(C) == wb, "a press in a menu gave it the keyboard");
    check(C->x.wm->active && !C->x.wm->active->or_, "X was told a menu is the window in use");
    xwm_focus(C, wmb->xw);                                     /* and the X side refuses it on its own too */
    check(C->x.wm->active && !C->x.wm->active->or_, "the X side made a menu the window in use");
    finish();
}

static const struct { const char *name; void (*fn)(void); } SC[] = {
    { "takeover",         sc_takeover },
    { "map",              sc_map },
    { "listed-when-known", sc_not_listed_until_known },
    { "surface-late",     sc_surface_late },
    { "configure",        sc_configure },
    { "close",            sc_close },
    { "focus",            sc_focus },
    { "client-configure", sc_client_configure },
    { "menu",             sc_menu },
    { "destroy",          sc_destroy },
    { "unmap-remap",      sc_unmap_remap },
    { "title-change",     sc_title_change },
    { "requests",         sc_requests },
    { "hostile",          sc_hostile },
    { "wrong-sequence",   sc_wrong_sequence },
    { "frameless",        sc_frameless },
    { "unsolicited",      sc_unsolicited },
    { "role-kept",        sc_role_kept },
    { "clip-text-wl-to-x", sc_clip_text_wl_to_x },
    { "clip-latin1",      sc_clip_latin1 },
    { "clip-image-wl-to-x", sc_clip_image_wl_to_x },
    { "clip-wl-hangs",    sc_clip_wl_hangs },
    { "clip-text-x-to-wl", sc_clip_text_x_to_wl },
    { "clip-image-x-to-wl", sc_clip_image_x_to_wl },
    { "clip-incr",        sc_clip_incr },
    { "clip-refused",     sc_clip_refused },
    { "clip-newest-wins", sc_clip_newest_wins },
    { "clip-too-big",     sc_clip_too_big },
    { "restack",          sc_restack },
    { "menu-bound",       sc_menu_bound },
    { "x-goes-away",      sc_x_goes_away },
};
#define NSC ((int)(sizeof SC / sizeof SC[0]))

/* A hung test is a bug in the window manager (a loop that does not end);
 * say where. */
static void on_alarm(int sig)
{
    (void)sig;
    void *bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "wltest-x: still running after 90 s, stuck here:\n");
    backtrace_symbols_fd(bt, n, 2);
    _exit(3);
}

int main(int argc, char **argv)
{
    signal(SIGALRM, on_alarm);
    alarm(90);
    if (argc > 1 && !strcmp(argv[1], "-l")) { for (int i = 0; i < NSC; i++) printf("%2d %s\n", i, SC[i].name); return 0; }
    int only = -1;
    if (argc > 1) {
        for (int i = 0; i < NSC; i++) if (!strcmp(argv[1], SC[i].name)) only = i;
        if (only < 0 && argv[1][0] >= '0' && argv[1][0] <= '9') only = atoi(argv[1]);
        if (only < 0 || only >= NSC) { fprintf(stderr, "no such scenario: %s\n", argv[1]); return 2; }
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);
    int bad = 0, ran = 0;
    for (int i = 0; i < NSC; i++) {
        if (only >= 0 && i != only) continue;
        printf("  %-18s\n", SC[i].name);
        int before = fails; surf_n = 0;
        SC[i].fn();
        int ok = fails == before;
        printf("  %-18s  %s\n", SC[i].name, ok ? "ok" : "FAIL");
        bad += !ok; ran++;
    }
    printf("\n%s: %d of %d scenarios passed\n", bad ? "FAIL" : "PASS", ran - bad, ran);
    return bad ? 1 : 0;
}
