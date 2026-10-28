/* ═══════════════════════════════════════════════════════════════════
 *  hwcompat -- will her hardware work? Inventory in, sentences out.
 *
 *  Stream HW (docs/handoff/streams/HW.md). Pure C: no Windows header, no
 *  file read. The facts it judges against (which PCI/USB id which driver
 *  in the image's kernel claims, whether that driver's firmware is in the
 *  image, which printer models CUPS can drive, the hand-written
 *  exceptions) are hw/compat.json, made from the image by
 *  tools/hw/gen-compat.py and compiled in through headers that
 *  tools/hw/gen-header.py writes at build time.
 *
 *  Callers:
 *    AurBridge's wizard (stream B) shows `says` of each verdict, and
 *    stops on `blocks`; the report (hwinv_win.c) prints them all.
 *    The staging environment (stream A2) may link the same file to check
 *    again after the restart.
 *
 *  THE RULES IT FOLLOWS
 *    - "Works" needs evidence: a driver in the image's kernel claims the
 *      id AND the firmware it must have is in the image. Otherwise it is
 *      "won't work yet" (we know why) or "can't tell" (we do not). Never
 *      "works" for a guess.
 *    - A hand-written rule (hw/seeds.json) beats the automatic answer.
 *    - Of several devices of one kind, the best one counts: a working USB
 *      Wi-Fi stick makes "your Wi-Fi will work" true.
 *    - `blocks` is set only when the computer would have NO way to reach
 *      the internet in AurOS: Wi-Fi won't work and there is no network
 *      cable socket that will. The wizard refuses then, before anything
 *      has changed.
 * ═══════════════════════════════════════════════════════════════════ */
#ifndef HWCOMPAT_H
#define HWCOMPAT_H

#include "hwinv.h"

typedef struct {
    int         n_real;       /* modules in the image that drive this id   */
    int         n_bus_only;   /* modules that only open the bus (bcma/ssb) */
    int         fw_unusable;  /* every real driver lacks a must-have file  */
    const char *module;       /* the first real driver, else NULL          */
    const char *bus_module;   /* the first bus-only module, else NULL      */
} hwc_hit;

/* Which modules claim this device. Only drivers of the device's own kind
 * count (a Wi-Fi card is not "driven" by a USB sound module that happens
 * to claim every vendor-specific interface): cat is the kind's number
 * 1..6 (wifi, ethernet, bluetooth, audio, camera, gpu), 0 for any.
 * Returns the number of real drivers. */
int hwcompat_lookup(const hw_dev *d, int cat, hwc_hit *out);

/* Fill inv->verdicts (replacing what was there) from inv->devs and
 * inv->printers. Returns the number of verdicts. */
int hwcompat_judge(hw_inv *inv);

/* The kernel the facts are about, e.g. "6.8.0-147-generic". */
const char *hwcompat_kernel(void);

/* Cases: BCM4352 refused or warned plainly, AX201 fine, printers, the
 * rules' order. Prints one line each; returns the number wrong. */
int hwcompat_selftest(void);

#endif
