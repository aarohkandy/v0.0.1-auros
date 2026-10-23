/* store.c — see store.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "store.h"

#define FILE_MAX (256 * 1024)

static const char *const ST_NAMES[] = {
    "on", "off", "paused:limit", "paused:missing", "paused:newer", "paused:broken" };

const char *store_status_name(int s) { return s >= 0 && s <= ST_BROKEN ? ST_NAMES[s] : "paused:broken"; }

int store_path(char *out, size_t n)
{
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    char dir[600];
    if (x && *x == '/') snprintf(dir, sizeof dir, "%s/auros", x);
    else if (h && *h == '/') snprintf(dir, sizeof dir, "%s/.local/share/auros", h);
    else return -1;
    /* Each level made private if we make it; an existing one is left. */
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0700); *p = '/'; }
    mkdir(tmp, 0700);
    int k = snprintf(out, n, "%s/wishes.json", dir);
    return k > 0 && (size_t)k < n ? 0 : -1;
}

static stored *fresh(void) { return calloc(1, sizeof(stored)); }

void store_free(store *st)
{
    for (int i = 0; i < st->n; i++) { free(st->s[i]->raw); free(st->s[i]); }
    memset(st, 0, sizeof *st);
}

static long long ival(const jv *o, const char *k, long long lo, long long hi, long long def)
{
    const jv *v = jget(o, k);
    return v && v->type == J_INT && v->i >= lo && v->i <= hi ? v->i : def;
}

static const char *sval(const jv *o, const char *k)
{
    const jv *v = jget(o, k);
    return v && v->type == J_STR ? v->s : NULL;
}

