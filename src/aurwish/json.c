/* json.c — see json.h. Recursive descent, bounded in every direction. */
#include <stdio.h>
#include <string.h>
#include "json.h"

typedef struct {
    jarena        *a;
    const jlimits *lim;
    const unsigned char *p, *end;
    int nodes;
} P;

static void *grab(P *x, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (x->a->used + n > x->a->cap) { x->a->err = "out of room"; return NULL; }
    void *r = x->a->mem + x->a->used;
    x->a->used += n;
    memset(r, 0, n);
    return r;
}

static jv *node(P *x, int type)
{
    if (++x->nodes > x->lim->max_nodes) { x->a->err = "too many values"; return NULL; }
    jv *v = grab(x, sizeof *v);
    if (v) v->type = type;
    return v;
}

static void ws(P *x)
{
    while (x->p < x->end && (*x->p == ' ' || *x->p == '\t' || *x->p == '\n' || *x->p == '\r'))
        x->p++;
}

static int bad(P *x, const char *why) { if (!x->a->err) x->a->err = why; return -1; }

/* A code point that may appear in any string we accept. */
static int cp_ok(unsigned c)
{
    if (c < 0x20 || (c >= 0x7F && c <= 0x9F)) return 0;   /* controls */
    if (c >= 0xD800 && c <= 0xDFFF) return 0;             /* surrogates */
    if (c > 0x10FFFF) return 0;
    return 1;
}

