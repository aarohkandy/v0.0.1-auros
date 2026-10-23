/* wltest-x-clip.inc -- the clipboard between X programs and Wayland programs.
 *
 * Included by wltest-x.c after its helpers. Besides the pretend X server and the
 * pretend Xwayland, there is now a pretend Wayland PROGRAM: a second child with a
 * window and a data device, driven line by line like the first.
 *
 *   W              open a window (so it can have the keyboard)
 *   T <text>       copy text (text/plain;charset=utf-8)
 *   I <mime> <n>   copy n bytes of a known pattern as <mime> (an image, say)
 *   S              copy something it then never writes (a program that hangs)
 *   X              clear its copy
 *   V <mime>       paste: reply "<bytes> <fnv32>" of what arrived ("0 811c9dc5" if nothing)
 *   M              reply the mime types the clipboard offers it, comma separated, or "none"
 *   K              reply how many times its copy was cancelled (somebody else's is newer)
 *
 * What is proved, both ways: text, a picture too big for one request (300 000
 * bytes, with INCR on the X side and without), Latin-1 for programs that only know
 * STRING, TARGETS, a target nobody has, a program that never writes, an owner that
 * refuses, the newest copy winning, an X owner that goes away. */
#include "xdg-shell-client.h"

/* ── the pretend Wayland program ─────────────────────────────────── */
struct ap {
    struct wl_display *d;
    struct wl_compositor *comp; struct wl_shm *shm; struct xdg_wm_base *wm;
    struct wl_data_device_manager *ddm; struct wl_seat *seat; struct wl_data_device *dev;
    struct wl_data_offer *pending, *cur;
    char mimes[16][64]; int nm, cur_nm;
    struct wl_data_source *src;
    uint8_t *payload; size_t plen; int hang;
    int cancelled, configured;
};
static void pattern(uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(i * 31 + 7 + (i >> 8)); }
static uint32_t fnv(const uint8_t *p, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h; }

static void ap_reg(void *data, struct wl_registry *r, uint32_t name, const char *i, uint32_t v)
{
    struct ap *a = data; (void)v;
    if (!strcmp(i, "wl_compositor")) a->comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(i, "wl_shm")) a->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(i, "xdg_wm_base")) a->wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(i, "wl_data_device_manager")) a->ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, 3);
    else if (!strcmp(i, "wl_seat")) a->seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
}
static void ap_reg_rm(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener ap_reg_l = { ap_reg, ap_reg_rm };
static void ap_ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener ap_wm_l = { ap_ping };
static void ap_xs_conf(void *d, struct xdg_surface *xs, uint32_t serial) { struct ap *a = d; xdg_surface_ack_configure(xs, serial); a->configured = 1; }
static const struct xdg_surface_listener ap_xs_l = { ap_xs_conf };
static void ap_tl_conf(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s) { (void)d; (void)t; (void)w; (void)h; (void)s; }
static void ap_tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; }
static const struct xdg_toplevel_listener ap_tl_l = { .configure = ap_tl_conf, .close = ap_tl_close };

