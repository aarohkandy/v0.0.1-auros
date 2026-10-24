/* wish.c — see wish.h. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include "wish.h"
#include "wishover.h"
#include "wishgallery.h"
#include "aurfeatures.h"
#include "foot.h"
#include "draw.h"
#include "run.h"
#include "settings.h"

#define TEXT_MAX   300         /* the service takes 300 characters      */
#define SENT_MAX   480
#define TITLE_MAX  64
#define ROWS_MAX   50          /* a list of 50 wishes is long: so is the line */
#define WLINE_OUT  4096        /* docs/AI.md §3.7: shell -> engine        */
#define WLINE_MAX  65536       /* engine -> shell: a longer line closes   */
#define WAIT_GIVE_UP_MS  20000 /* §7.9: waiting gives up after 20 s     */
#define SAME_TEXT_MS     30000 /* §7.9: the same text within 30 s       */
#define UNDO_MS   (10 * 60 * 1000)
#define LISTEN_MAX_MS    30000 /* §7.9: the microphone, at most 30 s    */

/* ── the clock (a pointer so the test can move it) ───────────────── */
static uint64_t real_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
static uint64_t (*now_ms)(void) = real_now_ms;

/* ── the examples, and the device's own lines ──────────────────────
 *
 * Six on the first page (§7.9). The first three are the owner's; the
 * rest are chosen to show the range -- a now, a startup, a time -- and
 * every one of them is a wish the engine accepts as written. */
static const char *const EXAMPLES[] = {
    "Make the words bigger",
    "Remind me to take my pills at 9",
    "Fill my screen with potatoes when I type potato",
    "Make the screen brighter",
    "Play birdsong when I switch on",
    "Say good morning to me at 8",
};
#define N_EXAMPLES ((int)(sizeof EXAMPLES / sizeof EXAMPLES[0]))

/* What each `error` means to her, when the engine sent no sentence of
 * its own. One plain line and what to do next (docs/EASY.md rule 8). */
static const struct { const char *code, *head, *line; int retry; } ERRS[] = {
    { "offline", "Wishes needs the internet",
      "Connect to the internet, then try again. Wishes you already made still work.", 1 },
    { "busy",    "Wishes are resting for a little while",
      "Try again in a few minutes.", 1 },
    { "resting", "Wishes are resting for a little while",
      "Try again in a few minutes.", 1 },
    { "limit",   "You've made lots of wishes today",
      "More tomorrow. The wishes you already made keep working.", 0 },
    { "refused", "I can't do that",
      "That is not something this computer can do safely.", 0 },
    { "slow",    "That took too long",
      "Nothing has changed. Please try again.", 1 },
    { "broken",  "Wishes isn't working right now",
      "The rest of your computer is fine.", 1 },
    { "unsure",  "I didn't quite catch that",
      "Please say it again, or type it.", 1 },
    { "nomic",   "I can't hear anything",
      "This computer has no microphone I can use. You can type your wish.", 0 },
};
#define N_ERRS ((int)(sizeof ERRS / sizeof ERRS[0]))

static const char *const CONSENT[] = {
    "When you make a wish, its words are sent over the internet to work "
    "out what you mean.",
    "Nothing else leaves this computer: not your files, not what you type "
    "anywhere else, not where you are.",
    "Your words go to AurOS's wish service on Cloudflare, and on to "
    "Anthropic, who make Claude. Both are in the United States.",
    "Anthropic may keep them for up to 30 days, or up to 2 years if its "
    "safety checks flag them.",
    "Your computer always tells you what a wish will do, and does nothing "
    "until you press Yes. You can turn Wishes off in Settings.",
};
#define N_CONSENT ((int)(sizeof CONSENT / sizeof CONSENT[0]))

/* ── state ───────────────────────────────────────────────────────── */

enum { OP_NONE, OP_HELLO, OP_CONSENT, OP_WISH, OP_ASK, OP_CHECK, OP_CONFIRM,
       OP_DISCARD, OP_LIST, OP_ENABLE, OP_REMOVE, OP_TEST, OP_ALLOFF,
       OP_ALLON, OP_UNDO, OP_RESTORE, OP_KEEP, OP_SNOOZE, OP_TODAY,
       OP_FORGET };

typedef struct {
    char wid[40];
    char title[TITLE_MAX];
    char sentence[SENT_MAX];
    int  on;
    int  paused;                /* status "paused:<why>"                 */
} wish_row;

static struct {
    int  page;
    int  open_kind;             /* what c->wish_open was when opened     */
    char text[TEXT_MAX + 1];    /* the box                               */
    char said[TEXT_MAX + 1];    /* what went off to the engine           */
    int  voice;                 /* this one came from her voice          */
    char sentence[SENT_MAX];    /* the engine's own sentence             */
    char title[TITLE_MAX];
    char pending[48];
    char dest[96];              /* an outward action's destination      */
    char dest_host[96];         /* and the address it is on, if told    */
    int  retry_after;           /* seconds, from an error, or 0         */
    char help_page[48];
    char note[SENT_MAX];        /* cannot / error: the engine's line    */
    int  err;                   /* index into ERRS                       */
    char hint[96];              /* one gentle line under the box        */

    int  req_id, req_op;        /* the ONE request she is waiting on    */
    uint64_t req_t0;
    char last_text[TEXT_MAX + 1];
    uint64_t last_ms;

    int  consent;               /* -1 unknown, 0 no, 1 yes (from hello)  */
    int  all_off;
    int  shown_once;            /* the first open shows all six examples */

    struct { char title[TITLE_MAX]; int times; char tstr[24]; } today[ROWS_MAX];
    int  n_today;
    wish_row rows[ROWS_MAX];
    int  n_rows, first_row, removing;
    int  undo_engine;           /* the engine says Undo is available    */
    uint64_t undo_until;

    int  hover;                 /* index into the current targets, -1    */
    int  sel;                   /* keyboard: a target index, -1 = none  */
    int  box_focus;             /* typing goes into the box             */

    /* voice */
    pid_t vpid;
    int   vfd;
    char  vbuf[TEXT_MAX + 64];
    size_t vn;
    uint64_t v_t0;

    /* The gallery: which template, which blank she is on (-1 = the list
     * of templates), what she chose so far, and how far down the list. */
    int  g_tmpl, g_stage, g_first;
    int  g_choice[WG_MAX_SLOTS];

    char sent_last[WLINE_OUT];
    char recv_last[512];
} W = { .page = WP_ASK, .consent = -1, .removing = -1, .hover = -1,
        .sel = -1, .box_focus = 1, .vpid = 0, .vfd = -1, .g_stage = -1 };

/* ── a very small JSON reader ──────────────────────────────────────
 *
 * Only what the socket carries: objects, arrays, strings, numbers,
 * true/false/null. Every pointer is checked against the end of the
 * line; a malformed line yields "absent", never a read past it. The
 * engine is ours and runs as her, but the strings in it were, at one
 * remove, written by a model -- so they are decoded, not trusted to be
 * well formed. */

static const char *js_ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static const char *js_skip(const char *p, const char *e, int depth)
{
    p = js_ws(p, e);
    if (p >= e || depth > 12) return NULL;
    if (*p == '"') {
        for (p++; p < e && *p != '"'; p++)
            if (*p == '\\' && ++p >= e) return NULL;
        return p < e ? p + 1 : NULL;
    }
    if (*p == '{' || *p == '[') {
        int obj = (*p == '{');
        char close = obj ? '}' : ']';
        p = js_ws(p + 1, e);
        if (p < e && *p == close) return p + 1;
        for (;;) {
            if (obj) {
                if (p >= e || *p != '"') return NULL;
                p = js_skip(p, e, depth + 1);
                if (!p) return NULL;
                p = js_ws(p, e);
                if (p >= e || *p != ':') return NULL;
                p++;
            }
            p = js_skip(p, e, depth + 1);
            if (!p) return NULL;
            p = js_ws(p, e);
            if (p >= e) return NULL;
            if (*p == ',') { p = js_ws(p + 1, e); continue; }
            if (*p == close) return p + 1;
            return NULL;
        }
    }
    const char *s = p;
    while (p < e && (isalnum((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.'))
        p++;
    return p > s ? p : NULL;
}

/* The value of `key` in the object at p, or NULL. *ve is its end. */
static const char *js_get(const char *p, const char *e, const char *key,
                          const char **ve)
{
    p = js_ws(p, e);
    if (p >= e || *p != '{') return NULL;
    p = js_ws(p + 1, e);
    size_t kl = strlen(key);
    while (p < e && *p == '"') {
        const char *ks = p + 1, *ke = js_skip(p, e, 1);
        if (!ke) return NULL;
        int match = (size_t)(ke - 1 - ks) == kl && !memcmp(ks, key, kl);
        p = js_ws(ke, e);
        if (p >= e || *p != ':') return NULL;
        p = js_ws(p + 1, e);
        const char *v = p, *vend = js_skip(p, e, 1);
        if (!vend) return NULL;
        if (match) { if (ve) *ve = vend; return v; }
        p = js_ws(vend, e);
        if (p < e && *p == ',') { p = js_ws(p + 1, e); continue; }
        return NULL;
    }
    return NULL;
}

static void put_utf8(char *out, size_t n, size_t *o, unsigned cp)
{
    char b[4]; int k;
    if (cp < 0x80)       { b[0] = (char)cp; k = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 63)); k = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 63));
                             b[2] = (char)(0x80 | (cp & 63)); k = 3; }
    else { b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 63));
           b[2] = (char)(0x80 | ((cp >> 6) & 63)); b[3] = (char)(0x80 | (cp & 63)); k = 4; }
    if (*o + (size_t)k >= n) return;
    memcpy(out + *o, b, (size_t)k);
    *o += (size_t)k;
}

static int hexv(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* Decode the string at p into out. Control characters become spaces:
 * nothing she reads may move the cursor or ring a bell. -1 if p is not
 * a string. */
static int js_str(const char *p, const char *e, char *out, size_t n)
{
    if (!n) return -1;
    out[0] = 0;
    if (!p || p >= e || *p != '"') return -1;
    size_t o = 0;
    for (p++; p < e && *p != '"'; p++) {
        unsigned ch = (unsigned char)*p;
        if (ch == '\\') {
            if (++p >= e) break;
            switch (*p) {
            case 'n': case 'r': case 't': case 'b': case 'f': ch = ' '; break;
            case 'u': {
                if (e - p < 5) return -1;
                int v = 0;
                for (int i = 1; i <= 4; i++) {
                    int h = hexv(p[i]); if (h < 0) return -1;
                    v = v * 16 + h;
                }
                p += 4;
                unsigned cp = (unsigned)v;
                if (cp >= 0xD800 && cp < 0xDC00 && e - p > 6 && p[1] == '\\' && p[2] == 'u') {
                    int lo = 0, okp = 1;
                    for (int i = 3; i <= 6; i++) {
                        int h = hexv(p[i]); if (h < 0) { okp = 0; break; }
                        lo = lo * 16 + h;
                    }
                    if (okp && lo >= 0xDC00 && lo < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned)lo - 0xDC00);
                        p += 6;
                    }
                }
                if (cp < 0x20 || (cp >= 0xD800 && cp < 0xE000)) cp = ' ';
                put_utf8(out, n, &o, cp);
                continue;
            }
            default: ch = (unsigned char)*p; break;
            }
        }
        if (ch < 0x20) ch = ' ';
        if (o + 1 < n) out[o++] = (char)ch;
    }
    /* Never leave half a character at the end of a truncated string. */
    while (o > 0 && ((unsigned char)out[o - 1] & 0xC0) == 0x80) {
        size_t s = o - 1;
        while (s > 0 && ((unsigned char)out[s] & 0xC0) == 0x80) s--;
        unsigned lead = (unsigned char)out[s];
        size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (o - s >= want) break;
        o = s;
    }
    out[o] = 0;
    return 0;
}

static int js_getstr(const char *obj, const char *e, const char *key,
                     char *out, size_t n)
{
    const char *ve, *v = js_get(obj, e, key, &ve);
    if (!v) { if (n) out[0] = 0; return -1; }
    return js_str(v, ve, out, n);
}

static int js_bool(const char *obj, const char *e, const char *key, int dflt)
{
    const char *ve, *v = js_get(obj, e, key, &ve);
    if (!v) return dflt;
    if (ve - v == 4 && !memcmp(v, "true", 4)) return 1;
    if (ve - v == 5 && !memcmp(v, "false", 5)) return 0;
    return dflt;
}

static long js_int(const char *obj, const char *e, const char *key, long dflt)
{
    const char *ve, *v = js_get(obj, e, key, &ve);
    if (!v || !(isdigit((unsigned char)*v) || *v == '-')) return dflt;
    char b[24]; size_t l = (size_t)(ve - v);
    if (l >= sizeof b) return dflt;
    memcpy(b, v, l); b[l] = 0;
    return strtol(b, NULL, 10);
}

/* Array iteration: the first element of the array at p, then the next. */
static const char *js_first(const char *p, const char *e)
{
    p = js_ws(p, e);
    if (p >= e || *p != '[') return NULL;
    p = js_ws(p + 1, e);
    return (p < e && *p != ']') ? p : NULL;
}
static const char *js_next(const char *p, const char *e)
{
    p = js_skip(p, e, 1);
    if (!p) return NULL;
    p = js_ws(p, e);
    if (p >= e || *p != ',') return NULL;
    return js_ws(p + 1, e);
}

/* Her words, quoted for a JSON line. */
static void js_quote(char *out, size_t n, const char *s)
{
    size_t o = 0;
    if (n < 3) { if (n) out[0] = 0; return; }
    out[o++] = '"';
    for (; *s && o + 8 < n; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = (char)ch; }
        else if (ch < 0x20) o += (size_t)snprintf(out + o, n - o, "\\u%04x", ch);
        else out[o++] = (char)ch;
    }
    out[o++] = '"';
    out[o] = 0;
}

/* ── the socket ──────────────────────────────────────────────────── */

static struct {
    int fd;
    char in[WLINE_MAX * 2];
    size_t in_n;
    char out[WLINE_OUT * 4];
    size_t out_n;
    uint64_t next_try;
    int next_id;
    struct { int id, op; } pend[24];
} K = { .fd = -1, .next_id = 1 };

static void handle_line(shell_ctx *c, const char *line, size_t len);

/* What the engine was last told about her (see send_presence). */
static int      input_pending;
static uint64_t input_sent_ms;
static char     hold_sent[160];

static int sock_path(char *out, size_t n)
{
    const char *rd = getenv("XDG_RUNTIME_DIR");
    if (!rd || !*rd) return -1;
    int k = snprintf(out, n, "%s/%s", rd, WISH_SOCK_NAME);
    return (k > 0 && (size_t)k < n) ? 0 : -1;
}

static void k_flush(void)
{
    while (K.fd >= 0 && K.out_n > 0) {
        ssize_t k = send(K.fd, K.out, K.out_n, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (k <= 0) return;           /* the next pass tries again */
        memmove(K.out, K.out + k, K.out_n - (size_t)k);
        K.out_n -= (size_t)k;
    }
}

/* Remember which op an id was, so its reply can be understood. */
static void k_note(int id, int op)
{
    for (int i = 0; i < (int)(sizeof K.pend / sizeof K.pend[0]); i++)
        if (!K.pend[i].id) { K.pend[i].id = id; K.pend[i].op = op; return; }
    /* Full: the oldest goes. A reply to it is then just ignored. */
    memmove(&K.pend[0], &K.pend[1], sizeof K.pend - sizeof K.pend[0]);
    K.pend[(sizeof K.pend / sizeof K.pend[0]) - 1].id = id;
    K.pend[(sizeof K.pend / sizeof K.pend[0]) - 1].op = op;
}
static int k_take(int id)
{
    for (int i = 0; i < (int)(sizeof K.pend / sizeof K.pend[0]); i++)
        if (K.pend[i].id == id && id) { K.pend[i].id = 0; return K.pend[i].op; }
    return OP_NONE;
}

/* Write one line. `fmt` is the JSON after `{"op":"x","id":N` -- i.e.
 * starting with a comma or empty. Returns the id, or -1 if there is no
 * engine to write to (the line is still recorded for the test). */
static int k_send(int op, const char *opname, const char *rest)
{
    int id = K.next_id++;
    char line[WLINE_OUT];
    int n = snprintf(line, sizeof line, "{\"op\":\"%s\",\"id\":%d%s}\n",
                     opname, id, rest ? rest : "");
    if (n <= 0 || n >= (int)sizeof line) return -1;
    snprintf(W.sent_last, sizeof W.sent_last, "%s", line);
    if (K.fd < 0) return -1;
    if (K.out_n + (size_t)n > sizeof K.out) return -1;
    memcpy(K.out + K.out_n, line, (size_t)n);
    K.out_n += (size_t)n;
    k_note(id, op);
    k_flush();
    return id;
}

/* A line with no id and no reply: "did". */
static void k_send_raw(const char *line)
{
    snprintf(W.sent_last, sizeof W.sent_last, "%s\n", line);
    if (K.fd < 0) return;
    size_t n = strlen(line);
    if (K.out_n + n + 1 > sizeof K.out) return;
    memcpy(K.out + K.out_n, line, n);
    K.out[K.out_n + n] = '\n';
    K.out_n += n + 1;
    k_flush();
}

static void set_error(const char *code, const char *line);

static void k_close(shell_ctx *c)
{
    (void)c;
    if (K.fd >= 0) close(K.fd);
    K.fd = -1;
    K.in_n = K.out_n = 0;
    memset(K.pend, 0, sizeof K.pend);
    K.next_try = now_ms() + 3000;
    /* She was waiting on it: say so, rather than wait 20 s for nothing. */
    if (W.req_id) { W.req_id = 0; set_error("broken", NULL); }
}

static int k_connect(void)
{
    if (K.fd >= 0) return 0;
    char p[sizeof(((struct sockaddr_un *)0)->sun_path)];
    if (sock_path(p, sizeof p) < 0) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    memcpy(a.sun_path, p, strlen(p) + 1);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        close(fd);
        K.next_try = now_ms() + 5000;
        return -1;
    }
    K.fd = fd;
    K.in_n = K.out_n = 0;
    hold_sent[0] = 0;                    /* say it again to a new engine */
    k_send(OP_HELLO, "hello", NULL);
    return 0;
}

