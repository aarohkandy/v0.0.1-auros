/* fuzz_xwm.c -- the X window manager against X servers and X programs that lie to it.
 *
 * src/aurwl/xwm.inc reads, by hand, everything an X server sends it: events
 * about every window of every X program (their titles, classes, size hints
 * and protocols are written by those programs), replies to its own
 * requests, and whatever a broken or hostile Xwayland writes. It runs in
 * the process that owns her keyboard, so one overrun is a way for any X
 * program to run code there, and one crash closes every window on her
 * screen.
 *
 * This harness is the X server. It answers the window manager's requests
 * the way a server does -- atoms, properties, the window tree, attributes,
 * extensions -- but with contents and shapes taken from the fuzzer's bytes:
 * strings that are not UTF-8, hints that claim the moon, counts that
 * exceed the data, replies with the wrong sequence number or a length that
 * is a lie. It sends events about windows that exist and windows that do
 * not, in orders no server would, and names surfaces (WL_SURFACE_ID) that
 * are real, gone, or somebody else's. Between those it does what the shell
 * does every frame: configure, focus, close, kill, ask for popup parents
 * and sizes, read the pixels. Surfaces are made as Xwayland's Wayland
 * connection would make them.
 *
 * At the end Xwayland "exits" and is collected, and the compositor must be
 * back to empty: no X window, no surface, no connection. LeakSanitizer
 * catches what is merely forgotten.
 *
 * The clipboard (xclip.inc) is part of it: the X server answers ConvertSelection,
 * GetAtomName and big or INCR property reads from the fuzzer's bytes, announces
 * owners with XFixes, and Wayland programs copy and paste (a data source made as a
 * program's would be, a paste into a pipe).
 *
 * aurwl.c is included whole (it includes xwm.inc), so the harness reaches
 * the window manager's own state.
 *
 * Input encoding (for whoever writes seeds): a stream of bytes; each step
 * reads one byte whose low three bits pick what happens, the rest are
 * arguments; see step(). A seed that does something useful is made by
 * tools/fuzz/mkseeds_bin.py.
 */
#define _GNU_SOURCE
#include "../../src/aurwl/aurwl.c"

#include <stdarg.h>
#include <stdint.h>
#include <sys/socket.h>

/* ── the input ───────────────────────────────────────────────────── */
static const uint8_t *in_p, *in_end;
static int in_done;
static uint8_t u8(void)
{
    if (in_p >= in_end) { in_done = 1; return 0; }
    return *in_p++;
}
static uint32_t u32(void)
{
    uint32_t v = u8();
    v |= (uint32_t)u8() << 8; v |= (uint32_t)u8() << 16; v |= (uint32_t)u8() << 24;
    return v;
}

/* ── the pretend X server ─────────────────────────────────────────── */
#define FX_ROOT   0x2au
#define FX_BASE   0x00400000u
#define FX_MASK   0x001fffffu
#define NPOOL     6                          /* window ids the fuzzer plays with */
#define NSURF     8

static aurwl *C;
static int debug;                            /* FUZZ_XWM_DEBUG=1: say what each input did */
static int sfd = -1, pfd = -1;
static struct wl_client *xc;
static struct wl_resource *comp;
static uint8_t fin[1 << 17];
static size_t  fin_n;
static int     got_setup;
static uint16_t fseq;
static aurwl_win *surf[NSURF];
static uint32_t surf_id[NSURF];