static void ap_offer_offer(void *d, struct wl_data_offer *o, const char *mime)
{
    struct ap *a = d; (void)o;
    if (a->nm < 16) snprintf(a->mimes[a->nm++], sizeof a->mimes[0], "%s", mime);
}
static const struct wl_data_offer_listener ap_offer_l = { .offer = ap_offer_offer };
static void ap_dev_offer(void *d, struct wl_data_device *dev, struct wl_data_offer *o)
{
    struct ap *a = d; (void)dev;
    a->nm = 0; a->pending = o;
    wl_data_offer_add_listener(o, &ap_offer_l, a);
}
static void ap_dev_selection(void *d, struct wl_data_device *dev, struct wl_data_offer *o)
{
    struct ap *a = d; (void)dev;
    a->cur = o; a->cur_nm = o ? a->nm : 0;
}
static void ap_dev_noop() {}
static const struct wl_data_device_listener ap_dev_l = {
    .data_offer = ap_dev_offer, .enter = (void *)ap_dev_noop, .leave = (void *)ap_dev_noop,
    .motion = (void *)ap_dev_noop, .drop = (void *)ap_dev_noop, .selection = ap_dev_selection,
};
static void ap_src_target(void *d, struct wl_data_source *s, const char *m) { (void)d; (void)s; (void)m; }
static void ap_src_send(void *d, struct wl_data_source *s, const char *mime, int32_t fd)
{
    struct ap *a = d; (void)s; (void)mime;
    if (a->hang) return;                                    /* never writes, never closes: the paste has to give up */
    size_t off = 0;
    while (off < a->plen) { ssize_t k = write(fd, a->payload + off, a->plen - off); if (k <= 0) break; off += (size_t)k; }
    close(fd);
}
static void ap_src_cancelled(void *d, struct wl_data_source *s) { struct ap *a = d; a->cancelled++; wl_data_source_destroy(s); a->src = NULL; }
static const struct wl_data_source_listener ap_src_l = {
    .target = ap_src_target, .send = ap_src_send, .cancelled = ap_src_cancelled,
    .dnd_drop_performed = (void *)ap_dev_noop, .dnd_finished = (void *)ap_dev_noop, .action = (void *)ap_dev_noop,
};

static void ap_copy(struct ap *a, const char *mime, const uint8_t *data, size_t n, int hang)
{
    free(a->payload); a->payload = malloc(n + 1); memcpy(a->payload, data, n); a->plen = n; a->hang = hang;
    if (a->src) { wl_data_source_destroy(a->src); a->src = NULL; }
    a->src = wl_data_device_manager_create_data_source(a->ddm);
    wl_data_source_add_listener(a->src, &ap_src_l, a);
    wl_data_source_offer(a->src, mime);
    wl_data_device_set_selection(a->dev, a->src, 1);
    wl_display_roundtrip(a->d);
}

