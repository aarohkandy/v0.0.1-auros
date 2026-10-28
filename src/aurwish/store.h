/* store.h — wishes.json, her wishes on her disk (docs/AI.md §3.1, §7.8).
 *
 * ~/.local/share/auros/wishes.json, mode 0600, written through a
 * temporary file and rename(2) so a power cut leaves the old file or the
 * new one, never half of one. Anything in it is re-checked on every load
 * as if the cloud had just sent it: the file is in her home directory and
 * anything she runs can rewrite it.
 *
 * Nothing in it is ever silently dropped (§7.8). A wish this version
 * cannot read stays, verbatim, as "paused: needs a newer AurOS"; a file
 * that is not readable at all is moved aside, kept, and the engine starts
 * with no wishes rather than not at all. */
#ifndef AURWISH_STORE_H
#define AURWISH_STORE_H

#include "wish.h"

#define STORE_MAX 50

enum { ST_ON, ST_OFF, ST_LIMIT, ST_MISSING, ST_NEWER, ST_BROKEN };

typedef struct {
    char      id[12];          /* "w12" */
    int       status;
    long long paused_until;    /* ST_LIMIT: resumes by itself (§7.5) */
    long long created_at, last_ran;
    int       by_voice;
    int       runs;            /* notify: tell_me for the first 3 */
    int       keep;            /* 0 not asked, 1 card shown, 2 kept (§7.8) */
    long long keep_at;         /* when the card was shown */
    char      said[1024];      /* her words, for re-checking free text */
    int       ok;              /* `w` is valid */
    wish      w;
    char     *raw;             /* the document as found, when !ok */
    size_t    rawlen;
    /* Runtime only, never saved (engine.c). */
    struct {
        long long hist[6];     /* when its last actions ran (§7.5) */
        long long last_fire, last_overlay, every_base, seen_due;
        int armed, known;      /* edge triggers (§7.4) */
        int speak_day, speak_n, today_day, today_n;
        long long snooze_at;   /* Remind me in 10 minutes */
    } rt;
} stored;

typedef struct {
    int     all_off;
    int     consent;           /* version she agreed to; 0 = not asked/no */
    int     next;
    int     n;
    stored *s[STORE_MAX];
    /* The value each setting had before the first wish changed it, for
     * "Put everything back how it was" (§7.7). Resolved actions. */
    int     nbase;
    wish_do base[8];
} store;

/* The path, from XDG_DATA_HOME or HOME. */
int  store_path(char *out, size_t n);

/* Loads. Never fails in a way that stops the engine: returns 0 with an
 * empty store when there is nothing, 1 when the file was unreadable and
 * has been moved aside (the caller tells her once). env->sensors is
 * ignored here; store_recheck() applies it once sensors are known. */
int  store_load(store *st, const char *path, const wish_env *env);
/* 0 on success. Mode 0600, atomic. */
int  store_save(const store *st, const char *path);
void store_free(store *st);

/* §7.8: does each wish's program, look and sensor still exist? Moves
 * wishes between ST_ON and ST_MISSING. Returns how many changed. */
int  store_recheck(store *st, const wish_env *env);

stored *store_find(store *st, const char *id);
/* Adds a validated wish. NULL when full. */
stored *store_add(store *st, const wish *w, const char *said, int by_voice, long long now);
int  store_remove(store *st, const char *id);

const char *store_status_name(int st);

/* Parse one resolved action (a `prev` from the shell, or a baseline).
 * Only the settings Undo can put back are accepted. */
int  store_parse_prev(const jv *o, const wish_env *env, wish_do *d);

#endif