static int k_pump(shell_ctx *c)
{
    if (K.fd < 0) return 0;
    int dirty = 0;
    for (;;) {
        if (K.in_n >= sizeof K.in) { k_close(c); return 1; }
        ssize_t k = recv(K.fd, K.in + K.in_n, sizeof K.in - K.in_n, MSG_DONTWAIT);
        if (k == 0) { k_close(c); return 1; }
        if (k < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
            k_close(c); return 1;
        }
        K.in_n += (size_t)k;
        char *nl;
        while (K.fd >= 0 && (nl = memchr(K.in, '\n', K.in_n)) != NULL) {
            size_t len = (size_t)(nl - K.in);
            if (len > WLINE_MAX) { k_close(c); return 1; }
            *nl = 0;
            handle_line(c, K.in, len);
            dirty = 1;
            size_t used = len + 1;
            memmove(K.in, K.in + used, K.in_n - used);
            K.in_n -= used;
        }
        if (K.fd >= 0 && K.in_n > WLINE_MAX) { k_close(c); return 1; }
    }
    k_flush();
    return dirty;
}

int wish_fd(void) { return AUROS_WISHES ? K.fd : -1; }

/* ── pages ───────────────────────────────────────────────────────── */

static void go(int page)
{
    W.page = page;
    W.hover = -1;
    W.sel = -1;
    W.box_focus = (page == WP_ASK);
    W.removing = -1;
}

static int err_index(const char *code)
{
    for (int i = 0; i < N_ERRS; i++) if (!strcmp(ERRS[i].code, code)) return i;
    return 6;                         /* broken */
}

static void set_error(const char *code, const char *line)
{
    W.err = err_index(code ? code : "broken");
    W.retry_after = 0;
    snprintf(W.note, sizeof W.note, "%s", line && *line ? line : "");
    go(WP_ERROR);
}

static void begin_request(int op, int id)
{
    W.req_op = op;
    W.req_id = id;
    W.req_t0 = now_ms();
}

/* What she typed or said, normalised for the three phrases the device
 * answers itself (§7.9): Stop, turn off wishes, what can you do. */
