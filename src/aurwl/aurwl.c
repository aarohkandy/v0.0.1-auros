/* aurwl.c — see aurwl.h for why this exists and why it is built this way.
 *
 * The protocol surface here is not a guess. It is the set that a real
 * browser was observed to bind: WebKitGTK's Epiphany, run against a
 * headless reference compositor with WAYLAND_DEBUG=1, bound
 * wl_compositor v5, wl_subcompositor, wl_shm, wl_output v3,
 * zxdg_output_manager_v1, wl_data_device_manager, wp_viewporter,
 * wp_presentation and xdg_wm_base v5 -- and then rendered its entire
 * window through wl_shm, with no dmabuf and no GL at all. That last
 * fact is the one this whole design rests on: a CPU-only compositor is
 * enough to run a browser, which is exactly what a machine from 2013
 * with no usable GPU driver needs to hear.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-server.h"
#include "xdg-decoration-server.h"
#include "viewporter-server.h"
#include "xdg-output-server.h"

/* Protocols added after the build lists were written are compiled INTO this
 * file: the generated marshalling code is included rather than linked, so
 * that a new protocol is one XML line in build/forge's generator loop and
 * one include here, and no list of source files in the forge, the fuzzers,
 * the test scripts or the screenshot tool has to learn about it. (They are
 * .inc, not .c, so a glob over *-protocol.c cannot link them twice.) */
#include "idle-inhibit-server.h"
#include "idle-inhibit-protocol.inc"
/* wp_cursor_shape_manager_v1's get_tablet_tool_v2 names zwp_tablet_tool_v2.
 * This machine serves no tablet protocol and nobody can hold a tablet tool
 * object, so the type only has to exist for the request table to resolve. */
const struct wl_interface zwp_tablet_tool_v2_interface = { "zwp_tablet_tool_v2", 1, 0, NULL, 0, NULL };
#include "cursor-shape-server.h"
#include "cursor-shape-protocol.inc"

#include "aurwl.h"

/* Wishes (docs/AI.md §3.5): the typed-word matcher lives in wishhook.c and is
 * linked only into an image built with wishes="on". These are weak, so a
 * build without it has no matcher and these lines do nothing. */
void wishhook_key(aurwl *c, uint32_t code, int pressed) __attribute__((weak));
void wishhook_reset(void) __attribute__((weak));
void wishhook_app(const char *app_id) __attribute__((weak));
/* ... and the text-input-v3 half, which lets Wishes tell a password field
 * from any other (textinput.c; same rule). */
void textinput_init(struct wl_display *d) __attribute__((weak));
void textinput_shutdown(void) __attribute__((weak));
void textinput_focus(struct wl_client *client, struct wl_resource *surface) __attribute__((weak));

/* ── limits ─────────────────────────────────────────────────────── */
/* Bounds, not capacity planning: a client that asks for a 40000x40000
 * buffer is either broken or hostile, and either way the answer is to
 * refuse rather than to try the allocation. */
#define CFG_REMEMBER 32     /* configure serials kept per window; see aurwl_win.sent */
#define MAX_DIM     16384
/* How deep a chain of parents or subsurfaces may be before we stop
 * walking it. Any real window tree is a handful deep; a client can make
 * one a hundred thousand deep for the price of some memory. */
#define PARENT_MAX    256
#define MAX_WINS      128
/* THE CEILINGS A CLIENT CANNOT PUSH THROUGH.
 *
 * MAX_WINS sat here, defined and used nowhere, from the day it was
 * written -- so nothing capped surfaces, and 100,030 of them from one
 * client took the compositor from 2.6 MB to 81.7 MB with no refusal
 * and no error. Worse than the memory: every input event walks these
 * lists, so 100,000 wl_pointers made ONE mouse movement cost twelve
 * milliseconds, and a touchpad emits a hundred a second. The pointer
 * being ruined is the system's, not the hostile client's.
 *
 * The numbers are deliberately generous -- a browser with many tabs
 * and many menus is a normal client -- and are per client, so one
 * program cannot spend another's. */
#define MAX_SURFACES  4096      /* toplevels, popups, subsurfaces, cursors */
#define MAX_SEAT_OBJ    64      /* pointers/keyboards/touches per client   */
#define MAX_DATA_OBJ    64      /* data devices and sources per client     */
/* The smallest a viewport destination may be clamped to, whatever the
 * output is. A tiny or not-yet-known output must not make a legitimate
 * client's window degenerate. The real bound is the output itself --
 * see viewport_set_destination(). */
#define VIEWPORT_FLOOR  1024

/* A ROLE IS FOR LIFE.
 *
 * wayland.xml, wl_surface: "Once a wl_surface is given a role, it is
 * set permanently for the whole lifetime of the wl_surface object."
 *
 * It was not. Every role check tested a RESOURCE POINTER rather than
 * the role, and every role-object destructor set the role back to
 * ROLE_NONE -- so a surface could be a subsurface and then a toplevel,
 * or a cursor and then anything, and a mapped focused window could be
 * made to disappear from the window list without the unmap path
 * running, leaving the compositor's focus pointing at a window the
 * shell had already torn down and the client believing its keys were
 * still held.
 *
 * The comment on get_toplevel records that the two-xdg-roles version
 * of exactly this was a 127-byte write into freed memory. That hole
 * was closed for xdg-against-xdg and left open for
 * subsurface-against-xdg and cursor-against-anything. */
enum role { ROLE_NONE = 0, ROLE_TOPLEVEL, ROLE_POPUP, ROLE_SUBSURFACE, ROLE_CURSOR, ROLE_X11 };

static const char *role_name(int r)
{
    switch (r) {
    case ROLE_TOPLEVEL:   return "a window";
    case ROLE_POPUP:      return "a menu";
    case ROLE_SUBSURFACE: return "part of another window";
    case ROLE_CURSOR:     return "a pointer or a drag icon";
    case ROLE_X11:        return "an X11 window";
    default:              return "nothing";
    }
}

/* A half-open rectangle, x0 <= x < x1. Everywhere damage is tracked. */
typedef struct { int x0, y0, x1, y1; } irect;

/* wl_surface.set_input_region, as far as we keep it: the whole surface,
 * nothing at all (a click falls through to what is behind), or one box. */
enum { IN_ALL = 0, IN_NONE, IN_BOX };

/* The half of wl_surface state that is double-buffered. Wayland's
 * central promise is that a frame is atomic: everything between two
 * commits lands together or not at all. Getting this wrong produces
 * tearing that looks like a driver bug. */
typedef struct {
    struct wl_resource *buffer;       /* pending only; never kept past commit */
    int                 attached;     /* attach() was called this cycle */
    int                 dx, dy;
    int                 dmg_x0, dmg_y0, dmg_x1, dmg_y1;  /* buffer coords */
    int                 has_damage;
    int                 scale;
    int                 transform;
    /* viewporter */
    int                 vp_dst_w, vp_dst_h;
    int                 vp_src_set, vp_sx, vp_sy, vp_sw, vp_sh;  /* buffer px */
    /* input region */
    int                 in_kind, in_x0, in_y0, in_x1, in_y1;
} surf_state;

struct aurwl_win {
    aurwl              *c;
    struct wl_resource *res;          /* wl_surface */
    uint32_t            id;

    surf_state          pending, current;
    /* A client may destroy a wl_buffer it has attached but not yet
     * committed -- and an ordinary toolkit does exactly that when a
     * synchronized subsurface parks a buffer until its parent commits.
     * Without this listener the next commit called wl_shm_buffer_get()
     * on freed memory and then sent a release event through it. */
    struct wl_listener  buf_gone;
    struct wl_resource *buf_listening;
    struct wl_list      frame_cbs;    /* wl_resource link list */

    /* Our own copy of the client's pixels. Owning a copy rather than
     * mapping the client's buffer is a deliberate trade: one memcpy of
     * the damaged region per commit, in exchange for the client getting
     * its buffer back immediately and for the shell being free to
     * repaint at any moment without asking anyone's permission. The
     * alternative -- holding client memory across our frame -- makes
     * every paint a lifetime question and every truncated shm file a
     * SIGBUS in the compositor. */
    surface            *store;
    /* A window onto `store` -- same pixels, same stride, origin moved
     * to the client's declared window geometry. See
     * aurwl_win_content(). */
    surface             view;
    int                 cw, ch;       /* client's own idea of its size  */

    int                 role;
    int                 mapped;
    int                 dead;
    int                 opaque_fmt;   /* the last buffer was XRGB: every pixel opaque */
    int                 off_x, off_y; /* sum of wl_surface.attach offsets: a drag icon's hotspot */
    struct xwin        *xw;           /* ROLE_X11: the X window this surface is (xwm.inc) */

    /* xdg */
    struct wl_resource *xdg_surface;
    struct wl_resource *xdg_toplevel;
    struct wl_resource *xdg_popup;
    struct wl_resource *decoration;
    int                 deco_client;         /* it asked to draw its own bar */
    unsigned            req;                 /* AURWL_REQ_*, until the shell takes them */
    struct wl_resource *viewport;
    char                title[128];
    char                app_id[128];
    int                 want_w, want_h;      /* last configure we sent  */
    int                 want_act, want_max, want_full;
    int                 acked;
    int                 geo_x, geo_y, geo_w, geo_h;   /* window geometry */
    /* The configure serials sent to this surface, newest CFG_REMEMBER of
     * them, and the serial just past the last one the client answered.
     *
     * NOT just the newest. A client is entitled to be behind -- we send
     * configure A, then B, and it acks A, having only just processed it.
     * Firefox answers every one of them, in order, and a window that was
     * busy for a second while the shell moved windows about had a dozen
     * waiting; the answer to the oldest was refused as "never sent" and
     * the browser was disconnected (found on the booted image, when a
     * second window opened). What is refused is a serial that was NEVER
     * sent: see serial_was_sent. */
    uint32_t            sent[CFG_REMEMBER];
    uint32_t            n_sent;
    uint32_t            ack_floor;
    int                 has_geo;

    /* popup placement, relative to the parent surface's origin */
    int                 px, py;
    struct aurwl_win   *parent;

    /* SUBSURFACES.
     *
     * A toplevel is not one buffer but a tree of them: Firefox draws its
     * frame into the toplevel and the whole page into a wl_subsurface;
     * Chromium puts video in one, GTK4 and Qt put popups and video in
     * them. Each surface keeps its own pixels (`store`); the tree is
     * flattened into `flat` when the shell asks for the window's content.
     *
     * Stacking is wayland.xml's: a parent keeps one ordered list of its
     * children AND ITSELF, bottom to top, so "below the parent" is a
     * place in the list rather than a special case. There are two such
     * lists because place_above/place_below and set_position are state of
     * the PARENT, double-buffered: they take effect when the parent's own
     * state is applied. `sub_pend` is what the requests edit; `sub_cur` is
     * what is drawn and hit-tested. */
    struct wl_resource *subsurface;
    int                 sub_x, sub_y;           /* applied; parent coordinates */
    int                 sub_px, sub_py;         /* requested; applied with the parent */
    int                 sub_sync;               /* this one's own mode        */
    /* A synchronized commit's state waits here until the parent's state
     * is applied (wayland.xml, wl_subsurface). It is a snapshot taken AT
     * the commit: attach, damage and the like made afterwards belong to
     * the next one, and `pending` goes on collecting them. */
    surf_state          cached;
    int                 has_cache;
    struct wl_listener  cbuf_gone;
    struct wl_resource *cbuf_listening;
    struct wl_list      sub_pend, sub_cur;      /* the stack, bottom first    */
    struct wl_list      self_pend, self_cur;    /* where THIS surface sits in it */
    struct wl_list      pend_link, cur_link;    /* this one, in its parent's  */

    /* The flattened window: this surface and everything stacked on it,
     * in the surface's own pixels. Only a root has one, and only while
     * it has a visible subsurface -- a plain window is shown straight
     * from `store`, as it always was. Recomposed lazily, and only the
     * part that changed. */
    surface            *flat;
    irect               fdirty;
    int                 has_fdirty, fdirty_all;

    struct wl_list      children;            /* popups, by child_link    */
    struct wl_list      child_link;
    struct wl_list      link;                /* c->surfaces              */
};

/* One zwp_idle_inhibitor_v1 (see "idle inhibit" below). */
struct idle_inh {
    struct wl_resource *res;
    aurwl_win          *w;          /* NULL once its surface has gone */
    struct wl_list      link;
};

struct aurwl {
    struct wl_display  *display;
    struct wl_event_loop *loop;
    const char         *socket;

    struct wl_global   *g_compositor, *g_subcompositor, *g_seat, *g_output;
    struct wl_global   *g_xdg, *g_ddm, *g_deco, *g_viewporter, *g_xdg_output;
    struct wl_global   *g_idle_inhibit, *g_cursor_shape;

    struct wl_list      surfaces;
    struct wl_list      seats;        /* wl_seat resources              */
    struct wl_list      pointers, keyboards, touches, outputs, devices;
    /* Live wl_data_offers. An offer holds its source resource as user
     * data, and the source can be destroyed while the offer is still on
     * a client's clipboard menu -- so the offers have to be findable
     * when that happens. */
    struct wl_list      offers;
    /* Tracked so a mode change can re-send the logical size. It could
     * not before, so after a resize every client kept the old screen
     * size and sized its full-screen windows to it. */
    struct wl_list      xdg_outputs;
    /* zwp_idle_inhibitor_v1 objects: while one of them is on a visible
     * window the desktop must not turn the screen off. */
    struct wl_list      inhibitors;

    /* THE POINTER'S PICTURE. What the program under the pointer asked the
     * pointer to look like, for the shell to draw: nothing (the shell's own
     * arrow), a picture (wl_pointer.set_cursor), a named shape
     * (wp_cursor_shape_v1), or "hide it". It is valid only while the
     * pointer stays on that program's surface: any enter or leave clears it,
     * and the program sets it again, as the protocol requires. */
    struct {
        int                 kind;        /* AURWL_CURSOR_*                   */
        aurwl_win          *surf;        /* the cursor surface, for IMAGE    */
        int                 hx, hy;      /* hotspot in that surface          */
        int                 ox, oy;      /* its attach offsets at that moment */
        int                 shape;       /* wp_cursor_shape_device_v1 shape  */
    } cursor;
    /* The clipboard, when an X program owns it (xclip.inc): offered to Wayland programs as the mime types below. */
    int                 xsel_active;
    char                xsel_mimes[16][64];
    int                 xsel_n;
    /* XWAYLAND (xwm.inc): X programs run through it, started on demand. */
    struct {
        int                 enabled;
        int                 display;        /* the N of DISPLAY=:N                     */
        int                 lfd;            /* the listening socket, kept for good     */
        char                lock_path[64], sock_path[64];
        struct wl_event_source *lsrc;       /* watching it while Xwayland is not running */
        pid_t               pid;            /* Xwayland, 0 when not running            */
        struct xwm         *wm;
        struct wl_client   *client;         /* its Wayland connection                  */
        struct wl_listener  gone;
        struct timespec     started;
        int                 failures;
    } x;
    uint32_t            ptr_enter_serial;  /* the enter a set_cursor must quote */
    struct wl_list      cursor_devs;       /* wp_cursor_shape_device_v1        */

    int                 ow, oh, refresh_mhz;
    uint32_t            next_id;

    /* input */
    struct xkb_context *xkb;
    struct xkb_keymap  *keymap;
    struct xkb_state   *xkb_state;
    int                 keymap_fd;
    size_t              keymap_size;

    aurwl_win          *focus;        /* keyboard                        */
    aurwl_win          *ptr_focus;    /* pointer: the window (a root)    */
    /* The SURFACE the pointer is on -- the window itself, or one of the
     * subsurfaces stacked on it. This is the one that was sent
     * wl_pointer.enter and the one that gets motion and buttons. */
    aurwl_win          *ptr_surf;
    int                 btns_down;    /* for the implicit grab           */
    uint32_t            btn_serial;   /* serial of the last button press sent: start_drag needs it */

    /* DRAG AND DROP (wl_data_device.start_drag). While one is running the
     * pointer belongs to it: no wl_pointer event reaches any client, and
     * the surface under the pointer is told enter/motion/leave/drop
     * instead. */
    struct {
        int                 active;
        struct wl_resource *source;      /* the wl_data_source being dragged  */
        aurwl_win          *origin;      /* where it started                  */
        aurwl_win          *icon;        /* the picture that follows the pointer, or NULL */
        aurwl_win          *target;      /* the surface that last got `enter` */
        struct wl_resource *dev;         /* the target client's data device   */
        struct wl_resource *offer;       /* the offer it was handed           */
        uint32_t            serial;      /* of that enter                     */
        char                mime[64];    /* what the target accepted, "" none */
        uint32_t            dst_actions, dst_pref, chosen;
        int                 dropped;     /* `drop` sent; waiting for `finish` */
    } drag;
    int                 ptr_x, ptr_y; /* surface-local, last sent        */
    int                 ptr_rx, ptr_ry; /* the window's surface coordinates, last given */

    /* clipboard: the one data source currently offered as the selection */
    struct wl_resource *selection;
    uint32_t            selection_serial;

    uint32_t            damage_seq;

    pid_t               kids[64];
    int                 n_kids;
};

/* ── X11 windows (xwm.inc, included below) ─────────────────────── */
struct xwin;
static void        xw_surface_gone(aurwl_win *w);
static void        xw_surface_created(aurwl_win *w);
static int         xw_listed(const aurwl_win *w);          /* an X window the shell may show */
static int         xw_csd(const aurwl_win *w);             /* it draws its own title bar (_MOTIF_WM_HINTS) */
static void        xw_raise(aurwl *c, aurwl_win *w);        /* top of the X stack: the pointer is entering it */
static void        xw_selection_changed(aurwl *c);          /* the Wayland clipboard changed hands */
static void        xw_paste(aurwl *c, const char *mime, int fd);   /* a Wayland program pastes what an X program copied */
static void        selection_broadcast(aurwl *c);
static int         xw_is_popup(const aurwl_win *w);        /* override-redirect: a menu      */
static aurwl_win  *xw_popup_parent(const aurwl_win *w);
static void        xw_popup_offset(const aurwl_win *w, int *x, int *y);
static int         xw_configure(aurwl_win *w, int width, int height, int act, int max, int full);
static void        xw_focus(aurwl *c, aurwl_win *w);
static int         xw_close(aurwl_win *w);
static int         xw_kill(aurwl_win *w);
static void        xw_init(aurwl *c);
static void        xw_shutdown(aurwl *c);
static void        xw_reaped(aurwl *c, pid_t pid);

/* ── small helpers ──────────────────────────────────────────────── */

static uint32_t serial_of(aurwl *c) { return wl_display_next_serial(c->display); }

static void noop_destroy(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; wl_resource_destroy(r); }

/* One place sends a configure, and one place remembers which serial
 * went out -- so ack_configure has something to check against. */
static void send_configure(aurwl_win *w)
{
    if (!w || !w->xdg_surface) return;
    uint32_t ser = serial_of(w->c);
    w->sent[w->n_sent % CFG_REMEMBER] = ser;
    w->n_sent++;
    xdg_surface_send_configure(w->xdg_surface, ser);
}

static int serial_was_sent(const aurwl_win *w, uint32_t ser)
{
    uint32_t n = w->n_sent < CFG_REMEMBER ? w->n_sent : CFG_REMEMBER;
    for (uint32_t i = 0; i < n; i++) if (w->sent[i] == ser) return 1;
    /* More than CFG_REMEMBER went unanswered and the oldest are forgotten.
     * Serials only grow, so one between the last answer and the oldest we
     * still know is one we sent or one nobody could tell from it; refusing
     * it would disconnect a correct client for being slow. Only a hostile
     * one gains, and all it gains is that the window counts as answered. */
    if (w->n_sent > CFG_REMEMBER) {
        uint32_t oldest = w->sent[w->n_sent % CFG_REMEMBER];
        return (int32_t)(ser - w->ack_floor) >= 0 && (int32_t)(oldest - ser) > 0;
    }
    return 0;
}

static void res_unlink(struct wl_resource *r) { wl_list_remove(wl_resource_get_link(r)); }

/* Give a surface its one role, or refuse. `err_res` and `err_code` are
 * what the protocol says to complain on for THIS way of asking. */
static int take_role(aurwl_win *w, int role, struct wl_resource *err_res,
                     uint32_t err_code)
{
    if (!w) return 0;
    if (w->role != ROLE_NONE && w->role != role) {
        wl_resource_post_error(err_res, err_code,
                               "this surface is already %s and cannot also be %s",
                               role_name(w->role), role_name(role));
        return 0;
    }
    w->role = role;
    return 1;
}

/* HOW MANY OF A THING ONE CLIENT ALREADY HAS.
 *
 * Every input event walks these lists -- aurwl_pointer_motion,
 * aurwl_key, send_modifiers -- so their length is the cost of using
 * the machine. One client holding a hundred thousand wl_pointers made
 * a SINGLE mouse movement take twelve milliseconds, and a touchpad
 * sends a hundred a second. The pointer that stops working is not the
 * hostile program's; it is hers.
 *
 * Counting on demand rather than keeping a tally: these lists are one
 * or two entries long on a real machine, the count happens only when
 * a client asks for another, and a number kept in two places is a
 * number that will disagree with itself. */
static int count_for_client(struct wl_list *list, struct wl_client *cl)
{
    int n = 0;
    struct wl_resource *r;
    wl_resource_for_each(r, list)
        if (wl_resource_get_client(r) == cl) n++;
    return n;
}

/* Say no, in the way the protocol has for saying no. A client that has
 * asked for four thousand of something is either broken or hostile,
 * and either way the honest answer is to stop it rather than to let it
 * make the desktop unusable for everybody. */
static int too_many(struct wl_client *cl, struct wl_resource *r,
                    struct wl_list *list, int cap, const char *what)
{
    if (count_for_client(list, cl) < cap) return 0;
    wl_resource_post_error(r, 0, "no more than %d %s per program", cap, what);
    return 1;
}

/* RETIRE AN OFFER WITHOUT DESTROYING IT.
 *
 * A wl_data_offer is created by the compositor but OWNED BY THE
 * CLIENT: the protocol says "the client must destroy the previous
 * selection data_offer", and the interface has a destroy REQUEST and
 * no destructor EVENT -- so there is no way to tell a client that we
 * have destroyed one.
 *
 * Destroying them server-side, which is what retiring used to mean,
 * freed a server-allocated id and handed the same id out for the next
 * offer. The client's own object map refuses to re-reserve a live
 * server slot, so the connection died with EINVAL -- and a dead
 * connection is a dead application for GTK and Qt. TWO ORDINARY
 * COPIES, from one window, killed the program.
 *
 * The reason retiring exists is sound and unchanged: a client holding
 * an old offer must not go on reading a clipboard the user has since
 * replaced. So the offer is disarmed -- its back-pointer nulled, which
 * doffer_receive() already handles -- and dropped from our list,
 * because from that moment it is the client's object and nothing
 * else. The link is re-initialised so the destructor's own
 * wl_list_remove is harmless whenever the client gets round to it. */