static int be_wlapp(int sock, int cmd, int res)
{
    struct ap a = {0};
    signal(SIGPIPE, SIG_IGN);                        /* a paste that is refused closes the pipe under a writer */
    a.d = wl_display_connect_to_fd(sock);
    if (!a.d) return 1;
    wl_registry_add_listener(wl_display_get_registry(a.d), &ap_reg_l, &a);
    wl_display_roundtrip(a.d);
    if (!a.comp || !a.shm || !a.wm || !a.ddm || !a.seat) return 2;
    xdg_wm_base_add_listener(a.wm, &ap_wm_l, &a);
    a.dev = wl_data_device_manager_get_data_device(a.ddm, a.seat);
    wl_data_device_add_listener(a.dev, &ap_dev_l, &a);
    wl_display_roundtrip(a.d);
    for (;;) {
        while (wl_display_prepare_read(a.d) != 0) wl_display_dispatch_pending(a.d);
        wl_display_flush(a.d);
        struct pollfd p[2] = { { .fd = cmd, .events = POLLIN }, { .fd = wl_display_get_fd(a.d), .events = POLLIN } };
        poll(p, 2, -1);
        if (p[1].revents & POLLIN) wl_display_read_events(a.d); else wl_display_cancel_read(a.d);
        wl_display_dispatch_pending(a.d);
        if (!(p[0].revents & (POLLIN | POLLHUP))) continue;
        char line[256]; size_t n = 0;
        while (n < sizeof line - 1) { char ch; if (read(cmd, &ch, 1) != 1) return 0; if (ch == '\n') break; line[n++] = ch; }
        line[n] = 0;
        char out[512] = "ok";
        if (line[0] == 'W') {
            struct wl_surface *s = wl_compositor_create_surface(a.comp);
            struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(a.wm, s);
            xdg_surface_add_listener(xs, &ap_xs_l, &a);
            struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
            xdg_toplevel_add_listener(tl, &ap_tl_l, &a);
            wl_surface_commit(s);
            for (int i = 0; i < 20 && !a.configured; i++) wl_display_roundtrip(a.d);
            int fd = memfd_create("w", MFD_CLOEXEC);
            if (fd < 0 || ftruncate(fd, 40 * 30 * 4) < 0) return 3;
            struct wl_shm_pool *pool = wl_shm_create_pool(a.shm, fd, 40 * 30 * 4);
            struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, 40, 30, 160, WL_SHM_FORMAT_XRGB8888);
            wl_surface_attach(s, b, 0, 0); wl_surface_damage(s, 0, 0, 40, 30); wl_surface_commit(s);
            wl_display_roundtrip(a.d);
        } else if (line[0] == 'T') {
            ap_copy(&a, "text/plain;charset=utf-8", (const uint8_t *)line + 2, strlen(line + 2), 0);
        } else if (line[0] == 'I') {
            char mime[64]; size_t cnt = 0;
            if (sscanf(line + 2, "%63s %zu", mime, &cnt) != 2 || cnt > (20u << 20)) return 4;
            uint8_t *buf = malloc(cnt + 1); pattern(buf, cnt);
            ap_copy(&a, mime, buf, cnt, 0);
            free(buf);
        } else if (line[0] == 'S') {
            ap_copy(&a, "text/plain;charset=utf-8", (const uint8_t *)"never", 5, 1);
        } else if (line[0] == 'X') {
            wl_data_device_set_selection(a.dev, NULL, 1);
            wl_display_roundtrip(a.d);
        } else if (line[0] == 'K') {
            snprintf(out, sizeof out, "%d", a.cancelled);
        } else if (line[0] == 'M') {
            wl_display_roundtrip(a.d);
            out[0] = 0;
            if (!a.cur || !a.cur_nm) snprintf(out, sizeof out, "none");
            for (int i = 0; a.cur && i < a.cur_nm; i++) { if (i) strcat(out, ","); strncat(out, a.mimes[i], sizeof out - strlen(out) - 2); }
        } else if (line[0] == 'V') {
            wl_display_roundtrip(a.d);
            int pf[2];
            uint32_t h = 2166136261u; size_t total = 0;
            if (a.cur && pipe(pf) == 0) {
                wl_data_offer_receive(a.cur, line + 2, pf[1]);
                wl_display_flush(a.d);
                close(pf[1]);
                for (;;) {
                    struct pollfd rp = { .fd = pf[0], .events = POLLIN };
                    if (poll(&rp, 1, 5000) <= 0) break;
                    uint8_t buf[65536]; ssize_t k = read(pf[0], buf, sizeof buf);
                    if (k <= 0) break;
                    for (ssize_t i = 0; i < k; i++) h = (h ^ buf[i]) * 16777619u;
                    total += (size_t)k;
                }
                close(pf[0]);
            }
            snprintf(out, sizeof out, "%zu %08x", total, h);
        }
        strcat(out, "\n");
        if (write(res, out, strlen(out)) < 0) return 5;
    }
}

