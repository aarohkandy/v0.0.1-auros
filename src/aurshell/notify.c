/* notify.c — see notify.h. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <dbus/dbus.h>

#include "notify.h"
#include "draw.h"
#include "foot.h"

/* ── what is on screen ──────────────────────────────────────────── */

typedef struct {
    uint32_t id;
    char     app[NOTIFY_APP];
    char     summary[NOTIFY_SUMMARY];
    char     body[NOTIFY_BODY];
    /* TWO DIFFERENT THINGS, and they were one.
     *
     * `urgent` is how it is DRAWN, and a sender may ask for it: at
     * worst a program makes its own card look important, which is
     * rude and not dangerous.
     *
     * `sticky` is the shell's own warning -- the battery -- and a
     * sender may NOT ask for that. It was the same flag, read straight
     * out of the hints dictionary, so any program could make a card
     * that nothing else could push aside. Only the shell sets sticky,
     * and the shell only ever says things she must act on: a sticky
     * one she has not seen is never dropped to make room. */
    int      urgent;
    int      sticky;
    /* She has seen it: she pressed OK, or opened Messages. A seen one
     * is no longer on the card, and is still in the list. */
    int      seen;
    /* The tag replace-in-place notices carry instead of an id: the
     * volume, a download. The same app and tag is the same card. */
    char     tag[48];
    /* Pressing this card opens a program, by its id in the app table,
     * rather than only putting the card away. Only the shell sets it:
     * "VLC is ready -- press to open". Empty for every other card. */
    char     open_id[32];
} note;

/* What the last press asked to open, until the host takes it. */
static char take_open[32];

#define BUS_NAME  "org.freedesktop.Notifications"
#define BUS_PATH  "/org/freedesktop/Notifications"

/* Trying again costs a connection attempt, so not every frame. */
#define RETRY_MS     1000

static struct {
    DBusConnection *conn;
    int      owned;
    /* The bus address this connection was made on. Compared against
     * the environment every time, because the shell MOVES: it starts
     * on the private bus its unit makes and adopts logind's a second
     * or two later, and that move can be triggered from anywhere --
     * the frame loop, or a spawn (see session.c). A server that is
     * still on the old bus is a server nothing can find, so this is
     * checked here rather than relying on whoever moved to say so. */
    char     addr[512];
    int32_t  next_try;
    int      state;                /* NS_* */
    DBusPendingCall *pending;
    int32_t  deadline;
    /* Oldest first; the newest is n[count - 1]. */
    note     n[NOTIFY_KEEP];
    int      count;
    uint32_t next_id;
    int      list_open;           /* the Messages list is on screen     */
    int      first;               /* ...starting at this message        */
} N = { NULL, 0, {0}, 0, 0, NULL, 0, {{0}}, 0, 1, 0, 0 };

static int32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* Copy, and make it printable.
 *
 * Every string here came from another program, so none of it may be
 * trusted to be short, to be one line, or to be free of the control
 * characters that would otherwise be drawn as boxes or would run the
 * text off the card. The body of a notification is also allowed by the
 * specification to contain a little markup -- <b>, <i>, <a href> --
 * which this shell does not render and must not show raw. */
/* The tags the specification actually permits in a body: <b>, <i>,
 * <u>, <a href=...> and <img ...>, and their closers. Nothing else in
 * a notification is markup. */
static int markup_tag(const char *p, size_t *len)
{
    static const char *TAG[] = { "b", "i", "u", "a", "img" };
    const char *q = p + 1;                       /* past the '<' */
    if (*q == '/') q++;
    size_t nlen = 0;
    while (q[nlen] && (q[nlen] == '-' ||
                       (q[nlen] >= 'a' && q[nlen] <= 'z') ||
                       (q[nlen] >= 'A' && q[nlen] <= 'Z'))) nlen++;
    if (!nlen || nlen > 3) return 0;
    int known = 0;
    for (size_t i = 0; i < sizeof TAG / sizeof TAG[0]; i++)
        if (strlen(TAG[i]) == nlen && !strncasecmp(q, TAG[i], nlen)) known = 1;
    if (!known) return 0;
    /* It only counts as a tag if it is CLOSED. An unterminated one is
     * a less-than sign somebody typed. */
    const char *close = strchr(q + nlen, '>');
    if (!close) return 0;
    *len = (size_t)(close - p) + 1;
    return 1;
}

/* Copy, and make it printable.
 *
 * Every string here came from another program, so none of it may be
 * trusted to be short, to be one line, or to be free of the control
 * characters that would otherwise be drawn as boxes or would run the
 * text off the card.
 *
 * ONLY REAL MARKUP IS REMOVED. The first version dropped everything
 * between a '<' and the next '>', and never reset that state at the
 * end of the string -- so a '<' with no '>' after it ate the whole
 * remainder. "Saved <report 2024>.pdf" arrived as "Saved .pdf", which
 * deletes the one word the notification exists to say, and "Happy
 * birthday <3" arrived as "Happy birthday". The server does not even
 * advertise "body-markup", so senders are required to send plain text
 * and angle brackets in it are hers, not a toolkit's.
 */
static void sane(char *dst, size_t n, const char *src)
{
    if (!n) return;
    dst[0] = 0;
    if (!src) return;
    size_t o = 0;
    for (const char *p = src; *p && o + 1 < n; ) {
        if (*p == '<') {
            size_t taglen = 0;
            if (markup_tag(p, &taglen)) { p += taglen; continue; }
        }
        unsigned char ch = (unsigned char)*p;
        /* Newlines and tabs become spaces: a card is a fixed shape and
         * a program that sends ten lines must not be able to decide
         * how much of the screen it gets. */
        if (ch == '\n' || ch == '\t' || ch == '\r') { ch = ' '; p++; }
        else if (ch < 0x20 || ch == 0x7F) { p++; continue; }
        else if (ch >= 0x80) {
            /* A MULTI-BYTE CHARACTER GOES IN WHOLE OR NOT AT ALL.
             * Cutting one in half leaves the font engine two bytes it
             * cannot decode and draws a pair of empty boxes at the end
             * of every long summary in a language that is not English
             * -- on a product whose last commit was about being able
             * to draw those languages at all. */
            size_t len = (ch >= 0xF0) ? 4 : (ch >= 0xE0) ? 3 :
                         (ch >= 0xC0) ? 2 : 1;
            if (len == 1) { p++; continue; }        /* a stray trail byte */
            size_t have = 0;
            while (have < len && p[have]) have++;
            if (have < len) break;                  /* truncated at source */
            if (o + len >= n) break;                /* no room: stop clean */
            for (size_t i = 0; i < len; i++) dst[o++] = p[i];
            p += len;
            continue;
        }
        else p++;
        /* Runs of space collapse, so padding cannot be used to push
         * the readable part off the end. */
        if (ch == ' ' && (o == 0 || dst[o - 1] == ' ')) continue;
        dst[o++] = (char)ch;
    }
    while (o && dst[o - 1] == ' ') o--;
    dst[o] = 0;
}

