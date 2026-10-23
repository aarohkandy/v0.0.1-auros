/* sensors.h — the compass, the way-up sensor and the light sensor, from
 * iio-sensor-proxy over D-Bus (net.hadess.SensorProxy), and USB plugging
 * from the kernel's uevents.
 *
 * Claimed only while a wish needs them (a claimed sensor keeps the
 * hardware awake, which costs battery) and released when the last such
 * wish goes. Polled at the daemon's own one-second tick rather than
 * subscribed to: one small property read a second is easier to get right
 * than the D-Bus watch machinery, and nothing is read when no wish
 * needs a sensor. A service that is missing, slow or wrong never blocks
 * for more than 200 ms and is left alone for a while afterwards. */
#ifndef AURWISH_SENSORS_H
#define AURWISH_SENSORS_H

#include <stddef.h>

typedef struct sensors sensors;

/* `address` NULL = the system bus. Never fails: a sensors object with no
 * bus simply has nothing. */
sensors *sensors_open(const char *address);
void     sensors_close(sensors *s);

/* Asks which sensors exist: bits 1<<S_COMPASS, 1<<S_TILT, 1<<S_LIGHT
 * (wish.h). Cheap to call again; asks the bus at most every 5 minutes
 * unless `force`. */
unsigned sensors_have(sensors *s, long long now, int force);

/* Claims what is wanted and releases what is not (bitmask as above). */
void     sensors_want(sensors *s, unsigned mask, long long now);

typedef struct {
    int    have_heading; double heading;       /* degrees, 0 = north */
    int    have_orient;  char orient[16];      /* normal, bottom-up, left-up, right-up, undefined */
    int    have_lux;     double lux;
} sensor_reading;

/* One read of the claimed sensors. */
void     sensors_read(sensors *s, long long now, sensor_reading *out);

/* ── the kernel's uevents (USB) ────────────────────────────────────── */

/* Opens a NETLINK_KOBJECT_UEVENT socket, or -1. */
int      uevent_open(void);
/* Reads what is waiting; returns the number of USB devices that appeared. */
int      uevent_read(int fd);
/* Parses one message: 1 for "a USB device was plugged in". For tests. */
int      uevent_is_usb_add(const char *msg, size_t len);

#endif