static pid_t app_kid;
static int app_cmd = -1, app_res = -1;
static void app_start(void)
{
    int ws[2], cp[2], rp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ws) || pipe2(cp, O_CLOEXEC) || pipe2(rp, O_CLOEXEC)) abort();
    fflush(stdout);
    app_kid = fork();
    if (app_kid == 0) { close(ws[0]); close(cp[1]); close(rp[0]); close(cmd_fd); close(res_fd); close(X.fd); _exit(be_wlapp(ws[1], cp[0], rp[1])); }
    close(ws[1]); close(cp[0]); close(rp[1]);
    wl_client_create(C->display, ws[0]);
    app_cmd = cp[1]; app_res = rp[0];
}
static void app_finish(void)
{
    if (app_cmd >= 0) close(app_cmd);
    if (app_res >= 0) close(app_res);
    app_cmd = app_res = -1;
    int st; if (app_kid > 0) waitpid(app_kid, &st, 0);
    app_kid = 0;
}
/* Say a line to the program, and hear its one line back (while the compositor and the X side run). */
static const char *ask_app(const char *line)
{
    static char buf[512];
    size_t n = 0; buf[0] = 0;
    if (write(app_cmd, line, strlen(line)) < 0 || write(app_cmd, "\n", 1) < 0) return "";
    for (int i = 0; i < 1500 && n < sizeof buf - 1; i++) {
        pump(10);
        struct pollfd p = { .fd = app_res, .events = POLLIN };
        while (poll(&p, 1, 0) > 0) {
            char ch;
            if (read(app_res, &ch, 1) != 1) return buf;
            if (ch == '\n') { buf[n] = 0; return buf; }
            buf[n++] = ch;
        }
    }
    buf[n] = 0;
    return buf;
}
static aurwl_win *app_window(void)
{
    for (int i = 0; i < aurwl_window_count(C); i++) { aurwl_win *w = aurwl_window_at(C, i); if (w->role == ROLE_TOPLEVEL) return w; }
    return NULL;
}

/* ── events from the pretend X server ─────────────────────────────── */
static void ev_sel_request(uint32_t requestor, uint32_t selection, uint32_t target, uint32_t prop)
{
    uint8_t f[28] = {0};
    xw32(f + 4, C->x.wm->check); xw32(f + 8, requestor); xw32(f + 12, selection); xw32(f + 16, target); xw32(f + 20, prop);
    ev32(30, f);
}
static void ev_xfixes_owner(uint32_t owner)
{
    uint8_t f[28] = {0};
    xw32(f, FX_ROOT); xw32(f + 4, owner); xw32(f + 8, atom_id("CLIPBOARD"));
    ev32(87, f);
}

/* What the window manager said to a requestor: the ChangeProperty, and the SelectionNotify. */
static req *got_prop(uint32_t requestor, int from) { return last_req(18, requestor, from); }
static req *got_notify(uint32_t requestor, int from) { return last_req(25, requestor, from); }
static uint32_t notify_prop(const req *n) { return n ? xr32(n->body + 8 + 20) : 0xdeadbeef; }   /* event: body+8; property at +20 */

static void clip_start(void)
{
    start();
    app_start();
    ask_app("W");
    aurwl_win *w = app_window();
    if (w) aurwl_set_focus(C, w);
    for (int i = 0; i < 4; i++) { free(SP[i].data); SP[i].data = NULL; SP[i].used = 0; }
    memset(&XO, 0, sizeof XO);
    xc_timeout_ms = 10000;
}
static void clip_finish(void)
{
    app_finish();
    free(XO.text); free(XO.img); XO.text = NULL; XO.img = NULL;
    finish();
}