static void offer_retire(struct wl_resource *offer)
{
    wl_resource_set_user_data(offer, NULL);
    struct wl_list *l = wl_resource_get_link(offer);
    wl_list_remove(l);
    wl_list_init(l);
}

/* Send an event to every resource in a list. The seat lists are usually
 * one element long; they are lists because a client is entitled to hold
 * two wl_pointers and expect both to work. */
#define FOR_EACH_RES(var, list) \
    struct wl_resource *var; wl_resource_for_each(var, list)

static struct wl_client *win_client(aurwl_win *w);

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int64_t clamp64(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ── wl_region ──────────────────────────────────────────────────── */
/* Regions describe where a surface is opaque and where it accepts
 * input. We track the bounding box only. The opaque hint is an
 * optimisation we do not yet take, and the input hint matters for
 * exactly one thing we care about -- a client-side drop shadow that
 * should not swallow clicks -- which a bounding box gets right for
 * every real toolkit, because they all subtract a border rather than
 * carve a hole. */
typedef struct { int x0, y0, x1, y1; int set; } aurwl_region;

/* x + w in 64 bits, saturated: both are the client's, and an int32 sum
 * past INT32_MAX is undefined behaviour in the process that owns the
 * display (found by tools/fuzz/fuzz_aurwl.c). */
static int end_of(int32_t at, int32_t len)
{
    int64_t e = (int64_t)at + len;
    return e > INT32_MAX ? INT32_MAX : (int)e;
}

static void region_add(struct wl_client *cl, struct wl_resource *r,
                       int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)cl; aurwl_region *g = wl_resource_get_user_data(r);
    if (w <= 0 || h <= 0) return;
    int x1 = end_of(x, w), y1 = end_of(y, h);
    if (!g->set) { g->x0 = x; g->y0 = y; g->x1 = x1; g->y1 = y1; g->set = 1; return; }
    if (x < g->x0) g->x0 = x;
    if (y < g->y0) g->y0 = y;
    if (x1 > g->x1) g->x1 = x1;
    if (y1 > g->y1) g->y1 = y1;
}
static void region_subtract(struct wl_client *cl, struct wl_resource *r,
                            int32_t x, int32_t y, int32_t w, int32_t h)
{ (void)cl; (void)r; (void)x; (void)y; (void)w; (void)h; }

static const struct wl_region_interface region_impl = {
    .destroy = noop_destroy, .add = region_add, .subtract = region_subtract,
};
static void region_free(struct wl_resource *r) { free(wl_resource_get_user_data(r)); }

/* ── wp_viewport ────────────────────────────────────────────────── */

static void viewport_destroy(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl;
    aurwl_win *w = wl_resource_get_user_data(r);
    if (w && w->viewport == r) {
        w->pending.vp_dst_w = w->pending.vp_dst_h = 0;
        w->pending.vp_src_set = 0;
        w->viewport = NULL;
    }
    wl_resource_destroy(r);
}
static void viewport_gone(struct wl_resource *r)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    if (w && w->viewport == r) w->viewport = NULL;
}
/* 24.8 fixed to whole pixels, rounding up, without overflowing: both
 * numbers are the client's. */
static int fixed_ceil(wl_fixed_t f)
{
    int64_t v = ((int64_t)f + 255) >> 8;
    return v > MAX_DIM ? MAX_DIM : (v < 1 ? 1 : (int)v);
}

/* The source rectangle crops the buffer before it is scaled -- Chromium
 * and mpv use it to hide the padding rows a video decoder adds. It is
 * kept in whole buffer pixels (the fraction is dropped: a video frame is
 * never cropped to half a pixel on purpose) and applied only where we
 * scale at all, which is when a subsurface is composited.
 *
 * (-1,-1,-1,-1) forgets it. Anything else with a negative origin or a
 * non-positive size is bad_value, and after the surface has gone every
 * request is no_surface. */
static void viewport_set_source(struct wl_client *cl, struct wl_resource *r,
                                wl_fixed_t x, wl_fixed_t y, wl_fixed_t w, wl_fixed_t h)
{
    (void)cl;
    aurwl_win *s = wl_resource_get_user_data(r);
    if (!s) {
        wl_resource_post_error(r, WP_VIEWPORT_ERROR_NO_SURFACE,
                               "the surface this viewport belongs to is gone");
        return;
    }
    const wl_fixed_t m1 = wl_fixed_from_int(-1);
    if (x == m1 && y == m1 && w == m1 && h == m1) { s->pending.vp_src_set = 0; return; }
    if (x < 0 || y < 0 || w <= 0 || h <= 0) {
        wl_resource_post_error(r, WP_VIEWPORT_ERROR_BAD_VALUE,
                               "source rectangle is not a rectangle");
        return;
    }
    /* Clamped: a fixed-point 24.8 value is at most 2^23, far past any
     * buffer we accept, and everything downstream is bounded by MAX_DIM. */
    s->pending.vp_src_set = 1;
    s->pending.vp_sx = clampi(wl_fixed_to_int(x), 0, MAX_DIM);
    s->pending.vp_sy = clampi(wl_fixed_to_int(y), 0, MAX_DIM);
    s->pending.vp_sw = fixed_ceil(w);
    s->pending.vp_sh = fixed_ceil(h);
}
/* SIX REQUESTS USED TO FREEZE THE WHOLE MACHINE.
 *
 * A destination is what the surface is scaled TO, and this took any
 * positive int32 and it was resampled to, which allocated dw*dh*4 and
 * ran a bilinear pass with per-channel float maths over every pixel --
 * on every commit. set_destination(16384, 16384) was
 * one gibibyte resident and 9.4 seconds inside a single dispatch, with
 * no repaint, no cursor and no keystroke anywhere on the desktop for
 * the duration; a client that simply kept committing kept it that way
 * for as long as it liked. On the 2 GB machines this product exists to
 * rescue, the allocation alone is an out-of-memory kill of the
 * compositor, which is every window on the screen.
 *
 * Nothing larger than the output can ever be SEEN, so nothing larger
 * than that is worth computing. The cap is a few multiples of a big
 * screen rather than exactly the output, because a client may
 * legitimately be configured for a display it has not been told about
 * yet, and because a cap that tracks a moving number is a cap that is
 * wrong during the move.
 *
 * viewporter.xml also requires bad_value for any non-positive pair
 * other than (-1,-1), which means "no destination". That was silently
 * turned into zero. */
static void viewport_set_destination(struct wl_client *cl, struct wl_resource *r,
                                     int32_t w, int32_t h)
{
    (void)cl;
    aurwl_win *s = wl_resource_get_user_data(r);
    if (!s) {
        wl_resource_post_error(r, WP_VIEWPORT_ERROR_NO_SURFACE,
                               "the surface this viewport belongs to is gone");
        return;
    }
    if (w == -1 && h == -1) {            /* "forget the destination" */
        s->pending.vp_dst_w = s->pending.vp_dst_h = 0;
        return;
    }
    if (w <= 0 || h <= 0) {
        wl_resource_post_error(r, WP_VIEWPORT_ERROR_BAD_VALUE,
                               "destination %dx%d is not a size", w, h);
        return;
    }
    /* THE SCREEN IS THE ONLY SIZE THAT MEANS ANYTHING.
     *
     * The destination is what the surface is resampled to, and the
     * shell then scales that into whatever space the archetype gave
     * it. Computing more pixels than the display has cannot make one
     * of them more visible; it only costs a bilinear pass over every
     * one of them, on every commit, on a machine with no GPU.
     *
     * A fixed generous cap was not enough: eight thousand squared is
     * still 268 megabytes and sixty-seven million pixels, which the
     * harness catches as a freeze. */
    int capw = s->c->ow > VIEWPORT_FLOOR ? s->c->ow : VIEWPORT_FLOOR;
    int caph = s->c->oh > VIEWPORT_FLOOR ? s->c->oh : VIEWPORT_FLOOR;
    if (w > capw) w = capw;
    if (h > caph) h = caph;
    s->pending.vp_dst_w = w;
    s->pending.vp_dst_h = h;
}
static const struct wp_viewport_interface viewport_impl = {
    .destroy = viewport_destroy, .set_source = viewport_set_source,
    .set_destination = viewport_set_destination,
};

/* ── the buffer copy ────────────────────────────────────────────── */

/* Copy the damaged rows of a freshly committed shm buffer into our own
 * store. Returns 0 if the buffer was unusable, in which case the caller
 * still releases it -- refusing to copy is not a reason to strand a
 * client waiting for its buffer back.
 *
 * `copied` is the rectangle of the store that was rewritten (buffer
 * pixels), and `resized` says the store is a different size from before
 * (or new): the flattened window needs that to repaint only what moved. */
static int take_buffer(aurwl_win *w, struct wl_resource *buf, irect *copied, int *resized)
{
    *copied = (irect){0, 0, 0, 0};
    *resized = 0;
    struct wl_shm_buffer *shm = wl_shm_buffer_get(buf);
    if (!shm) {
        /* No dmabuf support: a client that somehow got a non-shm buffer
         * to us gets told plainly rather than silently showing nothing. */
        wl_resource_post_error(buf, 0, "aurwl accepts wl_shm buffers only");
        return 0;
    }
    int32_t  bw     = wl_shm_buffer_get_width(shm);
    int32_t  bh     = wl_shm_buffer_get_height(shm);
    int32_t  stride = wl_shm_buffer_get_stride(shm);
    uint32_t fmt    = wl_shm_buffer_get_format(shm);

    if (bw <= 0 || bh <= 0 || bw > MAX_DIM || bh > MAX_DIM) return 0;
    if (fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XRGB8888) {
        wl_resource_post_error(buf, 0, "unsupported shm format %u", fmt);
        return 0;
    }
    if (stride < bw * 4) return 0;

    int full = 0;
    if (!w->store || w->store->w != bw || w->store->h != bh) {
        if (w->store) surface_free(w->store);
        w->store = surface_new(bw, bh);
        if (!w->store) return 0;
        full = 1;                       /* a new store has no old pixels */
        *resized = 1;
    }
    w->cw = bw; w->ch = bh;
    w->opaque_fmt = (fmt == WL_SHM_FORMAT_XRGB8888);

    int x0 = 0, y0 = 0, x1 = bw, y1 = bh;
    if (!full && w->current.has_damage) {
        x0 = clampi(w->current.dmg_x0, 0, bw);
        y0 = clampi(w->current.dmg_y0, 0, bh);
        x1 = clampi(w->current.dmg_x1, 0, bw);
        y1 = clampi(w->current.dmg_y1, 0, bh);
    }
    if (x1 <= x0 || y1 <= y0) return 1;  /* committed with nothing new */
    *copied = (irect){ x0, y0, x1, y1 };

    /* begin_access installs libwayland's SIGBUS handler, so a client
     * that truncates the shm file under us fails the copy instead of
     * killing the compositor and everything else running on it. */
    wl_shm_buffer_begin_access(shm);
    const uint8_t *src = wl_shm_buffer_get_data(shm);
    if (src) {
        int opaque = (fmt == WL_SHM_FORMAT_XRGB8888);
        /* memcpy, then the alpha in OUR (aligned) copy: the offset and
         * stride are the client's, and nothing makes them multiples of
         * four, so reading its pixels as uint32_t was a misaligned load
         * (undefined behaviour; found by tools/fuzz/fuzz_aurwl.c). */
        for (int y = y0; y < y1; y++) {
            const uint8_t *s = src + (size_t)y * stride + (size_t)x0 * 4;
            uint32_t *d = w->store->px + (size_t)y * w->store->stride + x0;
            memcpy(d, s, (size_t)(x1 - x0) * 4);
            if (opaque) for (int x = 0; x < x1 - x0; x++) d[x] |= 0xFF000000u;
        }
    }
    wl_shm_buffer_end_access(shm);
    return 1;
}

/* Resample a store to the size a viewport asks for.
 *
 * wp_viewporter says the surface's size is the viewport's destination,
 * not the buffer's -- the buffer is scaled to it. We advertised the
 * global and ignored the request, which happens to be harmless for
 * every client measured (GTK and Chromium both set a destination equal
 * to their buffer at scale 1) and is a lie the moment one does not.
 *
 * Bilinear, and only when the sizes actually differ, so the common case
 * costs a comparison. */
/* THE COMPOSITOR DOES NOT RESAMPLE. THE SHELL SCALES ONCE.
 *
 * There was a bilinear resample here, run on every commit, to turn the
 * buffer into the viewport's destination size. Two things were wrong
 * with it.
 *
 * It was the whole of a denial of service. The destination was any
 * positive int32, so six requests -- set_destination(16384, 16384) and
 * a one-pixel buffer -- allocated a gibibyte and spent nine seconds
 * inside a single dispatch, with nothing painted and no key or click
 * answered anywhere on the desktop, for as long as the client cared to
 * keep committing. Clamping the destination to the output helped and
 * was not enough: a one-pixel buffer stretched to the screen is still
 * a full pass over every pixel of it, on every commit, for ever.
 *
 * And it was wasted even when honest. The shell fits a window's
 * content into whatever rectangle the archetype gave it -- that is one
 * scale, already. Resampling first meant every viewport client was
 * scaled twice, losing quality both times, and paying a fresh
 * allocate-and-free of the whole surface on every frame.
 *
 * What a destination actually says is "this surface's LOGICAL size is
 * N by M". That is a fact about layout and about where the pointer is,
 * not about pixels, so it is recorded as one and costs nothing. */
static void logical_size(const aurwl_win *w, int *lw, int *lh)
{
    int bw = w->cw, bh = w->ch;
    if (w->has_geo) {
        /* The window, not the surface: aurwl_win_content() hands the shell
         * the window's rectangle, and a point on it is mapped over THAT.
         * Geometry first even when the surface has a viewport destination:
         * GTK4 sets one to the size of its whole buffer, shadow included,
         * so mapping the pointer over it put every click 20% too far right
         * and down -- big rows still opened, the Install button in the
         * store never did (seen with the real store on the real shell). */
        bw = w->geo_w; bh = w->geo_h;
    } else if (w->current.vp_dst_w > 0 && w->current.vp_dst_h > 0) {
        bw = w->current.vp_dst_w;
        bh = w->current.vp_dst_h;
    }
    if (lw) *lw = bw > 0 ? bw : 1;
    if (lh) *lh = bh > 0 ? bh : 1;
}

/* ── the subsurface tree ────────────────────────────────────────── */

/* Total pixels one flattened window may take: 128 MiB. A 4K screen is
 * 8 million. Past this the window is shown without its subsurfaces
 * rather than allocating whatever a client asked for. */
#define FLAT_MAX_PIXELS (32 * 1024 * 1024)

/* A list node that is always safe to remove again. */
static void unlink_init(struct wl_list *n) { wl_list_remove(n); wl_list_init(n); }

/* The surface at the top of this one's chain of parents: the toplevel or
 * popup the shell actually shows. */
static aurwl_win *root_of(aurwl_win *w)
{
    for (int i = 0; w && w->role == ROLE_SUBSURFACE && w->parent; i++) {
        if (i > PARENT_MAX) return NULL;
        w = w->parent;
    }
    return w;
}

/* wayland.xml, wl_subsurface: a surface behaves as synchronized if it is
 * set to, OR if its parent does -- "even if it is set to desynchronized,
 * a parent sub-surface may override it". So the question is asked of the
 * whole chain. */
static int eff_sync(const aurwl_win *w)
{
    for (int i = 0; w && w->role == ROLE_SUBSURFACE && w->parent; w = w->parent) {
        if (w->sub_sync) return 1;
        if (++i > PARENT_MAX) return 1;
    }
    return 0;
}

/* The surface's size in ITS OWN coordinates: what set_position and the
 * pointer are measured in. A viewport destination wins, then a source
 * rectangle with no destination, else the buffer divided by its scale.
 * (aurwl_win_logical_size() is a different question -- it also knows
 * the window geometry, which only a toplevel has.) */
static void surf_lsize(const aurwl_win *w, int *lw, int *lh)
{
    const surf_state *c = &w->current;
    int x = w->cw, y = w->ch;
    if (c->vp_dst_w > 0 && c->vp_dst_h > 0) { x = c->vp_dst_w; y = c->vp_dst_h; }
    else if (c->vp_src_set)                 { x = c->vp_sw;    y = c->vp_sh; }
    else {
        int sc = c->scale < 1 ? 1 : c->scale;
        x = (x + sc - 1) / sc; y = (y + sc - 1) / sc;
    }
    *lw = x > 0 ? x : 0; *lh = y > 0 ? y : 0;
}

/* How many flattened pixels are one unit of surface coordinates. 1,
 * unless the toplevel itself uses a buffer scale and no viewport. */
static int root_k(const aurwl_win *r)
{
    const surf_state *c = &r->current;
    if ((c->vp_dst_w > 0 && c->vp_dst_h > 0) || c->vp_src_set) return 1;
    return c->scale < 1 ? 1 : c->scale;
}

static int flat_dims(const aurwl_win *r, int *fw, int *fh)
{
    int lw, lh; surf_lsize(r, &lw, &lh);
    int64_t k = root_k(r), w = lw * k, h = lh * k;
    if (w <= 0 || h <= 0 || w > MAX_DIM * k || h > MAX_DIM * k || w * h > FLAT_MAX_PIXELS)
        return 0;
    *fw = (int)w; *fh = (int)h;
    return 1;
}

/* ── which part of the flattened window is stale ─────────────────── */
/* One rectangle, grown to cover everything reported. A caret blinking in
 * the page is a few hundred pixels; a video overlay moving is the whole
 * window. Two rectangles would repaint less in the rare case where both
 * happen at once, and cost a list on every commit of every frame. */
static void dirty_add(aurwl_win *r, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    if (r->fdirty_all) return;
    const int64_t B = (int64_t)1 << 30;      /* keep every sum inside int */
    if (x0 < -B) x0 = -B;
    if (y0 < -B) y0 = -B;
    if (x1 > B) x1 = B;
    if (y1 > B) y1 = B;
    if (x1 <= x0 || y1 <= y0) return;
    if (!r->has_fdirty) {
        r->fdirty = (irect){ (int)x0, (int)y0, (int)x1, (int)y1 };
        r->has_fdirty = 1;
        return;
    }
    if (x0 < r->fdirty.x0) r->fdirty.x0 = (int)x0;
    if (y0 < r->fdirty.y0) r->fdirty.y0 = (int)y0;
    if (x1 > r->fdirty.x1) r->fdirty.x1 = (int)x1;
    if (y1 > r->fdirty.y1) r->fdirty.y1 = (int)y1;
}

/* Something about the stack changed shape (a surface came, went, moved,
 * was restacked, resized): repaint the whole window. These are rare next
 * to content damage, and working out the exact union of old and new
 * rectangles for a restack is where the bugs would be. */
static void dirty_all(aurwl_win *w)
{
    aurwl_win *r = root_of(w);
    if (r) r->fdirty_all = 1;
}

/* A surface's own damage. `buf` is in its buffer's pixels, or NULL for
 * all of it. Mapped through the surface's position and, if the buffer is
 * being scaled to a different size, widened to the whole surface. */
static void dirty_surface(aurwl_win *w, const irect *buf)
{
    aurwl_win *r = root_of(w);
    if (!r || r->fdirty_all) return;
    int ax = 0, ay = 0, i = 0;
    for (const aurwl_win *a = w; a->role == ROLE_SUBSURFACE && a->parent; a = a->parent) {
        ax += a->sub_x; ay += a->sub_y;
        if (++i > PARENT_MAX) { r->fdirty_all = 1; return; }
    }
    int64_t k = root_k(r);
    int lw, lh; surf_lsize(w, &lw, &lh);
    int64_t dw = lw * k, dh = lh * k, ox = ax * k, oy = ay * k;
    if (buf && dw == w->cw && dh == w->ch && !w->current.vp_src_set)
        dirty_add(r, ox + buf->x0, oy + buf->y0, ox + buf->x1, oy + buf->y1);
    else
        dirty_add(r, ox, oy, ox + dw, oy + dh);
}

/* ── compositing ─────────────────────────────────────────────────── */

/* v * a / 255, rounded. */
static inline uint32_t mul255(uint32_t v, uint32_t a)
{ uint32_t t = v * a + 128; return (t + (t >> 8)) >> 8; }

/* Source over destination, both premultiplied ARGB -- what wl_shm hands
 * us and what the store holds. Each channel is s + d * (255 - sa) / 255,
 * so alpha composites the same way colour does. */
static inline uint32_t over_px(uint32_t d, uint32_t s)
{
    uint32_t sa = s >> 24;
    if (sa == 255) return s;
    if (sa == 0)   return d;
    uint32_t ia = 255 - sa, out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t v = mul255((d >> sh) & 255, ia) + ((s >> sh) & 255);
        out |= (v > 255 ? 255 : v) << sh;
    }
    return out;
}

/* Put the `sr` part of `src` on the dw x dh rectangle at (dx,dy) of
 * `dst`, inside `clip` only. The same size is a straight copy per row;
 * a different size (a viewport) is nearest-neighbour, sampled at pixel
 * centres. Nearest, because this runs on the CPU of a 2013 laptop for
 * every damaged pixel of a video, and the shell's own bilinear pass
 * still runs once over the result. */
static int64_t blit_over(surface *dst, const irect *clip,
                         int64_t dx, int64_t dy, int64_t dw, int64_t dh,
                         const surface *src, irect sr, int opaque)
{
    int64_t x0 = dx > clip->x0 ? dx : clip->x0;
    int64_t y0 = dy > clip->y0 ? dy : clip->y0;
    int64_t x1 = dx + dw < clip->x1 ? dx + dw : clip->x1;
    int64_t y1 = dy + dh < clip->y1 ? dy + dh : clip->y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dst->w) x1 = dst->w;
    if (y1 > dst->h) y1 = dst->h;
    int64_t srw = sr.x1 - sr.x0, srh = sr.y1 - sr.y0;
    if (x0 >= x1 || y0 >= y1 || srw <= 0 || srh <= 0 || dw <= 0 || dh <= 0) return 0;

    int same = (dw == srw && dh == srh);
    for (int64_t y = y0; y < y1; y++) {
        int64_t sy = sr.y0 + (same ? y - dy : ((2 * (y - dy) + 1) * srh) / (2 * dh));
        const uint32_t *srow = src->px + (size_t)sy * src->stride;
        uint32_t *drow = dst->px + (size_t)y * dst->stride;
        if (same) {
            const uint32_t *sp = srow + sr.x0 + (x0 - dx);
            if (opaque) memcpy(drow + x0, sp, (size_t)(x1 - x0) * 4);
            else for (int64_t x = x0; x < x1; x++) drow[x] = over_px(drow[x], sp[x - x0]);
        } else {
            for (int64_t x = x0; x < x1; x++) {
                int64_t sx = sr.x0 + ((2 * (x - dx) + 1) * srw) / (2 * dw);
                drow[x] = over_px(drow[x], srow[sx]);
            }
        }
    }
    return (x1 - x0) * (y1 - y0);
}

