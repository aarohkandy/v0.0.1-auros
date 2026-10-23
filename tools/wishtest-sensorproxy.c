/* wishtest-sensorproxy.c — a stand-in for iio-sensor-proxy, for tests.
 *
 *   wishtest-sensorproxy BUS_ADDRESS CONTROL_FILE LOG_FILE
 *
 * Owns net.hadess.SensorProxy on the given bus and answers what aurwish
 * asks (docs: iio-sensor-proxy's net.hadess.SensorProxy and
 * net.hadess.SensorProxy.Compass interfaces): Properties.Get for HasCompass,
 * HasAccelerometer, HasAmbientLight, CompassHeading,
 * AccelerometerOrientation, LightLevel, and the ClaimX and ReleaseX methods.
 * Values come from CONTROL_FILE, read on every call:
 *     heading=10.5
 *     orient=bottom-up
 *     lux=30
 *     has=compass,tilt,light
 * Every Claim and Release is appended to LOG_FILE, so a test can prove a
 * sensor is claimed only while a wish needs it. */
#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *ctl, *logf;

static void logline(const char *s)
{
    FILE *f = fopen(logf, "a");
    if (f) { fputs(s, f); fputc('\n', f); fclose(f); }
}

static int control(const char *key, char *out, size_t n)
{
    FILE *f = fopen(ctl, "r");
    char line[200];
    int found = 0;
    while (f && fgets(line, sizeof line, f)) {
        size_t kl = strlen(key);
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            snprintf(out, n, "%s", line + kl + 1);
            out[strcspn(out, "\r\n")] = 0;
            found = 1;
        }
    }
    if (f) fclose(f);
    return found;
}

static int has(const char *what)
{
    char v[100];
    return control("has", v, sizeof v) && strstr(v, what);
}

static void reply_variant(DBusConnection *c, DBusMessage *m, int type, const void *val)
{
    DBusMessage *r = dbus_message_new_method_return(m);
    DBusMessageIter it, v;
    char sig[2] = { (char)type, 0 };
    dbus_message_iter_init_append(r, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, sig, &v);
    dbus_message_iter_append_basic(&v, type, val);
    dbus_message_iter_close_container(&it, &v);
    dbus_connection_send(c, r, NULL);
    dbus_message_unref(r);
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    ctl = argv[2]; logf = argv[3];
    DBusError err;
    dbus_error_init(&err);
    DBusConnection *c = dbus_connection_open_private(argv[1], &err);
    if (!c || !dbus_bus_register(c, &err)) { fprintf(stderr, "bus: %s\n", err.message); return 1; }
    if (dbus_bus_request_name(c, "net.hadess.SensorProxy", 0, &err) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) return 1;
    puts("ready");
    fflush(stdout);
    for (;;) {
        if (!dbus_connection_read_write(c, 200)) break;
        DBusMessage *m;
        while ((m = dbus_connection_pop_message(c))) {
            const char *iface = dbus_message_get_interface(m), *mem = dbus_message_get_member(m);
            if (iface && mem && !strcmp(iface, "org.freedesktop.DBus.Properties") && !strcmp(mem, "Get")) {
                const char *i2 = NULL, *prop = NULL;
                dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &i2, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
                char v[100];
                if (prop && !strcmp(prop, "HasCompass")) { dbus_bool_t b = has("compass"); reply_variant(c, m, DBUS_TYPE_BOOLEAN, &b); }
                else if (prop && !strcmp(prop, "HasAccelerometer")) { dbus_bool_t b = has("tilt"); reply_variant(c, m, DBUS_TYPE_BOOLEAN, &b); }
                else if (prop && !strcmp(prop, "HasAmbientLight")) { dbus_bool_t b = has("light"); reply_variant(c, m, DBUS_TYPE_BOOLEAN, &b); }
                else if (prop && !strcmp(prop, "CompassHeading")) { double d = control("heading", v, sizeof v) ? atof(v) : -1; reply_variant(c, m, DBUS_TYPE_DOUBLE, &d); }
                else if (prop && !strcmp(prop, "LightLevel")) { double d = control("lux", v, sizeof v) ? atof(v) : 0; reply_variant(c, m, DBUS_TYPE_DOUBLE, &d); }
                else if (prop && !strcmp(prop, "AccelerometerOrientation")) {
                    if (!control("orient", v, sizeof v)) strcpy(v, "undefined");
                    const char *p = v;
                    reply_variant(c, m, DBUS_TYPE_STRING, &p);
                } else dbus_connection_send(c, dbus_message_new_error(m, "org.freedesktop.DBus.Error.UnknownProperty", prop), NULL);
            } else if (mem && (!strncmp(mem, "Claim", 5) || !strncmp(mem, "Release", 7))) {
                logline(mem);
                DBusMessage *r = dbus_message_new_method_return(m);
                dbus_connection_send(c, r, NULL);
                dbus_message_unref(r);
            }
            dbus_message_unref(m);
        }
        dbus_connection_flush(c);
    }
    return 0;
}