static void plain_words(const char *in, char *out, size_t n)
{
    size_t o = 0; int sp = 1;
    for (; *in && o + 1 < n; in++) {
        unsigned char ch = (unsigned char)*in;
        if (isalnum(ch) || ch >= 0x80) { out[o++] = (char)tolower(ch); sp = 0; }
        else if (!sp) { out[o++] = ' '; sp = 1; }
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

static int is_one_of(const char *s, const char *const *set)
{
    for (; *set; set++) if (!strcmp(s, *set)) return 1;
    return 0;
}

static void send_text(shell_ctx *c, const char *text, int voice);

/* Her words -> a request, or one of the things the device does itself. */
static void submit(shell_ctx *c)
{
    char plain[TEXT_MAX + 1];
    plain_words(W.text, plain, sizeof plain);
    static const char *const STOP[] = { "stop", "stop it", "stop that",
                                        "make it stop", NULL };
    static const char *const OFF[]  = { "turn off wishes", "turn off all wishes",
                                        "turn off my wishes", "stop all wishes",
                                        "stop wishes", "wishes off", NULL };
    static const char *const WHAT[] = { "what can you do", "help",
                                        "what can i wish for", NULL };
    W.hint[0] = 0;
    if (is_one_of(plain, STOP)) {
        wishover_clear(c);
        snprintf(W.sentence, sizeof W.sentence, "%s",
                 "Everything your wishes put on the screen has gone.");
        W.title[0] = 0;
        W.undo_until = 0;
        go(WP_DONE);
        return;
    }
    if (is_one_of(plain, OFF)) {
        k_send(OP_ALLOFF, "all_off", NULL);
        wishover_clear(c);
        W.all_off = 1;
        snprintf(W.sentence, sizeof W.sentence, "%s",
                 "All your wishes are off. You can turn them back on in My wishes.");
        W.undo_until = 0;
        go(WP_DONE);
        return;
    }
    if (is_one_of(plain, WHAT)) {
        W.text[0] = 0;
        W.shown_once = 0;             /* all six examples again */
        snprintf(W.hint, sizeof W.hint, "%s",
                 "Here are some things you can ask for. Press one to try it.");
        return;
    }
    /* §7.9: under 3 characters is not sent. */
    if (strlen(plain) < 3) {
        snprintf(W.hint, sizeof W.hint, "%s",
                 "Tell me a little more about what you would like.");
        return;
    }
    send_text(c, W.text, W.voice);
}

static void send_text(shell_ctx *c, const char *text_in, int voice)
{
    (void)c;
    /* A copy first: the caller may be handing us W.said itself (consent
     * arrived, now send what she had asked), and copying a string onto
     * itself leaves it empty. */
    char text[TEXT_MAX + 1];
    snprintf(text, sizeof text, "%s", text_in);
    /* One at a time (§7.9): a second press while one is out is nothing. */
    if (W.req_id) return;
    /* The same words again within 30 s are not sent again. */
    if (!strcmp(text, W.last_text) && now_ms() - W.last_ms < SAME_TEXT_MS &&
        W.last_ms) {
        snprintf(W.hint, sizeof W.hint, "%s",
                 "You asked that a moment ago. Wait a little, then try again.");
        go(WP_ASK);
        snprintf(W.text, sizeof W.text, "%s", text);
        return;
    }
    snprintf(W.said, sizeof W.said, "%s", text);
    snprintf(W.last_text, sizeof W.last_text, "%s", text);
    W.last_ms = now_ms();
    if (K.fd < 0) k_connect();
    if (K.fd < 0) { set_error("broken", NULL); return; }
    if (W.consent == 0) { go(WP_CONSENT); return; }
    char q[TEXT_MAX * 6 + 8], rest[WLINE_OUT];
    js_quote(q, sizeof q, text);
    snprintf(rest, sizeof rest, ",\"text\":%s,\"lang\":\"en\",\"voice\":%s",
             q, voice ? "true" : "false");
    int id = k_send(OP_WISH, "wish", rest);
    if (id < 0) { set_error("broken", NULL); return; }
    begin_request(OP_WISH, id);
    go(WP_WAIT);
}

static void cancel_request(void)
{
    if (W.req_id) {
        char rest[32];
        /* cancel names the request it stops, so it carries that id. */
        snprintf(rest, sizeof rest, "{\"op\":\"cancel\",\"id\":%d}", W.req_id);
        k_send_raw(rest);
    }
    W.req_id = 0;
    W.last_ms = 0;                    /* she may ask the same thing again */
}

/* Read aloud: her voice used, or "Read it to me". The sentence's * marks
 * are for the eye only. */
static void say_aloud(const char *s)
{
    char plain[SENT_MAX];
    size_t o = 0;
    for (; *s && o + 1 < sizeof plain; s++) if (*s != '*') plain[o++] = *s;
    plain[o] = 0;
    if (!plain[0] || access(WISH_VOICE_BIN, X_OK) != 0) return;
    const char *argv[] = { WISH_VOICE_BIN, "say", plain, NULL };
    run_detached(argv);
}

/* ── what the engine says ───────────────────────────────────────── */

static void on_reply(shell_ctx *c, int id, int op, const char *kind,
                     const char *L, const char *E)
{
    (void)c;
    int mine = (id && id == W.req_id);
    if (!strcmp(kind, "hello")) {
        W.consent = js_bool(L, E, "consent", 0);
        W.all_off = js_bool(L, E, "all_off", 0);
        return;
    }
    if (!strcmp(kind, "list")) {
        W.all_off = js_bool(L, E, "all_off", W.all_off);
        W.undo_engine = js_bool(L, E, "undo", 0);
        const char *ve, *arr = js_get(L, E, "wishes", &ve);
        W.n_rows = 0;
        for (const char *it = arr ? js_first(arr, ve) : NULL; it && W.n_rows < ROWS_MAX;
             it = js_next(it, ve)) {
            const char *ie = js_skip(it, ve, 1);
            if (!ie || *it != '{') continue;
            wish_row *r = &W.rows[W.n_rows];
            memset(r, 0, sizeof *r);
            if (js_getstr(it, ie, "wid", r->wid, sizeof r->wid) < 0 || !r->wid[0]) continue;
            js_getstr(it, ie, "title", r->title, sizeof r->title);
            js_getstr(it, ie, "sentence", r->sentence, sizeof r->sentence);
            char st[48];
            js_getstr(it, ie, "status", st, sizeof st);
            r->on = !strcmp(st, "on");
            r->paused = !strncmp(st, "paused", 6);
            W.n_rows++;
        }
        if (W.first_row >= W.n_rows) W.first_row = 0;
        return;
    }
    if (!strcmp(kind, "today")) {
        const char *ve, *arr = js_get(L, E, "items", &ve);
        W.n_today = 0;
        for (const char *it = arr ? js_first(arr, ve) : NULL; it && W.n_today < ROWS_MAX;
             it = js_next(it, ve)) {
            const char *ie = js_skip(it, ve, 1);
            if (!ie || *it != '{') continue;
            int i = W.n_today;
            W.today[i].title[0] = W.today[i].tstr[0] = 0;
            js_getstr(it, ie, "title", W.today[i].title, sizeof W.today[i].title);
            const char *tv, *t = js_get(it, ie, "times", &tv);
            W.today[i].times = t ? (int)js_int(it, ie, "times", -1) : -1;
            if (t && W.today[i].times < 0) js_getstr(it, ie, "times", W.today[i].tstr, sizeof W.today[i].tstr);
            if (W.today[i].title[0]) W.n_today++;
        }
        if (W.first_row >= W.n_today) W.first_row = 0;
        return;
    }
    if (!strcmp(kind, "error")) {
        char code[24], line[SENT_MAX];
        js_getstr(L, E, "error", code, sizeof code);
        js_getstr(L, E, "text", line, sizeof line);
        if (!mine && op != OP_CONSENT) return;
        W.req_id = 0;
        if (!strcmp(code, "consent")) { W.consent = 0; go(WP_CONSENT); return; }
        long ra = js_int(L, E, "retry_after", 0);
        set_error(code, line);
        W.retry_after = ra > 0 && ra <= 3600 ? (int)ra : 0;
        return;
    }
    if (!strcmp(kind, "wish") && mine) {
        W.req_id = 0;
        js_getstr(L, E, "pending", W.pending, sizeof W.pending);
        js_getstr(L, E, "sentence", W.sentence, sizeof W.sentence);
        js_getstr(L, E, "title", W.title, sizeof W.title);
        W.dest[0] = W.dest_host[0] = 0;
        const char *ve, *o = js_get(L, E, "outward", &ve);
        if (o) {
            js_getstr(o, ve, "name", W.dest, sizeof W.dest);
            js_getstr(o, ve, "host", W.dest_host, sizeof W.dest_host);
        }
        if (!W.pending[0] || !W.sentence[0]) { set_error("broken", NULL); return; }
        go(WP_CONFIRM);
        if (W.voice) say_aloud(W.sentence);
        return;
    }
    if (!strcmp(kind, "cannot") && mine) {
        W.req_id = 0;
        js_getstr(L, E, "text", W.note, sizeof W.note);
        if (!W.note[0]) {
            char miss[24];
            js_getstr(L, E, "missing", miss, sizeof miss);
            snprintf(W.note, sizeof W.note, "%s",
                     !strcmp(miss, "compass") ? "This computer has no compass." :
                     !strcmp(miss, "tilt")    ? "This computer cannot tell which way up it is." :
                     !strcmp(miss, "light")   ? "This computer has no light sensor." :
                     !strcmp(miss, "install") ? "Wishes cannot install programs. Get more programs can." :
                                                "That is not something a wish can do yet.");
        }
        /* "I can't do that, but I can ...": the nearest wish the engine can do,
         * already checked and waiting for her Yes. */
        W.pending[0] = W.sentence[0] = W.title[0] = W.dest[0] = W.dest_host[0] = 0;
        const char *ae, *alt = js_get(L, E, "alt", &ae);
        if (alt && *alt == '{') {
            js_getstr(alt, ae, "pending", W.pending, sizeof W.pending);
            js_getstr(alt, ae, "sentence", W.sentence, sizeof W.sentence);
            js_getstr(alt, ae, "title", W.title, sizeof W.title);
            const char *oe, *o = js_get(alt, ae, "outward", &oe);
            if (o && *o == '{') {
                js_getstr(o, oe, "name", W.dest, sizeof W.dest);
                js_getstr(o, oe, "host", W.dest_host, sizeof W.dest_host);
            }
            if (!W.sentence[0]) W.pending[0] = 0;
        }
        go(WP_CANNOT);
        if (W.voice) say_aloud(W.note);
        return;
    }
    if (!strcmp(kind, "help") && mine) {
        W.req_id = 0;
        js_getstr(L, E, "page", W.help_page, sizeof W.help_page);
        js_getstr(L, E, "pending", W.pending, sizeof W.pending);
        js_getstr(L, E, "sentence", W.sentence, sizeof W.sentence);
        if (!W.pending[0]) W.sentence[0] = 0;
        go(WP_ANSWER);
        return;
    }
    if (!strcmp(kind, "done")) {
        if (op == OP_CONFIRM) {
            char wid[40];
            js_getstr(L, E, "wid", wid, sizeof wid);
            /* A `now` wish has no wid: it ran, and Undo is offered. */
            W.undo_until = wid[0] ? 0 : now_ms() + UNDO_MS;
            go(WP_DONE);
            /* Next to where she makes wishes, for ten minutes: a button
             * that puts it back. It is a card, so it is there when she
             * closes the panel, and it never takes the keyboard. */
            if (W.undo_until)
                wishover_card(c, WO_UNDO, "It is done. If it is not what you wanted, you can put it back.",
                              W.title[0] ? W.title : NULL, NULL, NULL, 0);
        } else if (op == OP_CONSENT && W.consent == 1 && W.said[0]) {
            W.last_ms = 0;
            snprintf(W.text, sizeof W.text, "%s", W.said);
            send_text(c, W.said, W.voice);
        } else if (op == OP_ALLOFF || op == OP_ALLON || op == OP_ENABLE ||
                   op == OP_REMOVE || op == OP_RESTORE || op == OP_UNDO) {
            if (W.page == WP_MINE) k_send(OP_LIST, "list", NULL);
        }
        return;
    }
}

/* ── what only the shell can do (§3.7 "shell" events) ──────────── */

static const struct { const char *name; float scale; } SIZES[] = {
    { "normal", 1.00f }, { "bigger", 1.30f }, { "biggest", 1.60f },
};

void wish_apply_text_scale(shell_ctx *c, float v);
int  wish_apply_look(shell_ctx *c, const char *look);

void wish_apply_text_scale(shell_ctx *c, float v)
{
    if (v < 0.8f) v = 0.8f;
    if (v > 2.0f) v = 2.0f;
    c->text_scale = v;
    c->text_changed = 1;
    foot_save_text_scale(v);
}

static const char *const LOOKS[] = { "taskbar", "dock", "rail", "tiles", NULL };

/* The six the shell has: what a wish may NOT choose (the kiosk and the
 * tiler) but must be able to go back to if that is what she had. */
static const char *const LOOKS_ALL[] = { "taskbar", "dock", "rail", "tiles",
                                         "workbench", "locked", NULL };

/* 1 if the look was changed. The ONE place that decides a look name is
 * allowed: the file it names is then looked for in the shell's own
 * directory, so no path she was sent can lead anywhere else. */
static int apply_look(shell_ctx *c, const char *look, const char *const *allowed)
{
    if (!is_one_of(look, allowed)) return 0;
    const char *home = getenv("HOME");
    if (!home || !*home) return 0;
    char dir[448], link[512], target[512];
    snprintf(dir, sizeof dir, "%s/.config", home);
    mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/.config/auros", home);
    mkdir(dir, 0755);
    snprintf(link, sizeof link, "%s/active.shell", dir);
    const char *sd = getenv("AUROS_SHELLS");
    snprintf(target, sizeof target, "%s/%s.shell",
             sd && *sd ? sd : "/usr/share/auros/shells", look);
    if (access(target, R_OK) != 0) return 0;
    unlink(link);
    if (symlink(target, link) != 0) return 0;
    c->want_reload = 1;
    return 1;
}

/* A wish may choose one of four. */
int wish_apply_look(shell_ctx *c, const char *look)
{
    return apply_look(c, look, LOOKS);
}

/* Putting back what she had: any of the six, and only what the shell
 * itself reported as its look. */
int wish_restore_look(shell_ctx *c, const char *look)
{
    return apply_look(c, look, LOOKS_ALL);
}

/* An https address with nothing in it a browser could take as a flag
 * or the shell as a word. The engine took it from the shipped site
 * list; this is the second look, not the first. */
static int plain_https(const char *u)
{
    if (strncmp(u, "https://", 8) || strlen(u) > 200) return 0;
    for (const char *p = u; *p; p++)
        if ((unsigned char)*p <= ' ' || *p == '"' || *p == '\'' || *p == '\\' ||
            *p == '`' || *p == '<' || *p == '>' || (unsigned char)*p >= 0x7F) return 0;
    return 1;
}

static void open_url(shell_ctx *c, const char *url)
{
    if (!c->spawn) return;
    char blob[512];
    static const char HEAD[] =
        "sh\0-c\0"
        "command -v firefox >/dev/null 2>&1 && exec firefox --new-window \"$0\"; "
        "exec xdg-open \"$0\"";
    size_t hl = sizeof HEAD;          /* includes the NUL after the script */
    size_t ul = strlen(url) + 1;
    if (hl + ul + 1 > sizeof blob) return;
    memcpy(blob, HEAD, hl);
    memcpy(blob + hl, url, ul);
    blob[hl + ul] = 0;
    c->spawn(c, blob, 4);
}

static void do_shell(shell_ctx *c, const char *L, const char *E)
{
    long n = js_int(L, E, "n", 0);
    char title[TITLE_MAX], wid[40], type[32];
    js_getstr(L, E, "title", title, sizeof title);
    js_getstr(L, E, "wid", wid, sizeof wid);
    const char *de, *d = js_get(L, E, "do", &de);
    char line[600];
    int ok = 0;
    char prev[256] = "";
    if (d && js_getstr(d, de, "type", type, sizeof type) == 0) {
        if (!strcmp(type, "set_text_size")) {
            char sz[16];
            js_getstr(d, de, "size", sz, sizeof sz);
            for (int i = 0; i < 3; i++)
                if (!strcmp(sz, SIZES[i].name)) {
                    float old = c->text_scale > 0.1f ? c->text_scale : 1.f;
                    /* The nearest named size, for the engine's Undo. */
                    int near = 0;
                    for (int j = 1; j < 3; j++) {
                        float a = SIZES[j].scale - old, b = SIZES[near].scale - old;
                        if (a * a < b * b) near = j;
                    }
                    snprintf(prev, sizeof prev,
                             "{\"type\":\"set_text_size\",\"size\":\"%s\"}",
                             SIZES[near].name);
                    char olds[16];
                    snprintf(olds, sizeof olds, "%.2f", (double)old);
                    wish_apply_text_scale(c, SIZES[i].scale);
                    wishover_keep(c, WO_KEEP_TEXT, olds, title);
                    ok = 1;
                }
        } else if (!strcmp(type, "set_look")) {
            char look[16];
            js_getstr(d, de, "look", look, sizeof look);
            char old[24];
            snprintf(old, sizeof old, "%s", c->layout_id);
            /* The engine's Undo only knows the four; Keep this? knows all
             * six, so a wish can always be taken back by pressing it. */
            if (is_one_of(old, LOOKS))
                snprintf(prev, sizeof prev,
                         "{\"type\":\"set_look\",\"look\":\"%s\"}", old);
            ok = wish_apply_look(c, look);
            if (ok && is_one_of(old, LOOKS_ALL)) wishover_keep(c, WO_KEEP_LOOK, old, title);
            if (!ok) prev[0] = 0;
        } else if (!strcmp(type, "open_app")) {
            char app[64];
            js_getstr(d, de, "app", app, sizeof app);
            size_t al = strlen(app);
            for (int i = 0; i < c->n_apps && al; i++) {
                const char *id = c->apps[i].id;
                if (!strcmp(id, app) ||
                    (!strncmp(id, app, al) && !strcmp(id + al, ".desktop"))) {
                    c->want_launch = i + 1;
                    ok = 1;
                    break;
                }
            }
        } else if (!strcmp(type, "open_website")) {
            char url[256];
            js_getstr(d, de, "url", url, sizeof url);
            if (plain_https(url) && c->spawn) { open_url(c, url); ok = 1; }
        } else if (!strcmp(type, "do_not_disturb")) {
            /* The engine applies and times this before it sends the shell
             * event: its may_interrupt() is the control that holds back
             * wish cards and sounds. This side only acknowledges that
             * already-applied local state, and still rejects a malformed
             * event rather than claiming it worked. */
            const char *be, *b = js_get(d, de, "on", &be);
            ok = b && ((be - b == 4 && !memcmp(b, "true", 4)) ||
                       (be - b == 5 && !memcmp(b, "false", 5)));
        }
#if AUROS_WISHES
        else if (!strcmp(type, "set_wallpaper")) {
            char theme[48], old[48];
            const char *ce;
            int has_color = js_get(d, de, "color", &ce) != NULL;
            if (!has_color &&
                js_getstr(d, de, "theme_name", theme, sizeof theme) == 0 &&
                settings_theme_current(old, sizeof old) == 0 &&
                settings_theme_apply(c, theme)) {
                snprintf(prev, sizeof prev,
                         "{\"type\":\"set_wallpaper\",\"theme\":\"%s\"}", old);
                ok = 1;
            }
        }
#endif
        /* Color wallpaper, night_light and pointer_size have
         * no control in this shell yet: refused honestly, nothing faked. */
    }
    if (prev[0])
        snprintf(line, sizeof line, "{\"op\":\"did\",\"n\":%ld,\"ok\":%s,\"prev\":%s}",
                 n, ok ? "true" : "false", prev);
    else
        snprintf(line, sizeof line, "{\"op\":\"did\",\"n\":%ld,\"ok\":%s}",
                 n, ok ? "true" : "false");
    k_send_raw(line);
}

/* The typed-word matcher is the compositor's (src/aurwl/wishhook.c,
 * stream W1). The shell hands it the list and passes on what it found.
 * Weak, so this file links into every harness that has no compositor. */
extern void wishhook_set(const char *const *words, int nwords,
                         const char *const *apps, int napps) __attribute__((weak));
extern int  wishhook_take_word(char out[33]) __attribute__((weak));
extern int  wishhook_take_app(char out[128]) __attribute__((weak));

static void on_triggers(const char *L, const char *E)
{
    static char words[10][33], apps[16][64];
    const char *wp[10], *ap[16];
    int nw = 0, na = 0;
    const char *ve, *arr = js_get(L, E, "words", &ve);
    for (const char *it = arr ? js_first(arr, ve) : NULL; it && nw < 10; it = js_next(it, ve)) {
        const char *ie = js_skip(it, ve, 1);
        if (ie && js_str(it, ie, words[nw], sizeof words[nw]) == 0 && words[nw][0]) {
            wp[nw] = words[nw]; nw++;
        }
    }
    arr = js_get(L, E, "apps", &ve);
    for (const char *it = arr ? js_first(arr, ve) : NULL; it && na < 16; it = js_next(it, ve)) {
        const char *ie = js_skip(it, ve, 1);
        if (ie && js_str(it, ie, apps[na], sizeof apps[na]) == 0 && apps[na][0]) {
            ap[na] = apps[na]; na++;
        }
    }
    if (wishhook_set) wishhook_set(wp, nw, ap, na);
}

static void on_event(shell_ctx *c, const char *ev, const char *L, const char *E)
{
    char title[TITLE_MAX], wid[40], text[SENT_MAX];
    js_getstr(L, E, "title", title, sizeof title);
    js_getstr(L, E, "wid", wid, sizeof wid);
    if (!strcmp(ev, "triggers")) { on_triggers(L, E); return; }
    if (!strcmp(ev, "changed")) {
        if (c->wish_open && W.page == WP_MINE) k_send(OP_LIST, "list", NULL);
        return;
    }
    if (!strcmp(ev, "fill")) {
        char emoji[32];
        js_getstr(L, E, "emoji", emoji, sizeof emoji);
        js_getstr(L, E, "text", text, sizeof text);
        long s = js_int(L, E, "seconds", 3);
        wishover_fill(c, emoji, text, (int)s, title, wid);
        return;
    }
    if (!strcmp(ev, "message")) {
        js_getstr(L, E, "text", text, sizeof text);
        wishover_card(c, WO_MESSAGE, text, title, wid, NULL, 0);
        return;
    }
    if (!strcmp(ev, "remind")) {
        char when[24];
        js_getstr(L, E, "text", text, sizeof text);
        js_getstr(L, E, "for", when, sizeof when);
        wishover_card(c, WO_REMIND, text, title, wid, when[0] ? when : NULL,
                      js_bool(L, E, "private", 0));
        return;
    }
    if (!strcmp(ev, "notice")) {
        js_getstr(L, E, "text", text, sizeof text);
        wishover_card(c, WO_MESSAGE, text, NULL, NULL, NULL, 0);
        return;
    }
    if (!strcmp(ev, "keep_it")) {
        js_getstr(L, E, "sentence", text, sizeof text);
        wishover_card(c, WO_KEEP, text, title, wid, NULL, 0);
        return;
    }
    if (!strcmp(ev, "shell")) { do_shell(c, L, E); return; }
}

static void handle_line(shell_ctx *c, const char *line, size_t len)
{
    snprintf(W.recv_last, sizeof W.recv_last, "%.*s", (int)(len < sizeof W.recv_last - 1 ? len : sizeof W.recv_last - 1), line);
    const char *E = line + len;
    const char *p = js_ws(line, E);
    if (p >= E || *p != '{' || js_skip(p, E, 0) == NULL) return;
    char kind[24], ev[24];
    if (js_getstr(p, E, "ev", ev, sizeof ev) == 0) { on_event(c, ev, p, E); return; }
    if (js_getstr(p, E, "kind", kind, sizeof kind) < 0) return;
    int id = (int)js_int(p, E, "id", 0);
    int op = k_take(id);
    on_reply(c, id, op, kind, p, E);
}

void wish_feed_line(shell_ctx *c, const char *line)
{
    handle_line(c, line, strlen(line));
}
const char *wish_last_sent(void) { return W.sent_last; }
const char *wish_last_recv(void) { return W.recv_last; }

/* ── voice: tap to start, tap to stop, at most 30 s (§7.9) ────────── */

static void voice_kill(int sig)
{
    if (W.vpid > 0) kill(W.vpid, sig);
}

static void voice_reap(void)
{
    if (W.vpid > 0) {
        int st;
        if (waitpid(W.vpid, &st, WNOHANG) == W.vpid) W.vpid = 0;
    }
}

static int voice_here(void) { return access(WISH_VOICE_BIN, X_OK) == 0; }

static void voice_start(void)
{
    if (W.vpid > 0 || !voice_here()) { set_error("nomic", NULL); return; }
    int p[2];
    if (pipe2(p, O_CLOEXEC) < 0) { set_error("nomic", NULL); return; }
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); set_error("nomic", NULL); return; }
    if (pid == 0) {
        dup2(p[1], 1);
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 2); }
        execl(WISH_VOICE_BIN, WISH_VOICE_BIN, "listen", "--max", "30", (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    W.vpid = pid;
    W.vfd = p[0];
    W.vn = 0;
    W.v_t0 = now_ms();
    go(WP_LISTEN);
}

/* She pressed "I've finished": the helper stops recording and writes
 * down what it heard. Its audio never leaves its own memory. */
static void voice_stop(void)
{
    if (W.vpid > 0) voice_kill(SIGUSR1);
    go(WP_HEARING);
}

static void voice_cancel(void)
{
    if (W.vpid > 0) voice_kill(SIGTERM);
    if (W.vfd >= 0) { close(W.vfd); W.vfd = -1; }
    voice_reap();
}

/* What the helper printed, once it has finished:
 *   heard <words>     unsure     nomic     (anything else: broken) */
static void voice_result(shell_ctx *c)
{
    W.vbuf[W.vn] = 0;
    char *nl = strchr(W.vbuf, '\n');
    if (nl) *nl = 0;
    if (!strncmp(W.vbuf, "heard ", 6) && W.vbuf[6]) {
        snprintf(W.text, sizeof W.text, "%.300s", W.vbuf + 6);
        W.voice = 1;
        go(WP_ASK);
        submit(c);
    } else if (!strcmp(W.vbuf, "nomic")) {
        set_error("nomic", NULL);
    } else {
        set_error("unsure", NULL);
    }
}

static int voice_step(shell_ctx *c)
{
    if (W.vfd < 0) return 0;
    int dirty = 0;
    for (;;) {
        if (W.vn + 1 >= sizeof W.vbuf) break;
        ssize_t k = read(W.vfd, W.vbuf + W.vn, sizeof W.vbuf - 1 - W.vn);
        if (k > 0) { W.vn += (size_t)k; continue; }
        if (k == 0) {
            close(W.vfd); W.vfd = -1;
            voice_reap();
            if (W.page == WP_LISTEN || W.page == WP_HEARING) voice_result(c);
            return 1;
        }
        break;
    }
    /* The 30-second ceiling is the shell's too, not only the helper's. */
    if (W.page == WP_LISTEN && now_ms() - W.v_t0 > LISTEN_MAX_MS) {
        voice_stop();
        dirty = 1;
    }
    voice_reap();
    return dirty;
}

/* ── the host's half ─────────────────────────────────────────────── */

void wish_command(shell_ctx *c, int cmd)
{
    if (!c || c->kiosk || c->no_wishes || !AUROS_WISHES) return;
    if (cmd == WISH_CMD_TODAY) {
        c->wish_open = 0;                    /* foot_open_only toggles */
        foot_open_only(c, &c->wish_open);
        c->wish_open = WISH_OPEN_TODAY;      /* the host loop calls wish_opened */
    } else if (cmd == WISH_CMD_STOP_ALL) {
        if (K.fd < 0) k_connect();
        k_send(OP_ALLOFF, "all_off", NULL);
        wishover_clear(c);
        W.all_off = 1;
        W.undo_until = 0;
        wishover_card(c, WO_MESSAGE, "All your wishes are off. You can turn them back on in My wishes.",
                      NULL, NULL, NULL, 0);
    }
}

int wish_available(const shell_ctx *c)
{
    return c && !c->kiosk;
}

void wish_opened(shell_ctx *c)
{
    W.open_kind = c->wish_open;
    if (K.fd < 0) k_connect();
    if (c->wish_open == WISH_OPEN_TODAY) {
        W.first_row = 0;
        go(WP_TODAY);
        k_send(OP_TODAY, "today", NULL);
        return;
    }
    if (c->wish_open == WISH_OPEN_MINE) {
        W.first_row = 0;
        go(WP_MINE);
        k_send(OP_LIST, "list", NULL);
        return;
    }
    if (W.req_id) { go(WP_WAIT); return; }
    W.g_stage = -1;
    W.g_first = 0;
    W.text[0] = 0;
    W.hint[0] = 0;
    W.voice = 0;
    go(W.consent == 0 ? WP_CONSENT : WP_ASK);
}

void wish_closed(shell_ctx *c)
{
    (void)c;
    if (W.page == WP_CONFIRM && W.pending[0]) {
        char rest[96];
        snprintf(rest, sizeof rest, ",\"pending\":\"%s\"", W.pending);
        k_send(OP_DISCARD, "discard", rest);
    }
    W.pending[0] = 0;
    if (W.vpid > 0 || W.vfd >= 0) voice_cancel();
    if (W.req_id) cancel_request();
    W.shown_once = 1;
}

/* What the engine must know to keep a wish from interrupting her
 * (docs/AI.md §3.5, §3.7): somebody is here, and what she is in the middle
 * of. */
void wish_input_seen(void) { if (AUROS_WISHES) input_pending = 1; }

static void send_presence(shell_ctx *c)
{
    if (K.fd < 0) return;
    /* The shell's own panel where she types a password: nothing a wish
     * does may land on it. (The compositor adds `call`, `camera`,
     * `share`, `fullscreen` and sensitive fields through wishhook.) */
    char reasons[64] = "";
    if (c->net_open || c->bt_open) snprintf(reasons, sizeof reasons, "%s", "\"password\"");
    char line[160];
    snprintf(line, sizeof line, "{\"op\":\"hold\",\"reasons\":[%s]}", reasons);
    if (strcmp(line, hold_sent)) {
        k_send_raw(line);
        snprintf(hold_sent, sizeof hold_sent, "%s", line);
    }
    uint64_t t = now_ms();
    if (input_pending && t - input_sent_ms >= 1000) {
        k_send_raw("{\"op\":\"input\"}");
        input_sent_ms = t;
        input_pending = 0;
    }
}

int wish_step(shell_ctx *c)
{
    /* A build without Wishes (D14) never looks for the engine, never wakes
     * the loop, never sends a line. */
    if (!AUROS_WISHES) return 0;
    int dirty = 0;
    if (K.fd < 0 && now_ms() >= K.next_try && !c->kiosk) {
        if (k_connect() == 0) dirty = 1;
    }
    if (k_pump(c)) dirty = 1;
    send_presence(c);
    if (voice_step(c)) dirty = 1;
    if (W.req_id && now_ms() - W.req_t0 > WAIT_GIVE_UP_MS) {
        cancel_request();
        set_error("slow", NULL);
        dirty = 1;
    }
    /* The compositor's half: what it matched, handed on. */
    char got[128], q[560], line[640];
    if (wishhook_take_word)
        while (wishhook_take_word(got)) {
            js_quote(q, sizeof q, got);
            snprintf(line, sizeof line, "{\"op\":\"typed\",\"word\":%s}", q);
            k_send_raw(line);
        }
    if (wishhook_take_app)
        while (wishhook_take_app(got)) {
            js_quote(q, sizeof q, got);
            snprintf(line, sizeof line, "{\"op\":\"app\",\"app\":%s}", q);
            k_send_raw(line);
        }
    if (W.undo_until && now_ms() > W.undo_until) { W.undo_until = 0; dirty = 1; }
    if (wishover_step(c)) dirty = 1;
    /* While waiting, the three dots breathe. */
    if (c->wish_open && (W.page == WP_WAIT || W.page == WP_LISTEN ||
                         W.page == WP_HEARING)) dirty = 1;
    return dirty;
}

int wish_wait_ms(void)
{
    if (!AUROS_WISHES) return -1;
    int w = wishover_wait_ms();
    int mine = (W.req_id || W.vpid > 0 || W.vfd >= 0) ? 120 : -1;
    if (K.fd < 0 && mine < 0) mine = 5000;
    if (mine >= 0 && (w < 0 || mine < w)) w = mine;
    return w;
}

void wish_send_stop(const char *wid)
{
    if (!wid || !*wid) return;
    char q[96], rest[128];
    js_quote(q, sizeof q, wid);
    snprintf(rest, sizeof rest, ",\"wid\":%s,\"on\":false", q);
    k_send(OP_ENABLE, "enable", rest);
}

void wish_send_snooze(const char *wid, int minutes)
{
    (void)minutes;                    /* the engine's snooze is 10 min */
    if (!wid || !*wid) return;
    char q[96], rest[128];
    js_quote(q, sizeof q, wid);
    snprintf(rest, sizeof rest, ",\"wid\":%s", q);
    k_send(OP_SNOOZE, "snooze", rest);
}

void wish_send_keep(const char *wid, int yes)
{
    if (!wid || !*wid) return;
    char q[96], rest[128];
    js_quote(q, sizeof q, wid);
    snprintf(rest, sizeof rest, ",\"wid\":%s,\"yes\":%s", q, yes ? "true" : "false");
    k_send(OP_KEEP, "keep", rest);
}

int wish_undo_offered(void) { return W.undo_until != 0; }

void wish_undo(shell_ctx *c)
{
    (void)c;
    k_send(OP_UNDO, "undo", NULL);
    W.undo_until = 0;
}

void wish_set_page(int page) { if (page >= 0 && page < WP_N) go(page); }
int  wish_page(void) { return W.page; }

/* ── where everything is ─────────────────────────────────────────── */

/* docs/DESIGN-DIRECTION.md §3: a 4 px grid; inset 24 at 1024 wide and
 * 32 above; 32 between groups; 8 from a label to its control; targets
 * 48 px and primary buttons 56; 24 px between things with different
 * consequences and 32 before anything that leaves or undoes. */
typedef struct {
    float k;
    int gx, cw, sh;
    int px_head, px_body, px_lead, px_sent, px_said;
    int btn_h, gap, group, pad;
    int head_y, head_h;          /* the heading block                  */
    int head_lines;              /* 0 when the page gave it up         */
    int labels;                  /* the small labels are shown         */
    int overflow;
    rect a[8];                   /* text areas, by A_*                 */
    wish_target t[WISH_MAX_TARGETS];
    int n;
    int n_ex, ex_cols;
    int mine_stacked, mine_n, mine_row_h, mine_text_h;
} wgeom;

enum { A_SUB, A_LABEL1, A_TEXT1, A_LABEL2, A_TEXT2, A_DEST, A_NOTE, A_BAR };


/* How wide a line of `len` characters is likely to be at `px`. The
 * geometry is a pure function of the view, so it cannot ask a font;
 * 0.56 em is a little wider than the UI face's average, so what it
 * reserves is enough and the painter never has to cut a word off. */
static int est_w(int len, int px) { return (int)(0.56f * (float)px * (float)len); }

static void add_t(wgeom *g, rect r, int kind, int arg)
{
    if (g->n >= WISH_MAX_TARGETS) return;
    g->t[g->n].r = r; g->t[g->n].kind = kind; g->t[g->n].arg = arg;
    g->n++;
}

static const char *label_of(int kind, int page);

/* Buttons that leave the panel or undo something stand apart. */
static int leaves(int kind)
{
    return kind == WT_CLOSE || kind == WT_UNDO || kind == WT_CONSENT_NO ||
           kind == WT_RESTORE || kind == WT_ALL_OFF;
}

/* A row of buttons, left to right, wrapping when they do not fit;
 * `equal` gives them all the width of the widest. Top-down from y.
 * Returns the y below the last row. */
static int place_row(wgeom *g, int page, int y, const int *kinds, int n, int equal)
{
    int w[8], maxw = 0;
    for (int i = 0; i < n && i < 8; i++) {
        const char *l = label_of(kinds[i], page);
        w[i] = est_w((int)strlen(l), g->px_lead) + 2 * g->pad;
        if (w[i] < (int)(120.f * g->k)) w[i] = (int)(120.f * g->k);
        if (w[i] > g->cw) w[i] = g->cw;
        if (w[i] > maxw) maxw = w[i];
    }
    if (equal) {
        int pair = (g->cw - g->group) / 2;
        int want = (int)(320.f * g->k);
        if (n == 2 && maxw < want) maxw = want < pair ? want : pair;
        if (maxw > pair && n == 2 && est_w(20, g->px_lead) < pair) maxw = pair;
        for (int i = 0; i < n; i++) w[i] = maxw;
    }
    int x = g->gx;
    for (int i = 0; i < n && i < 8; i++) {
        /* The pair of answers is 24 apart; a way out is 32 from the rest. */
        int sep = (i && (leaves(kinds[i]) || equal)) ? g->group : g->gap;
        if (i) x += sep - g->gap;
        if (x > g->gx && x + w[i] > g->gx + g->cw) {
            x = g->gx;
            y += g->btn_h + g->gap;
        }
        add_t(g, (rect){ x, y, w[i], g->btn_h }, kinds[i], 0);
        x += w[i] + g->gap;
    }
    return y + g->btn_h;
}

static int row_height(wgeom *g, int page, const int *kinds, int n, int equal)
{
    wgeom tmp = *g;
    tmp.n = 0;
    return place_row(&tmp, page, 0, kinds, n, equal);
}

static const char *EXAMPLE_TEXT(int i) { return EXAMPLES[i]; }

/* One page, with or without its heading and its small labels. Sets
 * g->overflow when the page did not fit as drawn; layout() then asks
 * again with less. Nothing is ever moved on top of something else. */
static void layout_page(const shell_ctx *c, int sw, int sh, const wish_view *v,
                        wgeom *g, float k, int with_head, int with_labels)
{
    (void)c;
    /* While her text is bigger than the theme's own, a page that cannot
     * show its main thing at that size is a page to try again at a smaller
     * one, not to squeeze: the sentence she must read, or at least a few
     * choices. At the theme's own size there is nothing smaller to try. */
    int strict = k > 1.001f;
    memset(g, 0, sizeof *g);
    g->k = k;
    g->sh = sh;
    g->gx = sw <= 1100 ? 24 : 32;
    g->cw = sw - 2 * g->gx;
    if (g->cw > 1040) g->cw = 1040;
    g->px_head = (int)(40.f * k);
    g->px_body = (int)(20.f * k);
    g->px_lead = (int)(22.f * k);
    g->px_sent = (int)(26.f * k);
    g->px_said = (int)(24.f * k);
    g->btn_h = (int)(36.f + 20.f * k); if (g->btn_h < 48) g->btn_h = 48;
    g->gap = (int)(12.f * k); if (g->gap < 12) g->gap = 12;
    g->group = (int)(24.f * k); if (g->group < 24) g->group = 24;
    g->pad = (int)(24.f * k);
    g->labels = with_labels;
    int page = v->page;

    int top = sw <= 1100 ? 24 : 32;
    int bottom = top;
    int lh_body = (int)((float)g->px_body * 1.5f);
    int lh_label = with_labels ? (int)(18.f * k * 1.45f) + 8 : 0;
    int floor_y = sh - bottom;

    int y = top;
    if (with_head) {
        g->head_lines = est_w(v->head_len > 0 ? v->head_len : 24, g->px_head) > g->cw ? 2 : 1;
        g->head_y = top;
        /* A line is 1.35 em: the face is 1.3 em from the top of its tallest
         * letter to the bottom of its lowest, and a box shorter than that
         * paints nothing at all. */
        g->head_h = (int)((float)g->px_head * 1.35f) * g->head_lines;
        y = top + g->head_h + g->group;
    }

    switch (page) {
    case WP_ASK: {
        int said_lh = (int)((float)g->px_said * 1.35f);
        int box_h = 2 * said_lh + g->pad;
        if (box_h < 2 * g->btn_h) box_h = 2 * g->btn_h;
        int mic_w = v->voice ? box_h : 0;
        if (mic_w && mic_w < est_w(6, g->px_lead) + g->pad) mic_w = est_w(6, g->px_lead) + g->pad;
        if (mic_w > g->cw / 3) mic_w = g->cw / 3;
        int bx_w = g->cw - (mic_w ? mic_w + g->gap : 0);
        int kinds[4] = { WT_ASK, WT_GALLERY, WT_MINE, WT_CLOSE };
        int bh = row_height(g, page, kinds, 4, 0);
        if (y + box_h + 8 + lh_body + g->group + bh > floor_y) {
            g->overflow = 1;
            box_h = g->btn_h + said_lh;
        }
        add_t(g, (rect){ g->gx, y, bx_w, box_h }, WT_BOX, 0);
        if (mic_w) add_t(g, (rect){ g->gx + bx_w + g->gap, y, mic_w, box_h }, WT_MIC, 0);
        y += box_h + 8;
        g->a[A_SUB] = (rect){ g->gx, y, g->cw, lh_body };
        y += lh_body + g->gap;

        int by = floor_y - bh;
        /* The examples take the room between the box and the buttons,
         * one or two columns; an example too long for its column gets a
         * second line rather than an ellipsis. */
        int longest = 0;
        for (int i = 0; i < N_EXAMPLES; i++) {
            int l = (int)strlen(EXAMPLE_TEXT(i));
            if (l > longest) longest = l;
        }
        int need_one = est_w(longest, g->px_body) + 2 * g->pad;
        g->ex_cols = (g->cw >= 2 * need_one + g->gap || g->cw >= 900) ? 2 : 1;
        int colw = (g->cw - (g->ex_cols - 1) * g->gap) / g->ex_cols;
        g->a[A_LABEL1] = (rect){ g->gx, y, g->cw, lh_label };
        int ey = y + lh_label;
        int n = 0, rowy = ey;
        for (int i = 0; i < v->n_examples && i < N_EXAMPLES; i += g->ex_cols) {
            int rh = g->btn_h;
            for (int j = i; j < i + g->ex_cols && j < v->n_examples && j < N_EXAMPLES; j++)
                if (est_w((int)strlen(EXAMPLE_TEXT(j)), g->px_body) > colw - 2 * g->pad)
                    rh = 2 * lh_body + g->gap;
            if (rowy + rh > by - g->group) break;
            for (int j = i; j < i + g->ex_cols && j < v->n_examples && j < N_EXAMPLES; j++)
                add_t(g, (rect){ g->gx + (j - i) * (colw + g->gap), rowy, colw, rh }, WT_EXAMPLE, j);
            n += (v->n_examples - i < g->ex_cols) ? v->n_examples - i : g->ex_cols;
            rowy += rh + g->gap;
        }
        if (n == 0) g->a[A_LABEL1].h = 0;
        g->n_ex = n;
        int brow = n ? rowy - g->gap + g->group : y;
        if (brow > by) brow = by;
        place_row(g, page, brow, kinds, 4, 0);
        break;
    }
    case WP_LISTEN: {
        g->a[A_SUB] = (rect){ g->gx, y, g->cw, 2 * lh_body };
        y += 2 * lh_body + g->group;
        int bw = g->cw < (int)(520.f * k) ? g->cw : (int)(520.f * k);
        int bh = (int)((float)g->btn_h * 1.6f);
        int kinds[1] = { WT_CANCEL };
        int h = row_height(g, page, kinds, 1, 0);
        if (y + bh + g->group + h > floor_y) bh = g->btn_h;
        if (y + bh + g->group + h > floor_y) g->overflow = 1;
        add_t(g, (rect){ g->gx, y, bw, bh }, WT_STOP_LISTEN, 0);
        y += bh + g->group;
        place_row(g, page, y, kinds, 1, 0);
        break;
    }
    case WP_HEARING:
    case WP_WAIT: {
        g->a[A_LABEL1] = (rect){ g->gx, y, g->cw, lh_label };
        y += lh_label;
        int said_lh = (int)((float)g->px_said * 1.35f);
        int lines = est_w(v->said_len, g->px_said) > g->cw ? 2 : 1;
        if (page == WP_HEARING) lines = 0;
        g->a[A_TEXT1] = (rect){ g->gx, y, g->cw, said_lh * lines };
        y += said_lh * lines + (lines ? g->group : 0);
        g->a[A_BAR] = (rect){ g->gx, y, (int)(180.f * k), (int)(8.f * k) < 6 ? 6 : (int)(8.f * k) };
        y += g->a[A_BAR].h + g->gap;
        int nl = (est_w(70, g->px_body) + g->cw - 1) / g->cw;
        if (nl < 1) nl = 1;
        if (nl > 3) nl = 3;
        g->a[A_NOTE] = (rect){ g->gx, y, g->cw, lh_body * nl };
        y += lh_body * nl + g->group;
        int kinds[1] = { WT_CANCEL };
        int h = row_height(g, page, kinds, 1, 0);
        if (y + h > floor_y) g->overflow = 1;
        place_row(g, page, y, kinds, 1, 0);
        break;
    }
    case WP_CONFIRM: {
        /* You said (press to change) -- what will happen -- Yes / No --
         * Show me / Read it to me. Top down while it fits. The sentence
         * box is what gives: the painter fits the sentence to it, a
         * size smaller at a time, never cut off and never below 18 px. */
        int kinds1[2] = { WT_YES, WT_NO };
        int kinds2[2] = { WT_SHOW, WT_READ };
        int h1 = row_height(g, page, kinds1, 2, 1);
        int h2 = row_height(g, page, kinds2, 2, 0);
        int said_lh = (int)((float)g->px_said * 1.35f);
        int said_lines = est_w(v->said_len, g->px_said) >
                         g->cw - est_w(6, g->px_lead) - 3 * g->pad ? 2 : 1;
        if (!with_labels) said_lines = 1;
        int said_pad = g->pad / 2 < 12 ? 12 : g->pad / 2;
        int said_h = said_lh * said_lines + 2 * said_pad;
        if (said_h < g->btn_h) said_h = g->btn_h;
        int dest_h = v->has_dest ? lh_label + (int)((float)g->px_head * 1.25f) +
                                   (v->has_dest > 1 ? lh_label : 0) : 0;
        /* A line of the device's sentence is 1.15 x the face's own 1.3 em: the
         * painter's pitch (draw_rich, and draw_wrapped at 1.1), not the bare
         * 1.3 em the geometry used to assume. */
        int sl = (int)((float)g->px_sent * 1.5f);
        int sent_min = (int)(18.f * 1.3f) * 3;       /* three lines at the floor */
        if (strict) {
            int cwid = g->cw > 1 ? g->cw : 1;
            int lines_needed = est_w(v->sent_len, g->px_sent) / cwid + 2;
            if (lines_needed > 4) lines_needed = 4;
            sent_min = lines_needed * sl;
        }
        int rows = h1 + g->gap + h2;
        int fixed = lh_label + said_h + g->group + lh_label + g->group + dest_h + rows;
        int room = floor_y - y - fixed;
        if (room < sent_min) { g->overflow = 1; room = sent_min; }
        int want_lines = est_w(v->sent_len, g->px_sent) / (g->cw > 1 ? g->cw : 1) + 2;
        if (want_lines > 5) want_lines = 5;
        int sent_h = sl * want_lines < room ? sl * want_lines : room;
        g->a[A_LABEL1] = (rect){ g->gx, y, g->cw, lh_label };
        y += lh_label;
        add_t(g, (rect){ g->gx, y, g->cw, said_h }, WT_SAID, 0);
        y += said_h + g->group;
        g->a[A_LABEL2] = (rect){ g->gx, y, g->cw, lh_label };
        y += lh_label;
        g->a[A_TEXT2] = (rect){ g->gx, y, g->cw, sent_h };
        y += sent_h;
        if (dest_h) { g->a[A_DEST] = (rect){ g->gx, y + g->gap, g->cw, dest_h }; y += dest_h + g->gap; }
        y += g->group;
        y = place_row(g, page, y, kinds1, 2, 1) + g->gap;
        place_row(g, page, y, kinds2, 2, 0);
        break;
    }
    case WP_CANNOT:
    case WP_ANSWER:
    case WP_DONE:
    case WP_ERROR:
    case WP_CONSENT: {
        int kinds[6], nk = 0;
        if (page == WP_CANNOT && v->has_offer) {
            /* "I can't do that, but I can ...": her answer to what the engine can do. */
            kinds[nk++] = WT_YES; kinds[nk++] = WT_NO; kinds[nk++] = WT_CLOSE;
        } else if (page == WP_CANNOT) {
            kinds[nk++] = WT_AGAIN; kinds[nk++] = WT_GALLERY; kinds[nk++] = WT_CLOSE;
        }
        if (page == WP_ANSWER) {
            if (v->has_help) kinds[nk++] = WT_HELP;
            if (v->has_offer) { kinds[nk++] = WT_YES; kinds[nk++] = WT_NO; }
            kinds[nk++] = WT_CLOSE;
        }
        if (page == WP_DONE) {
            kinds[nk++] = WT_CLOSE;
            kinds[nk++] = WT_NEW;
            if (v->now_wish) kinds[nk++] = WT_UNDO;
        }
        if (page == WP_ERROR) {
            if (v->can_retry) kinds[nk++] = WT_AGAIN;
            /* When the service is out of reach or resting for the day, the
             * list is still there: it needs no internet (docs/AI.md §1.6). */
            kinds[nk++] = WT_GALLERY;
            kinds[nk++] = WT_CLOSE;
        }
        if (page == WP_CONSENT) { kinds[nk++] = WT_CONSENT_YES; kinds[nk++] = WT_CONSENT_NO; }
        int eq = (page == WP_CONSENT) || (page == WP_CANNOT && v->has_offer);
        int h = row_height(g, page, kinds, nk, eq);
        int room = floor_y - h - g->group - y;
        /* A line of the device's sentence is 1.15 x the face's own 1.3 em: the
         * painter's pitch (draw_rich, and draw_wrapped at 1.1), not the bare
         * 1.3 em the geometry used to assume. */
        int sl = (int)((float)g->px_sent * 1.5f);
        int want = page == WP_CONSENT ? lh_body * 16
                 : (page == WP_ANSWER || page == WP_CANNOT) && v->has_offer ? sl * 2 + lh_label + sl * 3
                 : sl * 3 + g->gap + lh_body * 2;
        int least = page == WP_CONSENT ? lh_body * 6 : sl * 2;
        /* With an offered sentence under the first line, room for it at the
         * smallest size it is ever drawn: three lines of 18 px. */
        if ((page == WP_ANSWER || page == WP_CANNOT) && v->has_offer)
            least = sl + g->group + lh_label + 3 * (int)(18.f * 1.5f);
        if (room < least) { g->overflow = 1; room = least; }
        g->a[A_TEXT2] = (rect){ g->gx, y, g->cw, room < want ? room : want };
        y += g->a[A_TEXT2].h + g->group;
        place_row(g, page, y, kinds, nk, eq);
        break;
    }
    case WP_TODAY: {
        int sub_n = est_w(v->sub_len, g->px_body) > g->cw ? 2 : 1;
        g->a[A_SUB] = (rect){ g->gx, y, g->cw, lh_body * sub_n };
        y += lh_body * sub_n + g->gap;
        int kk[4], nk = 0;
        if (v->more) kk[nk++] = WT_MORE;
        kk[nk++] = WT_STOP_ALL;
        kk[nk++] = WT_MINE;
        kk[nk++] = WT_CLOSE;
        int h = row_height(g, page, kk, nk, 0);
        int by = floor_y - h - g->group;
        int row_h = lh_body + g->gap / 2;
        int fit = (by - y) / row_h;
        if (fit < (strict && v->n_rows - v->first_item > 2 ? 3 : 1)) { g->overflow = 1; if (fit < 1) fit = 1; }
        int n = v->n_rows - v->first_item;
        if (n > fit) n = fit;
        if (n < 0) n = 0;
        g->a[A_TEXT1] = (rect){ g->gx, y, g->cw, n * row_h };
        g->mine_n = n; g->mine_row_h = row_h;
        place_row(g, page, floor_y - h, kk, nk, 0);
        break;
    }
    case WP_PICK: {
        /* A page of choices: lines (the templates), words, or pictures. As
         * many as fit, then Show more; Go back is always there. */
        g->a[A_SUB] = (rect){ g->gx, y, g->cw, lh_body };
        y += lh_body + g->gap;
        int kk[3], nk = 0;
        if (v->more) kk[nk++] = WT_MORE;
        kk[nk++] = WT_BACK;
        kk[nk++] = WT_CLOSE;
        int h = row_height(g, page, kk, nk, 0);
        int by = floor_y - h - g->group;
        int cols, cell_w, cell_h;
        if (v->pick_mode == 2) {
            cell_w = (int)(132.f * k); cell_h = (int)(104.f * k);
            if (cell_h < g->btn_h) cell_h = g->btn_h;
            cols = (g->cw + g->gap) / (cell_w + g->gap);
            if (cols < 2) cols = 2;
            cell_w = (g->cw - (cols - 1) * g->gap) / cols;
        } else if (v->pick_mode == 1) {
            cols = est_w(26, g->px_body) + 2 * g->pad + g->gap <= g->cw / 3 ? 3
                 : est_w(26, g->px_body) + 2 * g->pad + g->gap <= g->cw / 2 ? 2 : 1;
            cell_w = (g->cw - (cols - 1) * g->gap) / cols;
            cell_h = g->btn_h;
        } else {
            cols = 1;
            cell_w = g->cw;
            cell_h = g->btn_h;
        }
        g->ex_cols = cols;
        int rowh = cell_h;
        /* A label too long for its cell gets two lines, never an ellipsis. */
        if (v->pick_mode != 2 && est_w(v->pick_long, g->px_body) > cell_w - 2 * g->pad)
            rowh = 2 * lh_body + g->gap;
        int shown = 0, ry = y;
        for (int i = v->first_item; i < v->n_items; i += cols) {
            if (ry + rowh > by) break;
            for (int j = i; j < i + cols && j < v->n_items; j++) {
                add_t(g, (rect){ g->gx + (j - i) * (cell_w + g->gap), ry, cell_w, rowh }, WT_ITEM, j);
                shown++;
            }
            ry += rowh + g->gap;
        }
        int want_shown = v->n_items - v->first_item;
        if (want_shown > 3) want_shown = 3;
        if (shown < want_shown && (strict || shown == 0)) g->overflow = 1;
        g->n_ex = shown;
        g->mine_n = shown;
        place_row(g, page, floor_y - h, kk, nk, 0);
        break;
    }
    case WP_MINE: {
        int sub_n = est_w(v->sub_len, g->px_body) > g->cw ? 2 : 1;
        g->a[A_SUB] = (rect){ g->gx, y, g->cw, lh_body * sub_n };
        y += lh_body * sub_n + g->gap;
        int kinds[5] = { WT_NEW, WT_MORE, WT_ALL_OFF, WT_RESTORE, WT_CLOSE };
        int nk = 0, kk[5];
        for (int i = 0; i < 5; i++)
            if (kinds[i] != WT_MORE || v->more) kk[nk++] = kinds[i];
        int h = row_height(g, page, kk, nk, 0);
        int by = floor_y - h;
        int sw_w = (int)(112.f * k), rm_w = est_w(6, g->px_lead) + 2 * g->pad;
        /* "Remove it" and "Keep it", asked when she presses Remove: wider
         * than the switch and Remove they replace, and each as wide as its
         * own name. */
        int yes_w = est_w(9, g->px_lead) + 2 * g->pad, no_w = est_w(7, g->px_lead) + 2 * g->pad;
        int norm_w = sw_w + g->group + rm_w, ask_w = yes_w + g->group + no_w;
        int ctl_w = norm_w > ask_w ? norm_w : ask_w;
        int stacked = (g->cw - ctl_w - g->group) < (int)(0.5f * (float)g->cw);
        /* Two lines of each wish, three or four when the longest on the page
         * needs them: the sentence is never cut off. */
        int tw = stacked ? g->cw : g->cw - ctl_w - g->group;
        int need = (est_w(v->mine_long, g->px_body) + (tw > 1 ? tw : 1) - 1) / (tw > 1 ? tw : 1);
        int text_h = (need < 2 ? 2 : need > 4 ? 4 : need) * lh_body;
        int row_h = stacked ? text_h + 8 + g->btn_h + g->group
                            : (text_h > g->btn_h ? text_h : g->btn_h) + g->group;
        int fit = (by - y) / row_h;
        if (fit < (strict && v->n_rows > 1 ? 2 : 1) && v->n_rows > 0) { g->overflow = 1; fit = 1; }
        int n = v->n_rows < fit ? v->n_rows : fit;
        for (int i = 0; i < n; i++) {
            int ry = y + i * row_h;
            int cy = stacked ? ry + text_h + 8 : ry + (row_h - g->group - g->btn_h) / 2;
            if (v->removing == i) {
                int cx = stacked ? g->gx : g->gx + g->cw - ask_w;
                add_t(g, (rect){ cx, cy, yes_w, g->btn_h }, WT_REMOVE_YES, i);
                add_t(g, (rect){ cx + yes_w + g->group, cy, no_w, g->btn_h }, WT_REMOVE_NO, i);
            } else {
                int cx = stacked ? g->gx : g->gx + g->cw - norm_w;
                add_t(g, (rect){ cx, cy, sw_w, g->btn_h }, WT_SWITCH, i);
                add_t(g, (rect){ cx + sw_w + g->group, cy, rm_w, g->btn_h }, WT_REMOVE, i);
            }
        }
        g->a[A_TEXT1] = (rect){ g->gx, y, stacked ? g->cw : g->cw - ctl_w - g->group, row_h };
        g->mine_stacked = stacked; g->mine_n = n; g->mine_row_h = row_h; g->mine_text_h = text_h;
        int ry = y + n * row_h;
        if (ry + h > floor_y) g->overflow = 1;
        place_row(g, page, ry, kk, nk, 0);
        break;
    }
    }
    /* Anything below the floor is an overflow too: a button off the
     * bottom of the screen is a button that does not exist. */
    for (int i = 0; i < g->n; i++)
        if (g->t[i].r.y + g->t[i].r.h > floor_y + bottom) g->overflow = 1;
}

/* The full page if it fits; without the heading if not; then without
 * the small labels too. If even that does not fit -- her largest text
 * on the smallest screen -- the panel's words step down a tenth at a
 * time, never below the theme's own size, until every button is on the
 * screen. A smaller word she can press beats a bigger one she cannot.
 * The answers are never what gives way. */
static void layout(const shell_ctx *c, int sw, int sh, const wish_view *v, wgeom *g)
{
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
    for (;;) {
        for (int mode = 0; mode < 3; mode++) {
            layout_page(c, sw, sh, v, g, k, mode == 0, mode < 2);
            if (!g->overflow) return;
        }
        if (k <= 1.f) return;
        k -= 0.1f;
        if (k < 1.f) k = 1.f;
    }
}

int wish_targets(const shell_ctx *c, int sw, int sh, const wish_view *v,
                 wish_target *out, int max)
{
    wgeom g;
    layout(c, sw, sh, v, &g);
    int n = g.n < max ? g.n : max;
    memcpy(out, g.t, (size_t)n * sizeof *out);
    return n;
}

static int rows_shown(const shell_ctx *c, const wish_view *v)
{
    wgeom g;
    layout(c, c->screen_w, c->screen_h, v, &g);
    return g.mine_n;
}

/* ── the gallery's page (src/aurshell/wishgallery.c has the tables) ── */

/* How many choices this stage has, how to show them, and the longest. */
static int pick_items(int *mode, int *longest)
{
    int n, lo = 0;
    if (W.g_stage < 0) {
        n = wg_count();
        *mode = 0;
        for (int i = 0; i < n; i++) { int l = (int)strlen(wg_get(i)->pick); if (l > lo) lo = l; }
    } else {
        const wg_tmpl *t = wg_get(W.g_tmpl);
        const wg_slot *s = t ? wg_slot_get(t->slot[W.g_stage]) : NULL;
        if (!s) { *mode = 0; *longest = 0; return 0; }
        n = s->n;
        *mode = s->pic ? 2 : 1;
        for (int i = 0; i < n; i++) { int l = (int)strlen(s->opts[i].label); if (l > lo) lo = l; }
    }
    *longest = lo;
    return n;
}

static const char *pick_label(int i, const char **emoji)
{
    *emoji = NULL;
    if (W.g_stage < 0) { const wg_tmpl *t = wg_get(i); return t ? t->pick : ""; }
    const wg_tmpl *t = wg_get(W.g_tmpl);
    const wg_slot *s = t ? wg_slot_get(t->slot[W.g_stage]) : NULL;
    if (!s || i < 0 || i >= s->n) return "";
    if (s->pic) *emoji = s->opts[i].value;
    return s->opts[i].label;
}

static const char *pick_head(void)
{
    if (W.g_stage < 0) return "Pick a wish";
    const wg_tmpl *t = wg_get(W.g_tmpl);
    const wg_slot *s = t ? wg_slot_get(t->slot[W.g_stage]) : NULL;
    return s ? s->ask : "Pick a wish";
}

/* The line under the heading of My wishes and of What my wishes did today:
 * one function for the painter and for the geometry, so the room made for it
 * is the room it takes. */
static const char *sub_text(int page)
{
    if (page == WP_TODAY)
        return W.n_today ? "Each time one of your wishes did something today."
                         : "None of your wishes has done anything today.";
    return W.n_rows ? (W.all_off ? "All your wishes are off. Press Turn my wishes back on to start them again."
                                 : "Each wish keeps working until you turn it off.")
                    : "You have no wishes yet. Press Make a new wish to start.";
}

static const char *head_of(void);

static wish_view view_now(const shell_ctx *c)
{
    wish_view v;
    memset(&v, 0, sizeof v);
    v.page = W.page;
    v.n_examples = W.shown_once ? 3 : N_EXAMPLES;
    v.voice = 1;
    v.has_dest = (W.dest[0] != 0) + (W.dest[0] != 0 && W.dest_host[0] != 0);
    v.has_help = W.help_page[0] != 0;
    v.has_offer = ((W.page == WP_ANSWER || W.page == WP_CANNOT) && W.pending[0]);
    v.now_wish = (W.page == WP_DONE && W.undo_until != 0);
    v.can_retry = ERRS[W.err].retry;
    v.removing = W.removing;
    v.said_len = (int)strlen(W.said);
    v.sent_len = (int)strlen(W.sentence);
    v.head_len = (int)strlen(head_of());
    if (W.page == WP_MINE || W.page == WP_TODAY) v.sub_len = (int)strlen(sub_text(W.page));
    if (W.page == WP_MINE) {
        v.n_rows = W.n_rows - W.first_row;
        if (v.n_rows < 0) v.n_rows = 0;
        for (int i = W.first_row; i < W.n_rows; i++) {
            int l = (int)strlen(W.rows[i].sentence) + (W.removing >= 0 ? 18 : 0);
            if (l > v.mine_long) v.mine_long = l;
        }
        /* Whether there are more than fit is itself geometry: ask it,
         * with the Show more button in, since that takes room too. */
        v.more = 1;
        int shown = rows_shown(c, &v);
        v.more = (shown < v.n_rows) || W.first_row > 0;
    }
    if (W.page == WP_TODAY) {
        v.n_rows = W.n_today;
        v.first_item = W.first_row < W.n_today ? W.first_row : 0;
        v.more = 1;
        int shown = rows_shown(c, &v);
        v.more = (v.first_item + shown < W.n_today) || v.first_item > 0;
    }
    if (W.page == WP_PICK) {
        v.n_items = pick_items(&v.pick_mode, &v.pick_long);
        v.first_item = W.g_first < v.n_items ? W.g_first : 0;
        v.more = 1;
        int shown = rows_shown(c, &v);
        v.more = (v.first_item + shown < v.n_items) || v.first_item > 0;
    }
    return v;
}

/* ── words on buttons (docs/DESIGN-DIRECTION.md §4: a verb that names
 * the result; never a bare OK, Yes, No or Close) ───────────────────── */

static const char *label_of(int kind, int page)
{
    switch (kind) {
    case WT_MIC:         return "Say it";
    case WT_ASK:         return "Ask";
    case WT_CLOSE:       return "Close Wishes";
    case WT_MINE:        return "My wishes";
    case WT_STOP_LISTEN: return "I've finished";
    case WT_CANCEL:      return page == WP_WAIT ? "Stop asking" : "Stop listening";
    case WT_YES:         return "Yes, do that";
    case WT_NO:          return "No, thanks";
    case WT_SHOW:        return "Show me";
    case WT_READ:        return "Read it to me";
    case WT_AGAIN:       return page == WP_ERROR ? "Try again" : "Try something else";
    case WT_HELP:        return "Show me how";
    case WT_UNDO:        return "Undo my last wish";
    case WT_CONSENT_YES: return "Yes, turn on Wishes";
    case WT_CONSENT_NO:  return "No, keep Wishes off";
    case WT_REMOVE:      return "Remove";
    case WT_REMOVE_YES:  return "Remove it";
    case WT_REMOVE_NO:   return "Keep it";
    case WT_ALL_OFF:     return W.all_off ? "Turn my wishes back on" : "Turn off all wishes";
    case WT_RESTORE:     return "Put everything back how it was";
    case WT_MORE:        return "Show more wishes";
    case WT_NEW:         return "Make a new wish";
    case WT_GALLERY:     return page == WP_ASK ? "Pick from a list" : "Pick a wish from a list";
    case WT_BACK:        return "Go back";
    case WT_STOP_ALL:    return "Stop all wishes";
    case WT_SAID:        return "Change";
    }
    return "";
}

static int primary(int kind)
{
    return kind == WT_ASK || kind == WT_STOP_LISTEN || kind == WT_CONSENT_YES ||
           kind == WT_HELP || kind == WT_REMOVE_YES || kind == WT_AGAIN;
}

static const char *head_of(void)
{
    switch (W.page) {
    case WP_CONSENT: return "Before your first wish";
    case WP_ASK:     return "What would you like your computer to do?";
    case WP_LISTEN:  return "I'm listening.";
    case WP_HEARING: return "Writing down what you said.";
    case WP_WAIT:    return "Thinking about your wish.";
    case WP_CONFIRM: return "Is this what you want?";
    case WP_CANNOT:  return "I can't do that yet.";
    case WP_ANSWER:  return "Here is how.";
    case WP_DONE:    return W.undo_until ? "Done." : "Your wish is on.";
    case WP_ERROR:   return ERRS[W.err].head;
    case WP_MINE:    return "My wishes";
    case WP_PICK:    return pick_head();
    case WP_TODAY:   return "What my wishes did today";
    }
    return "";
}

/* ── painting ────────────────────────────────────────────────────── */

/* The panel's type, at the sizes docs/DESIGN-DIRECTION.md §3 sets --
 * Display 40, Title 32, Lead 22 bold, Body 20, Small 18 bold, and the
 * device's sentence at 26 in both weights so the words that matter can
 * be bold without changing size -- opened from the theme's own font
 * files and scaled with her text size. Where the theme names a font
 * the shell has to look up, each falls back to the shell's own face. */
static wish_type P;
static char P_key[220];

static void fonts_free(void)
{
    font **all[] = { &P.head, &P.title, &P.lead, &P.body, &P.label,
                     &P.sent, &P.sentb, &P.said };
    for (int i = 0; i < 8; i++) if (*all[i]) { font_free(*all[i]); *all[i] = NULL; }
}

static font *open_abs(const char *path, float px)
{
    return (path && path[0] == '/') ? font_load(path, px) : NULL;
}

const wish_type *wish_type_for(const shell_ctx *c, const shell_fonts *f, float k)
{
    static wish_type out;
    if (k < 0.5f) k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
    const char *sans = theme_str(&c->theme, "font_sans", "");
    const char *disp = theme_str(&c->theme, "font_display", sans);
    const char *text = theme_str(&c->theme, "font_text", sans);
    const char *bold = theme_str(&c->theme, "font_text_bold", text);
    char key[220];
    snprintf(key, sizeof key, "%.2f|%s|%s|%s", (double)k, disp, text, bold);
    if (strcmp(key, P_key)) {
        fonts_free();
        snprintf(P_key, sizeof P_key, "%s", key);
        P.head  = open_abs(disp, 40.f * k);
        P.title = open_abs(disp, 32.f * k);
        P.lead  = open_abs(bold, 22.f * k);
        P.body  = open_abs(text, 20.f * k);
        P.label = open_abs(bold, 18.f * k);
        P.sent  = open_abs(text, 26.f * k);
        P.sentb = open_abs(bold, 26.f * k);
        P.said  = open_abs(text, 24.f * k);
    }
    font *small = f ? (f->small ? f->small : f->mid) : NULL;
    font *mid = f ? (f->mid ? f->mid : small) : NULL;
    out.head  = P.head  ? P.head  : (f ? (f->huge ? f->huge : f->big) : NULL);
    out.title = P.title ? P.title : (f ? (f->big ? f->big : out.head) : NULL);
    out.lead  = P.lead  ? P.lead  : mid;
    out.body  = P.body  ? P.body  : small;
    out.label = P.label ? P.label : (f ? (f->label ? f->label : mid) : NULL);
    out.sent  = P.sent  ? P.sent  : out.body;
    out.sentb = P.sentb ? P.sentb : out.lead;
    out.said  = P.said  ? P.said  : out.body;
    return &out;
}


/* One line of `t` from *pos that fits `w`, broken at a space where it
 * can be; returns its length in bytes and moves *pos past it. */
static size_t fit_line(font *f, const char *t, size_t *pos, float w)
{
    const char *s = t + *pos;
    while (*s == ' ') { s++; (*pos)++; }
    size_t len = strlen(s), best = 0;
    char buf[SENT_MAX + 64];
    for (size_t j = 0; j <= len; j++) {
        if (s[j] != ' ' && s[j] != 0) continue;
        if (j >= sizeof buf) break;
        memcpy(buf, s, j); buf[j] = 0;
        if (shell_text_w(f, buf) > w) {
            if (!best) best = j;          /* one word wider than the line */
            break;
        }
        best = j;
        if (!s[j]) break;
    }
    if (!best) best = len < sizeof buf - 1 ? len : sizeof buf - 1;
    *pos += best;
    return best;
}

/* How many lines `t` takes at width w. */
static int count_lines(font *f, const char *t, float w)
{
    if (!f || !t || !*t) return 0;
    size_t pos = 0, n = strlen(t);
    int lines = 0;
    while (pos < n) {
        size_t before = pos;
        fit_line(f, t, &pos, w);
        lines++;
        if (pos == before) break;
        while (pos < n && t[pos] == ' ') pos++;
    }
    return lines;
}

/* Words cut off while painting: the geometry exists so that there are none,
 * and tools/wishpainttest.sh paints every page at every size and fails if
 * there is one. Read and cleared by wish_clipped(). */
static int clip_n;
static char clip_what[40];
#define CLIP(text) (clip_n++, snprintf(clip_what, sizeof clip_what, "%.39s", (text)))
int wish_clipped(void) { int n = clip_n; clip_n = 0; return n; }
const char *wish_clip_what(void) { return clip_what; }       /* the start of the last text cut off */

/* Plain text wrapped into a box. Returns the y below the last line. If
 * the box is too short the last line that fits is ended with an
 * ellipsis -- a last resort the geometry exists to avoid. */
static int draw_wrapped(surface *s, font *f, rect box, const char *t,
                        uint32_t col, float lh)
{
    if (!f || !t || !*t) return box.y;
    size_t pos = 0, n = strlen(t);
    float y = (float)box.y + font_ascent(f);
    while (pos < n) {
        if (box.h > 0 && y + font_descent(f) > (float)(box.y + box.h) + 1.f) { CLIP(t); break; }
        size_t at = pos;
        size_t l = fit_line(f, t, &pos, (float)box.w);
        while (at < n && t[at] == ' ') at++;
        char line[SENT_MAX + 64];
        if (l > sizeof line - 8) l = sizeof line - 8;
        memcpy(line, t + at, l); line[l] = 0;
        int last = box.h > 0 && (y + lh + font_descent(f) > (float)(box.y + box.h) + 1.f);
        if (last && pos < n) {
            CLIP(t);
            strcat(line, " \xE2\x80\xA6");
            shell_text_elided(s, f, (float)box.x, y, (float)box.w, line, col, 1.f);
        } else {
            shell_text(s, f, (float)box.x, y, line, col, 1.f);
        }
        y += lh;
        while (pos < n && t[pos] == ' ') pos++;
    }
    return (int)(y - font_ascent(f));
}

/* The device's sentence: `*` toggles bold (docs/AI.md §3.7). Wrapped
 * word by word across the two faces. Returns 1 if it all fitted. */
static int rich_end_y;          /* the y under the last line draw_rich drew */
static int draw_rich(surface *s, font *r, font *b, rect box, const char *t,
                     uint32_t col, int measure_only)
{
    rich_end_y = box.y;
    if (!r || !t) return 1;
    if (!b) b = r;
    float lh = font_line_height(r) * 1.15f;
    float x = (float)box.x, y = (float)box.y + font_ascent(r);
    float space = shell_text_w(r, " ");
    int bold = 0, fits = 1;
    char word[128];
    const char *p = t;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        /* A word may hold a toggle; each run of it is drawn in its own
         * face, and the word is kept together on one line. */
        const char *q = p;
        float ww = 0.f;
        int b2 = bold;
        size_t wl = 0;
        while (*q && *q != ' ') {
            if (*q == '*') {
                if (wl) { word[wl] = 0; ww += shell_text_w(b2 ? b : r, word); wl = 0; }
                b2 = !b2; q++; continue;
            }
            if (wl + 1 < sizeof word) word[wl++] = *q;
            q++;
        }
        if (wl) { word[wl] = 0; ww += shell_text_w(b2 ? b : r, word); }
        if (x > (float)box.x && x + ww > (float)(box.x + box.w)) {
            x = (float)box.x;
            y += lh;
        }
        if (y + font_descent(r) > (float)(box.y + box.h) + 1.f) { fits = 0; break; }
        wl = 0;
        for (q = p; *q && *q != ' '; q++) {
            if (*q == '*') {
                if (wl) {
                    word[wl] = 0;
                    if (!measure_only) shell_text(s, bold ? b : r, x, y, word, col, 1.f);
                    x += shell_text_w(bold ? b : r, word);
                    wl = 0;
                }
                bold = !bold;
                continue;
            }
            if (wl + 1 < sizeof word) word[wl++] = *q;
        }
        if (wl) {
            word[wl] = 0;
            if (!measure_only) shell_text(s, bold ? b : r, x, y, word, col, 1.f);
            x += shell_text_w(bold ? b : r, word);
        }
        x += space;
        p = q;
        rich_end_y = (int)(y + font_descent(r));
    }
    return fits;
}

