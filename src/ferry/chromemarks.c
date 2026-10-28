/* ═══════════════════════════════════════════════════════════════════
 *  ferry-chromemarks — Edge, Chrome and Brave bookmarks, as the HTML
 *  file Firefox imports.
 *
 *  Chromium keeps bookmarks in a plain JSON file ("Bookmarks", no
 *  encryption: unlike saved passwords it is not sealed to the Windows
 *  sign-in), so they can come across from an offline disk. This turns
 *  that file into the Netscape bookmark format, which every browser
 *  reads.
 *
 *  THE INPUT IS HOSTILE. It is a file from a computer we did not build,
 *  and ferry reads it. So:
 *    - a small JSON reader of our own, bounded: nesting is capped, the
 *      node count is capped, strings are length-capped, nothing is
 *      recursive beyond the cap, and every read checks the end;
 *    - only addresses beginning http://, https:// or ftp:// are kept. A
 *      javascript: or data: or file: address in a bookmark is a program
 *      or a way into her own disk, not a web page; it is dropped and
 *      counted, and the report says how many;
 *    - everything written is HTML-escaped, control characters are
 *      removed, and bytes that are not valid UTF-8 become U+FFFD, so a
 *      name cannot break out of its tag;
 *    - this file is also what the fuzz harness (fuzz/) feeds.
 *
 *  As a library (CM_AS_LIBRARY) it is what ferry-copy's `marks` mode
 *  uses, which runs as HER uid. Standalone, for debugging:
 *      ferry-chromemarks [--toolbar] NAME=FILE ... > bookmarks.html
 *  ═════════════════════════════════════════════════════════════════ */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CM_MAX_DEPTH  48
#define CM_MAX_NODES  1000000
#define CM_MAX_STR    (1u << 20)
#define CM_MAX_URL    8192
#define CM_MAX_NAME   512

struct cm_stats { unsigned urls, dropped, folders; };

struct cm_node {
    char *name, *url;
    int is_folder;
    struct cm_node **kid;
    size_t nkid, ckid;
};

struct cm_out { char *buf; size_t len, cap; int bad; };

/* ── output ──────────────────────────────────────────────────────── */
static void co_put(struct cm_out *o, const char *s, size_t n)
{
    if (o->bad) return;
    if (o->len + n + 1 > o->cap) {
        size_t nc = (o->cap ? o->cap : 4096);
        char *nb;
        while (nc < o->len + n + 1) nc *= 2;
        nb = realloc(o->buf, nc);
        if (!nb) { o->bad = 1; return; }
        o->buf = nb; o->cap = nc;
    }
    memcpy(o->buf + o->len, s, n);
    o->len += n;
    o->buf[o->len] = '\0';
}
static void co_str(struct cm_out *o, const char *s) { co_put(o, s, strlen(s)); }

/* Length of the valid UTF-8 sequence at s (1-4), or 0 when it is not one. */
static int utf8_len(const unsigned char *s, size_t avail)
{
    if (s[0] < 0x80) return 1;
    if (s[0] >= 0xC2 && s[0] <= 0xDF && avail >= 2 && (s[1] & 0xC0) == 0x80) return 2;
    if (s[0] >= 0xE0 && s[0] <= 0xEF && avail >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        if (s[0] == 0xE0 && s[1] < 0xA0) return 0;                 /* overlong */
        if (s[0] == 0xED && s[1] >= 0xA0) return 0;                /* surrogate */
        return 3;
    }
    if (s[0] >= 0xF0 && s[0] <= 0xF4 && avail >= 4 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        if (s[0] == 0xF0 && s[1] < 0x90) return 0;
        if (s[0] == 0xF4 && s[1] >= 0x90) return 0;
        return 4;
    }
    return 0;
}