static uint32_t pool_win(uint8_t b)
{
    if (b % 16 == 15) return FX_ROOT;
    if (b % 16 == 14) return u32();
    return FX_BASE + 1u + (uint32_t)(b % NPOOL);
}
static uint32_t atom_pick(uint8_t b)
{
    if (b < 200) return 1000u + (uint32_t)(b % XA_COUNT);
    static const uint32_t pre[] = { XP_PRIMARY, XP_ATOM, XP_CARDINAL, XP_STRING, XP_WINDOW, XP_WM_HINTS, XP_WM_NAME,
                                    XP_WM_NORMAL_HINTS, XP_WM_CLASS, XP_WM_TRANSIENT_FOR, 0 };
    return b < 250 ? pre[b % (sizeof pre / sizeof pre[0])] : u32();
}
static uint32_t atom_of_name(const char *name)
{
    for (int i = 0; i < XA_COUNT; i++) if (!strcmp(name, XA_NAMES[i])) return 1000u + (uint32_t)i;
    return 2000u;
}

static void quiet(const char *fmt, va_list ap) { (void)fmt; (void)ap; }

static void fx_dispatch(void) { wl_event_loop_dispatch(C->loop, 0); }

static void fx_send(const void *b, size_t n)
{
    if (sfd < 0) return;
    const uint8_t *p = b;
    for (int spins = 0; n && spins < 2000; ) {
        ssize_t k = write(sfd, p, n);
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && errno == EAGAIN) { fx_dispatch(); spins++; continue; }   /* let the other end read */
        if (k <= 0) return;
        p += k; n -= (size_t)k;
    }
}

/* One reply. A little of the time it is wrong in a way a lying server would be. */
static void fx_reply(uint8_t byte1, uint16_t seq, const void *extra, size_t extra_len, const uint8_t head[24])
{
    uint8_t buf[32 + 1024 * 4] = {0};
    if (extra_len > 4096) extra_len = 4096;
    size_t padded = (extra_len + 3) & ~(size_t)3;
    buf[0] = 1; buf[1] = byte1;
    xw16(buf + 2, seq);
    xw32(buf + 4, (uint32_t)(padded / 4));
    if (head) memcpy(buf + 8, head, 24);
    if (extra_len) memcpy(buf + 32, extra, extra_len);
    size_t total = 32 + padded;
    uint8_t lie = in_done ? 0 : u8();
    if (lie < 3) {
        switch (lie) {
        case 0: xw16(buf + 2, (uint16_t)(seq + 1 + u8())); break;                     /* the wrong request */
        case 1: xw32(buf + 4, u32()); break;                                          /* a length that is a lie */
        case 2: total = total > 40 ? 40 + (size_t)(u8() % 8) : total; break;           /* cut short */
        }
    }
    if (total > sizeof buf) total = sizeof buf;
    fx_send(buf, total);
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
    body[o] = 24; body[o + 1] = 32; body[o + 2] = 32; o += 8;
    xw32(body + o, FX_ROOT); xw32(body + o + 4, 0x20); xw32(body + o + 8, 0xffffff);
    xw16(body + o + 20, 1280); xw16(body + o + 22, 800);
    xw32(body + o + 32, 0x21); body[o + 38] = 24;
    o += 40;
    xw16(b + 6, (uint16_t)(o / 4));
    fx_send(b, 8 + o);
}

/* Properties on the window manager's own window (the clipboard's), and INCR. */
static struct { uint32_t win, atom, type; int format; uint8_t data[512]; size_t n; int used; } sp[4];
static int sp_incr;                                    /* more INCR chunks to send after each read */
static void sp_set(uint32_t win, uint32_t atom, uint32_t type, int format, const uint8_t *data, size_t n)
{
    int i = 0;
    for (; i < 4; i++) if (sp[i].used && sp[i].win == win && sp[i].atom == atom) break;
    if (i == 4) for (i = 0; i < 4 && sp[i].used; i++) ;
    if (i == 4) i = 0;
    sp[i].used = 1; sp[i].win = win; sp[i].atom = atom; sp[i].type = type; sp[i].format = format;
    sp[i].n = n < sizeof sp[i].data ? n : sizeof sp[i].data;
    memcpy(sp[i].data, data, sp[i].n);
}

