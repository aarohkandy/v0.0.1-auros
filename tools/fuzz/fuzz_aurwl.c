/* fuzz_aurwl.c -- the Wayland compositor against programs that lie to it.
 *
 * Every program she runs talks to src/aurwl/aurwl.c over a socket, and
 * aurwl runs inside the shell: one bad request that crashes it closes
 * every window on her screen, and one use-after-free is a way for any
 * program to run code in the process that reads her keyboard. So this
 * harness IS a program: two of them, in fact, because a clipboard, a
 * keyboard focus or a popup parent shared between two programs is where
 * one of them leaving does the damage.
 *
 * libwayland checks the wire format; what it cannot check is whether
 * aurwl does the right thing with well-formed requests in a hostile
 * order. So the requests are built from the protocol's own generated
 * signatures (the same tables the real client library marshals from):
 * the fuzzer picks an object it holds, a request that object's
 * interface and version have, and the values -- object arguments are
 * drawn from objects of the right interface, new ids are tracked with
 * their interface, serials can be the one the compositor just sent,
 * shm pools are backed by real memfds. Between requests it also does
 * what the shell does every frame: configure, focus, pointer and key
 * events, frame callbacks, reading each window's pixels, closing,
 * killing, resizing the output.
 *
 * At the end of every input both programs disconnect, and the
 * compositor must be back to empty: no window, no surface, no focus,
 * no selection, no seat or output object left behind. Anything left
 * would be a pointer into a program that is gone. LeakSanitizer
 * catches what is merely forgotten.
 *
 * aurwl.c is included whole so that check can see its lists.
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

static aurwl *C;
static int debug;              /* FUZZ_AURWL_DEBUG=1: say what each input does */

/* A number a request might carry: small ones, the edges of int32, the
 * sizes the compositor caps at, the serial it just sent, or anything. */
static uint32_t num(void)
{
    uint8_t b = u8();
    switch (b >> 6) {
    case 0: return (uint32_t)((int)(b & 63) - 8);
    case 1: {
        uint32_t ser = wl_display_get_serial(C->display);
        const uint32_t K[] = { 0, 1, 2, 3, 4, 64, 100, 600, 800, 4096,
            16384, 16385, 65535, 0x7fffffffu, 0x80000000u, 0xffffffffu,
            0xc0000000u, 0x40000000u, ser, ser - 1, ser + 1, 256, 1024, 32 };
        return K[(b & 63) % (sizeof K / sizeof *K)];
    }
    default: return u32();
    }
}

/* ── the two programs ────────────────────────────────────────────── */
#define MAX_OBJ 512
typedef struct { uint32_t id; const struct wl_interface *iface; int ver; } obj;
typedef struct {
    struct wl_client *cl;
    struct wl_listener gone;
    int    fd;                  /* the program's end of the socket */
    int    alive;
    obj    o[MAX_OBJ];
    int    n;
    uint32_t next_id;
} prog;
static prog P[2];

static void prog_gone(struct wl_listener *l, void *data)
{
    (void)data;
    prog *p = wl_container_of(l, p, gone);
    p->alive = 0;
    p->cl = NULL;
}

static void prog_start(prog *p)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) abort();
    memset(p->o, 0, sizeof p->o);
    p->fd = sv[0];
    p->cl = wl_client_create(C->display, sv[1]);
    if (!p->cl) abort();
    p->gone.notify = prog_gone;
    wl_client_add_destroy_listener(p->cl, &p->gone);
    p->alive = 1;
    p->o[0] = (obj){ 1, &wl_display_interface, 1 };
    p->n = 1;
    p->next_id = 2;
}

/* Read and drop whatever the compositor sent, closing any fds that came
 * with it (keymaps, clipboard pipes): a program that never reads is a
 * socket that fills, and that is a different test. */
static void prog_drain(prog *p)
{
    if (p->fd < 0) return;
    for (;;) {
        char buf[65536];
        char cbuf[CMSG_SPACE(sizeof(int) * 32)];
        struct iovec io = { buf, sizeof buf };
        struct msghdr mh = { 0 };
        mh.msg_iov = &io; mh.msg_iovlen = 1;
        mh.msg_control = cbuf; mh.msg_controllen = sizeof cbuf;
        ssize_t k = recvmsg(p->fd, &mh, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mh); k > 0 && cm; cm = CMSG_NXTHDR(&mh, cm))
            if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
                int *f = (int *)CMSG_DATA(cm);
                size_t nf = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                for (size_t i = 0; i < nf; i++) close(f[i]);
            }
        if (k <= 0) return;
    }
}