/* ── the queue ──────────────────────────────────────────────────── */

static int find_id(uint32_t id)
{
    for (int i = 0; i < N.count; i++) if (N.n[i].id == id) return i;
    return -1;
}

static void drop_at(int i)
{
    if (i < 0 || i >= N.count) return;
    for (int k = i; k + 1 < N.count; k++) N.n[k] = N.n[k + 1];
    N.count--;
}

static void closed_signal(uint32_t id, uint32_t reason)
{
    if (!N.conn) return;
    DBusMessage *sig = dbus_message_new_signal(BUS_PATH, BUS_NAME,
                                               "NotificationClosed");
    if (!sig) return;
    dbus_message_append_args(sig, DBUS_TYPE_UINT32, &id,
                                  DBUS_TYPE_UINT32, &reason,
                                  DBUS_TYPE_INVALID);
    dbus_connection_send(N.conn, sig, NULL);
    dbus_message_unref(sig);
}

static uint32_t mint_id(void)
{
    for (int tries = 0; tries < NOTIFY_KEEP + 2; tries++) {
        uint32_t id = N.next_id++;
        if (N.next_id == 0) N.next_id = 1;      /* 0 means "new" */
        if (id && find_id(id) < 0) return id;
    }
    return N.next_id++;                         /* cannot happen */
}

/* She has seen everything waiting: OK, or the Messages list. The
 * sending program is told (2 = dismissed by her), so one that is
 * waiting to know can stop waiting. The words stay in the list. */
static void see_all(void)
{
    for (int i = 0; i < N.count; i++)
        if (!N.n[i].seen) { N.n[i].seen = 1; closed_signal(N.n[i].id, 2); }
}

/* The newest one she has not seen: what the card shows. -1 for none. */
static int card_note(void)
{
    for (int i = N.count - 1; i >= 0; i--) if (!N.n[i].seen) return i;
    return -1;
}

static uint32_t put(const char *app, uint32_t replaces, const char *tag,
                    const char *summary, const char *body, int urgent, int sticky)
{
    int slot = -1;
    uint32_t id;
    char a[NOTIFY_APP], t[sizeof N.n[0].tag];
    sane(a, sizeof a, app);
    sane(t, sizeof t, tag);

    if (replaces) slot = find_id(replaces);
    /* REPLACE IN PLACE. The volume and a download say the same thing
     * again and again; some programs keep the id they were given, the
     * rest send a tag. Either way it is the card they already have,
     * changed -- not one more card. */
    if (slot < 0 && t[0])
        for (int i = 0; i < N.count; i++)
            if (!strcmp(N.n[i].tag, t) && !strcmp(N.n[i].app, a)) { slot = i; break; }

    if (slot >= 0) {
        /* It moves to the front, because it is the newest thing said,
         * and it is unseen again, because it says something new. */
        note keep = N.n[slot];
        drop_at(slot);
        slot = N.count++;
        N.n[slot] = keep;
        id = keep.id;
    } else {
        /* A replaces_id naming nothing is a NEW notification, and it
         * gets a new id. It used to be given the id it asked for,
         * without advancing the counter -- so `replaces_id=1` from any
         * program collided with the first id this shell hands out, two
         * live cards shared it, find_id() returned the wrong one, and
         * CloseNotification closed somebody else's. One program taking
         * over another's notification, reachable by accident. */
        id = mint_id();
        if (N.count >= NOTIFY_KEEP) {
            /* Full. The oldest she has seen goes; then the oldest a
             * program sent; never a warning of the shell's own that
             * she has not seen -- the battery warning was once pushed
             * off by four downloads finishing. */
            int victim = -1;
            for (int i = 0; i < N.count && victim < 0; i++)
                if (N.n[i].seen) victim = i;
            for (int i = 0; i < N.count && victim < 0; i++)
                if (!N.n[i].sticky) victim = i;
            if (victim < 0 && sticky) victim = 0;   /* shell over shell */
            if (victim < 0) {
                /* Every slot is something she must act on, and this is
                 * not. Tell the sender at once rather than silently
                 * dropping it or pushing a warning out. */
                closed_signal(id, 4);
                return id;
            }
            if (!N.n[victim].seen) closed_signal(N.n[victim].id, 4);
            drop_at(victim);
        }
        slot = N.count++;
        memset(&N.n[slot], 0, sizeof N.n[slot]);
        N.n[slot].id = id;
        memcpy(N.n[slot].app, a, sizeof a);
        memcpy(N.n[slot].tag, t, sizeof t);
    }

    note *n = &N.n[slot];
    sane(n->summary, sizeof n->summary, summary);
    sane(n->body,    sizeof n->body,    body);
    n->urgent = urgent;
    n->sticky = sticky;
    /* Said while she is looking at the list: she has seen it there. */
    n->seen = N.list_open;

    /* Something with no words at all is not a notification; it is a
     * card she cannot read and cannot tell apart from the next one. */
    if (!n->summary[0] && !n->body[0])
        snprintf(n->summary, sizeof n->summary, "%s",
                 n->app[0] ? n->app : "A program has a message");
    return id;
}

void notify_local(const char *summary, const char *body)
{
    /* Sticky: never pushed out of the list unseen. Only the shell may
     * do this, and the shell only says things she has to act on -- a
     * battery that is about to run out. */
    put("This computer", 0, NULL, summary, body, 1, 1);
}

void notify_local_open(const char *summary, const char *body, const char *app_id)
{
    /* Not sticky: a program being ready is good news, not something she
     * must act on. The program is on her home screen either way; this
     * is the shortcut, not the only door. */
    uint32_t id = put("This computer", 0, NULL, summary, body, 0, 0);
    int i = find_id(id);
    if (i >= 0 && app_id)
        snprintf(N.n[i].open_id, sizeof N.n[i].open_id, "%s", app_id);
}