/* Paint `s` and everything stacked with it, bottom to top. The surface
 * itself is one entry in its own stack, so a child placed below it is
 * painted first and shows through wherever the surface is transparent. */
static void compose_node(aurwl_win *s, int64_t ox, int64_t oy, int64_t k,
                         surface *flat, const irect *clip, int depth, int64_t *left)
{
    /* No buffer means not mapped, and a surface that is not mapped takes
     * everything stacked on it out of sight with it. `left` is the work
     * this repaint may still do, in pixels: a program can stack four
     * thousand subsurfaces, each stretched over the whole window by a
     * viewport, and nothing else limits what painting them costs. A real
     * window overdraws a few times; past the budget the rest is skipped
     * (the top of a hostile stack is lost, and it is the only one hurt). */
    if (depth > PARENT_MAX || !s->store || *left <= 0) return;
    for (struct wl_list *n = s->sub_cur.next; n != &s->sub_cur; n = n->next) {
        if (n == &s->self_cur) {
            int lw, lh; surf_lsize(s, &lw, &lh);
            irect sr = { 0, 0, s->store->w, s->store->h };
            if (s->current.vp_src_set) {
                irect q = { s->current.vp_sx, s->current.vp_sy,
                            s->current.vp_sx + s->current.vp_sw,
                            s->current.vp_sy + s->current.vp_sh };
                if (q.x1 > sr.x1) q.x1 = sr.x1;
                if (q.y1 > sr.y1) q.y1 = sr.y1;
                if (q.x0 < q.x1 && q.y0 < q.y1) sr = q;
            }
            *left -= blit_over(flat, clip, ox, oy, lw * k, lh * k, s->store, sr, s->opaque_fmt);
        } else {
            aurwl_win *ch = wl_container_of(n, ch, cur_link);
            compose_node(ch, ox + (int64_t)ch->sub_x * k, oy + (int64_t)ch->sub_y * k,
                         k, flat, clip, depth + 1, left);
        }
    }
}

static int has_visible_sub(const aurwl_win *r)
{
    for (const struct wl_list *n = r->sub_cur.next; n != &r->sub_cur; n = n->next) {
        if (n == &r->self_cur) continue;
        const aurwl_win *ch = wl_container_of(n, ch, cur_link);
        if (ch->store) return 1;
    }
    return 0;
}

/* The window as one picture, or NULL when it IS one picture (no visible
 * subsurface) and `store` should be shown directly. Brings the picture up
 * to date first -- only the rectangle that changed since the last call. */
static surface *flat_get(aurwl_win *r)
{
    int fw, fh;
    if (r->role == ROLE_SUBSURFACE || !r->store || !has_visible_sub(r) ||
        !flat_dims(r, &fw, &fh)) {
        if (r->flat) { surface_free(r->flat); r->flat = NULL; }
        r->fdirty_all = 1;          /* nothing kept up to date meanwhile */
        return NULL;
    }
    if (!r->flat || r->flat->w != fw || r->flat->h != fh) {
        if (r->flat) surface_free(r->flat);
        r->flat = surface_new(fw, fh);
        if (!r->flat) return NULL;
        r->fdirty_all = 1;
    }
    if (!r->fdirty_all && !r->has_fdirty) return r->flat;

    irect clip = r->fdirty_all ? (irect){ 0, 0, fw, fh } : r->fdirty;
    if (clip.x0 < 0) clip.x0 = 0;
    if (clip.y0 < 0) clip.y0 = 0;
    if (clip.x1 > fw) clip.x1 = fw;
    if (clip.y1 > fh) clip.y1 = fh;
    if (clip.x0 < clip.x1 && clip.y0 < clip.y1) {
        for (int y = clip.y0; y < clip.y1; y++)
            memset(r->flat->px + (size_t)y * r->flat->stride + clip.x0, 0,
                   (size_t)(clip.x1 - clip.x0) * 4);
        int64_t left = 8 * (int64_t)fw * fh + (1 << 20);
        compose_node(r, 0, 0, root_k(r), r->flat, &clip, 0, &left);
    }
    r->fdirty_all = r->has_fdirty = 0;
    return r->flat;
}

/* ── which surface is under a point ──────────────────────────────── */

static int in_input(const aurwl_win *s, int x, int y)
{
    int lw, lh; surf_lsize(s, &lw, &lh);
    if (x < 0 || y < 0 || x >= lw || y >= lh) return 0;
    const surf_state *c = &s->current;
    switch (c->in_kind) {
    case IN_NONE: return 0;
    case IN_BOX:  return x >= c->in_x0 && x < c->in_x1 && y >= c->in_y0 && y < c->in_y1;
    default:      return 1;
    }
}

/* The topmost surface in `s`'s tree that takes input at (x,y), given in
 * `s`'s coordinates; *lx,*ly come back in that surface's own. Top down,
 * so what is drawn on top is what is clicked. A surface with an empty
 * input region is transparent to the pointer, which is how an overlay
 * lets clicks through to the page under it. */
static aurwl_win *pick(aurwl_win *s, int x, int y, int *lx, int *ly, int depth)
{
    if (depth > PARENT_MAX || !s->store) return NULL;
    for (struct wl_list *n = s->sub_cur.prev; n != &s->sub_cur; n = n->prev) {
        if (n == &s->self_cur) {
            if (in_input(s, x, y)) { *lx = x; *ly = y; return s; }
            continue;
        }
        aurwl_win *ch = wl_container_of(n, ch, cur_link);
        aurwl_win *hit = pick(ch, x - ch->sub_x, y - ch->sub_y, lx, ly, depth + 1);
        if (hit) return hit;
    }
    return NULL;
}

/* ── wl_surface ─────────────────────────────────────────────────── */

static void surface_map(aurwl_win *w);
static void surface_unmap(aurwl_win *w);
static void ptr_leave(aurwl *c);
static void drag_cancel(aurwl *c);
static void drag_leave(aurwl *c);
static void cursor_reset(aurwl *c);

static void buffer_gone(struct wl_listener *l, void *data)
{
    (void)data;
    aurwl_win *w = wl_container_of(l, w, buf_gone);
    /* Forget the attach rather than treating it as attach(NULL): a
     * client that throws away a buffer it never committed has not asked
     * to be unmapped, and unmapping it would make a window vanish
     * because of a bookkeeping detail the user never sees. */
    w->pending.buffer = NULL;
    w->pending.attached = 0;
    w->buf_listening = NULL;
}
static void watch_buffer(aurwl_win *w, struct wl_resource *buf)
{
    if (w->buf_listening == buf) return;
    if (w->buf_listening) { wl_list_remove(&w->buf_gone.link); w->buf_listening = NULL; }
    if (!buf) return;
    w->buf_gone.notify = buffer_gone;
    wl_resource_add_destroy_listener(buf, &w->buf_gone);
    w->buf_listening = buf;
}

/* The same for a buffer parked in the cache of a synchronized
 * subsurface: the client may destroy it before the parent commits. The
 * frame is lost -- the surface goes on showing its last one -- but the
 * rest of that commit still lands. */
static void cbuffer_gone(struct wl_listener *l, void *data)
{
    (void)data;
    aurwl_win *w = wl_container_of(l, w, cbuf_gone);
    w->cached.buffer = NULL;
    w->cached.attached = 0;
    w->cbuf_listening = NULL;
}
static void cwatch_buffer(aurwl_win *w, struct wl_resource *buf)
{
    if (w->cbuf_listening == buf) return;
    if (w->cbuf_listening) { wl_list_remove(&w->cbuf_gone.link); w->cbuf_listening = NULL; }
    if (!buf) return;
    w->cbuf_gone.notify = cbuffer_gone;
    wl_resource_add_destroy_listener(buf, &w->cbuf_gone);
    w->cbuf_listening = buf;
}

static void surf_attach(struct wl_client *cl, struct wl_resource *r,
                        struct wl_resource *buf, int32_t dx, int32_t dy)
{
    (void)cl; aurwl_win *w = wl_resource_get_user_data(r);
    watch_buffer(w, buf);
    w->pending.buffer = buf;
    w->pending.attached = 1;
    if (wl_resource_get_version(r) < WL_SURFACE_OFFSET_SINCE_VERSION) {
        w->pending.dx = dx; w->pending.dy = dy;
    }
}
static void surf_offset(struct wl_client *cl, struct wl_resource *r, int32_t dx, int32_t dy)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r); w->pending.dx = dx; w->pending.dy = dy; }

static void damage_accum(surf_state *st, int x, int y, int ww, int hh, int scale)
{
    if (ww <= 0 || hh <= 0) return;
    /* In 64 bits and clamped. A client may send any int32 for any of
     * these, and (x + ww) * scale overflows -- which is undefined
     * behaviour, not merely a wrong rectangle. The copy clamps to the
     * buffer afterwards, so this never reached out of bounds, but
     * undefined behaviour is not a thing to leave lying about in the
     * process that owns the display. */
    long long sc = scale < 1 ? 1 : scale;
    long long lx0 = (long long)x * sc,          ly0 = (long long)y * sc;
    long long lx1 = ((long long)x + ww) * sc,   ly1 = ((long long)y + hh) * sc;
    if (lx0 < 0) lx0 = 0;
    if (ly0 < 0) ly0 = 0;
    if (lx1 > MAX_DIM) lx1 = MAX_DIM;
    if (ly1 > MAX_DIM) ly1 = MAX_DIM;
    if (lx1 <= lx0 || ly1 <= ly0) return;
    int x0 = (int)lx0, y0 = (int)ly0, x1 = (int)lx1, y1 = (int)ly1;
    if (!st->has_damage) { st->dmg_x0 = x0; st->dmg_y0 = y0; st->dmg_x1 = x1; st->dmg_y1 = y1; st->has_damage = 1; return; }
    if (x0 < st->dmg_x0) st->dmg_x0 = x0;
    if (y0 < st->dmg_y0) st->dmg_y0 = y0;
    if (x1 > st->dmg_x1) st->dmg_x1 = x1;
    if (y1 > st->dmg_y1) st->dmg_y1 = y1;
}
static void surf_damage(struct wl_client *cl, struct wl_resource *r,
                        int32_t x, int32_t y, int32_t ww, int32_t hh)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r);
  damage_accum(&w->pending, x, y, ww, hh, w->pending.scale); }

static void surf_damage_buffer(struct wl_client *cl, struct wl_resource *r,
                               int32_t x, int32_t y, int32_t ww, int32_t hh)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r);
  damage_accum(&w->pending, x, y, ww, hh, 1); }

static void surf_frame(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    struct wl_resource *cb = wl_resource_create(cl, &wl_callback_interface, 1, id);
    if (!cb) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(cb, NULL, NULL, res_unlink);
    wl_list_insert(w->frame_cbs.prev, wl_resource_get_link(cb));
}
static void surf_set_opaque(struct wl_client *cl, struct wl_resource *r, struct wl_resource *reg)
{ (void)cl; (void)r; (void)reg; }
/* Where the surface takes the pointer. NULL is "everywhere"; a region
 * with nothing in it is "nowhere" -- a click goes to whatever is behind --
 * and anything else is the box around what was added (see aurwl_region).
 * The region is copied now: changing the wl_region later changes nothing. */
static void surf_set_input(struct wl_client *cl, struct wl_resource *r, struct wl_resource *reg)
{
    (void)cl; aurwl_win *w = wl_resource_get_user_data(r);
    surf_state *p = &w->pending;
    aurwl_region *g = reg ? wl_resource_get_user_data(reg) : NULL;
    if (!reg || !g)  { p->in_kind = IN_ALL; return; }
    if (!g->set)     { p->in_kind = IN_NONE; return; }
    p->in_kind = IN_BOX;
    p->in_x0 = g->x0; p->in_y0 = g->y0; p->in_x1 = g->x1; p->in_y1 = g->y1;
}
static void surf_set_transform(struct wl_client *cl, struct wl_resource *r, int32_t t)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r); w->pending.transform = t; }
static void surf_set_scale(struct wl_client *cl, struct wl_resource *r, int32_t s)
{
    (void)cl; aurwl_win *w = wl_resource_get_user_data(r);
    if (s < 1) { wl_resource_post_error(r, WL_SURFACE_ERROR_INVALID_SCALE, "scale %d", s); return; }
    w->pending.scale = s;
}

/* THE COMMIT, AND THE CACHE.
 *
 * A commit takes the pending state and applies it -- unless the surface
 * is a subsurface that behaves as synchronized, in which case it is set
 * aside until its parent's state is applied. That is what lets a parent
 * and its children change together: a window and the page inside it
 * resize as one frame instead of tearing against each other.
 *
 * Both paths start the same way: fold what `pending` holds into the
 * cache. Pending is then empty and goes on collecting the NEXT frame;
 * the cache is the frame. Two synchronized commits before the parent's
 * merge into one, the newer buffer winning and the older being released
 * to its client unseen. */
static void cache_pending(aurwl_win *w)
{
    surf_state *p = &w->pending, *k = &w->cached;
    if (!w->has_cache) { memset(k, 0, sizeof *k); w->has_cache = 1; }
    if (p->attached) {
        if (k->attached && k->buffer && k->buffer != p->buffer)
            wl_buffer_send_release(k->buffer);        /* never reached the screen */
        k->buffer = p->buffer; k->attached = 1;
        cwatch_buffer(w, p->buffer);
    }
    if (p->has_damage) {
        if (!k->has_damage) {
            k->dmg_x0 = p->dmg_x0; k->dmg_y0 = p->dmg_y0;
            k->dmg_x1 = p->dmg_x1; k->dmg_y1 = p->dmg_y1; k->has_damage = 1;
        } else {
            if (p->dmg_x0 < k->dmg_x0) k->dmg_x0 = p->dmg_x0;
            if (p->dmg_y0 < k->dmg_y0) k->dmg_y0 = p->dmg_y0;
            if (p->dmg_x1 > k->dmg_x1) k->dmg_x1 = p->dmg_x1;
            if (p->dmg_y1 > k->dmg_y1) k->dmg_y1 = p->dmg_y1;
        }
    }
    /* Everything else is a standing value, not an event: the newest wins. */
    k->scale = p->scale; k->transform = p->transform;
    k->vp_dst_w = p->vp_dst_w; k->vp_dst_h = p->vp_dst_h;
    k->vp_src_set = p->vp_src_set;
    k->vp_sx = p->vp_sx; k->vp_sy = p->vp_sy; k->vp_sw = p->vp_sw; k->vp_sh = p->vp_sh;
    k->in_kind = p->in_kind;
    k->in_x0 = p->in_x0; k->in_y0 = p->in_y0; k->in_x1 = p->in_x1; k->in_y1 = p->in_y1;

    k->dx = (int)clamp64((int64_t)k->dx + p->dx, -(1 << 20), 1 << 20);   /* attach offsets add up until applied */
    k->dy = (int)clamp64((int64_t)k->dy + p->dy, -(1 << 20), 1 << 20);
    p->dx = p->dy = 0;
    p->attached = 0; p->buffer = NULL; p->has_damage = 0;
    watch_buffer(w, NULL);
}

static void drop_cache(aurwl_win *w)
{
    if (!w->has_cache) return;
    if (w->cached.attached && w->cached.buffer) wl_buffer_send_release(w->cached.buffer);
    cwatch_buffer(w, NULL);
    memset(&w->cached, 0, sizeof w->cached);
    w->has_cache = 0;
}

static void apply_cached(aurwl_win *w, int depth);

/* The parent's state has just taken effect: its children's positions and
 * stacking take effect with it, and so does anything a synchronized child
 * was holding back for this moment. */
static void children_apply(aurwl_win *p, int depth)
{
    int changed = 0;
    for (struct wl_list *n = p->sub_pend.next; n != &p->sub_pend; n = n->next) {
        if (n == &p->self_pend) continue;
        aurwl_win *ch = wl_container_of(n, ch, pend_link);
        if (ch->sub_x != ch->sub_px || ch->sub_y != ch->sub_py) {
            ch->sub_x = ch->sub_px; ch->sub_y = ch->sub_py;
            changed = 1;
        }
    }
    /* Same order as the one being drawn? Walk both. */
    struct wl_list *a = p->sub_pend.next, *b = p->sub_cur.next;
    int same = 1;
    for (; a != &p->sub_pend && b != &p->sub_cur; a = a->next, b = b->next) {
        const aurwl_win *wa = (a == &p->self_pend) ? p : wl_container_of(a, wa, pend_link);
        const aurwl_win *wb = (b == &p->self_cur)  ? p : wl_container_of(b, wb, cur_link);
        if (wa != wb) { same = 0; break; }
    }
    if (same && (a != &p->sub_pend || b != &p->sub_cur)) same = 0;
    if (!same) {
        wl_list_init(&p->sub_cur);
        for (struct wl_list *n = p->sub_pend.next; n != &p->sub_pend; n = n->next) {
            if (n == &p->self_pend) { wl_list_insert(p->sub_cur.prev, &p->self_cur); continue; }
            aurwl_win *ch = wl_container_of(n, ch, pend_link);
            wl_list_insert(p->sub_cur.prev, &ch->cur_link);
        }
        changed = 1;
    }
    if (changed) { dirty_all(p); p->c->damage_seq++; }

    for (struct wl_list *n = p->sub_pend.next; n != &p->sub_pend; n = n->next) {
        if (n == &p->self_pend) continue;
        aurwl_win *ch = wl_container_of(n, ch, pend_link);
        if (ch->has_cache) apply_cached(ch, depth + 1);
    }
}

/* Make `st` this surface's current state. */
static void apply_state(aurwl_win *w, surf_state *st, int depth)
{
    aurwl *c = w->c;
    surf_state *cur = &w->current;

    int restyled = cur->scale != st->scale || cur->transform != st->transform ||
                   cur->vp_dst_w != st->vp_dst_w || cur->vp_dst_h != st->vp_dst_h ||
                   cur->vp_src_set != st->vp_src_set ||
                   (st->vp_src_set && (cur->vp_sx != st->vp_sx || cur->vp_sy != st->vp_sy ||
                                       cur->vp_sw != st->vp_sw || cur->vp_sh != st->vp_sh));

    /* Carry damage into current before take_buffer reads it, since the
     * copy is bounded by the damage that arrived with this buffer. */
    cur->has_damage = st->has_damage;
    cur->dmg_x0 = st->dmg_x0; cur->dmg_y0 = st->dmg_y0;
    cur->dmg_x1 = st->dmg_x1; cur->dmg_y1 = st->dmg_y1;
    cur->scale = st->scale; cur->transform = st->transform;
    cur->vp_dst_w = st->vp_dst_w; cur->vp_dst_h = st->vp_dst_h;
    cur->vp_src_set = st->vp_src_set;
    cur->vp_sx = st->vp_sx; cur->vp_sy = st->vp_sy; cur->vp_sw = st->vp_sw; cur->vp_sh = st->vp_sh;
    cur->in_kind = st->in_kind;
    cur->in_x0 = st->in_x0; cur->in_y0 = st->in_y0; cur->in_x1 = st->in_x1; cur->in_y1 = st->in_y1;

    w->off_x = (int)clamp64((int64_t)w->off_x + st->dx, -(1 << 20), 1 << 20);
    w->off_y = (int)clamp64((int64_t)w->off_y + st->dy, -(1 << 20), 1 << 20);
    if (st->attached) {
        if (st->buffer) {
            irect cp; int resized;
            int was_mapped = w->mapped;
            take_buffer(w, st->buffer, &cp, &resized);
            c->damage_seq++;
            wl_buffer_send_release(st->buffer);
            if (!w->mapped) surface_map(w);
            if (resized || !was_mapped || restyled) dirty_surface(w, NULL);
            else if (cp.x1 > cp.x0 && cp.y1 > cp.y0) dirty_surface(w, &cp);
        } else {
            /* attach(NULL) is how a client unmaps itself. */
            if (w->mapped) surface_unmap(w);
            if (w->store) { surface_free(w->store); w->store = NULL; }
            w->cw = w->ch = 0;
            dirty_all(w);
            c->damage_seq++;
        }
    } else if (restyled) {
        dirty_surface(w, NULL);
        c->damage_seq++;
    }

    children_apply(w, depth);
}

/* Not an infinite recursion -- the cache is emptied before its state is
 * applied and nothing inside refills it, so a ring terminates. What is
 * unbounded is the DEPTH: a chain of a hundred thousand synchronized
 * subsurfaces, each committed once, recurses that far on one commit and
 * walks off the end of the stack. That is affordable for a client to
 * build, so the depth is capped. */
static void apply_cached(aurwl_win *w, int depth)
{
    if (depth > PARENT_MAX) return;
    surf_state st = w->cached;
    w->has_cache = 0;
    memset(&w->cached, 0, sizeof w->cached);
    cwatch_buffer(w, NULL);
    apply_state(w, &st, depth);
}

/* A surface that has just stopped being synchronized (its own set_desync,
 * or its parent's) applies what it was holding -- and so does anything
 * below it that was only held because of it. */
static void flush_desync(aurwl_win *w, int depth)
{
    if (depth > PARENT_MAX) return;
    if (w->has_cache && !eff_sync(w)) { apply_cached(w, depth); return; }
    for (struct wl_list *n = w->sub_pend.next; n != &w->sub_pend; n = n->next) {
        if (n == &w->self_pend) continue;
        aurwl_win *ch = wl_container_of(n, ch, pend_link);
        flush_desync(ch, depth + 1);
    }
}

static void surf_commit(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl; aurwl_win *w = wl_resource_get_user_data(r);

    cache_pending(w);
    if (w->role == ROLE_SUBSURFACE && eff_sync(w)) return;   /* waits for the parent */

    /* xdg-shell's handshake: the first commit carries no buffer and
     * asks us what size to be. Answering it is not optional -- a client
     * that never receives a configure never draws, which presents as a
     * window that silently fails to appear. */
    int handshake = w->xdg_surface && !w->acked && !w->cached.buffer;
    apply_cached(w, 0);
    if (handshake) {
        if (w->xdg_toplevel) {
            struct wl_array states; wl_array_init(&states);
            xdg_toplevel_send_configure(w->xdg_toplevel, 0, 0, &states);
            wl_array_release(&states);
        }
        send_configure(w);
    }
}

static const struct wl_surface_interface surface_impl = {
    .destroy = noop_destroy, .attach = surf_attach, .damage = surf_damage,
    .frame = surf_frame, .set_opaque_region = surf_set_opaque,
    .set_input_region = surf_set_input, .commit = surf_commit,
    .set_buffer_transform = surf_set_transform, .set_buffer_scale = surf_set_scale,
    .damage_buffer = surf_damage_buffer, .offset = surf_offset,
};