/* Decode one UTF-8 sequence strictly; returns its length or -1. */
static int utf8_one(const unsigned char *s, const unsigned char *end, unsigned *out)
{
    unsigned c = s[0];
    int n;
    unsigned min;
    if (c < 0x80) { *out = c; return 1; }
    else if ((c & 0xE0) == 0xC0) { n = 2; c &= 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { n = 3; c &= 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { n = 4; c &= 0x07; min = 0x10000; }
    else return -1;
    if (end - s < n) return -1;
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return -1;
        c = (c << 6) | (s[i] & 0x3F);
    }
    if (c < min) return -1;                                  /* overlong */
    *out = c;
    return n;
}

static int put_cp(char *d, unsigned c)
{
    if (c < 0x80) { d[0] = (char)c; return 1; }
    if (c < 0x800) { d[0] = (char)(0xC0 | c >> 6); d[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        d[0] = (char)(0xE0 | c >> 12); d[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        d[2] = (char)(0x80 | (c & 0x3F)); return 3;
    }
    d[0] = (char)(0xF0 | c >> 18); d[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    d[2] = (char)(0x80 | ((c >> 6) & 0x3F)); d[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

static int hex4(P *x, unsigned *out)
{
    if (x->end - x->p < 4) return bad(x, "short \\u escape");
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char h = x->p[i];
        v <<= 4;
        if (h >= '0' && h <= '9') v |= h - '0';
        else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
        else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
        else return bad(x, "bad \\u escape");
    }
    x->p += 4;
    *out = v;
    return 0;
}

/* Parses a string at x->p (which is at the opening quote). */
static const char *str(P *x, int *len_out)
{
    x->p++;                                       /* " */
    int max = x->lim->max_str;
    char *d = grab(x, (size_t)max + 1);
    if (!d) return NULL;
    int n = 0;
    for (;;) {
        if (x->p >= x->end) { bad(x, "unterminated string"); return NULL; }
        unsigned c = *x->p;
        if (c == '"') { x->p++; break; }
        if (c == '\\') {
            x->p++;
            if (x->p >= x->end) { bad(x, "unterminated string"); return NULL; }
            unsigned e = *x->p++;
            switch (e) {
            case '"': c = '"'; break;   case '\\': c = '\\'; break;
            case '/': c = '/'; break;   case 'b': c = 8; break;
            case 'f': c = 12; break;    case 'n': c = 10; break;
            case 'r': c = 13; break;    case 't': c = 9; break;
            case 'u': {
                if (hex4(x, &c) < 0) return NULL;
                if (c >= 0xD800 && c <= 0xDBFF) {
                    unsigned lo;
                    if (x->end - x->p < 6 || x->p[0] != '\\' || x->p[1] != 'u') {
                        bad(x, "lone surrogate"); return NULL;
                    }
                    x->p += 2;
                    if (hex4(x, &lo) < 0) return NULL;
                    if (lo < 0xDC00 || lo > 0xDFFF) { bad(x, "lone surrogate"); return NULL; }
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                } else if (c >= 0xDC00 && c <= 0xDFFF) {
                    bad(x, "lone surrogate"); return NULL;
                }
                break;
            }
            default: bad(x, "bad escape"); return NULL;
            }
        } else {
            int k = utf8_one(x->p, x->end, &c);
            if (k < 0) { bad(x, "invalid UTF-8"); return NULL; }
            x->p += k;
        }
        if (!cp_ok(c)) { bad(x, "control or surrogate character"); return NULL; }
        if (n + 4 > max) { bad(x, "string too long"); return NULL; }
        n += put_cp(d + n, c);
    }
    d[n] = 0;
    /* Give back what the string did not use. */
    x->a->used -= ((size_t)max + 1 + 7) & ~(size_t)7;
    x->a->used += ((size_t)n + 1 + 7) & ~(size_t)7;
    *len_out = n;
    return d;
}

static jv *value(P *x, int depth);

static jv *container(P *x, int depth, int obj)
{
    jv *v = node(x, obj ? J_OBJ : J_ARR);
    if (!v) return NULL;
    x->p++;
    ws(x);
    char close = obj ? '}' : ']';
    if (x->p < x->end && *x->p == close) { x->p++; return v; }
    if (depth + 1 > x->lim->max_depth) { bad(x, "nested too deeply"); return NULL; }
    jv **tail = &v->kid;
    for (;;) {
        const char *key = NULL;
        if (obj) {
            ws(x);
            if (x->p >= x->end || *x->p != '"') { bad(x, "expected a key"); return NULL; }
            int kl;
            key = str(x, &kl);
            if (!key) return NULL;
            for (jv *k = v->kid; k; k = k->next)
                if (!strcmp(k->key, key)) { bad(x, "duplicate key"); return NULL; }
            ws(x);
            if (x->p >= x->end || *x->p != ':') { bad(x, "expected ':'"); return NULL; }
            x->p++;
        }
        jv *kid = value(x, depth + 1);
        if (!kid) return NULL;
        kid->key = key;
        *tail = kid;
        tail = &kid->next;
        v->n++;
        ws(x);
        if (x->p >= x->end) { bad(x, "unterminated container"); return NULL; }
        if (*x->p == ',') { x->p++; continue; }
        if (*x->p == close) { x->p++; return v; }
        bad(x, "expected ',' or end");
        return NULL;
    }
}

static jv *value1(P *x, int depth);

static jv *value(P *x, int depth)
{
    ws(x);
    const unsigned char *at = x->p;
    jv *v = value1(x, depth);
    if (v) { v->src = (const char *)at; v->srclen = (size_t)(x->p - at); }
    return v;
}

static jv *value1(P *x, int depth)
{
    if (x->p >= x->end) { bad(x, "expected a value"); return NULL; }
    if (depth > x->lim->max_depth) { bad(x, "nested too deeply"); return NULL; }
    unsigned char c = *x->p;
    if (c == '{' || c == '[') return container(x, depth, c == '{');
    if (c == '"') {
        jv *v = node(x, J_STR);
        if (!v) return NULL;
        v->s = str(x, &v->n);
        return v->s ? v : NULL;
    }
    static const struct { const char *w; int t, b; } lits[] = {
        { "true", J_BOOL, 1 }, { "false", J_BOOL, 0 }, { "null", J_NULL, 0 } };
    for (unsigned i = 0; i < sizeof lits / sizeof lits[0]; i++) {
        size_t l = strlen(lits[i].w);
        if ((size_t)(x->end - x->p) >= l && !memcmp(x->p, lits[i].w, l)) {
            x->p += l;
            jv *v = node(x, lits[i].t);
            if (v) v->i = lits[i].b;
            return v;
        }
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        int neg = 0;
        if (c == '-') { neg = 1; x->p++; }
        if (x->p >= x->end || *x->p < '0' || *x->p > '9') { bad(x, "bad number"); return NULL; }
        if (*x->p == '0' && x->p + 1 < x->end && x->p[1] >= '0' && x->p[1] <= '9') {
            bad(x, "leading zero"); return NULL;
        }
        long long n = 0;
        while (x->p < x->end && *x->p >= '0' && *x->p <= '9') {
            n = n * 10 + (*x->p++ - '0');
            if (n > 1000000000000LL) { bad(x, "number too big"); return NULL; }
        }
        if (x->p < x->end && (*x->p == '.' || *x->p == 'e' || *x->p == 'E')) {
            bad(x, "not a whole number"); return NULL;
        }
        if (neg && n == 0) { bad(x, "negative zero"); return NULL; }
        jv *v = node(x, J_INT);
        if (v) v->i = neg ? -n : n;
        return v;
    }
    bad(x, "unexpected character");
    return NULL;
}

const jv *json_parse(jarena *a, const jlimits *lim, const char *src, size_t len)
{
    a->used = 0;
    a->err = NULL;
    if (!src) { a->err = "nothing"; return NULL; }
    if (len > lim->max_bytes) { a->err = "too big"; return NULL; }
    P x = { a, lim, (const unsigned char *)src, (const unsigned char *)src + len, 0 };
    jv *v = value(&x, 1);
    if (!v) { if (!a->err) a->err = "refused"; return NULL; }
    ws(&x);
    if (x.p != x.end) { a->err = "something after the value"; return NULL; }
    return v;
}

const jv *jget(const jv *o, const char *key)
{
    if (!o || o->type != J_OBJ) return NULL;
    for (const jv *k = o->kid; k; k = k->next)
        if (!strcmp(k->key, key)) return k;
    return NULL;
}

int jonly(const jv *o, const char *const *keys)
{
    if (!o || o->type != J_OBJ) return 0;
    for (const jv *k = o->kid; k; k = k->next) {
        int ok = 0;
        for (int i = 0; keys[i]; i++) if (!strcmp(keys[i], k->key)) { ok = 1; break; }
        if (!ok) return 0;
    }
    return 1;
}

/* ── writing ─────────────────────────────────────────────────────── */

void jw_rawn(jout *o, const char *s, size_t l)
{
    if (o->bad || o->len + l + 1 > o->cap) { o->bad = 1; return; }
    memcpy(o->b + o->len, s, l);
    o->len += l;
    o->b[o->len] = 0;
}

void jw_raw(jout *o, const char *s) { jw_rawn(o, s, strlen(s)); }

void jw_str(jout *o, const char *s)
{
    jw_raw(o, "\"");
    char e[8];
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { e[0] = '\\'; e[1] = (char)*p; e[2] = 0; jw_raw(o, e); }
        else if (*p < 0x20) { snprintf(e, sizeof e, "\\u%04x", *p); jw_raw(o, e); }
        else { e[0] = (char)*p; e[1] = 0; jw_raw(o, e); }
    }
    jw_raw(o, "\"");
}

void jw_int(jout *o, long long v)
{
    char b[32];
    snprintf(b, sizeof b, "%lld", v);
    jw_raw(o, b);
}

void jw_key(jout *o, const char *k)
{
    if (o->len && o->b[o->len - 1] != '{' && o->b[o->len - 1] != '[') jw_raw(o, ",");
    jw_str(o, k);
    jw_raw(o, ":");
}
