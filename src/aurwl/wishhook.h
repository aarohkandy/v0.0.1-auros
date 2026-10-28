/* wishhook.h — what the compositor tells Wishes, and nothing else.
 *
 * docs/AI.md §1.5 and §3.5: the compositor matches what she types against
 * the few trigger words her wishes registered, and reports only "word X was
 * typed". Everything else a key press carries -- every other word, every
 * letter, every password -- is dropped on the floor, in this file, before
 * it can reach anything that stores or sends.
 *
 * HOW IT STAYS HARMLESS
 *   - With no words registered it does nothing at all: it does not even ask
 *     the keymap what a key types.
 *   - It sees only keys the compositor is delivering to a CLIENT window.
 *     The shell's own text fields (the Wi-Fi password, the lock screen) take
 *     the keyboard away from clients first, so they never come here.
 *   - It keeps at most the 32 letters of the word being typed, in RAM, in
 *     one static buffer, cleared (explicit_bzero) at the end of every word,
 *     on every click and on every change of focus. It writes nothing to a
 *     file, a log or a socket. A word is reported only if it is one of the
 *     registered words, spelled the same, typed as a whole word.
 *   - It cannot tell a password field from any other (aurwl has no
 *     text-input-v3 yet). That is why Wishes ship switched off (D14).
 *
 * The hooks in aurwl.c are weak references: a build that does not link this
 * file (the image, unless the profile says wishes="on") has no matcher at
 * all. */
#ifndef AUROS_WISHHOOK_H
#define AUROS_WISHHOOK_H

#include <stddef.h>
#include <stdint.h>
#include "aurwl.h"

/* From the shell, when the engine sends its `triggers` line (AI.md §3.7).
 * Words: lowercase a-z, 4-32 letters; apps: desktop ids or app ids. At
 * most 10 and 20. NULL/0 clears. Anything malformed is skipped. */
void wishhook_set(const char *const *words, int nwords, const char *const *apps, int napps);

/* The shell drains these each frame and sends `typed` / `app` to the
 * engine. 1 and fills `out`, or 0 when nothing is waiting. */
int  wishhook_take_word(char out[33]);
int  wishhook_take_app(char out[128]);

/* The names the shell (src/aurshell/wish.c) calls. Weak references on its
 * side, so a build without a compositor links without them. `wishhook_take`
 * returns 1 and fills `kind` ("typed" or "app") and `val` (the registered
 * word, or the application id), 0 when nothing is waiting. */
int  wishhook_set_triggers(const char *const *words, int n_words, const char *const *apps, int n_apps);
int  wishhook_take(char *kind, size_t kn, char *val, size_t vn);

/* The hooks aurwl.c calls (weakly). */
void wishhook_key(aurwl *c, uint32_t evdev_code, int pressed);
void wishhook_reset(void);                 /* a click, a change of focus */
void wishhook_app(const char *app_id);     /* a window announced its application */

/* For tests only: letters currently held (0 after every word). */
int  wishhook_buffered(void);

#endif