/* ── surface lifecycle ──────────────────────────────────────────── */

static void surface_map(aurwl_win *w)
{
    /* Cursors and drag icons are not on any output the client cares
     * about; everything else -- including a subsurface, which toolkits
     * read their scale from independently of the window it belongs to --
     * is told. */
    if (w->role == ROLE_CURSOR || w->role == ROLE_NONE) { w->mapped = 1; return; }
    w->mapped = 1;
    /* The client needs to know which output it is on before it can pick
     * a scale factor; GTK will not finish its first layout without it. */
    FOR_EACH_RES(o, &w->c->outputs)
        if (wl_resource_get_client(o) == wl_resource_get_client(w->res))
            wl_surface_send_enter(w->res, o);
}

static void surface_unmap(aurwl_win *w)
{
    aurwl *c = w->c;
    w->mapped = 0;
    /* An unmapped xdg_surface goes back to needing the initial
     * handshake: the client commits with no buffer and waits for a
     * configure. `acked` stayed set across the unmap, so that second
     * handshake was skipped, no configure was ever sent, and the client
     * waited forever -- a window that is hidden and never comes back.
     * GTK dodges it by destroying the role object on hide; a client
     * that follows the documented lighter path does not. */
    w->acked = 0;

    /* The surface object is still alive -- the client only attached a
     * null buffer -- so it has no way to find out it lost focus unless
     * we say so. Without the keyboard leave it must assume its keys are
     * still down, and key repeat runs forever. */
    uint32_t ser = serial_of(c);
    if (c->focus == w) {
        struct wl_client *cl = win_client(w);
        FOR_EACH_RES(k, &c->keyboards)
            if (wl_resource_get_client(k) == cl) wl_keyboard_send_leave(k, ser, w->res);
        c->focus = NULL;
        if (textinput_focus) textinput_focus(NULL, NULL);
    }
    /* The pointer may be on this very surface, or on a subsurface of it
     * (this is the window the shell knew about). Either way it has to be
     * told, or the client goes on thinking it is hovering a button. */
    if (c->ptr_surf == w || c->ptr_focus == w) {
        ptr_leave(c);
        if (c->ptr_focus == w) c->ptr_focus = NULL;
    }
}

static void surface_free_res(struct wl_resource *r)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    if (!w) return;
    aurwl *c = w->c;
    xw_surface_gone(w);
    if (c->drag.active) {
        if (c->drag.origin == w) drag_cancel(c);     /* nothing to drag from */
        else if (c->drag.target == w) drag_leave(c);
        if (c->drag.icon == w) c->drag.icon = NULL;
    }
    if (c->focus == w)     c->focus = NULL;
    /* Whatever the pointer was asked to look like was asked by the program
     * that is going: it is not left behind (the fuzzer: a program that
     * named a shape and quit left the shape set). */
    if (c->cursor.surf == w || c->ptr_surf == w || c->ptr_focus == w) cursor_reset(c);
    if (c->ptr_surf == w)  c->ptr_surf = NULL;
    if (c->ptr_focus == w) { c->ptr_focus = NULL; c->ptr_surf = NULL; }

    /* When a client dies, libwayland destroys its resources in an order
     * we do not choose -- and it destroyed the wl_surface BEFORE the
     * xdg_surface built on it. The xdg_surface's destructor then wrote
     * through this freed pointer.
     *
     * That is not a corner case. It is what happens every single time
     * an application crashes or is killed, and it took the compositor
     * down with it -- so one misbehaving program closed every other
     * window on the machine. Found by SIGKILLing a terminal under
     * AddressSanitizer; it is not reachable by any polite client, which
     * is exactly why it survived the polite tests.
     *
     * Every resource that points back here is disarmed before the
     * memory goes. Their destructors already check for NULL, so they
     * become no-ops in whatever order libwayland runs them. */
    struct wl_resource *dependents[] = {
        w->xdg_toplevel, w->xdg_popup, w->xdg_surface,
        w->subsurface, w->decoration, w->viewport,
    };
    for (size_t i = 0; i < sizeof dependents / sizeof dependents[0]; i++)
        if (dependents[i]) wl_resource_set_user_data(dependents[i], NULL);

    /* A parent may outlive its children or the other way round; both
     * directions have to be unhooked or the next walk of either list
     * follows a pointer into freed memory. */
    aurwl_win *ch, *tmp;
    wl_list_for_each_safe(ch, tmp, &w->children, child_link) {
        wl_list_remove(&ch->child_link);
        wl_list_init(&ch->child_link);
        ch->parent = NULL;
    }
    /* The subsurfaces stacked on this one are orphaned: still the
     * client's objects, but part of no window any more. */
    for (struct wl_list *n = w->sub_pend.next, *nx; n != &w->sub_pend; n = nx) {
        nx = n->next;
        if (n == &w->self_pend) continue;
        aurwl_win *sc = wl_container_of(n, sc, pend_link);
        unlink_init(&sc->pend_link);
        unlink_init(&sc->cur_link);
        sc->parent = NULL;
    }
    /* And if this one was itself a subsurface, the window it was part of
     * must stop showing it. Before the link is cut: finding that window
     * is what the link is for. */
    if (w->role == ROLE_SUBSURFACE && w->parent) dirty_all(w);
    unlink_init(&w->pend_link);
    unlink_init(&w->cur_link);
    /* child_link is wl_list_init()'d at creation, so it is always a
     * valid node -- removing one that was never inserted just re-points
     * it at itself. */
    wl_list_remove(&w->child_link);
    w->parent = NULL;

    watch_buffer(w, NULL);
    cwatch_buffer(w, NULL);
    if (w->flat) surface_free(w->flat);

    /* An inhibitor outlives its surface until the client destroys it; it
     * must stop pointing at it. (It then inhibits nothing: the window it
     * was for is gone.) */
    struct idle_inh *ih;
    wl_list_for_each(ih, &c->inhibitors, link)
        if (ih->w == w) ih->w = NULL;

    struct wl_resource *cb, *cbt;
    wl_resource_for_each_safe(cb, cbt, &w->frame_cbs) wl_resource_destroy(cb);

    wl_list_remove(&w->link);
    if (w->store) surface_free(w->store);
    free(w);
}

/* ── wl_compositor ──────────────────────────────────────────────── */

/* NOT count_for_client(): c->surfaces is a list of aurwl_win, linked
 * by w->link, not a list of wl_resource links. Walking it as the
 * latter reads whatever happens to sit at that offset -- which is the
 * kind of mistake that compiles, runs, and gives a plausible number. */
static int too_many_surfaces(struct wl_client *cl, struct wl_resource *r, aurwl *c)
{
    int n = 0;
    aurwl_win *w;
    wl_list_for_each(w, &c->surfaces, link)
        if (w->res && wl_resource_get_client(w->res) == cl) n++;
    if (n < MAX_SURFACES) return 0;
    wl_resource_post_error(r, 0, "no more than %d windows per program",
                           MAX_SURFACES);
    return 1;
}

static void comp_create_surface(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    aurwl *c = wl_resource_get_user_data(r);
    /* A browser with many tabs and many menus is a normal client, so
     * the ceiling is high -- but there IS one. There was none: a
     * hundred thousand surfaces from one program took the compositor
     * from 2.6 MB to 82 MB without a word, and every one of them is
     * walked by the shell once per frame and again for every pointer
     * event. */
    if (too_many_surfaces(cl, r, c)) return;
    aurwl_win *w = calloc(1, sizeof *w);
    if (!w) { wl_client_post_no_memory(cl); return; }
    w->c = c;
    w->id = ++c->next_id;
    w->pending.scale = w->current.scale = 1;
    wl_list_init(&w->frame_cbs);
    wl_list_init(&w->children);
    wl_list_init(&w->child_link);
    /* Every surface is the one entry in its own stack until something is
     * stacked on it. */
    wl_list_init(&w->sub_pend);  wl_list_init(&w->sub_cur);
    wl_list_init(&w->pend_link); wl_list_init(&w->cur_link);
    wl_list_insert(&w->sub_pend, &w->self_pend);
    wl_list_insert(&w->sub_cur,  &w->self_cur);

    w->res = wl_resource_create(cl, &wl_surface_interface, wl_resource_get_version(r), id);
    if (!w->res) { free(w); wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(w->res, &surface_impl, w, surface_free_res);
    wl_list_insert(c->surfaces.prev, &w->link);
    xw_surface_created(w);        /* an X window may have been waiting for exactly this */
}

static void comp_create_region(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    (void)r;
    aurwl_region *g = calloc(1, sizeof *g);
    if (!g) { wl_client_post_no_memory(cl); return; }
    struct wl_resource *res = wl_resource_create(cl, &wl_region_interface, 1, id);
    if (!res) { free(g); wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(res, &region_impl, g, region_free);
}

static const struct wl_compositor_interface compositor_impl = {
    .create_surface = comp_create_surface, .create_region = comp_create_region,
};

static void bind_compositor(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &wl_compositor_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &compositor_impl, data, NULL);
}

/* ── wl_subsurface ──────────────────────────────────────────────── */

/* The surface stops being part of its parent's window, at once and
 * without waiting for the parent to commit (wayland.xml: "the wl_surface
 * is unmapped immediately"). Its contents go with it: a surface that is
 * made a subsurface again starts with nothing. */
static void sub_detach(aurwl_win *w)
{
    if (w->parent) {
        dirty_all(w);                      /* while the chain to the window is intact */
        unlink_init(&w->pend_link);
        unlink_init(&w->cur_link);
        w->parent = NULL;
        w->c->damage_seq++;
    }
    drop_cache(w);
    watch_buffer(w, NULL);
    w->pending.attached = 0; w->pending.buffer = NULL; w->pending.has_damage = 0;
    if (w->mapped) surface_unmap(w);
    if (w->store) { surface_free(w->store); w->store = NULL; }
    w->cw = w->ch = 0;
}

static void sub_destroy(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl;
    wl_resource_destroy(r);
}
static void subsurface_gone(struct wl_resource *r)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    if (!w || w->subsurface != r) return;
    w->subsurface = NULL;
    sub_detach(w);
}

/* Position and stacking are state of the PARENT: they are recorded now
 * and take effect when the parent's own state is applied
 * (children_apply). A client that moves a subsurface and then commits
 * only the subsurface sees nothing move -- which is the protocol, and the
 * reason a video overlay and the page around it never disagree. */
static void sub_set_position(struct wl_client *cl, struct wl_resource *r, int32_t x, int32_t y)
{
    (void)cl; aurwl_win *w = wl_resource_get_user_data(r);
    if (!w) return;
    w->sub_px = clampi(x, -MAX_DIM, MAX_DIM);
    w->sub_py = clampi(y, -MAX_DIM, MAX_DIM);
}

static void sub_place(struct wl_resource *r, aurwl_win *w,
                     struct wl_resource *sib_res, int above)
{
    if (!w || !w->parent) return;
    aurwl_win *p = w->parent;
    aurwl_win *sib = sib_res ? wl_resource_get_user_data(sib_res) : NULL;

    /* The sibling must be an actual sibling, or the parent. A client
     * passing its OWN surface was accepted, and then:
     *
     *   place_below -- wl_list_remove() zeroes both links, so
     *     sib->child_link.prev is read as NULL and wl_list_insert()
     *     dereferences address 8. Four requests, and the compositor is
     *     dead along with every window on the machine.
     *
     *   place_above -- no crash, but the surface ends up in a self-ring
     *     while w->parent still names its parent. It is then invisible
     *     to the parent's walk in surface_free_res(), so when the parent
     *     goes, this child keeps a pointer to freed memory -- which the
     *     shell dereferences every frame in session_paint_popups().
     *
     * Two sources of truth, `parent` and the stack, and this was the
     * one function that could pull them apart. */
    if (sib == w) {
        wl_resource_post_error(r, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                               "a subsurface cannot be its own sibling");
        return;
    }
    struct wl_list *at;
    if (sib && sib == p)                                         at = &p->self_pend;
    else if (sib && sib->role == ROLE_SUBSURFACE && sib->parent == p) at = &sib->pend_link;
    else {
        wl_resource_post_error(r, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                               "not a sibling of this subsurface");
        return;
    }
    /* The parent is IN the stack, so "below the parent" is a place like
     * any other. Cut first, then read where to put it: the neighbour may
     * have been this very node. */
    unlink_init(&w->pend_link);
    wl_list_insert(above ? at : at->prev, &w->pend_link);
}
static void sub_place_above(struct wl_client *cl, struct wl_resource *r, struct wl_resource *s)
{ (void)cl; sub_place(r, wl_resource_get_user_data(r), s, 1); }
static void sub_place_below(struct wl_client *cl, struct wl_resource *r, struct wl_resource *s)
{ (void)cl; sub_place(r, wl_resource_get_user_data(r), s, 0); }
static void sub_set_sync(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r); if (w) w->sub_sync = 1; }
static void sub_set_desync(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl; aurwl_win *w = wl_resource_get_user_data(r);
    if (!w) return;
    w->sub_sync = 0;
    /* Becoming independent applies what was being held -- but only if no
     * ancestor is still holding THIS one: then it waits for them. */
    flush_desync(w, 0);
}
static const struct wl_subsurface_interface subsurface_impl = {
    .destroy = sub_destroy, .set_position = sub_set_position,
    .place_above = sub_place_above, .place_below = sub_place_below,
    .set_sync = sub_set_sync, .set_desync = sub_set_desync,
};

/* Would making `cand` the parent of `w` close a loop?
 *
 * `parent` is shared between subsurfaces and popups, and only the
 * subsurface path ever checked -- so two popups could name each other
 * and the subsurface check would then walk that ring forever, spinning
 * at 100% inside a client request with the event loop never reached
 * again. A hang in the process that owns the display is as fatal as a
 * crash, and harder to describe to the person it happens to.
 *
 * The step bound is belt and braces: it makes the walk terminate even
 * if some future path builds a ring this cannot see. */
static int parent_loop(aurwl_win *cand, aurwl_win *w)
{
    int steps = 0;
    for (aurwl_win *a = cand; a; a = a->parent) {
        if (a == w) return 1;
        if (++steps > PARENT_MAX) return 1;
    }
    return 0;
}

static void subcomp_get(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                        struct wl_resource *surf, struct wl_resource *parent)
{
    (void)r;
    aurwl_win *w = surf ? wl_resource_get_user_data(surf) : NULL;
    aurwl_win *p = parent ? wl_resource_get_user_data(parent) : NULL;
    if (!w || !p || w == p) {
        wl_resource_post_error(r, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "a surface cannot be its own subsurface");
        return;
    }
    if (parent_loop(p, w)) {
        wl_resource_post_error(r, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "subsurface loop");
        return;
    }
    if (w->subsurface || w->xdg_toplevel || w->xdg_popup) {
        wl_resource_post_error(r, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "this surface already has a role");
        return;
    }
    struct wl_resource *res = wl_resource_create(cl, &wl_subsurface_interface, 1, id);
    if (!res) { wl_client_post_no_memory(cl); return; }
    if (!take_role(w, ROLE_SUBSURFACE, r, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE)) {
        wl_resource_destroy(res);
        return;
    }
    wl_resource_set_implementation(res, &subsurface_impl, w, subsurface_gone);
    w->subsurface = res;
    w->sub_sync = 1;                 /* the protocol's default */
    w->sub_x = w->sub_y = w->sub_px = w->sub_py = 0;
    unlink_init(&w->pend_link);
    unlink_init(&w->cur_link);
    w->parent = p;
    /* On top of its siblings, and at (0,0). Entered in both stacks at
     * once: it has no position or order to be late with, and a child
     * that commits before its parent does should still be drawn. */
    wl_list_insert(p->sub_pend.prev, &w->pend_link);
    wl_list_insert(p->sub_cur.prev,  &w->cur_link);
}
static const struct wl_subcompositor_interface subcompositor_impl = {
    .destroy = noop_destroy, .get_subsurface = subcomp_get,
};
static void bind_subcompositor(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &wl_subcompositor_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &subcompositor_impl, data, NULL);
}

/* ── wp_viewporter ──────────────────────────────────────────────── */

static void vper_get(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                     struct wl_resource *surf)
{
    (void)r;
    struct wl_resource *res = wl_resource_create(cl, &wp_viewport_interface, 1, id);
    if (!res) { wl_client_post_no_memory(cl); return; }
    aurwl_win *w = surf ? wl_resource_get_user_data(surf) : NULL;
    if (w && w->viewport) {
        wl_resource_destroy(res);
        wl_resource_post_error(r, WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS,
                               "this surface already has a viewport");
        return;
    }
    wl_resource_set_implementation(res, &viewport_impl, w, viewport_gone);
    if (w) w->viewport = res;
}
static const struct wp_viewporter_interface viewporter_impl = {
    .destroy = noop_destroy, .get_viewport = vper_get,
};
static void bind_viewporter(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &wp_viewporter_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &viewporter_impl, data, NULL);
}

/* ── wl_output ──────────────────────────────────────────────────── */

static void output_send(aurwl *c, struct wl_resource *r)
{
    int ver = wl_resource_get_version(r);
    /* Physical size 0x0 means "unknown", which is honest: we get the
     * mode from KMS but not reliably the panel's millimetres, and a
     * wrong number makes a toolkit choose a wrong DPI. */
    wl_output_send_geometry(r, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN,
                            "AurOS", "Display", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                        c->ow, c->oh, c->refresh_mhz);
    if (ver >= WL_OUTPUT_SCALE_SINCE_VERSION) wl_output_send_scale(r, 1);
    /* name and description are sent once per object and never again --
     * this function also runs on every mode change and VT return. */
    if (ver >= WL_OUTPUT_DONE_SINCE_VERSION)  wl_output_send_done(r);
}
static const struct wl_output_interface output_impl = { .release = noop_destroy };
static void bind_output(struct wl_client *cl, void *data, uint32_t ver_, uint32_t id)
{
    aurwl *c = data;
    struct wl_resource *r = wl_resource_create(cl, &wl_output_interface, (int)ver_, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &output_impl, c, res_unlink);
    wl_list_insert(&c->outputs, wl_resource_get_link(r));
    int ver = (int)ver_;
    if (ver >= WL_OUTPUT_NAME_SINCE_VERSION)        wl_output_send_name(r, "AUR-1");
    if (ver >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION) wl_output_send_description(r, "AurOS display");
    output_send(c, r);
}

/* zxdg_output_manager_v1 predates wl_output's name/description events
 * and GTK still asks for it; without it the toolkit waits for an output
 * description that never arrives before settling its first frame. */
static const struct zxdg_output_v1_interface xdg_output_impl = { .destroy = noop_destroy };
/* Send an xdg_output's properties and close the sequence.
 *
 * From version 3 the closing event is wl_output.done, not
 * zxdg_output_v1.done -- and a toolkit that follows that waits for the
 * wl_output.done which used to be sent before get_xdg_output was ever
 * called, so it never applied the logical size and reported the monitor
 * as unknown. The name and description guard was also inverted: they
 * exist since version 2, and were being sent to version 1 clients
 * (where libwayland calls a listener slot that does not exist and
 * aborts the client) and withheld from the versions that want them. */
static void xdg_output_send(aurwl *c, struct wl_resource *res)
{
    int ver = wl_resource_get_version(res);
    zxdg_output_v1_send_logical_position(res, 0, 0);
    zxdg_output_v1_send_logical_size(res, c->ow, c->oh);
    if (ver >= 2 && ver < 3) {
        zxdg_output_v1_send_name(res, "AUR-1");
        zxdg_output_v1_send_description(res, "AurOS display");
    }
    if (ver < 3) {
        zxdg_output_v1_send_done(res);
    } else {
        struct wl_client *cl = wl_resource_get_client(res);
        FOR_EACH_RES(o, &c->outputs)
            if (wl_resource_get_client(o) == cl &&
                wl_resource_get_version(o) >= WL_OUTPUT_DONE_SINCE_VERSION)
                wl_output_send_done(o);
    }
}

static void xdgout_get(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                       struct wl_resource *out)
{
    aurwl *c = wl_resource_get_user_data(r);
    (void)out;
    struct wl_resource *res = wl_resource_create(cl, &zxdg_output_v1_interface,
                                                 wl_resource_get_version(r), id);
    if (!res) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(res, &xdg_output_impl, c, res_unlink);
    wl_list_insert(&c->xdg_outputs, wl_resource_get_link(res));
    xdg_output_send(c, res);
}
static const struct zxdg_output_manager_v1_interface xdg_output_mgr_impl = {
    .destroy = noop_destroy, .get_xdg_output = xdgout_get,
};
static void bind_xdg_output(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &zxdg_output_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &xdg_output_mgr_impl, data, NULL);
}

/* ── wl_seat ────────────────────────────────────────────────────── */

static const struct wl_pointer_interface pointer_impl_s;

/* WHO MAY CHANGE THE POINTER. Only the program the pointer is on, and only
 * quoting the serial of the enter that put it there -- otherwise a program
 * in the background could turn the pointer into anything, or hide it,
 * whenever it liked, and she would be left with a pointer that lies. */
static int cursor_allowed(aurwl *c, struct wl_client *cl, uint32_t serial)
{
    return c->ptr_surf && win_client(c->ptr_surf) == cl && serial == c->ptr_enter_serial;
}

static void cursor_reset(aurwl *c)
{
    if (c->cursor.kind != AURWL_CURSOR_DEFAULT) c->damage_seq++;     /* the shell repaints it */
    memset(&c->cursor, 0, sizeof c->cursor);
}

static void ptr_set_cursor(struct wl_client *cl, struct wl_resource *r, uint32_t serial,
                           struct wl_resource *surf, int32_t hx, int32_t hy)
{
    aurwl *c = wl_resource_get_user_data(r);
    aurwl_win *w = surf ? wl_resource_get_user_data(surf) : NULL;
    /* wayland.xml, wl_pointer.set_cursor: giving a surface the cursor
     * role when it already has another is wl_pointer.role. This used
     * to overwrite it unconditionally, so a mapped focused toplevel
     * could be turned into a cursor -- dropping it out of the window
     * list with no unmap, no leave event, and the shell's slot for it
     * still standing. */
    if (w && !take_role(w, ROLE_CURSOR, r, WL_POINTER_ERROR_ROLE)) return;
    if (!cursor_allowed(c, cl, serial)) return;
    if (!w) {                                   /* a null surface: hide the pointer */
        memset(&c->cursor, 0, sizeof c->cursor);
        c->cursor.kind = AURWL_CURSOR_HIDDEN;
    } else {
        memset(&c->cursor, 0, sizeof c->cursor);
        c->cursor.kind = AURWL_CURSOR_IMAGE;
        c->cursor.surf = w;
        c->cursor.hx = clampi(hx, -MAX_DIM, MAX_DIM);
        c->cursor.hy = clampi(hy, -MAX_DIM, MAX_DIM);
        c->cursor.ox = w->off_x; c->cursor.oy = w->off_y;
    }
    c->damage_seq++;
}
static const struct wl_pointer_interface pointer_impl_s = {
    .set_cursor = ptr_set_cursor, .release = noop_destroy,
};
static const struct wl_keyboard_interface keyboard_impl_s = { .release = noop_destroy };
static const struct wl_touch_interface    touch_impl_s    = { .release = noop_destroy };

