/* textinput.c — see textinput.h. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include "text-input-server.h"
#include "textinput.h"

#define MAX_PER_CLIENT 4
#define MAX_TOTAL      64

typedef struct { int enabled; uint32_t hint, purpose; } ti_state;

struct ti {
    struct wl_resource *res;
    struct wl_client   *client;
    struct wl_list      link;
    int                 entered;       /* the focus surface is ours to speak about */
    ti_state            pend, cur;     /* double-buffered: commit copies */
    uint32_t            commits;
};

static struct wl_list     tis;
static int                inited, n_total;
static struct wl_client  *f_client;
static struct wl_resource *f_surf;
static struct wl_listener f_gone;

static void zero_state(struct ti *t)
{
    memset(&t->pend, 0, sizeof t->pend);
    memset(&t->cur, 0, sizeof t->cur);
}

/* ── requests ───────────────────────────────────────────────────── */

static struct ti *ti_of(struct wl_resource *r) { return wl_resource_get_user_data(r); }

static void ti_destroy_req(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; wl_resource_destroy(r); }

static void ti_enable(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl;
    struct ti *t = ti_of(r);
    /* Only on the surface we told it about (enter), and enabling resets
     * everything it said before (the protocol's own rule). */
    if (!t || !t->entered) return;
    memset(&t->pend, 0, sizeof t->pend);
    t->pend.enabled = 1;
}

static void ti_disable(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl;
    struct ti *t = ti_of(r);
    if (!t || !t->entered) return;
    memset(&t->pend, 0, sizeof t->pend);
}

/* The surrounding text is what she has typed around the cursor. It is
 * never copied, stored or looked at. */
static void ti_surrounding(struct wl_client *cl, struct wl_resource *r, const char *text,
                           int32_t cursor, int32_t anchor)
{ (void)cl; (void)r; (void)text; (void)cursor; (void)anchor; }

static void ti_cause(struct wl_client *cl, struct wl_resource *r, uint32_t cause)
{ (void)cl; (void)r; (void)cause; }

static void ti_content_type(struct wl_client *cl, struct wl_resource *r, uint32_t hint, uint32_t purpose)
{
    (void)cl;
    struct ti *t = ti_of(r);
    if (!t || !t->entered) return;
    t->pend.hint = hint;
    t->pend.purpose = purpose;
}

static void ti_cursor_rect(struct wl_client *cl, struct wl_resource *r, int32_t x, int32_t y,
                           int32_t w, int32_t h)
{ (void)cl; (void)r; (void)x; (void)y; (void)w; (void)h; }

static void ti_commit(struct wl_client *cl, struct wl_resource *r)
{
    (void)cl;
    struct ti *t = ti_of(r);
    if (!t || !t->entered) return;
    t->commits++;
    t->cur = t->pend;
    /* One text input enabled at a time on the seat: a second enable is
     * ignored (the protocol's rule, and it means two objects cannot
     * disagree about which field she is in). */
    if (t->cur.enabled) {
        struct ti *o;
        wl_list_for_each(o, &tis, link)
            if (o != t && o->client == t->client && o->entered && o->cur.enabled) {
                t->cur.enabled = t->pend.enabled = 0;
                break;
            }
    }
}

static const struct zwp_text_input_v3_interface ti_impl = {
    .destroy = ti_destroy_req, .enable = ti_enable, .disable = ti_disable,
    .set_surrounding_text = ti_surrounding, .set_text_change_cause = ti_cause,
    .set_content_type = ti_content_type, .set_cursor_rectangle = ti_cursor_rect,
    .commit = ti_commit,
};

static void ti_resource_gone(struct wl_resource *r)
{
    struct ti *t = ti_of(r);
    if (!t) return;
    wl_list_remove(&t->link);
    n_total--;
    free(t);
}

/* ── focus ──────────────────────────────────────────────────────── */

static void leave_current(int tell)
{
    struct ti *t;
    wl_list_for_each(t, &tis, link)
        if (t->entered) {
            if (tell && f_surf) zwp_text_input_v3_send_leave(t->res, f_surf);
            t->entered = 0;
            zero_state(t);
        }
    if (f_surf) { wl_list_remove(&f_gone.link); wl_list_init(&f_gone.link); }
    f_surf = NULL;
    f_client = NULL;
}

