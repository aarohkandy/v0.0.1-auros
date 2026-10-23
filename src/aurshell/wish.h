/* wish.h — Wishes: what she sees and says (docs/AI.md §4, §7.7, §7.9).
 *
 * She says or types what she wants her computer to do. The engine
 * (src/aurwish, a separate program running as her) turns it into a wish
 * and writes its own sentence about it; this panel shows that sentence
 * and two equal buttons, and nothing happens until she presses Yes.
 *
 * WHAT THIS FILE NEVER DOES
 *
 * It never talks to the internet. The engine is the only thing that
 * does, on a private socket in her runtime directory. It never decides
 * what a wish means and never shows text the cloud wrote: every
 * sentence she reads is the device's (the engine's, or one of the
 * fixed lines below). It never runs anything a wish asks for except
 * the four things only the shell can do -- the look, the text size,
 * opening a program, opening one of her sites -- and those arrive
 * already checked by the engine and are checked again here.
 *
 * Same arrangement as the other panels: one flag in shell_ctx
 * (wish_open), the host calls paint/click/key/motion/step, and the
 * geometry is ONE pure function that painting, pressing and
 * tools/wishtest.c all ask.
 */
#ifndef AUROS_WISH_H
#define AUROS_WISH_H

#include "shell.h"

/* c->wish_open: which part of the panel is up. */
#define WISH_OPEN_ASK   1      /* "What would you like your computer to do?" */
#define WISH_OPEN_MINE  2      /* My wishes, from Settings                    */
#define WISH_OPEN_TODAY 3      /* What my wishes did today, from Help         */

/* The engine's socket, in $XDG_RUNTIME_DIR. Overridable for tests. */
#ifndef WISH_SOCK_NAME
#define WISH_SOCK_NAME "aurwish.sock"
#endif
/* On-device voice (src/aurwish/voice.c). */
#ifndef WISH_VOICE_BIN
#define WISH_VOICE_BIN "/usr/lib/auros/aurvoice"
#endif
/* Where the help pages are (stream K), for an answer to a question. */
#ifndef WISH_HELP_DIR
#define WISH_HELP_DIR "/usr/share/auros/help"
#endif

/* The pages. */
enum {
    WP_CONSENT,   /* first use: what is sent where, Yes / No            */
    WP_ASK,       /* the question, the box, the microphone, examples    */
    WP_LISTEN,    /* the microphone is on                               */
    WP_HEARING,   /* turning what she said into words                   */
    WP_WAIT,      /* "Thinking about your wish…"                        */
    WP_CONFIRM,   /* You said / the device's sentence / Yes / No        */
    WP_CANNOT,    /* "I can't do that yet", maybe with something close  */
    WP_ANSWER,    /* a question: the help page that shows how           */
    WP_DONE,      /* it is on                                           */
    WP_ERROR,     /* offline, busy, resting: one plain line             */
    WP_MINE,      /* My wishes                                          */
    WP_PICK,      /* wishes she can press: the gallery and its blanks   */
    WP_TODAY,     /* What my wishes did today                           */
    WP_N
};

/* What the geometry depends on, and nothing else -- so a test can ask
 * about every page at every size without an engine or a microphone. */
typedef struct {
    int page;
    int n_examples;     /* WP_ASK: how many example wishes are offered */
    int has_offer;      /* CANNOT/ANSWER: a wish she can say yes to    */
    int has_dest;       /* CONFIRM: an outward action (its own screen) */
    int has_help;       /* ANSWER: a help page to open                 */
    int can_retry;      /* ERROR: "Try again" makes sense              */
    int n_rows;         /* MINE: wishes on this page                   */
    int more;           /* MINE: there are more than fit               */
    int removing;       /* MINE: row asking "Remove this wish?", or -1 */
    int voice;          /* the microphone button is offered            */
    int said_len;       /* CONFIRM/WAIT: bytes of what she said        */
    int sent_len;       /* CONFIRM: bytes of the device's sentence     */
    int now_wish;       /* DONE: it ran once (Undo), not a lasting wish */
    int n_items;        /* PICK: choices on this page (before paging)  */
    int pick_mode;      /* PICK: 0 a list of lines, 1 words, 2 pictures */
    int first_item;     /* PICK: the first one shown                   */
    int pick_long;      /* PICK: the longest label, in characters      */
    int sub_len;        /* MINE, TODAY: the line under the heading, in characters */
    int head_len;       /* the heading, in characters                  */
    int mine_long;      /* MINE: the longest wish sentence on the page (with the question, if asked), in characters */
} wish_view;

