/* cloud.c — see cloud.h. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "cloud.h"
#include "json.h"
#include "text.h"
#include "wish.h"

#define MAX_REPLY   (64 * 1024)
#define DEADLINE_S  20            /* §7.9: she waits at most 20 s */
#define POW_SLICE_MS 25
#define VERSION     "1"

/* ── SHA-256 ──────────────────────────────────────────────────────── */

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void block(uint32_t h[8], const unsigned char *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_of(const void *data, size_t len, unsigned char out[32])
{
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    const unsigned char *p = data;
    size_t n = len;
    for (; n >= 64; n -= 64, p += 64) block(h, p);
    unsigned char t[128] = {0};
    memcpy(t, p, n);
    t[n] = 0x80;
    size_t tl = n < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) t[tl - 1 - i] = (unsigned char)(bits >> (8 * i));
    block(h, t);
    if (tl == 128) block(h, t + 64);
    for (int i = 0; i < 8; i++) {
        out[4*i] = (unsigned char)(h[i] >> 24); out[4*i+1] = (unsigned char)(h[i] >> 16);
        out[4*i+2] = (unsigned char)(h[i] >> 8); out[4*i+3] = (unsigned char)h[i];
    }
}

int pow_ok(const unsigned char h[32], int bits)
{
    for (int i = 0; i < bits; i++)
        if (h[i / 8] & (0x80 >> (i % 8))) return 0;
    return 1;
}

/* ── the cleaned text (§7.12) ─────────────────────────────────────── */

static int is_mail_char(unsigned char c)
{
    return isalnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
}

int cloud_clean_text(const char *in, char *out, size_t n)
{
    if (!in || !n) return -1;
    size_t w = 0;
    int cps = 0;
    const char *s = in;
    /* valid UTF-8 and nothing that is a control: the shell's text box
     * should never send otherwise, and the service would refuse it. */
    if (!text_valid(in)) return -1;
    while (*s) {
        /* a number: digits, with the gaps people put in phone numbers */
        if (*s >= '0' && *s <= '9') {
            const char *e = s;
            int digits = 0;
            for (;;) {
                if (*e >= '0' && *e <= '9') { digits++; e++; }
                else if ((*e == ' ' || *e == '-' || *e == '.' || *e == '(' || *e == ')') &&
                         ((e[1] >= '0' && e[1] <= '9') || ((e[1] == ' ' || e[1] == '(') && e[2] >= '0' && e[2] <= '9')))
                    e++;
                else break;
            }
            if (digits >= 5) {
                if (w + 9 >= n) return -1;
                memcpy(out + w, "[number]", 8); w += 8; cps += 8; s = e;
                continue;
            }
        }
        /* an e-mail address: expand around an '@' that has text on both sides */
        if (is_mail_char((unsigned char)*s)) {
            const char *e = s;
            while (is_mail_char((unsigned char)*e)) e++;
            if (*e == '@' && is_mail_char((unsigned char)e[1])) {
                const char *f = e + 1;
                while (is_mail_char((unsigned char)*f)) f++;
                if (w + 8 >= n) return -1;
                memcpy(out + w, "[email]", 7); w += 7; cps += 7; s = f;
                continue;
            }
        }
        const char *q = s;
        utf8_next(&q);
        size_t k = (size_t)(q - s);
        if (w + k + 1 >= n) return -1;
        memcpy(out + w, s, k);
        w += k; s = q; cps++;
    }
    out[w] = 0;
    return cps;
}

const char *cloud_lang(const char *in, char *out, size_t n)
{
    size_t l = in ? strlen(in) : 0;
    int ok = l >= 2 && l <= 12 && n > l && isalpha((unsigned char)in[0]) && isalpha((unsigned char)in[1]);
    for (size_t i = 0; ok && i < l; i++)
        if (!(isalnum((unsigned char)in[i]) || in[i] == '-')) ok = 0;
    snprintf(out, n, "%s", ok ? in : "en");
    return out;
}

