/* xwm.inc — Xwayland and the small X window manager that makes it usable.
 *
 * #included into aurwl.c (see the note there: no build list has to learn a
 * new file). Everything here is `static` and reaches aurwl's own types.
 *
 * WHY THIS EXISTS. Plenty of programs a person will install from "Get more
 * programs" speak X11 and nothing else: VLC, Zoom, Spotify, every Electron
 * wrapper. Without an X server they open no window at all, and "no suitable
 * interface module" is all she is told. Xwayland is the X server that draws
 * into Wayland surfaces; this file starts it, and does the one job it needs
 * a desktop for -- be its window manager -- so each X window becomes one
 * window on the shell like any other.
 *
 * HOW IT FITS.
 *   - Xwayland is started when the first X program connects, not before
 *     (it costs memory this product is short of), and goes away when the last
 *     one has. We hold the listening socket in between.
 *   - It is an ordinary Wayland client of ours (we make its connection
 *     ourselves, so we know which one it is). Each X window it shows is a
 *     wl_surface; the X window manager tells us which window a surface is
 *     (the WL_SURFACE_ID message) and the surface becomes an aurwl_win with
 *     role X11. From there the shell treats it as it treats any window.
 *   - We are the window manager: the X server sends us MapRequest,
 *     ConfigureRequest, property changes and messages, we answer on the
 *     connection Xwayland gave us (-wm). There are NO frames and NO
 *     reparenting: the shell draws the title bar. Each window is kept at
 *     (0,0) of the X screen, so a menu that positions itself in X screen
 *     coordinates lands where the window is.
 *   - Menus, tooltips and drop-downs are override-redirect windows that
 *     position themselves; they are shown as popups of the window in use.
 *
 * THE X11 WIRE FORMAT IS READ BY HAND, not through libxcb: no library to
 * link into every program that links aurwl, no headers to install in the
 * image build, and every byte that arrives is read by code in this file
 * that can be fuzzed (tools/fuzz/fuzz_xwm.c) -- the X clients are not
 * trusted, and what they write into a property is parsed in the process
 * that owns the keyboard. Little-endian only (it is a local server).
 *
 * The clipboard between X and Wayland programs, text and pictures, is xclip.inc
 * (included at the end of this file).
 *
 * WHAT IS NOT HERE (yet): PRIMARY (middle-click paste), drag and drop across
 * the two kinds of program, XRandR, icons, the system tray. */

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "xwm.inc speaks X11 in little-endian only"
#endif

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/stat.h>

#define XW_MAX_WINS    256          /* X windows tracked per server           */
#define XC_MAX         (16u << 20)  /* the most clipboard data either way (a screenshot) */
#define XW_IN_MAX      (XC_MAX + (1u << 20))   /* the most we hold of what it sent       */
#define XW_OUT_MAX     (XC_MAX + (1u << 20))   /* ...and of what we have yet to send     */
#define XW_REPLY_MAX   (XC_MAX + 4096)         /* one reply larger than this is a lie    */
#define XW_PEND_MAX    512          /* requests awaiting a reply              */
#define XW_PROP_WORDS  4096         /* property words we ask for (16 KiB)     */
#define XW_SIZE_MAX    16384

/* ── atoms ───────────────────────────────────────────────────────── */
enum {
    XA_WM_PROTOCOLS, XA_WM_DELETE_WINDOW, XA_WM_TAKE_FOCUS, XA_WM_STATE, XA_WM_CHANGE_STATE,
    XA_WL_SURFACE_ID, XA_NET_SUPPORTED, XA_NET_SUPPORTING_WM_CHECK, XA_NET_WM_NAME, XA_UTF8_STRING,
    XA_NET_ACTIVE_WINDOW, XA_NET_CLIENT_LIST, XA_NET_CLOSE_WINDOW,
    XA_NET_WM_WINDOW_TYPE, XA_TYPE_NORMAL, XA_TYPE_DIALOG, XA_TYPE_UTILITY, XA_TYPE_SPLASH,
    XA_TYPE_MENU, XA_TYPE_DROPDOWN, XA_TYPE_POPUP, XA_TYPE_TOOLTIP, XA_TYPE_NOTIFICATION,
    XA_TYPE_COMBO, XA_TYPE_DND,
    XA_NET_WM_STATE, XA_STATE_FULLSCREEN, XA_STATE_MAX_VERT, XA_STATE_MAX_HORZ, XA_STATE_FOCUSED,
    XA_WM_S0, XA_MOTIF_WM_HINTS,
    XA_CLIPBOARD, XA_TARGETS, XA_TEXT, XA_AUR_SEL, XA_AUR_TGT, XA_INCR,
    XA_COUNT
};
static const char *const XA_NAMES[XA_COUNT] = {
    "WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS", "WM_STATE", "WM_CHANGE_STATE",
    "WL_SURFACE_ID", "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME", "UTF8_STRING",
    "_NET_ACTIVE_WINDOW", "_NET_CLIENT_LIST", "_NET_CLOSE_WINDOW",
    "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_DIALOG",
    "_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_MENU",
    "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
    "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_DND",
    "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_MAXIMIZED_VERT",
    "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_FOCUSED",
    "WM_S0", "_MOTIF_WM_HINTS",
    "CLIPBOARD", "TARGETS", "TEXT", "_AUR_SELECTION", "_AUR_TARGETS", "INCR",
};
/* the atoms X predefines (X11 protocol appendix) */
enum { XP_PRIMARY = 1, XP_ATOM = 4, XP_CARDINAL = 6, XP_STRING = 31, XP_WINDOW = 33,
       XP_WM_HINTS = 35, XP_WM_NAME = 39, XP_WM_NORMAL_HINTS = 40, XP_WM_CLASS = 67,
       XP_WM_TRANSIENT_FOR = 68 };

/* event masks */
enum { XM_STRUCTURE = 0x20000, XM_SUBSTRUCTURE_NOTIFY = 0x80000, XM_SUBSTRUCTURE_REDIRECT = 0x100000,
       XM_PROPERTY = 0x400000 };
/* ConfigureWindow / ConfigureRequest value mask */
enum { XC_X = 1, XC_Y = 2, XC_W = 4, XC_H = 8, XC_BORDER = 16, XC_SIBLING = 32, XC_STACK = 64 };

/* ── the objects ─────────────────────────────────────────────────── */
typedef struct xwm xwm;

typedef struct xwin {
    xwm            *m;
    uint32_t        id;
    int             x, y, w, h;           /* the X server's idea, from its notifies */
    int             cfg_w, cfg_h;         /* the last size we asked for              */
    int             or_;                  /* override-redirect: positions itself     */
    int             mapped;               /* X says it is mapped                     */
    int             managed;              /* we handled its MapRequest (or adopted)  */
    int             ready;                /* properties read, window mapped by us    */
    int             props_left;           /* GetProperty replies outstanding         */
    int             withdrawn;
    char            title[128], wm_title[128], app_id[128];
    uint32_t        transient_for;
    uint32_t        type;                 /* the first _NET_WM_WINDOW_TYPE           */
    int             wm_delete, wm_take_focus, input_hint;
    int             hint_min_w, hint_min_h, hint_max_w, hint_max_h;
    int             hint_inc_w, hint_inc_h, hint_base_w, hint_base_h;
    int             fullscreen;           /* _NET_WM_STATE_FULLSCREEN is set         */
    int             csd;                  /* _MOTIF_WM_HINTS: it draws its own title bar */
    int             activated;            /* the shell last said it is the window in use */
    uint32_t        surf_id;              /* WL_SURFACE_ID, 0 until sent             */
    aurwl_win      *win;                  /* the surface, once associated            */
    struct wl_list  link;
} xwin;

typedef struct { uint16_t seq; uint8_t kind; uint32_t a, b; } xpend;
enum { XK_ATOM = 1, XK_PROP, XK_TREE, XK_ATTR, XK_EXT, XK_BIGREQ, XK_XFVER, XK_CLIPATOM, XK_CLIPTGT, XK_ATOMNAME, XK_CLIPDATA };