/* Text for an HTML body or attribute: escaped, scrubbed, length-capped. */
static void co_text(struct cm_out *o, const char *s, size_t cap)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s), i = 0, written = 0;
    while (i < n && written < cap) {
        int l = utf8_len(p + i, n - i);
        if (l == 0) { co_str(o, "\xEF\xBF\xBD"); i++; written += 3; continue; }
        if (l == 1) {
            unsigned char c = p[i++];
            written++;
            switch (c) {
            case '&': co_str(o, "&amp;");  break;
            case '<': co_str(o, "&lt;");   break;
            case '>': co_str(o, "&gt;");   break;
            case '"': co_str(o, "&quot;"); break;
            case '\'': co_str(o, "&#39;"); break;
            case '\t': case '\n': case '\r': co_str(o, " "); break;
            default:  if (c >= 0x20 && c != 0x7f) co_put(o, (const char *)&c, 1);
            }
        } else {
            co_put(o, s + i, (size_t)l);
            i += (size_t)l; written += (size_t)l;
        }
    }
}

/* ── the reader ──────────────────────────────────────────────────── */
struct cm_p { const char *s; size_t n, i; size_t nodes; int bad; };

static void ws(struct cm_p *p)
{
    while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' ||
                           p->s[p->i] == '\n' || p->s[p->i] == '\r')) p->i++;
}
static int peek(struct cm_p *p) { ws(p); return p->i < p->n ? (unsigned char)p->s[p->i] : -1; }