/* Her own words (plain text) at the size given if they fit in the box; a size
 * smaller at a time if not, never below 18 px. Returns the face to draw them
 * in; a smaller one is kept between calls. */
static font *plain_fit(const shell_ctx *c, font *f0, float px0, rect box, const char *text, float lh_mul)
{
    static font *pf;
    static char pkey[SENT_MAX + 48];
    if (!f0 || !text) return f0;
    float lh0 = font_line_height(f0) * lh_mul;
    if ((float)count_lines(f0, text, (float)box.w) * lh0 <= (float)box.h) return f0;
    char key[SENT_MAX + 48];
    snprintf(key, sizeof key, "%d|%d|%.1f|%.2f|%s", box.w, box.h, (double)px0, (double)lh_mul, text);
    if (pf && !strcmp(key, pkey)) return pf;
    const char *tp = theme_str(&c->theme, "font_text", "");
    font *best = NULL;
    for (float px = px0 - 2.f; px >= 18.f; px -= 2.f) {
        font *nf = open_abs(tp, px);
        if (!nf) break;
        if (best) font_free(best);
        best = nf;
        if ((float)count_lines(best, text, (float)box.w) * font_line_height(best) * lh_mul <= (float)box.h) break;
    }
    if (!best) return f0;
    if (pf) font_free(pf);
    pf = best;
    snprintf(pkey, sizeof pkey, "%s", key);
    return pf;
}