int notify_take_open(char *out, size_t n)
{
    if (!take_open[0]) return 0;
    if (out && n) snprintf(out, n, "%s", take_open);
    take_open[0] = 0;
    return 1;
}

/* ── the bus ────────────────────────────────────────────────────── */

static const char *INTROSPECT =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    " \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    " <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "  <method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method>\n"
    " </interface>\n"
    " <interface name=\"org.freedesktop.Notifications\">\n"
    "  <method name=\"Notify\">\n"
    "   <arg type=\"s\" name=\"app_name\" direction=\"in\"/>\n"
    "   <arg type=\"u\" name=\"replaces_id\" direction=\"in\"/>\n"
    "   <arg type=\"s\" name=\"app_icon\" direction=\"in\"/>\n"
    "   <arg type=\"s\" name=\"summary\" direction=\"in\"/>\n"
    "   <arg type=\"s\" name=\"body\" direction=\"in\"/>\n"
    "   <arg type=\"as\" name=\"actions\" direction=\"in\"/>\n"
    "   <arg type=\"a{sv}\" name=\"hints\" direction=\"in\"/>\n"
    "   <arg type=\"i\" name=\"expire_timeout\" direction=\"in\"/>\n"
    "   <arg type=\"u\" name=\"id\" direction=\"out\"/>\n"
    "  </method>\n"
    "  <method name=\"CloseNotification\">\n"
    "   <arg type=\"u\" name=\"id\" direction=\"in\"/>\n"
    "  </method>\n"
    "  <method name=\"GetCapabilities\">\n"
    "   <arg type=\"as\" name=\"caps\" direction=\"out\"/>\n"
    "  </method>\n"
    "  <method name=\"GetServerInformation\">\n"
    "   <arg type=\"s\" name=\"name\" direction=\"out\"/>\n"
    "   <arg type=\"s\" name=\"vendor\" direction=\"out\"/>\n"
    "   <arg type=\"s\" name=\"version\" direction=\"out\"/>\n"
    "   <arg type=\"s\" name=\"spec_version\" direction=\"out\"/>\n"
    "  </method>\n"
    "  <signal name=\"NotificationClosed\">\n"
    "   <arg type=\"u\"/><arg type=\"u\"/>\n"
    "  </signal>\n"
    "  <signal name=\"ActionInvoked\">\n"
    "   <arg type=\"u\"/><arg type=\"s\"/>\n"
    "  </signal>\n"
    " </interface>\n"
    "</node>\n";

static void reply_str(DBusConnection *c, DBusMessage *m, const char *s)
{
    DBusMessage *r = dbus_message_new_method_return(m);
    if (!r) return;
    dbus_message_append_args(r, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID);
    dbus_connection_send(c, r, NULL);
    dbus_message_unref(r);
}

/* The two hints this server reads, out of a{sv}.
 *
 * "urgency": anything that is not a byte, or is not there at all, is
 * "normal", which is what the specification says and also the only
 * safe reading of a program we do not control.
 *
 * The replace-in-place tag: notify-send's volume and brightness
 * helpers, and most of what sends progress, name the card they mean
 * with one of these instead of keeping the id they were given. */
static int read_hints(DBusMessageIter *hints, char *tag, size_t tagn)
{
    static const char *TAGS[] = { "x-canonical-private-synchronous",
                                  "x-dunst-stack-tag", "synchronous" };
    int urgency = 1;
    if (tagn) tag[0] = 0;
    if (dbus_message_iter_get_arg_type(hints) != DBUS_TYPE_ARRAY) return urgency;
    DBusMessageIter arr;
    dbus_message_iter_recurse(hints, &arr);
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter e, v;
        dbus_message_iter_recurse(&arr, &e);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_STRING)
            dbus_message_iter_get_basic(&e, &key);
        dbus_message_iter_next(&e);
        if (key && dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_VARIANT) {
            dbus_message_iter_recurse(&e, &v);
            int t = dbus_message_iter_get_arg_type(&v);
            if (!strcmp(key, "urgency") && t == DBUS_TYPE_BYTE) {
                unsigned char u = 1;
                dbus_message_iter_get_basic(&v, &u);
                urgency = (int)u;
            }
            for (size_t i = 0; i < sizeof TAGS / sizeof TAGS[0]; i++)
                if (!strcmp(key, TAGS[i]) && t == DBUS_TYPE_STRING && tagn) {
                    const char *val = NULL;
                    dbus_message_iter_get_basic(&v, &val);
                    snprintf(tag, tagn, "%s", val ? val : "");
                }
        }
        dbus_message_iter_next(&arr);
    }
    if (urgency < 0 || urgency > 2) urgency = 1;
    return urgency;
}