static void put_utf8(char *o, size_t *n, uint32_t c)
{
    if (c < 0x80)        o[(*n)++] = (char)c;
    else if (c < 0x800)  { o[(*n)++] = (char)(0xC0 | c >> 6); o[(*n)++] = (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000){ o[(*n)++] = (char)(0xE0 | c >> 12); o[(*n)++] = (char)(0x80 | ((c >> 6) & 0x3F)); o[(*n)++] = (char)(0x80 | (c & 0x3F)); }
    else                 { o[(*n)++] = (char)(0xF0 | c >> 18); o[(*n)++] = (char)(0x80 | ((c >> 12) & 0x3F)); o[(*n)++] = (char)(0x80 | ((c >> 6) & 0x3F)); o[(*n)++] = (char)(0x80 | (c & 0x3F)); }
}

static int hex4(const char *s, uint32_t *v)
{
    uint32_t r = 0;
    for (int k = 0; k < 4; k++) {
        char c = s[k];
        r <<= 4;
        if (c >= '0' && c <= '9') r |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') r |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') r |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    *v = r;
    return 1;
}

/* A JSON string into a malloc'd, NUL-terminated UTF-8 buffer. Decoding
 * never grows a string (\n is 2 bytes in, 1 out; \uXXXX is 6 in, at most
 * 3 out), so the buffer is the raw length: total memory is bounded by the
 * size of the file. */
static char *rd_string(struct cm_p *p)
{
    char *out;
    size_t n = 0, end, st;
    ws(p);
    if (p->i >= p->n || p->s[p->i] != '"') { p->bad = 1; return NULL; }
    p->i++;
    st = p->i;
    for (end = st; end < p->n && p->s[end] != '"'; end++)
        if (p->s[end] == '\\') end++;                     /* the escaped byte is never the end */
    if (end >= p->n) { p->bad = 1; return NULL; }           /* no closing quote */
    out = malloc(end - st + 1);
    if (!out) { p->bad = 1; return NULL; }
    while (p->i < end) {
        unsigned char c = (unsigned char)p->s[p->i++];
        uint32_t cp;
        if (c != '\\') { out[n++] = (char)c; continue; }
        if (p->i >= end) break;                             /* a backslash at the very end */
        c = (unsigned char)p->s[p->i++];
        switch (c) {
        case '"': case '\\': case '/': out[n++] = (char)c; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'n': out[n++] = '\n'; break;
        case 'r': out[n++] = '\r'; break;
        case 't': out[n++] = '\t'; break;
        case 'u':
            if (p->i + 4 > end || !hex4(p->s + p->i, &cp)) { p->bad = 1; free(out); return NULL; }
            p->i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF) {                       /* a high surrogate: needs its pair */
                uint32_t lo;
                if (p->i + 6 <= end && p->s[p->i] == '\\' && p->s[p->i + 1] == 'u' &&
                    hex4(p->s + p->i + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p->i += 6;
                } else cp = 0xFFFD;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;       /* a lone low one */
            put_utf8(out, &n, cp ? cp : 0xFFFD);                        /* no NUL in a name */
            break;
        default: p->bad = 1; free(out); return NULL;
        }
    }
    p->i = end + 1;
    out[n] = '\0';
    return out;
}

static void skip_value(struct cm_p *p, int depth);

static void skip_string(struct cm_p *p)
{
    char *s = rd_string(p);
    free(s);
}

/* Skip any JSON value without building anything. */
static void skip_value(struct cm_p *p, int depth)
{
    int c;
    if (p->bad) return;
    if (depth > CM_MAX_DEPTH) { p->bad = 1; return; }
    c = peek(p);
    if (c == '"') { skip_string(p); return; }
    if (c == '{' || c == '[') {
        int close = c == '{' ? '}' : ']';
        p->i++;
        if (peek(p) == close) { p->i++; return; }
        for (;;) {
            if (c == '{') {
                skip_string(p); if (p->bad) return;
                if (peek(p) != ':') { p->bad = 1; return; }
                p->i++;
            }
            skip_value(p, depth + 1); if (p->bad) return;
            c = peek(p);
            if (c == ',') { p->i++; c = close == '}' ? '{' : '['; continue; }
            if (c == close) { p->i++; return; }
            p->bad = 1; return;
        }
    }
    /* number, true, false, null */
    {
        size_t st = p->i;
        while (p->i < p->n && strchr("+-.0123456789eEtruefalsn", p->s[p->i]) && p->s[p->i]) p->i++;
        if (p->i == st) p->bad = 1;
    }
}

static void node_free(struct cm_node *n)
{
    size_t i;
    if (!n) return;
    for (i = 0; i < n->nkid; i++) node_free(n->kid[i]);
    free(n->kid); free(n->name); free(n->url); free(n);
}

static struct cm_node *rd_node(struct cm_p *p, int depth);

static void rd_children(struct cm_p *p, struct cm_node *n, int depth)
{
    if (peek(p) != '[') { skip_value(p, depth + 1); return; }
    p->i++;
    if (peek(p) == ']') { p->i++; return; }
    for (;;) {
        struct cm_node *k;
        if (peek(p) != '{') { skip_value(p, depth + 1); }
        else {
            k = rd_node(p, depth + 1);
            if (p->bad) { node_free(k); return; }
            if (k) {
                if (n->nkid == n->ckid) {
                    size_t nc = n->ckid ? n->ckid * 2 : 8;
                    struct cm_node **nk = realloc(n->kid, nc * sizeof *nk);
                    if (!nk) { node_free(k); p->bad = 1; return; }
                    n->kid = nk; n->ckid = nc;
                }
                n->kid[n->nkid++] = k;
            }
        }
        if (p->bad) return;
        {
            int c = peek(p);
            if (c == ',') { p->i++; continue; }
            if (c == ']') { p->i++; return; }
            p->bad = 1; return;
        }
    }
}

/* One bookmark or folder: an object with type, name, url, children. */
static struct cm_node *rd_node(struct cm_p *p, int depth)
{
    struct cm_node *n;
    char *type = NULL;
    int saw_children = 0;
    if (depth > CM_MAX_DEPTH || ++p->nodes > CM_MAX_NODES) { p->bad = 1; return NULL; }
    if (peek(p) != '{') { p->bad = 1; return NULL; }
    p->i++;
    n = calloc(1, sizeof *n);
    if (!n) { p->bad = 1; return NULL; }
    if (peek(p) == '}') { p->i++; free(n); return NULL; }
    for (;;) {
        char *key = rd_string(p);
        if (!key) { free(type); node_free(n); return NULL; }
        if (peek(p) != ':') { p->bad = 1; free(key); free(type); node_free(n); return NULL; }
        p->i++;
        if (!strcmp(key, "type") && peek(p) == '"')          { free(type); type = rd_string(p); }
        else if (!strcmp(key, "name") && peek(p) == '"')     { free(n->name); n->name = rd_string(p); }
        else if (!strcmp(key, "url") && peek(p) == '"')      { free(n->url); n->url = rd_string(p); }
        else if (!strcmp(key, "children"))                   { saw_children = 1; rd_children(p, n, depth); }
        else                                                 skip_value(p, depth + 1);
        free(key);
        if (p->bad) { free(type); node_free(n); return NULL; }
        {
            int c = peek(p);
            if (c == ',') { p->i++; continue; }
            if (c == '}') { p->i++; break; }
            p->bad = 1; free(type); node_free(n); return NULL;
        }
    }
    n->is_folder = (type && !strcasecmp(type, "folder")) || (!type && saw_children);
    if (type && !strcasecmp(type, "url")) n->is_folder = 0;
    free(type);
    return n;
}

/* ── emitting ────────────────────────────────────────────────────── */
static int safe_scheme(const char *u)
{
    return !strncasecmp(u, "http://", 7) || !strncasecmp(u, "https://", 8) ||
           !strncasecmp(u, "ftp://", 6);
}

static int has_content(const struct cm_node *n)
{
    size_t i;
    if (!n->is_folder) return n->url && safe_scheme(n->url);
    for (i = 0; i < n->nkid; i++) if (has_content(n->kid[i])) return 1;
    return 0;
}

/* Every address with a scheme we refuse, anywhere in the tree. */
static void count_dropped(const struct cm_node *n, int depth, struct cm_stats *st)
{
    size_t i;
    if (depth > CM_MAX_DEPTH) return;
    if (n->is_folder) { for (i = 0; i < n->nkid; i++) count_dropped(n->kid[i], depth + 1, st); return; }
    if (n->url && (!safe_scheme(n->url) || strlen(n->url) > CM_MAX_URL)) st->dropped++;
}

static void emit_node(struct cm_out *o, const struct cm_node *n, int depth, struct cm_stats *st)
{
    size_t i;
    if (depth > CM_MAX_DEPTH) return;
    if (n->is_folder) {
        if (!has_content(n)) return;
        st->folders++;
        co_str(o, "<DT><H3>"); co_text(o, n->name ? n->name : "Folder", CM_MAX_NAME); co_str(o, "</H3>\n<DL><p>\n");
        for (i = 0; i < n->nkid; i++) emit_node(o, n->kid[i], depth + 1, st);
        co_str(o, "</DL><p>\n");
        return;
    }
    if (!n->url) return;
    if (!safe_scheme(n->url) || strlen(n->url) > CM_MAX_URL) return;
    st->urls++;
    co_str(o, "<DT><A HREF=\""); co_text(o, n->url, CM_MAX_URL); co_str(o, "\">");
    co_text(o, n->name && *n->name ? n->name : n->url, CM_MAX_NAME);
    co_str(o, "</A>\n");
}

/* ── the interface ───────────────────────────────────────────────── */
static void cm_begin(struct cm_out *o)
{
    memset(o, 0, sizeof *o);
    co_str(o, "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
              "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
              "<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n");
}

/* Add one Chromium "Bookmarks" file. `label` names the wrapper folder
 * ("Edge"); with `toolbar` set its bookmarks bar becomes the Firefox
 * toolbar and everything else goes into the wrapper. Returns 0 when the
 * file was read (even if it held nothing), -1 when it was not a
 * bookmarks file. */
static int cm_add(struct cm_out *o, const char *json, size_t len, const char *label,
                  int toolbar, struct cm_stats *st)
{
    struct cm_p p = { json, len, 0, 0, 0 };
    struct cm_node **root = NULL;
    char **rkey = NULL;
    size_t nroot = 0, i;
    int found = 0, rc = 0, c;
    memset(st, 0, sizeof *st);

    if (peek(&p) != '{') return -1;
    p.i++;
    if (peek(&p) != '}') for (;;) {
        char *key = rd_string(&p);
        if (!key) { rc = -1; break; }
        if (peek(&p) != ':') { free(key); rc = -1; break; }
        p.i++;
        if (!strcmp(key, "roots") && peek(&p) == '{') {
            found = 1;
            p.i++;
            if (peek(&p) != '}') for (;;) {
                char *rk = rd_string(&p);
                struct cm_node *nd;
                if (!rk) { rc = -1; break; }
                if (peek(&p) != ':') { free(rk); rc = -1; break; }
                p.i++;
                if (peek(&p) == '{') {
                    nd = rd_node(&p, 1);
                    if (p.bad) { free(rk); node_free(nd); rc = -1; break; }
                    if (nd) {
                        struct cm_node **nr = realloc(root, (nroot + 1) * sizeof *root);
                        char **nk = realloc(rkey, (nroot + 1) * sizeof *rkey);
                        if (!nr || !nk) { free(rk); node_free(nd); p.bad = 1; rc = -1; if (nr) root = nr; if (nk) rkey = nk; break; }
                        root = nr; rkey = nk;
                        root[nroot] = nd; rkey[nroot] = rk; nroot++;
                    } else free(rk);
                } else { skip_value(&p, 2); free(rk); if (p.bad) { rc = -1; break; } }
                c = peek(&p);
                if (c == ',') { p.i++; continue; }
                if (c == '}') { p.i++; break; }
                rc = -1; break;
            }
            else p.i++;
        } else skip_value(&p, 1);
        free(key);
        if (rc || p.bad) { rc = -1; break; }
        c = peek(&p);
        if (c == ',') { p.i++; continue; }
        if (c == '}') break;
        rc = -1; break;
    }
    if (p.bad) rc = -1;

    if (rc == 0 && found) {
        for (i = 0; i < nroot; i++) count_dropped(root[i], 1, st);
        /* the bookmarks bar first, as the toolbar; the rest in a wrapper */
        int wrote_wrapper = 0;
        for (i = 0; i < nroot; i++) {
            if (!strcmp(rkey[i], "bookmark_bar") && toolbar && has_content(root[i])) {
                size_t k;
                co_str(o, "<DT><H3 PERSONAL_TOOLBAR_FOLDER=\"true\">Bookmarks bar</H3>\n<DL><p>\n");
                for (k = 0; k < root[i]->nkid; k++) emit_node(o, root[i]->kid[k], 1, st);
                co_str(o, "</DL><p>\n");
            }
        }
        for (i = 0; i < nroot; i++) {
            int already = !strcmp(rkey[i], "bookmark_bar") && toolbar;
            if (already || !has_content(root[i])) continue;
            if (!wrote_wrapper) {
                co_str(o, "<DT><H3>"); co_text(o, label, CM_MAX_NAME); co_str(o, " bookmarks</H3>\n<DL><p>\n");
                wrote_wrapper = 1;
            }
            if (root[i]->name == NULL) { root[i]->name = strdup(rkey[i]); }
            root[i]->is_folder = 1;
            emit_node(o, root[i], 1, st);
        }
        if (wrote_wrapper) co_str(o, "</DL><p>\n");
    }
    for (i = 0; i < nroot; i++) { node_free(root[i]); free(rkey[i]); }
    free(root); free(rkey);
    if (rc == 0 && !found) rc = -1;
    return rc;
}

static void cm_end(struct cm_out *o) { co_str(o, "</DL><p>\n"); }

#ifndef CM_AS_LIBRARY
int main(int argc, char **argv)
{
    struct cm_out o;
    int a, toolbar = 0, first = 1, rc = 0;
    cm_begin(&o);
    for (a = 1; a < argc; a++) {
        char *eq, *buf;
        FILE *f;
        long sz;
        struct cm_stats st;
        if (!strcmp(argv[a], "--toolbar")) { toolbar = 1; continue; }
        eq = strchr(argv[a], '=');
        if (!eq) { fprintf(stderr, "usage: ferry-chromemarks [--toolbar] NAME=FILE ...\n"); return 2; }
        *eq = '\0';
        f = fopen(eq + 1, "rb");
        if (!f) { fprintf(stderr, "cannot read %s\n", eq + 1); rc = 1; continue; }
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz < 0 || sz > (64L << 20)) { fclose(f); rc = 1; continue; }
        buf = malloc((size_t)sz + 1);
        if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); rc = 1; continue; }
        fclose(f);
        if (cm_add(&o, buf, (size_t)sz, argv[a], toolbar && first, &st) != 0) { fprintf(stderr, "%s: not a bookmarks file\n", argv[a]); rc = 1; }
        else fprintf(stderr, "%s\t%u\t%u\n", argv[a], st.urls, st.dropped);
        first = 0;
        free(buf);
    }
    cm_end(&o);
    if (o.bad) return 1;
    fwrite(o.buf, 1, o.len, stdout);
    free(o.buf);
    return rc;
}
#endif