/* ── the client ───────────────────────────────────────────────────── */

enum { IDLE, POW, REG, CALL };

struct cloud {
    cloud_cfg cfg;
    char install_id[40], token[96];
    long long expires_at;
    int st, kind, retried_auth, retried_pow;
    long long id, started;
    char path[16];
    char body[8192];
    size_t blen;
    /* proof of work */
    char prefix[160], day[16];
    int bits;
    unsigned long long counter;
    char nonce[16];
    /* the curl child */
    pid_t pid;
    int fd;
    char out[MAX_REPLY + 64];
    size_t olen;
    pid_t fpid;                 /* a forget in flight */
};

static long long mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

cloud *cloud_new(const cloud_cfg *cfg)
{
    cloud *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->cfg = *cfg;
    c->fd = -1;
    return c;
}

int cloud_available(const cloud *c) { return c->cfg.url[0] != 0; }
void cloud_set_sensors(cloud *c, unsigned sensors) { c->cfg.sensors = sensors; }
int cloud_busy(const cloud *c) { return c->st != IDLE; }
int cloud_fd(const cloud *c) { return c->fd; }
int cloud_cpu(const cloud *c) { return c->st == POW; }

static void kill_child(cloud *c)
{
    if (c->pid > 0) { kill(c->pid, SIGKILL); waitpid(c->pid, NULL, 0); }
    c->pid = 0;
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
}

void cloud_free(cloud *c)
{
    if (!c) return;
    kill_child(c);
    if (c->fpid > 0) { kill(c->fpid, SIGKILL); waitpid(c->fpid, NULL, 0); }
    free(c);
}

void cloud_cancel(cloud *c)
{
    kill_child(c);
    c->st = IDLE;
}

/* ── the token file ───────────────────────────────────────────────── */

static void rand_hex(char *out, int bytes)
{
    unsigned char b[32];
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    ssize_t k = fd >= 0 ? read(fd, b, (size_t)bytes) : -1;
    if (fd >= 0) close(fd);
    if (k != bytes) { for (int i = 0; i < bytes; i++) b[i] = (unsigned char)rand(); }
    for (int i = 0; i < bytes; i++) snprintf(out + 2 * i, 3, "%02x", b[i]);
}

static int token_ok(const char *t)
{
    size_t l = strlen(t);
    if (l < 30 || l > 90) return 0;
    for (; *t; t++) if (!(isalnum((unsigned char)*t) || *t == '_' || *t == '-' || *t == '.')) return 0;
    return 1;
}

static void token_load(cloud *c)
{
    c->token[0] = 0;
    c->expires_at = 0;
    int fd = open(c->cfg.token_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    char buf[1024];
    ssize_t k = fd >= 0 ? read(fd, buf, sizeof buf - 1) : -1;
    if (fd >= 0) close(fd);
    if (k > 0) {
        static char mem[8192];
        jarena a = { mem, sizeof mem, 0, NULL };
        static const jlimits L = { 1024, 3, 20, 128 };
        const jv *o = json_parse(&a, &L, buf, (size_t)k);
        const jv *id = jget(o, "install_id"), *tk = jget(o, "token"), *ex = jget(o, "expires_at");
        if (id && id->type == J_STR && strlen(id->s) >= 22 && strlen(id->s) <= 39 &&
            strspn(id->s, "0123456789abcdefABCDEF-_ghijklmnopqrstuvwxyzGHIJKLMNOPQRSTUVWXYZ") == strlen(id->s))
            snprintf(c->install_id, sizeof c->install_id, "%s", id->s);
        if (tk && tk->type == J_STR && token_ok(tk->s)) snprintf(c->token, sizeof c->token, "%s", tk->s);
        if (ex && ex->type == J_INT) c->expires_at = ex->i;
    }
    if (!c->install_id[0]) rand_hex(c->install_id, 16);          /* 128 random bits */
}

static void token_save(cloud *c)
{
    char b[512];
    snprintf(b, sizeof b, "{\"install_id\":\"%s\",\"token\":\"%s\",\"expires_at\":%lld}\n",
             c->install_id, c->token, c->expires_at);
    char tmp[700];
    snprintf(tmp, sizeof tmp, "%s.new", c->cfg.token_path);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    size_t l = strlen(b);
    int bad = write(fd, b, l) != (ssize_t)l || fsync(fd) != 0;
    if (close(fd) != 0) bad = 1;
    if (bad || rename(tmp, c->cfg.token_path) != 0) unlink(tmp);
}

/* ── curl ─────────────────────────────────────────────────────────── */

static int url_ok(const char *u, int allow_http)
{
    const char *p = u;
    if (!strncmp(p, "https://", 8)) p += 8;
    else if (allow_http && !strncmp(p, "http://", 7)) p += 7;
    else return 0;
    if (!*p) return 0;
    for (; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '-' || *p == ':')) return 0;
    return 1;
}