struct xwm {
    aurwl              *c;
    int                 fd;
    struct wl_event_source *src;
    struct wl_client   *xclient;          /* Xwayland's Wayland connection           */
    uint8_t            *in;   size_t in_n,  in_cap;
    uint8_t            *out;  size_t out_n, out_cap;
    int                 dead;
    int                 state;            /* 0 wait for setup, 1 atoms, 2 running    */
    uint32_t            id_base, id_mask, id_next;
    uint32_t            root, root_w, root_h;
    uint32_t            seq;              /* last request number sent                */
    xpend               pend[XW_PEND_MAX];
    int                 pend_head, pend_n;
    uint32_t            atom[XA_COUNT];
    int                 atoms_left;
    uint32_t            check;            /* the _NET_SUPPORTING_WM_CHECK window     */
    struct wl_list      wins;
    int                 n_wins;
    xwin               *active;
    /* the clipboard (xclip.inc) */
    uint32_t            maxreq;           /* the most one request may carry, in 4-byte units   */
    int                 bigreq;           /* BIG-REQUESTS is on: a request may exceed 256 KiB  */
    uint32_t            xfixes_major, xfixes_event;
    int                 clip_wl;          /* we own CLIPBOARD for a Wayland program            */
    uint32_t            clip_gen;         /* which Wayland selection the interned names are for */
    int                 clip_atoms_left;
    struct { uint32_t atom; char name[64]; } names[48];   /* mime name <-> X atom, both ways    */
    int                 n_names, names_next;
    uint32_t            clip_x;           /* the X window that owns CLIPBOARD, 0 none          */
    uint32_t            xq_gen;           /* which owner change the target list is for         */
    int                 xq_left;          /* names still to learn                              */
    struct wl_list      xfers;            /* Wayland -> X: reads from a program's pipe         */
    struct wl_list      pastes;           /* X -> Wayland: pastes in progress or waiting       */
    uint32_t            next_job;
};

/* ── bytes ───────────────────────────────────────────────────────── */
static inline uint16_t xr16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline uint32_t xr32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void     xw16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static inline void     xw32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static inline size_t   xpad4(size_t n) { return (n + 3) & ~(size_t)3; }

static void xwm_kill(xwm *m);
/* the clipboard, below (xclip.inc) */
static void xc_reply(xwm *m, const xpend *e, const uint8_t *r, size_t total);
static void xc_failed(xwm *m, const xpend *e);
static int  xc_event(xwm *m, const uint8_t *e);
static void xc_start(xwm *m);
static void xc_cleanup(xwm *m);

/* AUROS_XWM_DEBUG=1, or a file /tmp/auros-xwm-debug (the shell cleans its environment): narrate (stderr). */
#define XDBG(...) do { if (getenv("AUROS_XWM_DEBUG") || access("/tmp/auros-xwm-debug", F_OK) == 0) { fprintf(stderr, "aurwl xwm: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int xbuf_grow(uint8_t **buf, size_t *cap, size_t need, size_t limit)
{
    if (need > limit) return -1;
    if (need <= *cap) return 0;
    size_t nc = *cap ? *cap : 4096;
    while (nc < need) nc *= 2;
    if (nc > limit) nc = limit;
    uint8_t *nb = realloc(*buf, nc);
    if (!nb) return -1;
    *buf = nb; *cap = nc;
    return 0;
}

/* Start a request: header written, `body` bytes (padded) zeroed, a pointer
 * to the body returned. NULL if the connection is already dead or the queue
 * would pass its limit (which kills it: a server that does not read is
 * gone). */
static uint8_t *xreq(xwm *m, uint8_t op, uint8_t d1, size_t body)
{
    if (m->dead) return NULL;
    size_t hdr = 4, total = 4 + xpad4(body);
    if (total / 4 > 0xFFFF) {               /* BIG-REQUESTS: a zero length, then the real one, counting that extra word */
        hdr = 8; total += 4;
        if (!m->bigreq || total / 4 > m->maxreq) { xwm_kill(m); return NULL; }
    }
    if (xbuf_grow(&m->out, &m->out_cap, m->out_n + total, XW_OUT_MAX) < 0) { xwm_kill(m); return NULL; }
    uint8_t *p = m->out + m->out_n;
    memset(p, 0, total);
    p[0] = op; p[1] = d1;
    if (hdr == 4) xw16(p + 2, (uint16_t)(total / 4)); else xw32(p + 4, (uint32_t)(total / 4));
    m->out_n += total;
    m->seq++;
    return p + hdr;
}

static void xexpect(xwm *m, int kind, uint32_t a, uint32_t b)
{
    if (m->pend_n >= XW_PEND_MAX) { xwm_kill(m); return; }
    xpend *e = &m->pend[(m->pend_head + m->pend_n) % XW_PEND_MAX];
    e->seq = (uint16_t)m->seq; e->kind = (uint8_t)kind; e->a = a; e->b = b;
    m->pend_n++;
}

static uint32_t xnewid(xwm *m)
{
    uint32_t shift = m->id_mask ? (uint32_t)__builtin_ctz(m->id_mask) : 0;
    uint32_t id = m->id_base | ((m->id_next++ << shift) & m->id_mask);
    return id;
}

static void xflush(xwm *m)
{
    while (!m->dead && m->out_n) {
        ssize_t n = write(m->fd, m->out, m->out_n);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) break;
            xwm_kill(m);
            return;
        }
        memmove(m->out, m->out + n, m->out_n - (size_t)n);
        m->out_n -= (size_t)n;
    }
    if (m->src && !m->dead)
        wl_event_source_fd_update(m->src, m->out_n ? (WL_EVENT_READABLE | WL_EVENT_WRITABLE) : WL_EVENT_READABLE);
    if (!m->out_n && m->out_cap > (1u << 20)) { free(m->out); m->out = NULL; m->out_cap = 0; }
}

/* ── requests ────────────────────────────────────────────────────── */
/* Each builds one request in the output queue. Offsets are into the body
 * (after the 4-byte header), as the X11 protocol lays them out. */