/* The device's sentence in its box at full size if all of it fits there; a
 * size smaller at a time if not, never below the 18 px floor and never cut
 * off. The fitted pair is kept between frames, so this is worked out once
 * per sentence, not once per frame. Leaves rich_end_y under the last line. */
static void draw_fitted(surface *s, const shell_ctx *c, font *r0, font *b0, float k,
                        rect box, const char *text, uint32_t col)
{
    static font *fr, *fb;
    static char fkey[SENT_MAX + 48];
    char key[SENT_MAX + 48];
    snprintf(key, sizeof key, "%d|%d|%.2f|%s", box.w, box.h, (double)k, text);
    font *r = r0, *b = b0;
    if (!strcmp(key, fkey) && fr && fb) { r = fr; b = fb; }
    else if (!draw_rich(s, r, b, box, text, col, 1)) {
        if (fr) font_free(fr);
        if (fb) font_free(fb);
        fr = fb = NULL;
        const char *tp = theme_str(&c->theme, "font_text", "");
        const char *bp = theme_str(&c->theme, "font_text_bold", tp);
        for (float px = 26.f * k - 2.5f; px >= 18.f; px -= 2.5f) {
            font *nr = open_abs(tp, px), *nb = open_abs(bp, px);
            if (!nr || !nb) { if (nr) font_free(nr); if (nb) font_free(nb); break; }
            if (fr) font_free(fr);
            if (fb) font_free(fb);
            fr = nr; fb = nb;
            r = fr; b = fb;
            if (draw_rich(s, r, b, box, text, col, 1)) break;
        }
        snprintf(fkey, sizeof fkey, "%s", key);
    } else {
        snprintf(fkey, sizeof fkey, "%s", key);
        if (fr) font_free(fr);
        if (fb) font_free(fb);
        fr = fb = NULL;
    }
    if (!draw_rich(s, r, b, box, text, col, 0)) CLIP(text);
}

