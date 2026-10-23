/* notify.h — when a program has something to tell her.
 *
 * Nothing in this image implemented org.freedesktop.Notifications, and
 * that is not a missing nicety. It is the one way a Linux desktop
 * application has of saying anything outside its own window, and every
 * toolkit on the machine calls it:
 *
 *   the browser     a download finished, or failed
 *   her files       the USB stick can be taken out now
 *   the store       the program you asked for is installed
 *   printers        out of paper; the job was sent
 *
 * With no server on the bus, every one of those calls comes back
 * "org.freedesktop.DBus.Error.ServiceUnknown" and the application
 * shrugs. She takes the USB stick out too early, waits for a download
 * that finished ten minutes ago, and presses Install a second time.
 * Nothing anywhere says anything. It is the same shape as every other
 * bug this product keeps finding in itself, one layer out: a thing
 * that looks like it works and does not.
 *
 * WHY THE SHELL AND NOT A PACKAGE
 *
 * The two usual answers do not fit. dunst is X11. mako wants
 * wlr-layer-shell, which this compositor does not implement and should
 * not have to. Both would draw in their own colours, at their own text
 * size, in their own words -- on a machine whose whole argument is
 * that those three things are hers to set.
 *
 * So the shell is the server. It already owns the screen, the theme,
 * her chosen text size and the word list in docs/EASY.md.
 *
 * WHY libdbus
 *
 * The same call the compositor made about libwayland-server. The wire
 * format is not the interesting part, and hand-rolling a parser for
 * a{sv} variants arriving from every program on the machine is a
 * security surface, not a craft exercise. Everything a person SEES
 * here is this program's.
 *
 * NOTHING VANISHES, NOTHING PILES UP (docs/DESIGN-DIRECTION.md §5 C6)
 *
 * A card used to go by itself after eight seconds, which was argued
 * past docs/EASY.md rule 5 as "nothing she NEEDS is on a timer". It was
 * still a sentence taken away while a slow reader was halfway through
 * it, and four of them stacked down the side of her screen over
 * whatever was there. Now:
 *
 *   - ONE card, the newest, in a fixed corner of her look that has no
 *     button under it. When more are waiting it says "3 messages", and
 *     that is a button which opens them all.
 *   - It stays until she presses OK or opens Messages. Nothing here
 *     runs on a clock.
 *   - A program that says the same thing again -- a download's
 *     percentage, the volume -- changes the card it already has (by
 *     its id, or by the tag those notices carry) instead of adding one.
 *   - Everything said is kept, newest first, in a Messages list opened
 *     by pressing the clock. Pressing OK puts the card away; it does
 *     not throw the words away.
 */
#ifndef AUROS_NOTIFY_H
#define AUROS_NOTIFY_H

#include "shell.h"

/* How many messages are kept for the Messages list. The oldest she has
 * already seen goes first; the shell's own warnings she has not seen
 * never go to make room. */
#define NOTIFY_KEEP     16
#define NOTIFY_SUMMARY  128
#define NOTIFY_BODY     320
#define NOTIFY_APP      64
/* docs/DESIGN-DIRECTION.md §3: every target 48 px on its shorter side. */
#define NOTIFY_TARGET   48

/* Open the connection and claim the name, if it is not already
 * claimed. Idempotent and rate-limited, so the host may call it every
 * frame: it retries until it succeeds, because the bus the desktop
 * ends up on is not the bus that exists when the shell starts (see
 * run.c). Returns 1 while the service is ours. */
int  notify_open(void);
/* Give the name up -- used when the shell moves to logind's bus and
 * has to re-announce itself there. */
void notify_close(void);
/* Give the name up and forget every message. */
void notify_fini(void);

/* For the host's poll(). -1 when there is no connection. */
int  notify_fd(void);

/* The longest the host may sleep, in milliseconds, or -1 for "no
 * opinion, sleep as long as you like".
 *
 * Joining a bus is several round trips -- libdbus's own authentication
 * exchange, then Hello, then RequestName -- and some of them are
 * waiting to WRITE rather than to read, so watching the socket for
 * readability is not enough to wake for them. An idle desktop polls
 * for a second at a time, which turned a handshake of eight passes
 * into eight seconds and timed it out. Rather than teach the host
 * about DBusWatch for the sake of one second of one boot, the join
 * simply asks it not to sleep long while it is happening. */
int  notify_wait_ms(void);
/* Read whatever arrived. 1 if the screen has something new to show. */
int  notify_pump(shell_ctx *c);

/* How many she has not seen yet: what the card stands for ("3
 * messages"). 0 means there is no card. */
int  notify_showing(void);
/* How many are in the Messages list, seen or not. */
int  notify_kept(void);
/* 1 while the Messages list is open; and which message it starts at. */
int  notify_list_open(void);
int  notify_list_first(void);