/* What a window's property holds, as the fuzzer says. */
static void gen_prop(uint32_t atom, uint8_t out[4096], uint32_t head[3], int *format, size_t *bytes)
{
    uint8_t shape = u8();
    *format = 8; *bytes = 0;
    head[0] = XP_STRING; head[1] = 0;
    size_t units = 0;
    if (atom == XP_WM_NORMAL_HINTS || (shape & 0xF0) == 0x10) {
        *format = 32; head[0] = XP_WM_NORMAL_HINTS; units = 12 + (shape & 7);
    } else if (atom == XP_WM_HINTS) {
        *format = 32; head[0] = XP_WM_HINTS; units = 1 + (shape & 7);
    } else if (atom == 1000u + XA_WM_PROTOCOLS || atom == 1000u + XA_NET_WM_WINDOW_TYPE) {
        *format = 32; head[0] = XP_ATOM; units = shape & 7;
    } else if (atom == XP_WM_TRANSIENT_FOR) {
        *format = 32; head[0] = XP_WINDOW; units = 1;
    } else {
        *format = 8; units = (shape & 0x3F) + ((shape & 0x40) ? (size_t)u8() * 8 : 0);
        if (atom == 1000u + XA_NET_WM_NAME) head[0] = 1000u + XA_UTF8_STRING;
    }
    if ((shape & 0xC0) == 0xC0) *format = (int[]){ 0, 7, 16, 32, 8, 64 }[shape % 6];       /* odd formats */
    size_t unit = *format >= 8 ? (size_t)(*format / 8) : 1;
    if (units * unit > 4096) units = 4096 / unit;
    size_t n = units * unit;
    for (size_t i = 0; i < n; i += 4) {
        uint32_t v = u32();
        if (*format == 32 && atom == 1000u + XA_WM_PROTOCOLS) v = atom_pick((uint8_t)v);
        if (*format == 32 && atom == XP_WM_TRANSIENT_FOR) v = pool_win((uint8_t)v);
        memcpy(out + i, &v, i + 4 <= 4096 ? 4 : 0);
    }
    *bytes = n;
    head[2] = (uint32_t)units;
    if ((shape & 0x3F) >= 0x38) head[2] = (uint32_t)units + (u32() % 4000);   /* a count that exceeds the data */
}

