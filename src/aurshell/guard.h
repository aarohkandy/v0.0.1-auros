/* guard.h — two cards that stand between her and a scam.
 *
 * "GET ME OUT OF THIS PAGE". A page can already never cover the band
 * (foot.c) or go truly full-screen (session.c). What it can still do
 * is shout: a fake warning, a phone number, a beep that will not stop,
 * and a browser that answers Escape by going full-window again. So:
 *
 *   - Escape three times within two seconds, or Ctrl+Alt+Delete (the
 *     keys twenty years of Windows taught her), or the big button at
 *     the top of Help, open a calm card. The shell sees every key
 *     before any program does, so no page can stop this.
 *   - The sound goes off the moment it opens, and comes back as it was
 *     when it closes, however it closes.
 *   - "Close this page" stops the program she was looking at at once.
 *     It does not ask the program first: a scam page is exactly the
 *     thing that would answer by asking her something else.
 *
 * "BEFORE YOU INSTALL THIS". polkit refuses the first attempt to
 * install a remote-control program (rootfs/etc/polkit-1/rules.d/
 * 40-auros-remote-access.rules) and a signal on the system bus tells
 * this file, which asks her plainly. "I called someone I know" writes
 * GUARD_CONSENT_DIR/<name> as her; the next attempt goes through.
 *
 * Only the FLAG (c->guard_open) lives in shell.h, so foot.c can open
 * this from Help without linking it. Everything else is here.
 */
#ifndef AUROS_GUARD_H
#define AUROS_GUARD_H

#include <stdint.h>
#include "shell.h"

#ifndef GUARD_CONSENT_DIR
#define GUARD_CONSENT_DIR "/run/auros-remote-access"
#endif

enum { GUARD_NONE, GUARD_OUT, GUARD_REMOTE, GUARD_REMOTE_OK };
/* What the buttons do. */
enum { G_CLOSE = 1, G_BACK, G_OK, G_NO, G_YES };
#define GUARD_MAX_BUTTONS 2

/* EVERY key, before anything else sees it (but after a dark screen has
 * woken). `value` is evdev's: 1 down, 0 up, 2 repeat. Returns 1 when
 * the key was the guard's -- it opened the card -- and then nothing
 * else may have it, neither its press nor its release. */
int  guard_watch_key(shell_ctx *c, int code, int value, int64_t ms);

/* polkit stopped an install of `name` ("anydesk"). Plain lower-case
 * letters and digits only; anything else is ignored. */
void guard_remote(shell_ctx *c, const char *name);

/* The system bus: connects (and reconnects) by itself. Once per pass
 * of the main loop. 1 if a card opened. */
int  guard_pump(shell_ctx *c);
/* Once per pass, after anything that may have opened or closed the
 * card: turns the sound off while "Get me out" is up, and back to how
 * it was when it goes, however it went (Escape, a button, the band). */
int  guard_step(shell_ctx *c);

int  guard_mode(void);
/* ONE function owns where the buttons are: paint, click, keys, tests. */
int  guard_buttons(const shell_ctx *c, int sw, int sh, rect *out, int *which);
void guard_paint(shell_ctx *c, surface *s, shell_fonts *f);
int  guard_click(shell_ctx *c, int x, int y);   /* 1 if taken */
void guard_motion(shell_ctx *c, int x, int y);
int  guard_key(shell_ctx *c, int k);            /* 1 if taken */

#endif
