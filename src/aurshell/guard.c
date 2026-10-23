/* guard.c — see guard.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dbus/dbus.h>

#include "guard.h"
#include "foot.h"
#include "power.h"
#include "draw.h"

/* evdev codes, so this file needs no kernel header. */
#define K_ESC    1
#define K_TAB   15
#define K_RET   28
#define K_LCTRL 29
#define K_LALT  56
#define K_SPACE 57
#define K_KPENT 96
#define K_RCTRL 97
#define K_RALT 100
#define K_UP   103
#define K_LEFT 105
#define K_RIGHT 106
#define K_DOWN 108
#define K_DEL  111

#define ESC_PRESSES  3
#define ESC_WINDOW_MS 2000
/* The Escape that opened the card is often followed by a fourth from a
 * hand still pressing it. That one must not close the card again. */
#define ESC_GRACE_MS 1000

static struct {
    int      mode;
    char     name[33];          /* the program polkit stopped */
    int      sel;               /* keyboard choice, -1 none   */
    int      hover;             /* button id under the pointer, 0 none */
    int64_t  esc[ESC_PRESSES];  /* when the last few Escapes went down */
    int64_t  last_key_ms, opened_ms;
    int      ctrl, alt;
    unsigned char held[32];     /* keys we took down, so we take them up */
    int      was_open;
    int      muted_by_us;
    DBusConnection *bus;
    int64_t  bus_tried_ms;
} G = { .sel = -1 };

static int64_t mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int guard_mode(void) { return G.mode; }

static void open_as(shell_ctx *c, int mode)
{
    G.mode = mode;
    G.sel = -1;
    G.hover = 0;
    G.opened_ms = G.last_key_ms;
    if (!c->guard_open) foot_open_only(c, &c->guard_open);
}

/* ── the keys no page can have ───────────────────────────────────── */

int guard_watch_key(shell_ctx *c, int code, int value, int64_t ms)
{
    if (code < 0 || code >= 256) return 0;
    int bit = 1 << (code & 7);
    if (value == 0 && (G.held[code >> 3] & bit)) {   /* our key coming up */
        G.held[code >> 3] &= (unsigned char)~bit;
        return 1;
    }
    if (code == K_LCTRL || code == K_RCTRL) G.ctrl = value != 0;
    if (code == K_LALT  || code == K_RALT)  G.alt  = value != 0;
    if (value != 1 || c->kiosk) return 0;
    G.last_key_ms = ms;

    int open = 0;
    if (code == K_DEL && G.ctrl && G.alt) {
        open = 1;
    } else if (code == K_ESC && !SHELL_PANEL_OPEN(c) &&
               c->focus >= 0 && c->focus < c->n_wins) {
        /* Only while she is looking at a program: that is the only
         * thing that can be holding on to her. */
        memmove(G.esc, G.esc + 1, sizeof G.esc - sizeof G.esc[0]);
        G.esc[ESC_PRESSES - 1] = ms;
        open = G.esc[0] && ms - G.esc[0] <= ESC_WINDOW_MS;
    }
    if (!open) return 0;
    memset(G.esc, 0, sizeof G.esc);
    G.held[code >> 3] |= (unsigned char)bit;
    if (!c->guard_open || G.mode != GUARD_OUT) open_as(c, GUARD_OUT);
    return 1;
}

/* ── remote-control programs ─────────────────────────────────────── */

static int plain_name(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n < 2 || n > 32) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9'))) return 0;
    return 1;
}

void guard_remote(shell_ctx *c, const char *name)
{
    if (!plain_name(name) || c->kiosk) return;
    snprintf(G.name, sizeof G.name, "%s", name);
    open_as(c, GUARD_REMOTE);
}

/* Her answer, where the polkit helper can read it: a file of hers,
 * named for the program, freshly touched. */
static int write_consent(void)
{
    if (!plain_name(G.name)) return -1;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", GUARD_CONSENT_DIR, G.name);
    int fd = open(p, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "aurshell: could not note her answer in %s: %s\n",
                p, strerror(errno));
        return -1;
    }
    int rc = futimens(fd, NULL);
    close(fd);
    return rc;
}

