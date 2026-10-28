/* welcome.h — phase 9, the part she sees.
 *
 * WHAT THIS PANEL IS FOR
 *
 * The installer has finished. AurOS is on the disk, it started, and
 * she is looking at it. And the machine still starts Windows when it
 * is switched on -- deliberately, because rule 3 of docs/AURBRIDGE.md
 * says nothing about this computer becomes irreversible until AurOS
 * has booted and the person has said it works.
 *
 * This is where she says it. Everything the answer does happens in
 * src/aurfirst, which is a separate program of about four hundred
 * lines that runs as root; this file draws the question, and it is the
 * only thing in the product that ever asks it.
 *
 * WHY IT OPENS BY ITSELF, AND KEEPS COMING BACK
 *
 * A notification would be wrong twice. It goes away, and the one thing
 * that must not go away is the only route to the decision; and it is
 * four lines of text, where this is the sentence that explains why her
 * computer has two operating systems on it this morning.
 *
 * So it opens on the first frame of every session until it is
 * answered. "Let me look first" closes it for this session and it is
 * back tomorrow -- which is not nagging, it is the promise: nothing is
 * decided until she decides it, and the question does not expire. The
 * panel says so, in those words, because a person who does not know it
 * will come back is a person who answers it to make it go away.
 *
 * WHAT IT NEVER DOES
 *
 * It does not write NVRAM, mount anything, or run Ferry. It writes one
 * word into aurshell's own runtime directory and waits for an answer,
 * and rootfs/usr/lib/auros/answer.sh -- started by a path unit, as
 * root -- is what acts on it. Three words are accepted and anything
 * else is refused.
 *
 * That is not the same as saying this panel is harmless if it is
 * wrong, and an earlier version of this comment did say so. One of the
 * three words is `confirm`, which rewrites what the computer starts.
 * The bound is not "nothing happens", it is "nothing happens that she
 * could not have asked for by pressing the button in front of her".
 *
 * IT READS THE ANSWER SOMEWHERE ELSE. The request goes into her own
 * runtime directory; the answer, the import log and the report come
 * out of /run/auros-answer, which belongs to root. That asymmetry is
 * the whole of the privilege boundary: root never opens a path she
 * owns. src/aurfirst/request.c has the escalation it closes.
 */
#ifndef AUROS_WELCOME_H
#define AUROS_WELCOME_H

#include "shell.h"

/* Where the two halves meet. Overridable at compile time for
 * tools/welcometest.c and for nothing else. */
#ifndef WELCOME_RUN
#define WELCOME_RUN   "/run/auros"
#endif
/* Where the root side answers. Root-owned and world-readable; nothing
 * the desktop writes goes here. */
#ifndef WELCOME_ANSWER
#define WELCOME_ANSWER "/run/auros-answer"
#endif
/* Written by aurfirst on every run, read here. One small key=value
 * file rather than the shell spawning a program and parsing it: the
 * desktop should not have to become root-adjacent to find out what
 * question to ask. */
#ifndef WELCOME_STATE
#define WELCOME_STATE "/run/auros-first.state"
#endif
/* firstboot.sh leaves this behind if there is a Windows on the disk
 * worth importing from. Its absence is why the import is offered on
 * some machines and not others. */
#ifndef WELCOME_FOUND
#define WELCOME_FOUND "/var/lib/auros/windows-found.json"
#endif

/* `ferry`, which this panel runs only to LIST the USB sticks (it reads, it
 * writes nothing, and it needs no privilege); saving onto one is the root
 * side's (answer.sh "stick:"). Overridable for tools/welcometest.c. */
#ifndef WELCOME_FERRY
#define WELCOME_FERRY "/usr/bin/ferry"
#endif

/* Which screen.
 *
 * The first nine are the question and what follows it. The rest are the
 * things Ferry has to ask or say (docs/FERRY.md): whose files, when there
 * are several Windows people; that there was nothing to bring; saving her
 * files on a USB stick (plug in, choose, saving, done); and why Put
 * Windows back could not save them in Windows first. */
enum { W_ASK, W_WORKING, W_CONFIRMED, W_IMPORTING, W_IMPORTED,
       W_DECLINED, W_TROUBLE, W_SURE_YES, W_SURE_NO,
       W_WHOSE, W_EMPTY, W_STICK_PLUG, W_STICK_PICK, W_STICK_SAVING,
       W_STICK_DONE, W_BLOCKED, W_PAGE_N };

/* "Save my files to a USB stick first", from Settings (Put Windows back).
 * Opens at "plug in a stick" or, when one is already there, at the choice. */
void welcome_open_stick(shell_ctx *c);

/* Put Windows back could not save her files in Windows first, and says why
 * (answer.sh's why=): toomany, locked, noroom, dirty, nodrive, some -- or
 * anything else for the plain sentence. Offers the USB stick, and says what
 * is and is not changed. */
void welcome_open_blocked(shell_ctx *c, const char *why);

/* Called once, when the shell starts. Reads the state and decides
 * whether the question needs asking; sets c->welcome_open if it does.
 * Returns 1 if the panel is up. */
int  welcome_init(shell_ctx *c);

void welcome_paint(shell_ctx *c, surface *s, shell_fonts *f);
int  welcome_click(shell_ctx *c, int x, int y);
void welcome_motion(shell_ctx *c, int x, int y);
int  welcome_key(shell_ctx *c, int k);     /* 1 if this panel took it */

/* Once per pass. 1 if the screen has something new -- which is how a
 * request that has been answered, or an import that has said another
 * line, reaches the screen. */
int  welcome_step(shell_ctx *c);
/* How long the host may sleep, in ms, or -1 for "no opinion". Short
 * only while something is in flight: an idle desktop that polls four
 * times a second for a file nobody is going to write is a laptop with
 * an hour less battery. */
int  welcome_wait_ms(void);

/* ── measuring it ───────────────────────────────────────────────────
 *
 * The same contract the wifi, settings and headphones panels keep: the
 * layout is a pure function of a VIEW and a panel size, so
 * tools/targets.c can measure every screen at every text size on a
 * machine that has no NVRAM, no Windows and nobody pressing anything.
 */
typedef struct {
    int page;
    int can_import;     /* there is a Windows on this disk to read   */
} welcome_view;

int  welcome_targets(const shell_ctx *c, int sw, int sh,
                     const welcome_view *v, rect *out, int max);
/* For the test: drive the panel without a screen. */
void welcome_set_page(int page);
int  welcome_page(void);
/* ...and give it the people ("Bob,Harold") and the sticks (the lines
 * `ferry stick list` prints: device, name (size), bytes, file system, tab
 * separated) a real run would have read. */
void welcome_test_people(const char *csv);
void welcome_test_sticks(const char *lines);
void welcome_test_blocked(const char *why);
/* The rest of what a page reads: opened from Settings (ctx), because Windows
 * could not hold her files, how many were saved, whether some were not, and
 * the line a running copy would show. */
void welcome_test_state(int ctx, int from_blocked, int files, int partial,
                        const char *progress);

#endif
