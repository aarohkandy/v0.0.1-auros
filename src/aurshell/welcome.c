/* welcome.c — see welcome.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "welcome.h"
#include "draw.h"
#include "run.h"

#define NOTE_MAX  200
#define WEL_PEOPLE 4          /* Windows people the question can name       */
#define WEL_STICKS 3          /* USB sticks the choice can show             */

static struct {
    int  page;
    int  can_import;
    int  asked;             /* a word is in flight                   */
    char wanted[16];        /* WHICH word. See welcome_step.         */
    int  partial;           /* the import finished with things left  */
    int  hover_act;
    int  sel;               /* which action the keyboard is on       */
    char note[NOTE_MAX];    /* what went wrong, in the answer's words */
    int  checking;          /* she went to check; back when it closes */
    int  check_panel;       /* ...a panel (Wi-Fi), not a program      */
    int  check_wins;        /* how many windows were open before      */
    int  keyed;             /* she has moved the selection by keyboard */

    /* Ferry's questions (docs/FERRY.md). */
    int  n_people;          /* "whose files?": the names, hers first    */
    char person[WEL_PEOPLE][48];
    int  n_sticks;          /* the USB sticks `ferry stick list` showed */
    struct { char dev[32]; char name[72]; } stick[WEL_STICKS];
    uint32_t looked_at;     /* when the sticks were last looked for      */
    struct timespec asked_at; /* when the request went in: an answer older than this is not its answer */
    int  ctx;               /* 1: opened from Settings, not the first start */
    int  from_blocked;      /* the stick was offered because Windows could not hold her files */
    char why[16];           /* why Put Windows back could not save them  */
    char progress[160];     /* the newest plain line of the running copy */
    int  files;             /* how many files the copy says it saved     */
} W = { .hover_act = -1, .sel = 0 };

static uint32_t now_ms_(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* Is this computer on a network? net.c answers from the kernel's routing
 * table; a harness that does not link net.c has no opinion, which here
 * means "online" -- the check then simply offers the internet. */
extern int net_online(void) __attribute__((weak));
static int offline_now_(void) { return net_online && net_online() == 0; }

/* ── reading the two files this panel is allowed to read ─────────── */

static int slurp(const char *path, char *buf, size_t n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t k = read(fd, buf, n - 1);
    close(fd);
    if (k < 0) return -1;
    buf[k] = 0;
    return (int)k;
}

/* value of `key=` in a key=value blob, or "" */
static void field(const char *blob, const char *key, char *out, size_t n)
{
    out[0] = 0;
    size_t kl = strlen(key);
    for (const char *p = blob; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t i = 0;
            while (v[i] && v[i] != '\n' && i + 1 < n) { out[i] = v[i]; i++; }
            out[i] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

/* What aurfirst last said about this machine. */
static void read_state(char *phase, size_t n)
{
    char blob[1024];
    phase[0] = 0;
    if (slurp(WELCOME_STATE, blob, sizeof blob) < 0) return;
    field(blob, "phase", phase, n);
}

static int windows_worth_importing(void)
{
    struct stat st;
    /* ferry-detect writes JSON. An empty object is "nothing found",
     * and an absent file is a machine where the scan never ran -- both
     * mean there is nothing to offer, and offering it anyway is a
     * button that fails when pressed, which this product keeps
     * deciding is worse than no button. */
    if (stat(WELCOME_FOUND, &st) != 0) return 0;
    if (st.st_size < 16) return 0;
    char blob[4096];
    if (slurp(WELCOME_FOUND, blob, sizeof blob) < 0) return 0;
    return strstr(blob, "\"device\"") != NULL || strstr(blob, "\"windows\"") != NULL;
}

/* ── asking for something ────────────────────────────────────────── */

/* `expect` is the request name the answer will carry: "import:Harold" is
 * answered as request=import, "stick:/dev/sdb1" as request=stick. */
static int ask_word(const char *word, const char *expect)
{
    snprintf(W.wanted, sizeof W.wanted, "%s", expect);
    W.asked = 1;
    W.note[0] = 0;
    W.progress[0] = 0;
    W.files = 0;
    /* The file system's own clock (coarse), so a result written just after
     * this carries a time that is not before it. */
    clock_gettime(CLOCK_REALTIME_COARSE, &W.asked_at);
    char path[512];
    snprintf(path, sizeof path, "%s/answer", WELCOME_RUN);
    /* O_EXCL: two presses in the same second must not make two
     * requests, and a request already sitting there is one the root
     * side has not picked up yet.
     *
     * AND `W.asked` IS SET EITHER WAY. It used to be set only on the
     * path that created the file, so a leftover request -- from a
     * previous session, or from an answering service that was killed
     * -- made this return "in flight" while nothing ever polled for
     * the answer. The panel then sat on "One moment" for ever, which
     * is the one screen with no way out of it. */
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0 && errno == EEXIST) return 0;
    if (fd < 0) { W.asked = 0; return -1; }
    ssize_t k = write(fd, word, strlen(word));
    close(fd);
    if (k != (ssize_t)strlen(word)) { W.asked = 0; return -1; }
    return 0;
}
static int ask_for(const char *word) { return ask_word(word, word); }

/* ── what Ferry told us ──────────────────────────────────────────── */

/* The name in "import:NAME" goes through src/aurfirst/request.c's door, which
 * refuses what Windows does not allow in an account name and anything that
 * could be read as something else. A name that door would refuse is not
 * offered: a button whose answer never comes is the one thing worse than none. */
static int person_ok(const char *n)
{
    size_t l = strlen(n);
    if (l == 0 || l > 64) return 0;
    if (n[0] == '-' || n[0] == '.' || n[0] == ' ') return 0;
    if (n[l - 1] == ' ' || n[l - 1] == '.') return 0;
    for (size_t i = 0; i < l; i++) {
        unsigned char ch = (unsigned char)n[i];
        if (ch < 0x20 || ch == 0x7f || strchr("\"/\\[]:;|=,+*?<>", ch)) return 0;
    }
    return 1;
}

/* "Bob,Harold" -> the names, hers first (the one that is this account's own
 * name, if one is). More than the page can show are left out; a house with
 * five Windows accounts is not a house this version asks well. */
static void people_from(const char *csv)
{
    char all[WEL_PEOPLE * 3][48];
    int n = 0;
    W.n_people = 0;
    for (const char *p = csv; p && *p && n < (int)(sizeof all / sizeof all[0]); ) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l > 0 && l < sizeof all[0]) {
            memcpy(all[n], p, l); all[n][l] = 0;
            if (person_ok(all[n])) n++;
        }
        p = e ? e + 1 : p + l;
    }
    const char *me = getenv("USER");
    int first = -1;
    for (int i = 0; i < n && me && *me; i++) if (!strcasecmp(all[i], me)) { first = i; break; }
    if (first >= 0 && W.n_people < WEL_PEOPLE) snprintf(W.person[W.n_people++], sizeof W.person[0], "%.47s", all[first]);
    for (int i = 0; i < n && W.n_people < WEL_PEOPLE; i++)
        if (i != first) snprintf(W.person[W.n_people++], sizeof W.person[0], "%.47s", all[i]);
}

