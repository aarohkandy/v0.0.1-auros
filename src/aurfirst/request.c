/* request.c — reading the one word the desktop is allowed to ask for.
 *
 * WHY THIS IS C AND NOT THREE LINES OF SHELL, which is what it was.
 *
 * The desktop runs as the person using the machine. It asks for
 * something by creating a file in aurshell's own runtime directory --
 * mode 0700, owned by her -- and a systemd path unit runs the answer
 * as root. The argument for that arrangement over a setuid binary is
 * written in auros-answer.path and it is a good argument, and the
 * first implementation of it was strictly worse than the thing it
 * rejected.
 *
 * SHE OWNS THAT DIRECTORY, SO SHE DECIDES WHAT EVERY NAME IN IT IS.
 * The shell version did `head -c 64 "$REQ"`, `: > "$RES.new"`,
 * `>> "$RES.new"` and `chmod 0644 "$RES.new"` -- as root, through
 * paths she controls, with no O_NOFOLLOW anywhere. An adversarial
 * review reproduced all four primitives:
 *
 *   ln -s /etc/shadow /run/auros/answer.result.new   -> truncated,
 *      appended to, and chmod 0644, by root, from one request.
 *   ln -s <a root-only file> /run/auros/answer       -> the first 64
 *      bytes of it, filtered, echoed back into a file she can read.
 *
 * That is a local root escalation on every built image, reachable by
 * anything running as her -- a compromised browser tab is enough.
 *
 * The fix is structural rather than a longer list of checks:
 *
 *   ROOT NEVER OPENS A PATH SHE OWNS FOR WRITING. Every answer, log
 *   and report is written into /run/auros-answer, which is the
 *   answering service's own RuntimeDirectory: root-owned, mode 0755,
 *   world-readable. The desktop reads from there.
 *
 *   THE ONE PATH ROOT DOES READ IS OPENED O_NOFOLLOW, and then
 *   fstat()ed on the descriptor it will actually read -- not stat()ed
 *   on the name, which is a race she wins by swapping the file
 *   afterwards. A symlink is not followed, a directory is not read, a
 *   FIFO does not block us, and what is removed is removed by
 *   unlinkat on the same directory descriptor.
 *
 * And the word is still not a command: it is matched against a fixed
 * list by the caller, and anything else is reported as unknown without
 * being echoed anywhere she could not already read.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aurfirst.h"

/* The desktop's runtime directory. Overridable at compile time and, on
 * a build that asked for it, by the environment -- which exists for
 * tools/aurfirsttest.sh and for nothing else, and is guarded so that a
 * shipped binary cannot be pointed somewhere else by whoever starts
 * it. */
#ifndef AF_RUN_USER
#define AF_RUN_USER "/run/auros"
#endif

static const char *run_dir(void)
{
#ifdef AF_ALLOW_ENV_DIRS
    const char *e = getenv("AF_RUN_USER");
    if (e && *e == '/') return e;
#endif
    return AF_RUN_USER;
}

/* The five, and nothing else. Spelled here so that the shell around
 * this cannot widen the list by accident. `putback` restarts the
 * computer into the restore that removes AurOS: the bound is still
 * "nothing she could not have asked for by pressing the button in
 * front of her", and that button asks twice. `windows` starts Windows
 * for one start and changes nothing else: BootNext at the entry that
 * starts Windows Boot Manager (aurfirst windows), then a restart. */
static int is_one_of_ours(const char *w)
{
    return !strcmp(w, "confirm") || !strcmp(w, "decline") ||
           !strcmp(w, "import")  || !strcmp(w, "putback") ||
           !strcmp(w, "windows");
}

/* "import:NAME" -- the answer to "whose files?" when Ferry found several
 * Windows people (docs/FERRY.md, exit 6, `users=Bob,Harold`). NAME is
 * only ever an argument to `ferry run --user`, which accepts nothing but
 * a real account; this is the first of two doors, and it keeps out
 * everything that could be read as something else:
 *
 *   - a path (`/`, `\\`, a name made of dots, `..`);
 *   - an option (a leading `-`);
 *   - anything Windows itself does not allow in an account name
 *     (`" / \\ [ ] : ; | = , + * ? < >`), and control characters
 *     (so no newline can start a second line of the request);
 *   - leading or trailing space or dot, and an empty or long (> 64
 *     bytes) name.
 * Spaces, `$`, `(`, quotes and non-ASCII letters are allowed -- real
 * people have them -- and are safe because the caller only ever hands
 * the name over as one quoted argument, never to a shell to read. */
