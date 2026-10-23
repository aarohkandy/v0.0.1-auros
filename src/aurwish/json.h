/* json.h — a strict JSON reader and a small writer, for wishes.
 *
 * Every byte this reads came from somewhere that is not trusted: the
 * cloud (docs/AI.md §7.1: treat it as if an attacker wrote it), a socket,
 * or a file in her home directory that anything she runs can rewrite.
 * So it refuses rather than guesses:
 *
 *   - a size limit, a depth limit and a node limit, all set by the caller
 *   - invalid UTF-8, overlong forms, surrogates (raw or \u), a BOM
 *   - any control character, raw or escaped (U+0000-001F, U+007F-009F)
 *   - duplicate keys (which one "wins" differs between parsers, and an
 *     attacker picks the parser that is not ours)
 *   - numbers that are not plain integers, or bigger than 10^12
 *   - anything after the value
 *
 * The tree lives in one caller-owned arena; nothing is malloc'd. */
#ifndef AURWISH_JSON_H
#define AURWISH_JSON_H

#include <stddef.h>

enum { J_NULL, J_BOOL, J_INT, J_STR, J_ARR, J_OBJ };

typedef struct jv {
    int          type;
    int          n;        /* J_ARR/J_OBJ: children; J_STR: byte length */
    long long    i;        /* J_INT value; J_BOOL 0/1 */
    const char  *s;        /* J_STR: NUL-terminated UTF-8, in the arena */
    const char  *key;      /* this node's key, when it is an object member */
    struct jv   *kid;      /* J_ARR/J_OBJ: first child */
    struct jv   *next;     /* next sibling */
    const char  *src;      /* where this value was in the input ... */
    size_t       srclen;   /* ... and how long, so it can be kept verbatim */
} jv;

typedef struct {
    size_t max_bytes;      /* whole input */
    int    max_depth;      /* 1 = a scalar or an empty container at top */
    int    max_nodes;
    int    max_str;        /* decoded bytes per string */
} jlimits;

typedef struct {
    char  *mem;            /* arena; must outlive the tree */
    size_t cap, used;
    const char *err;       /* why it was refused (for logs and tests) */
} jarena;

/* NULL on refusal, with a->err set. */
const jv *json_parse(jarena *a, const jlimits *lim, const char *src, size_t len);

/* Lookups. NULL when absent or of another type. */
const jv *jget(const jv *obj, const char *key);
/* 1 if every key of obj is in the NULL-terminated list. */
int  jonly(const jv *obj, const char *const *keys);

/* ── writing ─────────────────────────────────────────────────────── */

typedef struct { char *b; size_t cap, len; int bad; } jout;

void jw_raw(jout *o, const char *s);
void jw_rawn(jout *o, const char *s, size_t n);
void jw_str(jout *o, const char *s);          /* quoted + escaped */
void jw_int(jout *o, long long v);
void jw_key(jout *o, const char *k);          /* "k": with a comma if needed */
/* Comma bookkeeping is the caller's: jw_key writes a leading comma when
 * the previous byte is not '{' or '['. */

#endif