static void seat_get_pointer(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    aurwl *c = wl_resource_get_user_data(r);
    if (too_many(cl, r, &c->pointers, MAX_SEAT_OBJ, "pointers")) return;
    struct wl_resource *p = wl_resource_create(cl, &wl_pointer_interface,
                                               wl_resource_get_version(r), id);
    if (!p) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(p, &pointer_impl_s, c, res_unlink);
    wl_list_insert(&c->pointers, wl_resource_get_link(p));
}
static void seat_get_keyboard(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    aurwl *c = wl_resource_get_user_data(r);
    if (too_many(cl, r, &c->keyboards, MAX_SEAT_OBJ, "keyboards")) return;
    struct wl_resource *k = wl_resource_create(cl, &wl_keyboard_interface,
                                               wl_resource_get_version(r), id);
    if (!k) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(k, &keyboard_impl_s, c, res_unlink);
    wl_list_insert(&c->keyboards, wl_resource_get_link(k));
    /* An fd, always. libwayland dups every fd argument as it marshals,
     * and dup(-1) fails -- which drops the message and flags the client
     * errored. So a machine with no xkb data did not degrade to "no key
     * translation" as intended: it disconnected every client at
     * get_keyboard, and nothing could open a window at all. */
    wl_keyboard_send_keymap(k,
        c->keymap_size ? WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1
                       : WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP,
        c->keymap_fd, (uint32_t)c->keymap_size);
    if (wl_resource_get_version(k) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
        wl_keyboard_send_repeat_info(k, 25, 400);
}
static void seat_get_touch(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    aurwl *c = wl_resource_get_user_data(r);
    if (too_many(cl, r, &c->touches, MAX_SEAT_OBJ, "touch devices")) return;
    struct wl_resource *t = wl_resource_create(cl, &wl_touch_interface,
                                               wl_resource_get_version(r), id);
    if (!t) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(t, &touch_impl_s, c, res_unlink);
    wl_list_insert(&c->touches, wl_resource_get_link(t));
}
static const struct wl_seat_interface seat_impl = {
    .get_pointer = seat_get_pointer, .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch, .release = noop_destroy,
};
static void bind_seat(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    aurwl *c = data;
    struct wl_resource *r = wl_resource_create(cl, &wl_seat_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &seat_impl, c, res_unlink);
    wl_list_insert(&c->seats, wl_resource_get_link(r));
    /* Touch is advertised unconditionally: these machines are often
     * convertibles, hotplug exists, and a client that learns about a
     * capability late handles it far worse than one that always had it. */
    wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_POINTER |
                                 WL_SEAT_CAPABILITY_KEYBOARD |
                                 WL_SEAT_CAPABILITY_TOUCH);
    if (wl_resource_get_version(r) >= WL_SEAT_NAME_SINCE_VERSION)
        wl_seat_send_name(r, "seat0");
}

/* ── clipboard ──────────────────────────────────────────────────── */
/* Copy and paste is not a nicety on a machine whose purpose is a web
 * browser. The mechanism is a relay: the owning client keeps the data,
 * we keep only the offer, and when someone pastes we hand the reader's
 * pipe to the owner and step out of the way. */

typedef struct {
    aurwl              *c;
    struct wl_resource *res;
    char                mimes[16][64];
    int                 n_mimes;
    uint32_t            actions;      /* wl_data_device_manager dnd actions it allows */
    int                 has_actions;
    struct wl_list      link;
} data_source;

/* The "source" of a clipboard offer whose data is in an X program (xclip.inc): not a
 * program's wl_data_source, so nothing may treat the pointer as a resource.
 * ponytail: xsel_c is one compositor per process, which is what this is. */
static char xsel_marker;
static aurwl *xsel_c;
#define XSEL ((struct wl_resource *)&xsel_marker)

static void dsrc_offer(struct wl_client *cl, struct wl_resource *r, const char *mime)
{
    (void)cl; data_source *s = wl_resource_get_user_data(r);
    if (!s || s->n_mimes >= 16) return;
    snprintf(s->mimes[s->n_mimes], sizeof s->mimes[0], "%s", mime);
    s->n_mimes++;
}
static void dsrc_set_actions(struct wl_client *cl, struct wl_resource *r, uint32_t a)
{
    (void)cl; data_source *s = wl_resource_get_user_data(r);
    if (!s) return;
    if (a & ~7u) {
        wl_resource_post_error(r, WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                               "action mask %u has bits that are not actions", a);
        return;
    }
    s->actions = a; s->has_actions = 1;
}

static void drag_source_gone(aurwl *c, struct wl_resource *src);
static const struct wl_data_source_interface data_source_impl = {
    .offer = dsrc_offer, .destroy = noop_destroy, .set_actions = dsrc_set_actions,
};
static void dsrc_gone(struct wl_resource *r)
{
    data_source *s = wl_resource_get_user_data(r);
    if (!s) return;
    int was_selection = (s->c->selection == r);
    if (was_selection) s->c->selection = NULL;
    drag_source_gone(s->c, r);
    /* Any offer still pointing at this source now points at freed
     * memory. A paste from a menu that was open when the copying
     * application quit would have written through it -- which is a
     * perfectly ordinary thing for a person to do. */
    struct wl_resource *o, *ot;
    wl_resource_for_each_safe(o, ot, &s->c->offers)
        if (wl_resource_get_user_data(o) == r) offer_retire(o);
    /* And say so -- but ONLY if what died was the clipboard, and only
     * to the client that is entitled to hear about it.
     *
     * This used to fire on any source at all, to every device on the
     * machine. So destroying a superseded source -- which is the
     * ordinary end of a copy that has been replaced -- told every
     * program the clipboard was empty while c->selection still named a
     * live one. It also broadcast, which is the very thing the
     * function below it is written not to do, for a reason it spells
     * out at length. */
    if (was_selection) {
        struct wl_client *t = win_client(s->c->focus);
        struct wl_resource *dev;
        wl_resource_for_each(dev, &s->c->devices)
            if (!t || wl_resource_get_client(dev) == t)
                wl_data_device_send_selection(dev, NULL);
    }
    aurwl *cc = s->c;
    free(s);
    if (was_selection) xw_selection_changed(cc);               /* X programs lose it too */
}

static void drag_actions_changed(aurwl *c);

/* Only the offer handed to the drag's current target means anything here;
 * a clipboard offer, or an old one, is ignored as it always was. */
static int is_drag_offer(aurwl *c, struct wl_resource *r)
{ return c->drag.active && c->drag.offer == r; }

static void doffer_accept(struct wl_client *cl, struct wl_resource *r, uint32_t serial, const char *m)
{
    (void)cl;
    /* The compositor is reached through the offer's source: offers keep no
     * back-pointer, and the drag state lives in the compositor. */
    struct wl_resource *src = wl_resource_get_user_data(r);
    if (src == XSEL) return;                                   /* an X program's clipboard: no drag */
    data_source *ds = src ? wl_resource_get_user_data(src) : NULL;
    aurwl *c = ds ? ds->c : NULL;
    if (!c || !is_drag_offer(c, r) || serial != c->drag.serial) return;
    if (m && *m) {
        /* Only what the source offered can be accepted. */
        int ok = 0;
        for (int i = 0; i < ds->n_mimes; i++) if (!strcmp(ds->mimes[i], m)) ok = 1;
        if (!ok) m = NULL;
    }
    snprintf(c->drag.mime, sizeof c->drag.mime, "%s", m ? m : "");
    wl_data_source_send_target(c->drag.source, m && *m ? c->drag.mime : NULL);
}
static void doffer_receive(struct wl_client *cl, struct wl_resource *r, const char *mime, int32_t fd)
{
    (void)cl;
    /* NULL means the application that copied has since quit. The read
     * end still has to be closed or the paste hangs forever. */
    struct wl_resource *src = wl_resource_get_user_data(r);
    if (src == XSEL) { xw_paste(xsel_c, mime, fd); return; }   /* the data is in an X program; xw_paste closes fd */
    if (src) wl_data_source_send_send(src, mime, fd);
    /* Our end of the pipe must go, or the reader never sees EOF and a
     * paste hangs forever on a mime type the owner declines to write. */
    close(fd);
}
static void drag_finish(aurwl *c);
static void doffer_finish(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl;
    struct wl_resource *src = wl_resource_get_user_data(r);
    if (src == XSEL) return;
    data_source *ds = src ? wl_resource_get_user_data(src) : NULL;
    if (!ds || !is_drag_offer(ds->c, r)) return;
    if (!ds->c->drag.dropped) {
        wl_resource_post_error(r, WL_DATA_OFFER_ERROR_INVALID_FINISH,
                               "finish before the drop");
        return;
    }
    drag_finish(ds->c);
}
static void doffer_set_actions(struct wl_client *cl, struct wl_resource *r, uint32_t a, uint32_t b)
{
    (void)cl;
    if (a & ~7u) {
        wl_resource_post_error(r, WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
                               "action mask %u has bits that are not actions", a);
        return;
    }
    if (b != 0 && b != 1 && b != 2 && b != 4) {
        wl_resource_post_error(r, WL_DATA_OFFER_ERROR_INVALID_ACTION,
                               "%u is not one action", b);
        return;
    }
    struct wl_resource *src = wl_resource_get_user_data(r);
    if (src == XSEL) return;
    data_source *ds = src ? wl_resource_get_user_data(src) : NULL;
    if (!ds || !is_drag_offer(ds->c, r)) return;
    ds->c->drag.dst_actions = a;
    ds->c->drag.dst_pref = b;
    drag_actions_changed(ds->c);
}
static const struct wl_data_offer_interface data_offer_impl = {
    .accept = doffer_accept, .receive = doffer_receive, .destroy = noop_destroy,
    .finish = doffer_finish, .set_actions = doffer_set_actions,
};

/* Offer the current selection to one data_device. */
static void send_selection_to(aurwl *c, struct wl_resource *dev)
{
    if (!c->selection && c->xsel_active) {                      /* an X program has it: offered as if we were the source */
        struct wl_client *xcl = wl_resource_get_client(dev);
        struct wl_resource *xo, *xot;
        wl_resource_for_each_safe(xo, xot, &c->offers)
            if (wl_resource_get_client(xo) == xcl) offer_retire(xo);
        struct wl_resource *xoffer = wl_resource_create(xcl, &wl_data_offer_interface, wl_resource_get_version(dev), 0);
        if (!xoffer) return;
        xsel_c = c;
        wl_resource_set_implementation(xoffer, &data_offer_impl, XSEL, res_unlink);
        wl_list_insert(&c->offers, wl_resource_get_link(xoffer));
        wl_data_device_send_data_offer(dev, xoffer);
        for (int i = 0; i < c->xsel_n; i++) wl_data_offer_send_offer(xoffer, c->xsel_mimes[i]);
        wl_data_device_send_selection(dev, xoffer);
        return;
    }
    if (!c->selection) { wl_data_device_send_selection(dev, NULL); return; }
    data_source *s = wl_resource_get_user_data(c->selection);
    if (!s) { wl_data_device_send_selection(dev, NULL); return; }
    struct wl_client *cl = wl_resource_get_client(dev);
    /* One live offer per client, not one per focus change. This is
     * called every time keyboard focus arrives, and nothing here
     * retired the last one -- so alt-tabbing between two windows of
     * one program minted a fresh wl_data_offer, on both sides, every
     * time, for ever. The comment on set_selection said the leak was
     * fixed; it was fixed for set_selection and not for this. */
    struct wl_resource *o, *ot;
    wl_resource_for_each_safe(o, ot, &c->offers)
        if (wl_resource_get_client(o) == cl) offer_retire(o);

    struct wl_resource *offer = wl_resource_create(cl, &wl_data_offer_interface,
                                                   wl_resource_get_version(dev), 0);
    if (!offer) return;
    wl_resource_set_implementation(offer, &data_offer_impl, c->selection, res_unlink);
    wl_list_insert(&c->offers, wl_resource_get_link(offer));
    wl_data_device_send_data_offer(dev, offer);
    for (int i = 0; i < s->n_mimes; i++) wl_data_offer_send_offer(offer, s->mimes[i]);
    wl_data_device_send_selection(dev, offer);
}

/* ── drag and drop ──────────────────────────────────────────────── */
/* Moving a file from one folder to another, a photo into a message, a
 * link onto a tab: she does these with her hand, and every program that
 * lets her uses this protocol for it -- Thunar even inside one window.
 * start_drag used to answer "cancelled" to all of them.
 *
 * The compositor's part is a go-between. The pointer belongs to the drag
 * for as long as it lasts; whatever surface is under it is told enter,
 * motion and leave and handed an offer of what the source can provide; the
 * two ends agree between themselves (accept, set_actions, action, target)
 * on what would happen; on release the target is told to drop and the
 * source that it happened. Nothing is copied by us: the data goes through
 * the pipe the target gives the source, as for the clipboard. */

static int res_in_list(struct wl_list *list, struct wl_resource *res)
{
    struct wl_resource *o;
    if (!res) return 0;
    wl_resource_for_each(o, list) if (o == res) return 1;
    return 0;
}

/* The target stops being the target: it is told so, and the offer it was
 * holding is disarmed (it is the client's object, to destroy). */
static void drag_leave(aurwl *c)
{
    if (c->drag.target) {
        if (res_in_list(&c->devices, c->drag.dev)) wl_data_device_send_leave(c->drag.dev);
        if (c->drag.source) wl_data_source_send_target(c->drag.source, NULL);
        if (res_in_list(&c->offers, c->drag.offer)) offer_retire(c->drag.offer);
    }
    c->drag.target = NULL; c->drag.dev = NULL; c->drag.offer = NULL;
    c->drag.mime[0] = 0;
    c->drag.dst_actions = c->drag.dst_pref = c->drag.chosen = 0;
}

static void drag_end(aurwl *c)
{
    c->drag.active = 0;
    c->drag.source = NULL;
    c->drag.origin = c->drag.icon = NULL;
    c->drag.dropped = 0;
    drag_leave(c);
    /* The pointer is nobody's until the next motion says where it is; that
     * is what gives it back to whatever it is over. */
    c->ptr_surf = NULL;
}

static void drag_cancel(aurwl *c)
{
    if (!c->drag.active) return;
    struct wl_resource *src = c->drag.source;
    drag_leave(c);
    drag_end(c);
    if (src) wl_data_source_send_cancelled(src);
}

static void drag_finish(aurwl *c)
{
    struct wl_resource *src = c->drag.source;
    int ok = c->drag.chosen != 0;
    c->drag.target = NULL;           /* it was dropped on: leave is NOT sent after a drop */
    drag_end(c);
    if (src && ok && wl_resource_get_version(src) >= WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION)
        wl_data_source_send_dnd_finished(src);
}

/* The source went away mid-drag (its program quit): there is nothing left
 * to drop. No event to it -- it is gone. */
static void drag_source_gone(aurwl *c, struct wl_resource *src)
{
    if (c->drag.active && c->drag.source == src) {
        c->drag.source = NULL;
        drag_leave(c);
        drag_end(c);
    }
}

/* What would happen, from what each end allows (wayland.xml,
 * wl_data_offer.set_actions): the actions both allow, the receiver's
 * preference first, else copy, move, ask in that order. */
static void drag_actions_changed(aurwl *c)
{
    if (!c->drag.active || !c->drag.source) return;
    data_source *ds = wl_resource_get_user_data(c->drag.source);
    if (!ds) return;
    uint32_t src_a = ds->has_actions ? ds->actions : WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
    uint32_t common = src_a & c->drag.dst_actions, chosen = 0;
    if (c->drag.dst_pref && (common & c->drag.dst_pref)) chosen = c->drag.dst_pref;
    else if (common & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) chosen = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
    else if (common & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) chosen = WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
    else if (common & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)  chosen = WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
    if (chosen == c->drag.chosen) return;
    c->drag.chosen = chosen;
    if (wl_resource_get_version(c->drag.source) >= WL_DATA_SOURCE_ACTION_SINCE_VERSION)
        wl_data_source_send_action(c->drag.source, chosen);
    if (c->drag.offer && wl_resource_get_version(c->drag.offer) >= WL_DATA_OFFER_ACTION_SINCE_VERSION)
        wl_data_offer_send_action(c->drag.offer, chosen);
}

/* The pointer moved while dragging: `w` is the window under it (NULL: the
 * shell's own furniture) and (sx,sy) the point in its surface coordinates. */