/* WHAT IS ACTUALLY IN MESSAGE `i` (0 is the newest), after everything a
 * sending program asked for has been through the sanitiser. Read-only,
 * NUL-terminated, and "" for one that is not there.
 *
 * This exists because counting cards proves nothing about the text in
 * them, and the text IS the notification: a harness that checked only
 * notify_showing() passed while "Saved <report 2024>.pdf" was being
 * drawn as "Saved .pdf". */
const char *notify_card_app(int i);
const char *notify_card_summary(int i);
const char *notify_card_body(int i);
/* 1 if this is the shell's own warning, which is never pushed out of
 * the list to make room -- and which only the shell may ask for. */
int         notify_card_sticky(int i);
/* The id the sending program knows this message by. Two sharing one
 * id means CloseNotification closes the wrong one and the real owner
 * can never replace or close its own. */
unsigned    notify_card_id(int i);

/* ── the geometry contract ──────────────────────────────────────────
 *
 * A pure function of a VIEW and a screen size, so a harness can measure
 * the card at every text size on a machine with no D-Bus at all. A
 * geometry function that can only be asked about the state the machine
 * happens to be in is a geometry function nothing can check. */
typedef struct {
    int   unseen;       /* the card stands for this many; 0 = no card   */
    int   kept;         /* rows the Messages list has to show           */
    int   list_open;
    float text_scale;   /* hers */
    int   foot_h;       /* the band's height; nothing here goes over it */
    int   bar_h;        /* the look's strip, when it has no clock       */
    /* Where the look drew its clock (c->clock_r). The card and the list
     * hang from it, and they never cover it: it is how Messages opens. */
    rect  clock;
    /* The card's corner is the bottom right rather than the top right:
     * looks whose clock is at the bottom, and "One thing at a time",
     * whose top right is a tile. */
    int   bottom;
    /* How tall the card's words are (their lines, wrapped to the card's
     * width, at most two for the heading and two for the rest):
     * measured when it was painted, so a press lands on the card she
     * sees. 0 means "one line of each". */
    int   text_h;
    /* Which message the Messages list starts at (0 the newest): the
     * list shows what fits and has Older / Newer for the rest, because
     * a message she cannot reach is a message that has vanished. */
    int   first;
} notify_view;

typedef struct {
    rect card, ok, more;   /* card.w 0: no card. more.w 0: only one     */
    rect list, close;      /* list.w 0: the list is shut                */
    rect older, newer;     /* w 0: nothing further that way              */
    rect row[NOTIFY_KEEP];
    int  rows;             /* how many rows are drawn, from message `first` */
    int  per;              /* how many make a page: Older / Newer step it */
    int  first;
} notify_geom;

void notify_layout(int sw, int sh, const notify_view *v, notify_geom *g);
/* The live view, for the host and for a harness that wants the real
 * one rather than an invented one. */
void notify_view_now(const shell_ctx *c, notify_view *v);

/* Words wrapped at spaces to width w, lines lh apart from baseline y;
 * at most max_lines drawn, the last ending in "…" only if a program's
 * text is longer than that. Returns the lines the whole text needs;
 * s NULL only measures. Shared with osd.c. */
int  notify_wrap(surface *s, font *f, float x, float y, float w, float lh,
                 int max_lines, const char *t, uint32_t col, float a);

/* Painted above the archetype and below the panels and the band -- the
 * way out is never covered by it. Full-height surface. */
void notify_paint(shell_ctx *c, surface *s, shell_fonts *f);
/* A press, screen coordinates. Returns 1 if it was the card's, the
 * list's or the clock's, in which case nothing else may act on it. */
int  notify_click(shell_ctx *c, int x, int y);

/* A key, while the Messages list is open: Escape closes it. Returns 1
 * if it was taken -- every key is, while the list is up, for the reason
 * the panels give. 0 when the list is shut, so a card never takes a key
 * from the program she is typing in. */
int  notify_key(shell_ctx *c, int keycode);

/* Put one up from inside the shell itself, in her words. The battery
 * warning and "this program would not start" are notifications, not
 * on-screen messages: they are things she may want to read twice. */
void notify_local(const char *summary, const char *body);

/* The same, for good news about a program: pressing the card opens it.
 * "VLC is ready" / "Press here to open it." -- app_id is the program's
 * id in the app table. The host collects the press with
 * notify_take_open() and starts the program the way a home screen
 * would, so the archetype brings it into view. */
void notify_local_open(const char *summary, const char *body, const char *app_id);
/* 1 and the id, once, if a press on a card asked for a program. */
int  notify_take_open(char *out, size_t n);

#endif