static void x_select_events(xwm *m, uint32_t win, uint32_t mask)      /* ChangeWindowAttributes: event-mask */
{
    uint8_t *p = xreq(m, 2, 0, 12);
    if (!p) return;
    xw32(p, win); xw32(p + 4, 0x800); xw32(p + 8, mask);
}
static void x_map(xwm *m, uint32_t win)        { uint8_t *p = xreq(m, 8, 0, 4);   if (p) xw32(p, win); }
static void x_kill_client(xwm *m, uint32_t win){ uint8_t *p = xreq(m, 113, 0, 4); if (p) xw32(p, win); }
static void x_query_tree(xwm *m, uint32_t win)
{
    uint8_t *p = xreq(m, 15, 0, 4);
    if (!p) return;
    xw32(p, win);
    xexpect(m, XK_TREE, win, 0);
}
static void x_get_attrs(xwm *m, uint32_t win)
{
    uint8_t *p = xreq(m, 3, 0, 4);
    if (!p) return;
    xw32(p, win);
    xexpect(m, XK_ATTR, win, 0);
}
static void x_get_prop(xwm *m, uint32_t win, uint32_t prop)
{
    uint8_t *p = xreq(m, 20, 0, 20);
    if (!p) return;
    xw32(p, win); xw32(p + 4, prop); xw32(p + 8, 0 /* AnyPropertyType */);
    xw32(p + 12, 0); xw32(p + 16, XW_PROP_WORDS);
    xexpect(m, XK_PROP, win, prop);
}
static void x_intern(xwm *m, int index)
{
    const char *name = XA_NAMES[index];
    size_t n = strlen(name);
    uint8_t *p = xreq(m, 16, 0, 4 + n);
    if (!p) return;
    xw16(p, (uint16_t)n);
    memcpy(p + 4, name, n);
    xexpect(m, XK_ATOM, (uint32_t)index, 0);
}
/* ConfigureWindow with only the fields in `mask`. */
static void x_configure(xwm *m, uint32_t win, unsigned mask, int32_t x, int32_t y, int32_t w, int32_t h)
{
    unsigned k = (unsigned)__builtin_popcount(mask & (XC_X | XC_Y | XC_W | XC_H));
    uint8_t *p = xreq(m, 12, 0, 8 + 4 * k);
    if (!p) return;
    xw32(p, win); xw16(p + 4, (uint16_t)(mask & (XC_X | XC_Y | XC_W | XC_H)));
    unsigned i = 0;
    if (mask & XC_X) xw32(p + 8 + 4 * i++, (uint32_t)x);
    if (mask & XC_Y) xw32(p + 8 + 4 * i++, (uint32_t)y);
    if (mask & XC_W) xw32(p + 8 + 4 * i++, (uint32_t)w);
    if (mask & XC_H) xw32(p + 8 + 4 * i++, (uint32_t)h);
}
static int x_change_prop(xwm *m, uint32_t win, uint32_t prop, uint32_t type, int format,
                         const void *data, size_t n_units)
{
    size_t bytes = n_units * (size_t)(format / 8);
    if (bytes > XC_MAX || (28 + xpad4(bytes)) / 4 + 1 > (m->maxreq ? m->maxreq : 65535)) return 0;
    uint8_t *p = xreq(m, 18, 0 /* Replace */, 20 + bytes);
    if (!p) return 0;
    xw32(p, win); xw32(p + 4, prop); xw32(p + 8, type);
    p[12] = (uint8_t)format;
    xw32(p + 16, (uint32_t)n_units);
    if (bytes) memcpy(p + 20, data, bytes);
    return 1;
}
/* SendEvent of a 32-byte event to `win`. */
static void x_send_event(xwm *m, uint32_t win, uint32_t mask, const uint8_t ev[32])
{
    uint8_t *p = xreq(m, 25, 0, 40);
    if (!p) return;
    xw32(p, win); xw32(p + 4, mask);
    memcpy(p + 8, ev, 32);
}
static void x_client_message(xwm *m, uint32_t win, uint32_t type, uint32_t d0, uint32_t d1)
{
    uint8_t ev[32] = {0};
    ev[0] = 33; ev[1] = 32;
    xw32(ev + 4, win); xw32(ev + 8, type);
    xw32(ev + 12, d0); xw32(ev + 16, d1);
    x_send_event(m, win, 0, ev);
}
/* QueryExtension: the major opcode requests of that extension carry. */
static void x_query_extension(xwm *m, const char *name, uint32_t tag)
{
    size_t n = strlen(name);
    uint8_t *p = xreq(m, 98, 0, 4 + n);
    if (!p) return;
    xw16(p, (uint16_t)n);
    memcpy(p + 4, name, n);
    xexpect(m, XK_EXT, tag, 0);
}

/* SetSelectionOwner: how a window manager announces itself (ICCCM 2.8). */
static void x_set_selection_owner(xwm *m, uint32_t owner, uint32_t selection)
{
    uint8_t *p = xreq(m, 22, 0, 12);
    if (!p) return;
    xw32(p, owner); xw32(p + 4, selection); xw32(p + 8, 0 /* CurrentTime */);
}
/* To the top of the X stack (ConfigureWindow, stack-mode Above). Every X window sits at the
 * same place, so which one Xwayland hands a click to is decided by stacking alone: the
 * window she is pointing at, or typing into, must be the top one. */
static void x_raise(xwm *m, uint32_t win)
{
    uint8_t *p = xreq(m, 12, 0, 8 + 4);
    if (!p) return;
    xw32(p, win); xw16(p + 4, XC_STACK);
    xw32(p + 8, 0 /* Above */);
}
static void x_set_focus(xwm *m, uint32_t win)
{
    uint8_t *p = xreq(m, 42, 2 /* RevertToParent */, 8);
    if (!p) return;
    xw32(p, win); xw32(p + 4, 0 /* CurrentTime */);
}

/* ── windows ─────────────────────────────────────────────────────── */
static xwin *xwin_find(xwm *m, uint32_t id)
{
    xwin *w;
    wl_list_for_each(w, &m->wins, link) if (w->id == id) return w;
    return NULL;
}

static xwin *xwin_new(xwm *m, uint32_t id)
{
    if (m->n_wins >= XW_MAX_WINS) return NULL;
    xwin *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->m = m; w->id = id; w->input_hint = 1;
    wl_list_insert(&m->wins, &w->link);
    m->n_wins++;
    return w;
}

static void xwin_free(xwin *w)
{
    xwm *m = w->m;
    if (w->win) { w->win->xw = NULL; w->win->c->damage_seq++; }
    if (m->active == w) m->active = NULL;
    wl_list_remove(&w->link);
    m->n_wins--;
    free(w);
}

/* On screen: the shell may list it. */
static int xwin_listed(const xwin *w)
{ return w->win && (w->or_ || w->ready); }
static xwin *xwm_popup_parent(xwm *m);
/* ...and is shown: a window of the program in use, or a menu of it. A menu of any other program
 * is not (it has no parent, and a different X client than the window that has the keyboard). */
static int xwin_shown(const xwin *w)
{
    if (!xwin_listed(w)) return 0;
    if (!w->or_) return 1;
    xwin *p = xwm_popup_parent(w->m);
    return p && ((p->id ^ w->id) & ~w->m->id_mask) == 0;       /* the resource-id base names the client */
}

static void xwin_sync_names(xwin *w)
{
    if (!w->win) return;
    const char *t = w->title[0] ? w->title : w->wm_title;
    int first = w->win->app_id[0] == 0;
    snprintf(w->win->title, sizeof w->win->title, "%s", t);
    snprintf(w->win->app_id, sizeof w->win->app_id, "%s", w->app_id);
    if (first && w->app_id[0] && wishhook_app) wishhook_app(w->win->app_id);
    w->win->c->damage_seq++;
}

/* Text from a client, made safe to show: valid UTF-8 (Latin-1 converted),
 * no control characters, bounded. What is not valid becomes '?'. */
static void x_text(char *dst, size_t n, const uint8_t *src, size_t len, int latin1)
{
    size_t o = 0;
    for (size_t i = 0; i < len && o + 5 < n; ) {
        uint8_t b = src[i];
        if (b == 0) break;
        if (b < 0x20 || b == 0x7F) { dst[o++] = ' '; i++; continue; }
        if (b < 0x80) { dst[o++] = (char)b; i++; continue; }
        if (latin1) {                                   /* U+0080..U+00FF */
            dst[o++] = (char)(0xC0 | (b >> 6)); dst[o++] = (char)(0x80 | (b & 0x3F)); i++;
            continue;
        }
        int need = (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : 0;
        uint32_t cp = need == 1 ? b & 0x1F : need == 2 ? b & 0x0F : b & 0x07;
        int ok = need > 0 && i + (size_t)need < len;
        for (int k = 1; ok && k <= need; k++) {
            if ((src[i + (size_t)k] & 0xC0) != 0x80) ok = 0;
            else cp = (cp << 6) | (src[i + (size_t)k] & 0x3F);
        }
        /* not overlong, not a surrogate, not past U+10FFFF, and not a control */
        if (ok && ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000) ||
                   (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF || (cp >= 0x80 && cp < 0xA0))) ok = 0;
        if (!ok) { dst[o++] = '?'; i++; continue; }
        for (int k = 0; k <= need; k++) dst[o++] = (char)src[i + (size_t)k];
        i += (size_t)need + 1;
    }
    dst[o] = 0;
}

