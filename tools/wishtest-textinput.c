/* wishtest-textinput.c — can the compositor tell a password field? (docs/AI.md §3.5)
 *
 * The REAL compositor (src/aurwl/aurwl.c with textinput.c and wishhook.c
 * linked in) and a REAL Wayland client in a second process, speaking
 * zwp_text_input_v3 the way GTK does: wait for `enter`, `enable`, say what
 * the field is, `commit`. The parent then types a registered word with
 * aurwl_key() -- the same call the shell makes -- and says whether the
 * matcher reported it.
 *
 * What is proven:
 *   - a field declared password, PIN, terminal, an unknown purpose, or
 *     carrying the hidden_text / sensitive_data hint: the word is NOT
 *     reported, and nothing is held back to be reported later;
 *   - an ordinary field: it is;
 *   - a program that never announces a field: reported for an ordinary
 *     program, NOT for a browser (or a program that will not say who it is);
 *   - leaving a secret field for an ordinary one carries nothing across;
 *   - the compositor survives a client that misbehaves: requests before
 *     `enter`, commit with nothing enabled, two inputs enabled at once,
 *     more text inputs than it will give, the object destroyed while
 *     focused, surrounding text of any size.
 *
 *   wishtest-textinput              runs everything
 *   wishtest-textinput --client APP (internal) be the client
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <wayland-client.h>
#include "xdg-shell-client.h"
#include "text-input-client.h"

#include "../src/aurwl/aurwl.h"
#include "../src/aurwl/textinput.h"
#include "../src/aurwl/wishhook.h"

static int fails, passes;
static const char *cur;
#define STAGE(x) do { if (getenv("WTI_TRACE")) fprintf(stderr, "  .. %s\n", x); } while (0)
#define CHECK(c) do { if (c) passes++; else { fails++; \
    fprintf(stderr, "FAIL %s:%d %s: %s\n", __FILE__, __LINE__, cur, #c); } } while (0)

/* ── the client ─────────────────────────────────────────────────── */

struct cl {
    struct wl_display *dpy;
    struct wl_compositor *comp;
    struct xdg_wm_base *wm;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct zwp_text_input_manager_v3 *tim;
    struct zwp_text_input_v3 *ti[8];
    int entered, left, configured, nti;
    struct wl_surface *surf;
    struct wl_keyboard *kb;
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *i, uint32_t v)
{
    struct cl *c = d;
    if (!strcmp(i, "wl_compositor")) c->comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(i, "xdg_wm_base")) c->wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(i, "wl_shm")) c->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(i, "wl_seat")) c->seat = wl_registry_bind(r, name, &wl_seat_interface, v < 5 ? v : 5);
    else if (!strcmp(i, "zwp_text_input_manager_v3")) c->tim = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };
static void xs_conf(void *d, struct xdg_surface *x, uint32_t s) { struct cl *c = d; xdg_surface_ack_configure(x, s); c->configured = 1; }
static const struct xdg_surface_listener xs_l = { xs_conf };
static void tl_conf(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s) { (void)d; (void)t; (void)w; (void)h; (void)s; }
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; }
static const struct xdg_toplevel_listener tl_l = { .configure = tl_conf, .close = tl_close };
static void ti_enter(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s) { struct cl *c = d; (void)t; (void)s; c->entered++; }
static void ti_leave(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s) { struct cl *c = d; (void)t; (void)s; c->left++; c->entered = c->entered ? c->entered - 1 : 0; }
static void ti_pre(void *d, struct zwp_text_input_v3 *t, const char *x, int32_t a, int32_t b) { (void)d; (void)t; (void)x; (void)a; (void)b; }
static void ti_com(void *d, struct zwp_text_input_v3 *t, const char *x) { (void)d; (void)t; (void)x; }
static void ti_del(void *d, struct zwp_text_input_v3 *t, uint32_t a, uint32_t b) { (void)d; (void)t; (void)a; (void)b; }
static void ti_done(void *d, struct zwp_text_input_v3 *t, uint32_t s) { (void)d; (void)t; (void)s; }
static const struct zwp_text_input_v3_listener ti_l = { ti_enter, ti_leave, ti_pre, ti_com, ti_del, ti_done };

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t s) { (void)d; (void)k; (void)f; (void)s; close(fd); }
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *su, struct wl_array *a) { (void)d; (void)k; (void)s; (void)su; (void)a; }
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *su) { (void)d; (void)k; (void)s; (void)su; }
static void kb_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t st) { (void)d; (void)k; (void)s; (void)t; (void)key; (void)st; }
static void kb_mod(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t e) { (void)d; (void)k; (void)s; (void)a; (void)b; (void)c; (void)e; }
static void kb_rep(void *d, struct wl_keyboard *k, int32_t a, int32_t b) { (void)d; (void)k; (void)a; (void)b; }
static const struct wl_keyboard_listener kb_l = { kb_keymap, kb_enter, kb_leave, kb_key, kb_mod, kb_rep };
static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
    struct cl *c = d;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !c->kb) {
        c->kb = wl_seat_get_keyboard(s);
        wl_keyboard_add_listener(c->kb, &kb_l, c);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_l = { seat_caps, seat_name };

static void say(int fd, char ch) { if (write(fd, &ch, 1) < 0) _exit(3); }

static int client_main(const char *sock, const char *app, int up, int down)
{
    struct cl c;
    memset(&c, 0, sizeof c);
    c.dpy = wl_display_connect(sock);
    if (!c.dpy) return 2;
    struct wl_registry *reg = wl_display_get_registry(c.dpy);
    wl_registry_add_listener(reg, &reg_l, &c);
    wl_display_roundtrip(c.dpy);
    if (!c.comp || !c.wm || !c.shm || !c.seat || !c.tim) return 4;
    xdg_wm_base_add_listener(c.wm, &wm_l, &c);
    wl_seat_add_listener(c.seat, &seat_l, &c);
    wl_display_roundtrip(c.dpy);
    c.ti[0] = zwp_text_input_manager_v3_get_text_input(c.tim, c.seat);
    zwp_text_input_v3_add_listener(c.ti[0], &ti_l, &c);
    c.nti = 1;

    c.surf = wl_compositor_create_surface(c.comp);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(c.wm, c.surf);
    xdg_surface_add_listener(xs, &xs_l, &c);
    struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_l, &c);
    if (app && *app) xdg_toplevel_set_app_id(tl, app);
    wl_surface_commit(c.surf);
    while (!c.configured) if (wl_display_dispatch(c.dpy) < 0) return 5;
    int fd = memfd_create("px", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, 64 * 64 * 4) < 0) return 6;
    struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 64 * 64 * 4);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, 64, 64, 256, WL_SHM_FORMAT_ARGB8888);
    wl_surface_attach(c.surf, buf, 0, 0);
    wl_surface_commit(c.surf);
    wl_display_roundtrip(c.dpy);
    say(down, 'M');                                  /* mapped */

    for (;;) {
        struct pollfd p[2] = { { wl_display_get_fd(c.dpy), POLLIN, 0 }, { up, POLLIN, 0 } };
        wl_display_flush(c.dpy);
        if (wl_display_prepare_read(c.dpy) != 0) { wl_display_dispatch_pending(c.dpy); continue; }
        if (poll(p, 2, 100) < 0 && errno != EINTR) return 7;
        if (p[0].revents & POLLIN) { if (wl_display_read_events(c.dpy) < 0) return 8; }
        else wl_display_cancel_read(c.dpy);
        if (wl_display_dispatch_pending(c.dpy) < 0) return 9;
        if (!(p[1].revents & (POLLIN | POLLHUP))) continue;
        char ch;
        if (read(up, &ch, 1) != 1) return 0;
        struct zwp_text_input_v3 *t = c.ti[0];
        uint32_t H = ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
        switch (ch) {
        case 'e': say(down, c.entered ? 'y' : 'n'); continue;           /* has text-input focus? */
        case 'P': zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, H, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD); zwp_text_input_v3_commit(t); break;
        case 'W': zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, 0, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD); zwp_text_input_v3_commit(t); break;   /* the purpose alone */
        case 'I': zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, 0, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN); zwp_text_input_v3_commit(t); break;
        case 'T': zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, 0, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL); zwp_text_input_v3_commit(t); break;
        case 'X': zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, 0, 99); zwp_text_input_v3_commit(t); break;
        case 'S': zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL); zwp_text_input_v3_commit(t); break;
        case 'H': zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL); zwp_text_input_v3_commit(t); break;
        case 'N': zwp_text_input_v3_disable(t); zwp_text_input_v3_commit(t);
                  zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL); zwp_text_input_v3_commit(t); break;
        case 'E': zwp_text_input_v3_disable(t); zwp_text_input_v3_set_content_type(t, 0, 0); zwp_text_input_v3_commit(t);
                  zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, 0, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL); zwp_text_input_v3_commit(t); break;
        case 'U': zwp_text_input_v3_disable(t); zwp_text_input_v3_commit(t); break;
        /* the misbehaviours */
        case 'c': zwp_text_input_v3_commit(t); zwp_text_input_v3_commit(t); break;                      /* commit with nothing */
        case 'b': { struct zwp_text_input_v3 *t2 = zwp_text_input_manager_v3_get_text_input(c.tim, c.seat);
                    zwp_text_input_v3_add_listener(t2, &ti_l, &c); c.ti[c.nti++] = t2;
                    zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, 0, 0); zwp_text_input_v3_commit(t);
                    zwp_text_input_v3_enable(t2); zwp_text_input_v3_set_content_type(t2, H, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD); zwp_text_input_v3_commit(t2);
                    break; }                                                                                /* two at once */
        case 'm': for (int i = 0; i < 6; i++) { struct zwp_text_input_v3 *x = zwp_text_input_manager_v3_get_text_input(c.tim, c.seat);
                    if (c.nti < 8) c.ti[c.nti++] = x; } break;                                            /* too many */
        case 's': { char big[4000]; memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
                    zwp_text_input_v3_enable(t); zwp_text_input_v3_set_surrounding_text(t, big, 5, 5);
                    zwp_text_input_v3_set_text_change_cause(t, 7); zwp_text_input_v3_set_cursor_rectangle(t, -1, -1, -5, 1 << 30);
                    zwp_text_input_v3_set_content_type(t, 0xFFFFFFFF, 0xFFFFFFFF); zwp_text_input_v3_commit(t); break; }
        case 'd': zwp_text_input_v3_enable(t); zwp_text_input_v3_set_content_type(t, 0, 0); zwp_text_input_v3_commit(t);
                  zwp_text_input_v3_destroy(c.ti[0]); c.ti[0] = zwp_text_input_manager_v3_get_text_input(c.tim, c.seat);
                  zwp_text_input_v3_add_listener(c.ti[0], &ti_l, &c); break;                              /* destroyed while enabled */
        case 'k': xdg_toplevel_destroy(tl); xdg_surface_destroy(xs); wl_surface_destroy(c.surf); break;  /* the window goes */
        case 'Q': wl_display_roundtrip(c.dpy); return 0;
        default: break;
        }
        if (wl_display_roundtrip(c.dpy) < 0) { say(down, 'X'); return 10; }
        say(down, 'k');
    }
}