int guard_pump(shell_ctx *c)
{
    if (!G.bus) {
        int64_t now = mono_ms();
        if (G.bus_tried_ms && now - G.bus_tried_ms < 10000) return 0;
        G.bus_tried_ms = now;
        DBusError e; dbus_error_init(&e);
        G.bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &e);
        if (!G.bus) { dbus_error_free(&e); return 0; }
        dbus_connection_set_exit_on_disconnect(G.bus, FALSE);
        dbus_bus_add_match(G.bus, "type='signal',interface='org.auros.Guard',"
                                  "member='RemoteAccess'", &e);
        if (dbus_error_is_set(&e)) {
            fprintf(stderr, "aurshell: cannot listen for installs: %s\n", e.message);
            dbus_error_free(&e);
        }
    }
    int changed = 0;
    dbus_connection_read_write(G.bus, 0);
    DBusMessage *m;
    while ((m = dbus_connection_pop_message(G.bus))) {
        const char *name = NULL;
        /* Anyone on this computer could send this signal. The worst it
         * can do is show her this card: her answer is written only by
         * her press, only for the program named, and anything able to
         * send it as her could already install that program for her
         * alone without asking anybody. */
        if (dbus_message_is_signal(m, "org.auros.Guard", "RemoteAccess") &&
            dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_INVALID)) {
            fprintf(stderr, "aurshell: install of '%s' stopped until she answers\n",
                    name);
            guard_remote(c, name);
            changed = 1;
        }
        dbus_message_unref(m);
    }
    if (!dbus_connection_get_is_connected(G.bus)) {
        dbus_connection_close(G.bus);
        dbus_connection_unref(G.bus);
        G.bus = NULL;
    }
    return changed;
}

int guard_step(shell_ctx *c)
{
    int changed = 0;
    if (c->guard_open && !G.was_open && G.mode == GUARD_NONE) {
        G.mode = GUARD_OUT;                       /* opened from Help */
        G.sel = -1;
        G.opened_ms = G.last_key_ms;
    }
    if (c->guard_open && G.mode == GUARD_OUT && !G.muted_by_us && !power_muted()) {
        power_mute_set(1);
        G.muted_by_us = 1;
        changed = 1;
    }
    if (!c->guard_open && G.was_open) {
        if (G.muted_by_us) power_mute_set(0);
        G.muted_by_us = 0;
        G.mode = GUARD_NONE;
        changed = 1;
    }
    G.was_open = c->guard_open;
    return changed;
}

/* ── what it says ────────────────────────────────────────────────── */

static const char *label_of(int id)
{
    switch (id) {
    case G_CLOSE: return "Close this page";
    case G_BACK:  return "Go back to it";
    case G_OK:    return "OK";
    case G_NO:    return "Don't install it";
    case G_YES:   return "I called someone I know";
    }
    return "";
}

static int words(const shell_ctx *c, const char **head, const char **sub,
                 const char *lines[3])
{
    int n = 0;
    switch (G.mode) {
    case GUARD_REMOTE:
        *head = "Before you install this";
        *sub  = "This program lets someone far away use your computer.";
        lines[n++] = "Scammers ask people to install this.";
        lines[n++] = "Only continue if YOU called someone you know.";
        break;
    case GUARD_REMOTE_OK:
        *head = "All right";
        *sub  = "Now try installing it again.";
        lines[n++] = "If anyone asks you for money, gift cards or your bank "
                     "details, hang up.";
        break;
    default:
        *head = "It's all right";
        *sub  = "The sound is off while you read this.";
        lines[n++] = "A page that says your computer is in danger, or tells "
                     "you to call a number, is a trick.";
        lines[n++] = "Don't call the number, and don't let anyone into your "
                     "computer.";
        break;
    }
    (void)c;
    return n;
}

static int actions(const shell_ctx *c, int *which)
{
    int n = 0;
    switch (G.mode) {
    case GUARD_REMOTE:    which[n++] = G_NO; which[n++] = G_YES; break;
    case GUARD_REMOTE_OK: which[n++] = G_OK; break;
    default:
        if (c->focus >= 0 && c->focus < c->n_wins) {
            which[n++] = G_CLOSE; which[n++] = G_BACK;
        } else {
            which[n++] = G_OK;
        }
    }
    return n;
}

/* ── where things are ────────────────────────────────────────────── */

static float scale(const shell_ctx *c) { return c->text_scale > 0.1f ? c->text_scale : 1.f; }

int guard_buttons(const shell_ctx *c, int sw, int sh, rect *out, int *which)
{
    float k = scale(c);
    int n = actions(c, which);
    int gx = sw / 14; if (gx < 20) gx = 20; if (gx > 110) gx = 110;
    int w = sw - 2 * gx; if (w > (int)(560.f * k)) w = (int)(560.f * k);
    int h = (int)(34.f + 16.f * k); if (h < 44) h = 44;
    int gap = (int)(10.f * k); if (gap < 8) gap = 8;
    int y = sh - (int)(16.f * k) - n * h - (n - 1) * gap;
    int floor_y = (int)(130.f * k);        /* below the headline, always */
    if (y < floor_y) y = floor_y;
    for (int i = 0; i < n; i++) out[i] = (rect){ gx, y + i * (h + gap), w, h };
    return n;
}

