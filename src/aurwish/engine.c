/* engine.c — see engine.h. The rules of docs/AI.md §3.3 and §7.4-§7.8
 * live here, each with a test in tools/wishtest-engine.c. */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine.h"
#include "text.h"

#define HOUR            3600
#define PER_WISH_HOUR   6          /* §7.5 */
#define OVERALL_HOUR    20         /* §7.5 */
#define PAUSE_FOR       HOUR       /* §7.5: resumes by itself */
#define OVERLAY_GAP     60         /* §7.4: per wish */
#define SENSOR_GAP      600        /* §7.4: sensor and idle */
#define TYPING_QUIET    3          /* §7.6: "after 3 s with no typing" */
#define SPEAK_PER_DAY   3          /* §7.4 */
#define REMIND_PER_DAY  4          /* §7.4 */
#define TRIGGER_WORDS   10         /* §7.6 */
#define HELD_FOR        600        /* a held card or sound is dropped after 10 min */
#define REMIND_HELD_FOR (12 * HOUR) /* §7.8: a reminder waits 12 hours */
#define UNDO_FOR        600        /* §7.7 */
#define PENDING_FOR     600
#define KEEP_ASK_AFTER  (24 * HOUR)
#define KEEP_GIVE_UP    (30 * 24 * HOUR)

enum { F_TYPED = 1, F_APP = 2, F_TEST = 4 };

typedef struct {
    char wid[12];
    long long at;
    unsigned mask;             /* which of its actions are waiting */
    int flags;
    char missed_for[16];
} held;

typedef struct {
    char id[8];
    wish w;
    char said[1024];
    int voice;
    long long at;
    int used;
} pending;

typedef struct { long long n; int undo_slot; long long at; } shell_ev;

struct engine {
    store *st;
    const char *path;
    wish_env *env;
    engine_io io;
    /* §7.5's overall hour, remembering whose actions they were: a paused
     * wish's share is given back, so pausing the one that broke the
     * limit is enough and the others carry on. */
    struct { long long t; char wid[12]; } overall[OVERALL_HOUR];
    unsigned holds, lholds;
    long long last_input;
    int dnd;
    long long dnd_until;       /* 0 = until turned off */
    int bat_known, bat_pct, bat_plugged;
    int have_heading; double heading;
    char orient[16];
    int have_lux; double lux;
    held q[32];
    int nq;
    pending pend[4];
    int pnext;
    shell_ev sev[16];
    long long evn;
    /* §7.7 Undo my last wish: what the last `now` wish changed. */
    long long undo_at;
    int nundo;
    wish_do undo[8];
    int undo_wait;             /* shell changes whose "did" has not come */
};

/* ── small things ────────────────────────────────────────────────── */

static void save(engine *e)
{
    if (e->path) store_save(e->st, e->path);
}

static int day_of(long long t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    return tm.tm_year * 400 + tm.tm_yday;
}

static int quiet(long long now)
{
    time_t tt = (time_t)now;
    struct tm tm;
    localtime_r(&tt, &tm);
    return tm.tm_hour >= 21 || tm.tm_hour < 8;
}

static int emit(engine *e, const char *line) { return e->io.emit ? e->io.emit(e->io.ctx, line) : 0; }

static void notice(engine *e, const char *text)
{
    char b[1200];
    jout o = { b, sizeof b, 0, 0 };
    jw_raw(&o, "{"); jw_key(&o, "ev"); jw_str(&o, "notice"); jw_key(&o, "text"); jw_str(&o, text);
    jw_raw(&o, "}");
    if (!o.bad) emit(e, b);
}

static void changed(engine *e)
{
    emit(e, "{\"ev\":\"changed\"}");
    char t[1024];
    engine_triggers_line(e, t, sizeof t);
    emit(e, t);
}

static void title_of(const stored *s, char *out, size_t n)
{
    if (s->ok) wish_title(&s->w, out, n);
    else snprintf(out, n, "%s", "A wish");
}

/* ── doing things ─────────────────────────────────────────────────── */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Records what a `now` wish is about to change, for Undo (§7.7) and for
 * "Put everything back how it was" (the first value ever seen). */
static void remember(engine *e, const wish_do *prev, int for_undo)
{
    if (for_undo && e->nundo < 8) e->undo[e->nundo++] = *prev;
    for (int i = 0; i < e->st->nbase; i++)
        if (e->st->base[i].type == prev->type) return;
    if (e->st->nbase < 8) e->st->base[e->st->nbase++] = *prev;
}

static void shell_do(engine *e, const stored *s, const wish_do *d, int for_undo)
{
    char b[2048], t[96];
    jout o = { b, sizeof b, 0, 0 };
    long long n = ++e->evn;
    jw_raw(&o, "{"); jw_key(&o, "ev"); jw_str(&o, "shell");
    jw_key(&o, "n"); jw_int(&o, n);
    if (s) {
        title_of(s, t, sizeof t);
        jw_key(&o, "wid"); jw_str(&o, s->id);
        jw_key(&o, "title"); jw_str(&o, t);
    }
    jw_key(&o, "do"); wish_do_json(d, &o);
    jw_raw(&o, "}");
    if (o.bad) return;
    shell_ev *slot = &e->sev[n % 16];
    slot->n = n;
    slot->undo_slot = for_undo;
    slot->at = 0;
    if (emit(e, b) > 0 && for_undo) e->undo_wait++;
}

static void overlay(engine *e, stored *s, const wish_do *d, const char *missed_for)
{
    char b[2048], t[96];
    jout o = { b, sizeof b, 0, 0 };
    title_of(s, t, sizeof t);
    jw_raw(&o, "{");
    jw_key(&o, "ev");
    jw_str(&o, d->type == A_FILL ? "fill" : d->type == A_MESSAGE ? "message" : "remind");
    jw_key(&o, "n"); jw_int(&o, ++e->evn);
    jw_key(&o, "wid"); jw_str(&o, s->id);
    jw_key(&o, "title"); jw_str(&o, t);
    if (d->type == A_FILL) {
        if (d->emoji[0]) { jw_key(&o, "emoji"); jw_str(&o, d->emoji); }
        else { jw_key(&o, "text"); jw_str(&o, d->text); }
        jw_key(&o, "seconds"); jw_int(&o, d->seconds);
    } else {
        jw_key(&o, "text"); jw_str(&o, d->text);
    }
    if (d->type == A_REMIND) {
        if (missed_for && *missed_for) { jw_key(&o, "for"); jw_str(&o, missed_for); }
        jw_key(&o, "private"); jw_raw(&o, d->priv ? "true" : "false");
    }
    jw_key(&o, "tell"); jw_raw(&o, s->runs <= 3 ? "true" : "false");
    jw_raw(&o, "}");
    if (!o.bad) emit(e, b);
}

