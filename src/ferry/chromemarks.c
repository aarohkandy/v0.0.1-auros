/* ═══════════════════════════════════════════════════════════════════
 *  ferry-chromemarks — her Edge and Chrome bookmarks, for Firefox.
 *
 *  Most of the people AurOS is for used Edge, because Windows opens it.
 *  Chromium browsers keep bookmarks in one plain JSON file, `Bookmarks`
 *  (not encrypted, unlike their passwords, which never come). This
 *  turns one or more of them into the bookmarks.html format Firefox
 *  imports: the bookmarks bar becomes Firefox's toolbar, "Other
 *  bookmarks" its Other Bookmarks; a second browser's become a folder.
 *
 *  It parses a file from a disk we did not build, as her, so: a
 *  size cap, a depth cap, every string escaped for HTML, and only
 *  http, https and ftp links (a `javascript:` bookmark is a program,
 *  and a `file:` one points at a Windows path that is not here).
 *
 *    ferry-chromemarks LABEL FILE [LABEL FILE]...  > bookmarks.html
 *
 *  stderr: "N bookmarks, M left out". exit 0 at least one bookmark ·
 *  1 none · 2 unreadable or not a bookmarks file.
 * ═══════════════════════════════════════════════════════════════════ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_FILE  (32u << 20)
#define MAX_DEPTH 64

/* ── a small JSON reader: just enough DOM for one Bookmarks file ── */

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
struct jv {
    int type;
    char *str;                 /* J_STR: UTF-8, NUL-terminated; J_NUM: text */
    struct jv **kid;           /* J_ARR / J_OBJ values */
    char **key;                /* J_OBJ keys */
    size_t n, cap;
};

static const char *p, *end;
static int depth;

static void ws(void) { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }

static void jfree(struct jv *v)
{
    if (!v) return;
    for (size_t i = 0; i < v->n; i++) {
        jfree(v->kid[i]);
        if (v->key) free(v->key[i]);
    }
    free(v->kid); free(v->key); free(v->str); free(v);
}