/* The focused surface is being destroyed: nothing can be said to a dead
 * object, so forget it without a leave. */
static void surf_gone(struct wl_listener *l, void *data)
{
    (void)l; (void)data;
    leave_current(0);
}

static void enter(struct ti *t)
{
    t->entered = 1;
    zero_state(t);
    zwp_text_input_v3_send_enter(t->res, f_surf);
}

void textinput_focus(struct wl_client *client, struct wl_resource *surface)
{
    if (!inited) return;
    if (!client || !surface) { client = NULL; surface = NULL; }
    if (client == f_client && surface == f_surf) return;
    leave_current(1);
    if (!surface) return;
    f_client = client;
    f_surf = surface;
    f_gone.notify = surf_gone;
    wl_resource_add_destroy_listener(surface, &f_gone);
    struct ti *t;
    wl_list_for_each(t, &tis, link)
        if (t->client == client) enter(t);
}

/* ── the global ─────────────────────────────────────────────────── */

static void mgr_destroy(struct wl_client *cl, struct wl_resource *r)
{ (void)cl; wl_resource_destroy(r); }

static void mgr_get_text_input(struct wl_client *cl, struct wl_resource *mgr, uint32_t id,
                               struct wl_resource *seat)
{
    (void)seat;                              /* there is one seat */
    int mine = 0;
    struct ti *o;
    wl_list_for_each(o, &tis, link) mine += o->client == cl;
    if (mine >= MAX_PER_CLIENT || n_total >= MAX_TOTAL) {
        wl_resource_post_error(mgr, WL_DISPLAY_ERROR_NO_MEMORY, "too many text inputs");
        return;
    }
    struct ti *t = calloc(1, sizeof *t);
    if (!t) { wl_client_post_no_memory(cl); return; }
    t->client = cl;
    t->res = wl_resource_create(cl, &zwp_text_input_v3_interface, wl_resource_get_version(mgr), id);
    if (!t->res) { free(t); wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(t->res, &ti_impl, t, ti_resource_gone);
    wl_list_insert(&tis, &t->link);
    n_total++;
    /* A client that asks while it already has the keyboard is told at once. */
    if (cl == f_client && f_surf) enter(t);
}

static const struct zwp_text_input_manager_v3_interface mgr_impl = {
    .destroy = mgr_destroy, .get_text_input = mgr_get_text_input,
};

static void bind_manager(struct wl_client *cl, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *r = wl_resource_create(cl, &zwp_text_input_manager_v3_interface, (int)version, id);
    if (!r) { wl_client_post_no_memory(cl); return; }
    wl_resource_set_implementation(r, &mgr_impl, NULL, NULL);
}

void textinput_init(struct wl_display *d)
{
    wl_list_init(&tis);
    wl_list_init(&f_gone.link);
    n_total = 0;
    f_client = NULL;
    f_surf = NULL;
    inited = wl_global_create(d, &zwp_text_input_manager_v3_interface, 1, NULL, bind_manager) != NULL;
}

/* After the display (and with it every text input) is gone. */
void textinput_shutdown(void)
{
    inited = 0;
    n_total = 0;
    f_client = NULL;
    f_surf = NULL;
}

/* ── what the program with the keyboard has said ─────────────────── */

static int sensitive(const ti_state *s)
{
    /* password and PIN say so; the hints say "hide it" or "do not store
     * it"; a terminal may be at a password prompt that echoes nothing;
     * and a purpose we have never heard of is not one we can call safe. */
    if (s->purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD ||
        s->purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN ||
        s->purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL ||
        s->purpose > ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL) return 1;
    return (s->hint & (ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT |
                       ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA)) != 0;
}

int textinput_state(void)
{
    if (!inited || !f_surf) return TI_UNKNOWN;
    int any = 0, sens = 0;
    struct ti *t;
    wl_list_for_each(t, &tis, link)
        if (t->entered && t->cur.enabled) { any = 1; sens |= sensitive(&t->cur); }
    return !any ? TI_UNKNOWN : sens ? TI_SENSITIVE : TI_PLAIN;
}
