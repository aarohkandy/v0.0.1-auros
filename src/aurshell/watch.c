/* watch.c — see watch.h. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/statvfs.h>

#include "watch.h"
#include "power.h"
#include "osd.h"
#include "notify.h"

/* Which warnings have already been given on this discharge. Cleared
 * the moment a cable goes in, so unplugging again gets the full
 * sequence rather than silence. */
static struct {
    uint32_t next_ms;
    int armed;
    int said_low, said_very, said_critical;
    int was_plugged;
    uint32_t disk_next_ms;
    int disk_armed;
    int said_disk;
} W;

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

/* Is it time? A clock that starts at 0 and is compared as a signed difference reads "not yet"
 * for the first 24.8 days it could be asked -- so a shell started on a computer that had been on
 * for longer never looked at the battery or the room left. Nothing is due-yet before it is set. */
int watch_due(uint32_t now, uint32_t next, int set)
{
    return !set || (int32_t)(now - next) >= 0;
}

/* What `df` calls Use%: used of (used + what she can still have), so the reserve the file
 * system keeps for itself is not counted as room she has. */
int watch_disk_used(const char *path)
{
    struct statvfs v;
    if (!path || statvfs(path, &v) != 0 || v.f_blocks == 0) return -1;
    unsigned long long used = (unsigned long long)(v.f_blocks - v.f_bfree);
    unsigned long long all = used + (unsigned long long)v.f_bavail;
    if (all == 0) return -1;
    return (int)((used * 100ull + all - 1) / all);
}

int watch_disk_check(shell_ctx *c, int used)
{
    (void)c;
    if (used < 0) return 0;
    if (used < WATCH_DISK_REARM) { W.said_disk = 0; return 0; }
    if (used < WATCH_DISK_FULL || W.said_disk) return 0;
    W.said_disk = 1;
    /* Calm: nothing is wrong yet, and nothing she has is lost. Her words, no sizes. The card
     * stays until she presses OK (notify.h), and pressing it opens the page. */
    notify_local_open("This computer is getting full",
                      "There is not much room left for new things. Press here to see what you can do.",
                      "help:" WATCH_DISK_HELP);
    return 1;
}

/* Once a minute: where her files are (her own folder, else the whole computer). */
static int disk_tick(shell_ctx *c)
{
    if (!watch_due(now_ms(), W.disk_next_ms, W.disk_armed)) return 0;
    W.disk_armed = 1;
    W.disk_next_ms = now_ms() + 60000;
    const char *home = getenv("HOME");
    int used = watch_disk_used(home && *home ? home : "/");
    if (used < 0) used = watch_disk_used("/");
    return watch_disk_check(c, used);
}

static int battery_tick(shell_ctx *c)
{
    power_battery b;
    power_battery_read(&b);
    if (!b.present || b.percent < 0) return 0;

    if (b.plugged || b.charging) {
        /* A cable went in. Everything is forgiven and the sequence
         * starts again next time it comes out. */
        if (!W.was_plugged) {
            W.said_low = W.said_very = W.said_critical = 0;
            W.was_plugged = 1;
        }
        return 0;
    }
    W.was_plugged = 0;

    /* Most urgent first, so a machine that wakes up at 2% does not
     * work its way down through three messages before acting. */
    if (b.percent <= WATCH_CRITICAL) {
        if (W.said_critical) return 0;
        W.said_critical = W.said_very = W.said_low = 1;
        /* BOTH, on purpose. The indicator because it is in the middle
         * of the screen and the machine is about to stop; the card
         * because the machine is about to stop, and what she needs to
         * read is the sentence explaining why it did -- which she will
         * be reading AFTER she plugs it in and wakes it up. A message
         * that lasts three seconds and then the computer sleeps is a
         * computer that turned itself off for no reason she saw. */
        osd_say("The battery is empty. Saving what you have and going to sleep.");
        notify_local("This computer went to sleep",
                     "The battery was empty. It is safe to plug it in and "
                     "press a key; everything you had open is still here.");
        /* Sleep, not shut down. Everything she has open comes back
         * when the cable goes in; a shutdown at this point is the
         * machine deciding to close her work for her. */
        c->want_power_off = 3;
        return 1;
    }
    if (b.percent <= WATCH_VERY_LOW) {
        if (W.said_very) return 0;
        W.said_very = W.said_low = 1;
        /* A card, not a flash. This is a thing she has to ACT on, and
         * docs/EASY.md rule 5 is about exactly that: the indicator
         * goes away by itself after three seconds, and a person who
         * was looking at the keyboard would never have known. This
         * one stays until she presses it. */
        notify_local("The battery is very low",
                     "Please plug this computer in. It will go to sleep on "
                     "its own if the battery runs out.");
        return 1;
    }
    if (b.percent <= WATCH_LOW) {
        if (W.said_low) return 0;
        W.said_low = 1;
        notify_local("The battery is getting low",
                     "There is still plenty of time. Plug this computer in "
                     "when it suits you.");
        return 1;
    }
    /* Back above the line -- a battery that recovers a little, or a
     * reading that was briefly wrong -- so the warnings re-arm. */
    if (b.percent > WATCH_LOW + 5) {
        W.said_low = W.said_very = W.said_critical = 0;
    }
    return 0;
}

int watch_tick(shell_ctx *c)
{
    if (!watch_due(now_ms(), W.next_ms, W.armed)) return 0;
    W.armed = 1;
    W.next_ms = now_ms() + 1000;
    int said_disk = disk_tick(c);
    int said_battery = battery_tick(c);
    return said_disk || said_battery;
}