static int theme_exists(const wish_env *env, const char *id)
{
    if (!id || !*id || strlen(id) > 40) return 0;
    for (const char *p = id; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return 0;
    const char *dir = env->themes_dir ? env->themes_dir : getenv("AUROS_THEME_DIR");
    if (!dir) dir = "/usr/share/auros/themes";
    char p[600];
    snprintf(p, sizeof p, "%s/%s.theme", dir, id);
    return access(p, R_OK) == 0;
}

int store_parse_prev(const jv *o, const wish_env *env, wish_do *d)
{
    memset(d, 0, sizeof *d);
    if (!o || o->type != J_OBJ) return -1;
    const char *t = sval(o, "type");
    static const char *const LOOKS[] = { "taskbar", "dock", "rail", "tiles", NULL };
    static const char *const SIZES[] = { "normal", "bigger", "biggest", NULL };
    #define PICK(s, l, out) do { out = -1; for (int i_ = 0; s && l[i_]; i_++) if (!strcmp(s, l[i_])) out = i_; } while (0)
    if (!t) return -1;
    if (!strcmp(t, "set_look")) {
        static const char *const K[] = { "type", "look", NULL };
        d->type = A_LOOK;
        PICK(sval(o, "look"), LOOKS, d->look);
        return jonly(o, K) && d->look >= 0 ? 0 : -1;
    }
    if (!strcmp(t, "set_text_size") || !strcmp(t, "pointer_size")) {
        static const char *const K[] = { "type", "size", NULL };
        d->type = t[0] == 's' ? A_TEXT : A_POINTER;
        PICK(sval(o, "size"), SIZES, d->size);
        return jonly(o, K) && d->size >= 0 ? 0 : -1;
    }
    if (!strcmp(t, "night_light") || !strcmp(t, "do_not_disturb")) {
        static const char *const K[] = { "type", "on", NULL };
        const jv *on = jget(o, "on");
        d->type = t[0] == 'n' ? A_NIGHT : A_DND;
        if (!jonly(o, K) || !on || on->type != J_BOOL) return -1;
        d->on = (int)on->i;
        return 0;
    }
    if (!strcmp(t, "brightness") || !strcmp(t, "volume")) {
        static const char *const K[] = { "type", "set_percent", NULL };
        d->type = t[0] == 'b' ? A_BRIGHT : A_VOLUME;
        d->is_set = 1;
        /* Her own earlier value, which may be below a wish's floor. */
        d->pct = (int)ival(o, "set_percent", d->type == A_BRIGHT ? 5 : 0, 100, -1);
        return jonly(o, K) && d->pct >= 0 ? 0 : -1;
    }
    if (!strcmp(t, "set_wallpaper")) {
        static const char *const K[] = { "type", "color", "theme", NULL };
        const char *c = sval(o, "color"), *th = sval(o, "theme");
        d->type = A_WALL;
        if (!jonly(o, K) || (c != NULL) == (th != NULL)) return -1;
        if (c) {
            if (strlen(c) != 7 || c[0] != '#' || strspn(c + 1, "0123456789abcdefABCDEF") != 6) return -1;
            d->has_color = 1;
            d->color = (unsigned)strtoul(c + 1, NULL, 16);
            return 0;
        }
        if (!theme_exists(env, th)) return -1;
        snprintf(d->theme, sizeof d->theme, "%s", th);
        snprintf(d->theme_label, sizeof d->theme_label, "%s", th);
        return 0;
    }
    return -1;
}

/* Reads one stored wish. Never fails: what cannot be understood is kept
 * verbatim with a status that says so. */
static stored *load_one(const jv *e, const wish_env *env)
{
    stored *s = fresh();
    if (!s) return NULL;
    s->status = ST_BROKEN;
    const char *id = sval(e, "id");
    if (id && strlen(id) < sizeof s->id && id[0] == 'w' && id[1] &&
        strspn(id + 1, "0123456789") == strlen(id + 1))
        snprintf(s->id, sizeof s->id, "%s", id);
    s->created_at = ival(e, "created_at", 0, 1LL << 40, 0);
    s->last_ran   = ival(e, "last_ran", 0, 1LL << 40, 0);
    s->paused_until = ival(e, "paused_until", 0, 1LL << 40, 0);
    s->runs       = (int)ival(e, "runs", 0, 1000000000, 0);
    s->keep       = (int)ival(e, "keep", 0, 2, 0);
    s->keep_at    = ival(e, "keep_at", 0, 1LL << 40, 0);
    const char *by = sval(e, "created_by");
    s->by_voice = by && !strcmp(by, "voice");
    const char *said = sval(e, "said");
    snprintf(s->said, sizeof s->said, "%s", said ? said : "");

    const jv *doc = jget(e, "wish");
    if (doc && doc->srclen) {
        s->raw = malloc(doc->srclen + 1);
        if (s->raw) { memcpy(s->raw, doc->src, doc->srclen); s->raw[doc->srclen] = 0; s->rawlen = doc->srclen; }
    }
    const jv *v = doc ? jget(doc, "v") : NULL;
    if (v && v->type == J_INT && v->i > 1) { s->status = ST_NEWER; return s; }

    wish_env all = *env;
    all.sensors = ~0u;                   /* sensors are judged by store_recheck */
    all.said = s->said;
    wish_err err;
    if (doc && wish_parse(doc, &all, &s->w, &err) == 0) {
        s->ok = 1;
        const char *st = sval(e, "status");
        s->status = ST_ON;
        if (st && !strcmp(st, "off")) s->status = ST_OFF;
        else if (st && !strcmp(st, "paused:limit")) s->status = ST_LIMIT;
    } else if (doc && err.missing && strcmp(err.missing, "other") != 0) {
        s->status = ST_MISSING;          /* its program or look has gone */
    }
    return s;
}

int store_load(store *st, const char *path, const wish_env *env)
{
    memset(st, 0, sizeof *st);
    st->next = 1;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;                       /* no wishes yet */
    struct stat sb;
    char *buf = NULL;
    int ok = 0;
    if (fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode) && sb.st_size <= FILE_MAX &&
        (buf = malloc((size_t)sb.st_size + 1))) {
        ssize_t got = 0, k;
        while (got < sb.st_size && (k = read(fd, buf + got, (size_t)(sb.st_size - got))) > 0) got += k;
        ok = got == sb.st_size;
    }
    close(fd);

    static char mem[2 * 1024 * 1024];
    jarena a = { mem, sizeof mem, 0, NULL };
    static const jlimits L = { FILE_MAX, 8, 20000, 4096 };
    const jv *top = ok ? json_parse(&a, &L, buf, (size_t)sb.st_size) : NULL;
    const jv *list = top ? jget(top, "wishes") : NULL;
    const jv *v = top ? jget(top, "v") : NULL;
    if (!top || top->type != J_OBJ || !v || v->type != J_INT || v->i != 1 ||
        !list || list->type != J_ARR) {
        /* Unreadable. Keep it -- it is hers -- and start with nothing. */
        free(buf);
        char aside[700];
        snprintf(aside, sizeof aside, "%s.unreadable-%lld", path, (long long)time(NULL));
        rename(path, aside);
        return 1;
    }
    const jv *ao = jget(top, "all_off");
    st->all_off = ao && ao->type == J_BOOL && ao->i;
    st->consent = (int)ival(top, "consent", 0, 1000, 0);
    st->next = (int)ival(top, "next", 1, 1000000000, 1);
    int maxid = 0;
    for (const jv *e = list->kid; e && st->n < STORE_MAX; e = e->next) {
        if (e->type != J_OBJ) continue;
        stored *s = load_one(e, env);
        if (!s) break;
        int idn = s->id[0] ? atoi(s->id + 1) : 0;
        /* A missing or repeated id gets a new one: two wishes must never
         * answer to the same Stop button. */
        int dup = 0;
        for (int i = 0; i < st->n; i++) if (!strcmp(st->s[i]->id, s->id)) dup = 1;
        if (!s->id[0] || dup) { idn = 0; s->id[0] = 0; }
        if (idn > maxid) maxid = idn;
        st->s[st->n++] = s;
    }
    if (st->next <= maxid) st->next = maxid + 1;
    for (int i = 0; i < st->n; i++)
        if (!st->s[i]->id[0]) snprintf(st->s[i]->id, sizeof st->s[i]->id, "w%d", st->next++);
    const jv *base = jget(top, "baseline");
    if (base && base->type == J_ARR)
        for (const jv *b = base->kid; b && st->nbase < 8; b = b->next)
            if (store_parse_prev(b, env, &st->base[st->nbase]) == 0) st->nbase++;
    free(buf);
    return 0;
}