/* One property's value, applied. */
static void xwin_apply_prop(xwin *w, uint32_t atom, uint32_t type, int format,
                            const uint8_t *data, size_t units)
{
    xwm *m = w->m;
    if (atom == XP_WM_NAME && format == 8) {
        x_text(w->wm_title, sizeof w->wm_title, data, units, type == XP_STRING);
    } else if (atom == m->atom[XA_NET_WM_NAME] && format == 8) {
        x_text(w->title, sizeof w->title, data, units, 0);
    } else if (atom == XP_WM_CLASS && format == 8) {
        /* "instance\0Class\0": the second is the one that names the program */
        size_t i = 0;
        while (i < units && data[i]) i++;
        const uint8_t *cls = i + 1 < units ? data + i + 1 : data;
        size_t cl = i + 1 < units ? units - i - 1 : i;
        x_text(w->app_id, sizeof w->app_id, cls, cl, 1);
    } else if (atom == XP_WM_TRANSIENT_FOR && format == 32 && units >= 1) {
        w->transient_for = xr32(data);
    } else if (atom == XP_WM_NORMAL_HINTS && format == 32 && units >= 15) {
        uint32_t fl = xr32(data);
        #define H(i) ((int)(xr32(data + 4 * (i)) > XW_SIZE_MAX ? XW_SIZE_MAX : xr32(data + 4 * (i))))
        w->hint_min_w = w->hint_min_h = w->hint_max_w = w->hint_max_h = 0;
        w->hint_inc_w = w->hint_inc_h = w->hint_base_w = w->hint_base_h = 0;
        if (fl & 16)  { w->hint_min_w = H(5);  w->hint_min_h = H(6); }
        if (fl & 32)  { w->hint_max_w = H(7);  w->hint_max_h = H(8); }
        if (fl & 64)  { w->hint_inc_w = H(9);  w->hint_inc_h = H(10); }
        if (fl & 256) { w->hint_base_w = H(13); w->hint_base_h = H(14); }
        #undef H
    } else if (atom == XP_WM_HINTS && format == 32 && units >= 2) {
        w->input_hint = (xr32(data) & 1) ? xr32(data + 4) != 0 : 1;
    } else if (atom == m->atom[XA_WM_PROTOCOLS] && format == 32) {
        w->wm_delete = w->wm_take_focus = 0;
        for (size_t i = 0; i < units; i++) {
            uint32_t a = xr32(data + 4 * i);
            if (a == m->atom[XA_WM_DELETE_WINDOW]) w->wm_delete = 1;
            if (a == m->atom[XA_WM_TAKE_FOCUS])    w->wm_take_focus = 1;
        }
    } else if (atom == m->atom[XA_NET_WM_WINDOW_TYPE] && format == 32 && units >= 1) {
        w->type = xr32(data);
    } else if (atom == m->atom[XA_MOTIF_WM_HINTS] && format == 32 && units >= 3) {
        /* flags, functions, decorations, ...: bit 1 of flags says `decorations` is meant, and 0 is "none":
         * a frameless Qt or Electron window (Zoom) that draws its own caption. The shell then draws none. */
        int csd = (xr32(data) & 2) && xr32(data + 8) == 0;
        if (csd != w->csd) { w->csd = csd; if (w->win) w->win->c->damage_seq++; }
    }
    xwin_sync_names(w);
}

static void xwm_client_list(xwm *m)
{
    uint32_t ids[XW_MAX_WINS];
    int n = 0;
    xwin *w;
    wl_list_for_each(w, &m->wins, link) if (w->managed && w->ready && n < XW_MAX_WINS) ids[n++] = w->id;
    x_change_prop(m, m->root, m->atom[XA_NET_CLIENT_LIST], XP_WINDOW, 32, ids, (size_t)n);
}

static void xwm_set_wm_state(xwm *m, xwin *w, uint32_t state)
{
    uint32_t v[2] = { state, 0 };
    x_change_prop(m, w->id, m->atom[XA_WM_STATE], m->atom[XA_WM_STATE], 32, v, 2);
}

static void xwm_set_net_state(xwm *m, xwin *w)
{
    uint32_t v[1]; size_t n = 0;
    if (w->fullscreen) v[n++] = m->atom[XA_STATE_FULLSCREEN];
    x_change_prop(m, w->id, m->atom[XA_NET_WM_STATE], XP_ATOM, 32, v, n);
}

/* Properties read: put it on the screen. */
static void xwm_finish_manage(xwin *w)
{
    xwm *m = w->m;
    w->ready = 1;
    x_select_events(m, w->id, XM_PROPERTY);
    x_configure(m, w->id, XC_X | XC_Y, 0, 0, 0, 0);
    w->x = w->y = 0;                        /* where we have just put it */
    xwm_set_wm_state(m, w, 1 /* NormalState */);
    xwm_set_net_state(m, w);
    if (!w->mapped) x_map(m, w->id);
    xwm_client_list(m);
    xwin_sync_names(w);
    XDBG("window 0x%x ready: \"%s\" class \"%s\" type 0x%x transient 0x%x csd %d min %dx%d max %dx%d inc %dx%d",
         w->id, w->title[0] ? w->title : w->wm_title, w->app_id, w->type, w->transient_for, w->csd,
         w->hint_min_w, w->hint_min_h, w->hint_max_w, w->hint_max_h, w->hint_inc_w, w->hint_inc_h);
}

#define XW_NPROPS 9
static const uint32_t XW_PROPS_ATOMS[XW_NPROPS] = { XP_WM_NAME, 0 /* _NET_WM_NAME */, XP_WM_CLASS, XP_WM_TRANSIENT_FOR,
                                            XP_WM_NORMAL_HINTS, XP_WM_HINTS, 0 /* WM_PROTOCOLS */,
                                            0 /* _NET_WM_WINDOW_TYPE */, 0 /* _MOTIF_WM_HINTS */ };

/* A window asked to be shown (MapRequest), or was already mapped when we
 * arrived: read what we need to know about it, then map it. */
static void xwm_manage(xwin *w)
{
    xwm *m = w->m;
    w->withdrawn = 0;
    if (w->ready) { x_map(m, w->id); xwm_set_wm_state(m, w, 1); return; }
    if (w->managed) return;                 /* properties already on their way */
    w->managed = 1;
    w->props_left = XW_NPROPS;
    for (int i = 0; i < XW_NPROPS; i++) {
        uint32_t a = XW_PROPS_ATOMS[i];
        if (i == 1) a = m->atom[XA_NET_WM_NAME];
        if (i == 6) a = m->atom[XA_WM_PROTOCOLS];
        if (i == 7) a = m->atom[XA_NET_WM_WINDOW_TYPE];
        if (i == 8) a = m->atom[XA_MOTIF_WM_HINTS];
        x_get_prop(m, w->id, a);
    }
}

/* The surface Xwayland made for this window: it becomes a window of ours.
 *
 * The two halves of the news travel on different sockets -- the surface on
 * Xwayland's Wayland connection, the window's name for it on the window
 * manager's X connection -- and either may arrive first. If the message
 * comes before the surface exists, the number is kept and the pairing is
 * made when the surface is created (xw_surface_created). */
static void xwm_associate(xwin *w, uint32_t surface_id)
{
    xwm *m = w->m;
    w->surf_id = surface_id;
    struct wl_resource *r = surface_id ? wl_client_get_object(m->xclient, surface_id) : NULL;
    if (!r || !wl_resource_instance_of(r, &wl_surface_interface, &surface_impl)) {
        XDBG("window 0x%x names surface %u, which does not exist yet", w->id, surface_id);
        return;
    }
    XDBG("window 0x%x is surface %u", w->id, surface_id);
    aurwl_win *s = wl_resource_get_user_data(r);
    if (!s || s->dead) return;
    if (s->role != ROLE_NONE && s->role != ROLE_X11) return;     /* a role is for life */
    if (s->xw && s->xw != w) s->xw->win = NULL;
    if (w->win && w->win != s) w->win->xw = NULL;
    s->role = ROLE_X11;
    s->xw = w; w->win = s;
    xwin_sync_names(w);
    /* Told which output it is on, as every surface that is shown is. */
    if (s->mapped) {
        FOR_EACH_RES(o, &s->c->outputs)
            if (wl_resource_get_client(o) == wl_resource_get_client(s->res)) wl_surface_send_enter(s->res, o);
    }
    s->c->damage_seq++;
}

/* ── what the shell asks of an X window ──────────────────────────── */
static int xw_clamp_dim(int v, int lo, int hi, int inc, int base)
{
    if (v < 1) v = 1;
    if (lo > 0 && v < lo) v = lo;
    if (hi > 0 && v > hi) v = hi;
    if (inc > 1) {
        int b = base > 0 ? base : (lo > 0 ? lo : 0);
        if (v > b) v = b + ((v - b) / inc) * inc;
        if (lo > 0 && v < lo) v = lo;
    }
    return v > XW_SIZE_MAX ? XW_SIZE_MAX : v;
}