/* ── Wayland -> X ─────────────────────────────────────────────────── */
static void sc_clip_text_wl_to_x(void)
{
    clip_start();
    check(app_window() != NULL, "setup: the Wayland program has no window");
    XO.n_targets = 0; XO.targets[XO.n_targets++] = atom_id("TARGETS"); XO.targets[XO.n_targets++] = atom_id("UTF8_STRING");
    XO.text = malloc(6); memcpy(XO.text, "stale", 5); XO.text_n = 5;       /* what a (mistaken) question to the X side would find */
    int base = X.n_reqs;
    ask_app("I image/png 10");                                        /* so that "image/png" is a name the window manager knows ... */
    WAIT(last_req(22, C->x.wm->check, base));
    base = X.n_reqs;
    ask_app("T hello from wayland");                                  /* ... and the copy it is now asked about has only text */
    WAIT(last_req(22, C->x.wm->check, base));
    req *so = last_req(22, C->x.wm->check, base);
    check(so && xr32(so->body + 4) == atom_id("CLIPBOARD"), "the window manager did not take the CLIPBOARD selection for its window");
    check(so != NULL, "the text copy was not taken for X");

    uint32_t rq = 0x500001;
    base = X.n_reqs;
    ev_sel_request(rq, atom_id("CLIPBOARD"), atom_id("TARGETS"), 0x601);                     /* what can you give me? */
    WAIT(got_notify(rq, base));
    req *cp = got_prop(rq, base);
    check(cp && xr32(cp->body + 4) == 0x601 && xr32(cp->body + 8) == XP_ATOM && cp->body[12] == 32, "TARGETS was not answered as a list of atoms");
    int has_utf8 = 0, has_string = 0, has_targets = 0;
    for (uint32_t i = 0; cp && i < xr32(cp->body + 16) && 20 + 4 * i + 4 <= sizeof cp->body; i++) {
        uint32_t a = xr32(cp->body + 20 + 4 * i);
        if (a == atom_id("UTF8_STRING")) has_utf8 = 1;
        if (a == XP_STRING) has_string = 1;
        if (a == atom_id("TARGETS")) has_targets = 1;
    }
    check(has_utf8 && has_string && has_targets, "TARGETS lacks UTF8_STRING (%d), STRING (%d) or TARGETS (%d)", has_utf8, has_string, has_targets);
    check(notify_prop(got_notify(rq, base)) == 0x601, "the program was not told the TARGETS answer is in its property");

    base = X.n_reqs;
    ev_sel_request(rq, atom_id("CLIPBOARD"), atom_id("UTF8_STRING"), 0x602);                  /* the text itself */
    WAIT(got_notify(rq, base));
    cp = got_prop(rq, base);
    const char *want = "hello from wayland";
    check(cp && xr32(cp->body + 8) == atom_id("UTF8_STRING") && cp->dlen == strlen(want) && cp->hash == fnv((const uint8_t *)want, strlen(want)),
          "the text was not delivered byte for byte (%u bytes)", cp ? cp->dlen : 0);
    check(notify_prop(got_notify(rq, base)) == 0x602, "the program was not told its text is in the property");

    base = X.n_reqs;
    ev_sel_request(rq, atom_id("CLIPBOARD"), atom_id("image/png"), 0x603);                    /* nobody has this */
    WAIT(got_notify(rq, base));
    check(notify_prop(got_notify(rq, base)) == 0 && !got_prop(rq, base), "a target nobody offers was not refused");
    check(!strcmp(ask_app("K"), "0"), "the program's own copy was cancelled: the window manager took its own claim for an X program's");
    clip_finish();
}

static void sc_clip_latin1(void)
{
    clip_start();
    int base = X.n_reqs;
    ask_app("T caf\xc3\xa9 \xc4\x80 \xe2\x98\x83!");
    WAIT(last_req(22, C->x.wm->check, base));
    uint32_t rq = 0x500002;
    base = X.n_reqs;
    ev_sel_request(rq, atom_id("CLIPBOARD"), XP_STRING, 0x604);                               /* a program that only knows STRING */
    WAIT(got_notify(rq, base));
    req *cp = got_prop(rq, base);
    const uint8_t want[] = { 'c', 'a', 'f', 0xe9, ' ', '?', ' ', '?', '!' };
    check(cp && xr32(cp->body + 8) == XP_STRING && cp->dlen == sizeof want && cp->hash == fnv(want, sizeof want),
          "STRING is not Latin-1 with ? for what does not fit (%u bytes)", cp ? cp->dlen : 0);
    clip_finish();
}