static void say_time(engine *e, long long now)
{
    time_t tt = (time_t)now;
    struct tm tm;
    localtime_r(&tt, &tm);
    int h12 = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
    char t[64];
    snprintf(t, sizeof t, "It is %d:%02d %s.", h12, tm.tm_min, tm.tm_hour < 12 ? "am" : "pm");
    if (e->io.say) e->io.say(e->io.ctx, t);
}

/* One action, now. `s` is NULL for an undo or a restore. */
static void run_one(engine *e, stored *s, const wish_do *d, const char *missed_for,
                    int for_undo, long long now)
{
    wish_do prev;
    memset(&prev, 0, sizeof prev);
    switch (d->type) {
    case A_FILL: case A_MESSAGE: case A_REMIND:
        if (s) overlay(e, s, d, missed_for);
        break;
    case A_SOUND:
        if (e->io.sound) e->io.sound(e->io.ctx, d->sound);
        break;
    case A_SPEAK:
        if (e->io.say) e->io.say(e->io.ctx, d->text);
        break;
    case A_SPEAK_TIME:
        say_time(e, now);
        break;
    case A_BRIGHT: {
        int cur = e->io.bright_get ? e->io.bright_get(e->io.ctx) : -1;
        if (!d->is_set && cur < 0) break;          /* a change of an unknown */
        int want = d->is_set ? d->pct : cur + d->pct;
        /* The floor is a wish's (30 %); an undo puts back hers. */
        if (s) want = clampi(want, 30, 100);
        if (cur >= 0 && s) {
            prev.type = A_BRIGHT; prev.is_set = 1; prev.pct = clampi(cur, 5, 100);
            remember(e, &prev, for_undo);
        }
        if (e->io.bright_set) e->io.bright_set(e->io.ctx, want);
        break;
    }
    case A_VOLUME: {
        int cur = e->io.vol_get ? e->io.vol_get(e->io.ctx) : -1;
        if (!d->is_set && cur < 0) break;
        int want = d->is_set ? d->pct : cur + d->pct;
        if (s) want = clampi(want, 20, 80);
        if (cur >= 0 && s) {
            prev.type = A_VOLUME; prev.is_set = 1; prev.pct = clampi(cur, 0, 100);
            remember(e, &prev, for_undo);
        }
        if (e->io.vol_set) e->io.vol_set(e->io.ctx, want);
        break;
    }
    case A_DND:
        e->dnd = d->on;
        e->dnd_until = 0;
        if (d->on && d->has_until) {
            time_t tt = (time_t)now;
            struct tm tm;
            localtime_r(&tt, &tm);
            tm.tm_hour = d->until_hh; tm.tm_min = d->until_mm; tm.tm_sec = 0; tm.tm_isdst = -1;
            long long u = (long long)mktime(&tm);
            if (u <= now) { tm.tm_mday++; tm.tm_isdst = -1; u = (long long)mktime(&tm); }
            e->dnd_until = u;
        }
        shell_do(e, s, d, 0);
        break;
    default:            /* look, text, pointer, wallpaper, night, open_app, open_website */
        shell_do(e, s, d, for_undo);
        break;
    }
}

static int held_kind(int t) { return wish_do_overlay(t) || wish_do_sound(t) || wish_do_outward(t); }

static int may_interrupt(engine *e, long long now)
{
    if (e->holds || e->lholds || e->dnd) return 0;
    if (e->last_input && now - e->last_input < TYPING_QUIET) return 0;
    return 1;
}

/* Runs the actions in `mask`, or holds the ones that would interrupt. */
static void run_mask(engine *e, stored *s, unsigned mask, int flags, const char *missed_for,
                     long long now)
{
    unsigned later = 0;
    for (int i = 0; i < s->w.n; i++) {
        if (!(mask & (1u << i))) continue;
        const wish_do *d = &s->w.d[i];
        /* §7.4 quiet hours: sound and speech always; cards unless she
         * just did the thing (typed the word, opened the program), or it
         * is the reminder she set for this time. A button press (test)
         * is her doing it too. */
        if (quiet(now) && !(flags & F_TEST)) {
            int remind = d->type == A_REMIND && s->w.when.type == W_TIME;
            if (wish_do_sound(d->type)) continue;
            if (wish_do_overlay(d->type) && !(flags & (F_TYPED | F_APP)) && !remind) continue;
        }
        if (held_kind(d->type) && !may_interrupt(e, now)) { later |= 1u << i; continue; }
        run_one(e, s, d, missed_for, 0, now);
    }
    if (later && e->nq < (int)(sizeof e->q / sizeof e->q[0])) {
        held *h = &e->q[e->nq++];
        memset(h, 0, sizeof *h);
        snprintf(h->wid, sizeof h->wid, "%s", s->id);
        h->at = now;
        h->mask = later;
        h->flags = flags;
        snprintf(h->missed_for, sizeof h->missed_for, "%s", missed_for ? missed_for : "");
    }
}

static void pause_for_limit(engine *e, stored *s, long long now)
{
    for (int i = 0; i < OVERALL_HOUR; i++)
        if (!strcmp(e->overall[i].wid, s->id)) { e->overall[i].t = 0; e->overall[i].wid[0] = 0; }
    s->status = ST_LIMIT;
    s->paused_until = now + PAUSE_FOR;
    save(e);
    /* Told once: a paused wish does not fire, so cannot pause again
     * until it is back. */
    char t[96], line[300];
    title_of(s, t, sizeof t);
    snprintf(line, sizeof line,
             "A wish was happening very often, so it is resting for an hour: \xE2\x80\x9C%s\xE2\x80\x9D.", t);
    notice(e, line);
    changed(e);
}

/* A wish's trigger happened. Everything about whether it may act is
 * decided here, in one place. */