/* ── the compositor side ────────────────────────────────────────── */

static long long mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static aurwl *C;
static int up_w, down_r;
static pid_t kid;

/* Dispatches the compositor until the child says something (or ~3 s). */
static int hear(char want)
{
    long long end = mono_ms() + 3000;
    while (mono_ms() < end) {
        aurwl_dispatch(C);
        struct pollfd p = { down_r, POLLIN, 0 };
        if (poll(&p, 1, 5) > 0) {
            char ch;
            ssize_t k = read(down_r, &ch, 1);
            if (k == 1 && ch == want) return 1;
            if (k == 1 && ch == 'X') return 0;
            if (k <= 0) return 0;
        }
    }
    return 0;
}

static int cmd(char ch)
{
    if (write(up_w, &ch, 1) != 1) return 0;
    return hear('k');
}

static aurwl_win *win(void) { return aurwl_window_count(C) > 0 ? aurwl_window_at(C, 0) : NULL; }

/* Types "potato " the way the shell would: evdev codes into aurwl_key(). */
static void type_potato(void)
{
    static const int K[] = { 25, 24, 20, 30, 20, 24, 57 };
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        aurwl_key(C, (uint32_t)K[i], 1, (uint32_t)mono_ms());
        aurwl_key(C, (uint32_t)K[i], 0, (uint32_t)mono_ms());
    }
}
static int drain(void) { char w[33]; int n = 0; while (wishhook_take_word(w)) n++; return n; }
static int potato_heard(void) { drain(); type_potato(); return drain(); }

