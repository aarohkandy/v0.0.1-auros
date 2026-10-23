/* wishover.h — what a wish puts on the screen (docs/AI.md §2.2, §7.6, §7.7).
 *
 * Three things, and the rules that make them safe to put in front of
 * her without asking first:
 *
 *   fill     the whole screen, emoji or a few words, at most 10 s.
 *            The first key or click ends it AND IS USED UP: it never
 *            reaches the program underneath, which she cannot see.
 *   cards    show_message and remind. Never take the keyboard, never
 *            larger than half the screen, at most one per wish (a new
 *            one replaces it). Pressed, not typed at, to close.
 *   keep     "Keep this?" after a wish changed the look or the size of
 *            the words; goes back by itself after 15 s with no answer.
 *
 * Every one of them says which wish did it -- 'Your wish "X"' -- and
 * has a 44 px "Stop this wish" that needs one press. */
#ifndef AUROS_WISHOVER_H
#define AUROS_WISHOVER_H

#include "shell.h"
#include "wish.h"

enum { WO_MESSAGE = 1, WO_REMIND, WO_KEEP, WO_UNDO };
enum { WO_KEEP_LOOK = 1, WO_KEEP_TEXT };

#define WO_FILL_MAX_S  10
#define WO_KEEP_S      15

void wishover_fill(shell_ctx *c, const char *emoji, const char *text,
                   int seconds, const char *wish, const char *wid);
/* `for_time` is "9:00" for a reminder that was missed and shown late,
 * or NULL. `privately`: the card says only "You have a reminder" until
 * she presses it. */
void wishover_card(shell_ctx *c, int kind, const char *text,
                   const char *wish, const char *wid,
                   const char *for_time, int privately);
/* `old` is what to go back to: a look id, or a text scale as "%.2f". */
void wishover_keep(shell_ctx *c, int what, const char *old, const char *wish);

int  wishover_fill_on(void);
int  wishover_cards(void);          /* how many cards are up */
void wishover_paint(shell_ctx *c, surface *s, shell_fonts *f);
/* Full-height screen coordinates. 1 if it took the press. */
int  wishover_click(shell_ctx *c, int x, int y);
/* After it took a press: 1 once, for the matching release, so the
 * program underneath never sees half a click. */
int  wishover_take_release(void);
void wishover_motion(shell_ctx *c, int x, int y);
/* 1 if it took the key -- only ever while the fill is up. */
int  wishover_key(shell_ctx *c, int k);
int  wishover_step(shell_ctx *c);
int  wishover_wait_ms(void);
/* "Stop" said to the panel, or every wish turned off: nothing a wish
 * put up stays up. */
void wishover_clear(shell_ctx *c);

/* For tests and the picture sheet: the clock the overlays read. */
void wishover_set_clock(uint64_t (*now)(void));

/* For tests: every target, in full-height screen coordinates. */
enum { WOT_STOP = 1, WOT_CLOSE, WOT_SNOOZE, WOT_KEEP, WOT_UNDO, WOT_REVEAL,
       WOT_FILL, WOT_UNDOWISH };
int  wishover_targets(const shell_ctx *c, int sw, int sh,
                      wish_target *out, int max);

/* The emoji, drawn from the colour emoji font every AurOS image already
 * has (fonts-noto-color-emoji, docs/AI.md §7.3 allowlist). Returns an
 * RGBA bitmap `*w` x `*h`, premultiplied 0xAARRGGBB, owned by the cache,
 * or NULL if this emoji or the font is not here. Exposed for the test. */
const uint32_t *wishover_emoji(const char *utf8, int *w, int *h);
/* The emoji scaled into `r` (square, centred). 0 if this computer cannot
 * draw it, so the caller can show its name instead. */
int  wishover_draw_emoji(surface *s, const char *emoji, rect r);
/* The font file it reads; overridable for the test. */
#ifndef WISH_EMOJI_FONT
#define WISH_EMOJI_FONT "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"
#endif

#endif