static void fx_request(const uint8_t *r, size_t len)
{
    uint16_t seq = ++fseq;
    const uint8_t *b = r + 4;
    uint8_t head[24] = {0};
    switch (r[0]) {
    case 16: {                                            /* InternAtom */
        if (len < 8) break;
        uint16_t n = xr16(b);
        char name[64] = {0};
        if (n > len - 8) n = (uint16_t)(len - 8);
        memcpy(name, b + 4, n < 63 ? n : 63);
        xw32(head, atom_of_name(name));
        fx_reply(0, seq, NULL, 0, head);
        break;
    }
    case 20: {                                            /* GetProperty */
        if (len < 24) break;
        for (int i = 0; i < 4; i++) if (sp[i].used && sp[i].win == xr32(b) && sp[i].atom == xr32(b + 4)) {
            size_t unit = sp[i].format >= 8 ? (size_t)(sp[i].format / 8) : 1;
            size_t off = (size_t)xr32(b + 12) * 4, lim = (size_t)xr32(b + 16) * 4;
            if (off > sp[i].n) off = sp[i].n;
            size_t send = sp[i].n - off < lim ? sp[i].n - off : lim;
            send -= send % unit;
            xw32(head, sp[i].type); xw32(head + 4, (uint32_t)(sp[i].n - off - send)); xw32(head + 8, (uint32_t)(send / unit));
            fx_reply((uint8_t)sp[i].format, seq, sp[i].data + off, send, head);
            if (r[1] && sp[i].n - off - send == 0) {
                uint32_t win = sp[i].win, atom = sp[i].atom;
                sp[i].used = 0;
                if (sp_incr-- > 0) {                          /* INCR: the next chunk, and a property notify */
                    uint8_t chunk[64]; size_t cn = sp_incr ? 4 * (size_t)(1 + u8() % 16) : 0;
                    for (size_t k = 0; k < cn; k++) chunk[k] = u8();
                    sp_set(win, atom, XP_STRING, 8, chunk, cn);
                    uint8_t ev[32] = {0}; ev[0] = 28; xw32(ev + 4, win); xw32(ev + 8, atom);
                    fx_send(ev, 32);
                }
            }
            return;
        }
        uint8_t data[4096] = {0};
        uint32_t h[3]; int format; size_t bytes;
        gen_prop(xr32(b + 4), data, h, &format, &bytes);
        if ((u8() & 15) == 0) { fx_reply(0, seq, NULL, 0, head); break; }            /* no such property */
        xw32(head, h[0]); xw32(head + 4, h[1]); xw32(head + 8, h[2]);
        fx_reply((uint8_t)format, seq, data, bytes, head);
        break;
    }
    case 98: {                                            /* QueryExtension: Composite 140, BIG-REQUESTS 141, XFIXES 142 */
        uint16_t n = xr16(b);
        char name[32] = {0};
        if (n > len - 8) n = (uint16_t)(len - 8);
        memcpy(name, b + 4, n < 31 ? n : 31);
        head[0] = (u8() & 7) != 0;
        head[1] = !strcmp(name, "BIG-REQUESTS") ? 141 : !strcmp(name, "XFIXES") ? 142 : 140;
        head[2] = !strcmp(name, "XFIXES") ? 87 : 0;
        fx_reply(0, seq, NULL, 0, head);
        break;
    }
    case 141:                                             /* BIG-REQUESTS Enable */
        xw32(head, (u8() & 1) ? 65535u : (u32() % (8u << 20)));
        fx_reply(0, seq, NULL, 0, head);
        break;
    case 142:                                             /* XFIXES QueryVersion */
        if (r[1] == 0) { xw32(head, 5); fx_reply(0, seq, NULL, 0, head); }
        break;
    case 17: {                                            /* GetAtomName: mime types, and names that are not */
        static const char *names[] = { "image/png", "text/html", "text/uri-list", "application/x-foo", "text/plain;charset=utf-8",
            "TARGETS", "x", "a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/0/1/2/3/4/5/6/7/8/9/0/1/2/3/4/5/6/7/8/9/0/1/2/3/4/5/6/7/8/9",
            "no slash", "image/\x01\xff" };
        const char *nm = names[u8() % (sizeof names / sizeof names[0])];
        xw16(head, (uint16_t)strlen(nm));
        fx_reply(0, seq, nm, strlen(nm), head);
        break;
    }
    case 24: {                                            /* ConvertSelection: the owner answers, as the fuzzer likes */
        uint32_t requestor = xr32(b), selection = xr32(b + 4), target = xr32(b + 8), prop = xr32(b + 12);
        uint8_t how = u8();
        if (how & 1) {                                    /* something goes in the property (a target list, data, or INCR) */
            uint8_t data[512]; size_t n = (size_t)(u8() % 64) * 4;
            for (size_t i = 0; i < n; i++) data[i] = u8();
            if (how & 2) for (size_t i = 0; i + 4 <= n; i += 4) xw32(data + i, (how & 4) ? 1000u + (uint32_t)(u8() % XA_COUNT) : 3000u + (uint32_t)(u8() % 12));
            sp_set(requestor, prop, (how & 8) ? 1000u + XA_INCR : (how & 16) ? XP_ATOM : target, (how & 32) ? 8 : 32, data, n);
            if (how & 8) sp_incr = 1 + u8() % 4;
        }
        uint8_t f[28] = {0};
        xw32(f + 4, requestor); xw32(f + 8, selection); xw32(f + 12, target); xw32(f + 16, (how & 64) ? 0 : prop);
        uint8_t ev[32] = {0}; ev[0] = 31; memcpy(ev + 4, f, 28);
        fx_send(ev, 32);
        break;
    }
    case 15: {                                            /* QueryTree */
        uint32_t kids[12]; unsigned n = u8() % 12;
        for (unsigned i = 0; i < n; i++) kids[i] = pool_win(u8());
        xw32(head, FX_ROOT); xw16(head + 8, (uint16_t)n);
        fx_reply(0, seq, kids, n * 4, head);
        break;
    }
    case 3: {                                             /* GetWindowAttributes */
        uint8_t full[32 + 12] = {0};
        full[0] = 1; xw16(full + 2, seq); xw32(full + 4, 3);
        full[26] = (uint8_t)(u8() % 3); full[27] = (uint8_t)(u8() & 1);
        fx_send(full, sizeof full);
        break;
    }
    default: break;
    }
}