static void xwm_configure(xwin *w, int width, int height, int activated, int maximized, int fullscreen)
{
    xwm *m = w->m;
    (void)maximized;
    if (w->or_ || !w->ready) return;
    if (activated && !w->activated) { x_raise(m, w->id); xflush(m); }     /* it is the window in use: on top */
    w->activated = activated != 0;
    if (fullscreen != w->fullscreen) { w->fullscreen = fullscreen != 0; xwm_set_net_state(m, w); }
    if (width <= 0 || height <= 0) return;                 /* the shell has no opinion */
    int nw = xw_clamp_dim(width,  w->hint_min_w, w->hint_max_w, w->hint_inc_w, w->hint_base_w);
    int nh = xw_clamp_dim(height, w->hint_min_h, w->hint_max_h, w->hint_inc_h, w->hint_base_h);
    if (nw == w->cfg_w && nh == w->cfg_h) return;          /* the same ask is not made twice */
    w->cfg_w = nw; w->cfg_h = nh;
    x_configure(m, w->id, XC_W | XC_H, 0, 0, nw, nh);
    xflush(m);
}

static void xwm_focus(aurwl *c, xwin *w)
{
    xwm *m = c->x.wm;
    if (!m || m->dead) return;
    if (!w) {
        m->active = NULL;
        x_set_focus(m, 0);
        uint32_t none = 0;
        x_change_prop(m, m->root, m->atom[XA_NET_ACTIVE_WINDOW], XP_WINDOW, 32, &none, 1);
        xflush(m);
        return;
    }
    if (w->or_) return;                                      /* a menu is never given the keyboard */
    m->active = w;
    x_raise(m, w->id);
    XDBG("focus: SetInputFocus 0x%x (take_focus %d, input hint %d)", w->id, w->wm_take_focus, w->input_hint);
    x_set_focus(m, w->id);
    if (w->wm_take_focus) x_client_message(m, w->id, m->atom[XA_WM_PROTOCOLS], m->atom[XA_WM_TAKE_FOCUS], 0);
    x_change_prop(m, m->root, m->atom[XA_NET_ACTIVE_WINDOW], XP_WINDOW, 32, &w->id, 1);
    xflush(m);
}

static void xwm_close(xwin *w)
{
    xwm *m = w->m;
    if (w->wm_delete) x_client_message(m, w->id, m->atom[XA_WM_PROTOCOLS], m->atom[XA_WM_DELETE_WINDOW], 0);
    else              x_kill_client(m, w->id);
    xflush(m);
}
static void xwm_kill_window(xwin *w) { x_kill_client(w->m, w->id); xflush(w->m); }

/* The window a menu belongs to: the X window that has the keyboard, and nothing else. An
 * override-redirect window may be any size anywhere and nothing manages it, so a program
 * that is not the one in use (or one that never shows a window at all) must not get to paint
 * over everything and take her clicks: its menus are shown while that program is the one
 * she is using, and only then. */
static xwin *xwm_popup_parent(xwm *m)
{
    if (m->active && !m->active->or_ && xwin_listed(m->active)) return m->active;
    return NULL;
}

/* ── what the X server tells us ──────────────────────────────────── */
static void win_request(xwin *w, unsigned bit)
{
    if (!w->win) return;
    w->win->req |= bit;
    w->win->c->damage_seq++;
}

static void xwm_state_message(xwin *w, const uint8_t *d)
{
    xwm *m = w->m;
    uint32_t action = xr32(d);
    for (int k = 1; k <= 2; k++) {
        uint32_t a = xr32(d + 4 * k);
        if (!a) continue;
        if (a == m->atom[XA_STATE_FULLSCREEN]) {
            int now = action == 1 ? 1 : action == 0 ? 0 : !w->fullscreen;
            if (now != w->fullscreen) {
                w->fullscreen = now;
                xwm_set_net_state(m, w);
                win_request(w, now ? AURWL_REQ_FULLSCREEN : AURWL_REQ_UNFULLSCREEN);
            }
        } else if (a == m->atom[XA_STATE_MAX_VERT] || a == m->atom[XA_STATE_MAX_HORZ]) {
            win_request(w, (action == 0) ? AURWL_REQ_RESTORE : AURWL_REQ_MAXIMIZE);
        }
    }
}

static void xwm_event(xwm *m, const uint8_t *e)
{
    uint8_t type = e[0] & 0x7F;
    switch (type) {
    case 16: {                                          /* CreateNotify */
        uint32_t parent = xr32(e + 4), win = xr32(e + 8);
        if (parent != m->root || win == m->check) break;
        xwin *w = xwin_find(m, win);
        if (!w) w = xwin_new(m, win);
        if (!w) break;
        w->x = (int16_t)xr16(e + 12); w->y = (int16_t)xr16(e + 14);
        w->w = xr16(e + 16); w->h = xr16(e + 18);
        w->or_ = e[22] != 0;
        break;
    }
    case 17: {                                          /* DestroyNotify */
        xwin *w = xwin_find(m, xr32(e + 8));
        if (w) { xwin_free(w); xwm_client_list(m); }
        break;
    }
    case 18: {                                          /* UnmapNotify */
        xwin *w = xwin_find(m, xr32(e + 8));
        if (!w) break;
        w->mapped = 0;
        if (w->managed && !w->or_) { w->withdrawn = 1; xwm_set_wm_state(m, w, 0 /* Withdrawn */); }
        if (w->win) w->win->c->damage_seq++;
        break;
    }
    case 19: {                                          /* MapNotify */
        xwin *w = xwin_find(m, xr32(e + 8));
        if (!w) break;
        w->mapped = 1;
        w->or_ = e[12] != 0 || w->or_;
        if (w->win) w->win->c->damage_seq++;
        break;
    }
    case 20: {                                          /* MapRequest */
        uint32_t win = xr32(e + 8);
        if (xr32(e + 4) != m->root) break;
        xwin *w = xwin_find(m, win);
        if (!w) w = xwin_new(m, win);
        if (w) xwm_manage(w);
        else x_map(m, win);                             /* too many to manage: let it be seen */
        break;
    }
    case 22: {                                          /* ConfigureNotify */
        xwin *w = xwin_find(m, xr32(e + 8));
        if (!w) break;
        w->x = (int16_t)xr16(e + 16); w->y = (int16_t)xr16(e + 18);
        w->w = xr16(e + 20); w->h = xr16(e + 22);
        w->or_ = e[26] != 0;
        break;
    }
    case 23: {                                          /* ConfigureRequest */
        uint32_t win = xr32(e + 8);
        unsigned mask = xr16(e + 26);
        int32_t x = (int16_t)xr16(e + 16), y = (int16_t)xr16(e + 18);
        int32_t rw = xr16(e + 20), rh = xr16(e + 22);
        xwin *w = xwin_find(m, win);
        if (!w || w->or_) {                             /* not ours to manage: as asked */
            x_configure(m, win, mask & (XC_X | XC_Y | XC_W | XC_H), x, y, rw, rh);
            break;
        }
        int nw = (mask & XC_W) ? rw : w->w, nh = (mask & XC_H) ? rh : w->h;
        nw = xw_clamp_dim(nw, 0, 0, 0, 0); nh = xw_clamp_dim(nh, 0, 0, 0, 0);
        int changed = w->x != 0 || w->y != 0 || w->w != nw || w->h != nh;
        x_configure(m, win, XC_X | XC_Y | XC_W | XC_H, 0, 0, nw, nh);
        w->cfg_w = nw; w->cfg_h = nh;
        w->x = w->y = 0; w->w = nw; w->h = nh;          /* as asked: the server's notify confirms it */
        if (!changed) {                                 /* ICCCM: tell it where it is */
            uint8_t ev[32] = {0};
            ev[0] = 22;
            xw32(ev + 4, win); xw32(ev + 8, win);
            xw16(ev + 20, (uint16_t)nw); xw16(ev + 22, (uint16_t)nh);
            x_send_event(m, win, XM_STRUCTURE, ev);
        }
        break;
    }
    case 28: {                                          /* PropertyNotify */
        if (xr32(e + 4) == m->check) { xc_event(m, e); break; }
        xwin *w = xwin_find(m, xr32(e + 4));
        uint32_t a = xr32(e + 8);
        if (!w || e[16] != 0) break;
        if (a == XP_WM_NAME || a == XP_WM_CLASS || a == XP_WM_NORMAL_HINTS || a == XP_WM_HINTS ||
            a == XP_WM_TRANSIENT_FOR || a == m->atom[XA_NET_WM_NAME] || a == m->atom[XA_WM_PROTOCOLS] ||
            a == m->atom[XA_MOTIF_WM_HINTS])
            x_get_prop(m, w->id, a);
        break;
    }
    case 33: {                                          /* ClientMessage */
        uint32_t win = xr32(e + 4), t = xr32(e + 8);
        if (e[1] != 32) break;
        xwin *w = xwin_find(m, win);
        if (!w) break;
        if (t == m->atom[XA_WL_SURFACE_ID])           xwm_associate(w, xr32(e + 12));
        else if (t == m->atom[XA_NET_WM_STATE])        xwm_state_message(w, e + 12);
        else if (t == m->atom[XA_WM_CHANGE_STATE] && xr32(e + 12) == 3) win_request(w, AURWL_REQ_MINIMIZE);
        break;
    }
    default: xc_event(m, e); break;                      /* the clipboard's, or not ours to answer */
    }
}