static void fire(engine *e, stored *s, int flags, const char *missed_for, long long now)
{
    if (!s->ok || e->st->all_off) return;
    if (s->status != ST_ON && !(flags & F_TEST)) return;
    const wish *w = &s->w;

    int has_overlay = 0, speaks = 0;
    for (int i = 0; i < w->n; i++) {
        has_overlay |= wish_do_overlay(w->d[i].type);
        speaks |= w->d[i].type == A_SPEAK || w->d[i].type == A_SPEAK_TIME;
    }
    /* §7.4: at least a minute between one wish's cards. */
    if (has_overlay && s->rt.last_overlay && now - s->rt.last_overlay < OVERLAY_GAP) return;
    /* §7.4: three spoken a day per wish. */
    int today = day_of(now);
    if (speaks) {
        if (s->rt.speak_day != today) { s->rt.speak_day = today; s->rt.speak_n = 0; }
        if (s->rt.speak_n >= SPEAK_PER_DAY) return;
    }

    /* §7.5 limits: 6 actions an hour per wish, 20 overall; the reminder
     * she set for a time is not counted overall. Only the wish that
     * would break a limit is paused. */
    int k = w->n, mine = 0, all = 0;
    for (int i = 0; i < PER_WISH_HOUR; i++) if (s->rt.hist[i] && now - s->rt.hist[i] < HOUR) mine++;
    int exempt = w->when.type == W_TIME && w->d[0].type == A_REMIND && w->n == 1;
    for (int i = 0; i < OVERALL_HOUR; i++) if (e->overall[i].t && now - e->overall[i].t < HOUR) all++;
    if (mine + k > PER_WISH_HOUR || (!exempt && all + k > OVERALL_HOUR)) {
        pause_for_limit(e, s, now);
        return;
    }
    for (int j = 0; j < k; j++) {
        /* replace the oldest */
        int o = 0;
        for (int i = 1; i < PER_WISH_HOUR; i++) if (s->rt.hist[i] < s->rt.hist[o]) o = i;
        s->rt.hist[o] = now;
        if (!exempt) {
            /* the oldest slot, not the next one: a released slot is reused */
            int old = 0;
            for (int i = 1; i < OVERALL_HOUR; i++) if (e->overall[i].t < e->overall[old].t) old = i;
            e->overall[old].t = now;
            snprintf(e->overall[old].wid, sizeof e->overall[old].wid, "%s", s->id);
        }
    }
    if (speaks) s->rt.speak_n++;
    if (has_overlay) s->rt.last_overlay = now;
    if (s->rt.today_day != today) { s->rt.today_day = today; s->rt.today_n = 0; }
    s->rt.today_n++;
    s->rt.last_fire = now;
    s->runs++;
    s->last_ran = now;
    save(e);
    run_mask(e, s, (1u << w->n) - 1, flags, missed_for, now);
}

/* ── time ─────────────────────────────────────────────────────────── */

/* The most recent moment this `time` wish was due, at or before now. */
static long long last_due(const wish_when *w, long long now)
{
    time_t tt = (time_t)now;
    struct tm base;
    localtime_r(&tt, &base);
    for (int back = 0; back < 8; back++) {
        struct tm tm = base;
        tm.tm_mday -= back;
        tm.tm_hour = w->hh; tm.tm_min = w->mm; tm.tm_sec = 0; tm.tm_isdst = -1;
        time_t due = mktime(&tm);       /* local wall time, so it follows DST */
        if (due == (time_t)-1 || due > tt) continue;
        int wd = (tm.tm_wday + 6) % 7;  /* Monday = 0 */
        if (w->days & (1u << wd)) return (long long)due;
    }
    return 0;
}

static void tick_time(engine *e, stored *s, long long now)
{
    long long due = last_due(&s->w.when, now);
    if (!due || due <= s->rt.seen_due || due <= s->last_ran || due < s->created_at) return;
    s->rt.seen_due = due;
    int remind = s->w.d[0].type == A_REMIND;
    if (now - due < 90) { fire(e, s, 0, NULL, now); return; }
    /* Missed (the computer was off or asleep). §7.8: a reminder still
     * shows, within 12 hours, saying when it was for; nothing else
     * catches up. */
    if (remind && now - due <= REMIND_HELD_FOR) {
        char f[16];
        int h = s->w.when.hh, h12 = h % 12 ? h % 12 : 12;
        snprintf(f, sizeof f, "%d:%02d %s", h12, s->w.when.mm, h < 12 ? "am" : "pm");
        fire(e, s, 0, f, now);
    }
}

void engine_tick(engine *e, long long now)
{
    store *st = e->st;
    if (e->dnd && e->dnd_until && now >= e->dnd_until) { e->dnd = 0; e->dnd_until = 0; }
    int ch = 0;
    for (int i = 0; i < st->n; i++) {
        stored *s = st->s[i];
        /* §7.5: a paused wish comes back by itself after an hour. */
        if (s->status == ST_LIMIT && now >= s->paused_until) {
            s->status = ST_ON; s->paused_until = 0;
            memset(s->rt.hist, 0, sizeof s->rt.hist);
            ch = 1;
        }
        if (!s->ok || s->status != ST_ON) continue;
        const wish_when *w = &s->w.when;
        if (s->rt.snooze_at && now >= s->rt.snooze_at) {
            s->rt.snooze_at = 0;
            run_mask(e, s, 1, F_TEST, NULL, now);
        }
        if (w->type == W_TIME) tick_time(e, s, now);
        else if (w->type == W_EVERY) {
            if (!s->rt.every_base) s->rt.every_base = now;
            if (now - s->rt.every_base >= (long long)w->minutes * 60) {
                s->rt.every_base = now;
                fire(e, s, 0, NULL, now);
            }
        } else if (w->type == W_IDLE && e->last_input) {
            int idle = now - e->last_input >= (long long)w->minutes * 60;
            if (!idle) s->rt.armed = 1;
            else if (s->rt.armed && (!s->rt.last_fire || now - s->rt.last_fire >= SENSOR_GAP)) {
                s->rt.armed = 0;
                fire(e, s, 0, NULL, now);
            }
        }
        /* §7.8 "Keep it?" the day after; off after 30 days unanswered,
         * except a reminder. Only marked asked if a shell saw it. */
        if (w->type != W_NOW && s->keep == 0 && s->created_at && now - s->created_at >= KEEP_ASK_AFTER) {
            char b[1600], t[96], sen[1024];
            title_of(s, t, sizeof t);
            wish_sentence(&s->w, sen, sizeof sen);
            jout o = { b, sizeof b, 0, 0 };
            jw_raw(&o, "{"); jw_key(&o, "ev"); jw_str(&o, "keep_it");
            jw_key(&o, "wid"); jw_str(&o, s->id); jw_key(&o, "title"); jw_str(&o, t);
            jw_key(&o, "sentence"); jw_str(&o, sen); jw_raw(&o, "}");
            if (!o.bad && emit(e, b) > 0) { s->keep = 1; s->keep_at = now; save(e); }
        } else if (s->keep == 1 && now - s->keep_at >= KEEP_GIVE_UP &&
                   !(w->type == W_TIME && s->w.d[0].type == A_REMIND)) {
            s->status = ST_OFF;
            ch = 1;
        }
    }
    /* Held cards and sounds (§7.6): run when nothing is in the way;
     * dropped when they have waited too long to still make sense. */
    for (int i = 0; i < e->nq; ) {
        held *h = &e->q[i];
        stored *s = store_find(st, h->wid);
        int remind = s && s->ok && s->w.when.type == W_TIME && s->w.d[0].type == A_REMIND;
        long long life = remind ? REMIND_HELD_FOR : HELD_FOR;
        if (!s || !s->ok || st->all_off || (s->status != ST_ON && !(h->flags & F_TEST)) ||
            now - h->at > life) {
            e->q[i] = e->q[--e->nq];
            continue;
        }
        if (may_interrupt(e, now)) {
            held copy = *h;
            e->q[i] = e->q[--e->nq];
            run_mask(e, s, copy.mask, copy.flags, copy.missed_for, now);
            continue;
        }
        i++;
    }
    if (ch) { save(e); changed(e); }
}