static void prog_end(prog *p)
{
    if (p->alive && p->cl) wl_client_destroy(p->cl);
    p->alive = 0; p->cl = NULL;
    prog_drain(p);
    if (p->fd >= 0) close(p->fd);
    p->fd = -1;
}

/* ── the registry, as the real one numbers it ───────────────────── */
typedef struct { struct wl_global **g; const struct wl_interface *iface; int ver; } glob;
static uint32_t global_name(prog *p, int k, const struct wl_interface **iface, int *ver)
{
    const glob G[] = {
        { &C->g_compositor, &wl_compositor_interface, 5 },
        { &C->g_subcompositor, &wl_subcompositor_interface, 1 },
        { &C->g_seat, &wl_seat_interface, 7 },
        { &C->g_output, &wl_output_interface, 4 },
        { &C->g_xdg, &xdg_wm_base_interface, 5 },
        { &C->g_ddm, &wl_data_device_manager_interface, 3 },
        { &C->g_deco, &zxdg_decoration_manager_v1_interface, 1 },
        { &C->g_viewporter, &wp_viewporter_interface, 1 },
        { &C->g_xdg_output, &zxdg_output_manager_v1_interface, 3 },
        { NULL, &wl_shm_interface, 1 },
    };
    const glob *g = &G[k % (int)(sizeof G / sizeof *G)];
    *iface = g->iface; *ver = g->ver;
    /* wl_display_init_shm() creates wl_shm before aurwl creates any of
     * its own, and libwayland numbers globals in creation order. */
    if (!g->g) return wl_global_get_name(C->g_compositor, p->cl) - 1;
    return wl_global_get_name(*g->g, p->cl);
}

/* ── one request ─────────────────────────────────────────────────── */
static uint8_t msg[4096];
static size_t  mlen;
static int     mfds[8], nfds;

static void put32(uint32_t v)
{
    if (mlen + 4 <= sizeof msg) { memcpy(msg + mlen, &v, 4); mlen += 4; }
}
static void put_bytes(const uint8_t *b, size_t n, int nul)
{
    size_t total = n + (nul ? 1 : 0), padded = (total + 3) & ~(size_t)3;
    if (mlen + 4 + padded > sizeof msg) { put32(0); return; }
    put32((uint32_t)total);
    memset(msg + mlen, 0, padded);
    memcpy(msg + mlen, b, n);
    mlen += padded;
}
static void put_string(int nullable)
{
    uint8_t L = u8();
    if (nullable && L == 0) { put32(0); return; }
    uint8_t s[1200];
    size_t n = L < 200 ? (size_t)(L % 48) : (size_t)L * 4;   /* now and then, long */
    for (size_t i = 0; i < n; i++) {
        uint8_t c = i < 16 ? u8() : (uint8_t)('a' + i % 26);
        s[i] = c ? c : 'x';
    }
    put_bytes(s, n, 1);
}

static int pick_obj(prog *p, const struct wl_interface *want, uint32_t *id)
{
    int match[MAX_OBJ], m = 0;
    for (int i = 0; i < p->n; i++)
        if (!want || p->o[i].iface == want) match[m++] = i;
    uint8_t b = u8();
    if (!m || b == 255) { *id = b == 255 ? 0x7777 : 0; return 0; }  /* null, or nonsense */
    *id = p->o[match[b % m]].id;
    return 1;
}