static void fx_read(void)
{
    if (sfd < 0) return;
    ssize_t n = read(sfd, fin + fin_n, sizeof fin - fin_n);
    if (n > 0) fin_n += (size_t)n;
    size_t off = 0;
    for (;;) {
        if (!got_setup) {
            if (fin_n - off < 12) break;
            off += 12; got_setup = 1;
            fx_setup_reply();
            continue;
        }
        if (fin_n - off < 4) break;
        size_t len = 4 * (size_t)xr16(fin + off + 2);
        if (len < 4) { off = fin_n; break; }
        if (fin_n - off < len) break;
        fx_request(fin + off, len);
        off += len;
    }
    memmove(fin, fin + off, fin_n - off);
    fin_n -= off;
}

static void settle(void)
{
    for (int i = 0; i < 4; i++) { fx_dispatch(); fx_read(); }
}

/* An event from the server, shaped by the fuzzer. */
static void send_event(void)
{
    static const uint8_t types[] = { 16, 17, 18, 19, 20, 22, 23, 28, 33, 33, 33, 21, 24, 25, 26, 27, 29, 30, 31, 32, 34, 2, 60, 87, 87, 30, 31, 28 };
    uint8_t e[32];
    uint8_t t = types[u8() % sizeof types];
    for (int i = 0; i < 32; i++) e[i] = u8();
    e[0] = t;
    if (u8() & 3) {                                       /* mostly about windows that exist */
        xw32(e + 4, (t == 16 || t == 17 || t == 18 || t == 20 || t == 23) ? FX_ROOT : pool_win(e[4]));
        xw32(e + 8, pool_win(e[8]));
        if (t == 28) { xw32(e + 4, pool_win(e[4])); xw32(e + 8, atom_pick(e[9]) ); }
        if (t == 19) xw32(e + 4, pool_win(e[4]));
    }
    if (t == 33) {                                        /* ClientMessage: the ones that matter */
        e[1] = (u8() & 7) ? 32 : e[1];
        xw32(e + 4, pool_win(e[4]));
        uint8_t k = u8() % 5;
        uint32_t mt = k == 0 ? 1000u + XA_WL_SURFACE_ID : k == 1 ? 1000u + XA_NET_WM_STATE
                    : k == 2 ? 1000u + XA_WM_CHANGE_STATE : k == 3 ? 1000u + XA_NET_WM_STATE : u32();
        xw32(e + 8, mt);
        if (k == 0) {
            uint8_t s = u8();
            xw32(e + 12, s < 200 ? (surf_id[s % NSURF] ? surf_id[s % NSURF] : 0x10u + s % NSURF) : u32());
        } else if (k == 1 || k == 3) {
            xw32(e + 12, u8() % 4); xw32(e + 16, atom_pick(u8())); xw32(e + 20, atom_pick(u8()));
        }
    }
    if (t == 22 || t == 23) { xw16(e + 16, (uint16_t)(u32() % 4000)); xw16(e + 20, (uint16_t)(u32() % 5000)); }
    if (t == 87) {                                        /* XFixesSelectionNotify: who owns CLIPBOARD now */
        uint8_t who = u8();
        e[1] = 0;
        xw32(e + 8, who < 40 ? 0 : who < 80 ? C->x.wm->check : pool_win(who));
        xw32(e + 12, (u8() & 15) ? 1000u + XA_CLIPBOARD : atom_pick(u8()));
    }
    if (t == 29 || t == 30 || t == 31) {                   /* the selection events: about our window, mostly CLIPBOARD */
        uint32_t clip = (u8() & 7) ? 1000u + XA_CLIPBOARD : atom_pick(u8());
        if (t == 29) xw32(e + 12, clip);
        if (t == 30) { xw32(e + 12, pool_win(e[12])); xw32(e + 16, clip); xw32(e + 20, (u8() & 3) ? atom_pick(u8()) : 1000u + XA_TARGETS); }
        if (t == 31) { xw32(e + 8, (u8() & 7) ? C->x.wm->check : pool_win(e[8])); xw32(e + 12, clip); xw32(e + 16, (u8() & 3) ? atom_pick(u8()) : 1000u + XA_TARGETS);
                       xw32(e + 20, (u8() & 3) ? 1000u + XA_AUR_SEL + (u8() & 1) : 0); }
    }
    if (t == 28 && (u8() & 3) == 0) { xw32(e + 4, C->x.wm->check); xw32(e + 8, 1000u + XA_AUR_SEL); e[16] = 0; }
    fx_send(e, 32);
}