/* ── what it answers ─────────────────────────────────────────────── */
static void xwm_setup_done(xwm *m);

static void xwm_reply(xwm *m, const uint8_t *r, size_t total)
{
    if (!m->pend_n) { xwm_kill(m); return; }
    xpend e = m->pend[m->pend_head];
    if (e.seq != xr16(r + 2)) { xwm_kill(m); return; }
    m->pend_head = (m->pend_head + 1) % XW_PEND_MAX; m->pend_n--;
    switch (e.kind) {
    case XK_ATOM:
        if (e.a < XA_COUNT) m->atom[e.a] = xr32(r + 8);
        if (--m->atoms_left == 0) xwm_setup_done(m);
        break;
    case XK_PROP: {
        xwin *w = xwin_find(m, e.a);
        if (!w) break;
        uint32_t type = xr32(r + 8), units = xr32(r + 16);
        int format = r[1];
        size_t bytes = (size_t)units * (size_t)(format / 8);
        if (format != 0 && (format == 8 || format == 16 || format == 32) && 32 + bytes <= total && bytes <= 4 * XW_PROP_WORDS)
            xwin_apply_prop(w, e.b, type, format, r + 32, units);
        if (w->props_left > 0 && --w->props_left == 0) xwm_finish_manage(w);
        break;
    }
    case XK_TREE: {
        size_t n = xr16(r + 16);
        if (32 + 4 * n > total) break;
        for (size_t i = 0; i < n && i < XW_MAX_WINS; i++) {
            uint32_t win = xr32(r + 32 + 4 * i);
            if (win == m->check) continue;
            if (!xwin_find(m, win) && !xwin_new(m, win)) continue;
            x_get_attrs(m, win);
        }
        break;
    }
    case XK_EXT:
        /* COMPOSITE. Xwayland makes a wl_surface for an X window only when
         * the window is redirected for manual compositing; a window manager
         * that never says "I will composite the screen" gets windows that
         * are mapped and never drawn. We do composite -- the shell does --
         * so say so: RedirectSubwindows(root, Manual) (minor 2). */
        if (e.a == 0 && r[8]) {
            uint8_t major = r[9];
            uint8_t *p = xreq(m, major, 2, 8);
            if (p) { xw32(p, m->root); p[4] = 1; }
        } else if (e.a == 0) XDBG("this X server has no COMPOSITE: windows will not be drawn");
        else xc_reply(m, &e, r, total);
        break;
    case XK_BIGREQ: case XK_XFVER: case XK_CLIPATOM: case XK_CLIPTGT: case XK_ATOMNAME: case XK_CLIPDATA:
        xc_reply(m, &e, r, total);
        break;
    case XK_ATTR: {                                      /* a window already there when we arrived */
        xwin *w = xwin_find(m, e.a);
        if (!w || total < 32) break;
        uint8_t map_state = r[26], or_ = r[27];
        w->or_ = or_ != 0;
        if (map_state == 2 /* IsViewable */) {
            w->mapped = 1;
            if (!or_) xwm_manage(w);
        }
        break;
    }
    default: break;
    }
}

/* "Bigger text" for X programs (W32): Xft.dpi in RESOURCE_MANAGER on the root, which Qt, Chromium and GTK
 * on X read when they start (one already open keeps its size until reopened). Written when the window
 * manager comes up and whenever her size changes. */
static void xw_text_scale(aurwl *c)
{
    xwm *m = c->x.wm;
    if (!m || m->dead || m->state != 2) return;
    char t[48];
    int n = snprintf(t, sizeof t, "Xft.dpi:\t%d\n", (int)(96.f * (c->text_scale >= 1.f ? c->text_scale : 1.f) + 0.5f));
    x_change_prop(m, m->root, 23 /* RESOURCE_MANAGER */, XP_STRING, 8, t, (size_t)n);
    xflush(m);
}

/* The atoms are known: become the window manager. */
static void xwm_setup_done(xwm *m)
{
    m->state = 2;
    xw_text_scale(m->c);
    x_select_events(m, m->root, XM_SUBSTRUCTURE_REDIRECT | XM_SUBSTRUCTURE_NOTIFY | XM_PROPERTY);

    /* The window that says a window manager is running (EWMH). */
    m->check = xnewid(m);
    uint8_t *p = xreq(m, 1, 0 /* depth: copy from parent */, 28);
    if (p) {
        xw32(p, m->check); xw32(p + 4, m->root);
        xw16(p + 12, 1); xw16(p + 14, 1);
        xw16(p + 18, 2 /* InputOnly */);
    }
    /* THE WINDOW MANAGER SELECTION. Xwayland, started with -wm, does not
     * listen for ordinary X programs until somebody owns WM_S0 -- that is
     * how it knows a window manager is running before programs map windows
     * nobody would place. Without this line every X program waits forever
     * at a socket that is never read. */
    x_query_extension(m, "Composite", 0);
    xc_start(m);
    x_set_selection_owner(m, m->check, m->atom[XA_WM_S0]);
    x_change_prop(m, m->root, m->atom[XA_NET_SUPPORTING_WM_CHECK], XP_WINDOW, 32, &m->check, 1);
    x_change_prop(m, m->check, m->atom[XA_NET_SUPPORTING_WM_CHECK], XP_WINDOW, 32, &m->check, 1);
    x_change_prop(m, m->check, m->atom[XA_NET_WM_NAME], m->atom[XA_UTF8_STRING], 8, "AurOS", 5);
    uint32_t sup[] = { m->atom[XA_NET_SUPPORTED], m->atom[XA_NET_SUPPORTING_WM_CHECK], m->atom[XA_NET_WM_NAME],
                       m->atom[XA_NET_ACTIVE_WINDOW], m->atom[XA_NET_CLIENT_LIST], m->atom[XA_NET_WM_STATE],
                       m->atom[XA_STATE_FULLSCREEN], m->atom[XA_NET_WM_WINDOW_TYPE] };
    x_change_prop(m, m->root, m->atom[XA_NET_SUPPORTED], XP_ATOM, 32, sup, sizeof sup / sizeof sup[0]);
    xwm_client_list(m);
    x_query_tree(m, m->root);                            /* windows that mapped before we were ready */
}

/* Setup reply: the id range we may use and the root window. */
static int xwm_setup_reply(xwm *m, const uint8_t *r, size_t total)
{
    if (total < 40 || r[0] != 1) { XDBG("setup refused or short (status %d, %zu bytes)", total ? r[0] : -1, total); return -1; }
    m->id_base = xr32(r + 12); m->id_mask = xr32(r + 16);
    m->maxreq = xr16(r + 26);                 /* maximum-request-length, 4-byte units */
    size_t vendor = xpad4(xr16(r + 24)), formats = r[29], off = 40 + vendor + 8 * formats;
    if (!m->id_mask || off + 40 > total) return -1;
    m->root = xr32(r + off);
    m->root_w = xr16(r + off + 20); m->root_h = xr16(r + off + 22);
    m->state = 1;
    XDBG("setup ok: root 0x%x %ux%u, ids 0x%x/0x%x", m->root, m->root_w, m->root_h, m->id_base, m->id_mask);
    m->atoms_left = XA_COUNT;
    for (int i = 0; i < XA_COUNT; i++) x_intern(m, i);
    return 0;
}