/* Pressable: radius 8, a 2 px rule, or the go fill for the one primary.
 * Hover darkens the border to ink and does nothing else; the keyboard's
 * place is an ink ring 3 px outside (docs/DESIGN-DIRECTION.md §3). */
static void pressable(surface *s, const shell_ctx *c, rect r, int prim, int hot, int sel)
{
    float rad = 8.f;
    draw_round_rect(s, r, corners_all(rad), prim ? (hot ? c->accent_warm : c->accent)
                                                 : c->surface_c, 1.f);
    if (!prim)
        draw_round_rect_border(s, r, corners_all(rad), 2.f, hot ? c->fg : c->overlay, 1.f);
    if (sel)
        draw_round_rect_border(s, (rect){ r.x - 5, r.y - 5, r.w + 10, r.h + 10 },
                               corners_all(rad + 3.f), 3.f, c->fg, 1.f);
}

static void paint_button(surface *s, const shell_ctx *c, rect r, const char *label,
                         int prim, int hot, int sel, font *f)
{
    pressable(s, c, r, prim, hot, sel);
    if (!f) return;
    float w = shell_text_w(f, label);
    if (w > (float)r.w - 24.f) CLIP(label);          /* a button whose name is cut off */
    float x = (float)r.x + ((float)r.w - w) / 2.f;
    if (x < (float)r.x + 12.f) x = (float)r.x + 12.f;
    shell_text_elided(s, f, x, shell_baseline(f, (float)r.y, (float)r.h),
                      (float)r.w - 24.f, label, prim ? c->surface_c : c->fg, 1.f);
}

/* A microphone, from primitives on the icon grid: a capsule, a cradle,
 * a stem and a foot, 2-unit strokes with round ends. */