static int start(const char *app)
{
    int a[2], b[2];
    if (pipe(a) || pipe(b)) return 0;
    kid = fork();
    if (kid == 0) {
        close(a[1]); close(b[0]);
        _exit(client_main(aurwl_socket(C), app, a[0], b[1]));
    }
    close(a[0]); close(b[1]);
    up_w = a[1]; down_r = b[0];
    if (!hear('M')) return 0;
    aurwl_win *w = win();
    if (!w) return 0;
    aurwl_set_focus(C, w);
    aurwl_dispatch(C);
    return 1;
}

static int finish(void)
{
    cmd('Q');
    close(up_w); close(down_r);
    int st = 0;
    waitpid(kid, &st, 0);
    for (int i = 0; i < 20; i++) aurwl_dispatch(C);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static int has_text_focus(void)
{
    if (write(up_w, "e", 1) != 1) return 0;
    long long end = mono_ms() + 3000;
    while (mono_ms() < end) {
        aurwl_dispatch(C);
        struct pollfd p = { down_r, POLLIN, 0 };
        char ch;
        if (poll(&p, 1, 5) > 0 && read(down_r, &ch, 1) == 1) return ch == 'y';
    }
    return 0;
}

static void test_the_fields(const char *app, int browser)
{
    cur = app;
    CHECK(start(app));
    CHECK(has_text_focus());                               /* the client was told: enter */
    /* A program that has not announced a field: ordinary programs are trusted, browsers are not */
    CHECK(textinput_state() == TI_UNKNOWN);
    CHECK((potato_heard() == 1) == !browser);
    /* a password */
    CHECK(cmd('P'));
    CHECK(textinput_state() == TI_SENSITIVE);
    CHECK(potato_heard() == 0);
    CHECK(wishhook_buffered() == 0);
    /* an ordinary field in the same window */
    CHECK(cmd('N'));
    CHECK(textinput_state() == TI_PLAIN);
    CHECK(potato_heard() == 1);
    CHECK(cmd('E'));                                         /* an e-mail address field: ordinary */
    CHECK(textinput_state() == TI_PLAIN);
    CHECK(potato_heard() == 1);
    /* every way a field can say "secret" */
    static const char SECRET[] = { 'P', 'W', 'I', 'T', 'X', 'S', 'H', 0 };
    for (int i = 0; SECRET[i]; i++) {
        CHECK(cmd(SECRET[i]));
        CHECK(textinput_state() == TI_SENSITIVE);
        CHECK(potato_heard() == 0);
        CHECK(cmd('N'));
        CHECK(potato_heard() == 1);                          /* and back to ordinary */
    }
    /* secret, then the program disables text input (the field lost focus) */
    CHECK(cmd('P'));
    CHECK(cmd('U'));
    CHECK(textinput_state() == TI_UNKNOWN);
    CHECK((potato_heard() == 1) == !browser);
    /* half a word typed in a password does not complete in an ordinary field */
    CHECK(cmd('P'));
    aurwl_key(C, 25, 1, 0); aurwl_key(C, 25, 0, 0);          /* p */
    aurwl_key(C, 24, 1, 0); aurwl_key(C, 24, 0, 0);          /* o */
    CHECK(cmd('N'));
    static const int REST[] = { 20, 30, 20, 24, 57 };         /* "tato " */
    for (unsigned i = 0; i < 5; i++) { aurwl_key(C, (uint32_t)REST[i], 1, 0); aurwl_key(C, (uint32_t)REST[i], 0, 0); }
    CHECK(drain() == 0);
    /* focus away: the keyboard goes to the shell, so does text input */
    CHECK(cmd('P'));
    aurwl_set_focus(C, NULL);
    aurwl_dispatch(C);
    CHECK(textinput_state() == TI_UNKNOWN);
    aurwl_set_focus(C, win());                               /* back: the old state is gone, not remembered */
    aurwl_dispatch(C);
    CHECK(textinput_state() == TI_UNKNOWN);
    CHECK(has_text_focus());
    CHECK(finish());
}

static void test_misbehaviour(void)
{
    cur = __func__;
    CHECK(start("wishtest"));
    STAGE("commit nothing");
    CHECK(cmd('c'));                                         /* commit with nothing enabled */
    CHECK(textinput_state() == TI_UNKNOWN);
    STAGE("huge");
    CHECK(cmd('s'));                                         /* huge surrounding text, nonsense numbers */
    CHECK(textinput_state() == TI_SENSITIVE);                /* purpose 0xFFFFFFFF is not one we know */
    CHECK(potato_heard() == 0);
    STAGE("destroyed");
    CHECK(cmd('d'));                                         /* destroyed while enabled */
    CHECK(textinput_state() == TI_UNKNOWN);
    STAGE("two at once");
    CHECK(cmd('b'));                                         /* two enabled at once: the second is ignored */
    CHECK(textinput_state() == TI_PLAIN);
    CHECK(potato_heard() == 1);
    CHECK(cmd('N'));
    CHECK(finish());
    STAGE("finish 1");
    /* more text inputs than it will give: the client is disconnected, the compositor is not */
    CHECK(start("wishtest"));
    STAGE("too many");
    CHECK(!cmd('m'));                                        /* the protocol error kills that client */
    int st;
    waitpid(kid, &st, 0);
    close(up_w); close(down_r);
    for (int i = 0; i < 20; i++) aurwl_dispatch(C);
    CHECK(textinput_state() == TI_UNKNOWN);
    STAGE("next client");
    /* ... and the next client is served normally */
    CHECK(start("wishtest"));
    CHECK(cmd('P'));
    CHECK(textinput_state() == TI_SENSITIVE);
    CHECK(finish());
    STAGE("window goes");
    /* the window goes away while its field is secret */
    CHECK(start("wishtest"));
    CHECK(cmd('P'));
    CHECK(textinput_state() == TI_SENSITIVE);
    if (write(up_w, "k", 1) != 1) CHECK(0);
    hear('k');
    for (int i = 0; i < 20; i++) aurwl_dispatch(C);
    CHECK(textinput_state() == TI_UNKNOWN);
    close(up_w); close(down_r);
    waitpid(kid, &st, 0);
    for (int i = 0; i < 20; i++) aurwl_dispatch(C);
    STAGE("killed");
    /* a client killed in the middle */
    CHECK(start("wishtest"));
    CHECK(cmd('P'));
    kill(kid, SIGKILL);
    waitpid(kid, &st, 0);
    for (int i = 0; i < 30; i++) aurwl_dispatch(C);
    CHECK(textinput_state() == TI_UNKNOWN);
    close(up_w); close(down_r);
    /* ... and the focus it left behind does not poison the next program (the
     * killed one's surface is gone without ever having been unmapped) */
    STAGE("after the kill");
    CHECK(start("wishtest"));
    CHECK(has_text_focus());
    CHECK(cmd('P'));
    CHECK(textinput_state() == TI_SENSITIVE);
    CHECK(finish());
    CHECK(textinput_leftovers() == 0);
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    char dir[] = "/tmp/wishti-XXXXXX";
    if (!mkdtemp(dir)) return 2;
    setenv("XDG_RUNTIME_DIR", dir, 1);
    C = aurwl_create(800, 600, 60000);
    if (!C) { fprintf(stderr, "no compositor\n"); return 2; }
    (void)argc; (void)argv;
    const char *w[] = { "potato" };
    wishhook_set(w, 1, NULL, 0);
    STAGE("fields: ordinary program");
    test_the_fields("wishtest", 0);
    wishhook_set(w, 1, NULL, 0);
    STAGE("fields: browser");
    test_the_fields("firefox", 1);
    wishhook_set(w, 1, NULL, 0);
    test_misbehaviour();
    aurwl_destroy(C);
    char cmdline[64];
    snprintf(cmdline, sizeof cmdline, "rm -rf %s", dir);
    if (system(cmdline)) {}
    printf("wishtest-textinput: %d checks, %d failed\n", passes + fails, fails);
    return fails ? 1 : 0;
}