static int since_of(const struct wl_message *m)
{
    int v = 0;
    for (const char *s = m->signature; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return v ? v : 1;
}

static void add_obj(prog *p, uint32_t id, const struct wl_interface *iface, int ver)
{
    if (p->n < MAX_OBJ) p->o[p->n++] = (obj){ id, iface, ver };
}

static void request(prog *p)
{
    if (p->n == 0) return;
    obj *o = &p->o[u8() % p->n];
    const struct wl_interface *I = o->iface;
    if (!I->method_count) return;
    int op = u8() % I->method_count;
    const struct wl_message *m = &I->methods[op];
    if (since_of(m) > o->ver) return;
    uint32_t self = o->id;
    int self_ver = o->ver;
    int destroys = !strcmp(m->name, "destroy") || !strcmp(m->name, "release");

    mlen = 0; nfds = 0;
    put32(self); put32(0);
    if (I == &wl_registry_interface && !strcmp(m->name, "bind")) {
        const struct wl_interface *gi; int gv;
        uint32_t name = global_name(p, u8(), &gi, &gv);
        uint8_t vb = u8();
        int ver = vb == 255 ? gv + 1 : 1 + vb % gv;      /* now and then, too new */
        put32(name);
        put_bytes((const uint8_t *)gi->name, strlen(gi->name), 1);
        put32((uint32_t)ver);
        put32(p->next_id);
        add_obj(p, p->next_id++, gi, ver);
    } else {
        int t = 0, nullable = 0;
        for (const char *s = m->signature; *s; s++) {
            if (*s >= '0' && *s <= '9') continue;
            if (*s == '?') { nullable = 1; continue; }
            const struct wl_interface *ti = m->types ? m->types[t] : NULL;
            uint32_t id;
            switch (*s) {
            case 'i': case 'u': case 'f': put32(num()); break;
            case 's': put_string(nullable); break;
            case 'o': pick_obj(p, ti, &id); put32(id); break;
            case 'n':                   /* a child takes its parent's version */
                put32(p->next_id);
                if (ti) add_obj(p, p->next_id, ti, self_ver < ti->version ? self_ver : ti->version);
                p->next_id++;
                break;
            case 'a': {
                uint8_t b[64]; size_t n = u8() % sizeof b;
                for (size_t i = 0; i < n; i++) b[i] = u8();
                put_bytes(b, n, 0);
                break;
            }
            case 'h': {
                int fd = memfd_create("fuzz-aurwl-pool", MFD_CLOEXEC);
                if (fd < 0) abort();
                if (ftruncate(fd, (off_t)(u8() % 65) * 16384) < 0) abort();
                mfds[nfds++] = fd;
                break;
            }
            }
            t++; nullable = 0;
        }
    }
    if (debug)
        fprintf(stderr, "  prog%d %s@%u.%s (%zu bytes)\n", (int)(p - P), I->name, self, m->name, mlen);
    uint32_t hdr = (uint32_t)(mlen << 16) | (uint32_t)op;
    memcpy(msg + 4, &hdr, 4);

    struct iovec io = { msg, mlen };
    char cbuf[CMSG_SPACE(sizeof(int) * 8)];
    struct msghdr mh = { 0 };
    mh.msg_iov = &io; mh.msg_iovlen = 1;
    if (nfds) {
        mh.msg_control = cbuf; mh.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)nfds);
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)nfds);
        memcpy(CMSG_DATA(cm), mfds, sizeof(int) * (size_t)nfds);
    }
    ssize_t k = sendmsg(p->fd, &mh, MSG_DONTWAIT | MSG_NOSIGNAL);
    for (int i = 0; i < nfds; i++) close(mfds[i]);
    (void)k;
    if (destroys)
        for (int i = 0; i < p->n; i++)
            if (p->o[i].id == self) { p->o[i] = p->o[--p->n]; break; }
}