/* ── sources ──────────────────────────────────────────────────────── */

void engine_startup(engine *e, long long now)
{
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (s->ok && s->w.when.type == W_STARTUP) fire(e, s, 0, NULL, now);
    }
}

void engine_typed(engine *e, const char *word, long long now)
{
    char w[33];
    if (!word || word_rule(word, w)) return;
    /* §7.6: a word typed while a password is being asked for is not
     * held for later, it is dropped -- a potato appearing after the
     * dialog closes would say what she typed into it. */
    if ((e->holds | e->lholds) & H_PASSWORD) return;
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (s->ok && s->w.when.type == W_TYPED && !strcmp(s->w.when.word, w))
            fire(e, s, F_TYPED, NULL, now);
    }
}

static int app_match(const wish_app *a, const char *id)
{
    const char *d = strrchr(a->id, '.');
    const char *g = strrchr(id, '.');
    return text_eq(a->id, id) || (a->wmclass[0] && text_eq(a->wmclass, id)) ||
           text_eq(d ? d + 1 : a->id, g ? g + 1 : id);
}

void engine_app(engine *e, const char *app, long long now)
{
    if (!app || !*app || strlen(app) > 127) return;
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (s->ok && s->w.when.type == W_APP && app_match(&s->w.when.app, app))
            fire(e, s, F_APP, NULL, now);
    }
}

void engine_input(engine *e, long long now) { e->last_input = now; }

void engine_hold(engine *e, unsigned reasons, long long now) { (void)now; e->holds = reasons; }
void engine_hold_local(engine *e, unsigned reasons) { e->lholds = reasons; }

void engine_battery(engine *e, int present, int pct, int plugged, long long now)
{
    if (!present || pct < 0) return;
    int was_known = e->bat_known, was_plugged = e->bat_plugged;
    e->bat_known = 1; e->bat_pct = pct; e->bat_plugged = plugged;
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (!s->ok) continue;
        const wish_when *w = &s->w.when;
        if (w->type == W_PLUGGED && was_known && plugged && !was_plugged) fire(e, s, 0, NULL, now);
        if (w->type == W_UNPLUGGED && was_known && !plugged && was_plugged) fire(e, s, 0, NULL, now);
        if (w->type == W_BATTERY) {
            /* On the edge, with 3 points of hysteresis: 19-20-19-20 is
             * one warning, not four. */
            int below = !plugged && pct < w->percent;
            if (!s->rt.known) { s->rt.known = 1; s->rt.armed = !below; continue; }
            if (below && s->rt.armed) { s->rt.armed = 0; fire(e, s, 0, NULL, now); }
            else if (plugged || pct >= w->percent + 3) s->rt.armed = 1;
        }
    }
}

void engine_usb(engine *e, long long now)
{
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (s->ok && s->w.when.type == W_USB) fire(e, s, 0, NULL, now);
    }
}

/* Edge with hysteresis for the three sensors: `in` is the condition,
 * `out` is "clearly no longer", which is wider than !in. */
static void sensor_edge(engine *e, stored *s, int in, int out, long long now)
{
    if (!s->rt.known) { s->rt.known = 1; s->rt.armed = !in; return; }
    if (in && s->rt.armed) {
        s->rt.armed = 0;
        if (!s->rt.last_fire || now - s->rt.last_fire >= SENSOR_GAP) fire(e, s, 0, NULL, now);
    } else if (out) s->rt.armed = 1;
}

void engine_compass(engine *e, double heading, long long now)
{
    if (!isfinite(heading)) return;
    heading = fmod(fmod(heading, 360.0) + 360.0, 360.0);
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (!s->ok || s->w.when.type != W_SENSOR || s->w.when.sensor != S_COMPASS) continue;
        double target = 90.0 * s->w.when.heading;
        double d = fabs(fmod(heading - target + 540.0, 360.0) - 180.0);
        sensor_edge(e, s, d <= s->w.when.tol, d > s->w.when.tol + 5, now);
    }
}

void engine_tilt(engine *e, const char *o, long long now)
{
    static const char *const MAP[][2] = { { "normal", "normal" }, { "left-up", "left" },
                                          { "right-up", "right" }, { "bottom-up", "upside_down" } };
    static const char *const ORIENT[] = { "normal", "left", "right", "upside_down" };
    const char *mine = NULL;
    for (int i = 0; i < 4; i++) if (o && !strcmp(o, MAP[i][0])) mine = MAP[i][1];
    if (!mine) return;                  /* "undefined": lying flat */
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (!s->ok || s->w.when.type != W_SENSOR || s->w.when.sensor != S_TILT) continue;
        int in = !strcmp(ORIENT[s->w.when.orient], mine);
        sensor_edge(e, s, in, !in, now);
    }
}