/* What the shell does. */
static void shell_step(void)
{
    int n = aurwl_window_count(C);
    aurwl_win *w = n ? aurwl_window_at(C, (int)(u8() % (unsigned)n)) : NULL;
    switch (u8() % 12) {
    case 0: if (w) aurwl_win_configure(w, (int)(u32() % 3000) - 100, (int)(u32() % 3000) - 100, u8() & 1, u8() & 1, u8() & 1); break;
    case 1: aurwl_set_focus(C, w); break;
    case 2: if (w) aurwl_win_close(w); break;
    case 3: if (w && (u8() & 15) == 0) aurwl_win_kill(w); break;        /* rare: it takes the whole Xwayland with it for a plain surface */
    case 4: if (w) { int a, b; aurwl_win_popup_offset(w, &a, &b); (void)aurwl_win_parent(w); (void)aurwl_win_is_popup(w); } break;
    case 5: if (w) { surface *s = aurwl_win_content(w); if (s && s->w > 0) (void)s->px[0]; int a, b; aurwl_win_logical_size(w, &a, &b); aurwl_win_pref_size(w, &a, &b); } break;
    case 6: if (w) (void)aurwl_win_take_requests(w); break;
    case 7: if (w) { (void)aurwl_win_title(w); (void)aurwl_win_app_id(w); (void)aurwl_win_csd(w); } break;
    case 8: aurwl_pointer_motion(C, w, (int)(u32() % 200), (int)(u32() % 200), 1000); break;
    case 9: aurwl_key(C, 30 + u8() % 20, u8() & 1, 1000); break;
    case 10: aurwl_resize_output(C, 640 + (int)(u8() % 8) * 100, 480 + (int)(u8() % 5) * 100); break;
    default: aurwl_frame_done(C, 1000); break;
    }
}