#define IMPORT_PREFIX "import:"
#define NAME_MAX_BYTES 64

static int name_ok(const unsigned char *s, size_t n)
{
    if (n == 0 || n > NAME_MAX_BYTES) return 0;
    if (s[0] == '-' || s[0] == '.' || s[0] == ' ') return 0;
    if (s[n - 1] == ' ' || s[n - 1] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (c < 0x20 || c == 0x7f) return 0;
        if (strchr("\"/\\[]:;|=,+*?<>", c)) return 0;
    }
    return 1;
}

/* Remove it, whatever it turned out to be. unlinkat on the directory
 * descriptor, so nothing here ever resolves the name a second time --
 * and AT_REMOVEDIR as well, because a DIRECTORY called `answer` used
 * to wedge the machine for ever: nothing removed it, PathExists stayed
 * true so the unit never edged again, and the desktop's O_EXCL create
 * failed for ever. One `mkdir` by anything running as her took away
 * her ability to say "No, go back to Windows". */
static void drop(int dir)
{
    if (unlinkat(dir, "answer", 0) == 0) return;
    unlinkat(dir, "answer", AT_REMOVEDIR);
}

int af_request_take(char *out, size_t n)
{
    if (n < 2) return -1;
    out[0] = 0;

    int dir = open(run_dir(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return -1;

    /* O_NONBLOCK as well as O_NOFOLLOW, and it is not an optimisation:
     * openat on a FIFO without it blocks until somebody opens the
     * other end, which on a name she controls is root hanging for ever
     * on request. On a regular file it does nothing at all. */
    int fd = openat(dir, "answer",
                    O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        int gone = (errno == ENOENT);
        drop(dir);
        close(dir);
        /* SOMETHING WAS THERE AND IT WAS NOT A FILE. That is still a
         * request as far as the desktop is concerned -- it is sitting
         * on "One moment" waiting for an answer -- so it gets one.
         * Returning "nothing there" left the panel waiting for ever. */
        if (gone) return -1;
        snprintf(out, n, "%s", "unknown");
        return 1;
    }
    struct stat st;
    /* st_nlink is the one shape O_NOFOLLOW does not catch: a HARD LINK
     * to a root-only file is a regular file, and reading it would put
     * its first bytes where she can see them. */
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1) {
        close(fd);
        drop(dir);
        close(dir);
        snprintf(out, n, "%s", "unknown");
        return 1;
    }
    char raw[128];
    ssize_t k = read(fd, raw, sizeof raw - 1);
    close(fd);
    drop(dir);
    close(dir);
    if (k < 0) { snprintf(out, n, "%s", "unknown"); return 1; }
    raw[k] = 0;

    /* THE WORD EXACTLY, with at most a newline after it. This used to
     * keep the letters and drop everything else, so "con firm" and
     * "put back" were read as the words they spell -- harmless, since
     * she may send those words anyway, but a fixed list matched after
     * the input has been rewritten is not a fixed list. Anything else
     * in the file makes it unknown. */
    if (k > 0 && raw[k - 1] == '\n') raw[--k] = 0;
    /* "import:NAME": checked as a whole, or not at all. */
    if (k > (ssize_t)strlen(IMPORT_PREFIX) &&
        strncmp(raw, IMPORT_PREFIX, strlen(IMPORT_PREFIX)) == 0) {
        size_t pl = strlen(IMPORT_PREFIX), nl = (size_t)k - pl;
        if (name_ok((const unsigned char *)raw + pl, nl) &&
            pl + nl + 1 <= n) {
            memcpy(out, raw, pl + nl);
            out[pl + nl] = 0;
            return 0;
        }
        snprintf(out, n, "%s", "unknown");
        return 1;
    }
    size_t o = 0;
    int stray = 0;
    for (ssize_t i = 0; i < k; i++) {
        if (raw[i] < 'a' || raw[i] > 'z' || o + 1 >= n || o >= 16) { stray = 1; break; }
        out[o++] = raw[i];
    }
    out[o] = 0;
    if (stray) out[0] = 0;
    /* AND IT IS NOT ECHOED IF IT IS NOT ONE OF OURS. The shell version
     * wrote `request=<the filtered bytes>` into a file she can read,
     * which on a symlinked request was a read oracle for a file she
     * cannot. */
    if (!out[0] || !is_one_of_ours(out)) {
        snprintf(out, n, "%s", "unknown");
        return 1;
    }
    return 0;
}