/* What a target IS, for pressing and for the test. */
enum {
    WT_BOX = 1, WT_MIC, WT_ASK, WT_CLOSE, WT_MINE, WT_EXAMPLE,
    WT_STOP_LISTEN, WT_CANCEL, WT_SAID, WT_YES, WT_NO, WT_SHOW, WT_READ,
    WT_AGAIN, WT_HELP, WT_UNDO, WT_CONSENT_YES, WT_CONSENT_NO,
    WT_SWITCH, WT_REMOVE, WT_REMOVE_YES, WT_REMOVE_NO, WT_ALL_OFF,
    WT_RESTORE, WT_MORE, WT_NEW, WT_GALLERY, WT_BACK, WT_ITEM, WT_STOP_ALL
};

#define WISH_MAX_TARGETS 40
typedef struct { rect r; int kind; int arg; } wish_target;

/* ONE function owns where everything is. Returns how many targets. */
int  wish_targets(const shell_ctx *c, int sw, int sh, const wish_view *v,
                  wish_target *out, int max);

/* ── the host's half ────────────────────────────────────────────── */
void wish_opened(shell_ctx *c);           /* wish_open went 0 -> nonzero */
void wish_closed(shell_ctx *c);
void wish_paint(shell_ctx *c, surface *s, shell_fonts *f);
int  wish_click(shell_ctx *c, int x, int y);
void wish_motion(shell_ctx *c, int x, int y);
int  wish_key(shell_ctx *c, int k);        /* 1 if this panel took it   */
/* Once per pass: the socket, the microphone, the clock. 1 if the screen
 * has something new. Also where the engine's events (overlays, the
 * trigger words, the look) arrive, so it runs whether or not the panel
 * is open. */
int  wish_step(shell_ctx *c);
/* Called on every real input event. At most once a second the engine is
 * told somebody is here (idle wishes, and "3 seconds with no typing"). */
void wish_input_seen(void);
/* The socket, for the host's poll(), or -1. */
int  wish_fd(void);
/* How long the host may sleep, in ms, or -1 for "no opinion". */
int  wish_wait_ms(void);

/* For the overlay (wishover.c): she pressed "Stop this wish" or
 * "Remind me in 10 minutes" on something a wish put on the screen. */
void wish_send_stop(const char *wid);
void wish_send_snooze(const char *wid, int minutes);
/* The answer to the engine's day-after "Keep this wish?" card. */
void wish_send_keep(const char *wid, int yes);
/* What a wish changed, carried out (wishover.c's "Keep this?" uses them
 * to put it back). */
void wish_apply_text_scale(shell_ctx *c, float v);
int  wish_apply_look(shell_ctx *c, const char *look);
int  wish_restore_look(shell_ctx *c, const char *look);

/* "Undo my last wish" is offered for 10 minutes after a wish that
 * changed something now. 1 while it is. */
int  wish_undo_offered(void);
int  wish_clipped(void);          /* texts cut off since the last call (a test hook) */
const char *wish_clip_what(void); /* ... and the start of the last text that was */
void wish_undo(shell_ctx *c);

/* From the shell's host loop: c->want_wish was set by a button that is not
 * Wishes' own (Help's "What my wishes did today" and "Stop all wishes").
 * WISH_CMD_TODAY opens the page; WISH_CMD_STOP_ALL turns every wish off at
 * once and says so. */
#define WISH_CMD_TODAY     1
#define WISH_CMD_STOP_ALL  2
void wish_command(shell_ctx *c, int cmd);

/* Wishes are not on this machine at all (a kiosk, a policy that says
 * wishes="off"): no button anywhere. */
int  wish_available(const shell_ctx *c);

/* The panel's type at docs/DESIGN-DIRECTION.md's sizes, scaled with
 * her text size: shared with wishover.c so a card and the panel speak
 * at the same size. Owned here; never freed by the caller. */
typedef struct {
    font *head, *title, *lead, *body, *label, *sent, *sentb, *said;
} wish_type;
/* `k` is the text scale the geometry settled on (it can be a little
 * below hers when her largest text would not fit this screen); 0 means
 * hers. */
const wish_type *wish_type_for(const shell_ctx *c, const shell_fonts *f, float k);

/* ── for tests and the picture sheet ───────────────────────────── */
void wish_set_page(int page);
int  wish_page(void);
/* Feed one line from the engine, as if it had arrived on the socket. */
void wish_feed_line(shell_ctx *c, const char *line);
/* The last request line the panel wrote (or would have written). */
const char *wish_last_sent(void);
/* The last line the engine sent (for the end-to-end test's diagnostics). */
const char *wish_last_recv(void);

#endif