static int spawn_curl(cloud *c, const char *path, const char *body, size_t blen, int bearer, int detached)
{
    if (!url_ok(c->cfg.url, c->cfg.allow_http)) return -1;
    int pin[2], pout[2], pcfg[2];
    if (pipe2(pin, O_CLOEXEC) < 0) return -1;
    if (pipe2(pout, O_CLOEXEC) < 0) { close(pin[0]); close(pin[1]); return -1; }
    if (pipe2(pcfg, O_CLOEXEC) < 0) { close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]); return -1; }
    char cfgtxt[512];
    int cl = snprintf(cfgtxt, sizeof cfgtxt, "url = \"%s%s\"\n%s%s%s", c->cfg.url, path,
                      bearer ? "header = \"Authorization: Bearer " : "", bearer ? c->token : "",
                      bearer ? "\"\n" : "");
    if (cl <= 0 || (size_t)cl >= sizeof cfgtxt) goto fail;
    pid_t pid = fork();
    if (pid < 0) goto fail;
    if (pid == 0) {
        /* The child has three pipes and nothing else. curl reads its
         * config (the URL and the token) from stdin -- `--config -`; it
         * cannot read one from a pipe by path -- and the body from a
         * second pipe, which it can. */
        dup2(pcfg[0], 0);
        dup2(pout[1], 1);
#ifdef AURWISH_TEST
        int nul = getenv("AURWISH_CURL_ERR") ? open(getenv("AURWISH_CURL_ERR"), O_WRONLY | O_APPEND | O_CREAT, 0600)
                                             : open("/dev/null", O_WRONLY);
#else
        int nul = open("/dev/null", O_WRONLY);
#endif
        if (nul >= 0) { dup2(nul, 2); if (nul > 3) close(nul); }
        /* The body pipe becomes descriptor 10 exactly (not "the next free
         * one": that was closed again by the loop below once). */
        if (pin[0] != 10) dup2(pin[0], 10);
        else fcntl(10, F_SETFD, 0);                  /* not close-on-exec: curl reads it */
        for (int i = 3; i < 256; i++) if (i != 10) close(i);
        char bodypath[24];
        snprintf(bodypath, sizeof bodypath, "@/dev/fd/10");
        signal(SIGPIPE, SIG_DFL);
        const char *proto = c->cfg.allow_http ? "=http,https" : "=https";
        const char *argv[] = { "curl", "--silent", "--show-error", "--proto", proto,
                               "--max-time", "20", "--connect-timeout", "8",
                               "--max-filesize", "65536",
                               "--request", "POST",
                               "--header", "Content-Type: application/json",
                               "--header", "Accept: application/json",
                               "--header", "Expect:",
                               "--user-agent", "AurOS-Wishes/" VERSION,
                               "--dump-header", "/dev/fd/1",
                               "--write-out", "\n%{http_code}",
                               "--data-binary", bodypath,
                               "--config", "-", NULL };
        execvp("curl", (char *const *)argv);
        _exit(127);
    }
    close(pin[0]); close(pout[1]); close(pcfg[0]);
    /* Both fit the pipe at once (<= 8 KB). A child that died early makes
     * these fail, and the read side then ends, which is the answer. */
    if (write(pin[1], body, blen) < 0 || write(pcfg[1], cfgtxt, (size_t)cl) < 0) { /* see above */ }
    close(pin[1]); close(pcfg[1]);
    if (detached) { close(pout[0]); c->fpid = pid; return 0; }
    fcntl(pout[0], F_SETFL, fcntl(pout[0], F_GETFL) | O_NONBLOCK);
    c->pid = pid;
    c->fd = pout[0];
    c->olen = 0;
    c->out[0] = 0;
    return 0;