void guard_paint(shell_ctx *c, surface *s, shell_fonts *f)
{
    if (!c->guard_open) return;
    int sw = s->w, sh = c->screen_h;
    if (sh <= 0 || sh > s->h) sh = s->h;
    float k = scale(c);
    int gx = sw / 14; if (gx < 20) gx = 20; if (gx > 110) gx = 110;
    int cw = sw - 2 * gx;

    draw_rect(s, (rect){ 0, 0, sw, sh }, c->bg, 0.98f);

    const char *head = "", *sub = "", *lines[3];
    int nl = words(c, &head, &sub, lines);
    font *hf = f->huge ? f->huge : (f->big ? f->big : f->mid);
    font *sf = f->mid ? f->mid : f->small;
    font *bf = f->small ? f->small : sf;

    rect b[GUARD_MAX_BUTTONS]; int id[GUARD_MAX_BUTTONS];
    int nb = guard_buttons(c, sw, sh, b, id);
    int stop = nb ? b[0].y - (int)(8.f * k) : sh;

    int y = (int)(52.f * k);
    if (hf) shell_text(s, hf, (float)gx, (float)y, head, c->fg, 1.f);
    y += (int)(14.f * k);
    draw_hrule(s, gx, y, cw, 1, c->fg, 0.28f);
    y += (int)(30.f * k);
    if (sf) shell_text_elided(s, sf, (float)gx, (float)y, (float)cw, sub, c->fg, 0.95f);
    y += (int)(36.f * k);
    for (int i = 0; i < nl && bf && y < stop; i++) {
        shell_text_elided(s, bf, (float)gx, (float)y, (float)cw, lines[i], c->fg, 0.8f);
        y += (int)(28.f * k);
    }

    for (int i = 0; i < nb; i++) {
        int hot = G.hover == id[i] || G.sel == i;
        int primary = (i == 0);              /* the safe choice is first */
        draw_rect(s, b[i], primary ? c->accent : (hot ? c->surface_hi : c->surface_c), 1.f);
        if (!primary || hot) draw_frame(s, b[i], hot ? 3 : 1, c->fg, hot ? 0.9f : 0.5f);
        font *ft = f->mid ? f->mid : f->small;
        if (ft)
            shell_text_centred(s, ft, (float)b[i].x + b[i].w / 2.f,
                               shell_baseline(ft, (float)b[i].y, (float)b[i].h),
                               label_of(id[i]), primary ? c->bg : c->fg, 1.f);
    }
}

/* ── pressing it ─────────────────────────────────────────────────── */

static int act(shell_ctx *c, int id)
{
    switch (id) {
    case G_CLOSE:
        c->want_close_win = 2;               /* stop it now; see main.c */
        c->guard_open = 0;
        break;
    case G_BACK:
    case G_OK:
        c->guard_open = 0;
        break;
    case G_NO:
        c->guard_open = 0;
        snprintf(c->say, sizeof c->say, "%s", "Nothing was installed.");
        break;
    case G_YES:
        if (write_consent() == 0) { G.mode = GUARD_REMOTE_OK; G.sel = -1; G.hover = 0; }
        else snprintf(c->say, sizeof c->say, "%s", "That did not work. Please try again.");
        break;
    }
    return 1;
}

static int hit(rect r, int x, int y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

int guard_click(shell_ctx *c, int x, int y)
{
    if (!c->guard_open) return 0;
    rect b[GUARD_MAX_BUTTONS]; int id[GUARD_MAX_BUTTONS];
    int n = guard_buttons(c, c->screen_w, c->screen_h, b, id);
    for (int i = 0; i < n; i++) if (hit(b[i], x, y)) return act(c, id[i]);
    return 1;                 /* a press anywhere else is swallowed */
}

void guard_motion(shell_ctx *c, int x, int y)
{
    G.hover = 0;
    if (!c->guard_open) return;
    rect b[GUARD_MAX_BUTTONS]; int id[GUARD_MAX_BUTTONS];
    int n = guard_buttons(c, c->screen_w, c->screen_h, b, id);
    for (int i = 0; i < n; i++) if (hit(b[i], x, y)) { G.hover = id[i]; return; }
}

int guard_key(shell_ctx *c, int k)
{
    if (!c->guard_open) return 0;
    rect b[GUARD_MAX_BUTTONS]; int id[GUARD_MAX_BUTTONS];
    int n = guard_buttons(c, c->screen_w, c->screen_h, b, id);
    switch (k) {
    case K_ESC:
        if (G.last_key_ms - G.opened_ms < ESC_GRACE_MS) return 1;
        /* Escape is never a yes: on the install card it is the safe
         * answer, everywhere else it is "go back". */
        return act(c, G.mode == GUARD_REMOTE ? G_NO : (n > 1 ? G_BACK : G_OK));
    case K_DOWN: case K_RIGHT: case K_TAB:
        G.sel = (G.sel < 0) ? 0 : (G.sel + 1) % n; return 1;
    case K_UP: case K_LEFT:
        G.sel = (G.sel <= 0) ? n - 1 : G.sel - 1; return 1;
    case K_RET: case K_KPENT: case K_SPACE:
        /* Nothing chosen yet, nothing happens: a blind Return must not
         * close her browser, still less say yes to a stranger. */
        if (G.sel >= 0 && G.sel < n) act(c, id[G.sel]);
        return 1;
    }
    return 1;                 /* modal: every key is ours */
}