void engine_light(engine *e, double lux, long long now)
{
    if (!isfinite(lux) || lux < 0) return;
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (!s->ok || s->w.when.type != W_SENSOR || s->w.when.sensor != S_LIGHT) continue;
        double t = s->w.when.lux;
        if (s->w.when.lux_above) sensor_edge(e, s, lux > t, lux < t * 0.8 - 2, now);
        else sensor_edge(e, s, lux < t, lux > t * 1.25 + 2, now);
    }
}

void engine_sensors(engine *e, unsigned have)
{
    e->env->sensors = have;
    if (store_recheck(e->st, e->env)) { save(e); changed(e); }
}

/* ── the requests ─────────────────────────────────────────────────── */

const char *engine_cannot_text(const char *m)
{
    /* Every sentence she can be shown about a refusal is here, written
     * for her (docs/EASY.md rule 8): what happened, never why in
     * technical words, and what she can do next. */
    static const struct { const char *m, *t; } T[] = {
        { "compass", "This computer has no compass, so it cannot tell which way it is pointing." },
        { "tilt", "This computer cannot tell which way up it is being held." },
        { "light", "This computer has no light sensor, so it cannot tell how bright the room is." },
        { "app", "I could not find that program on this computer." },
        { "theme", "I could not find that look on this computer." },
        { "site", "For now I can only open well-known websites from a short list." },
        { "install", "A wish cannot add programs. You can use Get more programs for that." },
        { "duplicate", "You already have that wish." },
        { "limit", "You have as many wishes of that kind as you can have. You can remove one in My wishes." },
        { "unclear", "I did not quite understand. Could you say it another way?" },
        { "question", "That sounds like a question rather than a wish. You can ask it with Ask AurOS." },
        { "files", "A wish cannot change or look at your files." },
        { "messages", "A wish cannot send messages, e-mails or make calls." },
        { "money", "A wish cannot spend or move money." },
        { "private", "A wish cannot use your passwords or personal details." },
        { "security", "A wish cannot change the settings that keep this computer safe." },
        { NULL, "I can't do that wish yet." } };
    int i = 0;
    while (T[i].m && (!m || strcmp(T[i].m, m))) i++;
    return T[i].t;
}

static void reply_kind(char *r, size_t n, long long id, const char *kind)
{
    snprintf(r, n, "{\"id\":%lld,\"kind\":\"%s\"}", id, kind);
}

static void reply_cannot(char *r, size_t n, long long id, const char *missing)
{
    jout o = { r, n, 0, 0 };
    r[0] = 0;
    jw_raw(&o, "{"); jw_key(&o, "id"); jw_int(&o, id);
    jw_key(&o, "kind"); jw_str(&o, "cannot");
    jw_key(&o, "missing"); jw_str(&o, missing ? missing : "other");
    jw_key(&o, "text"); jw_str(&o, engine_cannot_text(missing));
    jw_raw(&o, "}");
}

static void reply_error(char *r, size_t n, long long id, const char *err)
{
    snprintf(r, n, "{\"id\":%lld,\"kind\":\"error\",\"error\":\"%s\"}", id, err);
}

static int count_words(engine *e, const char *add)
{
    int n = 0, dup = 0;
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (!s->ok || s->w.when.type != W_TYPED || s->status == ST_OFF) continue;
        n++;
        if (add && !strcmp(add, s->w.when.word)) dup = 1;
    }
    return n + (add && !dup ? 1 : 0);
}

/* A wish she is about to be asked about: every device-side rule that
 * needs her other wishes (§7.4, §7.6, §7.11). NULL when it may be
 * offered. */
static const char *fits(engine *e, const wish *w)
{
    char a[8192], b[8192];
    jout oa = { a, sizeof a, 0, 0 };
    wish_json(w, &oa);
    if (w->when.type != W_NOW) {
        if (e->st->n >= STORE_MAX) return "limit";
        for (int i = 0; i < e->st->n; i++) {
            stored *s = e->st->s[i];
            if (!s->ok) continue;
            jout ob = { b, sizeof b, 0, 0 };
            wish_json(&s->w, &ob);
            if (!oa.bad && !ob.bad && !strcmp(a, b)) return "duplicate";
        }
    }
    if (w->when.type == W_TYPED && count_words(e, w->when.word) > TRIGGER_WORDS) return "limit";
    if (w->when.type == W_TIME && w->d[0].type == A_REMIND) {
        for (int day = 0; day < 7; day++) {
            if (!(w->when.days & (1u << day))) continue;
            int n = 1;
            for (int i = 0; i < e->st->n; i++) {
                stored *s = e->st->s[i];
                if (s->ok && s->status != ST_OFF && s->w.when.type == W_TIME &&
                    s->w.d[0].type == A_REMIND && (s->w.when.days & (1u << day))) n++;
            }
            if (n > REMIND_PER_DAY) return "limit";
        }
    }
    return NULL;
}

/* Registers `w` as waiting for her Yes and writes its fields (pending,
 * sentence, title, outward) into `o`. Returns 0, or -1 with a reason
 * in *why. */
static int offer_fields(engine *e, const wish *w, const char *said, int voice,
                        long long now, jout *o, const char **why)
{
    *why = fits(e, w);
    if (*why) return -1;
    pending *p = &e->pend[e->pnext++ % 4];
    memset(p, 0, sizeof *p);
    snprintf(p->id, sizeof p->id, "p%d", e->pnext);
    p->w = *w;
    snprintf(p->said, sizeof p->said, "%s", said ? said : "");
    p->voice = voice;
    p->at = now;
    char sen[1024], t[96];
    wish_sentence(w, sen, sizeof sen);
    wish_title(w, t, sizeof t);
    jw_key(o, "pending"); jw_str(o, p->id);
    jw_key(o, "sentence"); jw_str(o, sen);
    jw_key(o, "title"); jw_str(o, t);
    for (int i = 0; i < w->n; i++) {
        const wish_do *d = &w->d[i];
        if (!wish_do_outward(d->type)) continue;
        /* §7.2: the destination, large, on its own Yes screen. */
        jw_key(o, "outward"); jw_raw(o, "{");
        jw_key(o, "name"); jw_str(o, d->type == A_OPEN_APP ? d->app.label : d->site_name);
        if (d->type == A_OPEN_SITE) {
            char host[128];
            const char *h = strstr(d->url, "://");
            h = h ? h + 3 : d->url;
            snprintf(host, sizeof host, "%.*s", (int)strcspn(h, "/?#"), h);
            jw_key(o, "host"); jw_str(o, host);
        }
        jw_raw(o, "}");
    }
    return 0;
}