static void put_utf8(char **o, unsigned long c)
{
    char *q = *o;
    if (c < 0x80) *q++ = (char)c;
    else if (c < 0x800) { *q++ = (char)(0xC0 | (c >> 6)); *q++ = (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { *q++ = (char)(0xE0 | (c >> 12)); *q++ = (char)(0x80 | ((c >> 6) & 0x3F));
                            *q++ = (char)(0x80 | (c & 0x3F)); }
    else { *q++ = (char)(0xF0 | (c >> 18)); *q++ = (char)(0x80 | ((c >> 12) & 0x3F));
           *q++ = (char)(0x80 | ((c >> 6) & 0x3F)); *q++ = (char)(0x80 | (c & 0x3F)); }
    *o = q;
}

static int hex4(const char *s, unsigned long *v)
{
    *v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        *v <<= 4;
        if (c >= '0' && c <= '9') *v |= (unsigned long)(c - '0');
        else if (c >= 'a' && c <= 'f') *v |= (unsigned long)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') *v |= (unsigned long)(c - 'A' + 10);
        else return 0;
    }
    return 1;
}

/* A string at p (on the opening quote). Escapes decoded; \u0000 and
 * lone surrogates become U+FFFD, so the result is always a C string. */
static char *jstring(void)
{
    const char *s = ++p;
    size_t raw = 0;
    while (s + raw < end && s[raw] != '"') { if (s[raw] == '\\') raw++; raw++; }
    if (s + raw >= end) return NULL;
    char *out = malloc(raw * 2 + 4), *o = out;   /* \uXXXX (6) -> at most 4 bytes */
    if (!out) return NULL;
    while (p < end && *p != '"') {
        if (*p != '\\') { *o++ = *p++; continue; }
        if (++p >= end) break;
        char e = *p++;
        switch (e) {
        case 'n': *o++ = '\n'; break;   case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;   case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'u': {
            unsigned long c, lo;
            if (end - p < 4 || !hex4(p, &c)) { free(out); return NULL; }
            p += 4;
            if (c >= 0xD800 && c < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u' &&
                hex4(p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                p += 6;
            } else if ((c >= 0xD800 && c < 0xE000) || c == 0) {
                c = 0xFFFD;
            }
            put_utf8(&o, c);
            break;
        }
        default: *o++ = e;               /* \" \\ \/ */
        }
    }
    if (p >= end) { free(out); return NULL; }
    p++;                                  /* closing quote */
    *o = '\0';
    return out;
}

static struct jv *jvalue(void);

static int push(struct jv *v, char *key, struct jv *kid)
{
    if (v->n == v->cap) {
        size_t c = v->cap ? v->cap * 2 : 8;
        struct jv **k = realloc(v->kid, c * sizeof *k);
        if (!k) return 0;
        v->kid = k;
        if (v->type == J_OBJ) {
            char **kk = realloc(v->key, c * sizeof *kk);
            if (!kk) return 0;
            v->key = kk;
        }
        v->cap = c;
    }
    v->kid[v->n] = kid;
    if (v->type == J_OBJ) v->key[v->n] = key;
    v->n++;
    return 1;
}

static struct jv *jvalue(void)
{
    struct jv *v;
    ws();
    if (p >= end || !(v = calloc(1, sizeof *v))) return NULL;
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        v->type = *p == '{' ? J_OBJ : J_ARR;
        if (++depth > MAX_DEPTH) goto bad;
        p++; ws();
        if (p < end && *p == close) { p++; depth--; return v; }
        for (;;) {
            char *key = NULL;
            ws();
            if (v->type == J_OBJ) {
                if (p >= end || *p != '"' || !(key = jstring())) goto bad;
                ws();
                if (p >= end || *p != ':') { free(key); goto bad; }
                p++;
            }
            struct jv *kid = jvalue();
            if (!kid || !push(v, key, kid)) { free(key); jfree(kid); goto bad; }
            ws();
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == close) { p++; break; }
            goto bad;
        }
        depth--;
        return v;
    }
    if (*p == '"') { v->type = J_STR; if (!(v->str = jstring())) goto bad; return v; }
    /* The buffer is the file as read, with no terminator: every
     * comparison is bounded by what is left (found by the fuzzer: a file
     * ending in "tru" was read four bytes past its end). */
    if (end - p >= 4 && (!strncmp(p, "true", 4) || !strncmp(p, "null", 4))) {
        v->type = *p == 't' ? J_BOOL : J_NULL; p += 4; return v;
    }
    if (end - p >= 5 && !strncmp(p, "false", 5)) { v->type = J_BOOL; p += 5; return v; }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        const char *s = p;
        while (p < end && strchr("+-.0123456789eE", *p)) p++;
        v->type = J_NUM;
        if (!(v->str = strndup(s, (size_t)(p - s)))) goto bad;
        return v;
    }
bad:
    jfree(v);
    return NULL;
}

static struct jv *get(const struct jv *o, const char *k)
{
    if (!o || o->type != J_OBJ) return NULL;
    for (size_t i = 0; i < o->n; i++) if (!strcmp(o->key[i], k)) return o->kid[i];
    return NULL;
}
static const char *str(const struct jv *o, const char *k)
{
    const struct jv *v = get(o, k);
    return v && v->type == J_STR ? v->str : NULL;
}

/* ── writing Netscape bookmarks ─────────────────────────────────── */

static long kept, dropped;

static void html(const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&': fputs("&amp;", stdout); break;
        case '<': fputs("&lt;", stdout); break;
        case '>': fputs("&gt;", stdout); break;
        case '"': fputs("&quot;", stdout); break;
        case '\n': case '\r': case '\t': putchar(' '); break;
        default: putchar(*s);
        }
    }
}

static int allowed(const char *url)
{
    static const char *ok[] = { "http://", "https://", "ftp://" };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++)
        if (!strncasecmp(url, ok[i], strlen(ok[i]))) return 1;
    return 0;
}

static void indent(int d) { for (int i = 0; i < d; i++) fputs("    ", stdout); }

/* Chromium counts microseconds from 1601; bookmarks.html, seconds from 1970. */
static long long unix_time(const char *chrome)
{
    long long t = chrome ? atoll(chrome) / 1000000 - 11644473600ll : 0;
    return t > 0 ? t : 0;
}

static void folder(const struct jv *node, int d, const char *attr, const char *title);