/* ── what the shell does between requests ────────────────────────── */
static void shell_step(void)
{
    /* Every value is read into a local first: the order C evaluates
     * function arguments in is unspecified, and a seed must mean the
     * same thing to every compiler. */
    int n = aurwl_window_count(C);
    uint8_t wi = u8();
    aurwl_win *w = n ? aurwl_window_at(C, wi % n) : NULL;
    uint32_t t = u32();
    uint8_t what = u8();
    if (debug) fprintf(stderr, "  shell %d on window %d of %d\n", what % 14, n ? wi % n : -1, n);
    switch (what % 14) {
    case 0: if (w) {
        int cw = (int)num(), ch = (int)num();
        uint8_t act = u8(), max = u8(), full = u8();
        aurwl_win_configure(w, cw, ch, act & 1, max & 1, full & 1);
    } break;
    case 1: aurwl_set_focus(C, w); break;
    case 2: { int x = (int)num(), y = (int)num(); aurwl_pointer_motion(C, w, x, y, t); } break;
    case 3: { uint8_t b = u8(), down = u8(); aurwl_pointer_button(C, 0x110 + b % 3, down & 1, t); } break;
    case 4: { uint8_t h = u8(), v = u8(); aurwl_pointer_axis(C, h & 1, (double)(int8_t)v / 4.0, t); } break;
    case 5: {
        uint32_t code = u8();
        uint8_t down = u8();
        char txt[16];
        (void)aurwl_key_utf8(C, code, txt, sizeof txt);
        (void)aurwl_key(C, code, down & 1, t);
        aurwl_update_modifiers(C);
        break;
    }
    case 6: aurwl_frame_done(C, t); break;
    case 7: if (w) {
        /* What a painter reads: every corner of the content it is
         * handed, through the stride it is told. */
        surface *s = aurwl_win_content(w);
        if (s && s->w > 0 && s->h > 0) {
            volatile uint32_t a = s->px[0];
            volatile uint32_t b = s->px[(size_t)(s->h - 1) * s->stride + (size_t)(s->w - 1)];
            (void)a; (void)b;
        }
        int x, y;
        aurwl_win_content_offset(w, &x, &y);
        aurwl_win_logical_size(w, &x, &y);
        aurwl_win_pref_size(w, &x, &y);
        aurwl_win_popup_offset(w, &x, &y);
        (void)strlen(aurwl_win_title(w)); (void)strlen(aurwl_win_app_id(w));
        (void)aurwl_win_is_popup(w); (void)aurwl_win_parent(w); (void)aurwl_win_id(w);
    } break;
    case 8: if (w) aurwl_win_close(w); break;
    case 9: { uint8_t k = u8(); if (w && k == 7) aurwl_win_kill(w); } break;
    case 10: { uint8_t a = u8(), b = u8(); aurwl_resize_output(C, 1 + a % 64 * 40, 1 + b % 64 * 30); } break;
    default: aurwl_dispatch(C); break;
    }
}

/* libwayland narrates every protocol error it posts; at a few thousand
 * inputs a second that is all the fuzzer would do. */
static void quiet(const char *fmt, va_list ap) { (void)fmt; (void)ap; }

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    static char dir[256];
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    snprintf(dir, sizeof dir, "%s/fuzz-aurwl-XXXXXX", base);
    if (!mkdtemp(dir)) abort();
    setenv("XDG_RUNTIME_DIR", dir, 1);
    debug = getenv("FUZZ_AURWL_DEBUG") != NULL;
    if (!debug) wl_log_set_handler_server(quiet);
    C = aurwl_create(1280, 720, 60000);
    if (!C) abort();
    P[0].fd = P[1].fd = -1;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!C) LLVMFuzzerInitialize(NULL, NULL);
    if (size > 16384) return 0;
    in_p = data; in_end = data + size; in_done = 0;
    aurwl_resize_output(C, 1280, 720);
    prog_start(&P[0]);
    prog_start(&P[1]);

    for (int steps = 0; steps < 2000 && !in_done; steps++) {
        uint8_t b = u8();
        if ((b & 7) == 7) shell_step();
        else {
            prog *p = &P[b >> 7];
            if (!p->alive) { prog_end(p); prog_start(p); }
            request(p);
        }
        aurwl_dispatch(C);
        prog_drain(&P[0]); prog_drain(&P[1]);
    }

    /* For whoever writes seeds: did that input get as far as it meant to? */
    if (debug)
        fprintf(stderr, "windows=%d focus=%d selection=%d alive=%d,%d objects=%d,%d\n",
                aurwl_window_count(C), !!C->focus, !!C->selection,
                P[0].alive, P[1].alive, P[0].n, P[1].n);
    prog_end(&P[0]);
    prog_end(&P[1]);
    aurwl_dispatch(C);

    /* Both programs are gone; so must be everything they had. */
    if (aurwl_window_count(C) || C->focus || C->ptr_focus || C->selection ||
        !wl_list_empty(&C->surfaces) || !wl_list_empty(&C->seats) ||
        !wl_list_empty(&C->pointers) || !wl_list_empty(&C->keyboards) ||
        !wl_list_empty(&C->touches) || !wl_list_empty(&C->outputs) ||
        !wl_list_empty(&C->devices) || !wl_list_empty(&C->offers) ||
        !wl_list_empty(&C->xdg_outputs)) {
        fprintf(stderr, "aurwl kept state for a program that has gone\n");
        abort();
    }
    return 0;
}