static const char *const MISSING[] = { "unclear", "question", "compass", "tilt", "light", "install",
                                       "files", "messages", "money", "private", "security", "app",
                                       "theme", "site", "duplicate", "limit", "other", NULL };

/* The cloud's `missing` is data like everything else: only a word we
 * know becomes a sentence. */
static const char *known_missing(const char *m)
{
    for (int i = 0; m && MISSING[i]; i++) if (!strcmp(m, MISSING[i])) return MISSING[i];
    return "other";
}

static void cannot_head(jout *o, long long id, const char *missing)
{
    jw_raw(o, "{"); jw_key(o, "id"); jw_int(o, id);
    jw_key(o, "kind"); jw_str(o, "cannot");
    jw_key(o, "missing"); jw_str(o, missing);
    jw_key(o, "text"); jw_str(o, engine_cannot_text(missing));
}

void engine_offer(engine *e, long long id, int how, const char *tag, const jv *doc,
                  const char *said, int voice, char *r, size_t n, long long now)
{
    wish w;
    wish_err err;
    wish_env env = *e->env;
    env.said = said;
    memset(&err, 0, sizeof err);
    int have = doc && wish_parse(doc, &env, &w, &err) == 0;
    jout o = { r, n, 0, 0 };
    r[0] = 0;
    const char *why = NULL;

    if (how == OFFER_WISH) {
        if (!have) {
            cannot_head(&o, id, known_missing(err.missing));
            jw_raw(&o, "}");
        } else {
            jw_raw(&o, "{"); jw_key(&o, "id"); jw_int(&o, id);
            jw_key(&o, "kind"); jw_str(&o, "wish");
            if (offer_fields(e, &w, said, voice, now, &o, &why) < 0) {
                o.len = 0; o.bad = 0; r[0] = 0;
                cannot_head(&o, id, why);
            }
            jw_raw(&o, "}");
        }
    } else if (how == OFFER_CANNOT) {
        cannot_head(&o, id, known_missing(tag));
        if (have) {
            size_t keep = o.len;
            jw_key(&o, "alt"); jw_raw(&o, "{");
            if (offer_fields(e, &w, said, voice, now, &o, &why) == 0) jw_raw(&o, "}");
            else { o.len = keep; r[keep] = 0; }
        }
        jw_raw(&o, "}");
    } else {
        jw_raw(&o, "{"); jw_key(&o, "id"); jw_int(&o, id);
        jw_key(&o, "kind"); jw_str(&o, "help");
        jw_key(&o, "page");
        if (tag) jw_str(&o, tag); else jw_raw(&o, "null");
        if (have) {
            size_t keep = o.len;
            if (offer_fields(e, &w, said, voice, now, &o, &why) < 0) { o.len = keep; r[keep] = 0; }
        }
        jw_raw(&o, "}");
    }
    if (o.bad) reply_error(r, n, id, "broken");
}

static pending *find_pending(engine *e, const char *pid, long long now)
{
    for (int i = 0; i < 4; i++) {
        pending *p = &e->pend[i];
        if (p->id[0] && pid && !strcmp(p->id, pid) && !p->used && now - p->at < PENDING_FOR) return p;
    }
    return NULL;
}

static void list_reply(engine *e, long long id, char *r, size_t n, long long now)
{
    jout o = { r, n, 0, 0 };
    r[0] = 0;
    jw_raw(&o, "{"); jw_key(&o, "id"); jw_int(&o, id);
    jw_key(&o, "kind"); jw_str(&o, "list");
    jw_key(&o, "all_off"); jw_raw(&o, e->st->all_off ? "true" : "false");
    jw_key(&o, "undo"); jw_raw(&o, e->nundo && now - e->undo_at < UNDO_FOR ? "true" : "false");
    jw_key(&o, "wishes"); jw_raw(&o, "[");
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        char sen[1024], t[96];
        if (s->ok) { wish_sentence(&s->w, sen, sizeof sen); wish_title(&s->w, t, sizeof t); }
        else if (s->status == ST_NEWER) {
            snprintf(sen, sizeof sen, "%s", "This wish needs a newer AurOS.");
            snprintf(t, sizeof t, "%s", "A wish");
        } else {
            snprintf(sen, sizeof sen, "%s", s->status == ST_MISSING
                     ? "This wish is paused because something it needs is no longer on this computer."
                     : "This wish could not be read, so it is paused.");
            snprintf(t, sizeof t, "%s", "A wish");
        }
        if (i) jw_raw(&o, ",");
        jw_raw(&o, "{");
        jw_key(&o, "wid"); jw_str(&o, s->id);
        jw_key(&o, "title"); jw_str(&o, t);
        jw_key(&o, "sentence"); jw_str(&o, sen);
        jw_key(&o, "status"); jw_str(&o, store_status_name(s->status));
        jw_key(&o, "last_ran"); jw_int(&o, s->last_ran);
        jw_raw(&o, "}");
    }
    jw_raw(&o, "]}");
    if (o.bad) reply_error(r, n, id, "broken");
}

static void today_reply(engine *e, long long id, char *r, size_t n, long long now)
{
    jout o = { r, n, 0, 0 };
    r[0] = 0;
    int today = day_of(now), first = 1;
    jw_raw(&o, "{"); jw_key(&o, "id"); jw_int(&o, id);
    jw_key(&o, "kind"); jw_str(&o, "today");
    jw_key(&o, "items"); jw_raw(&o, "[");
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (s->rt.today_day != today || !s->rt.today_n) continue;
        char t[96];
        title_of(s, t, sizeof t);
        if (!first) jw_raw(&o, ",");
        first = 0;
        jw_raw(&o, "{"); jw_key(&o, "wid"); jw_str(&o, s->id);
        jw_key(&o, "title"); jw_str(&o, t);
        jw_key(&o, "times"); jw_int(&o, s->rt.today_n); jw_raw(&o, "}");
    }
    jw_raw(&o, "]}");
}