static void items(const struct jv *kids, int d)
{
    if (!kids || kids->type != J_ARR) return;
    for (size_t i = 0; i < kids->n; i++) {
        const struct jv *k = kids->kid[i];
        const char *type = str(k, "type"), *name = str(k, "name"), *url = str(k, "url");
        if (type && !strcmp(type, "folder")) { folder(k, d, NULL, NULL); continue; }
        if (!url || !allowed(url)) { dropped++; continue; }
        indent(d);
        fputs("<DT><A HREF=\"", stdout); html(url);
        printf("\" ADD_DATE=\"%lld\">", unix_time(str(k, "date_added")));
        html(name && *name ? name : url);
        fputs("</A>\n", stdout);
        kept++;
    }
}

static void folder(const struct jv *node, int d, const char *attr, const char *title)
{
    const char *name = title ? title : str(node, "name");
    if (d > MAX_DEPTH) return;
    indent(d);
    printf("<DT><H3%s%s%s ADD_DATE=\"%lld\">", attr ? " " : "", attr ? attr : "",
           attr ? "=\"true\"" : "", unix_time(str(node, "date_added")));
    html(name && *name ? name : "Folder");
    fputs("</H3>\n", stdout);
    indent(d); fputs("<DL><p>\n", stdout);
    items(get(node, "children"), d + 1);
    indent(d); fputs("</DL><p>\n", stdout);
}

/* A Bookmarks document from a buffer, or NULL. */
static struct jv *parse(const char *buf, size_t n)
{
    struct jv *v;
    p = buf; end = buf + n; depth = 0;
    if (end - p >= 3 && !memcmp(p, "\xEF\xBB\xBF", 3)) p += 3;   /* a BOM */
    v = jvalue();
    if (v && !get(get(v, "roots"), "bookmark_bar")) { jfree(v); v = NULL; }
    return v;
}

static struct jv *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    struct jv *v;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 2 || (unsigned long)n > MAX_FILE ||
        fseek(f, 0, SEEK_SET) || !(buf = malloc((size_t)n))) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    v = parse(buf, (size_t)n);
    free(buf);
    return v;
}

/* The whole bookmarks.html for N documents (NULL ones skipped). */
static void emit(struct jv **doc, char **label, int n)
{
    int first = 1;
    fputs("<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
          "<!-- Brought across from Windows by AurOS. -->\n"
          "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
          "<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n", stdout);
    for (int i = 0; i < n; i++) {
        const struct jv *roots = get(doc[i], "roots");
        if (!roots) continue;
        if (first) {
            /* Her main browser's bar is Firefox's toolbar. */
            folder(get(roots, "bookmark_bar"), 1, "PERSONAL_TOOLBAR_FOLDER", NULL);
            if (get(roots, "other")) folder(get(roots, "other"), 1, "UNFILED_BOOKMARKS_FOLDER", NULL);
            if (get(roots, "synced")) folder(get(roots, "synced"), 1, NULL, NULL);
            first = 0;
        } else {
            char title[256];
            snprintf(title, sizeof title, "Bookmarks from %s", label[i]);
            indent(1); fputs("<DT><H3>", stdout); html(title); fputs("</H3>\n", stdout);
            indent(1); fputs("<DL><p>\n", stdout);
            folder(get(roots, "bookmark_bar"), 2, NULL, NULL);
            if (get(roots, "other")) folder(get(roots, "other"), 2, NULL, NULL);
            if (get(roots, "synced")) folder(get(roots, "synced"), 2, NULL, NULL);
            indent(1); fputs("</DL><p>\n", stdout);
        }
    }
    fputs("</DL><p>\n", stdout);
}

#ifndef CHROMEMARKS_NO_MAIN
int main(int argc, char **argv)
{
    int n = (argc - 1) / 2, good = 0;
    struct jv *doc[16] = { 0 };
    char *label[16];
    if (argc < 3 || argc % 2 == 0 || n > 16) {
        fprintf(stderr, "usage: ferry-chromemarks LABEL FILE [LABEL FILE]...\n");
        return 2;
    }
    for (int i = 0; i < n; i++) {
        label[i] = argv[1 + 2 * i];
        if ((doc[i] = load(argv[2 + 2 * i]))) good++;
        else fprintf(stderr, "ferry-chromemarks: %s is not a bookmarks file I can read\n",
                     argv[2 + 2 * i]);
    }
    if (!good) return 2;
    emit(doc, label, n);
    for (int i = 0; i < n; i++) jfree(doc[i]);
    fprintf(stderr, "%ld bookmarks, %ld left out\n", kept, dropped);
    return kept ? 0 : 1;
}
#endif