static void xwm_input(xwm *m)
{
    size_t off = 0;
    while (!m->dead) {
        size_t have = m->in_n - off;
        if (m->state == 0) {                             /* the connection setup reply */
            if (have < 8) break;
            size_t total = 8 + 4 * (size_t)xr16(m->in + off + 6);
            if (total > XW_REPLY_MAX) { xwm_kill(m); break; }
            if (have < total) break;
            if (xwm_setup_reply(m, m->in + off, total) < 0) { xwm_kill(m); break; }
            off += total;
            continue;
        }
        if (have < 32) break;
        const uint8_t *e = m->in + off;
        size_t total = 32;
        uint8_t t = e[0] & 0x7F;
        XDBG("packet type %u seq %u (state %d, %d pending)", t, xr16(e + 2), m->state, m->pend_n);
        if (t == 1 || t == 35) {                         /* a reply, or a generic event: longer */
            total += 4 * (size_t)xr32(e + 4);
            if (total > XW_REPLY_MAX) { xwm_kill(m); break; }
            if (have < total) break;
        }
        if (t == 1) xwm_reply(m, e, total);
        else if (t == 0) {                               /* an error: if it answers a request we waited on, stop waiting */
            if (m->pend_n && m->pend[m->pend_head].seq == xr16(e + 2)) {
                xpend dead = m->pend[m->pend_head];
                m->pend_head = (m->pend_head + 1) % XW_PEND_MAX; m->pend_n--;
                if (dead.kind == XK_ATOM) { if (--m->atoms_left == 0) xwm_setup_done(m); }
                else if (dead.kind == XK_PROP) {
                    xwin *w = xwin_find(m, dead.a);
                    if (w && w->props_left > 0 && --w->props_left == 0) xwm_finish_manage(w);
                }
                else if (dead.kind >= XK_BIGREQ) xc_failed(m, &dead);
            }
        }
        else if (t != 35) xwm_event(m, e);
        off += total;
    }
    if (off) {
        memmove(m->in, m->in + off, m->in_n - off);
        m->in_n -= off;
    }
    xflush(m);
    if (m->in_cap > (1u << 20) && m->in_n < 4096) {          /* a screenshot has passed: give the memory back */
        uint8_t *nb = realloc(m->in, 4096);
        if (nb) { m->in = nb; m->in_cap = 4096; }
    }
}

static int xwm_io(int fd, uint32_t mask, void *data)
{
    xwm *m = data;
    if (mask & WL_EVENT_WRITABLE) xflush(m);
    if (mask & WL_EVENT_READABLE) {
        for (;;) {
            if (xbuf_grow(&m->in, &m->in_cap, m->in_n + 4096, XW_IN_MAX) < 0) { xwm_kill(m); break; }
            ssize_t n = read(fd, m->in + m->in_n, m->in_cap - m->in_n);
            if (n > 0) { m->in_n += (size_t)n; xwm_input(m); if (m->dead) break; continue; }
            if (n == 0) { xwm_kill(m); break; }
            if (errno == EINTR) continue;
            if (errno != EAGAIN) xwm_kill(m);
            break;
        }
    } else if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) xwm_kill(m);
    return 0;
}

/* The connection is over (Xwayland exited, or sent nonsense): the X windows
 * are gone with it. The object stays until xwm_destroy so nothing holding
 * a pointer to it is left dangling mid-dispatch. */
static void xwm_kill(xwm *m)
{
    if (m->dead) return;
    XDBG("connection ended (state %d, %d windows, %zu bytes unread)", m->state, m->n_wins, m->in_n);
    m->dead = 1;
    if (m->src) { wl_event_source_remove(m->src); m->src = NULL; }
    if (m->fd >= 0) { close(m->fd); m->fd = -1; }
}

static xwm *xwm_create(aurwl *c, int fd, struct wl_client *xclient)
{
    XDBG("starting on fd %d", fd);
    xwm *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->c = c; m->fd = fd; m->xclient = xclient;
    m->maxreq = 65535;
    wl_list_init(&m->wins);
    wl_list_init(&m->xfers); wl_list_init(&m->pastes);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    m->src = wl_event_loop_add_fd(c->loop, fd, WL_EVENT_READABLE, xwm_io, m);
    if (!m->src) { close(fd); free(m); return NULL; }
    /* The connection setup request: little-endian, X11.0, no authority. */
    uint8_t setup[12] = { 'l', 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (xbuf_grow(&m->out, &m->out_cap, sizeof setup, XW_OUT_MAX) < 0) { xwm_kill(m); free(m); return NULL; }
    memcpy(m->out, setup, sizeof setup);
    m->out_n = sizeof setup;
    xflush(m);
    return m;
}

static void xwm_destroy(xwm *m)
{
    if (!m) return;
    xwm_kill(m);
    xc_cleanup(m);
    xwin *w, *t;
    wl_list_for_each_safe(w, t, &m->wins, link) xwin_free(w);
    free(m->in); free(m->out);
    free(m);
}

/* ── Xwayland itself ─────────────────────────────────────────────── */
/* The socket is made here, in /tmp/.X11-unix, with the lock file the X
 * tools look for; Xwayland is started on the first connection, handed the
 * already-listening socket, and exits when its last client does. We keep
 * the socket, so the display number is stable and a program started while
 * Xwayland is not running simply waits for it. File socket only, mode 0700:
 * the abstract socket Xorg also makes has no permissions at all. */

static int x_try_display(aurwl *c, int n)
{
    char lock[64], dir[] = "/tmp/.X11-unix", sock[64];
    snprintf(lock, sizeof lock, "/tmp/.X%d-lock", n);
    snprintf(sock, sizeof sock, "%s/X%d", dir, n);
    mkdir(dir, 01777);                                    /* usually there already (root's) */
    int lf = open(lock, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0444);
    if (lf < 0) {
        /* taken -- unless whoever took it is dead */
        char buf[16] = {0};
        int rf = open(lock, O_RDONLY | O_CLOEXEC);
        if (rf < 0) return -1;
        ssize_t k = read(rf, buf, sizeof buf - 1);
        close(rf);
        pid_t p = k > 0 ? (pid_t)atoi(buf) : 0;
        if (p > 0 && (kill(p, 0) == 0 || errno == EPERM)) return -1;
        unlink(lock);
        lf = open(lock, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0444);
        if (lf < 0) return -1;
    }
    char pid[16];
    int pl = snprintf(pid, sizeof pid, "%10d\n", (int)getpid());
    if (write(lf, pid, (size_t)pl) < 0) { /* the number is for the tools only */ }
    close(lf);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", sock);
    unlink(sock);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(fd, 8) < 0) {
        if (fd >= 0) close(fd);
        unlink(lock);
        return -1;
    }
    chmod(sock, 0700);
    c->x.display = n; c->x.lfd = fd;
    snprintf(c->x.lock_path, sizeof c->x.lock_path, "%s", lock);
    snprintf(c->x.sock_path, sizeof c->x.sock_path, "%s", sock);
    return 0;
}

static int x_available(void)
{
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/usr/bin:/bin";
    char *copy = strdup(path), *save = NULL;
    int found = 0;
    for (char *d = copy ? strtok_r(copy, ":", &save) : NULL; d; d = strtok_r(NULL, ":", &save)) {
        char full[512];
        snprintf(full, sizeof full, "%s/Xwayland", d);
        if (access(full, X_OK) == 0) { found = 1; break; }
    }
    free(copy);
    return found;
}

static int x_listen_ready(int fd, uint32_t mask, void *data);
static void x_xclient_gone(struct wl_listener *l, void *d);

static void x_arm(aurwl *c)
{
    if (c->x.enabled && c->x.lfd >= 0 && !c->x.lsrc && !c->x.pid)
        c->x.lsrc = wl_event_loop_add_fd(c->loop, c->x.lfd, WL_EVENT_READABLE, x_listen_ready, c);
}
static void x_disarm(aurwl *c)
{
    if (c->x.lsrc) { wl_event_source_remove(c->x.lsrc); c->x.lsrc = NULL; }
}