static void sc_clip_image_wl_to_x(void)
{
    clip_start();
    int base = X.n_reqs;
    ask_app("I image/png 300000");                                                           /* more than a plain request carries */
    WAIT(last_req(22, C->x.wm->check, base));
    check(last_req(22, C->x.wm->check, base) != NULL, "the selection was not taken for a picture");
    uint32_t rq = 0x500003;
    base = X.n_reqs;
    ev_sel_request(rq, atom_id("CLIPBOARD"), atom_id("image/png"), 0x605);
    WAIT(got_notify(rq, base));
    uint8_t *pat = malloc(300000); pattern(pat, 300000);
    req *cp = got_prop(rq, base);
    check(cp && xr32(cp->body + 8) == atom_id("image/png") && cp->dlen == 300000 && cp->hash == fnv(pat, 300000),
          "the picture was not delivered byte for byte (%u of 300000)", cp ? cp->dlen : 0);
    check(notify_prop(got_notify(rq, base)) == 0x605, "the program was not told its picture is in the property");
    free(pat);
    clip_finish();
}

static void sc_clip_wl_hangs(void)
{
    clip_start();
    int base = X.n_reqs;
    ask_app("S");
    WAIT(last_req(22, C->x.wm->check, base));
    xc_timeout_ms = 300;
    uint32_t rq = 0x500004;
    base = X.n_reqs;
    ev_sel_request(rq, atom_id("CLIPBOARD"), atom_id("UTF8_STRING"), 0x606);
    for (int i = 0; i < 100 && !got_notify(rq, base); i++) pump(10);
    check(got_notify(rq, base) && notify_prop(got_notify(rq, base)) == 0, "a program that never writes held the X program for ever");
    check(wl_list_empty(&C->x.wm->xfers), "the transfer was not given up");
    clip_finish();
}

/* ── X -> Wayland ─────────────────────────────────────────────────── */
static void x_owner_with(const char *text, size_t img_n, int incr)
{
    uint32_t img = atom_id("image/png");
    XO.n_targets = 0;
    XO.targets[XO.n_targets++] = atom_id("TARGETS");
    if (text) { XO.targets[XO.n_targets++] = atom_id("UTF8_STRING"); XO.targets[XO.n_targets++] = XP_STRING; XO.targets[XO.n_targets++] = atom_id("TEXT"); }
    if (img_n) XO.targets[XO.n_targets++] = img;
    free(XO.text); free(XO.img); XO.text = NULL; XO.img = NULL;
    if (text) { XO.text_n = strlen(text); XO.text = malloc(XO.text_n + 1); memcpy(XO.text, text, XO.text_n); }
    if (img_n) { XO.img = malloc(img_n); pattern(XO.img, img_n); XO.img_n = img_n; XO.img_atom = img; }
    XO.incr = incr;
    ev_xfixes_owner(0x500010);
}
static int wait_offer(void)
{
    for (int i = 0; i < 100; i++) { if (strcmp(ask_app("M"), "none")) return 1; pump(10); }
    return 0;
}
static void paste_check(const char *mime, const uint8_t *want, size_t n, const char *what)
{
    char line[96]; snprintf(line, sizeof line, "V %s", mime);
    const char *got = ask_app(line);
    char exp[64]; snprintf(exp, sizeof exp, "%zu %08x", n, fnv(want, n));
    check(!strcmp(got, exp), "%s: pasted \"%s\", expected \"%s\"", what, got, exp);
}

static void sc_clip_text_x_to_wl(void)
{
    clip_start();
    x_owner_with("hello from x \xe2\x98\x83", 0, 0);
    check(wait_offer(), "the Wayland program was never offered an X program's copy");
    const char *m = ask_app("M");
    check(strstr(m, "text/plain;charset=utf-8") && strstr(m, "text/plain"), "text/plain types are not offered (\"%s\")", m);
    const char *t = "hello from x \xe2\x98\x83";
    paste_check("text/plain;charset=utf-8", (const uint8_t *)t, strlen(t), "text");
    paste_check("text/plain", (const uint8_t *)t, strlen(t), "plain text");
    ev_xfixes_owner(0);                                                                      /* the X program quits */
    for (int i = 0; i < 100 && strcmp(ask_app("M"), "none"); i++) pump(10);
    check(!strcmp(ask_app("M"), "none"), "the offer outlived the X program that made it");
    paste_check("text/plain;charset=utf-8", (const uint8_t *)"", 0, "a paste with nothing there");
    clip_finish();
}