static void mic_glyph(surface *s, float cx, float cy, float sz, uint32_t ink)
{
    float u = sz / 24.f;
    float t = 2.f * u; if (t < 2.f) t = 2.f;
    draw_round_rect(s, (rect){ (int)(cx - 4.5f * u), (int)(cy - 11.f * u),
                               (int)(9.f * u), (int)(14.f * u) },
                    corners_all(4.5f * u), ink, 1.f);
    float rr = 7.5f * u, by = cy - 1.f * u;
    float px = cx - rr, py = by;
    for (int i = 1; i <= 18; i++) {
        float ang = 3.14159265f * (float)i / 18.f;
        float nx = cx - rr * cosf(ang), ny = by + rr * sinf(ang);
        draw_line(s, px, py, nx, ny, t, ink, 1.f);
        px = nx; py = ny;
    }
    draw_line(s, cx, by + rr, cx, cy + 10.f * u, t, ink, 1.f);
    draw_line(s, cx - 5.f * u, cy + 10.f * u, cx + 5.f * u, cy + 10.f * u, t, ink, 1.f);
}

void wish_paint(shell_ctx *c, surface *s, shell_fonts *f)
{
    if (!c->wish_open) return;
    int sw = s->w, sh = c->screen_h;
    if (sh <= 0 || sh > s->h) sh = s->h;
    wish_view v = view_now(c);
    wgeom g;
    layout(c, sw, sh, &v, &g);
    const wish_type *ty = wish_type_for(c, f, g.k);

    draw_rect(s, (rect){ 0, 0, sw, sh }, c->bg, 1.f);

    font *head  = ty->head;
    font *body  = ty->body;
    font *lead  = ty->lead;
    font *label = ty->label;
    font *sent  = ty->sent, *sentb = ty->sentb;
    font *said  = ty->said;
    float lh_body = (float)g.px_body * 1.5f;

    /* The heading, in the display face. Where it would need more lines
     * than the screen gave it, it steps down to the title size. */
    if (head && g.head_h > 0) {
        const char *h = head_of();
        font *hf = head;
        int room = g.head_lines;
        if (count_lines(hf, h, (float)g.cw) > room && ty->title) hf = ty->title;
        float hl = (float)g.head_h / (float)room;
        rect hb = { g.gx, g.head_y, g.cw, g.head_h + 4 };
        draw_wrapped(s, hf, hb, h, c->fg_hi, hl);
    }

    for (int i = 0; i < g.n; i++) {
        wish_target *t = &g.t[i];
        int hot = (W.hover == i), sel = (W.sel == i);
        switch (t->kind) {
        case WT_BOX: {
            /* A field: pressable, so the 2 px rule; while it has the
             * keyboard, the rule is ink. */
            draw_round_rect(s, t->r, corners_all(8.f), c->surface_c, 1.f);
            draw_round_rect_border(s, t->r, corners_all(8.f), 2.f,
                                   (W.box_focus || hot) ? c->fg : c->overlay, 1.f);
            rect in = { t->r.x + g.pad, t->r.y + g.pad / 2,
                        t->r.w - 2 * g.pad, t->r.h - g.pad };
            float lh = font_line_height(said);
            if (W.text[0])
                draw_wrapped(s, said, in, W.text, c->fg_hi, lh);
            else
                draw_wrapped(s, said, in, "Type it here, or press Say it.", c->subtle, lh);
            /* The caret: where the next letter goes. */
            if (W.box_focus && said) {
                size_t pos = 0, n = strlen(W.text), last = 0;
                int lines = 0;
                while (pos < n) {
                    while (pos < n && W.text[pos] == ' ' && lines) pos++;
                    last = pos;
                    size_t before = pos;
                    fit_line(said, W.text, &pos, (float)in.w);
                    lines++;
                    if (pos == before) break;
                }
                if (!n) lines = 1;
                char buf[TEXT_MAX + 4];
                size_t l = n - last; if (l >= sizeof buf) l = sizeof buf - 1;
                memcpy(buf, W.text + last, l); buf[l] = 0;
                float cx = (float)in.x + (n ? shell_text_w(said, buf) + 2.f : 0.f);
                float cy = (float)in.y + lh * (float)(lines - 1);
                int cw = (int)(2.f * g.k); if (cw < 2) cw = 2;
                if (cy + lh <= (float)(t->r.y + t->r.h))
                    draw_rect(s, (rect){ (int)cx, (int)cy + 3, cw, (int)lh - 4 }, c->fg, 1.f);
            }
            break;
        }
        case WT_MIC: {
            pressable(s, c, t->r, 0, hot, sel);
            float lbl = lead ? font_line_height(lead) : 0.f;
            float gsz = (float)t->r.h * 0.40f;
            if (gsz > 48.f * g.k) gsz = 48.f * g.k;
            float gy = (float)t->r.y + ((float)t->r.h - gsz - lbl - 8.f) / 2.f + gsz / 2.f;
            mic_glyph(s, (float)t->r.x + (float)t->r.w / 2.f, gy, gsz, c->fg);
            if (lead)
                shell_text_centred(s, lead, (float)t->r.x + (float)t->r.w / 2.f,
                                   gy + gsz / 2.f + 8.f + font_ascent(lead),
                                   "Say it", c->fg, 1.f);
            break;
        }
        case WT_EXAMPLE: {
            pressable(s, c, t->r, 0, hot, sel);
            if (body) {
                rect in = { t->r.x + g.pad, t->r.y, t->r.w - 2 * g.pad, t->r.h };
                int lines = count_lines(body, EXAMPLE_TEXT(t->arg), (float)in.w);
                float lh = lh_body;
                float th = (float)lines * lh;
                in.y = t->r.y + (int)(((float)t->r.h - th) / 2.f);
                in.h = (int)th + 2;
                draw_wrapped(s, body, in, EXAMPLE_TEXT(t->arg), c->fg, lh);
            }
            break;
        }
        case WT_SAID: {
            pressable(s, c, t->r, 0, hot, sel);
            const char *chg = "Change";
            float cwid = lead ? shell_text_w(lead, chg) : 0.f;
            rect in = { t->r.x + g.pad, t->r.y + g.pad / 2,
                        t->r.w - 3 * g.pad - (int)cwid, t->r.h - g.pad };
            font *sf = plain_fit(c, said, 24.f * g.k, in, W.said, 1.f);
            int lines = count_lines(sf, W.said, (float)in.w);
            float lh = font_line_height(sf);
            in.y = t->r.y + (int)(((float)t->r.h - (float)lines * lh) / 2.f);
            draw_wrapped(s, sf, in, W.said, c->fg_hi, lh);
            if (lead)
                shell_text(s, lead, (float)(t->r.x + t->r.w - g.pad) - cwid,
                           shell_baseline(lead, (float)t->r.y, (float)t->r.h),
                           chg, c->fg, 1.f);
            break;
        }
        case WT_ITEM: {
            const char *em = NULL;
            const char *lab = pick_label(t->arg, &em);
            pressable(s, c, t->r, 0, hot, sel);
            if (em) {
                int sz = (int)(40.f * g.k);
                rect er = { t->r.x + (t->r.w - sz) / 2, t->r.y + (int)(10.f * g.k), sz, sz };
                int drawn = wishover_draw_emoji(s, em, er);
                font *lf = label ? label : body;
                if (lf) {
                    float ly = drawn ? (float)(er.y + er.h) + 6.f * g.k + font_ascent(lf)
                                     : shell_baseline(lf, (float)t->r.y, (float)t->r.h);
                    shell_text_centred(s, lf, (float)t->r.x + (float)t->r.w / 2.f, ly, lab, c->fg, 1.f);
                }
            } else if (body) {
                rect in = { t->r.x + g.pad, t->r.y, t->r.w - 2 * g.pad, t->r.h };
                int lines = count_lines(body, lab, (float)in.w);
                float th = (float)lines * lh_body;
                in.y = t->r.y + (int)(((float)t->r.h - th) / 2.f);
                in.h = (int)th + 2;
                draw_wrapped(s, body, in, lab, c->fg, lh_body);
            }
            break;
        }
        case WT_SWITCH: {
            int ri = W.first_row + t->arg;
            int on = ri < W.n_rows && W.rows[ri].on && !W.all_off;
            /* It says its state in words as well as by the knob: the
             * colour is never the only signal. On is "what you chose",
             * so it is the go colour. */
            rect r = t->r;
            float rad = (float)r.h / 2.f;
            draw_round_rect(s, r, corners_all(rad), on ? c->accent : c->surface_c, 1.f);
            if (!on) draw_round_rect_border(s, r, corners_all(rad), 2.f, hot ? c->fg : c->overlay, 1.f);
            if (sel) draw_round_rect_border(s, (rect){ r.x - 5, r.y - 5, r.w + 10, r.h + 10 },
                                            corners_all(rad + 3.f), 3.f, c->fg, 1.f);
            float kn = (float)r.h - 12.f;
            float kx = on ? (float)(r.x + r.w) - 6.f - kn / 2.f : (float)r.x + 6.f + kn / 2.f;
            draw_circle(s, kx, (float)r.y + (float)r.h / 2.f, kn / 2.f,
                        on ? c->surface_c : c->fg, 1.f);
            if (lead) {
                const char *w = on ? "On" : "Off";
                float tx = on ? (float)r.x + 14.f * g.k
                              : (float)(r.x + r.w) - 14.f * g.k - shell_text_w(lead, w);
                shell_text(s, lead, tx, shell_baseline(lead, (float)r.y, (float)r.h), w,
                           on ? c->surface_c : c->fg, 1.f);
            }
            break;
        }
        default:
            paint_button(s, c, t->r, label_of(t->kind, W.page), primary(t->kind),
                         hot, sel, t->kind == WT_STOP_LISTEN ? ty->title : lead);
            break;
        }
    }

    /* The words that are not buttons. */
    switch (W.page) {
    case WP_ASK:
        if (W.hint[0] && body)
            draw_wrapped(s, body, g.a[A_SUB], W.hint, c->subtle, lh_body);
        if (g.n_ex && g.a[A_LABEL1].h && g.labels && label)
            shell_text(s, label, (float)g.gx, (float)g.a[A_LABEL1].y + font_ascent(label),
                       "Or press one of these", c->subtle, 1.f);
        break;
    case WP_LISTEN:
        if (body)
            draw_wrapped(s, body, g.a[A_SUB],
                         "Say what you would like, then press I've finished. "
                         "You can talk for up to 30 seconds.", c->fg, lh_body);
        break;
    case WP_WAIT:
    case WP_HEARING: {
        if (label && g.labels && W.page == WP_WAIT)
            shell_text(s, label, (float)g.gx, (float)g.a[A_LABEL1].y + font_ascent(label),
                       "You said", c->subtle, 1.f);
        if (W.page == WP_WAIT)
            draw_wrapped(s, said, g.a[A_TEXT1], W.said, c->fg_hi, font_line_height(said));
        /* Three segments, one lit, stepping every 600 ms: something is
         * happening, with no claim about how long it will take. */
        rect b = g.a[A_BAR];
        int seg = (b.w - 2 * 8) / 3;
        int lit = (int)((now_ms() / 600u) % 3u);
        for (int i = 0; i < 3; i++)
            draw_round_rect(s, (rect){ b.x + i * (seg + 8), b.y, seg, b.h },
                            corners_all((float)b.h / 2.f),
                            i == lit ? c->accent : c->overlay, i == lit ? 1.f : 0.35f);
        if (body)
            draw_wrapped(s, body, g.a[A_NOTE],
                         W.page == WP_WAIT ? "This usually takes a few seconds. Nothing changes until you say yes."
                                           : "Your voice stays on this computer.", c->subtle, lh_body);
        break;
    }
    case WP_CONFIRM: {
        if (label && g.labels) {
            shell_text(s, label, (float)g.gx, (float)g.a[A_LABEL1].y + font_ascent(label),
                       "You said", c->subtle, 1.f);
            shell_text(s, label, (float)g.gx, (float)g.a[A_LABEL2].y + font_ascent(label),
                       "If you press Yes", c->subtle, 1.f);
        }
        /* The sentence at full size if it fits; a step smaller each time
         * it does not, never below the 18 px floor and never cut off. */
        draw_fitted(s, c, sent, sentb, g.k, g.a[A_TEXT2], W.sentence, c->fg_hi);
        if (W.dest[0]) {
            rect d = g.a[A_DEST];
            if (label && g.labels) shell_text(s, label, (float)d.x, (float)d.y + font_ascent(label),
                                  "This opens", c->subtle, 1.f);
            font *df = ty->title ? ty->title : lead;
            int host_h = W.dest_host[0] ? (int)(18.f * g.k * 1.45f) + 8 : 0;
            if (df) shell_text_elided(s, df, (float)d.x,
                                      (float)(d.y + d.h - host_h) - font_descent(df),
                                      (float)d.w, W.dest, c->fg_hi, 1.f);
            if (W.dest_host[0] && label)
                shell_text_elided(s, label, (float)d.x, (float)(d.y + d.h) - font_descent(label),
                                  (float)d.w, W.dest_host, c->subtle, 1.f);
        }
        break;
    }
    case WP_CANNOT:
    case WP_ERROR: {
        rect a = g.a[A_TEXT2];
        char retry[96];
        retry[0] = 0;
        if (W.page == WP_ERROR && !W.note[0] && W.retry_after > 0 &&
            (!strcmp(ERRS[W.err].code, "busy") || !strcmp(ERRS[W.err].code, "resting"))) {
            int m = (W.retry_after + 59) / 60;
            if (m <= 1)       snprintf(retry, sizeof retry, "%s", "Try again in a minute.");
            else if (m < 90)  snprintf(retry, sizeof retry, "Try again in about %d minutes.", m);
            else              snprintf(retry, sizeof retry, "Try again in about %d hours.", (m + 30) / 60);
        }
        const char *line = W.page == WP_CANNOT ? W.note
                         : (W.note[0] ? W.note : retry[0] ? retry : ERRS[W.err].line);
        /* What she is asked to say Yes to must be whole, so the line above it
         * gives way first, to a smaller size. */
        int offer = W.page == WP_CANNOT && W.pending[0] && W.sentence[0];
        int lab = g.labels ? (int)(18.f * g.k * 1.45f) + 8 : 0;
        rect first = a;
        if (offer) { first.h -= g.group + lab + (int)(3.f * 18.f * 1.5f); if (first.h < 0) first.h = 0; }
        font *ff = offer ? plain_fit(c, sent, 26.f * g.k, first, line, 1.1f) : sent;
        int y = draw_wrapped(s, ff, offer ? first : a, line, c->fg_hi, font_line_height(ff) * 1.1f);
        if (offer) {
            rect b = { a.x, y + g.group, a.w, a.y + a.h - y - g.group };
            if (label && g.labels) shell_text(s, label, (float)b.x, (float)b.y + font_ascent(label),
                                              "But I can do this", c->subtle, 1.f);
            b.y += lab; b.h -= lab;
            draw_fitted(s, c, sent, sentb, g.k, b, W.sentence, c->fg_hi);
        } else if (W.page == WP_CANNOT && body) {
            rect b = { a.x, y + g.gap, a.w, a.y + a.h - y - g.gap };
            const char *fine = "Try asking in a different way, or press "
                               "Try something else to see what Wishes can do.";
            if (count_lines(body, fine, (float)b.w) * (int)lh_body <= b.h)
                draw_wrapped(s, body, b, fine, c->subtle, lh_body);
        }
        break;
    }
    case WP_ANSWER: {
        rect a = g.a[A_TEXT2];
        const char *line = W.help_page[0]
                         ? "A page in Help shows you, step by step. Press Show me how to open it."
                         : "I don't have an answer for that yet.";
        int offer = W.pending[0] && W.sentence[0];
        int lab = g.labels ? (int)(18.f * g.k * 1.45f) + 8 : 0;
        rect first = a;
        if (offer) { first.h -= g.group + lab + (int)(3.f * 18.f * 1.5f); if (first.h < 0) first.h = 0; }
        font *ff = offer ? plain_fit(c, sent, 26.f * g.k, first, line, 1.1f) : sent;
        int y = draw_wrapped(s, ff, offer ? first : a, line, c->fg_hi, font_line_height(ff) * 1.1f);
        if (offer) {
            rect b = { a.x, y + g.group, a.w, a.y + a.h - y - g.group };
            if (label && g.labels) shell_text(s, label, (float)b.x, (float)b.y + font_ascent(label),
                                  "Or I can do it for you", c->subtle, 1.f);
            b.y += lab;
            b.h -= lab;
            draw_fitted(s, c, sent, sentb, g.k, b, W.sentence, c->fg_hi);
        }
        break;
    }
    case WP_DONE: {
        rect a = g.a[A_TEXT2];
        draw_fitted(s, c, sent, sentb, g.k, a, W.sentence, c->fg_hi);
        int used = rich_end_y - a.y + g.gap;
        rect b = { a.x, a.y + used, a.w, a.h - used };
        /* The small print only if all of it fits: a sentence ending in an
         * ellipsis is worse than one that is not there. */
        const char *fine = W.undo_until
                         ? "Changed your mind? Press Undo my last wish in the next 10 minutes."
                         : "It keeps working until you turn it off. You can do that in "
                           "Settings, under My wishes.";
        if (body && W.title[0] && count_lines(body, fine, (float)b.w) * (int)lh_body <= b.h)
            draw_wrapped(s, body, b, fine, c->subtle, lh_body);
        break;
    }
    case WP_CONSENT: {
        rect a = g.a[A_TEXT2];
        int y = a.y;
        for (int i = 0; i < N_CONSENT; i++) {
            rect b = { a.x, y, a.w, a.y + a.h - y };
            int need = count_lines(body, CONSENT[i], (float)a.w) * (int)lh_body;
            if (b.h < need) break;               /* whole sentences only */
            /* A 4 px bar, not a border: this is to read, not to press. */
            draw_rect(s, (rect){ a.x, y + 4, 4, need - 8 }, c->overlay, 1.f);
            b.x += 16; b.w -= 16;
            y = draw_wrapped(s, body, b, CONSENT[i], c->fg, lh_body) + (int)(lh_body * 0.4f);
        }
        break;
    }
    case WP_TODAY: {
        if (body)
            draw_wrapped(s, body, g.a[A_SUB], sub_text(WP_TODAY), c->subtle, lh_body);
        rect tx = g.a[A_TEXT1];
        for (int i = 0; i < g.mine_n; i++) {
            int ri = v.first_item + i;
            if (ri >= W.n_today) break;
            char buf[TITLE_MAX + 48];
            if (W.today[ri].times == 1) snprintf(buf, sizeof buf, "%s: once", W.today[ri].title);
            else if (W.today[ri].times > 1) snprintf(buf, sizeof buf, "%s: %d times", W.today[ri].title, W.today[ri].times);
            else snprintf(buf, sizeof buf, "%s%s%s", W.today[ri].title, W.today[ri].tstr[0] ? ": " : "", W.today[ri].tstr);
            if (body)
                draw_wrapped(s, body, (rect){ tx.x, tx.y + i * g.mine_row_h, tx.w, g.mine_row_h },
                             buf, c->fg_hi, lh_body);
        }
        break;
    }
    case WP_PICK:
        if (body)
            draw_wrapped(s, body, g.a[A_SUB],
                         W.g_stage < 0 ? "These work even when the internet doesn't. Press one."
                                       : wg_get(W.g_tmpl)->pick, c->subtle, lh_body);
        break;
    case WP_MINE: {
        if (body)
            draw_wrapped(s, body, g.a[A_SUB], sub_text(WP_MINE), c->subtle, lh_body);
        rect tx = g.a[A_TEXT1];
        for (int i = 0; i < g.mine_n; i++) {
            int ri = W.first_row + i;
            if (ri >= W.n_rows) break;
            int ry = tx.y + i * g.mine_row_h;
            if (i > 0) draw_hrule(s, g.gx, ry - g.group / 2, g.cw, 1, c->overlay, 1.f);
            rect r = { tx.x, ry, tx.w, g.mine_text_h };
            if (!g.mine_stacked)
                r.y = ry + (g.mine_row_h - g.group - g.mine_text_h) / 2;
            char buf[SENT_MAX + 32];
            if (W.removing == i)
                snprintf(buf, sizeof buf, "Remove this wish? %s", W.rows[ri].sentence);
            else if (W.rows[ri].paused)
                snprintf(buf, sizeof buf, "Paused. %s", W.rows[ri].sentence);
            else
                snprintf(buf, sizeof buf, "%s", W.rows[ri].sentence);
            /* Bold marks are for the confirmation; here it is one plain
             * sentence. */
            char plain[SENT_MAX + 32];
            size_t o = 0;
            for (const char *p = buf; *p && o + 1 < sizeof plain; p++)
                if (*p != '*') plain[o++] = *p;
            plain[o] = 0;
            draw_wrapped(s, body, r, plain, W.rows[ri].on && !W.all_off ? c->fg_hi : c->subtle,
                         lh_body);
        }
        break;
    }
    }
}