/* The lines `ferry stick list` prints: device, "name (N GB)", bytes, file
 * system, tab separated. Anything else on its output (an error, a warning) is
 * not a line about a stick and is skipped. */
static void sticks_from(const char *text)
{
    W.n_sticks = 0;
    for (const char *p = text; p && *p && W.n_sticks < WEL_STICKS; ) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        char line[256];
        if (l > 0 && l < sizeof line && !strncmp(p, "/dev/", 5)) {
            memcpy(line, p, l); line[l] = 0;
            char *t1 = strchr(line, '\t');
            if (t1) {
                *t1++ = 0;
                char *t2 = strchr(t1, '\t');
                if (t2) *t2 = 0;
                /* the device must be the plain name request.c lets through */
                int good = strlen(line) > 5 && strlen(line) <= 5 + 24;
                for (const char *q = line + 5; *q && good; q++)
                    if (!((*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9'))) good = 0;
                if (good && *t1) {
                    snprintf(W.stick[W.n_sticks].dev, sizeof W.stick[0].dev, "%.31s", line);
                    /* "Name (16 GB)": a long name gives up its own end, never the size. */
                    char nm[72];
                    snprintf(nm, sizeof nm, "%.71s", t1);
                    char *sz = strrchr(nm, '(');
                    if (sz && sz > nm && (size_t)(sz - nm) > 22) {
                        char tail[24];
                        snprintf(tail, sizeof tail, "%.23s", sz);
                        size_t keep = 20;
                        while (keep > 0 && ((unsigned char)nm[keep] & 0xC0) == 0x80) keep--;
                        nm[keep] = 0;
                        snprintf(W.stick[W.n_sticks].name, sizeof W.stick[0].name, "%.40s\xE2\x80\xA6 %.23s", nm, tail);
                    } else {
                        snprintf(W.stick[W.n_sticks].name, sizeof W.stick[0].name, "%s", nm);
                    }
                    W.n_sticks++;
                }
            }
        }
        p = e ? e + 1 : p + l;
    }
}

/* Look for sticks now. Quick (lsblk) and bounded; only while the page that
 * wants them is up, every couple of seconds, never in the paint. */
static int sticks_look(void)
{
    char out[2048];
    char before[sizeof W.stick];
    int nb = W.n_sticks;
    memcpy(before, W.stick, sizeof before);
    const char *argv[] = { WELCOME_FERRY, "stick", "list", NULL };
    int n = run_capture(argv, out, sizeof out, 1500);
    W.looked_at = now_ms_();
    sticks_from(n > 0 ? out : "");
    return nb != W.n_sticks || memcmp(before, W.stick, sizeof before) != 0;
}

/* The newest plain line of a running copy (import.log / stick.log in the
 * root side's directory). Ferry never puts a file's name on it. Returns 1
 * if it is a different line from the last. */
static int progress_look(const char *log)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", WELCOME_ANSWER, log);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    char buf[2048];
    off_t sz = lseek(fd, 0, SEEK_END);
    off_t from = sz > (off_t)sizeof buf - 1 ? sz - (off_t)(sizeof buf - 1) : 0;
    ssize_t k = pread(fd, buf, sizeof buf - 1, from);
    close(fd);
    if (k <= 0) return 0;
    buf[k] = 0;
    char *end = buf + k;
    while (end > buf && (end[-1] == '\n' || end[-1] == ' ')) *--end = 0;
    char *start = strrchr(buf, '\n');
    start = start ? start + 1 : buf;
    if (!*start) return 0;
    char line[sizeof W.progress];
    snprintf(line, sizeof line, "%.159s", start);
    if (!strcmp(line, W.progress)) return 0;
    memcpy(W.progress, line, sizeof W.progress);
    return 1;
}

static void open_stick_page(shell_ctx *c)
{
    W.ctx = 1;
    sticks_look();
    W.page = W.n_sticks ? W_STICK_PICK : W_STICK_PLUG;
    W.sel = 0; W.keyed = 0; W.note[0] = 0; W.progress[0] = 0;
    c->welcome_open = 1;
}

void welcome_open_stick(shell_ctx *c)
{
    W.from_blocked = 0;
    open_stick_page(c);
}

void welcome_open_blocked(shell_ctx *c, const char *why)
{
    W.ctx = 1;
    W.from_blocked = 1;
    snprintf(W.why, sizeof W.why, "%s", why ? why : "");
    W.page = W_BLOCKED;
    W.sel = 0; W.keyed = 0; W.note[0] = 0;
    c->welcome_open = 1;
}

void welcome_test_people(const char *csv) { people_from(csv); }
void welcome_test_sticks(const char *lines) { sticks_from(lines); }
void welcome_test_state(int ctx, int from_blocked, int files, int partial, const char *progress)
{
    W.ctx = ctx; W.from_blocked = from_blocked; W.files = files; W.partial = partial;
    snprintf(W.progress, sizeof W.progress, "%.159s", progress ? progress : "");
}
void welcome_test_blocked(const char *why) { snprintf(W.why, sizeof W.why, "%s", why ? why : ""); W.from_blocked = 1; W.ctx = 1; }


/* ── the state machine ───────────────────────────────────────────── */

int welcome_init(shell_ctx *c)
{
    char phase[32];
    read_state(phase, sizeof phase);
    W.can_import = windows_worth_importing();
    W.page = W_ASK;
    W.sel = 0;
    /* ONLY WHILE THE QUESTION IS OPEN. A machine that answered it
     * yesterday, or was never converted from anything, sees nothing:
     * a desktop that greets you every morning is a desktop you stop
     * reading. */
    c->welcome_open = !strcmp(phase, "asking");
    return c->welcome_open;
}

int welcome_wait_ms(void)
{
    if (W.asked) return 200;
    /* Looking for a stick she is about to plug in: twice a second is plenty. */
    if (W.page == W_STICK_PLUG || W.page == W_STICK_PICK) return 500;
    return -1;
}

int welcome_step(shell_ctx *c)
{
    /* She pressed "Open Your files" or "Open Internet" to check. When
     * she has closed what that opened, the question is back where she
     * left it: one answer away, not tomorrow. */
    if (W.checking && !c->welcome_open && c->n_wins <= W.check_wins &&
        !c->want_launch && !SHELL_PANEL_OPEN(c)) {
        W.checking = 0;
        c->welcome_open = 1;
        return 1;
    }
    if (!c->welcome_open) return 0;
    int dirty = 0;
    /* Plugging a stick in moves the page on by itself. */
    if ((W.page == W_STICK_PLUG || W.page == W_STICK_PICK) && !W.asked &&
        (uint32_t)(now_ms_() - W.looked_at) >= 2000) {
        if (sticks_look()) dirty = 1;
        int want = W.n_sticks ? W_STICK_PICK : W_STICK_PLUG;
        if (want != W.page) { W.page = want; W.sel = 0; W.keyed = 0; dirty = 1; }
    }
    if (!W.asked) return dirty;
    /* What the copy says it is doing, while it runs. */
    if (W.page == W_IMPORTING && progress_look("import.log")) dirty = 1;
    if (W.page == W_STICK_SAVING && progress_look("stick.log")) dirty = 1;

    char path[512];
    snprintf(path, sizeof path, "%s/result", WELCOME_ANSWER);
    char blob[2048];
    /* THE ANSWER TO THIS ASK, not the last one with the same name. "Whose
     * files?" is asked as import and answered as request=import
     * result=choose; the person she presses is asked as import again, and until
     * the new answer is written the old one is still there. Read as the
     * answer, it sent her straight back to the question. */
    struct stat rst;
    if (stat(path, &rst) != 0) return dirty;
    if (rst.st_mtim.tv_sec < W.asked_at.tv_sec ||
        (rst.st_mtim.tv_sec == W.asked_at.tv_sec && rst.st_mtim.tv_nsec < W.asked_at.tv_nsec))
        return dirty;
    if (slurp(path, blob, sizeof blob) < 0) return dirty;

    char req[32], res[32], note[NOTE_MAX], users[256], files[16];
    field(blob, "request", req, sizeof req);
    field(blob, "result",  res, sizeof res);
    field(blob, "note",    note, sizeof note);
    field(blob, "users",   users, sizeof users);
    field(blob, "files",   files, sizeof files);
    /* A result file with no result line is one that was being written
     * while we read it. answer.sh writes it whole and renames, so this
     * should not happen -- and "should not happen" is why it is
     * checked rather than assumed. */
    if (!res[0]) return dirty;
    /* AND IT HAS TO BE AN ANSWER TO THE QUESTION THIS PANEL ASKED.
     *
     * The panel did not used to remember what it asked for, so an
     * answer to something else -- a result left over from a previous
     * session, or one a second request produced -- was acted on. A
     * confirm whose outcome nobody knows would show "Your files are
     * here. Open Files to see them." The file is left alone rather
     * than consumed: it is not ours to remove. */
    if (strcmp(req, W.wanted) != 0) return dirty;

    W.asked = 0;
    W.partial = 0;
    snprintf(W.note, sizeof W.note, "%s", note);
    W.files = atoi(files);

    if (!strcmp(req, "import") && !strcmp(res, "choose")) {
        /* Several Windows people and no way to tell whose: ask, by name. */
        people_from(users);
        if (W.n_people > 0) W.page = W_WHOSE;
        else { W.page = W_TROUBLE;
               snprintf(W.note, sizeof W.note, "AurOS could not read the names of the people who use Windows here."); }
    } else if (!strcmp(res, "empty") && (!strcmp(req, "import") || !strcmp(req, "stick"))) {
        W.page = W_EMPTY;
    } else if (!strcmp(res, "ok") || !strcmp(res, "partial")) {
        W.partial = !strcmp(res, "partial");
        if (!strcmp(req, "confirm"))      W.page = W_CONFIRMED;
        else if (!strcmp(req, "decline")) W.page = W_DECLINED;
        else if (!strcmp(req, "import"))  W.page = W_IMPORTED;
        else if (!strcmp(req, "stick"))   W.page = W_STICK_DONE;
        else                              W.page = W_TROUBLE;
    } else {
        /* refused, failed, or a word this panel does not know. All of
         * them are "that did not work", and the note says which -- a
         * result nothing recognised must not leave the page where it
         * was, because W_WORKING has no way out. */
        W.page = W_TROUBLE;
    }
    W.sel = 0;
    return 1;
}

/* ── where everything is ─────────────────────────────────────────── */

enum { A_YES, A_LATER, A_NO, A_SURE_YES, A_SURE_NO, A_NOT_YET,
       A_IMPORT, A_SKIP, A_START, A_FILES, A_NET, A_RESTART, A_RESTART_LATER,
       A_AGAIN, A_WIFI, A_BACKQ, A_LOOK, A_DONE, A_BLOCK_STICK, A_KEEP, A_N };
/* One button for each Windows person and each USB stick: their names are
 * data, so they are numbered from here (label_of / line_of). */
#define A_PERSON0 100
#define A_STICK0  110
/* Every button is a verb that names what happens (DESIGN-DIRECTION §4),
 * and the answers each carry one line saying where she lands. */
static const char *ACT_LABEL[A_N] = {
    "Yes, it all works", "Let me look first", "No, go back to Windows",
    "Start AurOS from now on", "Go back to Windows", "Not yet",
    "Bring my files across", "Not now", "Start using AurOS",
    "Open Your files", "Open Internet", "Restart now", "Restart later",
    "Try again", "Open Wi-Fi", "Back to the question",
    "Look again", "Done", "Save my files to a USB stick first", "Keep AurOS",
};
/* The one sentence for each moment (docs/handoff/streams/K.md, "One
 * truth about going back"), checked against what aurfirst does. */
#define SAY_YES "AurOS starts every time from now on. Windows and its files stay on this computer."
#define SAY_NO  "Windows starts the next time this computer starts. AurOS stays on this computer, with anything you saved in it. Nothing is deleted."
static const char *ACT_LINE[A_N] = {
    SAY_YES,
    "Nothing changes. AurOS asks again the next time it starts.",
    SAY_NO,
    NULL, NULL, "Back to the question. Nothing changes.",
    NULL, "You can do this later from Settings.", NULL,
    NULL, NULL, NULL, "AurOS stays open until you switch off.",
    NULL,                      /* A_AGAIN       */
    NULL, NULL,                /* A_WIFI, A_BACKQ */
    "AurOS also looks by itself, every few seconds.",          /* A_LOOK        */
    NULL,                                                       /* A_DONE        */
    "Your documents, pictures, music and videos are copied onto a stick you plug in. Nothing is removed from this computer.",
    "Nothing changes. Your files stay here.",                   /* A_KEEP        */
};

/* The buttons whose words are somebody's name. */
static char person_line_[WEL_PEOPLE][112];

static const char *label_of(int id)
{
    if (id >= A_PERSON0 && id < A_PERSON0 + WEL_PEOPLE) return W.person[id - A_PERSON0];
    if (id >= A_STICK0 && id < A_STICK0 + WEL_STICKS) return W.stick[id - A_STICK0].name;
    return (id >= 0 && id < A_N) ? ACT_LABEL[id] : "";
}
static const char *line_of(int id)
{
    if (id >= A_PERSON0 && id < A_PERSON0 + WEL_PEOPLE) {
        snprintf(person_line_[id - A_PERSON0], sizeof person_line_[0],
                 "Bring %s's documents, pictures, music and videos across.", W.person[id - A_PERSON0]);
        return person_line_[id - A_PERSON0];
    }
    if (id >= A_STICK0 && id < A_STICK0 + WEL_STICKS) return NULL;   /* the heading says it once */
    return (id >= 0 && id < A_N) ? ACT_LINE[id] : NULL;
}

#define WEL_MAX_ACT 6
typedef struct {
    int  gx, cw;
    int  head_y, sub_y, body_y;
    int  body_max_y;               /* last baseline the body may use   */
    int  check_y;                  /* top of the one check row, or 0   */
    int  rows;                     /* 1: each answer has its line beside it */
    int  line_px, line_max;        /* its line's height and most lines */
    int  no_sub;                   /* 1: no room for the sentence      */
    rect acts[WEL_MAX_ACT + 2]; int act[WEL_MAX_ACT + 2]; int n_acts;
} welcome_geom;

static int clampi_(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static welcome_view view_now(void)
{
    welcome_view v = { W.page, W.can_import };
    return v;
}

/* The answers on a page, in order. Primary first. */
static int actions_for(const welcome_view *v, int *which)
{
    int n = 0;
    switch (v->page) {
    case W_ASK:      which[n++] = A_YES; which[n++] = A_LATER; which[n++] = A_NO; break;
    case W_SURE_YES: which[n++] = A_SURE_YES; which[n++] = A_NOT_YET; break;
    case W_SURE_NO:  which[n++] = A_SURE_NO;  which[n++] = A_NOT_YET; break;
    case W_WORKING:  break;
    case W_CONFIRMED:
        if (v->can_import) { which[n++] = A_IMPORT; which[n++] = A_SKIP; }
        else which[n++] = A_START;
        break;
    case W_IMPORTING: break;
    case W_IMPORTED: which[n++] = A_FILES; which[n++] = A_START; break;
    case W_DECLINED: which[n++] = A_RESTART; which[n++] = A_RESTART_LATER; break;
    case W_TROUBLE:  which[n++] = A_AGAIN; which[n++] = W.ctx ? A_SKIP : A_LATER; break;
    case W_WHOSE:
        for (int i = 0; i < W.n_people; i++) which[n++] = A_PERSON0 + i;
        which[n++] = A_SKIP;
        break;
    case W_EMPTY:       which[n++] = W.ctx ? A_DONE : A_START; break;
    case W_STICK_PLUG:  which[n++] = A_LOOK; which[n++] = A_SKIP; break;
    case W_STICK_PICK:
        for (int i = 0; i < W.n_sticks; i++) which[n++] = A_STICK0 + i;
        which[n++] = A_SKIP;
        break;
    case W_STICK_SAVING: break;
    case W_STICK_DONE:  which[n++] = A_DONE; break;
    case W_BLOCKED:     which[n++] = A_BLOCK_STICK; which[n++] = A_KEEP; break;
    }
    return n;
}

static int body_of(int page, int partial, const char *lines[3]);
static const char *sub_of(int page, int can_import);

/* How many lines of 18 px a sentence needs in `w` pixels: an estimate
 * on purpose (geometry has no fonts), a little generous so that what is
 * reserved is never less than what is drawn. */
static int lines_for(const char *t, int w, float k)
{
    int cpl = (int)((float)w / (9.4f * k)); if (cpl < 8) cpl = 8;
    int n = ((int)strlen(t) + cpl - 1) / cpl;
    return n < 1 ? 1 : n;
}

/* ONE FUNCTION OWNS WHERE EVERYTHING IS: painting, pressing and
 * tools/targets.c all ask it.
 *
 * The question page is a list of choices: a button, and BESIDE it, in
 * 18 px ink, what pressing it does. (It was three narrow columns with
 * the sentences at 12 px and cut off: the one screen that decides for
 * good, and the sentences that say which button keeps AurOS were the
 * smallest, greyest text on it -- people-walkthrough F-07.) The check
 * gives way first when the screen is short, then the sentences, then
 * the one under the heading: never an answer. */
static void welcome_layout(const shell_ctx *c, int sw, int sh,
                           const welcome_view *v, welcome_geom *g)
{
    memset(g, 0, sizeof *g);
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;

    g->gx = clampi_(sw / 12, 24, 120);
    g->cw = sw - 2 * g->gx;
    if (g->cw > 900) g->cw = 900;
    if (g->cw < 120) { g->gx = 8; g->cw = sw - 16; }

    int act_h = (int)(34.f + 16.f * k); if (act_h < 48) act_h = 48;
    int gap   = (int)(16.f * k); if (gap < 12) gap = 12;
    int wide  = (int)(32.f * k);            /* before the way out    */
    g->line_px = (int)(24.f * k);

    g->head_y = (int)((float)sh * 0.12f + 40.f * k);
    g->sub_y  = g->head_y + (int)(44.f * k);
    int per_line = (int)((float)g->cw / (8.f * k)); if (per_line < 10) per_line = 10;
    const char *sub0 = sub_of(v->page, v->can_import);
    int sub_lines = sub0[0] ? 1 + (int)strlen(sub0) / per_line : 1;
    if (sub_lines > 3) sub_lines = 3;
    g->body_y = g->sub_y + (int)(8.f * k) + sub_lines * (int)(30.f * k);

    int which[A_N];
    int na = actions_for(v, which);
    g->n_acts = 0;

    const char *bl[3];
    int nb = body_of(v->page, W.partial, bl), nbody = 0;
    for (int i = 0; i < nb; i++) nbody += lines_for(bl[i], g->cw, k);
    if (v->page == W_TROUBLE && W.note[0]) nbody += lines_for(W.note, g->cw, k);
    int y = nbody ? g->body_y + nbody * (int)(30.f * k) : g->body_y - (int)(6.f * k);

    /* The one check, on the question page only, with its button. */
    int check_h = act_h + (int)(12.f * k);
    if (v->page == W_ASK) {
        y = g->body_y - (int)(14.f * k);
        g->check_y = y;
        y += check_h + (int)(24.f * k);
    }

    /* Each answer: its button, and what it does beside it. */
    int bw = 0, has_lines = 0;
    for (int i = 0; i < na; i++) {
        int w = (int)(48.f + 11.5f * k * (float)strlen(label_of(which[i])));
        if (w < 160) w = 160;
        if (w > bw) bw = w;
        if (line_of(which[i])) has_lines = 1;
    }
    if (bw > g->cw / 2 && has_lines) bw = g->cw / 2;
    int line_w = g->cw - bw - 24;
    int bottom = sh - (int)(16.f * k);

    for (int attempt = 0; attempt < 4; attempt++) {
        g->rows = has_lines && line_w >= 160;
        int total = 0;
        for (int i = 0; i < na; i++) {
            int rh = act_h;
            if (g->rows && line_of(which[i])) {
                int ln = lines_for(line_of(which[i]), line_w, k);
                if (ln > 4) ln = 4;
                int lh = ln * g->line_px + 6;
                if (lh > rh) rh = lh;
            }
            total += rh + (i ? ((v->page == W_ASK && which[i] == A_NO) ? wide : gap) : 0);
        }
        if (!g->rows) {            /* plain buttons, wrapping onto more rows if they must */
            int xx = 0, nrow = 1;
            for (int i = 0; i < na; i++) {
                int w = (int)(48.f + 11.5f * k * (float)strlen(label_of(which[i])));
                if (w < 160) w = 160;
                if (xx + w > g->cw && xx) { xx = 0; nrow++; }
                xx += w + gap;
            }
            total = nrow * act_h + (nrow - 1) * gap;
        }
        if (y + total <= bottom) break;
        if (attempt == 0 && g->check_y) {                 /* the check goes first */
            y = g->check_y; g->check_y = 0;
        } else if (attempt == 1) {                        /* then the sentences  */
            has_lines = 0;
        } else if (attempt == 2) {                        /* then the sentence under the heading */
            g->no_sub = 1;
            y = g->head_y + (int)(16.f * k);
        }
    }
    g->line_max = 4;
    /* Whatever is left over, the answers are on the screen: they move up
     * before they go off it. */
    {
        int total = 0;
        for (int i = 0; i < na; i++) {
            int rh = act_h;
            if (g->rows && line_of(which[i])) {
                int ln = lines_for(line_of(which[i]), line_w, k);
                if (ln > 4) ln = 4;
                if (ln * g->line_px + 6 > rh) rh = ln * g->line_px + 6;
            }
            total += rh + (i ? ((v->page == W_ASK && which[i] == A_NO && g->rows) ? wide : gap) : 0);
        }
        if (!g->rows) {
            int xx = 0, nrow = 1;
            for (int i = 0; i < na; i++) {
                int w = (int)(48.f + 11.5f * k * (float)strlen(label_of(which[i])));
                if (w < 160) w = 160;
                if (xx + w > g->cw && xx) { xx = 0; nrow++; }
                xx += w + gap;
            }
            total = nrow * act_h + (nrow - 1) * gap;
        }
        if (y + total > bottom) y = bottom - total;
        if (y < 0) y = 0;
    }
    g->body_max_y = (g->check_y ? g->check_y : y) - (int)(10.f * k);

    if (g->check_y) {
        int b = (int)(48.f + 11.5f * k * (float)strlen(ACT_LABEL[A_NET]));
        g->acts[g->n_acts] = (rect){ g->gx + g->cw - b, g->check_y, b, act_h };
        g->act[g->n_acts++] = A_NET;                    /* A_WIFI when offline; same place */
    }
    int x = g->gx, yy = y;
    for (int i = 0; i < na && g->n_acts < WEL_MAX_ACT + 2; i++) {
        rect r;
        int rh = act_h;
        if (g->rows) {
            if (line_of(which[i])) {
                int ln = lines_for(line_of(which[i]), line_w, k);
                if (ln > 4) ln = 4;
                int lh = ln * g->line_px + 6;
                if (lh > rh) rh = lh;
            }
            if (i) yy += (v->page == W_ASK && which[i] == A_NO) ? wide : gap;
            r = (rect){ g->gx, yy, bw, act_h };
            yy += rh;
        } else {
            int w = (int)(48.f + 11.5f * k * (float)strlen(label_of(which[i])));
            if (w < 160) w = 160;
            if (x + w > g->gx + g->cw) { x = g->gx; yy += act_h + gap; }
            r = (rect){ x, yy, w, act_h };
            x += w + gap;
        }
        g->acts[g->n_acts] = r;
        g->act[g->n_acts++] = which[i];
    }
}

/* While she is checking something, the question steps aside and this
 * chip stays: the way back is always on screen (E-03). */
static rect back_chip(const shell_ctx *c, int sw)
{
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
    int w = (int)(340.f * k), h = (int)(52.f * k);
    if (w > sw - 24) w = sw - 24;
    return (rect){ (sw - w) / 2, 6, w, h };
}

int welcome_targets(const shell_ctx *c, int sw, int sh,
                    const welcome_view *v, rect *out, int max)
{
    welcome_geom g;
    welcome_layout(c, sw, sh, v, &g);
    int n = 0;
    for (int i = 0; i < g.n_acts && n < max; i++) out[n++] = g.acts[i];
    return n;
}

void welcome_set_page(int page)
{ if (page >= 0 && page < W_PAGE_N) { W.page = page; W.sel = 0; W.keyed = 0; } }
int  welcome_page(void) { return W.page; }

/* ── what it says ────────────────────────────────────────────────── */

static const char *head_of(int page)
{
    switch (page) {
    case W_ASK:       return "Does everything work?";
    case W_SURE_YES:  return "Start AurOS from now on?";
    case W_SURE_NO:   return "Go back to Windows?";
    case W_WORKING:   return "Saving your answer.";
    case W_CONFIRMED: return "AurOS starts from now on";
    case W_IMPORTING: return "Bringing your files across";
    case W_IMPORTED:  return "Your files are here";
    case W_DECLINED:  return "Windows starts next time";
    case W_WHOSE:     return "Whose files?";
    case W_EMPTY:     return W.ctx ? "No files to save" : "No files to bring across";
    case W_STICK_PLUG:   return "Plug in a USB stick";
    case W_STICK_PICK:   return "Which USB stick?";
    case W_STICK_SAVING: return "Saving your files";
    case W_STICK_DONE:   return "Your files are on the USB stick";
    case W_BLOCKED:      return "Windows was not put back";
    default:          return "That did not work";
    }
}

static const char *sub_of(int page, int can_import)
{
    switch (page) {
    case W_ASK:       return "Check one thing first.";
    case W_SURE_YES:  return SAY_YES;
    case W_SURE_NO:   return SAY_NO;
    case W_WORKING:   return "";
    case W_CONFIRMED: return can_import
                          ? "Your documents and pictures are still in Windows. "
                            "AurOS can copy them here."
                          : "Windows and its files stay on this computer.";
    case W_IMPORTING: return "Your files are copied. Nothing in Windows changes.";
    case W_IMPORTED:  return "Open Your files to see them.";
    case W_DECLINED:  return "Press Restart now to go back to Windows.";
    case W_WHOSE:     return "More than one person uses Windows on this computer.";
    case W_EMPTY:     return W.ctx
                          ? "There were no documents, pictures, music or videos of yours in AurOS to save."
                          : "AurOS looked in Windows and found no documents, pictures, music or videos.";
    case W_STICK_PLUG:   return "AurOS will save your documents, pictures, music and videos on it.";
    case W_STICK_PICK:   return "Your files are saved on the one you press.";
    case W_STICK_SAVING: return "Leave the USB stick in and the computer on.";
    case W_STICK_DONE:   return "They are in the folder \"From AurOS\". It is safe to take the stick out now.";
    case W_BLOCKED:
        /* Why, in her words. AurOS saves her files into Windows before it
         * puts Windows back; these are the ways that cannot be done. */
        if (!strcmp(W.why, "toomany")) return "Your files are too many to carry across to Windows this way.";
        if (!strcmp(W.why, "locked"))  return "Windows' drive is locked, so AurOS cannot save your files in it first.";
        if (!strcmp(W.why, "noroom"))  return "There is not enough room in Windows to save your files first.";
        if (!strcmp(W.why, "dirty"))   return "Windows was not shut down properly, so AurOS cannot save your files in it first.";
        if (!strcmp(W.why, "nodrive")) return "AurOS could not find the Windows drive to save your files in.";
        if (!strcmp(W.why, "some"))    return "Some of your files could not be saved in Windows first.";
        return "AurOS could not save your files in Windows first.";
    default:
        /* IT USED TO SAY "Nothing has been changed." It cannot know
         * that: confirm can write BootOrder and then fail its own
         * read-back. The note under this line is what actually
         * happened, and it comes from the thing that did it. */
        return "AurOS could not finish that.";
    }
}

static int body_of(int page, int partial, const char *lines[3])
{
    int n = 0;
    static char count[96];
    switch (page) {
    case W_IMPORTING:
        /* What Ferry says it is doing: "Copying your Pictures: 1,200 of
         * 5,000 files. About 4 minutes left." (never a file's name). */
        if (W.progress[0]) lines[n++] = W.progress;
        lines[n++] = "Leave the computer on until this finishes.";
        break;
    case W_WHOSE:
        lines[n++] = "Press the name of the person whose files you want here. Nothing in Windows changes.";
        break;
    case W_EMPTY:
        lines[n++] = "Nothing is wrong, and nothing in Windows was changed.";
        break;
    case W_STICK_PLUG:
        lines[n++] = "It needs room for them. Anything already on the stick stays there.";
        break;
    case W_STICK_PICK:
        lines[n++] = "Nothing on the stick is deleted.";
        break;
    case W_STICK_SAVING:
        if (W.progress[0]) lines[n++] = W.progress;
        lines[n++] = "Your files stay here too. Nothing is removed from this computer.";
        break;
    case W_STICK_DONE:
        if (W.files == 1) snprintf(count, sizeof count, "1 file is saved.");
        else if (W.files >= 1000) snprintf(count, sizeof count, "%d,%03d files are saved.", W.files / 1000, W.files % 1000);
        else if (W.files > 1) snprintf(count, sizeof count, "%d files are saved.", W.files);
        else count[0] = 0;
        if (count[0]) lines[n++] = count;
        if (partial) lines[n++] = "Some files could not be saved. The note on the stick, \"Read me\", says which.";
        if (W.from_blocked)
            lines[n++] = "Windows is still not put back. AurOS checks again, before it removes anything, whenever you press it in Settings.";
        break;
    case W_BLOCKED:
        lines[n++] = "Nothing was removed, and nothing of yours was deleted.";
        lines[n++] = "AurOS puts Windows back only once your files have been saved. That is on purpose.";
        break;
    case W_IMPORTED:
        if (partial) lines[n++] = "Some things could not be brought across.";
        break;
    case W_DECLINED:
        lines[n++] = "AurOS is still on this computer, but Windows starts. "
                     "To use AurOS again, or to remove it, ask someone who "
                     "knows computers.";
        break;
    default: break;
    }
    return n;
}

static void paint_action(surface *s, shell_fonts *f, const shell_ctx *c,
                         rect r, const char *label, int hot, int primary)
{
    /* What can be pressed looks pressable: the go fill, or a 2 px rule.
     * Hover only darkens the rule (DESIGN-DIRECTION §3). */
    corners rr = corners_all(8.f);
    if (primary) draw_round_rect(s, r, rr, c->accent, 1.f);
    else {
        draw_round_rect(s, r, rr, c->surface_c, 1.f);
        draw_round_rect_border(s, r, rr, 2.f, hot ? c->fg : c->overlay, 1.f);
    }
    if (primary && hot)
        draw_round_rect_border(s, (rect){ r.x - 3, r.y - 3, r.w + 6, r.h + 6 },
                               corners_all(11.f), 2.f, c->fg, 1.f);
    font *ft = f->mid ? f->mid : f->small;
    if (ft)
        shell_text_centred(s, ft, (float)r.x + r.w / 2.f,
                           shell_baseline(ft, (float)r.y, (float)r.h),
                           label, primary ? c->bg : c->fg, 1.f);
}

void welcome_paint(shell_ctx *c, surface *s, shell_fonts *f)
{
    /* Checking something: only the way back is drawn. */
    if (!c->welcome_open) {
        if (W.checking) {
            rect b = back_chip(c, s->w);
            corners rr = corners_all(8.f);
            draw_round_rect(s, b, rr, c->surface_c, 1.f);
            draw_round_rect_border(s, b, rr, 2.f, c->fg, 1.f);
            font *ft = f->mid ? f->mid : f->small;
            if (ft) {
                float tw = shell_text_w(ft, ACT_LABEL[A_BACKQ]);
                float x0 = (float)b.x + ((float)b.w - tw - 36.f) * 0.5f;
                shell_icon_draw(s, ICON_BACK, x0 + 11.f, (float)b.y + (float)b.h * 0.5f,
                                24.f, c->fg_hi, c->surface_c, 1.f);
                shell_text(s, ft, x0 + 36.f, shell_baseline(ft, (float)b.y, (float)b.h),
                           ACT_LABEL[A_BACKQ], c->fg_hi, 1.f);
            }
        }
        return;
    }
    int sw = s->w, sh = c->screen_h;
    if (sh <= 0 || sh > s->h) sh = s->h;

    welcome_view v = view_now();
    welcome_geom g;
    welcome_layout(c, sw, sh, &v, &g);
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;

    /* Paper, opaque: a question, not a veil over the desktop. */
    draw_rect(s, (rect){ 0, 0, sw, sh }, c->bg, 1.f);

    font *head  = f->huge ? f->huge : (f->big ? f->big : f->mid);
    font *bodyf = f->small ? f->small : f->mid;
    font *name  = f->mid ? f->mid : f->small;
    font *lf    = f->tiny ? f->tiny : bodyf;

    if (head)
        shell_text(s, head, (float)g.gx, (float)g.head_y, head_of(W.page), c->fg, 1.f);
    const char *sub = sub_of(W.page, W.can_import);
    float lh = 30.f * k;
    if (bodyf && sub[0] && !g.no_sub)
        shell_text_wrap(s, bodyf, (float)g.gx, (float)g.sub_y, (float)g.cw, lh,
                        sub, c->subtle, 1.f, 3);

    const char *lines[3];
    int nl = body_of(W.page, W.partial, lines);
    int y = g.body_y;
    for (int i = 0; i < nl && bodyf && y <= g.body_max_y; i++)
        y += (int)lh * shell_text_wrap(s, bodyf, (float)g.gx, (float)y, (float)g.cw,
                                       lh, lines[i], c->fg, 1.f, 3);
    if (W.page == W_TROUBLE && W.note[0] && bodyf && y <= g.body_max_y)
        shell_text_wrap(s, bodyf, (float)g.gx, (float)y, (float)g.cw, lh,
                        W.note, c->fg, 1.f, 3);

    /* The check: a question on the left, its button on the right, a rule
     * under it. Offline it says so, and the button opens Wi-Fi. */
    if (g.check_y && name) {
        int offline = offline_now_();
        const char *q = offline ? "Not connected to the internet yet."
                                : "Does the internet work?";
        int bh = g.acts[0].h;
        shell_text(s, name, (float)g.gx,
                   shell_baseline(name, (float)g.check_y, (float)bh), q, c->fg, 1.f);
        draw_hrule(s, g.gx, g.check_y + bh + (int)(8.f * k), g.cw, 1, c->overlay, 1.f);
    }

    int offline = g.check_y ? offline_now_() : 0;
    for (int i = 0; i < g.n_acts; i++) {
        int id = g.act[i];
        int primary = id == A_YES || id == A_SURE_YES || id == A_SURE_NO ||
                      id == A_IMPORT || id == A_START || id == A_RESTART ||
                      id == A_AGAIN || (id == A_FILES && W.page == W_IMPORTED) ||
                      id == A_PERSON0 || id == A_STICK0 || id == A_LOOK ||
                      id == A_DONE || id == A_BLOCK_STICK;
        const char *label = (id == A_NET && offline) ? ACT_LABEL[A_WIFI] : label_of(id);
        paint_action(s, f, c, g.acts[i], label,
                     W.hover_act == id || (W.keyed && W.sel == i), primary);
        /* What pressing it does, beside it, in 18 px ink-2 and never cut. */
        if (g.rows && line_of(id) && lf) {
            rect r = g.acts[i];
            float lx = (float)(r.x + r.w) + 24.f;
            shell_text_wrap(s, lf, lx, (float)r.y + font_ascent(lf) + 4.f,
                            (float)(g.gx + g.cw) - lx, (float)g.line_px,
                            line_of(id), c->subtle, 1.f, g.line_max);
        }
    }
}

/* ── pressing it ─────────────────────────────────────────────────── */

static int hit(rect r, int x, int y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

/* Her files, or the internet, opened so she can check -- the question
 * steps aside, a chip says how to come back, and it returns by itself
 * when she has closed what she opened. */
static void check_with(shell_ctx *c, shell_icon icon)
{
    for (int i = 0; i < c->n_apps; i++)
        if (c->apps[i].icon == icon && c->apps[i].role == APP_ROLE_PROGRAM) {
            W.checking = 1;
            W.check_wins = c->n_wins;
            W.check_panel = 0;
            c->want_launch = i + 1;
            c->welcome_open = 0;
            return;
        }
}

static int do_action(shell_ctx *c, int id)
{
    const char *trouble = "AurOS could not reach the part of itself that changes this.";
    switch (id) {
    case A_YES: W.page = W_SURE_YES; W.sel = 0; return 1;
    case A_NO:  W.page = W_SURE_NO;  W.sel = 0; return 1;
    case A_NOT_YET: W.page = W_ASK;  W.sel = 0; return 1;
    case A_SURE_YES:
    case A_SURE_NO:
        if (ask_for(id == A_SURE_YES ? "confirm" : "decline") == 0) {
            W.page = W_WORKING; W.sel = 0;
        } else {
            W.page = W_TROUBLE; snprintf(W.note, sizeof W.note, "%s", trouble);
        }
        return 1;
    case A_IMPORT:
        if (ask_for("import") == 0) { W.page = W_IMPORTING; W.sel = 0; }
        else { W.page = W_TROUBLE;
               snprintf(W.note, sizeof W.note,
                        "AurOS could not start bringing your files across."); }
        return 1;
    case A_AGAIN:
        if (W.ctx) { open_stick_page(c); return 1; }      /* the stick flow starts over */
        W.page = W_ASK; W.sel = 0; W.note[0] = 0;
        return 1;
    case A_LOOK:
        sticks_look();
        W.page = W.n_sticks ? W_STICK_PICK : W_STICK_PLUG; W.sel = 0; W.keyed = 0;
        return 1;
    case A_BLOCK_STICK:
        W.from_blocked = 1;
        open_stick_page(c);
        return 1;
    case A_KEEP: case A_DONE:
        c->welcome_open = 0;
        return 1;
    case A_FILES:
        check_with(c, ICON_FILES);
        if (W.page == W_IMPORTED) W.checking = 0;   /* done: not coming back */
        return 1;
    case A_NET:
        /* Offline, "Open Internet" would show a browser error page and
         * nowhere to go: open the Wi-Fi page instead (E-02). */
        if (offline_now_()) {
            shell_open_net(c);
            W.checking = 1; W.check_panel = 1; W.check_wins = c->n_wins;
            return 1;
        }
        check_with(c, ICON_GLOBE);
        return 1;
    case A_RESTART:
        c->want_power_off = 2;          /* the band's own restart */
        c->welcome_open = 0;
        return 1;
    case A_LATER: case A_SKIP: case A_START: case A_RESTART_LATER:
        c->welcome_open = 0;
        return 1;
    }
    if (id >= A_PERSON0 && id < A_PERSON0 + WEL_PEOPLE && id - A_PERSON0 < W.n_people) {
        /* "Whose files?": import, and the files are theirs. The word carries
         * the name; the answer comes back as request=import. */
        char word[96];
        snprintf(word, sizeof word, "import:%s", W.person[id - A_PERSON0]);
        if (ask_word(word, "import") == 0) { W.page = W_IMPORTING; W.sel = 0; }
        else { W.page = W_TROUBLE;
               snprintf(W.note, sizeof W.note, "AurOS could not start bringing your files across."); }
        return 1;
    }
    if (id >= A_STICK0 && id < A_STICK0 + WEL_STICKS && id - A_STICK0 < W.n_sticks) {
        char word[48];
        snprintf(word, sizeof word, "stick:%s", W.stick[id - A_STICK0].dev);
        if (ask_word(word, "stick") == 0) { W.page = W_STICK_SAVING; W.sel = 0; }
        else { W.page = W_TROUBLE;
               snprintf(W.note, sizeof W.note, "AurOS could not start saving your files on the USB stick."); }
        return 1;
    }
    return 0;
}

int welcome_click(shell_ctx *c, int x, int y)
{
    if (!c->welcome_open && W.checking) {
        rect b = back_chip(c, c->screen_w);
        if (hit(b, x, y)) {
            W.checking = 0;
            c->welcome_open = 1;
            W.page = W_ASK;
            return 1;
        }
        return 0;
    }
    if (!c->welcome_open) return 0;
    int sw = c->screen_w, sh = c->screen_h;
    welcome_view v = view_now();
    welcome_geom g;
    welcome_layout(c, sw, sh, &v, &g);
    for (int i = 0; i < g.n_acts; i++)
        if (hit(g.acts[i], x, y)) return do_action(c, g.act[i]);
    /* A PRESS ANYWHERE ELSE DOES NOTHING, and is still consumed. This
     * is a question, not a notification: pressing the background to
     * make it go away is exactly the reflex that would answer it by
     * accident. */
    return 1;
}

void welcome_motion(shell_ctx *c, int x, int y)
{
    if (!c->welcome_open) return;
    welcome_view v = view_now();
    welcome_geom g;
    welcome_layout(c, c->screen_w, c->screen_h, &v, &g);
    W.hover_act = -1;
    for (int i = 0; i < g.n_acts; i++)
        if (hit(g.acts[i], x, y)) { W.hover_act = g.act[i]; break; }
}

int welcome_key(shell_ctx *c, int k)
{
    if (!c->welcome_open) return 0;
    welcome_view v = view_now();
    welcome_geom g;
    welcome_layout(c, c->screen_w, c->screen_h, &v, &g);
    if (!g.n_acts) return 1;            /* nothing to press yet */

    /* evdev: 105 left, 106 right, 103 up, 108 down, 28 enter, 57 space,
     * 1 escape. Arrows in both axes because the actions are a row on a
     * wide screen and a column on a narrow one, and she does not know
     * which. */
    /* Until she moves it, the selection is the first ANSWER, not the
     * first button (on the question page those are the two checks). */
    if (!W.keyed) W.sel = g.check_y ? 1 : 0;
    switch (k) {
    case 105: case 103:
        W.keyed = 1;
        W.sel = (W.sel + g.n_acts - 1) % g.n_acts; return 1;
    case 106: case 108:
        W.keyed = 1;
        W.sel = (W.sel + 1) % g.n_acts; return 1;
    case 28: case 57:
        do_action(c, g.act[clampi_(W.sel, 0, g.n_acts - 1)]); return 1;
    case 1:
        /* ESCAPE IS "LET ME LOOK FIRST", NEVER AN ANSWER -- and on a
         * confirming page it is "Not yet". */
        if (W.page == W_SURE_YES || W.page == W_SURE_NO) { W.page = W_ASK; W.sel = 0; }
        else c->welcome_open = 0;
        return 1;
    }
    return 1;                           /* the panel eats the rest */
}