static void x_start(aurwl *c)
{
    int wl[2], wmp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wl) < 0) return;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wmp) < 0) { close(wl[0]); close(wl[1]); return; }
    struct wl_client *xc = wl_client_create(c->display, wl[0]);
    if (!xc) { close(wl[0]); close(wl[1]); close(wmp[0]); close(wmp[1]); return; }

    pid_t pid = fork();
    if (pid < 0) { wl_client_destroy(xc); close(wl[1]); close(wmp[0]); close(wmp[1]); return; }
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);        /* if the shell goes, so does it */
        setsid();
        signal(SIGPIPE, SIG_DFL);
        /* the three descriptors it is handed must survive exec */
        int keep[3] = { wl[1], wmp[1], c->x.lfd };
        for (int i = 0; i < 3; i++) fcntl(keep[i], F_SETFD, 0);
        char sw[16], wm[16], lf[16], disp[16];
        snprintf(sw, sizeof sw, "%d", wl[1]);
        snprintf(wm, sizeof wm, "%d", wmp[1]);
        snprintf(lf, sizeof lf, "%d", c->x.lfd);
        snprintf(disp, sizeof disp, ":%d", c->x.display);
        setenv("WAYLAND_SOCKET", sw, 1);
        unsetenv("WAYLAND_DISPLAY");
        unsetenv("DISPLAY");
        execlp("Xwayland", "Xwayland", disp, "-rootless", "-shm", "-terminate", "-nolisten", "tcp",
               "-listenfd", lf, "-wm", wm, (char *)NULL);
        _exit(127);
    }
    close(wl[1]); close(wmp[1]);
    c->x.pid = pid;
    c->x.client = xc;
    clock_gettime(CLOCK_MONOTONIC, &c->x.started);
    c->x.gone.notify = x_xclient_gone;
    wl_client_add_destroy_listener(xc, &c->x.gone);
    c->x.wm = xwm_create(c, wmp[0], xc);
    x_disarm(c);
    if (c->n_kids < (int)(sizeof c->kids / sizeof c->kids[0])) c->kids[c->n_kids++] = pid;
}

static int x_listen_ready(int fd, uint32_t mask, void *data)
{
    (void)fd; (void)mask;
    aurwl *c = data;
    if (!c->x.pid) x_start(c);
    return 0;
}

static void x_xclient_gone(struct wl_listener *l, void *d)
{
    (void)d;
    aurwl *c = wl_container_of(l, c, x.gone);
    c->x.client = NULL;
    wl_list_init(&l->link);
}

/* Xwayland exited (the last X program ended, or it died). */
static void x_stopped(aurwl *c)
{
    c->x.pid = 0;
    if (c->x.wm) { xwm_destroy(c->x.wm); c->x.wm = NULL; }
    if (c->x.client) { wl_client_destroy(c->x.client); c->x.client = NULL; }
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    /* One that does not even live two seconds is not going to: a missing
     * library, a bad flag. After three the machine stops offering X rather
     * than leave a program waiting at a socket nobody serves. */
    if (now.tv_sec - c->x.started.tv_sec < 2) c->x.failures++; else c->x.failures = 0;
    if (c->x.failures >= 3) {
        fprintf(stderr, "aurwl: Xwayland keeps failing; X programs will not run\n");
        c->x.enabled = 0;
        if (c->x.lfd >= 0) { close(c->x.lfd); c->x.lfd = -1; }
        unlink(c->x.sock_path); unlink(c->x.lock_path);
        return;
    }
    x_arm(c);
}

static void x_init(aurwl *c)
{
    c->x.lfd = -1;
    if (getenv("AUROS_NO_XWAYLAND") || !x_available()) return;
    for (int n = 0; n < 33; n++) if (x_try_display(c, n) == 0) break;
    if (c->x.lfd < 0) { fprintf(stderr, "aurwl: no free X display; X programs will not run\n"); return; }
    c->x.enabled = 1;
    x_arm(c);
}

static void x_shutdown(aurwl *c)
{
    x_disarm(c);
    if (c->x.pid) { kill(c->x.pid, SIGTERM); waitpid(c->x.pid, NULL, 0); c->x.pid = 0; }
    if (c->x.wm) { xwm_destroy(c->x.wm); c->x.wm = NULL; }
    if (c->x.lfd >= 0) { close(c->x.lfd); c->x.lfd = -1; unlink(c->x.sock_path); unlink(c->x.lock_path); }
    c->x.enabled = 0;
}

/* ── what the rest of aurwl.c calls ──────────────────────────────── */
static void xw_surface_gone(aurwl_win *w)
{
    if (w->xw) { w->xw->win = NULL; w->xw = NULL; }
}
static int xw_listed(const aurwl_win *w)   { return w->xw && xwin_shown(w->xw); }
static int xw_csd(const aurwl_win *w)      { return w->role == ROLE_X11 && w->xw && w->xw->csd && !w->xw->or_; }
static int xw_is_popup(const aurwl_win *w) { return w->role == ROLE_X11 && w->xw && w->xw->or_ && xwin_listed(w->xw); }
static aurwl_win *xw_popup_parent(const aurwl_win *w)
{
    if (!w->xw || !w->xw->or_) return NULL;
    xwin *p = xwm_popup_parent(w->xw->m);
    return p ? p->win : NULL;
}
static uint32_t xw_transient_id(const aurwl_win *w)
{
    if (!w->xw || w->xw->or_ || !w->xw->transient_for) return 0;
    xwin *p = xwin_find(w->xw->m, w->xw->transient_for);
    return (p && p->win && p->win != w) ? p->win->id : 0;
}
static void xw_popup_offset(const aurwl_win *w, int *x, int *y)
{
    int ox = 0, oy = 0;
    if (w->xw) {
        xwin *p = xwm_popup_parent(w->xw->m);
        ox = w->xw->x - (p ? p->x : 0);
        oy = w->xw->y - (p ? p->y : 0);
    }
    if (x) *x = ox;
    if (y) *y = oy;
}
static int xw_configure(aurwl_win *w, int width, int height, int act, int max, int full)
{
    if (!w->xw) return 0;
    xwm_configure(w->xw, width, height, act, max, full);
    return 1;
}
static void xc_focus_changed(aurwl *c);                       /* xclip.inc: the clipboard follows the keyboard */
static void xw_focus(aurwl *c, aurwl_win *w)
{
    if (!c->x.wm) return;
    if (w && w->xw) xwm_focus(c, w->xw);
    else            xwm_focus(c, NULL);
    xc_focus_changed(c);
}
/* The pointer is about to enter this window's surface: make it the top X window first. */
static void xw_raise(aurwl *c, aurwl_win *w)
{
    xwm *m = c->x.wm;
    if (!m || m->dead || !w || !w->xw || w->xw->or_ || !w->xw->ready) return;
    x_raise(m, w->xw->id);
    xflush(m);
}
static int xw_close(aurwl_win *w) { if (!w->xw) return 0; xwm_close(w->xw); return 1; }
static int xw_kill(aurwl_win *w)  { if (!w->xw) return 0; xwm_kill_window(w->xw); return 1; }
static void xw_init(aurwl *c)     { wl_list_init(&c->x.gone.link); x_init(c); }
static void xw_shutdown(aurwl *c)
{
    x_shutdown(c);
    if (c->x.client) { wl_list_remove(&c->x.gone.link); wl_list_init(&c->x.gone.link); c->x.client = NULL; }
}
static void xw_reaped(aurwl *c, pid_t pid) { if (pid && pid == c->x.pid) x_stopped(c); }

/* A surface of Xwayland's was just made: is it one a window has already
 * claimed? */
static void xw_surface_created(aurwl_win *s)
{
    aurwl *c = s->c;
    if (!c->x.wm || c->x.wm->dead || wl_resource_get_client(s->res) != c->x.client) return;
    uint32_t id = wl_resource_get_id(s->res);
    xwin *w;
    wl_list_for_each(w, &c->x.wm->wins, link)
        if (w->surf_id == id && !w->win) { xwm_associate(w, id); break; }
}

#include "xclip.inc"