static DBusHandlerResult on_notify(DBusConnection *c, DBusMessage *m)
{
    DBusMessageIter it;
    /* FALSE means the message has NO arguments -- and returning
     * HANDLED here claimed it, so libdbus generated no error reply
     * and nothing was ever sent. The caller sat on its pending call
     * for the libdbus default of twenty-five seconds; a toolkit that
     * notifies synchronously froze for all of it. Every other wrong
     * shape was answered; only the empty one was silent. */
    if (!dbus_message_iter_init(m, &it)) goto bad;

    const char *app = "", *icon = "", *summary = "", *body = "";
    dbus_uint32_t replaces = 0;

    /* EVERY argument is checked before it is read. A message whose
     * signature does not match is a message from a program that got it
     * wrong, or from one that is trying something; neither may be
     * allowed to make this read a string out of an integer. */
    #define TAKE(t, p) do { \
        if (dbus_message_iter_get_arg_type(&it) != (t)) goto bad; \
        dbus_message_iter_get_basic(&it, (p)); \
        dbus_message_iter_next(&it); \
    } while (0)
    TAKE(DBUS_TYPE_STRING, &app);
    TAKE(DBUS_TYPE_UINT32, &replaces);
    TAKE(DBUS_TYPE_STRING, &icon);
    TAKE(DBUS_TYPE_STRING, &summary);
    TAKE(DBUS_TYPE_STRING, &body);
    #undef TAKE
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) goto bad;
    dbus_message_iter_next(&it);                  /* actions: not shown */
    char tag[48];
    int urgency = read_hints(&it, tag, sizeof tag);
    /* expire_timeout is not read. The specification lets the server
     * decide, and here nothing goes away until she has seen it. */

    /* urgency decides how it LOOKS. It does not decide whether it can
     * be pushed out of the list; see the note on the two flags above. */
    dbus_uint32_t id = put(app, replaces, tag, summary, body, urgency >= 2, 0);

    /* A fire-and-forget caller is not answered. The specification says
     * so, and every reply we send it is a message it will throw away. */
    if (!dbus_message_get_no_reply(m)) {
        DBusMessage *r = dbus_message_new_method_return(m);
        if (r) {
            dbus_message_append_args(r, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
            dbus_connection_send(c, r, NULL);
            dbus_message_unref(r);
        }
    }
    (void)icon;
    return DBUS_HANDLER_RESULT_HANDLED;

bad: {
        DBusMessage *e = dbus_message_new_error(m,
            "org.freedesktop.DBus.Error.InvalidArgs",
            "Notify takes (susssasa{sv}i)");
        if (e) { dbus_connection_send(c, e, NULL); dbus_message_unref(e); }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
}

static DBusHandlerResult on_msg(DBusConnection *c, DBusMessage *m, void *ud)
{
    (void)ud;
    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable",
                                    "Introspect")) {
        reply_str(c, m, INTROSPECT);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(m, BUS_NAME, "Notify"))
        return on_notify(c, m);

    if (dbus_message_is_method_call(m, BUS_NAME, "CloseNotification")) {
        dbus_uint32_t id = 0;
        DBusError err; dbus_error_init(&err);
        if (dbus_message_get_args(m, &err, DBUS_TYPE_UINT32, &id,
                                  DBUS_TYPE_INVALID)) {
            int i = find_id(id);
            if (i >= 0) { drop_at(i); closed_signal(id, 3); }
        }
        dbus_error_free(&err);
        DBusMessage *r = dbus_message_new_method_return(m);
        if (r) { dbus_connection_send(c, r, NULL); dbus_message_unref(r); }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(m, BUS_NAME, "GetCapabilities")) {
        /* Only what is true. Claiming "actions" would put buttons in
         * every notification that this shell would then not draw, and
         * a button that is not there is worse than one that was never
         * offered. "body" and "persistence" are true: the body is
         * shown, and nothing is lost when she is not looking. */
        const char *caps[] = { "body", "persistence" };
        DBusMessage *r = dbus_message_new_method_return(m);
        if (r) {
            DBusMessageIter it, arr;
            dbus_message_iter_init_append(r, &it);
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
            for (size_t i = 0; i < sizeof caps / sizeof caps[0]; i++)
                dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &caps[i]);
            dbus_message_iter_close_container(&it, &arr);
            dbus_connection_send(c, r, NULL);
            dbus_message_unref(r);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(m, BUS_NAME, "GetServerInformation")) {
        const char *name = "aurshell", *vendor = "AurOS";
        const char *ver = "1", *spec = "1.2";
        DBusMessage *r = dbus_message_new_method_return(m);
        if (r) {
            dbus_message_append_args(r, DBUS_TYPE_STRING, &name,
                                        DBUS_TYPE_STRING, &vendor,
                                        DBUS_TYPE_STRING, &ver,
                                        DBUS_TYPE_STRING, &spec,
                                        DBUS_TYPE_INVALID);
            dbus_connection_send(c, r, NULL);
            dbus_message_unref(r);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable VTABLE = { NULL, on_msg, NULL, NULL, NULL, NULL };

/* ── joining the bus, WITHOUT EVER BLOCKING THE SCREEN ───────────────
 *
 * dbus_bus_register() and dbus_bus_request_name() are synchronous
 * round trips, and libdbus makes them with an INFINITE poll -- not
 * even its own 25-second default. A gdb backtrace of this shell, on a
 * socket that accepted and then said nothing, sat in
 *
 *     __GI___poll (nfds=1, timeout=-1)
 *       dbus_pending_call_block -> dbus_bus_register -> notify_open
 *
 * for as long as it was left. Nothing painted, no key was read, no
 * click was answered, and no VT switch was acknowledged.
 *
 * That is not a contrived state. $XDG_RUNTIME_DIR/bus is a systemd
 * SOCKET unit: it exists and accepts connections before dbus.service
 * is answering on it, which is exactly the boot this whole feature is
 * about. The shell would have found the socket, connected, and stopped
 * being a desktop until the daemon got round to it.
 *
 * So the handshake is a state machine driven by the frame loop, with a
 * deadline this program owns. Hello and RequestName are sent with
 * send_with_reply() and collected in the pump; a reply that does not
 * come in time closes the connection and tries again later. Nothing
 * here waits for anything.
 */
static const char *bus_addr_now(void)
{
    const char *a = getenv("DBUS_SESSION_BUS_ADDRESS");
    return (a && *a) ? a : "";
}

enum { NS_OFF = 0, NS_HELLO, NS_NAME, NS_OWNED };

/* Long enough for any bus that is actually answering, short enough
 * that a broken one is retried rather than believed. */
#define HANDSHAKE_MS 4000

static int32_t mono32(void) { return now_ms(); }

static void drop_pending(void)
{
    if (!N.pending) return;
    dbus_pending_call_cancel(N.pending);
    dbus_pending_call_unref(N.pending);
    N.pending = NULL;
}

static int send_call(const char *member, DBusMessage **built)
{
    DBusMessage *m = dbus_message_new_method_call(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", member);
    if (!m) return 0;
    if (built) *built = m;
    return 1;
}

static int begin_hello(void)
{
    DBusMessage *m = NULL;
    if (!send_call("Hello", &m)) return 0;
    int ok = dbus_connection_send_with_reply(N.conn, m, &N.pending,
                                             HANDSHAKE_MS) && N.pending;
    dbus_message_unref(m);
    if (!ok) { N.pending = NULL; return 0; }
    N.state = NS_HELLO;
    N.deadline = mono32() + HANDSHAKE_MS;
    return 1;
}

static int begin_request_name(void)
{
    DBusMessage *m = NULL;
    if (!send_call("RequestName", &m)) return 0;
    const char *nm = BUS_NAME;
    dbus_uint32_t flags = DBUS_NAME_FLAG_DO_NOT_QUEUE;
    if (!dbus_message_append_args(m, DBUS_TYPE_STRING, &nm,
                                     DBUS_TYPE_UINT32, &flags,
                                     DBUS_TYPE_INVALID)) {
        dbus_message_unref(m); return 0;
    }
    int ok = dbus_connection_send_with_reply(N.conn, m, &N.pending,
                                             HANDSHAKE_MS) && N.pending;
    dbus_message_unref(m);
    if (!ok) { N.pending = NULL; return 0; }
    N.state = NS_NAME;
    N.deadline = mono32() + HANDSHAKE_MS;
    return 1;
}

/* One step of the handshake, if one is owed. Returns 1 if anything
 * changed that the caller might want to say out loud. */
static int handshake_step(void)
{
    if (N.state == NS_OFF || N.state == NS_OWNED) return 0;
    if (!N.pending) { notify_close(); return 1; }

    if (!dbus_pending_call_get_completed(N.pending)) {
        if ((int32_t)(mono32() - N.deadline) < 0) return 0;
        fprintf(stderr, "aurshell: the session bus accepted a connection and "
                        "then did not answer; will try again\n");
        notify_close();
        N.next_try = mono32() + 2000;
        return 1;
    }

    DBusMessage *r = dbus_pending_call_steal_reply(N.pending);
    dbus_pending_call_unref(N.pending);
    N.pending = NULL;
    if (!r) { notify_close(); return 1; }

    int err = dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR;
    if (N.state == NS_HELLO) {
        const char *unique = NULL;
        if (err || !dbus_message_get_args(r, NULL, DBUS_TYPE_STRING, &unique,
                                          DBUS_TYPE_INVALID) || !unique) {
            fprintf(stderr, "aurshell: the session bus would not have us (%s)\n",
                    err ? dbus_message_get_error_name(r) : "no name");
            dbus_message_unref(r);
            notify_close();
            return 1;
        }
        dbus_bus_set_unique_name(N.conn, unique);
        dbus_message_unref(r);
        if (!begin_request_name()) { notify_close(); return 1; }
        return 0;
    }

    /* NS_NAME */
    dbus_uint32_t res = 0;
    if (err || !dbus_message_get_args(r, NULL, DBUS_TYPE_UINT32, &res,
                                      DBUS_TYPE_INVALID)) {
        fprintf(stderr, "aurshell: could not claim %s (%s)\n", BUS_NAME,
                err ? dbus_message_get_error_name(r) : "no answer");
        dbus_message_unref(r);
        notify_close();
        return 1;
    }
    dbus_message_unref(r);
    if (res != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        /* Somebody else is the notification server. That is a fine
         * answer -- an organisation may ship its own -- and the right
         * thing is to stand down rather than fight over the name. */
        fprintf(stderr, "aurshell: %s is already served by another "
                        "program; not taking it over\n", BUS_NAME);
        notify_close();
        N.next_try = mono32() + 60000;
        return 1;
    }
    if (!dbus_connection_register_object_path(N.conn, BUS_PATH, &VTABLE, NULL)) {
        fprintf(stderr, "aurshell: could not serve %s\n", BUS_PATH);
        notify_close();
        return 1;
    }
    N.state = NS_OWNED;
    N.owned = 1;
    snprintf(N.addr, sizeof N.addr, "%s", bus_addr_now());
    fprintf(stderr, "aurshell: serving %s — programs can say things now\n",
            BUS_NAME);
    return 1;
}

int notify_open(void)
{
    if (N.owned && N.conn && dbus_connection_get_is_connected(N.conn)) {
        if (!strcmp(N.addr, bus_addr_now())) return 1;
        /* The shell moved. Give the name up here and take it again
         * over there; the cards on screen are kept, because they are
         * hers and have nothing to do with which socket they arrived
         * on. */
        fprintf(stderr, "aurshell: the session bus changed — announcing "
                        "notifications on the new one\n");
        notify_close();
        N.next_try = 0;                    /* at once, not in a second */
    }
    if (N.conn && !dbus_connection_get_is_connected(N.conn)) notify_close();

    /* A handshake already in flight: give it its step and nothing else. */
    if (N.state == NS_HELLO || N.state == NS_NAME) {
        handshake_step();
        return N.state == NS_OWNED;
    }

    int32_t t = mono32();
    if (N.next_try && (int32_t)(t - N.next_try) < 0) return 0;
    N.next_try = t + RETRY_MS;

    const char *want = bus_addr_now();
    if (!*want) return 0;                  /* no bus to be had yet */

    DBusError err; dbus_error_init(&err);
    /* OPENED BY ADDRESS, and privately.
     *
     * Privately because the shared connection is a process-wide
     * singleton that cannot be closed, and this one has to be: the
     * shell moves from the bus its unit made to the bus logind made.
     *
     * By ADDRESS because dbus_bus_get_private() does not re-read the
     * environment. libdbus caches the session bus address globally on
     * first use, so after the move it handed back a connection to the
     * bus we had just left -- and then request_name SUCCEEDED on it,
     * because we had only just given the name up there. The shell
     * logged "serving org.freedesktop.Notifications" on the wrong bus
     * and every application on the right one still found nothing.
     * Caught by a harness that started a second bus and sent to it.
     *
     * This is the one call here that can still wait on the kernel: a
     * connect() to a unix socket whose listen backlog is full. That is
     * bounded by the kernel and by a socket we have just stat()ed, and
     * it is not the unbounded wait the handshake used to be. */
    N.conn = dbus_connection_open_private(want, &err);
    if (!N.conn) {
        static int moaned = 0;
        if (!moaned++)
            fprintf(stderr, "aurshell: no session bus yet for notifications "
                            "(%s)\n", err.message ? err.message : "?");
        dbus_error_free(&err);
        return 0;
    }
    dbus_error_free(&err);
    /* Losing the bus must not kill the desktop. */
    dbus_connection_set_exit_on_disconnect(N.conn, FALSE);

    if (!begin_hello()) { notify_close(); return 0; }
    return 0;                              /* not ours yet; keep pumping */
}

void notify_close(void)
{
    drop_pending();
    N.state = NS_OFF;
    if (!N.conn) { N.owned = 0; return; }
    /* No release_name: that is another blocking round trip, and
     * closing the connection makes the bus drop every name we hold
     * anyway. */
    if (N.owned) dbus_connection_unregister_object_path(N.conn, BUS_PATH);
    dbus_connection_close(N.conn);
    dbus_connection_unref(N.conn);
    N.conn = NULL;
    N.owned = 0;
    N.addr[0] = 0;
}

void notify_fini(void)
{
    notify_close();
    N.count = 0;
    N.list_open = 0;
    N.first = 0;
}

int notify_fd(void)
{
    int fd = -1;
    /* WHENEVER THERE IS A CONNECTION, not only once we own the name.
     *
     * This returned -1 during the handshake, so the bus was not in the
     * host's poll() while joining it -- and an idle desktop polls for
     * a whole second at a time. The handshake takes several passes of
     * the loop (queue, write, read the Hello reply, send RequestName,
     * read that reply), and several passes at one second each is eight
     * seconds against a four-second deadline. So on a real machine it
     * timed out, backed off, and timed out again, for ever: the
     * desktop was fine and nothing could ever tell her anything.
     *
     * Found by booting the image. It cannot be found from a harness
     * that pumps in a tight loop, because there the passes cost
     * milliseconds -- which is exactly what putting the fd back in
     * poll() restores. */
    if (!N.conn) return -1;
    if (!dbus_connection_get_unix_fd(N.conn, &fd)) return -1;
    return fd;
}

int notify_wait_ms(void)
{
    if (N.state == NS_HELLO || N.state == NS_NAME) return 30;
    return -1;
}

int notify_pump(shell_ctx *c)
{
    (void)c;
    if (!N.conn) return 0;
    if (!N.owned) {
        /* Still joining. read_write(0) is non-blocking and is what
         * lets the pending Hello or RequestName complete. */
        dbus_connection_read_write(N.conn, 0);
        while (dbus_connection_dispatch(N.conn) == DBUS_DISPATCH_DATA_REMAINS) { }
        return handshake_step();
    }
    int before = N.count;
    uint32_t sig = 0;
    for (int i = 0; i < N.count; i++) sig = sig * 31u + N.n[i].id + (uint32_t)N.n[i].seen;

    /* read_write(0) writes what is queued without waiting, which is
     * what dbus_connection_flush() was doing -- except that flush
     * BLOCKS until the queue is empty, with no timeout, from inside
     * the frame loop. */
    dbus_connection_read_write(N.conn, 0);
    while (dbus_connection_dispatch(N.conn) == DBUS_DISPATCH_DATA_REMAINS) { }

    if (!dbus_connection_get_is_connected(N.conn)) { notify_close(); return 1; }

    uint32_t after = 0;
    for (int i = 0; i < N.count; i++) after = after * 31u + N.n[i].id + (uint32_t)N.n[i].seen;
    return before != N.count || sig != after;
}

int notify_showing(void)
{
    int n = 0;
    for (int i = 0; i < N.count; i++) n += !N.n[i].seen;
    return n;
}
int notify_kept(void)      { return N.count; }
int notify_list_open(void) { return N.list_open; }
int notify_list_first(void) { return N.first; }

/* Message i, 0 the newest. */
static const note *nth(int i)
{
    return (i >= 0 && i < N.count) ? &N.n[N.count - 1 - i] : NULL;
}
const char *notify_card_app(int i)     { return nth(i) ? nth(i)->app : ""; }
const char *notify_card_summary(int i) { return nth(i) ? nth(i)->summary : ""; }
const char *notify_card_body(int i)    { return nth(i) ? nth(i)->body : ""; }
unsigned notify_card_id(int i)         { return nth(i) ? (unsigned)nth(i)->id : 0u; }
int notify_card_sticky(int i)          { return nth(i) ? nth(i)->sticky : 0; }

/* ── where they go ──────────────────────────────────────────────── */

/* How tall the card's words were when it was last painted, so a press
 * lands on the card she is looking at. Fonts are a paint-time thing;
 * the geometry is not allowed to need them. */
static int card_text_h = 0;

void notify_view_now(const shell_ctx *c, notify_view *v)
{
    memset(v, 0, sizeof *v);
    v->unseen = notify_showing();
    v->kept = N.count;
    v->list_open = N.list_open;
    v->first = N.first;
    v->text_scale = (c && c->text_scale > 0.1f) ? c->text_scale : 1.f;
    v->foot_h = c ? foot_height(c) : 0;
    v->bar_h  = c ? c->bar_h : 0;
    v->text_h = card_text_h;
    if (!c) return;
    v->clock = c->clock_r;
    /* THE CORNER WITH NO BUTTON IN IT, per look. Under the clock at the
     * top right, where the strip looks have empty paper; above the bar
     * at the bottom right where the clock is down there ("Like
     * Windows"); and at the bottom right in "One thing at a time",
     * whose top right is a tile and whose bottom right is the empty
     * end of the Home strip. tools/journeytest.sh presses every word
     * under the card in each look to prove it. */
    v->bottom = (v->clock.w > 0 && v->clock.y > c->screen_h / 2) ||
                !strcmp(c->layout_id, "tiles");
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void notify_layout(int sw, int sh, const notify_view *v, notify_geom *g)
{
    memset(g, 0, sizeof *g);
    if (!v) return;
    float k = (v->text_scale > 0.1f) ? v->text_scale : 1.f;
    int m   = (int)(16.f * k); if (m < 14) m = 14;
    int pad = (int)(12.f * k); if (pad < 12) pad = 12;
    int bh  = (int)(48.f * k); if (bh < NOTIFY_TARGET) bh = NOTIFY_TARGET;
    int gap = (int)(12.f * k);
    int body = sh - v->foot_h;                 /* the band is below this  */

    /* Everything hangs from the clock, and stays off it. */
    rect ck = v->clock;
    int has_clock = ck.w > 0 && ck.h > 0;
    int clock_low = has_clock && ck.y > body / 2;
    int top = (has_clock && !clock_low) ? ck.y + ck.h : v->bar_h;
    /* With the clock down in the bar, the strip just above the bar is
     * where the on-screen message goes (osd.c), so the card keeps
     * clear of it rather than jumping when one appears. */
    int low = clock_low ? ck.y - ck.h : body;
    int right = sw - m;
    int w = clampi((int)(600.f * k), 280, sw - 2 * m);

    if (v->unseen > 0 && !v->list_open) {
        /* Words on the left, the buttons stacked on the right, and a
         * card no wider than the free end of any look at 1024 across:
         * the words wrap to two lines and two, no more -- a program
         * does not get to decide how much of her screen it takes. */
        int cw = clampi((int)(440.f * k), 280, sw - 2 * m);
        int bw = (int)(150.f * k); if (bw < 130) bw = 130;
        int nb = v->unseen > 1 ? 2 : 1;
        int btns = nb * bh + (nb - 1) * (gap > 8 ? 8 : gap);
        int words = v->text_h > 0 ? v->text_h : (int)(58.f * k);
        int h = pad + (words > btns ? words : btns) + pad;
        /* Close to the bar when it sits above one: the free end of
         * "One thing at a time" is only 139 px tall at 1024x600. */
        int y = v->bottom ? low - m / 2 - h : top + m;
        if (y < top) y = top;
        g->card = (rect){ right - cw, y, cw, h };
        int bx = right - pad - bw;
        int by = y + (h - btns) / 2;
        if (nb > 1) { g->more = (rect){ bx, by, bw, bh }; by += bh + (gap > 8 ? 8 : gap); }
        g->ok = (rect){ bx, by, bw, bh };
    }

    if (v->list_open) {
        int head = pad + (int)(40.f * k);
        int foot = gap + bh + pad;
        int rh = (int)(74.f * k); if (rh < NOTIFY_TARGET) rh = NOTIFY_TARGET;
        int avail = low - top - 2 * m;
        int fit = (avail - head - foot) / rh;
        if (fit < 1) fit = 1;
        if (fit > NOTIFY_KEEP) fit = NOTIFY_KEEP;
        int first = clampi(v->first, 0, v->kept > 0 ? v->kept - 1 : 0);
        int rows = v->kept - first < fit ? v->kept - first : fit;
        int h = head + (rows > 0 ? rows : 1) * rh + foot;
        int y = clock_low ? low - m - h : top + m;
        if (y < top) y = top;
        g->list = (rect){ right - w, y, w, h };
        g->rows = rows;
        g->per = fit;
        g->first = first;
        for (int i = 0; i < rows; i++)
            g->row[i] = (rect){ right - w + pad, y + head + i * rh, w - 2 * pad, rh };
        /* Newer and Older on the left, Close Messages on the right;
         * each only when there is something that way. */
        int by = y + h - pad - bh;
        int pw = (int)(130.f * k);
        int inner = w - 2 * pad;
        int cw = inner - 2 * (pw + gap);
        if (cw < (int)(150.f * k)) cw = (int)(150.f * k);
        if (cw > inner) cw = inner;
        g->close = (rect){ right - pad - cw, by, cw, bh };
        int px = right - w + pad;
        if (first > 0) { g->newer = (rect){ px, by, pw, bh }; px += pw + gap; }
        if (first + rows < v->kept) g->older = (rect){ px, by, pw, bh };
    }
}

/* Words wrapped at spaces into lines of width w, `lh` apart, from the
 * baseline y. Draws at most max_lines; when there is more than that,
 * the last one drawn ends in "…" (only ever a program's own text, too
 * long for any card). Returns how many lines the whole text needs. s
 * NULL measures and draws nothing. */
int notify_wrap(surface *s, font *f, float x, float y, float w, float lh,
                int max_lines, const char *t, uint32_t col, float a)
{
    char ln[NOTIFY_BODY + 1];
    int n = 0;
    if (!f) return 0;
    while (t && *t) {
        while (*t == ' ') t++;
        if (!*t) break;
        size_t fit = 0, end = 0;
        for (;;) {                                /* whole words that fit */
            while (t[end] && t[end] != ' ') end++;
            if (end >= sizeof ln) break;
            memcpy(ln, t, end); ln[end] = 0;
            float lw = shell_text_w(f, ln);
            if (fit && lw > w) break;
            fit = end;
            if (!t[end] || lw > w) break;
            end++;
        }
        if (!fit) break;
        memcpy(ln, t, fit); ln[fit] = 0;
        /* One word wider than the line: as many whole letters as fit. */
        while (fit > 1 && shell_text_w(f, ln) > w) {
            do fit--; while (fit > 1 && ((unsigned char)t[fit] & 0xC0) == 0x80);
            ln[fit] = 0;
        }
        n++;
        if (s && n <= max_lines) {
            const char *r = t + fit;
            while (*r == ' ') r++;
            if (n == max_lines && *r) shell_text_elided(s, f, x, y, w, t, col, a);
            else                      shell_text(s, f, x, y, ln, col, a);
            y += lh;
        }
        t += fit;
    }
    return n;
}

/* A button: what can be pressed looks pressable (§3). */
static void button(surface *s, font *ft, rect r, const char *label,
                   uint32_t fill, uint32_t edge, uint32_t ink)
{
    draw_round_rect(s, r, corners_all(8.f), fill, 1.f);
    if (edge != fill) draw_round_rect_border(s, r, corners_all(8.f), 2.f, edge, 1.f);
    if (ft) shell_text_centred(s, ft, (float)r.x + r.w * 0.5f,
                               shell_baseline(ft, (float)r.y, (float)r.h), label, ink, 1.f);
}

void notify_paint(shell_ctx *c, surface *s, shell_fonts *f)
{
    notify_view v; notify_view_now(c, &v);
    if (!v.unseen && !v.list_open) return;
    float k = v.text_scale;
    font *big = f->big   ? f->big   : f->mid;
    font *nm  = f->mid   ? f->mid   : f->small;
    font *sm  = f->small ? f->small : f->mid;
    font *tn  = f->tiny  ? f->tiny  : sm;
    int pad = (int)(12.f * k); if (pad < 12) pad = 12;
    uint32_t warn = theme_color(&c->theme, "warn", 0x8A5A12u);

    int ci = card_note();
    if (ci >= 0 && !v.list_open) {
        note *n = &N.n[ci];
        /* Measure first: the card is as tall as its words, one line or
         * two, and the press has to land on the card that is drawn. */
        notify_geom g; notify_layout(s->w, s->h, &v, &g);
        float tw = (float)(g.ok.x - g.card.x - 2 * pad);
        int hl = nm ? notify_wrap(NULL, nm, 0, 0, tw, 0, 2, n->summary, 0, 0) : 1;
        int bl = (sm && n->body[0]) ? notify_wrap(NULL, sm, 0, 0, tw, 0, 2, n->body, 0, 0) : 0;
        float hp = nm ? font_line_height(nm) : 30.f, bp = sm ? font_line_height(sm) : 28.f;
        int th = (int)((hl > 2 ? 2 : hl < 1 ? 1 : hl) * hp + (bl > 2 ? 2 : bl) * bp + 0.5f);
        if (th != card_text_h) { card_text_h = th; v.text_h = th; notify_layout(s->w, s->h, &v, &g); }

        rect r = g.card;
        draw_round_rect(s, r, corners_all(12.f), c->surface_c, 1.f);
        /* Pressable all over ("Press here to open it") gets the 2 px
         * rule of a thing she can press; the rest is a card. */
        draw_round_rect_border(s, r, corners_all(12.f), n->open_id[0] ? 2.f : 1.f,
                               c->overlay, 1.f);
        if (n->urgent) draw_rect(s, (rect){ r.x, r.y + 6, 4, r.h - 12 }, warn, 1.f);
        float tx = (float)(r.x + pad + (n->urgent ? 6 : 0));
        float by = (float)r.y + (float)(r.h - card_text_h) * 0.5f + (nm ? font_ascent(nm) : 0.f);
        if (n->urgent) tw -= 6.f;
        int hn = notify_wrap(s, nm, tx, by, tw, hp, 2, n->summary, c->fg_hi, 1.f);
        if (hn > 2) hn = 2;
        if (sm && n->body[0])
            notify_wrap(s, sm, tx, by + hn * hp - (nm ? font_ascent(nm) : 0.f) +
                               font_ascent(sm), tw, bp, 2, n->body, c->fg, 1.f);
        button(s, nm, g.ok, "OK", c->accent, c->accent, c->surface_c);
        if (g.more.w) {
            char lab[32];
            snprintf(lab, sizeof lab, "%d messages", v.unseen);
            button(s, nm, g.more, lab, c->surface_c, c->overlay, c->fg_hi);
        }
    }

    if (v.list_open) {
        notify_geom g; notify_layout(s->w, s->h, &v, &g);
        rect r = g.list;
        draw_round_rect(s, r, corners_all(12.f), c->surface_c, 1.f);
        draw_round_rect_border(s, r, corners_all(12.f), 1.f, c->overlay, 1.f);
        if (big) shell_text(s, big, (float)(r.x + pad),
                            (float)(r.y + pad) + font_ascent(big), "Messages", c->fg_hi, 1.f);
        if (!g.rows && sm)
            shell_text(s, sm, (float)(r.x + pad),
                       (float)(r.y + pad) + 40.f * k + font_ascent(sm),
                       "No messages.", c->subtle, 1.f);
        for (int i = 0; i < g.rows; i++) {
            const note *n = nth(g.first + i);
            rect rr = g.row[i];
            if (n->open_id[0])
                draw_round_rect_border(s, rr, corners_all(8.f), 2.f, c->overlay, 1.f);
            else if (i)
                draw_hrule(s, rr.x, rr.y, rr.w, 1, c->overlay, 1.f);
            float tx = (float)(rr.x + 8), tw = (float)(rr.w - 16);
            float y1 = (float)rr.y + 8.f + (nm ? font_ascent(nm) : 0.f);
            /* Who it is from, at the right of the first line, when it
             * fits: she needs to know whether the browser or the
             * computer is talking to her. */
            float aw = (tn && n->app[0]) ? shell_text_w(tn, n->app) : 0.f;
            if (aw > tw * 0.4f) aw = 0.f;
            if (aw > 0.f)
                shell_text(s, tn, tx + tw - aw, y1, n->app, c->subtle, 1.f);
            if (nm) shell_text_elided(s, nm, tx, y1, tw - (aw > 0.f ? aw + 12.f : 0.f),
                                      n->summary, c->fg_hi, 1.f);
            if (sm && n->body[0])
                shell_text_elided(s, sm, tx, y1 + 30.f * k, tw, n->body, c->fg, 1.f);
        }
        if (g.newer.w) button(s, nm, g.newer, "Newer", c->surface_c, c->overlay, c->fg_hi);
        if (g.older.w) button(s, nm, g.older, "Older", c->surface_c, c->overlay, c->fg_hi);
        button(s, nm, g.close, "Close Messages", c->surface_c, c->overlay, c->fg_hi);
    }
}

static int inside(rect r, int x, int y)
{
    return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int notify_key(shell_ctx *c, int keycode)
{
    (void)c;
    if (!N.list_open) return 0;
    if (keycode == 1 /* KEY_ESC */) N.list_open = 0;
    return 1;
}

int notify_click(shell_ctx *c, int x, int y)
{
    notify_view v; notify_view_now(c, &v);
    notify_geom g; notify_layout(c->screen_w, c->screen_h + v.foot_h, &v, &g);

    if (v.list_open) {
        /* A row that is a program opens it; Close Messages and a press
         * anywhere else put the list away. Every press is the list's
         * while it is up: one that fell through would act on something
         * under it she could not see. */
        if (inside(g.older, x, y)) { N.first = g.first + g.per; return 1; }
        if (inside(g.newer, x, y)) {
            N.first = g.first - g.per; if (N.first < 0) N.first = 0;
            return 1;
        }
        for (int i = 0; i < g.rows; i++)
            if (inside(g.row[i], x, y)) {
                const note *n = nth(g.first + i);
                if (!n->open_id[0]) return 1;
                snprintf(take_open, sizeof take_open, "%s", n->open_id);
                break;
            }
        if (inside(g.list, x, y) && !inside(g.close, x, y) && !take_open[0]) return 1;
        N.list_open = 0;
        return 1;
    }

    /* The clock opens Messages, which is also "she has seen them". */
    if (inside(v.clock, x, y)) {
        see_all();
        N.list_open = 1;
        N.first = 0;
        return 1;
    }

    int ci = card_note();
    if (ci < 0 || !inside(g.card, x, y)) return 0;
    if (inside(g.ok, x, y)) { see_all(); return 1; }
    if (inside(g.more, x, y)) { see_all(); N.list_open = 1; N.first = 0; return 1; }
    /* The card itself. A program's "is ready" card opens it, and that
     * one is seen; any other card takes the press and does nothing, so
     * a press on it never reaches what is underneath. */
    if (N.n[ci].open_id[0]) {
        snprintf(take_open, sizeof take_open, "%s", N.n[ci].open_id);
        N.n[ci].seen = 1;
        closed_signal(N.n[ci].id, 2);
    }
    return 1;
}