static void apply_back(engine *e, wish_do *list, int n, long long now)
{
    for (int i = n - 1; i >= 0; i--) run_one(e, NULL, &list[i], NULL, 0, now);
}

static unsigned hold_bits(const jv *a)
{
    static const char *const R[] = { "password", "call", "camera", "microphone", "share",
                                     "fullscreen", "lock", NULL };
    unsigned b = 0;
    if (!a || a->type != J_ARR) return 0;
    for (const jv *k = a->kid; k; k = k->next)
        for (int i = 0; R[i]; i++)
            if (k->type == J_STR && !strcmp(k->s, R[i])) b |= 1u << i;
    return b;
}

int engine_request(engine *e, const char *line, size_t len, char *r, size_t n, long long now)
{
    static char mem[96 * 1024];
    static const jlimits L = { 4096, 6, 300, 1024 };
    jarena a = { mem, sizeof mem, 0, NULL };
    r[0] = 0;
    const jv *m = json_parse(&a, &L, line, len);
    if (!m || m->type != J_OBJ) return -1;
    const jv *opv = jget(m, "op"), *idv = jget(m, "id");
    const char *op = opv && opv->type == J_STR ? opv->s : "";
    long long id = idv && idv->type == J_INT ? idv->i : -1;
    const jv *x;

    /* The compositor's half: no reply. */
    if (!strcmp(op, "typed")) {
        x = jget(m, "word");
        if (x && x->type == J_STR) engine_typed(e, x->s, now);
        return 0;
    }
    if (!strcmp(op, "app")) {
        x = jget(m, "app");
        if (x && x->type == J_STR) engine_app(e, x->s, now);
        return 0;
    }
    if (!strcmp(op, "input")) { engine_input(e, now); return 0; }
    if (!strcmp(op, "hold")) { engine_hold(e, hold_bits(jget(m, "reasons")), now); return 0; }
    if (!strcmp(op, "did")) {
        const jv *nv = jget(m, "n"), *ok = jget(m, "ok");
        if (!nv || nv->type != J_INT) return 0;
        shell_ev *se = &e->sev[nv->i % 16];
        if (se->n != nv->i || se->at) return 0;       /* unknown, or answered */
        se->at = now;
        if (se->undo_slot && e->undo_wait > 0) e->undo_wait--;
        wish_do prev;
        if (ok && ok->type == J_BOOL && ok->i &&
            store_parse_prev(jget(m, "prev"), e->env, &prev) == 0) {
            remember(e, &prev, se->undo_slot);
            save(e);
        }
        return 0;
    }

    if (id < 0) return 0;
    if (!strcmp(op, "wish") || !strcmp(op, "ask") || !strcmp(op, "cancel")) return 1;

    if (!strcmp(op, "hello")) {
        jout o = { r, n, 0, 0 };
        unsigned sn = e->env->sensors;
        jw_raw(&o, "{"); jw_key(&o, "id"); jw_int(&o, id);
        jw_key(&o, "kind"); jw_str(&o, "hello");
        jw_key(&o, "v"); jw_int(&o, 1);
        jw_key(&o, "consent"); jw_raw(&o, e->st->consent ? "true" : "false");
        jw_key(&o, "all_off"); jw_raw(&o, e->st->all_off ? "true" : "false");
        jw_key(&o, "sensors"); jw_raw(&o, "{");
        jw_key(&o, "compass"); jw_raw(&o, sn & (1u << S_COMPASS) ? "true" : "false");
        jw_key(&o, "tilt"); jw_raw(&o, sn & (1u << S_TILT) ? "true" : "false");
        jw_key(&o, "light"); jw_raw(&o, sn & (1u << S_LIGHT) ? "true" : "false");
        jw_raw(&o, "}}");
        return 0;
    }
    if (!strcmp(op, "consent")) {
        const jv *yes = jget(m, "yes"), *ver = jget(m, "version");
        int v = ver && ver->type == J_INT && ver->i > 0 && ver->i < 1000 ? (int)ver->i : 0;
        int was = e->st->consent;
        e->st->consent = yes && yes->type == J_BOOL && yes->i ? v : 0;
        /* §7.12: turning Wishes off tells the service to forget her. */
        if (was && !e->st->consent && e->io.forget) e->io.forget(e->io.ctx);
        save(e);
        reply_kind(r, n, id, "done");
        return 0;
    }
    if (!strcmp(op, "check")) {
        const jv *said = jget(m, "said"), *voice = jget(m, "voice");
        engine_offer(e, id, OFFER_WISH, NULL, jget(m, "wish"), said && said->type == J_STR ? said->s : NULL,
                     voice && voice->type == J_BOOL && voice->i, r, n, now);
        return 0;
    }
    if (!strcmp(op, "confirm") || !strcmp(op, "discard")) {
        x = jget(m, "pending");
        pending *p = find_pending(e, x && x->type == J_STR ? x->s : NULL, now);
        if (!p) { reply_error(r, n, id, "refused"); return 0; }
        p->used = 1;
        if (op[0] == 'd') { reply_kind(r, n, id, "done"); return 0; }
        /* The world may have moved since the offer: check again. */
        const char *why = fits(e, &p->w);
        if (why) { reply_cannot(r, n, id, why); return 0; }
        if (p->w.when.type == W_NOW) {
            /* A one-off runs now and is not kept; what it changes can be
             * undone for 10 minutes (§7.7). */
            stored tmp;
            memset(&tmp, 0, sizeof tmp);
            snprintf(tmp.id, sizeof tmp.id, "%s", "now");
            tmp.ok = 1;
            tmp.w = p->w;
            e->nundo = 0;
            e->undo_wait = 0;
            e->undo_at = now;
            for (int i = 0; i < tmp.w.n; i++) run_one(e, &tmp, &tmp.w.d[i], NULL, 1, now);
            save(e);
            reply_kind(r, n, id, "done");
            return 0;
        }
        if (e->st->n >= STORE_MAX) { reply_cannot(r, n, id, "limit"); return 0; }
        stored *s = store_add(e->st, &p->w, p->said, p->voice, now);
        if (!s) { reply_error(r, n, id, "broken"); return 0; }
        s->rt.every_base = now;
        if (store_save(e->st, e->path ? e->path : "/dev/null") < 0 && e->path) {
            store_remove(e->st, s->id);
            reply_error(r, n, id, "broken");
            return 0;
        }
        snprintf(r, n, "{\"id\":%lld,\"kind\":\"done\",\"wid\":\"%s\"}", id, s->id);
        changed(e);
        return 0;
    }
    if (!strcmp(op, "list")) { list_reply(e, id, r, n, now); return 0; }
    if (!strcmp(op, "today")) { today_reply(e, id, r, n, now); return 0; }
    if (!strcmp(op, "enable") || !strcmp(op, "remove") || !strcmp(op, "test") ||
        !strcmp(op, "keep") || !strcmp(op, "snooze")) {
        x = jget(m, "wid");
        stored *s = store_find(e->st, x && x->type == J_STR ? x->s : NULL);
        if (!s) { reply_error(r, n, id, "refused"); return 0; }
        if (op[0] == 'e') {
            const jv *on = jget(m, "on");
            if (!on || on->type != J_BOOL) { reply_error(r, n, id, "refused"); return 0; }
            if (on->i && !s->ok) { reply_error(r, n, id, "refused"); return 0; }
            s->status = on->i ? ST_ON : ST_OFF;
            s->paused_until = 0;
            memset(s->rt.hist, 0, sizeof s->rt.hist);
            /* Stopping a wish stops what it was about to do. */
            for (int i = 0; i < e->nq; ) if (!strcmp(e->q[i].wid, s->id)) e->q[i] = e->q[--e->nq]; else i++;
        } else if (op[0] == 'r') {
            for (int i = 0; i < e->nq; ) if (!strcmp(e->q[i].wid, s->id)) e->q[i] = e->q[--e->nq]; else i++;
            store_remove(e->st, s->id);
        } else if (op[0] == 't') {
            if (!s->ok) { reply_error(r, n, id, "refused"); return 0; }
            fire(e, s, F_TEST, NULL, now);
        } else if (op[0] == 'k') {
            const jv *yes = jget(m, "yes");
            int y = yes && yes->type == J_BOOL && yes->i;
            s->keep = 2;
            if (!y) s->status = ST_OFF;
        } else {
            /* Remind me in 10 minutes. */
            if (!s->ok || s->w.d[0].type != A_REMIND) { reply_error(r, n, id, "refused"); return 0; }
            s->rt.snooze_at = now + 600;
        }
        save(e);
        reply_kind(r, n, id, "done");
        changed(e);
        return 0;
    }
    if (!strcmp(op, "all_off") || !strcmp(op, "all_on")) {
        e->st->all_off = op[4] == 'o' && op[5] == 'f';
        if (e->st->all_off) e->nq = 0;
        save(e);
        reply_kind(r, n, id, "done");
        changed(e);
        return 0;
    }
    if (!strcmp(op, "undo")) {
        if (!e->nundo || now - e->undo_at >= UNDO_FOR) { reply_error(r, n, id, "refused"); return 0; }
        apply_back(e, e->undo, e->nundo, now);
        e->nundo = 0;
        reply_kind(r, n, id, "done");
        return 0;
    }
    if (!strcmp(op, "restore")) {
        apply_back(e, e->st->base, e->st->nbase, now);
        e->st->nbase = 0;
        e->nundo = 0;
        save(e);
        reply_kind(r, n, id, "done");
        return 0;
    }
    if (!strcmp(op, "forget")) {
        if (e->io.forget) e->io.forget(e->io.ctx);
        store_free(e->st);
        e->st->next = 1;
        e->nq = 0;
        e->nundo = 0;
        memset(e->pend, 0, sizeof e->pend);
        save(e);
        reply_kind(r, n, id, "done");
        changed(e);
        return 0;
    }
    reply_error(r, n, id, "refused");
    return 0;
}

