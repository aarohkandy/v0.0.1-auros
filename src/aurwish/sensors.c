/* sensors.c — see sensors.h. */
#define _GNU_SOURCE
#include <dbus/dbus.h>
#include <errno.h>
#include <linux/netlink.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/socket.h>
#include "sensors.h"
#include "wish.h"

#define SP_NAME   "net.hadess.SensorProxy"
#define SP_PATH   "/net/hadess/SensorProxy"
#define SP_IFACE  "net.hadess.SensorProxy"
#define CP_PATH   "/net/hadess/SensorProxy/Compass"
#define CP_IFACE  "net.hadess.SensorProxy.Compass"
#define CALL_MS   200
#define BACKOFF_S 30
#define RECHECK_S 300

struct sensors {
    DBusConnection *bus;
    unsigned have, claimed;
    long long have_at, quiet_until;
};

sensors *sensors_open(const char *address)
{
    sensors *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    DBusError err;
    dbus_error_init(&err);
    if (address && *address) {
        s->bus = dbus_connection_open_private(address, &err);
        if (s->bus && !dbus_bus_register(s->bus, &err)) { dbus_connection_close(s->bus); dbus_connection_unref(s->bus); s->bus = NULL; }
    } else s->bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (s->bus) dbus_connection_set_exit_on_disconnect(s->bus, FALSE);
    dbus_error_free(&err);
    return s;
}

void sensors_close(sensors *s)
{
    if (!s) return;
    if (s->bus) { dbus_connection_close(s->bus); dbus_connection_unref(s->bus); }
    free(s);
}