/* Surfaces, as Xwayland's connection makes them. */
static void surface_step(void)
{
    if (!C->x.client) return;
    unsigned i = u8() % NSURF;
    uint8_t what = u8() % 4;
    if (what == 0 && !surf[i]) {                           /* make one */
        uint32_t id = 0x10u + i;
        comp_create_surface(xc, comp, id);
        struct wl_resource *r = wl_client_get_object(xc, id);
        if (r && wl_resource_instance_of(r, &wl_surface_interface, &surface_impl)) {
            surf[i] = wl_resource_get_user_data(r); surf_id[i] = id;
        }
    } else if (what == 1 && surf[i]) {                     /* it has a picture: what attaching a buffer does */
        aurwl_win *s = surf[i];
        if (!s->store) s->store = surface_new(20 + (int)(u8() % 60), 20 + (int)(u8() % 60));
        if (s->store) { s->cw = s->store->w; s->ch = s->store->h; }
        s->mapped = 1;
        s->c->damage_seq++;
    } else if (what == 2 && surf[i]) {                     /* its picture goes */
        surf[i]->mapped = 0;
    } else if (what == 3 && surf[i]) {                     /* the surface is destroyed */
        struct wl_resource *r = surf[i]->res;
        surf[i] = NULL; surf_id[i] = 0;
        wl_resource_destroy(r);
    }
}

/* Wayland programs copy and paste. */
static int paste_rd[6], n_paste_rd;
static const char *const MIMES[] = { "text/plain;charset=utf-8", "text/plain", "image/png", "text/html", "UTF8_STRING", "image/png" };
static void clip_step(void)
{
    uint8_t k = u8();
    switch (k % 5) {
    case 0: {                                              /* a program copies (a source made as a program's would be) */
        if (!C->x.client || (C->selection && (k & 0x40))) break;
        data_source *ds = calloc(1, sizeof *ds);
        if (!ds) break;
        ds->c = C;
        ds->res = wl_resource_create(xc, &wl_data_source_interface, 3, 0);
        if (!ds->res) { free(ds); break; }
        wl_resource_set_implementation(ds->res, &data_source_impl, ds, dsrc_gone);
        int n = 1 + (int)(u8() % 3);
        for (int i = 0; i < n; i++) snprintf(ds->mimes[ds->n_mimes++], sizeof ds->mimes[0], "%s", MIMES[u8() % 6]);
        if (C->selection && C->selection != ds->res) wl_data_source_send_cancelled(C->selection);
        C->selection = ds->res;
        xw_selection_changed(C);
        break;
    }
    case 1:                                                /* ...and the copy goes */
        if (C->selection) wl_resource_destroy(C->selection);
        break;
    case 2:                                                /* a program pastes what an X program copied */
        if (n_paste_rd < 6) {
            int p[2];
            if (pipe2(p, O_CLOEXEC | O_NONBLOCK) == 0) { xw_paste(C, MIMES[u8() % 6], p[1]); paste_rd[n_paste_rd++] = p[0]; }
        }
        break;
    case 3:                                                /* a reader takes what has come */
        for (int i = 0; i < n_paste_rd; i++) { uint8_t buf[4096]; while (read(paste_rd[i], buf, sizeof buf) > 0) ; }
        break;
    default:                                               /* patience runs out: every transfer waiting is told to give up */
        if (C->x.wm) {
            xfer *x; xpaste *p;
            wl_list_for_each(x, &C->x.wm->xfers, link) if (x->timer) wl_event_source_timer_update(x->timer, 1);
            wl_list_for_each(p, &C->x.wm->pastes, link) if (p->timer) wl_event_source_timer_update(p->timer, 1);
            usleep(2000);
            wl_event_loop_dispatch(C->loop, 0);
        }
        break;
    }
}

static void sweep(void)                                    /* what the compositor holds must still be consistent */
{
    if (!C->x.client) {                                    /* the connection went (a kill): its surfaces with it */
        memset(surf, 0, sizeof surf); memset(surf_id, 0, sizeof surf_id);
        return;
    }
    for (unsigned i = 0; i < NSURF; i++) {
        if (!surf[i]) continue;
        struct wl_resource *r = wl_client_get_object(xc, surf_id[i]);
        if (!r) surf[i] = NULL;                            /* gone with its connection */
    }
}