static void sc_clip_image_x_to_wl(void)
{
    clip_start();
    x_owner_with(NULL, 300000, 0);
    check(wait_offer(), "no offer for an X program's picture");
    check(strstr(ask_app("M"), "image/png") != NULL, "image/png is not offered (\"%s\")", ask_app("M"));
    uint8_t *pat = malloc(300000); pattern(pat, 300000);
    paste_check("image/png", pat, 300000, "a picture in one property");
    free(pat);
    clip_finish();
}

static void sc_clip_incr(void)
{
    clip_start();
    x_owner_with(NULL, 450000, 1);                                                            /* sent in chunks of 100 000 */
    check(wait_offer(), "no offer for an X program's picture");
    int base = X.n_reqs;
    uint8_t *pat = malloc(450000); pattern(pat, 450000);
    paste_check("image/png", pat, 450000, "a picture by INCR");
    int deletes = 0;
    for (int i = base; i < X.n_reqs; i++) if (X.reqs[i].op == 20 && X.reqs[i].d1 == 1) deletes++;
    check(deletes >= 6, "INCR was not followed chunk by chunk (%d reads)", deletes);
    free(pat);
    clip_finish();
}

static void sc_clip_refused(void)
{
    clip_start();
    XO.n_targets = 0; free(XO.text); XO.text = NULL;                                          /* an owner that answers no TARGETS: refuses */
    XO.n_targets = -1;
    ev_xfixes_owner(0x500011);
    SETTLE();
    check(!strcmp(ask_app("M"), "none"), "an owner that would not say what it has was offered anyway");
    XO.n_targets = 0;
    clip_finish();
}

/* ── who wins ─────────────────────────────────────────────────────── */
static void sc_clip_newest_wins(void)
{
    clip_start();
    int base = X.n_reqs;
    ask_app("T from the program");                                                           /* a Wayland copy */
    WAIT(last_req(22, C->x.wm->check, base));
    x_owner_with("from x", 0, 0);                                                            /* then an X copy: newer */
    for (int i = 0; i < 100 && strcmp(ask_app("K"), "1"); i++) pump(10);
    check(!strcmp(ask_app("K"), "1"), "the older Wayland copy was not cancelled when an X program copied");
    check(wait_offer(), "the X copy was not offered");
    base = X.n_reqs;
    ask_app("T newest");                                                                     /* then a Wayland copy again */
    WAIT(last_req(22, C->x.wm->check, base));
    check(last_req(22, C->x.wm->check, base) != NULL, "the Wayland copy was not taken back for X");
    check(!C->xsel_active, "an X copy is still offered after a newer Wayland copy");
    clip_finish();
}

/* ── more than a transfer may carry: refused, never cut short ──────── */
static void sc_clip_too_big(void)
{
    clip_start();
    int base = X.n_reqs;
    ask_app("I image/png 17000000");                                                         /* over 16 MiB */
    WAIT(last_req(22, C->x.wm->check, base));
    uint32_t rq = 0x500005;
    base = X.n_reqs;
    ev_sel_request(rq, atom_id("CLIPBOARD"), atom_id("image/png"), 0x607);
    for (int i = 0; i < 600 && !got_notify(rq, base); i++) pump(10);
    check(got_notify(rq, base) && notify_prop(got_notify(rq, base)) == 0 && !got_prop(rq, base),
          "a picture over the limit was not refused (it must not arrive cut short)");
    ask_app("X");
    x_owner_with(NULL, 17000000, 0);
    check(wait_offer(), "no offer for the big X picture");
    paste_check("image/png", (const uint8_t *)"", 0, "a picture over the limit pasted from X");
    clip_finish();
}