int store_recheck(store *st, const wish_env *env)
{
    int changed = 0;
    for (int i = 0; i < st->n; i++) {
        stored *s = st->s[i];
        if (!s->raw || (s->status != ST_ON && s->status != ST_MISSING && s->status != ST_LIMIT))
            continue;
        wish_env e = *env;
        e.said = s->said;
        wish w;
        wish_err err;
        int r = wish_parse_text(s->raw, s->rawlen, &e, &w, &err);
        if (r == 0) {
            s->w = w;
            s->ok = 1;
            if (s->status == ST_MISSING) { s->status = ST_ON; changed++; }
        } else if (s->status != ST_MISSING) {
            s->status = (err.missing && strcmp(err.missing, "other")) ? ST_MISSING : ST_BROKEN;
            changed++;
        }
    }
    return changed;
}

stored *store_find(store *st, const char *id)
{
    if (!id) return NULL;
    for (int i = 0; i < st->n; i++) if (!strcmp(st->s[i]->id, id)) return st->s[i];
    return NULL;
}

stored *store_add(store *st, const wish *w, const char *said, int by_voice, long long now)
{
    if (st->n >= STORE_MAX) return NULL;
    stored *s = fresh();
    if (!s) return NULL;
    snprintf(s->id, sizeof s->id, "w%d", st->next++);
    s->status = ST_ON;
    s->created_at = now;
    s->by_voice = by_voice;
    snprintf(s->said, sizeof s->said, "%s", said ? said : "");
    s->ok = 1;
    s->w = *w;
    char buf[8192];
    jout o = { buf, sizeof buf, 0, 0 };
    wish_json(w, &o);
    if (!o.bad && (s->raw = malloc(o.len + 1))) { memcpy(s->raw, buf, o.len + 1); s->rawlen = o.len; }
    st->s[st->n++] = s;
    return s;
}