fail:
    close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]); close(pcfg[0]); close(pcfg[1]);
    return -1;
}

/* ── proof of work ────────────────────────────────────────────────── */

static void pow_begin(cloud *c, long long now, const char *day, int bits)
{
    if (day && *day) snprintf(c->day, sizeof c->day, "%s", day);
    else {
        time_t t = (time_t)now;
        struct tm tm;
        gmtime_r(&t, &tm);
        strftime(c->day, sizeof c->day, "%Y-%m-%d", &tm);
    }
    c->bits = bits > 0 && bits <= 30 ? bits : 21;
    snprintf(c->prefix, sizeof c->prefix, "auros-register-v1:%s:%s:", c->day, c->install_id);
    c->counter = 0;
    c->st = POW;
}

/* base-36 of the counter: letters and digits, as the service asks */
static void nonce_of(unsigned long long n, char *out)
{
    static const char D[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char t[16];
    int i = 0;
    do { t[i++] = D[n % 36]; n /= 36; } while (n && i < 12);
    for (int j = 0; j < i; j++) out[j] = t[i - 1 - j];
    out[i] = 0;
}

/* 1 when found */
static int pow_slice(cloud *c)
{
    long long end = mono_ms() + POW_SLICE_MS;
    size_t pl = strlen(c->prefix);
    unsigned char msg[256], h[32];
    memcpy(msg, c->prefix, pl);
    do {
        for (int i = 0; i < 4096; i++) {
            nonce_of(c->counter++, c->nonce);
            size_t nl = strlen(c->nonce);
            memcpy(msg + pl, c->nonce, nl);
            sha256_of(msg, pl + nl, h);
            if (pow_ok(h, c->bits)) return 1;
        }
    } while (mono_ms() < end);
    return 0;
}

/* ── the flow ─────────────────────────────────────────────────────── */

static int begin_register(cloud *c, long long now)
{
    int bits = 21;                  /* what the service asks today; a 400 corrects it */
#ifdef AURWISH_TEST
    if (getenv("AURWISH_POW_BITS")) bits = atoi(getenv("AURWISH_POW_BITS"));
#endif
    pow_begin(c, now, NULL, bits);
    return 0;
}

static int send_call(cloud *c)
{
    if (spawn_curl(c, c->path, c->body, c->blen, 1, 0) < 0) return -1;
    c->st = CALL;
    return 0;
}

static void jstr_into(jout *o, const char *k, const char *v) { jw_key(o, k); jw_str(o, v); }

int cloud_start(cloud *c, int kind, long long id, const char *text, const char *lang,
                const char *look, long long now, const char **err)
{
    *err = "busy";
    if (c->st != IDLE) return -1;
    *err = "offline";
    if (!cloud_available(c)) return -1;
    *err = "refused";
    if (!text || !*text) return -1;
    char lg[16];
    cloud_lang(lang, lg, sizeof lg);

    jout o = { c->body, sizeof c->body, 0, 0 };
    jw_raw(&o, "{");
    if (kind == CLOUD_WISH) {
        jstr_into(&o, "text", text);
        jstr_into(&o, "lang", lg);
        jw_key(&o, "schema"); jw_raw(&o, "{");
        jw_key(&o, "v"); jw_int(&o, 1);
        jw_key(&o, "triggers"); jw_raw(&o, "[");
        for (int i = 0; i < W_N; i++) { if (i) jw_raw(&o, ","); jw_str(&o, wish_when_name(i)); }
        jw_raw(&o, "]");
        jw_key(&o, "actions"); jw_raw(&o, "[");
        for (int i = 0; i < A_N; i++) { if (i) jw_raw(&o, ","); jw_str(&o, wish_do_name(i)); }
        jw_raw(&o, "]}");
        jw_key(&o, "sensors"); jw_raw(&o, "{");
        jw_key(&o, "compass"); jw_raw(&o, c->cfg.sensors & (1u << S_COMPASS) ? "true" : "false");
        jw_key(&o, "tilt"); jw_raw(&o, c->cfg.sensors & (1u << S_TILT) ? "true" : "false");
        jw_key(&o, "light"); jw_raw(&o, c->cfg.sensors & (1u << S_LIGHT) ? "true" : "false");
        jw_raw(&o, "}");
        snprintf(c->path, sizeof c->path, "/v1/wish");
    } else {
        jstr_into(&o, "question", text);
        jstr_into(&o, "lang", lg);
        jw_key(&o, "context"); jw_raw(&o, "{");
        const char *lk = look && (!strcmp(look, "taskbar") || !strcmp(look, "dock") ||
                                  !strcmp(look, "rail") || !strcmp(look, "tiles")) ? look : "rail";
        jstr_into(&o, "look", lk);
        jw_raw(&o, "}");
        snprintf(c->path, sizeof c->path, "/v1/ask");
    }
    jw_raw(&o, "}");
    if (o.bad || o.len > 8000) return -1;
    c->blen = o.len;
    c->kind = kind;
    c->id = id;
    c->started = mono_ms();
    c->retried_auth = c->retried_pow = 0;
    token_load(c);
    *err = "offline";
    if (!c->token[0] || c->expires_at - 86400 <= now) return begin_register(c, now), 0;
    return send_call(c);
}

void cloud_forget(cloud *c)
{
    if (!cloud_available(c)) return;
    cloud_cancel(c);
    token_load(c);
    if (c->token[0]) spawn_curl(c, "/v1/forget", "{}", 2, 1, 1);
    unlink(c->cfg.token_path);
    c->token[0] = 0;
    c->install_id[0] = 0;
    c->expires_at = 0;
}

/* Splits "<headers>\r\n\r\n<body>\n<code>" as curl wrote it. */
static int split(cloud *c, cloud_result *r)
{
    c->out[c->olen] = 0;
    char *nl = c->out + c->olen;
    while (nl > c->out && *nl != '\n') nl--;
    if (nl == c->out) return -1;
    if (!nl || nl == c->out + c->olen - 1) return -1;
    int code = atoi(nl + 1);
    *nl = 0;
    /* Headers, possibly a second block (an interim "100 Continue"), then
     * the body. */
    char *b = strstr(c->out, "\r\n\r\n");
    char *hdr_end = b;
    if (!b) return -1;
    *b = 0;
    b += 4;
    while (!strncmp(b, "HTTP/", 5)) {
        char *n2 = strstr(b, "\r\n\r\n");
        if (!n2) return -1;
        b = n2 + 4;
    }
    r->http = code;
    r->body = b;
    r->len = (size_t)(nl - b);
    r->retry_after = 0;
    for (char *l = c->out; l < hdr_end; ) {
        char *e = strstr(l, "\r\n");
        if (!e) e = hdr_end;
        if (!strncasecmp(l, "retry-after:", 12)) r->retry_after = atoi(l + 12);
        l = e + 2;
    }
    return 0;
}

static int finish(cloud_result *out, cloud *c, int http, const char *err)
{
    memset(out, 0, sizeof *out);
    out->id = c->id;
    out->kind = c->kind;
    out->http = http;
    out->err = err;
    out->body = c->out;
    if (!http) c->out[0] = 0;
    c->st = IDLE;
    return 1;
}

int cloud_step(cloud *c, long long now, cloud_result *out)
{
    if (c->fpid > 0 && waitpid(c->fpid, NULL, WNOHANG) != 0) c->fpid = 0;
    if (c->st == IDLE) return 0;
    if ((mono_ms() - c->started) / 1000 >= DEADLINE_S) {
        kill_child(c);
        return finish(out, c, 0, "busy");
    }
    if (c->st == POW) {
        if (!pow_slice(c)) return 0;
        char body[300];
        snprintf(body, sizeof body,
                 "{\"install_id\":\"%s\",\"version\":\"" VERSION "\",\"consent_version\":\"%d\","
                 "\"pow\":{\"day\":\"%s\",\"nonce\":\"%s\"}}",
                 c->install_id, c->cfg.consent_version, c->day, c->nonce);
        if (spawn_curl(c, "/v1/register", body, strlen(body), 0, 0) < 0) return finish(out, c, 0, "broken");
        c->st = REG;
        return 0;
    }
    /* a curl child: read what it has said */
    for (;;) {
        ssize_t k = read(c->fd, c->out + c->olen, MAX_REPLY - c->olen);
        if (k > 0) { c->olen += (size_t)k; if (c->olen >= MAX_REPLY) { kill_child(c); return finish(out, c, 0, "broken"); } continue; }
        if (k < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
        break;                                           /* EOF */
    }
    int status = 0;
    waitpid(c->pid, &status, 0);
    c->pid = 0;
    close(c->fd);
    c->fd = -1;
    cloud_result r;
    memset(&r, 0, sizeof r);
    if (split(c, &r) < 0 || r.http == 0) {
        /* curl could not connect, or died: she is offline for our purposes */
        int ex = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        /* 28: timed out; 63: the answer was bigger than we will read */
        return finish(out, c, 0, ex == 28 ? "busy" : ex == 63 ? "broken" : "offline");
    }
    if (c->st == REG) {
        static char mem[8192];
        jarena a = { mem, sizeof mem, 0, NULL };
        static const jlimits L = { 4096, 3, 20, 256 };
        const jv *o = json_parse(&a, &L, r.body, r.len);
        if (r.http == 400 && !c->retried_pow) {
            /* her clock is wrong: the service tells us its day and bits */
            const jv *d = jget(o, "day"), *b = jget(o, "bits");
            if (d && d->type == J_STR && strlen(d->s) == 10 && b && b->type == J_INT && b->i > 0 && b->i <= 30) {
                c->retried_pow = 1;
                char day[16];
                snprintf(day, sizeof day, "%s", d->s);
                pow_begin(c, now, day, (int)b->i);
                return 0;
            }
        }
        const jv *tk = jget(o, "token"), *ex = jget(o, "expires_at");
        if (r.http == 200 && tk && tk->type == J_STR && token_ok(tk->s)) {
            snprintf(c->token, sizeof c->token, "%s", tk->s);
            c->expires_at = ex && ex->type == J_INT ? ex->i : now + 80LL * 86400;
            token_save(c);
            if (send_call(c) < 0) return finish(out, c, 0, "broken");
            return 0;
        }
        int ra = r.retry_after, http = r.http == 200 ? 0 : r.http;
        finish(out, c, http, http ? NULL : "broken");
        out->retry_after = ra;
        out->len = 0;
        return 1;
    }
    /* CALL */
    if (r.http == 401 && !c->retried_auth) {             /* §5: register again, once, silently */
        c->retried_auth = 1;
        c->token[0] = 0;
        unlink(c->cfg.token_path);
        begin_register(c, now);
        return 0;
    }
    int http = r.http, ra = r.retry_after;
    size_t len = r.len;
    memmove(c->out, r.body, len);
    c->out[len] = 0;
    finish(out, c, http, NULL);
    out->len = len;
    out->retry_after = ra;
    return 1;
}