static void step(void)
{
    uint8_t b = u8();
    switch (b & 7) {
    case 0: case 1: send_event(); break;
    case 2: shell_step(); break;
    case 3: surface_step(); break;
    case 4: {                                              /* bytes that are not an event at all */
        uint8_t junk[96]; unsigned n = 1 + u8() % 95;
        for (unsigned i = 0; i < n; i++) junk[i] = u8();
        fx_send(junk, n);
        break;
    }
    case 5: {                                              /* a reply to a question nobody asked */
        uint8_t r[32 + 8] = {0};
        r[0] = 1; xw16(r + 2, (uint16_t)(u8() << 8 | u8())); xw32(r + 4, (uint32_t)(u8() % 3));
        fx_send(r, 32 + 4 * (size_t)(r[4] % 3));
        break;
    }
    case 6: if ((b >> 3) == 31 && sfd >= 0) { close(sfd); sfd = -1; } else clip_step(); break;    /* the X server dies, rarely */
    default: send_event(); send_event(); break;
    }
    settle();
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    static char dir[256];
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    snprintf(dir, sizeof dir, "%s/fuzz-xwm-XXXXXX", base);
    if (!mkdtemp(dir)) abort();
    setenv("XDG_RUNTIME_DIR", dir, 1);
    setenv("AUROS_NO_XWAYLAND", "1", 1);          /* no X sockets in /tmp from a fuzzer */
    debug = getenv("FUZZ_XWM_DEBUG") != NULL;
    if (!debug) wl_log_set_handler_server(quiet);
    C = aurwl_create(1280, 720, 60000);
    if (!C) abort();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!C) LLVMFuzzerInitialize(NULL, NULL);
    if (size > 8192) return 0;
    in_p = data; in_end = data + size; in_done = 0;

    int xs[2], ws[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xs) || socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ws)) abort();
    sfd = xs[1]; pfd = ws[1];
    fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL) | O_NONBLOCK);
    fin_n = 0; got_setup = 0; fseq = 0;
    memset(surf, 0, sizeof surf); memset(surf_id, 0, sizeof surf_id);

    xc = wl_client_create(C->display, ws[0]);
    if (!xc) abort();
    comp = wl_resource_create(xc, &wl_compositor_interface, 4, 0);
    if (!comp) abort();
    wl_resource_set_implementation(comp, NULL, C, NULL);
    C->x.client = xc;
    C->x.gone.notify = x_xclient_gone;
    wl_client_add_destroy_listener(xc, &C->x.gone);
    C->x.wm = xwm_create(C, xs[0], xc);
    if (!C->x.wm) abort();
    settle();

    for (int steps = 0; steps < 800 && !in_done; steps++) { step(); sweep(); }

    if (debug)
        fprintf(stderr, "x windows=%d listed=%d state=%d dead=%d\n", C->x.wm ? C->x.wm->n_wins : -1,
                aurwl_window_count(C), C->x.wm ? C->x.wm->state : -1, C->x.wm ? C->x.wm->dead : -1);

    /* Xwayland goes, and is collected: all of it with it. */
    if (sfd >= 0) { close(sfd); sfd = -1; }
    settle();
    C->x.pid = 4242;
    xw_reaped(C, 4242);
    aurwl_dispatch(C);
    close(pfd); pfd = -1;
    xc = NULL; comp = NULL;
    for (unsigned i = 0; i < NSURF; i++) surf[i] = NULL;
    for (int i = 0; i < n_paste_rd; i++) close(paste_rd[i]);
    n_paste_rd = 0;
    memset(sp, 0, sizeof sp); sp_incr = 0;

    if (C->x.wm || C->x.client || C->x.pid || aurwl_window_count(C) || C->focus || !wl_list_empty(&C->surfaces) ||
        C->selection || C->xsel_active) {
        fprintf(stderr, "aurwl kept state for an X server that has gone (wm %p client %p windows %d)\n",
                (void *)C->x.wm, (void *)C->x.client, aurwl_window_count(C));
        abort();
    }
    return 0;
}