/* One method call, with a short deadline. NULL on any failure. */
static DBusMessage *call(sensors *s, long long now, const char *path, const char *iface,
                         const char *method, const char *a1, const char *a2)
{
    if (!s->bus || now < s->quiet_until) return NULL;
    DBusMessage *m = dbus_message_new_method_call(SP_NAME, path, iface, method);
    if (!m) return NULL;
    if (a1) dbus_message_append_args(m, DBUS_TYPE_STRING, &a1, a2 ? DBUS_TYPE_STRING : DBUS_TYPE_INVALID,
                                     &a2, DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *r = dbus_connection_send_with_reply_and_block(s->bus, m, CALL_MS, &err);
    dbus_message_unref(m);
    if (!r) {
        /* "no such service" is an answer (no sensors); a timeout is a
         * reason to leave the service alone for a while. */
        if (dbus_error_is_set(&err) && err.name && strstr(err.name, "NoReply")) s->quiet_until = now + BACKOFF_S;
        dbus_error_free(&err);
        return NULL;
    }
    if (dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR) { dbus_message_unref(r); return NULL; }
    return r;
}

/* org.freedesktop.DBus.Properties.Get: the value inside the variant. */
static int get(sensors *s, long long now, const char *path, const char *iface, const char *prop,
               int want_type, void *out)
{
    DBusMessage *r = call(s, now, path, "org.freedesktop.DBus.Properties", "Get", iface, prop);
    if (!r) return -1;
    DBusMessageIter it, v;
    int ok = -1;
    if (dbus_message_iter_init(r, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&it, &v);
        if (dbus_message_iter_get_arg_type(&v) == want_type) {
            dbus_message_iter_get_basic(&v, out);
            ok = 0;
            if (want_type == DBUS_TYPE_STRING) {
                /* the string lives in the message: copy before it goes */
                static char keep[32];
                snprintf(keep, sizeof keep, "%s", *(const char **)out);
                *(const char **)out = keep;
            }
        }
    }
    dbus_message_unref(r);
    return ok;
}

unsigned sensors_have(sensors *s, long long now, int force)
{
    if (!s) return 0;
    if (!force && s->have_at && now - s->have_at < RECHECK_S) return s->have;
    s->have_at = now;
    unsigned h = 0;
    dbus_bool_t b = 0;
    if (get(s, now, CP_PATH, CP_IFACE, "HasCompass", DBUS_TYPE_BOOLEAN, &b) == 0 && b) h |= 1u << S_COMPASS;
    b = 0;
    if (get(s, now, SP_PATH, SP_IFACE, "HasAccelerometer", DBUS_TYPE_BOOLEAN, &b) == 0 && b) h |= 1u << S_TILT;
    b = 0;
    if (get(s, now, SP_PATH, SP_IFACE, "HasAmbientLight", DBUS_TYPE_BOOLEAN, &b) == 0 && b) h |= 1u << S_LIGHT;
    s->have = h;
    return h;
}

static void claim(sensors *s, long long now, unsigned bit, int on)
{
    static const char *const CL[] = { "ClaimCompass", "ClaimAccelerometer", "ClaimLight" };
    static const char *const RE[] = { "ReleaseCompass", "ReleaseAccelerometer", "ReleaseLight" };
    int i = bit == (1u << S_COMPASS) ? 0 : bit == (1u << S_TILT) ? 1 : 2;
    DBusMessage *r = call(s, now, i == 0 ? CP_PATH : SP_PATH, i == 0 ? CP_IFACE : SP_IFACE,
                          on ? CL[i] : RE[i], NULL, NULL);
    if (r) dbus_message_unref(r);
}

void sensors_want(sensors *s, unsigned mask, long long now)
{
    if (!s) return;
    mask &= s->have;
    unsigned bits[3] = { 1u << S_COMPASS, 1u << S_TILT, 1u << S_LIGHT };
    for (int i = 0; i < 3; i++) {
        if ((mask & bits[i]) && !(s->claimed & bits[i])) { claim(s, now, bits[i], 1); s->claimed |= bits[i]; }
        else if (!(mask & bits[i]) && (s->claimed & bits[i])) { claim(s, now, bits[i], 0); s->claimed &= ~bits[i]; }
    }
}

void sensors_read(sensors *s, long long now, sensor_reading *out)
{
    memset(out, 0, sizeof *out);
    if (!s) return;
    double d;
    const char *str;
    if ((s->claimed & (1u << S_COMPASS)) && get(s, now, CP_PATH, CP_IFACE, "CompassHeading", DBUS_TYPE_DOUBLE, &d) == 0 &&
        isfinite(d)) { out->have_heading = 1; out->heading = d; }
    if ((s->claimed & (1u << S_TILT)) && get(s, now, SP_PATH, SP_IFACE, "AccelerometerOrientation", DBUS_TYPE_STRING, &str) == 0) {
        out->have_orient = 1;
        snprintf(out->orient, sizeof out->orient, "%s", str);
    }
    if ((s->claimed & (1u << S_LIGHT)) && get(s, now, SP_PATH, SP_IFACE, "LightLevel", DBUS_TYPE_DOUBLE, &d) == 0 &&
        isfinite(d)) { out->have_lux = 1; out->lux = d; }
}

/* ── USB, from the kernel ──────────────────────────────────────────── */

int uevent_open(void)
{
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) return -1;
    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof sa);
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = 1;                      /* the kernel's own events */
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }
    return fd;
}

/* A message is "add@/devices/...\0KEY=value\0KEY=value\0...". */
int uevent_is_usb_add(const char *msg, size_t len)
{
    int add = 0, usb = 0, dev = 0;
    if (len < 5 || strncmp(msg, "add@", 4)) return 0;
    add = 1;
    for (size_t i = strlen(msg) + 1; i < len; i += strlen(msg + i) + 1) {
        if (!strcmp(msg + i, "SUBSYSTEM=usb")) usb = 1;
        else if (!strcmp(msg + i, "DEVTYPE=usb_device")) dev = 1;
    }
    return add && usb && dev;
}

int uevent_read(int fd)
{
    int n = 0;
    for (int i = 0; i < 32; i++) {
        char buf[4096];
        struct sockaddr_nl from;
        struct iovec iov = { buf, sizeof buf - 1 };
        char cb[CMSG_SPACE(sizeof(struct ucred))];
        struct msghdr mh = { &from, sizeof from, &iov, 1, cb, sizeof cb, 0 };
        ssize_t k = recvmsg(fd, &mh, 0);
        if (k < 0) break;
        /* Only the kernel (pid 0) speaks on group 1; anything else is
         * somebody pretending to be it. */
        if (from.nl_pid != 0) continue;
        buf[k] = 0;
        n += uevent_is_usb_add(buf, (size_t)k);
    }
    return n;
}