static void drag_motion(aurwl *c, aurwl_win *w, int sx, int sy, uint32_t t)
{
    aurwl_win *hit = NULL;
    int lx = sx, ly = sy;
    if (w) {
        hit = pick(w, sx, sy, &lx, &ly, 0);
        if (!hit) { hit = w; lx = sx; ly = sy; }
    }
    struct wl_resource *dev = NULL;
    if (hit) {
        struct wl_client *cl = win_client(hit);
        struct wl_resource *d;
        wl_resource_for_each(d, &c->devices)
            if (wl_resource_get_client(d) == cl) { dev = d; break; }
    }
    aurwl_win *want = dev ? hit : NULL;
    if (want != c->drag.target) {
        drag_leave(c);
        data_source *ds = c->drag.source ? wl_resource_get_user_data(c->drag.source) : NULL;
        if (want && ds) {
            struct wl_client *cl = wl_resource_get_client(dev);
            int ver = wl_resource_get_version(dev);
            struct wl_resource *offer = wl_resource_create(cl, &wl_data_offer_interface, ver, 0);
            if (offer) {
                wl_resource_set_implementation(offer, &data_offer_impl, c->drag.source, res_unlink);
                wl_list_insert(&c->offers, wl_resource_get_link(offer));
                wl_data_device_send_data_offer(dev, offer);
                for (int i = 0; i < ds->n_mimes; i++) wl_data_offer_send_offer(offer, ds->mimes[i]);
                if (ver >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
                    wl_data_offer_send_source_actions(offer,
                        ds->has_actions ? ds->actions : WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
                uint32_t ser = serial_of(c);
                c->drag.target = want; c->drag.dev = dev; c->drag.offer = offer; c->drag.serial = ser;
                wl_data_device_send_enter(dev, ser, want->res,
                                          wl_fixed_from_int(lx), wl_fixed_from_int(ly), offer);
            }
        }
    }
    c->ptr_x = lx; c->ptr_y = ly;
    if (c->drag.target && res_in_list(&c->devices, c->drag.dev))
        wl_data_device_send_motion(c->drag.dev, t, wl_fixed_from_int(lx), wl_fixed_from_int(ly));
}

/* After a drop the target has the data and the pointer is free again; the
 * drag only lasts until the target says finish. If it never does -- it
 * destroyed the offer, or quit -- that is the end of it too. */
static void drag_validate(aurwl *c)
{
    if (c->drag.active && c->drag.dropped &&
        (!res_in_list(&c->offers, c->drag.offer) || !res_in_list(&c->devices, c->drag.dev))) {
        c->drag.target = NULL;
        drag_end(c);
    }
}

/* A button came up: the drag ends, one way or the other. */
static void drag_release(aurwl *c)
{
    if (!c->drag.active) return;
    int took = c->drag.target && c->drag.mime[0] && c->drag.chosen &&
               res_in_list(&c->devices, c->drag.dev) && res_in_list(&c->offers, c->drag.offer);
    if (!took) { drag_cancel(c); return; }
    struct wl_resource *src = c->drag.source, *offer = c->drag.offer;
    wl_data_device_send_drop(c->drag.dev);
    if (src && wl_resource_get_version(src) >= WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION)
        wl_data_source_send_dnd_drop_performed(src);
    c->drag.dropped = 1;
    /* From version 3 the target says it has finished with the data (finish)
     * and only then does the source hear dnd_finished; before that the
     * drop is the end of it. */
    if (wl_resource_get_version(offer) < WL_DATA_OFFER_FINISH_SINCE_VERSION) {
        c->drag.target = NULL;           /* leave is NOT sent after a drop */
        drag_end(c);
    }
}

static void ddev_start_drag(struct wl_client *cl, struct wl_resource *r,
                            struct wl_resource *src, struct wl_resource *origin,
                            struct wl_resource *icon, uint32_t serial)
{
    (void)cl;
    aurwl *c = wl_resource_get_user_data(r);
    aurwl_win *ow = origin ? wl_resource_get_user_data(origin) : NULL;
    aurwl_win *iw = icon ? wl_resource_get_user_data(icon) : NULL;
    if (iw && !take_role(iw, ROLE_CURSOR, r, WL_DATA_DEVICE_ERROR_ROLE)) return;

    /* A drag begins with a button held down on a surface of the program
     * that asks, and the serial it quotes is that button's. Anything else
     * -- a program that was not clicked, a stale serial, a second drag
     * while one runs -- is refused the way it always was: cancelled. */
    int ok = src && ow && !c->drag.active && c->btns_down > 0 && serial == c->btn_serial &&
             c->ptr_surf && c->ptr_focus && root_of(ow) == c->ptr_focus &&
             wl_resource_get_client(c->ptr_focus->res) == cl;
    if (!ok) { if (src) wl_data_source_send_cancelled(src); return; }

    ptr_leave(c);                          /* the origin no longer has the pointer */
    memset(&c->drag, 0, sizeof c->drag);
    c->drag.active = 1;
    c->drag.source = src;
    c->drag.origin = ow;
    c->drag.icon = iw;
    /* Say where it is straight away, so the first thing the target hears is
     * enter at the pointer, not at wherever the next motion lands. */
    drag_motion(c, c->ptr_focus, c->ptr_rx, c->ptr_ry, 0);
}
static void ddev_set_selection(struct wl_client *cl, struct wl_resource *r,
                               struct wl_resource *src, uint32_t serial)
{
    (void)cl;
    aurwl *c = wl_resource_get_user_data(r);
    /* THE CLIPBOARD BELONGS TO WHOEVER IS BEING TYPED INTO.
     *
     * There was no check of any kind here: the serial was stored and
     * never compared with anything, and the caller's focus was never
     * considered. So any background process could take the clipboard
     * whenever it liked, and could keep firing `cancelled` at the
     * program that really owned it -- which on the other side reads as
     * copy quietly not working.
     *
     * Focus is the check the protocol's own wording implies (the
     * selection event is defined as going to the client that has
     * keyboard focus). A machine with nothing focused yet is the
     * start-up case and is allowed, or the first program to run could
     * never copy anything. */
    struct wl_client *owner = win_client(c->focus);
    if (owner && wl_resource_get_client(r) != owner) {
        if (src) wl_data_source_send_cancelled(src);
        return;
    }
    if (c->selection && c->selection != src) wl_data_source_send_cancelled(c->selection);
    c->selection = src;
    c->selection_serial = serial;
    xw_selection_changed(c);                       /* X programs are offered it (or lose it) */
    /* Retire the offers for the previous selection. Leaving them alive
     * leaked one wl_data_offer per device per focus change, and -- worse
     * than the leak -- a client that kept an old offer could go on
     * reading the clipboard contents it described long after the user
     * had copied something else. */
    struct wl_resource *old, *oldt;
    wl_resource_for_each_safe(old, oldt, &c->offers) offer_retire(old);
    /* Only the client with keyboard focus. The protocol says the
     * selection event goes to a client immediately before it receives
     * keyboard focus, and while it has focus -- and it says so for a
     * reason. Broadcasting it handed every background process on the
     * machine a live wl_data_offer for whatever the user had just
     * copied, readable at any moment with nothing on screen to show it.
     * Copy a password out of a password manager and every running
     * program could read it. */
    struct wl_client *t = win_client(c->focus);
    if (!t) return;
    struct wl_resource *dev;
    wl_resource_for_each(dev, &c->devices)
        if (wl_resource_get_client(dev) == t) send_selection_to(c, dev);
}
/* The clipboard changed hands without a Wayland program setting it (an X program did): the
 * offers are redone, and only the program with the keyboard hears of it. */
static void selection_broadcast(aurwl *c)
{
    struct wl_resource *old, *oldt;
    wl_resource_for_each_safe(old, oldt, &c->offers) offer_retire(old);
    struct wl_client *t = win_client(c->focus);
    if (!t) return;
    struct wl_resource *dev;
    wl_resource_for_each(dev, &c->devices)
        if (wl_resource_get_client(dev) == t) send_selection_to(c, dev);
}
static const struct wl_data_device_interface data_device_impl = {
    .start_drag = ddev_start_drag, .set_selection = ddev_set_selection,
    .release = noop_destroy,
};

static void ddm_create_source(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    aurwl *c = wl_resource_get_user_data(r);
    data_source *s = calloc(1, sizeof *s);
    if (!s) { wl_client_post_no_memory(cl); return; }
    s->c = c;
    s->res = wl_resource_create(cl, &wl_data_source_interface, wl_resource_get_version(r), id);
    if (!s->res) { free(s); wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(s->res, &data_source_impl, s, dsrc_gone);
}
static void ddm_get_device(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                           struct wl_resource *seat)
{
    (void)seat;
    aurwl *c = wl_resource_get_user_data(r);
    if (too_many(cl, r, &c->devices, MAX_DATA_OBJ, "clipboards")) return;
    struct wl_resource *dev = wl_resource_create(cl, &wl_data_device_interface,
                                                 wl_resource_get_version(r), id);
    if (!dev) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(dev, &data_device_impl, c, res_unlink);
    wl_list_insert(&c->devices, wl_resource_get_link(dev));
    /* Nothing is offered here. A client gets the selection when it is
     * given keyboard focus, not when it asks for a data device -- which
     * it does at startup, long before the user has chosen it. */
}
static const struct wl_data_device_manager_interface ddm_impl = {
    .create_data_source = ddm_create_source, .get_data_device = ddm_get_device,
};
static void bind_ddm(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &wl_data_device_manager_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &ddm_impl, data, NULL);
}

/* ── xdg-shell ──────────────────────────────────────────────────── */

typedef struct {
    int      w, h;
    int      ax, ay, aw, ah;      /* anchor rect, parent-local */
    uint32_t anchor, gravity, constraint;
    int      ox, oy;
} positioner;

/* EVERY NUMBER BELOW CAME FROM A CLIENT.
 *
 * None of them was checked, and positioner_resolve() then added them
 * together -- eight signed-overflow sites, confirmed by
 * UndefinedBehaviorSanitizer. It does not reach a wild write, because
 * draw_scaled clamps to the destination, so what it actually produces
 * is undefined behaviour in the process that owns the display and a
 * menu in the wrong place. damage_accum() in this same file does the
 * same arithmetic in 64 bits with a comment about exactly why UB here
 * is not acceptable; this is that job left half done.
 *
 * A menu bigger than a large screen is not a menu, so the bound is
 * the same one the rest of the file uses and the arithmetic cannot
 * leave int range. */
#define POS_MAX  MAX_DIM

static int pos_clamp(int32_t v)
{
    if (v >  POS_MAX) return  POS_MAX;
    if (v < -POS_MAX) return -POS_MAX;
    return (int)v;
}

static void pos_set_size(struct wl_client *cl, struct wl_resource *r, int32_t w, int32_t h)
{
    (void)cl; positioner *p = wl_resource_get_user_data(r);
    /* xdg-shell.xml: a size with a non-positive component is
     * invalid_input. It was accepted, and a zero-sized popup is a menu
     * she cannot see or press. */
    if (w <= 0 || h <= 0) {
        wl_resource_post_error(r, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "a menu cannot be %dx%d", w, h);
        return;
    }
    p->w = pos_clamp(w); p->h = pos_clamp(h);
}
static void pos_set_anchor_rect(struct wl_client *cl, struct wl_resource *r,
                                int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)cl; positioner *p = wl_resource_get_user_data(r);
    if (w < 0 || h < 0) {
        wl_resource_post_error(r, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "an anchor cannot be %dx%d", w, h);
        return;
    }
    p->ax = pos_clamp(x); p->ay = pos_clamp(y);
    p->aw = pos_clamp(w); p->ah = pos_clamp(h);
}
static void pos_set_anchor(struct wl_client *cl, struct wl_resource *r, uint32_t a)
{ (void)cl; ((positioner *)wl_resource_get_user_data(r))->anchor = a; }
static void pos_set_gravity(struct wl_client *cl, struct wl_resource *r, uint32_t g)
{ (void)cl; ((positioner *)wl_resource_get_user_data(r))->gravity = g; }
static void pos_set_constraint(struct wl_client *cl, struct wl_resource *r, uint32_t a)
{ (void)cl; ((positioner *)wl_resource_get_user_data(r))->constraint = a; }
static void pos_set_offset(struct wl_client *cl, struct wl_resource *r, int32_t x, int32_t y)
{ (void)cl; positioner *p = wl_resource_get_user_data(r);
  p->ox = pos_clamp(x); p->oy = pos_clamp(y); }
static void pos_set_reactive(struct wl_client *cl, struct wl_resource *r) { (void)cl; (void)r; }
static void pos_set_parent_size(struct wl_client *cl, struct wl_resource *r, int32_t w, int32_t h)
{ (void)cl; (void)r; (void)w; (void)h; }
static void pos_set_parent_configure(struct wl_client *cl, struct wl_resource *r, uint32_t s)
{ (void)cl; (void)r; (void)s; }
static const struct xdg_positioner_interface positioner_impl = {
    .destroy = noop_destroy, .set_size = pos_set_size,
    .set_anchor_rect = pos_set_anchor_rect, .set_anchor = pos_set_anchor,
    .set_gravity = pos_set_gravity, .set_constraint_adjustment = pos_set_constraint,
    .set_offset = pos_set_offset, .set_reactive = pos_set_reactive,
    .set_parent_size = pos_set_parent_size,
    .set_parent_configure = pos_set_parent_configure,
};
static void positioner_free(struct wl_resource *r) { free(wl_resource_get_user_data(r)); }

/* Place a popup relative to its parent. Menus are the one piece of UI
 * where being a few pixels wrong is instantly visible, and being off
 * the bottom of the screen makes a browser unusable rather than ugly,
 * so the sliding correction is not optional garnish. */
static void positioner_resolve(const positioner *p, aurwl_win *parent, aurwl *c,
                               int *out_x, int *out_y)
{
    int x = p->ax, y = p->ay;
    uint32_t a = p->anchor;
    if (a == XDG_POSITIONER_ANCHOR_TOP || a == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
        a == XDG_POSITIONER_ANCHOR_TOP_RIGHT) y = p->ay;
    else if (a == XDG_POSITIONER_ANCHOR_BOTTOM || a == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT ||
             a == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) y = p->ay + p->ah;
    else y = p->ay + p->ah / 2;

    if (a == XDG_POSITIONER_ANCHOR_LEFT || a == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
        a == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT) x = p->ax;
    else if (a == XDG_POSITIONER_ANCHOR_RIGHT || a == XDG_POSITIONER_ANCHOR_TOP_RIGHT ||
             a == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) x = p->ax + p->aw;
    else x = p->ax + p->aw / 2;

    uint32_t g = p->gravity;
    if (g == XDG_POSITIONER_GRAVITY_LEFT || g == XDG_POSITIONER_GRAVITY_TOP_LEFT ||
        g == XDG_POSITIONER_GRAVITY_BOTTOM_LEFT) x -= p->w;
    else if (!(g == XDG_POSITIONER_GRAVITY_RIGHT || g == XDG_POSITIONER_GRAVITY_TOP_RIGHT ||
               g == XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)) x -= p->w / 2;

    if (g == XDG_POSITIONER_GRAVITY_TOP || g == XDG_POSITIONER_GRAVITY_TOP_LEFT ||
        g == XDG_POSITIONER_GRAVITY_TOP_RIGHT) y -= p->h;
    else if (!(g == XDG_POSITIONER_GRAVITY_BOTTOM || g == XDG_POSITIONER_GRAVITY_BOTTOM_LEFT ||
               g == XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)) y -= p->h / 2;

    x += p->ox; y += p->oy;

    /* Keep it on screen. The parent's own position is the shell's
     * business, so we correct against the output and let the shell's
     * clipping take care of the rest. */
    int px = parent ? parent->geo_x : 0, py = parent ? parent->geo_y : 0;
    int sx = px + x, sy = py + y;
    if (sx + p->w > c->ow) sx = c->ow - p->w;
    if (sy + p->h > c->oh) sy = c->oh - p->h;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    *out_x = sx - px; *out_y = sy - py;
}

/* ── xdg_toplevel ───────────────────────────────────────────────── */

static void configure_toplevel(aurwl_win *w)
{
    /* Both, separately. A client may destroy the xdg_surface and keep
     * the xdg_toplevel; the window then stays mapped and in the list,
     * so the shell configures it every frame, and
     * xdg_surface_send_configure(NULL) dereferences its client. That
     * crashes with no further help from the client at all. */
    if (!w->xdg_toplevel || !w->xdg_surface) return;
    struct wl_array states;
    wl_array_init(&states);
    uint32_t *st;
    if (w->want_act)  { st = wl_array_add(&states, 4); if (st) *st = XDG_TOPLEVEL_STATE_ACTIVATED; }
    if (w->want_max)  { st = wl_array_add(&states, 4); if (st) *st = XDG_TOPLEVEL_STATE_MAXIMIZED; }
    if (w->want_full) { st = wl_array_add(&states, 4); if (st) *st = XDG_TOPLEVEL_STATE_FULLSCREEN; }
    xdg_toplevel_send_configure(w->xdg_toplevel, w->want_w, w->want_h, &states);
    wl_array_release(&states);
    send_configure(w);
}

static void tl_set_parent(struct wl_client *cl, struct wl_resource *r, struct wl_resource *p)
{ (void)cl; (void)r; (void)p; }
/* Every handler below can be reached after its surface is gone: the
 * resource outlives the surface for as long as it takes libwayland to
 * work through a dead client's object list, and we disarm it rather
 * than destroy it. So `w` being NULL is ordinary, not exceptional. */
static void tl_set_title(struct wl_client *cl, struct wl_resource *r, const char *t)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r); if (!w) return;
  snprintf(w->title, sizeof w->title, "%s", t ? t : ""); }
static void tl_set_app_id(struct wl_client *cl, struct wl_resource *r, const char *a)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r); if (!w) return;
  int first = w->app_id[0] == 0;
  snprintf(w->app_id, sizeof w->app_id, "%s", a ? a : "");
  if (first && wishhook_app) wishhook_app(w->app_id); }
static void tl_show_menu(struct wl_client *cl, struct wl_resource *r, struct wl_resource *s,
                         uint32_t ser, int32_t x, int32_t y)
{ (void)cl; (void)r; (void)s; (void)ser; (void)x; (void)y; }
/* What a client's own title bar can ask of us. They are not acted on here:
 * the shell owns where windows are, so they are written down and the shell
 * takes them once a frame (aurwl_win_take_requests). The serial is not
 * checked: the shell only moves a window while the button is down. */
static void win_ask(struct wl_resource *r, unsigned bit)
{ aurwl_win *w = wl_resource_get_user_data(r); if (w) w->req |= bit; }
static void tl_move(struct wl_client *cl, struct wl_resource *r, struct wl_resource *s, uint32_t ser)
{ (void)cl; (void)s; (void)ser; win_ask(r, AURWL_REQ_MOVE); }
static void tl_resize(struct wl_client *cl, struct wl_resource *r, struct wl_resource *s,
                      uint32_t ser, uint32_t edges)
{ (void)cl; (void)r; (void)s; (void)ser; (void)edges; }
static void tl_set_max(struct wl_client *cl, struct wl_resource *r, int32_t w_, int32_t h_)
{ (void)cl; (void)r; (void)w_; (void)h_; }
static void tl_set_min(struct wl_client *cl, struct wl_resource *r, int32_t w_, int32_t h_)
{ (void)cl; (void)r; (void)w_; (void)h_; }
static void tl_maximize(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r); if (!w) return;
  w->req |= AURWL_REQ_MAXIMIZE; w->want_max = 1; configure_toplevel(w); }
static void tl_unmaximize(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r); if (!w) return;
  w->req |= AURWL_REQ_RESTORE; w->want_max = 0; configure_toplevel(w); }
static void tl_fullscreen(struct wl_client *cl, struct wl_resource *r, struct wl_resource *o)
{ (void)cl; (void)o; aurwl_win *w = wl_resource_get_user_data(r); if (!w) return;
  w->req |= AURWL_REQ_FULLSCREEN; w->want_full = 1; configure_toplevel(w); }
static void tl_unfullscreen(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; aurwl_win *w = wl_resource_get_user_data(r); if (!w) return;
  w->req |= AURWL_REQ_UNFULLSCREEN; w->want_full = 0; configure_toplevel(w); }
static void tl_minimize(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; win_ask(r, AURWL_REQ_MINIMIZE); }

static const struct xdg_toplevel_interface toplevel_impl = {
    .destroy = noop_destroy, .set_parent = tl_set_parent, .set_title = tl_set_title,
    .set_app_id = tl_set_app_id, .show_window_menu = tl_show_menu, .move = tl_move,
    .resize = tl_resize, .set_max_size = tl_set_max, .set_min_size = tl_set_min,
    .set_maximized = tl_maximize, .unset_maximized = tl_unmaximize,
    .set_fullscreen = tl_fullscreen, .unset_fullscreen = tl_unfullscreen,
    .set_minimized = tl_minimize,
};
static void toplevel_gone(struct wl_resource *r)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    if (!w) return;
    if (w->xdg_toplevel != r) return;      /* a stale duplicate */
    /* The role is NOT cleared: it is permanent for the surface's
     * whole life, and clearing it here is what let a surface be given
     * a second one. */
    w->xdg_toplevel = NULL; w->acked = 0;
    if (w->mapped) surface_unmap(w);
}

/* ── xdg_popup ──────────────────────────────────────────────────── */

static void pop_grab(struct wl_client *cl, struct wl_resource *r, struct wl_resource *seat, uint32_t s)
{ (void)cl; (void)r; (void)seat; (void)s; }
static void pop_reposition(struct wl_client *cl, struct wl_resource *r,
                           struct wl_resource *pos_res, uint32_t token)
{
    (void)cl;
    aurwl_win *w = wl_resource_get_user_data(r);
    positioner *p = wl_resource_get_user_data(pos_res);
    if (!w || !p) return;
    if (!w->xdg_surface) return;
    positioner_resolve(p, w->parent, w->c, &w->px, &w->py);
    w->want_w = p->w; w->want_h = p->h;
    xdg_popup_send_repositioned(r, token);
    xdg_popup_send_configure(r, w->px, w->py, p->w, p->h);
    send_configure(w);
}
static const struct xdg_popup_interface popup_impl = {
    .destroy = noop_destroy, .grab = pop_grab, .reposition = pop_reposition,
};
static void popup_gone(struct wl_resource *r)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    if (!w) return;
    if (w->xdg_popup != r) return;
    w->xdg_popup = NULL; w->acked = 0;
    if (w->parent) { wl_list_remove(&w->child_link); wl_list_init(&w->child_link); w->parent = NULL; }
    if (w->mapped) surface_unmap(w);
}

/* ── xdg_surface ────────────────────────────────────────────────── */

static void xs_get_toplevel(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    if (!w) { wl_resource_post_error(r, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER, "surface is gone"); return; }
    /* One role object per surface, which the spec requires anyway. The
     * surface stores a single pointer to each kind, so a second one is
     * invisible to the disarming in surface_free_res() -- it stays
     * armed with a pointer to the freed window, and its handlers then
     * write attacker-chosen bytes into the freed chunk. set_title alone
     * is 127 of them at a fixed offset. */
    if (w->xdg_toplevel || w->xdg_popup) {
        wl_resource_post_error(r, XDG_WM_BASE_ERROR_ROLE,
                               "this surface already has a role");
        return;
    }
    struct wl_resource *tl = wl_resource_create(cl, &xdg_toplevel_interface,
                                                wl_resource_get_version(r), id);
    if (!tl) { wl_client_post_no_memory(cl); return; }
    if (!take_role(w, ROLE_TOPLEVEL, r, XDG_WM_BASE_ERROR_ROLE)) {
        wl_resource_destroy(tl);
        return;
    }
    wl_resource_set_implementation(tl, &toplevel_impl, w, toplevel_gone);
    w->xdg_toplevel = tl;

    /* Version 5 says this must arrive before the first configure. What
     * goes in it is what we actually do: maximize and fullscreen are
     * handled, and so is minimise (the shell takes the request); the
     * window menu is not, so claiming it would put a button in the
     * client's title bar that does nothing. */
    if (wl_resource_get_version(tl) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        struct wl_array caps; wl_array_init(&caps);
        uint32_t *cp;
        cp = wl_array_add(&caps, 4); if (cp) *cp = XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE;
        cp = wl_array_add(&caps, 4); if (cp) *cp = XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN;
        cp = wl_array_add(&caps, 4); if (cp) *cp = XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
        xdg_toplevel_send_wm_capabilities(tl, &caps);
        wl_array_release(&caps);
    }
}
static void xs_get_popup(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                         struct wl_resource *parent_res, struct wl_resource *pos_res)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    positioner *p = pos_res ? wl_resource_get_user_data(pos_res) : NULL;
    if (!w || !p) { wl_resource_post_error(r, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                               "surface or positioner is gone"); return; }
    if (w->xdg_toplevel || w->xdg_popup) {
        wl_resource_post_error(r, XDG_WM_BASE_ERROR_ROLE,
                               "this surface already has a role");
        return;
    }
    struct wl_resource *pr = wl_resource_create(cl, &xdg_popup_interface,
                                                wl_resource_get_version(r), id);
    if (!pr) { wl_client_post_no_memory(cl); return; }
    if (!take_role(w, ROLE_POPUP, r, XDG_WM_BASE_ERROR_ROLE)) {
        wl_resource_destroy(pr);
        return;
    }
    wl_resource_set_implementation(pr, &popup_impl, w, popup_gone);
    w->xdg_popup = pr;

    aurwl_win *parent = NULL;
    if (parent_res) {
        aurwl_win *pw = wl_resource_get_user_data(parent_res);   /* an xdg_surface */
        parent = pw;
    }
    if (parent) {
        if (parent_loop(parent, w)) {
            wl_resource_post_error(r, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                                   "popup parent loop");
            return;
        }
        wl_list_remove(&w->child_link);
        w->parent = parent;
        wl_list_insert(parent->children.prev, &w->child_link);
    }
    positioner_resolve(p, parent, w->c, &w->px, &w->py);
    w->want_w = p->w; w->want_h = p->h;
    xdg_popup_send_configure(pr, w->px, w->py, p->w, p->h);
}
static void xs_set_geometry(struct wl_client *cl, struct wl_resource *r,
                            int32_t x, int32_t y, int32_t ww, int32_t hh)
{
    (void)cl; aurwl_win *w = wl_resource_get_user_data(r); if (!w) return;
    /* The visible window is usually smaller than the buffer: a toolkit
     * draws its own shadow into the margin. Without this the shell would
     * lay windows out by their shadows and everything would look loose
     * by a dozen pixels on every edge. */
    if (ww <= 0 || hh <= 0) {
        wl_resource_post_error(r, XDG_SURFACE_ERROR_INVALID_SIZE,
                               "a window cannot be %dx%d", ww, hh);
        return;
    }
    /* Bounded like every other size here. Unbounded, a 2^31-wide
     * geometry became the window's logical size, the shell scaled the
     * pointer into it and aurwl multiplied that by 256 for wl_fixed --
     * a signed overflow on every mouse movement; and a parent at
     * x = 2^31-1 overflowed popup placement (px + x). Found by
     * tools/fuzz/fuzz_aurwl.c. */
    w->geo_w = clampi(ww, 1, MAX_DIM); w->geo_h = clampi(hh, 1, MAX_DIM);
    w->geo_x = clampi(x, -MAX_DIM, MAX_DIM);
    w->geo_y = clampi(y, -MAX_DIM, MAX_DIM);
    w->has_geo = 1;
}
/* The serial has to be one we actually sent. Anything is accepted
 * otherwise, which means a client that never received a configure --
 * or that invented a number -- can make the compositor believe it is
 * in a state it was never told to be in. xdg-shell.xml calls that
 * invalid_serial. */
static void xs_ack(struct wl_client *cl, struct wl_resource *r, uint32_t serial)
{
    (void)cl;
    aurwl_win *w = wl_resource_get_user_data(r);
    if (!w) return;
    if (!serial_was_sent(w, serial)) {
        wl_resource_post_error(r, XDG_SURFACE_ERROR_INVALID_SERIAL,
                               "no configure with serial %u was sent", serial);
        return;
    }
    w->acked = 1;
    w->ack_floor = serial + 1;
}

static const struct xdg_surface_interface xdg_surface_impl = {
    .destroy = noop_destroy, .get_toplevel = xs_get_toplevel, .get_popup = xs_get_popup,
    .set_window_geometry = xs_set_geometry, .ack_configure = xs_ack,
};
static void xdg_surface_gone(struct wl_resource *r)
{
    aurwl_win *w = wl_resource_get_user_data(r);
    if (w && w->xdg_surface == r) w->xdg_surface = NULL;
}

/* ── xdg_wm_base ────────────────────────────────────────────────── */