/* ── pressing it ─────────────────────────────────────────────────── */

static int hit(rect r, int x, int y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

/* She has chosen every blank: build the wish from the tables and ask the
 * engine to check it. Nothing goes to the internet; the engine checks it
 * as it checks one from the cloud and writes its own sentence. */
static void gallery_send(shell_ctx *c)
{
    (void)c;
    char said[300], doc[700];
    if (wg_build(W.g_tmpl, W.g_choice, said, sizeof said, doc, sizeof doc) < 0) {
        set_error("broken", NULL);
        return;
    }
    W.g_stage = -1;
    W.g_first = 0;
    if (W.req_id) return;
    if (K.fd < 0) k_connect();
    if (K.fd < 0) { set_error("broken", NULL); return; }
    char q[700], rest[1700];
    js_quote(q, sizeof q, said);
    snprintf(rest, sizeof rest, ",\"wish\":%s,\"said\":%s,\"voice\":false", doc, q);
    int id = k_send(OP_CHECK, "check", rest);
    if (id < 0) { set_error("broken", NULL); return; }
    snprintf(W.said, sizeof W.said, "%s", said);
    W.voice = 0;
    begin_request(OP_CHECK, id);
    go(WP_WAIT);
}

static void pick_back(void)
{
    W.g_first = 0;
    if (W.g_stage > 0) { W.g_stage--; W.hover = W.sel = -1; return; }
    if (W.g_stage == 0) { W.g_stage = -1; W.hover = W.sel = -1; return; }
    go(WP_ASK);
}

static void act(shell_ctx *c, const wish_target *t)
{
    switch (t->kind) {
    case WT_STOP_ALL:
        wish_command(c, WISH_CMD_STOP_ALL);
        snprintf(W.sentence, sizeof W.sentence, "%s",
                 "All your wishes are off. You can turn them back on in My wishes.");
        W.title[0] = 0;
        go(WP_DONE);
        break;
    case WT_GALLERY:
        W.g_stage = -1;
        W.g_first = 0;
        go(WP_PICK);
        break;
    case WT_BACK:
        pick_back();
        break;
    case WT_ITEM: {
        if (W.page != WP_PICK) break;
        if (W.g_stage < 0) {
            const wg_tmpl *tp = wg_get(t->arg);
            if (!tp) break;
            W.g_tmpl = t->arg;
            memset(W.g_choice, 0, sizeof W.g_choice);
            W.g_first = 0;
            W.hover = W.sel = -1;
            if (tp->n_slots == 0) gallery_send(c);
            else W.g_stage = 0;
        } else {
            const wg_tmpl *tp = wg_get(W.g_tmpl);
            if (!tp) break;
            W.g_choice[W.g_stage] = t->arg;
            W.g_first = 0;
            W.hover = W.sel = -1;
            if (++W.g_stage >= tp->n_slots) gallery_send(c);
        }
        break;
    }
    case WT_BOX:      W.box_focus = 1; break;
    case WT_MIC:      W.box_focus = 0; voice_start(); break;
    case WT_ASK:      submit(c); break;
    case WT_CLOSE:    c->wish_open = 0; break;
    case WT_MINE:
        W.first_row = 0;
        go(WP_MINE);
        k_send(OP_LIST, "list", NULL);
        break;
    case WT_EXAMPLE:
        snprintf(W.text, sizeof W.text, "%s", EXAMPLES[t->arg]);
        W.voice = 0;
        submit(c);
        break;
    case WT_STOP_LISTEN: voice_stop(); break;
    case WT_CANCEL:
        if (W.page == WP_LISTEN || W.page == WP_HEARING) voice_cancel();
        else cancel_request();
        go(WP_ASK);
        break;
    case WT_SAID:
        /* "You said" is editable: back to the box with her words in it. */
        if (W.pending[0]) {
            char rest[96];
            snprintf(rest, sizeof rest, ",\"pending\":\"%s\"", W.pending);
            k_send(OP_DISCARD, "discard", rest);
            W.pending[0] = 0;
        }
        snprintf(W.text, sizeof W.text, "%s", W.said);
        W.last_ms = 0;
        go(WP_ASK);
        break;
    case WT_YES: {
        if (!W.pending[0]) break;
        char rest[96];
        snprintf(rest, sizeof rest, ",\"pending\":\"%s\"", W.pending);
        k_send(OP_CONFIRM, "confirm", rest);
        /* The sentence stays for the Done page. */
        W.pending[0] = 0;
        break;
    }
    case WT_NO: {
        if (W.pending[0]) {
            char rest[96];
            snprintf(rest, sizeof rest, ",\"pending\":\"%s\"", W.pending);
            k_send(OP_DISCARD, "discard", rest);
            W.pending[0] = 0;
        }
        W.text[0] = 0;
        W.hint[0] = 0;
        go(WP_ASK);
        break;
    }
    case WT_SHOW: {
        if (!W.pending[0]) break;
        char rest[96];
        snprintf(rest, sizeof rest, ",\"pending\":\"%s\"", W.pending);
        k_send(OP_TEST, "test", rest);
        break;
    }
    case WT_READ:    say_aloud(W.sentence); break;
    case WT_AGAIN:
        W.text[0] = 0;
        W.hint[0] = 0;
        W.last_ms = 0;
        go(W.consent == 0 ? WP_CONSENT : WP_ASK);
        break;
    case WT_HELP: {
        /* Only a page that is on this computer, by a name with nothing
         * in it but letters, digits and dashes. */
        int okname = W.help_page[0] != 0;
        for (const char *p = W.help_page; *p; p++)
            if (!(isalnum((unsigned char)*p) || *p == '-')) okname = 0;
        char path[256];
        snprintf(path, sizeof path, "%s/%s.html", WISH_HELP_DIR, W.help_page);
        if (okname && access(path, R_OK) == 0) {
            char url[300];
            snprintf(url, sizeof url, "file://%s", path);
            open_url(c, url);
            c->wish_open = 0;
        }
        break;
    }
    case WT_UNDO:
        wish_undo(c);
        snprintf(W.sentence, sizeof W.sentence, "%s", "It is back how it was.");
        W.title[0] = 0;
        break;
    case WT_CONSENT_YES:
        W.consent = 1;
        k_send(OP_CONSENT, "consent", ",\"version\":1,\"yes\":true");
        if (!W.said[0]) go(WP_ASK);
        break;
    case WT_CONSENT_NO:
        W.consent = 0;
        k_send(OP_CONSENT, "consent", ",\"version\":1,\"yes\":false");
        c->wish_open = 0;
        break;
    case WT_SWITCH: {
        int ri = W.first_row + t->arg;
        if (ri >= W.n_rows) break;
        W.rows[ri].on = !W.rows[ri].on;
        char q[96], rest[128];
        js_quote(q, sizeof q, W.rows[ri].wid);
        snprintf(rest, sizeof rest, ",\"wid\":%s,\"on\":%s", q,
                 W.rows[ri].on ? "true" : "false");
        k_send(OP_ENABLE, "enable", rest);
        break;
    }
    case WT_REMOVE:    W.removing = t->arg; break;
    case WT_REMOVE_NO: W.removing = -1; break;
    case WT_REMOVE_YES: {
        int ri = W.first_row + t->arg;
        W.removing = -1;
        if (ri >= W.n_rows) break;
        char q[96], rest[128];
        js_quote(q, sizeof q, W.rows[ri].wid);
        snprintf(rest, sizeof rest, ",\"wid\":%s", q);
        k_send(OP_REMOVE, "remove", rest);
        memmove(&W.rows[ri], &W.rows[ri + 1], (size_t)(W.n_rows - ri - 1) * sizeof W.rows[0]);
        W.n_rows--;
        break;
    }
    case WT_ALL_OFF:
        if (W.all_off) { k_send(OP_ALLON, "all_on", NULL); W.all_off = 0; }
        else { k_send(OP_ALLOFF, "all_off", NULL); W.all_off = 1; wishover_clear(c); }
        for (int i = 0; i < W.n_rows; i++) W.rows[i].on = !W.all_off && W.rows[i].on;
        break;
    case WT_RESTORE:
        k_send(OP_RESTORE, "restore", NULL);
        break;
    case WT_MORE: {
        wish_view v = view_now(c);
        int shown = rows_shown(c, &v);
        if (W.page == WP_TODAY) {
            W.first_row += shown > 0 ? shown : 1;
            if (W.first_row >= W.n_today) W.first_row = 0;
            break;
        }
        if (W.page == WP_PICK) {
            W.g_first += shown > 0 ? shown : 1;
            if (W.g_first >= v.n_items) W.g_first = 0;
            W.hover = W.sel = -1;
            break;
        }
        W.first_row += shown > 0 ? shown : 1;
        if (W.first_row >= W.n_rows) W.first_row = 0;
        W.removing = -1;
        break;
    }
    case WT_NEW:
        W.text[0] = 0;
        W.hint[0] = 0;
        go(W.consent == 0 ? WP_CONSENT : WP_ASK);
        break;
    }
}

int wish_click(shell_ctx *c, int x, int y)
{
    if (!c->wish_open) return 0;
    wish_view v = view_now(c);
    wgeom g;
    layout(c, c->screen_w, c->screen_h, &v, &g);
    for (int i = 0; i < g.n; i++)
        if (hit(g.t[i].r, x, y)) {
            if (g.t[i].kind != WT_BOX) W.box_focus = (W.page == WP_ASK && g.t[i].kind == WT_BOX);
            act(c, &g.t[i]);
            return 1;
        }
    /* Anywhere else on the panel: nothing, and taken. It covers the
     * screen, so a press that fell through would land on something she
     * cannot see. */
    return 1;
}

void wish_motion(shell_ctx *c, int x, int y)
{
    if (!c->wish_open) return;
    wish_view v = view_now(c);
    wgeom g;
    layout(c, c->screen_w, c->screen_h, &v, &g);
    W.hover = -1;
    for (int i = 0; i < g.n; i++) if (hit(g.t[i].r, x, y)) { W.hover = i; break; }
}

/* Remove the last character (not the last byte) from her words. */
static void backspace(void)
{
    size_t n = strlen(W.text);
    while (n > 0) {
        n--;
        if (((unsigned char)W.text[n] & 0xC0) != 0x80) break;
    }
    W.text[n] = 0;
}

int wish_key(shell_ctx *c, int k)
{
    if (!c->wish_open) return 0;
    wish_view v = view_now(c);
    wgeom g;
    layout(c, c->screen_w, c->screen_h, &v, &g);

    /* evdev: 1 esc, 14 backspace, 15 tab, 28 enter, 57 space,
     * 103 up, 105 left, 106 right, 108 down. */
    if (k == 1) {
        /* Escape is "back" where there is a back, and "close" where
         * there is not -- never "yes". */
        if (W.page == WP_PICK) { pick_back(); }
        else if (W.page == WP_LISTEN || W.page == WP_HEARING) { voice_cancel(); go(WP_ASK); }
        else if (W.page == WP_WAIT) { cancel_request(); go(WP_ASK); }
        else if (W.page == WP_CONFIRM) {
            wish_target t = { .kind = WT_NO };
            act(c, &t);
        }
        else c->wish_open = 0;
        return 1;
    }
    if (W.page == WP_ASK && W.box_focus) {
        if (k == 28) { submit(c); return 1; }
        if (k == 14) { backspace(); W.hint[0] = 0; return 1; }
        if (k == 15 || k == 108) {        /* Tab / down: out of the box */
            W.box_focus = 0;
            W.sel = g.n > 1 ? 1 : 0;
            return 1;
        }
        const char *t = c->key_text;
        char fb[2] = { 0, 0 };
        if (!t[0]) { fb[0] = shell_key_char(k); t = fb; }
        if (t[0] && (unsigned char)t[0] >= 0x20) {
            if (strlen(W.text) + strlen(t) <= TEXT_MAX) strcat(W.text, t);
            W.hint[0] = 0;
            W.voice = 0;
        }
        return 1;
    }
    if (!g.n) return 1;
    switch (k) {
    case 15: case 106: case 108:
        W.sel = (W.sel + 1) % g.n;
        if (g.t[W.sel].kind == WT_BOX) { W.box_focus = 1; W.sel = -1; }
        return 1;
    case 105: case 103:
        W.sel = (W.sel <= 0 ? g.n : W.sel) - 1;
        if (g.t[W.sel].kind == WT_BOX) { W.box_focus = 1; W.sel = -1; }
        return 1;
    case 28: case 57:
        /* No default: Enter presses only what the keyboard is ON. On the
         * confirmation that is nothing until she moves to Yes or No. */
        if (W.sel >= 0 && W.sel < g.n) act(c, &g.t[W.sel]);
        return 1;
    }
    return 1;
}