unsigned engine_wants(const engine *e)
{
    unsigned w = 0;
    if (e->st->all_off) return 0;
    for (int i = 0; i < e->st->n; i++) {
        const stored *s = e->st->s[i];
        if (!s->ok || s->status != ST_ON) continue;
        const wish_when *x = &s->w.when;
        if (x->type == W_SENSOR) w |= 1u << x->sensor;
        else if (x->type == W_BATTERY || x->type == W_PLUGGED || x->type == W_UNPLUGGED) w |= WANT_BATTERY;
        else if (x->type == W_USB) w |= WANT_USB;
    }
    return w;
}

int engine_wants_fast(const engine *e, long long now)
{
    if (e->nq || (e->last_input && now - e->last_input < 2 * TYPING_QUIET)) return 1;
    if (engine_wants(e) & (WANT_COMPASS | WANT_TILT | WANT_LIGHT)) return 1;
    for (int i = 0; i < e->st->n; i++) {
        const stored *s = e->st->s[i];
        if (s->rt.snooze_at) return 1;
        if (s->ok && s->status == ST_ON && s->w.when.type == W_IDLE) return 1;
    }
    return 0;
}

void engine_triggers_line(engine *e, char *out, size_t n)
{
    jout o = { out, n, 0, 0 };
    out[0] = 0;
    jw_raw(&o, "{\"ev\":\"triggers\",\"words\":[");
    int k = 0;
    for (int i = 0; i < e->st->n && k < TRIGGER_WORDS; i++) {
        stored *s = e->st->s[i];
        if (e->st->all_off || !s->ok || s->status != ST_ON || s->w.when.type != W_TYPED) continue;
        if (k++) jw_raw(&o, ",");
        jw_str(&o, s->w.when.word);
    }
    jw_raw(&o, "],\"apps\":[");
    k = 0;
    for (int i = 0; i < e->st->n; i++) {
        stored *s = e->st->s[i];
        if (e->st->all_off || !s->ok || s->status != ST_ON || s->w.when.type != W_APP) continue;
        if (k++) jw_raw(&o, ",");
        jw_str(&o, s->w.when.app.id);
        if (s->w.when.app.wmclass[0]) { jw_raw(&o, ","); jw_str(&o, s->w.when.app.wmclass); }
    }
    jw_raw(&o, "]}");
    if (o.bad) snprintf(out, n, "%s", "{\"ev\":\"triggers\",\"words\":[],\"apps\":[]}");
}

engine *engine_new(store *st, const char *path, wish_env *env, const engine_io *io)
{
    engine *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->st = st;
    e->path = path;
    e->env = env;
    e->io = *io;
    return e;
}

void engine_free(engine *e) { free(e); }