static void wm_create_positioner(struct wl_client *cl, struct wl_resource *r, uint32_t id)
{
    (void)r;
    positioner *p = calloc(1, sizeof *p);
    if (!p) { wl_client_post_no_memory(cl); return; }
    struct wl_resource *res = wl_resource_create(cl, &xdg_positioner_interface,
                                                 wl_resource_get_version(r), id);
    if (!res) { free(p); wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(res, &positioner_impl, p, positioner_free);
}
static void wm_get_xdg_surface(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                               struct wl_resource *surf)
{
    aurwl_win *w = surf ? wl_resource_get_user_data(surf) : NULL;
    if (!w) { wl_resource_post_error(r, XDG_WM_BASE_ERROR_ROLE, "no such surface"); return; }
    if (w->xdg_surface) {
        wl_resource_post_error(r, XDG_WM_BASE_ERROR_ROLE,
                               "this surface already has an xdg_surface");
        return;
    }
    struct wl_resource *xs = wl_resource_create(cl, &xdg_surface_interface,
                                                wl_resource_get_version(r), id);
    if (!xs) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(xs, &xdg_surface_impl, w, xdg_surface_gone);
    w->xdg_surface = xs;
    w->acked = 0;
}
static void wm_pong(struct wl_client *cl, struct wl_resource *r, uint32_t serial)
{ (void)cl; (void)r; (void)serial; }
static const struct xdg_wm_base_interface wm_base_impl = {
    .destroy = noop_destroy, .create_positioner = wm_create_positioner,
    .get_xdg_surface = wm_get_xdg_surface, .pong = wm_pong,
};
static void bind_wm_base(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &xdg_wm_base_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &wm_base_impl, data, NULL);
}

/* ── xdg-decoration ─────────────────────────────────────────────── */
/* We ask every client to let us draw its title bar. That is not a
 * performance argument -- it is the whole product argument. A desktop
 * where the browser's title bar is GTK's and the file manager's is
 * Qt's is a desktop assembled from parts; the shell's own chrome, on
 * every window, is what makes an AurOS theme mean something. Clients
 * that insist on drawing their own are not fought with. */

/* A decoration configure is state the client must acknowledge, and the
 * serial to acknowledge it with comes from xdg_surface.configure. Sent
 * on its own, the client was handed something to ack and no way to ack
 * it -- toolkits latch the decoration mode inside their xdg_surface
 * configure handler, so a set_mode after mapping did not take effect
 * until some unrelated later configure happened to arrive. Two title
 * bars, or none. */
static void deco_answer(struct wl_resource *r, uint32_t mode)
{
    zxdg_toplevel_decoration_v1_send_configure(r, mode);
    aurwl_win *w = wl_resource_get_user_data(r);
    if (w) send_configure(w);
}
/* A client that says "I draw my own" is believed. The shell then draws no
 * bar over the top of it (aurwl_win_csd): two sets of window buttons, the
 * client's and ours, is the one thing nobody can work out. Anything else
 * -- no preference, or "you draw it" -- gets ours. */