int store_remove(store *st, const char *id)
{
    for (int i = 0; i < st->n; i++)
        if (!strcmp(st->s[i]->id, id)) {
            free(st->s[i]->raw);
            free(st->s[i]);
            memmove(&st->s[i], &st->s[i + 1], (size_t)(st->n - i - 1) * sizeof st->s[0]);
            st->n--;
            return 0;
        }
    return -1;
}

int store_save(const store *st, const char *path)
{
    size_t cap = FILE_MAX;
    char *buf = malloc(cap);
    if (!buf) return -1;
    jout o = { buf, cap, 0, 0 };
    jw_raw(&o, "{");
    jw_key(&o, "v"); jw_int(&o, 1);
    jw_key(&o, "all_off"); jw_raw(&o, st->all_off ? "true" : "false");
    jw_key(&o, "consent"); jw_int(&o, st->consent);
    jw_key(&o, "next"); jw_int(&o, st->next);
    jw_key(&o, "baseline"); jw_raw(&o, "[");
    for (int i = 0; i < st->nbase; i++) { if (i) jw_raw(&o, ","); wish_do_json(&st->base[i], &o); }
    jw_raw(&o, "]");
    jw_key(&o, "wishes"); jw_raw(&o, "[");
    for (int i = 0; i < st->n; i++) {
        const stored *s = st->s[i];
        if (i) jw_raw(&o, ",");
        jw_raw(&o, "\n{");
        jw_key(&o, "id"); jw_str(&o, s->id);
        jw_key(&o, "v"); jw_int(&o, 1);
        jw_key(&o, "status"); jw_str(&o, store_status_name(s->status));
        jw_key(&o, "created_at"); jw_int(&o, s->created_at);
        jw_key(&o, "created_by"); jw_str(&o, s->by_voice ? "voice" : "text");
        jw_key(&o, "last_ran"); jw_int(&o, s->last_ran);
        jw_key(&o, "paused_until"); jw_int(&o, s->paused_until);
        jw_key(&o, "runs"); jw_int(&o, s->runs);
        jw_key(&o, "notify"); jw_str(&o, s->runs < 3 ? "tell_me" : "quiet");
        jw_key(&o, "if_missed");
        jw_str(&o, s->ok && s->w.when.type == W_TIME && s->w.d[0].type == A_REMIND ? "when_back" : "skip");
        jw_key(&o, "keep"); jw_int(&o, s->keep);
        jw_key(&o, "keep_at"); jw_int(&o, s->keep_at);
        jw_key(&o, "said"); jw_str(&o, s->said);
        jw_key(&o, "wish");
        if (s->raw) jw_rawn(&o, s->raw, s->rawlen); else jw_raw(&o, "null");
        jw_raw(&o, "}");
    }
    jw_raw(&o, "\n]}\n");
    if (o.bad) { free(buf); return -1; }

    char tmp[700];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) { free(buf); return -1; }
    size_t done = 0;
    while (done < o.len) {
        ssize_t k = write(fd, buf + done, o.len - done);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; break; }
        done += (size_t)k;
    }
    free(buf);
    int bad = done != o.len || fsync(fd) != 0;
    if (close(fd) != 0) bad = 1;
    if (bad || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    /* The rename itself must reach the disk before we call it saved. */
    char dir[700];
    snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) {
        *sl = 0;
        int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) { fsync(dfd); close(dfd); }
    }
    return 0;
}