static void deco_set_mode(struct wl_client *cl, struct wl_resource *r, uint32_t mode)
{
    (void)cl;
    aurwl_win *w = wl_resource_get_user_data(r);
    int own = (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    if (w) w->deco_client = own;
    deco_answer(r, own ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                       : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
static void deco_unset_mode(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl;
    aurwl_win *w = wl_resource_get_user_data(r);
    if (w) w->deco_client = 0;
    deco_answer(r, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
static const struct zxdg_toplevel_decoration_v1_interface deco_impl = {
    .destroy = noop_destroy, .set_mode = deco_set_mode, .unset_mode = deco_unset_mode,
};
static void deco_gone(struct wl_resource *r)
{ aurwl_win *w = wl_resource_get_user_data(r); if (w && w->decoration == r) w->decoration = NULL; }

static void decomgr_get(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                        struct wl_resource *tl)
{
    aurwl_win *w = tl ? wl_resource_get_user_data(tl) : NULL;
    if (w && w->decoration) { wl_resource_post_error(r, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED,
                               "already decorated"); return; }
    struct wl_resource *d = wl_resource_create(cl, &zxdg_toplevel_decoration_v1_interface, 1, id);
    if (!d) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(d, &deco_impl, w, deco_gone);
    if (w) w->decoration = d;
    /* At construction the client has not made its initial commit yet,
     * so the configure that answers this one is the one that handshake
     * produces. Sending another here would be a second, unanswerable
     * sequence. */
    zxdg_toplevel_decoration_v1_send_configure(d, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
static const struct zxdg_decoration_manager_v1_interface decomgr_impl = {
    .destroy = noop_destroy, .get_toplevel_decoration = decomgr_get,
};
static void bind_deco(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &zxdg_decoration_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &decomgr_impl, data, NULL);
}

/* ── idle inhibit ───────────────────────────────────────────────── */
/* THE FILM THAT WENT DARK.
 *
 * The shell turns the screen off after ten minutes without a key or a
 * click, because on a laptop that was left alone that is right. A film
 * is the case it gets wrong: nobody touches anything for two hours. The
 * program that is playing one knows -- Firefox, mpv and VLC all create a
 * zwp_idle_inhibitor_v1 on their window while a video plays -- and this
 * is how it tells the desktop. An inhibitor only counts while the window
 * it belongs to is on screen: a hidden browser tab cannot keep the
 * screen lit.
 *
 * aurwl does not decide what idleness means; it answers one question for
 * the shell (aurwl_idle_inhibited) and the shell does the rest. */

static void inh_destroy(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; wl_resource_destroy(r); }
static const struct zwp_idle_inhibitor_v1_interface inh_impl = { .destroy = inh_destroy };
static void inh_gone(struct wl_resource *r)
{
    struct idle_inh *ih = wl_resource_get_user_data(r);
    if (!ih) return;
    wl_list_remove(&ih->link);
    free(ih);
}

static void inhmgr_create(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                          struct wl_resource *surf)
{
    aurwl *c = wl_resource_get_user_data(r);
    aurwl_win *w = surf ? wl_resource_get_user_data(surf) : NULL;
    /* Per program, like every other thing a client can make as many of as
     * it likes: they are walked by the shell every frame. */
    int mine = 0;
    struct idle_inh *o;
    wl_list_for_each(o, &c->inhibitors, link)
        if (wl_resource_get_client(o->res) == cl) mine++;
    if (mine >= MAX_DATA_OBJ) {
        wl_resource_post_error(r, 0, "no more than %d idle inhibitors per program", MAX_DATA_OBJ);
        return;
    }
    struct idle_inh *ih = calloc(1, sizeof *ih);
    if (!ih) { wl_client_post_no_memory(cl); return; }
    ih->res = wl_resource_create(cl, &zwp_idle_inhibitor_v1_interface, wl_resource_get_version(r), id);
    if (!ih->res) { free(ih); wl_client_post_no_memory(cl); return; }
    ih->w = w;
    wl_list_insert(&c->inhibitors, &ih->link);
    wl_resource_set_implementation(ih->res, &inh_impl, ih, inh_gone);
}
static const struct zwp_idle_inhibit_manager_v1_interface inhmgr_impl = {
    .destroy = noop_destroy, .create_inhibitor = inhmgr_create,
};
static void bind_idle_inhibit(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &zwp_idle_inhibit_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &inhmgr_impl, data, NULL);
}

/* ── wp_cursor_shape_v1 ─────────────────────────────────────────── */
/* A program that only wants "the hand" or "the text cursor" says so by name
 * and the SHELL decides what that looks like -- large, high contrast, the
 * size she chose for her text. (GTK4 and Qt6 do this; programs that load a
 * cursor theme themselves send a picture instead, see wl_pointer.set_cursor.) */
static void csdev_set_shape(struct wl_client *cl, struct wl_resource *r, uint32_t serial, uint32_t shape)
{
    aurwl *c = wl_resource_get_user_data(r);
    if (shape < WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT ||
        shape > WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT) {
        wl_resource_post_error(r, WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE,
                               "there is no cursor shape %u", shape);
        return;
    }
    if (!cursor_allowed(c, cl, serial)) return;
    memset(&c->cursor, 0, sizeof c->cursor);
    c->cursor.kind = AURWL_CURSOR_SHAPE;
    c->cursor.shape = (int)shape;
    c->damage_seq++;
}
static const struct wp_cursor_shape_device_v1_interface csdev_impl = {
    .destroy = noop_destroy, .set_shape = csdev_set_shape,
};
static void csmgr_get_pointer(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                              struct wl_resource *pointer)
{
    (void)pointer;
    aurwl *c = wl_resource_get_user_data(r);
    if (too_many(cl, r, &c->cursor_devs, MAX_SEAT_OBJ, "cursor shape devices")) return;
    struct wl_resource *d = wl_resource_create(cl, &wp_cursor_shape_device_v1_interface,
                                               wl_resource_get_version(r), id);
    if (!d) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(d, &csdev_impl, c, res_unlink);
    wl_list_insert(&c->cursor_devs, wl_resource_get_link(d));
}
static void csmgr_get_tablet(struct wl_client *cl, struct wl_resource *r, uint32_t id,
                             struct wl_resource *tool)
{
    (void)cl; (void)id; (void)tool;
    wl_resource_post_error(r, 0, "this computer has no drawing tablet");
}
static const struct wp_cursor_shape_manager_v1_interface csmgr_impl = {
    .destroy = noop_destroy, .get_pointer = csmgr_get_pointer, .get_tablet_tool_v2 = csmgr_get_tablet,
};
static void bind_cursor_shape(struct wl_client *cl, void *data, uint32_t ver, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(cl, &wp_cursor_shape_manager_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &csmgr_impl, data, NULL);
}

/* ── Xwayland and its window manager ────────────────────────────── */
#include "xwm.inc"

/* ── keymap ─────────────────────────────────────────────────────── */

/* The client does its own key translation from a keymap we hand it.
 * That indirection is the only reason an application we did not write
 * can be typed into in Greek, Dvorak or Hungarian: we never decide what
 * a key means, we only say which key moved. */

/* WHERE THE LAYOUT COMES FROM
 *
 * /etc/default/keyboard, which is the file the rest of the system --
 * the console, the installer, every other desktop -- already reads, and
 * which build/forge already writes from the profile's keyboard_layout.
 * Reading it here rather than inventing a second setting is the whole
 * point: profiles/multilingual.profile says keyboard_layout="fr" and
 * that line did nothing at all until this function read it, so a French
 * machine typed QWERTY into every application on it.
 *
 * Environment wins when it is set, because a person testing a layout
 * should not have to edit a system file to do it.
 *
 * KEY="value", KEY=value, comments and blank lines. Deliberately not a
 * shell parser: this file is a fixed four-line format written by the
 * distribution, and running a shell to read four assignments would be a
 * larger surface than the thing it reads.
 */
static void kb_defaults(char *buf, size_t bufn, const char **model,
                        const char **layout, const char **variant,
                        const char **options)
{
    /* The path is overridable so that a layout can be tried without
     * editing a system file -- by an installer offering a choice, by a
     * live session, and by tools/keytest.c, which is how the parser
     * below is proved against the quoting the real file uses. */
    const char *path = getenv("AUROS_KB_FILE");
    if (!path || !*path) path = "/etc/default/keyboard";

    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    size_t used = 0;
    while (fgets(line, sizeof line, f)) {
        char *k = line;
        while (*k == ' ' || *k == '\t') k++;
        if (*k == '#' || *k == '\n' || !*k) continue;
        char *eq = strchr(k, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;

        /* trim the key's trailing space, and the value's newline */
        for (char *e = eq - 1; e >= k && (*e == ' ' || *e == '\t'); e--) *e = 0;
        size_t vl = strlen(v);
        while (vl && (v[vl-1] == '\n' || v[vl-1] == '\r' ||
                      v[vl-1] == ' '  || v[vl-1] == '\t')) v[--vl] = 0;
        if (vl >= 2 && ((v[0] == '"' && v[vl-1] == '"') ||
                        (v[0] == '\'' && v[vl-1] == '\''))) { v[vl-1] = 0; v++; vl -= 2; }
        if (!vl) continue;                 /* XKBVARIANT="" means none */

        const char **slot = NULL;
        if      (!strcmp(k, "XKBMODEL"))   slot = model;
        else if (!strcmp(k, "XKBLAYOUT"))  slot = layout;
        else if (!strcmp(k, "XKBVARIANT")) slot = variant;
        else if (!strcmp(k, "XKBOPTIONS")) slot = options;
        if (!slot || *slot) continue;      /* env already answered */

        if (used + vl + 1 > bufn) continue;
        memcpy(buf + used, v, vl + 1);
        *slot = buf + used;
        used += vl + 1;
    }
    fclose(f);
}

static int build_keymap(aurwl *c)
{
    c->keymap_fd = -1;
    c->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!c->xkb) return -1;

    struct xkb_rule_names names = {0};
    names.model   = getenv("AUROS_KB_MODEL");
    names.layout  = getenv("AUROS_KB_LAYOUT");
    names.variant = getenv("AUROS_KB_VARIANT");
    names.options = getenv("AUROS_KB_OPTIONS");
    /* An empty environment variable is not a layout. */
    if (names.model   && !*names.model)   names.model   = NULL;
    if (names.layout  && !*names.layout)  names.layout  = NULL;
    if (names.variant && !*names.variant) names.variant = NULL;
    if (names.options && !*names.options) names.options = NULL;

    char kbbuf[512];
    kb_defaults(kbbuf, sizeof kbbuf, &names.model, &names.layout,
                &names.variant, &names.options);

    c->keymap = xkb_keymap_new_from_names(c->xkb, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!c->keymap && (names.layout || names.variant || names.options || names.model)) {
        /* A layout name that does not compile must not leave the
         * machine with no keyboard. Say so and fall back to xkb's own
         * default, which is a usable keyboard in the wrong language --
         * strictly better than a dead one. */
        fprintf(stderr, "aurwl: keyboard layout \"%s%s%s\" did not compile; "
                        "using the default layout\n",
                names.layout ? names.layout : "",
                names.variant ? "-" : "", names.variant ? names.variant : "");
        struct xkb_rule_names none = {0};
        c->keymap = xkb_keymap_new_from_names(c->xkb, &none, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (!c->keymap) return -1;
    c->xkb_state = xkb_state_new(c->keymap);
    if (!c->xkb_state) return -1;

    char *str = xkb_keymap_get_as_string(c->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!str) return -1;
    size_t len = strlen(str) + 1;

    int fd = memfd_create("aurwl-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) { free(str); return -1; }
    if (write(fd, str, len) != (ssize_t)len) { close(fd); free(str); return -1; }
    free(str);
    /* Sealed so a client cannot rewrite the keymap every other client
     * is reading from the same descriptor. */
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    c->keymap_fd = fd;
    c->keymap_size = len;
    return 0;
}

/* ── construction ───────────────────────────────────────────────── */

aurwl *aurwl_create(int w, int h, int refresh_mhz)
{
    aurwl *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->ow = w > 0 ? w : 1280;
    c->oh = h > 0 ? h : 720;
    c->refresh_mhz = refresh_mhz > 0 ? refresh_mhz : 60000;
    c->keymap_fd = -1;

    wl_list_init(&c->surfaces);  wl_list_init(&c->seats);
    wl_list_init(&c->pointers);  wl_list_init(&c->keyboards);
    wl_list_init(&c->touches);   wl_list_init(&c->outputs);
    wl_list_init(&c->devices);
    wl_list_init(&c->offers);
    wl_list_init(&c->xdg_outputs);
    wl_list_init(&c->inhibitors);
    wl_list_init(&c->cursor_devs);

    c->display = wl_display_create();
    if (!c->display) { free(c); return NULL; }
    c->loop = wl_display_get_event_loop(c->display);

    /* libwayland's own wl_shm: the pool bookkeeping, the SIGBUS guard
     * and the format negotiation are exactly the parts where writing
     * our own would be original in no useful way. */
    if (wl_display_init_shm(c->display) < 0) { aurwl_destroy(c); return NULL; }

    c->g_compositor    = wl_global_create(c->display, &wl_compositor_interface, 5, c, bind_compositor);
    c->g_subcompositor = wl_global_create(c->display, &wl_subcompositor_interface, 1, c, bind_subcompositor);
    c->g_seat          = wl_global_create(c->display, &wl_seat_interface, 7, c, bind_seat);
    c->g_output        = wl_global_create(c->display, &wl_output_interface, 4, c, bind_output);
    c->g_xdg           = wl_global_create(c->display, &xdg_wm_base_interface, 5, c, bind_wm_base);
    c->g_ddm           = wl_global_create(c->display, &wl_data_device_manager_interface, 3, c, bind_ddm);
    c->g_deco          = wl_global_create(c->display, &zxdg_decoration_manager_v1_interface, 1, c, bind_deco);
    c->g_viewporter    = wl_global_create(c->display, &wp_viewporter_interface, 1, c, bind_viewporter);
    c->g_xdg_output    = wl_global_create(c->display, &zxdg_output_manager_v1_interface, 3, c, bind_xdg_output);
    c->g_idle_inhibit  = wl_global_create(c->display, &zwp_idle_inhibit_manager_v1_interface, 1, c, bind_idle_inhibit);
    c->g_cursor_shape  = wl_global_create(c->display, &wp_cursor_shape_manager_v1_interface, 1, c, bind_cursor_shape);

    if (!c->g_compositor || !c->g_subcompositor || !c->g_seat || !c->g_output ||
        !c->g_xdg || !c->g_ddm || !c->g_deco || !c->g_viewporter || !c->g_xdg_output ||
        !c->g_idle_inhibit || !c->g_cursor_shape) {
        fprintf(stderr, "aurwl: could not create globals\n");
        aurwl_destroy(c); return NULL;
    }
    if (textinput_init) textinput_init(c->display);
    xw_init(c);

    if (build_keymap(c) < 0)
        fprintf(stderr, "aurwl: no xkb keymap; clients will get no key translation\n");

    if (c->keymap_fd < 0) {
        /* An empty sealed memfd rather than nothing: see the comment in
         * seat_get_keyboard about what -1 costs. */
        c->keymap_fd = memfd_create("aurwl-nokeymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (c->keymap_fd >= 0)
            fcntl(c->keymap_fd, F_ADD_SEALS,
                  F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
        c->keymap_size = 0;
    }

    c->socket = wl_display_add_socket_auto(c->display);
    if (!c->socket) {
        fprintf(stderr, "aurwl: no socket in XDG_RUNTIME_DIR (%s)\n",
                getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "unset");
        aurwl_destroy(c); return NULL;
    }
    /* A client that dies mid-write would otherwise take the compositor
     * with it, and with it every other application on the machine. */
    signal(SIGPIPE, SIG_IGN);
    return c;
}

void aurwl_destroy(aurwl *c)
{
    if (!c) return;
    if (c->xkb_state) xkb_state_unref(c->xkb_state);
    if (c->keymap)    xkb_keymap_unref(c->keymap);
    if (c->xkb)       xkb_context_unref(c->xkb);
    if (c->keymap_fd >= 0) close(c->keymap_fd);
    if (c->display) {
        /* wl_display_destroy() does not tear down connected clients, so
         * every surface, buffer and store they still hold is simply
         * abandoned. On process exit that is only untidy -- but it also
         * means the shutdown path never exercises the destructors, and
         * a destructor that is never run on a normal exit is one nobody
         * notices is broken. */
        xw_shutdown(c);
        wl_display_destroy_clients(c->display);
        wl_display_destroy(c->display);
        if (textinput_shutdown) textinput_shutdown();
    }
    free(c);
}

const char *aurwl_socket(const aurwl *c) { return c ? c->socket : NULL; }
uint32_t aurwl_damage_seq(const aurwl *c) { return c ? c->damage_seq : 0; }
int aurwl_fd(const aurwl *c) { return c ? wl_event_loop_get_fd(c->loop) : -1; }

void aurwl_dispatch(aurwl *c)
{
    if (!c) return;
    wl_event_loop_dispatch(c->loop, 0);
    wl_display_flush_clients(c->display);
}

void aurwl_resize_output(aurwl *c, int w, int h)
{
    if (!c || (w == c->ow && h == c->oh)) return;
    c->ow = w; c->oh = h;
    FOR_EACH_RES(r, &c->outputs) output_send(c, r);
    FOR_EACH_RES(x, &c->xdg_outputs) xdg_output_send(c, x);
}

/* ── the window list ────────────────────────────────────────────── */

static int win_visible(const aurwl_win *w)
{
    return w->mapped && !w->dead &&
           (w->role == ROLE_TOPLEVEL || w->role == ROLE_POPUP || (w->role == ROLE_X11 && xw_listed(w)));
}

surface *aurwl_drag_icon(aurwl *c, int *dx, int *dy)
{
    if (!c || !c->drag.active || c->drag.dropped) return NULL;
    aurwl_win *i = c->drag.icon;
    if (!i || !i->store) return NULL;
    if (dx) *dx = i->off_x;
    if (dy) *dy = i->off_y;
    return aurwl_win_content(i);
}

int aurwl_cursor(aurwl *c, surface **image, int *hot_x, int *hot_y, int *shape)
{
    if (image) *image = NULL;
    if (hot_x) *hot_x = 0;
    if (hot_y) *hot_y = 0;
    if (shape) *shape = 0;
    /* Over a program's window, and only then: anywhere else the pointer is
     * the shell's own. */
    if (!c || !c->ptr_surf) return AURWL_CURSOR_DEFAULT;
    switch (c->cursor.kind) {
    case AURWL_CURSOR_HIDDEN: return AURWL_CURSOR_HIDDEN;
    case AURWL_CURSOR_SHAPE:
        if (shape) *shape = c->cursor.shape;
        return AURWL_CURSOR_SHAPE;
    case AURWL_CURSOR_IMAGE: {
        aurwl_win *w = c->cursor.surf;
        /* A cursor surface that has no picture yet is not a hidden
         * pointer: until it has one, the shell's arrow. */
        if (!w || !w->store) return AURWL_CURSOR_DEFAULT;
        int sc = w->current.scale < 1 ? 1 : w->current.scale;
        if (image) *image = w->store;
        /* The hotspot is in the surface's coordinates and moves the
         * opposite way to any attach offset made since it was set. */
        if (hot_x) *hot_x = (c->cursor.hx - (w->off_x - c->cursor.ox)) * sc;
        if (hot_y) *hot_y = (c->cursor.hy - (w->off_y - c->cursor.oy)) * sc;
        return AURWL_CURSOR_IMAGE;
    }
    default: return AURWL_CURSOR_DEFAULT;
    }
}

int aurwl_idle_inhibited(const aurwl *c)
{
    if (!c) return 0;
    struct idle_inh *ih;
    wl_list_for_each(ih, &c->inhibitors, link) {
        if (!ih->w || !ih->w->store || !ih->w->mapped) continue;
        aurwl_win *r = root_of(ih->w);       /* on screen means its window is */
        if (r && win_visible(r)) return 1;
    }
    return 0;
}

int aurwl_window_count(const aurwl *c)
{
    if (!c) return 0;
    int n = 0; aurwl_win *w;
    wl_list_for_each(w, &c->surfaces, link) if (win_visible(w)) n++;
    return n;
}
aurwl_win *aurwl_window_at(const aurwl *c, int i)
{
    if (!c || i < 0) return NULL;
    aurwl_win *w;
    wl_list_for_each(w, &c->surfaces, link) if (win_visible(w) && i-- == 0) return w;
    return NULL;
}
uint32_t    aurwl_win_id(const aurwl_win *w)     { return w ? w->id : 0; }
const char *aurwl_win_title(const aurwl_win *w)  { return w ? w->title : ""; }
const char *aurwl_win_app_id(const aurwl_win *w) { return w ? w->app_id : ""; }
/* A toplevel draws its own title bar unless it was given ours: no
 * xdg-decoration object means "clients continue to self-decorate" (the
 * protocol's words), and so does a request for client-side. Settled
 * before the first buffer, because the decoration object must exist
 * before one is attached. Firefox and GTK 3 programs are this. */
int aurwl_win_csd(const aurwl_win *w)
{
    return w && ((w->xdg_toplevel && (!w->decoration || w->deco_client)) || xw_csd(w));
}
unsigned aurwl_win_take_requests(aurwl_win *w)
{
    if (!w) return 0;
    unsigned r = w->req;
    w->req = 0;
    return r;
}
/* What the window's geometry is measured against: the flattened tree
 * when it has one, else the toplevel's own buffer. */
static const surface *content_base(aurwl_win *w)
{
    surface *f = flat_get(w);
    return f ? f : w->store;
}

/* The same question for the const callers, which must not repaint: the
 * flattened picture has the size flat_dims() gives, and that is all they
 * need to clamp against. */
static int content_dims(const aurwl_win *w, int *cw, int *ch)
{
    int fw, fh;
    if (w->store && w->role != ROLE_SUBSURFACE && has_visible_sub(w) && flat_dims(w, &fw, &fh)) {
        *cw = fw; *ch = fh;
        return 1;
    }
    if (!w->store) return 0;
    *cw = w->store->w; *ch = w->store->h;
    return 1;
}

/* THE WINDOW, NOT THE BUFFER.
 *
 * A GTK client draws its own drop shadow into a margin all the way
 * round its buffer, and says which rectangle of that buffer is the
 * actual window through xdg_surface.set_window_geometry. The shell
 * fits whatever it is handed into the space the archetype gave it, so
 * handing it the whole buffer FITTED THE SHADOW to the frame: every
 * application sat inside a uniform empty border about thirty pixels
 * wide on every edge, looking loose and wasting a tenth of the screen
 * on a 1024x600 panel. The geometry was already being recorded, and
 * was used for nothing but popup placement.
 *
 * Seen by booting the image and putting a file manager on the screen.
 *
 * The view shares the pixels -- same stride, moved origin -- so this
 * costs nothing per frame, and its address is stable per window, which
 * the shell's damage tracking relies on. (`store` is not: it is freed
 * and reallocated whenever the client resizes.) */
surface *aurwl_win_content(aurwl_win *w)
{
    if (!w || !w->store) return NULL;
    const surface *s = content_base(w);
    int gx = 0, gy = 0, gw = s->w, gh = s->h;
    if (w->has_geo) {
        gx = w->geo_x; gy = w->geo_y; gw = w->geo_w; gh = w->geo_h;
        /* Every one of these came from a client, so none of them is
         * trusted to be inside the buffer it is describing. */
        if (gx < 0 || gx >= s->w) gx = 0;
        if (gy < 0 || gy >= s->h) gy = 0;
        if (gw <= 0 || gw > s->w - gx) gw = s->w - gx;
        if (gh <= 0 || gh > s->h - gy) gh = s->h - gy;
    }
    w->view = *s;
    w->view.px = s->px + (size_t)gy * (size_t)s->stride + (size_t)gx;
    w->view.w = gw;
    w->view.h = gh;
    return &w->view;
}

/* Where that view sits inside the client's own surface. A pointer
 * position measured against the view has to have this added before it
 * is sent back, or every click lands a shadow's width up and to the
 * left of where she pressed. */
void aurwl_win_content_offset(const aurwl_win *w, int *x, int *y)
{
    int gx = 0, gy = 0, bw, bh;
    if (w && w->has_geo && content_dims(w, &bw, &bh)) {
        gx = w->geo_x; gy = w->geo_y;
        if (gx < 0 || gx >= bw) gx = 0;
        if (gy < 0 || gy >= bh) gy = 0;
    }
    if (x) *x = gx;
    if (y) *y = gy;
}
int         aurwl_win_is_popup(const aurwl_win *w) { return w && (w->role == ROLE_POPUP || xw_is_popup(w)); }
aurwl_win  *aurwl_win_parent(const aurwl_win *w)
{
    if (!w) return NULL;
    if (w->role == ROLE_X11) return xw_popup_parent(w);
    return w->parent;
}

void aurwl_win_popup_offset(const aurwl_win *w, int *x, int *y)
{
    if (w && w->role == ROLE_X11) { xw_popup_offset(w, x, y); return; }
    if (x) *x = w ? w->px : 0;
    if (y) *y = w ? w->py : 0;
}

void aurwl_win_pref_size(const aurwl_win *w, int *w_out, int *h_out)
{
    if (!w) { if (w_out) *w_out = 0; if (h_out) *h_out = 0; return; }
    /* The size the client thinks its window is: a viewport destination
     * if it set one, else the window geometry -- which is the window
     * without its shadow -- else the buffer. */
    logical_size(w, w_out, h_out);
}

void aurwl_win_logical_size(const aurwl_win *w, int *lw, int *lh)
{
    if (!w) { if (lw) *lw = 1; if (lh) *lh = 1; return; }
    logical_size(w, lw, lh);
}

void aurwl_win_configure(aurwl_win *w, int width, int height,
                         int activated, int maximized, int fullscreen)
{
    if (w && w->role == ROLE_X11) { xw_configure(w, width, height, activated, maximized, fullscreen); return; }
    if (!w || !w->xdg_toplevel) return;
    if (width  < 0) width  = 0;
    if (height < 0) height = 0;
    /* Re-sending an identical configure makes some toolkits redraw and
     * re-ack forever, which reads as a window that flickers and a CPU
     * that never idles. Dropping the duplicate is what makes calling
     * this every frame safe, which is what the shell wants to do. */
    if (w->want_w == width && w->want_h == height && w->want_act == activated &&
        w->want_max == maximized && w->want_full == fullscreen && w->acked)
        return;
    w->want_w = width; w->want_h = height;
    w->want_act = activated; w->want_max = maximized; w->want_full = fullscreen;
    configure_toplevel(w);
}

void aurwl_win_close(aurwl_win *w)
{
    if (!w) return;
    if (xw_close(w)) return;
    if (w->xdg_toplevel) xdg_toplevel_send_close(w->xdg_toplevel);
    else if (w->xdg_popup) xdg_popup_send_popup_done(w->xdg_popup);
}
void aurwl_win_kill(aurwl_win *w)
{
    if (!w || !w->res) return;
    if (xw_kill(w)) return;           /* an X window: its own program, not Xwayland and every other */
    /* Last resort, and it takes the whole client: a browser is one
     * process for many windows, so this is the "it is wedged and the
     * user is stuck" button, not the close button. */
    wl_client_destroy(wl_resource_get_client(w->res));
}

/* ── input ──────────────────────────────────────────────────────── */

static struct wl_client *win_client(aurwl_win *w)
{ return (w && w->res) ? wl_resource_get_client(w->res) : NULL; }

static void send_modifiers(aurwl *c)
{
    if (!c->xkb_state) return;
    uint32_t dep = xkb_state_serialize_mods(c->xkb_state, XKB_STATE_MODS_DEPRESSED);
    uint32_t lat = xkb_state_serialize_mods(c->xkb_state, XKB_STATE_MODS_LATCHED);
    uint32_t lck = xkb_state_serialize_mods(c->xkb_state, XKB_STATE_MODS_LOCKED);
    uint32_t grp = xkb_state_serialize_layout(c->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
    uint32_t ser = serial_of(c);
    /* To the focused client only. With no focus this used to go to
     * everyone, telling every background process the live Shift, Ctrl
     * and Alt state of a keyboard none of them was reading. */
    struct wl_client *target = win_client(c->focus);
    if (!target) return;
    FOR_EACH_RES(k, &c->keyboards)
        if (wl_resource_get_client(k) == target)
            wl_keyboard_send_modifiers(k, ser, dep, lat, lck, grp);
}

void aurwl_update_modifiers(aurwl *c) { if (c) send_modifiers(c); }

int aurwl_key_utf8(const aurwl *c, uint32_t code, char *out, size_t n)
{
    if (out && n) out[0] = 0;
    if (!c || !c->xkb_state || !out || n < 2) return 0;
    /* +8 is the offset between an evdev keycode and an X/xkb one, which
     * is the convention every keymap in the world is written against. */
    int len = xkb_state_key_get_utf8(c->xkb_state, code + 8, out, n);
    if (len < 0 || (size_t)len >= n) { out[0] = 0; return 0; }
    /* Control characters are keys, not text: Return, Tab, Escape and
     * Backspace all produce one, and a text field that inserted them
     * would show a box where the person expected something to happen. */
    if (len == 1 && ((unsigned char)out[0] < 0x20 || out[0] == 0x7F)) {
        out[0] = 0;
        return 0;
    }
    return len;
}

void aurwl_set_focus(aurwl *c, aurwl_win *w)
{
    if (!c) return;
    /* A MENU IS NEVER GIVEN THE KEYBOARD. A press inside a popup used to move it: the window she
     * was typing into got a leave, the menu an enter, and when the menu closed focus was NULL,
     * so every key was dropped until she clicked a window again. (For an X menu it was also
     * SetInputFocus on a window nobody manages.) The keyboard stays with the window the popup
     * belongs to; a popup with no window to give it to changes nothing. */
    for (int i = 0; w && aurwl_win_is_popup(w); i++) {
        aurwl_win *p = aurwl_win_parent(w);
        if (!p || i > PARENT_MAX) return;
        w = p;
    }
    if (c->focus == w) return;
    if (wishhook_reset) wishhook_reset();            /* a new field: the word so far is not hers */
    uint32_t ser = serial_of(c);

    if (c->focus) {
        struct wl_client *old = win_client(c->focus);
        FOR_EACH_RES(k, &c->keyboards)
            if (wl_resource_get_client(k) == old)
                wl_keyboard_send_leave(k, ser, c->focus->res);
    }
    c->focus = w;
    xw_focus(c, w);                                  /* X input focus follows, or is taken away */
    if (!w) { if (textinput_focus) textinput_focus(NULL, NULL); return; }

    struct wl_client *nw = win_client(w);
    struct wl_array keys; wl_array_init(&keys);          /* nothing held */
    FOR_EACH_RES(k, &c->keyboards)
        if (wl_resource_get_client(k) == nw)
            wl_keyboard_send_enter(k, ser, w->res, &keys);
    wl_array_release(&keys);
    send_modifiers(c);

    /* A newly focused client must learn what is on the clipboard before
     * its paste menu is drawn, not after the user tries to use it. */
    FOR_EACH_RES(dev, &c->devices)
        if (wl_resource_get_client(dev) == nw) send_selection_to(c, dev);

    /* After the keyboard has entered, text input follows the keyboard. */
    if (textinput_focus) textinput_focus(nw, w->res);
}

aurwl_win *aurwl_focus(const aurwl *c) { return c ? c->focus : NULL; }

/* The pointer leaves whatever surface it is on. */
static void ptr_leave(aurwl *c)
{
    aurwl_win *s = c->ptr_surf;
    c->ptr_surf = NULL;
    cursor_reset(c);              /* whatever it asked for was for that surface */
    if (!s) return;
    struct wl_client *cl = win_client(s);
    uint32_t ser = serial_of(c);
    FOR_EACH_RES(p, &c->pointers)
        if (wl_resource_get_client(p) == cl) {
            wl_pointer_send_leave(p, ser, s->res);
            if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                wl_pointer_send_frame(p);
        }
}

/* A SURFACE IS NOT A WINDOW.
 *
 * `w` is the window the shell found under the pointer, and (sx,sy) is the
 * point in that window's surface coordinates. The surface that should
 * hear about it is not necessarily the window's own: Firefox's page is a
 * subsurface, and a click on it sent to the toplevel is a click on the
 * frame around it. So the point is resolved down the window's tree to
 * the surface actually under it, and enter, leave, motion and buttons
 * all go there, in THAT surface's coordinates.
 *
 * While a button is held the pointer stays on the surface it went down
 * on, as the protocol's implicit grab requires -- dragging a selection
 * out of the page into the frame must not tell the page the pointer has
 * left, or the selection stops. */
void aurwl_pointer_motion(aurwl *c, aurwl_win *w, int sx, int sy, uint32_t t)
{
    if (!c) return;
    if (w && !win_visible(w)) w = NULL;
    c->ptr_rx = sx; c->ptr_ry = sy;
    drag_validate(c);
    if (c->drag.active && !c->drag.dropped) {       /* the pointer belongs to the drag */
        c->ptr_focus = w;
        drag_motion(c, w, sx, sy, t);
        return;
    }
    /* The shell does not pass on a release that happens off every window,
     * so a grab must not outlive the pointer leaving them all. */
    if (!w) c->btns_down = 0;

    aurwl_win *hit = NULL;
    int lx = sx, ly = sy;
    if (w) {
        if (c->btns_down > 0 && c->ptr_surf && c->ptr_focus == w && root_of(c->ptr_surf) == w) {
            int ax = 0, ay = 0, i = 0;
            for (const aurwl_win *a = c->ptr_surf; a->role == ROLE_SUBSURFACE && a->parent; a = a->parent) {
                ax += a->sub_x; ay += a->sub_y;
                if (++i > PARENT_MAX) break;
            }
            hit = c->ptr_surf; lx = sx - ax; ly = sy - ay;
        } else {
            hit = pick(w, sx, sy, &lx, &ly, 0);
            /* Nothing takes input there -- a region that excludes the
             * point, say. The shell has already decided the pointer is
             * in this window, so the window it is. */
            if (!hit) { hit = w; lx = sx; ly = sy; }
        }
    }

    if (c->ptr_surf != hit) {
        ptr_leave(c);
        c->ptr_surf = hit;
        if (hit) {
            xw_raise(c, root_of(hit));          /* an X window must be the top one before Xwayland sees the pointer in it */
            struct wl_client *nw = win_client(hit);
            uint32_t ser = serial_of(c);
            c->ptr_enter_serial = ser;
            FOR_EACH_RES(p, &c->pointers)
                if (wl_resource_get_client(p) == nw) {
                    wl_pointer_send_enter(p, ser, hit->res,
                                          wl_fixed_from_int(lx), wl_fixed_from_int(ly));
                    if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                        wl_pointer_send_frame(p);
                }
        }
    }
    c->ptr_focus = w;
    c->ptr_x = lx; c->ptr_y = ly;
    if (!hit) return;
    struct wl_client *cl = win_client(hit);
    FOR_EACH_RES(p, &c->pointers)
        if (wl_resource_get_client(p) == cl) {
            wl_pointer_send_motion(p, t, wl_fixed_from_int(lx), wl_fixed_from_int(ly));
            if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                wl_pointer_send_frame(p);
        }
}

void aurwl_pointer_button(aurwl *c, uint32_t button, int pressed, uint32_t t)
{
    if (pressed && wishhook_reset) wishhook_reset();
    if (!c) return;
    /* Counted for the implicit grab above. Bounded below: a release the
     * shell sends for a press that went to the shell itself must not
     * leave the count negative and the next real press unbalanced. */
    if (pressed) { if (c->btns_down < 64) c->btns_down++; }
    else if (c->btns_down > 0) c->btns_down--;
    drag_validate(c);
    if (c->drag.active && !c->drag.dropped) {
        if (!pressed) drag_release(c);
        return;
    }
    if (!c->ptr_surf) return;
    struct wl_client *cl = win_client(c->ptr_surf);
    uint32_t ser = serial_of(c);
    if (pressed) c->btn_serial = ser;       /* the one a drag must quote */
    FOR_EACH_RES(p, &c->pointers)
        if (wl_resource_get_client(p) == cl) {
            wl_pointer_send_button(p, ser, t, button,
                pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
            if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                wl_pointer_send_frame(p);
        }
}

void aurwl_pointer_axis(aurwl *c, int horizontal, double step, uint32_t t)
{
    if (!c || !c->ptr_surf) return;
    if (c->drag.active && !c->drag.dropped) return;
    struct wl_client *cl = win_client(c->ptr_surf);
    uint32_t axis = horizontal ? WL_POINTER_AXIS_HORIZONTAL_SCROLL
                               : WL_POINTER_AXIS_VERTICAL_SCROLL;
    FOR_EACH_RES(p, &c->pointers)
        if (wl_resource_get_client(p) == cl) {
            int ver = wl_resource_get_version(p);
            if (ver >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
                wl_pointer_send_axis_source(p, WL_POINTER_AXIS_SOURCE_WHEEL);
            /* A wheel notch is 15 degrees, which the protocol expresses
             * as 10 units of scroll; a toolkit that only reads discrete
             * steps ignores the fixed value entirely, so both go out. */
            if (ver >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
                wl_pointer_send_axis_discrete(p, axis, step > 0 ? 1 : -1);
            wl_pointer_send_axis(p, t, axis, wl_fixed_from_double(step * 10.0));
            if (ver >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(p);
        }
}

int aurwl_key(aurwl *c, uint32_t code, int pressed, uint32_t t)
{
    if (!c) return 0;
    if (c->focus && wishhook_key) wishhook_key(c, code, pressed);   /* before the keymap state moves */
    /* Escape ends a drag without dropping (and is still delivered). */
    if (pressed && code == 1 && c->drag.active && !c->drag.dropped) drag_cancel(c);
    /* Modifier state is tracked whether or not anyone is listening: the
     * shell swallows some chords itself, and if those did not update
     * xkb the client's idea of Shift would drift from the user's hand. */
    if (c->xkb_state)
        xkb_state_update_key(c->xkb_state, code + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

    if (!c->focus) return 0;
    struct wl_client *cl = win_client(c->focus);
    uint32_t ser = serial_of(c);
    int sent = 0;
    if (c->focus->role == ROLE_X11) XDBG("key %u %s to an X window", code, pressed ? "down" : "up");
    FOR_EACH_RES(k, &c->keyboards)
        if (wl_resource_get_client(k) == cl) {
            wl_keyboard_send_key(k, ser, t, code,
                pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
            sent = 1;
        }
    if (sent) send_modifiers(c);
    return sent;
}

/* ── frame ──────────────────────────────────────────────────────── */

void aurwl_frame_done(aurwl *c, uint32_t t)
{
    if (!c) return;
    aurwl_win *w;
    wl_list_for_each(w, &c->surfaces, link) {
        struct wl_resource *cb, *tmp;
        wl_resource_for_each_safe(cb, tmp, &w->frame_cbs) {
            wl_callback_send_done(cb, t);
            wl_resource_destroy(cb);
        }
    }
    wl_display_flush_clients(c->display);
}

/* ── launching ──────────────────────────────────────────────────── */

pid_t aurwl_spawn(aurwl *c, const char *const argv[])
{
    if (!c || !argv || !argv[0] || !argv[0][0]) return -1;
    int cap = (int)(sizeof c->kids / sizeof c->kids[0]);
    if (c->n_kids >= cap) aurwl_reap(c);
    if (c->n_kids >= cap) {
        /* Sixty-four applications running at once is not a desktop, it
         * is a fault. Refusing is better than the alternative this used
         * to do, which was to start it anyway and forget the pid --
         * leaving a process nothing would ever collect. */
        fprintf(stderr, "aurwl: too many programs already running; "
                        "not starting %s\n", argv[0]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* A new session, so a crashing application cannot take the
         * shell's controlling terminal down with it, and so a Ctrl-C
         * meant for one program is not delivered to the desktop. */
        setsid();
        signal(SIGPIPE, SIG_DFL);
        setenv("WAYLAND_DISPLAY", c->socket, 1);
        if (c->x.enabled) {        /* X programs find Xwayland (started when they connect) */
            char d[16]; snprintf(d, sizeof d, ":%d", c->x.display);
            setenv("DISPLAY", d, 1);
        } else unsetenv("DISPLAY");
        /* Toolkits pick a backend from the environment before they look
         * at what is actually available, and the guesses they make on a
         * machine with no X server are all wrong in different ways. */
        setenv("GDK_BACKEND", "wayland", 1);
        setenv("QT_QPA_PLATFORM", "wayland", 1);
        setenv("SDL_VIDEODRIVER", "wayland", 1);
        setenv("CLUTTER_BACKEND", "wayland", 1);
        setenv("MOZ_ENABLE_WAYLAND", "1", 1);
        setenv("XDG_SESSION_TYPE", "wayland", 1);
        setenv("XDG_CURRENT_DESKTOP", "AurOS", 1);
        /* The pointer is drawn large (cursor.c); a program that loads a
         * cursor theme itself picks its size from this, and 48 is the
         * size the shell draws without scaling at normal text size. */
        setenv("XCURSOR_SIZE", "48", 1);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    c->kids[c->n_kids++] = pid;
    return pid;
}

/* Collect the ones WE started, and only those.
 *
 * This used to be waitpid(-1), which collects any child of this
 * process -- including children the compositor knows nothing about.
 * The shell is one process: when src/aurshell/net.c runs nmcli to look
 * for wifi, that child is the shell's too, and a compositor reaping it
 * out from under the module that started it means that module can
 * neither learn how it ended nor safely signal it afterwards, because
 * by then the number it is holding may belong to somebody else.
 *
 * Each subsystem waits for its own. Nothing is left behind, because
 * aurwl_spawn() now refuses rather than forgetting a pid. */
void aurwl_reap(aurwl *c)
{
    if (!c) return;
    for (int i = c->n_kids - 1; i >= 0; i--) {
        pid_t p = waitpid(c->kids[i], NULL, WNOHANG);
        /* >0 exited; <0 means it is not ours any more (already
         * collected, or never was), and either way the slot is stale. */
        if (p != 0) {
            pid_t done = c->kids[i];
            c->kids[i] = c->kids[--c->n_kids];
            xw_reaped(c, done);        /* Xwayland has gone: the next X program starts a new one */
        }
    }
}

const char *aurwl_x_display(aurwl *c)
{
    static char buf[16];
    if (!c || !c->x.enabled) return NULL;
    snprintf(buf, sizeof buf, ":%d", c->x.display);
    return buf;
}
