/* wish.c — see wish.h. Every rule here has a test in tools/wishtest.c
 * named after the attack it stops (docs/AI.md §7.1). */
#define _GNU_SOURCE
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "wish.h"

const jlimits WISH_LIMITS = { 8192, 5, 200, 1024 };

static const char *const WHEN_NAMES[W_N + 1] = {
    "now", "typed_word", "time", "every", "startup", "app_opened",
    "battery_below", "plugged_in", "unplugged", "usb_inserted", "idle", "sensor", NULL };
static const char *const DO_NAMES[A_N + 1] = {
    "fill_screen", "show_message", "remind", "speak", "play_sound",
    "brightness", "volume", "open_app", "open_website", "set_look",
    "set_text_size", "set_wallpaper", "night_light", "speak_time",
    "pointer_size", "do_not_disturb", NULL };
static const char *const SOUNDS[] = { "chime", "bell", "birds", "applause", "gentle", NULL };
static const char *const SOUND_WORDS[] = { "chime", "bell", "birdsong", "applause", "soft" };
static const char *const LOOKS[] = { "taskbar", "dock", "rail", "tiles", NULL };
static const char *const LOOK_NAMES[] = { "Like Windows", "Like a Mac", "Big and simple",
                                          "One thing at a time" };
static const char *const SIZES[] = { "normal", "bigger", "biggest", NULL };
static const char *const SENSORS[] = { "compass", "tilt", "light", NULL };
static const char *const HEADINGS[] = { "north", "east", "south", "west", NULL };
static const char *const ORIENTS[] = { "normal", "left", "right", "upside_down", NULL };
static const char *const DAYS[] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun", NULL };
static const char *const DAY_NAMES[] = { "Mondays", "Tuesdays", "Wednesdays", "Thursdays",
                                         "Fridays", "Saturdays", "Sundays" };

const char *wish_sound_name(int s) { return s >= 0 && s < SND_N ? SOUNDS[s] : "chime"; }
const char *wish_look_id(int l) { return l >= 0 && l < 4 ? LOOKS[l] : "rail"; }

/* §7.4: which triggers each action may be used with. */
#define B(t) (1u << (t))
#define REC (B(W_EVERY) | B(W_IDLE) | B(W_STARTUP) | B(W_SENSOR) | B(W_TYPED) | \
             B(W_APP) | B(W_BATTERY) | B(W_PLUGGED) | B(W_UNPLUGGED) | B(W_USB))
static const unsigned ALLOWED[A_N] = {
    [A_FILL]       = B(W_NOW) | B(W_TIME) | REC,
    [A_MESSAGE]    = B(W_NOW) | B(W_TIME) | REC,
    [A_SOUND]      = B(W_NOW) | B(W_TIME) | REC,
    [A_BRIGHT]     = B(W_NOW) | B(W_TIME) | REC,
    [A_NIGHT]      = B(W_NOW) | B(W_TIME) | REC,
    [A_REMIND]     = B(W_TIME),
    [A_SPEAK]      = B(W_NOW) | B(W_TIME),
    [A_SPEAK_TIME] = B(W_NOW) | B(W_TIME),
    [A_VOLUME]     = B(W_NOW),
    [A_LOOK]       = B(W_NOW),
    [A_TEXT]       = B(W_NOW),
    [A_WALL]       = B(W_NOW),
    [A_POINTER]    = B(W_NOW),
    [A_OPEN_APP]   = B(W_NOW) | B(W_TIME),
    [A_OPEN_SITE]  = B(W_NOW),
    [A_DND]        = B(W_NOW) | B(W_TIME),
};

int wish_do_overlay(int t) { return t == A_FILL || t == A_MESSAGE || t == A_REMIND; }
int wish_do_sound(int t)   { return t == A_SOUND || t == A_SPEAK || t == A_SPEAK_TIME; }
int wish_do_outward(int t) { return t == A_OPEN_APP || t == A_OPEN_SITE; }

/* ── refusing ─────────────────────────────────────────────────────── */

#define NO(miss, why_, wh) do {                                     \
        err->missing = (miss); err->why = (why_);                   \
        snprintf(err->where, sizeof err->where, "%s", (wh));        \
        return -1; } while (0)

static int pick(const char *s, const char *const *l)
{
    if (!s) return -1;
    for (int i = 0; l[i]; i++) if (!strcmp(s, l[i])) return i;
    return -1;
}

static const char *sval(const jv *o, const char *k)
{
    const jv *v = jget(o, k);
    return v && v->type == J_STR ? v->s : NULL;
}

/* 1 present and in range, 0 absent, -1 present and wrong. */
static int ival(const jv *o, const char *k, long long lo, long long hi, int *out)
{
    const jv *v = jget(o, k);
    if (!v) return 0;
    if (v->type != J_INT || v->i < lo || v->i > hi) return -1;
    *out = (int)v->i;
    return 1;
}

static int hhmm(const char *s, int *h, int *m)
{
    if (!s || strlen(s) != 5 || s[2] != ':') return -1;
    for (int i = 0; i < 5; i++) if (i != 2 && (s[i] < '0' || s[i] > '9')) return -1;
    *h = (s[0] - '0') * 10 + s[1] - '0';
    *m = (s[3] - '0') * 10 + s[4] - '0';
    return *h < 24 && *m < 60 ? 0 : -1;
}

/* ── what is installed ───────────────────────────────────────────── */

/* Reads the [Desktop Entry] keys we need from one file. 0 if it is a
 * program a person would open. */
static int desktop_read(const char *path, char *name, size_t nn, char *wm, size_t wn)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    int in = 0, app = 0, hidden = 0;
    name[0] = wm[0] = 0;
    int lines = 0;
    while (fgets(line, sizeof line, f) && ++lines < 2000) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { in = !strcmp(line, "[Desktop Entry]"); continue; }
        if (!in) continue;
        /* A value that does not fit is not cut (a cut can land inside a
         * UTF-8 letter): it is left empty, which skips the program. */
        if (!strncmp(line, "Name=", 5) && !name[0] && strlen(line + 5) < nn) memcpy(name, line + 5, strlen(line + 5) + 1);
        else if (!strcmp(line, "Type=Application")) app = 1;
        else if (!strcmp(line, "NoDisplay=true") || !strcmp(line, "Hidden=true")) hidden = 1;
        else if (!strncmp(line, "StartupWMClass=", 15) && strlen(line + 15) < wn) memcpy(wm, line + 15, strlen(line + 15) + 1);
    }
    fclose(f);
    return app && !hidden && name[0] ? 0 : -1;
}

static int id_ok(const char *s)
{
    if (!*s || strlen(s) > 120) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == '_'))
            return 0;
    return 1;
}

static const char *last_part(const char *id)
{
    const char *d = strrchr(id, '.');
    return d ? d + 1 : id;
}

/* §7.11: the model names a program; the device decides which one, and
 * only among what is installed. Ambiguous is the same as absent. */
static int find_app(const char *want, const char *dirs, wish_app *out)
{
    char defdirs[1200];
    if (!dirs) {
        const char *home = getenv("HOME");
        const char *xdh = getenv("XDG_DATA_HOME");
        char hd[480] = "";
        if (xdh && *xdh) snprintf(hd, sizeof hd, "%s", xdh);
        else if (home && *home) snprintf(hd, sizeof hd, "%s/.local/share", home);
        snprintf(defdirs, sizeof defdirs,
                 "%s/applications:%s/flatpak/exports/share/applications:"
                 "/var/lib/flatpak/exports/share/applications:"
                 "/usr/local/share/applications:/usr/share/applications", hd, hd);
        dirs = defdirs;
    }
    int found = 0;
    char dl[1200];
    snprintf(dl, sizeof dl, "%.*s", (int)sizeof dl - 1, dirs);
    for (char *save = NULL, *d = strtok_r(dl, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        DIR *dh = opendir(d);
        if (!dh) continue;
        struct dirent *e;
        int seen = 0;
        while ((e = readdir(dh)) && ++seen < 4000) {
            size_t l = strlen(e->d_name);
            if (l < 9 || l > 127 || strcmp(e->d_name + l - 8, ".desktop")) continue;
            char id[128];
            snprintf(id, sizeof id, "%.*s", (int)(l - 8), e->d_name);
            if (!id_ok(id)) continue;
            char path[1400], name[64], wm[128];
            snprintf(path, sizeof path, "%s/%s", d, e->d_name);
            if (desktop_read(path, name, sizeof name, wm, sizeof wm) < 0) continue;
            if (!text_eq(name, want) && !text_eq(id, want) && !text_eq(last_part(id), want))
                continue;
            if (!text_label_ok(name, 40)) continue;
            if (found && strcmp(out->id, id)) { closedir(dh); return -2; }   /* two programs */
            if (!found) {
                snprintf(out->id, sizeof out->id, "%s", id);
                memcpy(out->label, name, sizeof out->label);       /* same size */
                memcpy(out->wmclass, id_ok(wm) ? wm : "", id_ok(wm) ? sizeof out->wmclass : 1);
            }
            found = 1;
        }
        closedir(dh);
    }
    return found ? 0 : -1;
}

static int find_theme(const char *want, const char *dir, char *id, size_t idn,
                      char *label, size_t ln)
{
    if (!dir) dir = getenv("AUROS_THEME_DIR");
    if (!dir) dir = "/usr/share/auros/themes";
    DIR *dh = opendir(dir);
    if (!dh) return -1;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(dh))) {
        size_t l = strlen(e->d_name);
        if (l < 7 || l > 60 || strcmp(e->d_name + l - 6, ".theme")) continue;
        char tid[64];
        snprintf(tid, sizeof tid, "%.*s", (int)(l - 6), e->d_name);
        int ok = 1;
        for (char *p = tid; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) ok = 0;
        if (!ok) continue;
        char path[1100], line[512], name[128] = "";
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "theme_name=\"", 12)) {
                char *q = strchr(line + 12, '"');
                if (q && q - (line + 12) < (long)sizeof name) { *q = 0; memcpy(name, line + 12, (size_t)(q - line - 12) + 1); }
                break;
            }
        fclose(f);
        if (!name[0] || !text_label_ok(name, 30)) snprintf(name, sizeof name, "%s", tid);
        if (text_eq(want, tid) || text_eq(want, name)) {
            snprintf(id, idn, "%s", tid);
            snprintf(label, ln, "%s", name);
            found = 1;
            break;
        }
    }
    closedir(dh);
    return found ? 0 : -1;
}

/* WCAG relative luminance and contrast ratio. */
static double lum(unsigned rgb)
{
    double c[3];
    for (int i = 0; i < 3; i++) {
        double v = ((rgb >> (16 - 8 * i)) & 0xFF) / 255.0;
        c[i] = v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}

static double contrast(unsigned a, unsigned b)
{
    double x = lum(a), y = lum(b);
    if (x < y) { double t = x; x = y; y = t; }
    return (x + 0.05) / (y + 0.05);
}

/* ── parsing ───────────────────────────────────────────────────────── */

/* The fields each trigger, sensor and action may have. Anything else in
 * the object is a refusal (§1.1). */
static const char *const *const SK[] = {
    (const char *const[]){ "type", "sensor", "heading", "tolerance_deg", NULL },
    (const char *const[]){ "type", "sensor", "orientation", NULL },
    (const char *const[]){ "type", "sensor", "below_lux", "above_lux", NULL } };
static const char *const *const DO_KEYS[A_N] = {
    [A_FILL]       = (const char *const[]){ "type", "emoji", "text", "seconds", NULL },
    [A_MESSAGE]    = (const char *const[]){ "type", "text", "seconds", NULL },
    [A_REMIND]     = (const char *const[]){ "type", "text", "private", NULL },
    [A_SPEAK]      = (const char *const[]){ "type", "text", NULL },
    [A_SOUND]      = (const char *const[]){ "type", "sound", NULL },
    [A_BRIGHT]     = (const char *const[]){ "type", "set_percent", "change_percent", NULL },
    [A_VOLUME]     = (const char *const[]){ "type", "set_percent", "change_percent", NULL },
    [A_OPEN_APP]   = (const char *const[]){ "type", "app_name", NULL },
    [A_OPEN_SITE]  = (const char *const[]){ "type", "site", NULL },
    [A_LOOK]       = (const char *const[]){ "type", "look", NULL },
    [A_TEXT]       = (const char *const[]){ "type", "size", NULL },
    [A_WALL]       = (const char *const[]){ "type", "color", "theme_name", NULL },
    [A_NIGHT]      = (const char *const[]){ "type", "on", NULL },
    [A_SPEAK_TIME] = (const char *const[]){ "type", NULL },
    [A_POINTER]    = (const char *const[]){ "type", "size", NULL },
    [A_DND]        = (const char *const[]){ "type", "on", "until", NULL },
};

static int parse_when(const jv *o, const wish_env *env, wish_when *w, wish_err *err)
{
    static const char *const K_NONE[] = { "type", NULL };
    static const char *const K_WORD[] = { "type", "word", NULL };
    static const char *const K_TIME[] = { "type", "at", "days", NULL };
    static const char *const K_MIN[]  = { "type", "minutes", NULL };
    static const char *const K_APP[]  = { "type", "app_name", NULL };
    static const char *const K_PCT[]  = { "type", "percent", NULL };
    static const char *const K_SENS[] = { "type", "sensor", "heading", "tolerance_deg",
                                          "orientation", "below_lux", "above_lux", NULL };
    if (!o || o->type != J_OBJ) NO("other", "when is not an object", "when");
    w->type = pick(sval(o, "type"), WHEN_NAMES);
    if (w->type < 0) NO("other", "unknown trigger", "when.type");
    static const char *const *KEYS[W_N] = {
        [W_NOW] = K_NONE, [W_TYPED] = K_WORD, [W_TIME] = K_TIME, [W_EVERY] = K_MIN,
        [W_STARTUP] = K_NONE, [W_APP] = K_APP, [W_BATTERY] = K_PCT,
        [W_PLUGGED] = K_NONE, [W_UNPLUGGED] = K_NONE, [W_USB] = K_NONE,
        [W_IDLE] = K_MIN, [W_SENSOR] = K_SENS };
    if (!jonly(o, KEYS[w->type])) NO("other", "unknown field", "when");

    switch (w->type) {
    case W_TYPED: {
        const char *s = sval(o, "word");
        if (!s) NO("other", "no word", "when.word");
        const char *why = word_rule(s, w->word);
        if (why) NO("other", why, "when.word");
        break;
    }
    case W_TIME: {
        if (hhmm(sval(o, "at"), &w->hh, &w->mm) < 0) NO("other", "bad time", "when.at");
        const jv *d = jget(o, "days");
        if (!d) { w->days = 0x7F; break; }
        if (d->type != J_ARR || d->n < 1 || d->n > 7) NO("other", "bad days", "when.days");
        for (const jv *k = d->kid; k; k = k->next) {
            int i = k->type == J_STR ? pick(k->s, DAYS) : -1;
            if (i < 0 || (w->days & (1u << i))) NO("other", "bad or repeated day", "when.days");
            w->days |= 1u << i;
        }
        break;
    }
    case W_EVERY:
        if (ival(o, "minutes", 5, 1440, &w->minutes) != 1) NO("other", "minutes out of range", "when.minutes");
        break;
    case W_IDLE:
        if (ival(o, "minutes", 1, 240, &w->minutes) != 1) NO("other", "minutes out of range", "when.minutes");
        break;
    case W_APP: {
        const char *s = sval(o, "app_name");
        if (!s || !text_label_ok(s, 60)) NO("other", "bad app name", "when.app_name");
        snprintf(w->app.name, sizeof w->app.name, "%s", s);
        if (find_app(s, env->apps_dirs, &w->app) < 0) NO("app", "no such program", "when.app_name");
        break;
    }
    case W_BATTERY:
        if (ival(o, "percent", 5, 50, &w->percent) != 1) NO("other", "percent out of range", "when.percent");
        break;
    case W_SENSOR: {
        w->sensor = pick(sval(o, "sensor"), SENSORS);
        if (w->sensor < 0) NO("other", "unknown sensor", "when.sensor");
        if (!jonly(o, SK[w->sensor])) NO("other", "field of another sensor", "when");
        if (!(env->sensors & (1u << w->sensor)))
            NO(SENSORS[w->sensor], "this computer has no such sensor", "when.sensor");
        if (w->sensor == S_COMPASS) {
            w->heading = pick(sval(o, "heading"), HEADINGS);
            if (w->heading < 0) NO("other", "bad heading", "when.heading");
            if (ival(o, "tolerance_deg", 5, 45, &w->tol) != 1)
                NO("other", "tolerance missing or out of range", "when.tolerance_deg");
        } else if (w->sensor == S_TILT) {
            w->orient = pick(sval(o, "orientation"), ORIENTS);
            if (w->orient < 0) NO("other", "bad orientation", "when.orientation");
        } else {
            int b = ival(o, "below_lux", 0, 100000, &w->lux);
            int a = ival(o, "above_lux", 0, 100000, &w->lux);
            if (b < 0 || a < 0 || (a == 1) == (b == 1)) NO("other", "need exactly one of below/above", "when");
            w->lux_above = a == 1;
        }
        break;
    }
    }
    return 0;
}

static int free_text(const jv *o, const char *k, const wish_env *env, int max_g,
                     char *out, wish_err *err, const char *where)
{
    const char *s = sval(o, k);
    if (!s) NO("other", "missing text", where);
    if (!env->said) NO("other", "free text without her words", where);
    const char *why = text_free(s, env->said, max_g, (size_t)max_g * 6, out, WISH_TEXT);
    if (why) NO("other", why, where);
    return 0;
}

static int pct(const jv *o, int lo, int hi, wish_do *d, wish_err *err, const char *where)
{
    int s = ival(o, "set_percent", lo, hi, &d->pct);
    int c = ival(o, "change_percent", -50, 50, &d->pct);
    if (s < 0 || c < 0 || (s == 1) == (c == 1)) NO("other", "need exactly one of set/change in range", where);
    if (c == 1 && d->pct == 0) NO("other", "a change of nothing", where);
    d->is_set = s == 1;
    return 0;
}

static int parse_do(const jv *o, const wish_env *env, wish_do *d, wish_err *err, const char *where)
{
    if (!o || o->type != J_OBJ) NO("other", "action is not an object", where);
    const char *t = sval(o, "type");
    if (t && !strcmp(t, "install_program")) NO("install", "installing is not a wish in v1", where);
    d->type = pick(t, DO_NAMES);
    if (d->type < 0) NO("other", "unknown action", where);
    if (!jonly(o, DO_KEYS[d->type])) NO("other", "unknown field", where);

    switch (d->type) {
    case A_FILL: {
        int e = jget(o, "emoji") != NULL, x = jget(o, "text") != NULL;
        if (e == x) NO("other", "need exactly one of emoji/text", where);
        if (e) {
            const char *em = emoji_allowed(sval(o, "emoji"));
            if (!em) NO("other", "emoji not on the list", where);
            snprintf(d->emoji, sizeof d->emoji, "%s", em);
        } else if (free_text(o, "text", env, 24, d->text, err, where) < 0) return -1;
        if (ival(o, "seconds", 1, 10, &d->seconds) != 1) NO("other", "seconds out of range", where);
        break;
    }
    case A_MESSAGE:
        if (free_text(o, "text", env, 140, d->text, err, where) < 0) return -1;
        /* §7.7: a note stays until she closes it; seconds is accepted
         * for v0 documents and has no effect. */
        if (ival(o, "seconds", 3, 60, &d->seconds) < 0) NO("other", "seconds out of range", where);
        break;
    case A_REMIND: {
        if (free_text(o, "text", env, 140, d->text, err, where) < 0) return -1;
        const jv *p = jget(o, "private");
        if (p && p->type != J_BOOL) NO("other", "private is not true/false", where);
        d->priv = p ? (int)p->i : 0;
        break;
    }
    case A_SPEAK:
        if (free_text(o, "text", env, 200, d->text, err, where) < 0) return -1;
        break;
    case A_SOUND:
        d->sound = pick(sval(o, "sound"), SOUNDS);
        if (d->sound < 0) NO("other", "unknown sound", where);
        break;
    case A_BRIGHT:
        /* §7.4: never below 30 % from a wish. */
        if (pct(o, 30, 100, d, err, where) < 0) return -1;
        break;
    case A_VOLUME:
        /* §2.2 and §7.4: 20-80 %. */
        if (pct(o, 20, 80, d, err, where) < 0) return -1;
        break;
    case A_OPEN_APP: {
        const char *s = sval(o, "app_name");
        if (!s || !text_label_ok(s, 60)) NO("other", "bad app name", where);
        snprintf(d->app.name, sizeof d->app.name, "%s", s);
        if (find_app(s, env->apps_dirs, &d->app) < 0) NO("app", "no such program", where);
        break;
    }
    case A_OPEN_SITE: {
        const char *s = sval(o, "site"), *name, *url;
        if (!s || !text_label_ok(s, 60)) NO("other", "bad site", where);
        if (site_find(s, &name, &url) < 0) NO("site", "not a site on the list", where);
        snprintf(d->site, sizeof d->site, "%s", s);
        snprintf(d->site_name, sizeof d->site_name, "%s", name);
        snprintf(d->url, sizeof d->url, "%s", url);
        break;
    }
    case A_LOOK:
        d->look = pick(sval(o, "look"), LOOKS);
        if (d->look < 0) NO("other", "unknown look", where);
        break;
    case A_TEXT:
    case A_POINTER:
        d->size = pick(sval(o, "size"), SIZES);
        if (d->size < 0) NO("other", "unknown size", where);
        break;
    case A_WALL: {
        const char *c = sval(o, "color"), *th = sval(o, "theme_name");
        if ((c != NULL) == (th != NULL) || (jget(o, "color") && !c) ||
            (jget(o, "theme_name") && !th))
            NO("other", "need exactly one of color/theme_name", where);
        if (c) {
            if (strlen(c) != 7 || c[0] != '#') NO("other", "bad colour", where);
            for (int i = 1; i < 7; i++)
                if (!strchr("0123456789abcdefABCDEF", c[i])) NO("other", "bad colour", where);
            d->has_color = 1;
            d->color = (unsigned)strtoul(c + 1, NULL, 16);
            /* §7.4: the words on the desktop must stay readable. */
            if (contrast(d->color, env->fg) < 4.5) NO("other", "too little contrast", where);
        } else {
            if (!text_label_ok(th, 30)) NO("other", "bad theme name", where);
            snprintf(d->theme_name, sizeof d->theme_name, "%s", th);
            if (find_theme(th, env->themes_dir, d->theme, sizeof d->theme,
                           d->theme_label, sizeof d->theme_label) < 0)
                NO("theme", "no such look", where);
        }
        break;
    }
    case A_NIGHT:
    case A_DND: {
        const jv *on = jget(o, "on");
        if (!on || on->type != J_BOOL) NO("other", "on is not true/false", where);
        d->on = (int)on->i;
        const jv *u = jget(o, "until");
        if (u) {
            if (!d->on || hhmm(u->type == J_STR ? u->s : NULL, &d->until_hh, &d->until_mm) < 0)
                NO("other", "bad until", where);
            d->has_until = 1;
        }
        break;
    }
    case A_SPEAK_TIME:
        break;
    }
    return 0;
}

static int quiet_hour(int hh) { return hh >= 21 || hh < 8; }

int wish_parse(const jv *doc, const wish_env *env, wish *w, wish_err *err)
{
    static const char *const TOP[] = { "v", "when", "do", "enabled", NULL };
    memset(w, 0, sizeof *w);
    memset(err, 0, sizeof *err);
    if (!doc || doc->type != J_OBJ) NO("other", "not an object", "");
    /* §7.3: title and summary are the device's to write. A document that
     * carries its own is refused, not trimmed. */
    if (!jonly(doc, TOP)) NO("other", "unknown field", "");
    const jv *v = jget(doc, "v");
    if (!v || v->type != J_INT || v->i != 1) NO("other", "not version 1", "v");
    const jv *en = jget(doc, "enabled");
    if (en && en->type != J_BOOL) NO("other", "enabled is not true/false", "enabled");
    if (parse_when(jget(doc, "when"), env, &w->when, err) < 0) return -1;

    const jv *dl = jget(doc, "do");
    if (!dl || dl->type != J_ARR || dl->n < 1 || dl->n > WISH_MAX_DO)
        NO("other", "do must have 1-4 actions", "do");
    int outward = 0, sound = 0, i = 0;
    for (const jv *k = dl->kid; k; k = k->next, i++) {
        char where[16];
        snprintf(where, sizeof where, "do[%d]", i);
        wish_do *d = &w->d[i];
        if (parse_do(k, env, d, err, where) < 0) return -1;
        /* §7.4: the trigger decides what may follow it. */
        if (!(ALLOWED[d->type] & B(w->when.type))) NO("other", "action not allowed with this trigger", where);
        if ((d->type == A_BRIGHT || d->type == A_VOLUME) && !d->is_set && w->when.type != W_NOW)
            NO("other", "only set_percent on a trigger", where);
        if (wish_do_outward(d->type) && ++outward > 1) NO("other", "more than one outward action", where);
        if (d->type == A_SOUND || d->type == A_SPEAK || d->type == A_SPEAK_TIME) sound = 1;
        /* §7.4 quiet hours: a time inside 21:00-08:00 may not make a
         * sound or put a card up, except the reminder she set for then. */
        if (w->when.type == W_TIME && quiet_hour(w->when.hh) &&
            (wish_do_sound(d->type) || d->type == A_FILL || d->type == A_MESSAGE))
            NO("other", "sound or card in quiet hours", where);
    }
    w->n = i;
    if (w->when.type == W_EVERY && sound && w->when.minutes < 30)
        NO("other", "a sound more often than every 30 minutes", "when.minutes");
    return 0;
}

int wish_parse_text(const char *json, size_t len, const wish_env *env, wish *w, wish_err *err)
{
    static char mem[64 * 1024];
    jarena a = { mem, sizeof mem, 0, NULL };
    const jv *doc = json_parse(&a, &WISH_LIMITS, json, len);
    if (!doc) {
        memset(err, 0, sizeof *err);
        err->missing = "other";
        err->why = a.err;
        return -1;
    }
    return wish_parse(doc, env, w, err);
}

/* ── sentences ─────────────────────────────────────────────────────── */

typedef struct { char *b; size_t n, len; } sb;

static void sp(sb *s, const char *fmt, ...)
{
    if (s->len >= s->n) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(s->b + s->len, s->n - s->len, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    s->len += (size_t)k;
    if (s->len >= s->n) s->len = s->n - 1;
}

static void clock_words(int hh, int mm, char *out, size_t n)
{
    int h12 = hh % 12 ? hh % 12 : 12;
    snprintf(out, n, "%d:%02d %s", h12, mm, hh < 12 ? "am" : "pm");
}

static const char *plural(int n) { return n == 1 ? "" : "s"; }

static void when_phrase(sb *s, const wish_when *w)
{
    char t[32];
    switch (w->type) {
    case W_NOW: sp(s, "Right now"); break;
    case W_TYPED: sp(s, "Every time you type the word *%s*", w->word); break;
    case W_TIME:
        clock_words(w->hh, w->mm, t, sizeof t);
        if (w->days == 0x7F) sp(s, "Every day at *%s*", t);
        else if (w->days == 0x1F) sp(s, "Every weekday at *%s*", t);
        else {
            int n = __builtin_popcount(w->days), k = 0;
            sp(s, "On *");
            for (int i = 0; i < 7; i++) {
                if (!(w->days & (1u << i))) continue;
                k++;
                sp(s, "%s%s", k == 1 ? "" : k == n ? " and " : ", ", DAY_NAMES[i]);
            }
            sp(s, "* at *%s*", t);
        }
        break;
    case W_EVERY:
        if (w->minutes % 60 == 0)
            sp(s, "Every *%d hour%s*", w->minutes / 60, plural(w->minutes / 60));
        else if (w->minutes > 60)
            sp(s, "Every *%d hour%s and %d minute%s*", w->minutes / 60, plural(w->minutes / 60),
               w->minutes % 60, plural(w->minutes % 60));
        else sp(s, "Every *%d minutes*", w->minutes);
        break;
    case W_STARTUP: sp(s, "Each time the computer starts"); break;
    case W_APP: sp(s, "Every time you open *%s*", w->app.label); break;
    case W_BATTERY: sp(s, "When the battery drops below *%d%%*", w->percent); break;
    case W_PLUGGED: sp(s, "When you plug in the charger"); break;
    case W_UNPLUGGED: sp(s, "When you unplug the charger"); break;
    case W_USB: sp(s, "When you plug in a memory stick or another USB device"); break;
    case W_IDLE: sp(s, "When you have not used the computer for *%d minute%s*", w->minutes, plural(w->minutes)); break;
    case W_SENSOR:
        if (w->sensor == S_COMPASS) sp(s, "When the computer points *%s*", HEADINGS[w->heading]);
        else if (w->sensor == S_TILT) {
            static const char *const o[] = { "the normal way up", "onto its left side",
                                             "onto its right side", "upside down" };
            sp(s, "When the computer is turned *%s*", o[w->orient]);
        } else if (w->lux_above) sp(s, "When the room gets bright (above *%d lux*)", w->lux);
        else sp(s, "When the room gets dark (below *%d lux*)", w->lux);
        break;
    }
}

static void color_words(unsigned c, char *out, size_t n)
{
    double r = ((c >> 16) & 255) / 255.0, g = ((c >> 8) & 255) / 255.0, b = (c & 255) / 255.0;
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double l = (mx + mn) / 2, d = mx - mn;
    double sat = d == 0 ? 0 : d / (1 - (2 * l - 1 < 0 ? -(2 * l - 1) : 2 * l - 1));
    if (sat < 0.15 || d < 0.06) {
        snprintf(out, n, "%s", l < 0.15 ? "black" : l > 0.88 ? "white" : l < 0.4 ? "dark grey" : "grey");
        return;
    }
    double h;
    if (mx == r) h = 60 * ((g - b) / d); else if (mx == g) h = 60 * ((b - r) / d + 2);
    else h = 60 * ((r - g) / d + 4);
    if (h < 0) h += 360;
    const char *name = h < 15 ? "red" : h < 40 ? (l < 0.4 ? "brown" : "orange") : h < 65 ? "yellow" :
                       h < 165 ? "green" : h < 195 ? "turquoise" : h < 255 ? "blue" :
                       h < 290 ? "purple" : h < 335 ? "pink" : "red";
    snprintf(out, n, "%s%s", l > 0.75 ? "light " : l < 0.3 ? "dark " : "", name);
}

static void host_of(const char *url, char *out, size_t n)
{
    const char *h = strstr(url, "://");
    h = h ? h + 3 : url;
    size_t l = strcspn(h, "/?#");
    snprintf(out, n, "%.*s", (int)l, h);
}

static void do_phrase(sb *s, const wish_do *d)
{
    static const char *const SZ[] = { "back to *normal size*", "*bigger*", "*as big as they go*" };
    char t[64];
    switch (d->type) {
    case A_FILL:
        if (d->emoji[0]) sp(s, "your screen fills with %s for *%d second%s*", d->emoji, d->seconds, plural(d->seconds));
        else sp(s, "your screen fills with the words \xE2\x80\x9C%s\xE2\x80\x9D for *%d second%s*",
                d->text, d->seconds, plural(d->seconds));
        break;
    case A_MESSAGE:
        sp(s, "a note appears saying \xE2\x80\x9C%s\xE2\x80\x9D and stays until you close it", d->text);
        break;
    case A_REMIND:
        if (d->priv)
            sp(s, "a reminder appears that only says *You have a reminder* until you open it; "
                  "then it says \xE2\x80\x9C%s\xE2\x80\x9D", d->text);
        else sp(s, "a reminder appears saying \xE2\x80\x9C%s\xE2\x80\x9D and stays until you press *OK*", d->text);
        break;
    case A_SPEAK: sp(s, "the computer says \xE2\x80\x9C%s\xE2\x80\x9D out loud", d->text); break;
    case A_SPEAK_TIME: sp(s, "the computer says the time out loud"); break;
    case A_SOUND: sp(s, "a *%s* sound plays", SOUND_WORDS[d->sound]); break;
    case A_BRIGHT:
        if (d->is_set) sp(s, "the screen brightness goes to *%d%%*", d->pct);
        else sp(s, "the screen gets *%d%%* %s", d->pct < 0 ? -d->pct : d->pct, d->pct < 0 ? "dimmer" : "brighter");
        break;
    case A_VOLUME:
        if (d->is_set) sp(s, "the sound volume goes to *%d%%*", d->pct);
        else sp(s, "the sound gets *%d%%* %s", d->pct < 0 ? -d->pct : d->pct, d->pct < 0 ? "quieter" : "louder");
        break;
    case A_OPEN_APP: sp(s, "*%s* opens", d->app.label); break;
    case A_OPEN_SITE:
        host_of(d->url, t, sizeof t);
        sp(s, "the website *%s* (%s) opens", d->site_name, t);
        break;
    case A_LOOK: sp(s, "the computer changes to the *%s* look", LOOK_NAMES[d->look]); break;
    case A_TEXT: sp(s, "the words on the screen get %s", SZ[d->size]); break;
    case A_POINTER:
        sp(s, "the mouse pointer gets %s", d->size == 2 ? "*as big as it goes*" : SZ[d->size]);
        break;
    case A_WALL:
        if (d->has_color) { color_words(d->color, t, sizeof t); sp(s, "the background becomes plain *%s*", t); }
        else sp(s, "the computer's colours change to *%s*", d->theme_label);
        break;
    case A_NIGHT:
        sp(s, d->on ? "the screen changes to *warm night colours*" : "the screen goes back to *its normal colours*");
        break;
    case A_DND:
        if (!d->on) sp(s, "the computer may show notes and make sounds again");
        else if (d->has_until) {
            clock_words(d->until_hh, d->until_mm, t, sizeof t);
            sp(s, "the computer holds back notes and sounds until *%s*", t);
        } else sp(s, "the computer holds back notes and sounds until you say otherwise");
        break;
    }
}

static int changes_setting(int t)
{
    return t == A_BRIGHT || t == A_VOLUME || t == A_LOOK || t == A_TEXT ||
           t == A_WALL || t == A_NIGHT || t == A_POINTER || t == A_DND;
}

int wish_sentence(const wish *w, char *out, size_t n)
{
    sb s = { out, n, 0 };
    out[0] = 0;
    when_phrase(&s, &w->when);
    sp(&s, ", ");
    for (int i = 0; i < w->n; i++) {
        if (i) sp(&s, i == w->n - 1 ? " and " : ", ");
        do_phrase(&s, &w->d[i]);
    }
    sp(&s, ".");
    if (w->when.type == W_NOW) {
        int ch = 0;
        for (int i = 0; i < w->n; i++) ch |= changes_setting(w->d[i].type);
        if (ch) sp(&s, " You can put it back with *Undo my last wish*.");
    } else sp(&s, " To stop it, press *Stop this wish*, or switch it off in *My wishes*.");
    return (int)s.len;
}

int wish_title(const wish *w, char *out, size_t n)
{
    char t[200], c[32];
    sb s = { t, sizeof t, 0 };
    t[0] = 0;
    const wish_do *d = &w->d[0];
    static const char *const SHORT[A_N] = {
        [A_MESSAGE] = "A note", [A_REMIND] = "A reminder", [A_SPEAK] = "Words out loud",
        [A_SPEAK_TIME] = "The time out loud", [A_BRIGHT] = "Brightness", [A_VOLUME] = "Volume",
        [A_LOOK] = "A new look", [A_TEXT] = "Text size", [A_WALL] = "Background",
        [A_NIGHT] = "Night colours", [A_POINTER] = "Pointer size", [A_DND] = "Quiet time" };
    if (d->type == A_FILL) { if (d->emoji[0]) sp(&s, "%s on the screen", d->emoji); else sp(&s, "Words on the screen"); }
    else if (d->type == A_SOUND) sp(&s, "A %s sound", SOUND_WORDS[d->sound]);
    else if (d->type == A_OPEN_APP) sp(&s, "Open %s", d->app.label);
    else if (d->type == A_OPEN_SITE) sp(&s, "Open %s", d->site_name);
    else sp(&s, "%s", SHORT[d->type]);
    const wish_when *x = &w->when;
    switch (x->type) {
    case W_TYPED: sp(&s, " when you type %s", x->word); break;
    case W_TIME: clock_words(x->hh, x->mm, c, sizeof c); sp(&s, " at %s", c); break;
    case W_EVERY: sp(&s, " every %d min", x->minutes); break;
    case W_STARTUP: sp(&s, " at start"); break;
    case W_APP: sp(&s, " with %s", x->app.label); break;
    case W_BATTERY: sp(&s, " on low battery"); break;
    case W_PLUGGED: sp(&s, " when charging"); break;
    case W_UNPLUGGED: sp(&s, " off the charger"); break;
    case W_USB: sp(&s, " with USB"); break;
    case W_IDLE: sp(&s, " when idle"); break;
    case W_SENSOR:
        if (x->sensor == S_COMPASS) sp(&s, " facing %s", HEADINGS[x->heading]);
        else if (x->sensor == S_TILT) sp(&s, " when turned");
        else sp(&s, x->lux_above ? " in bright light" : " in the dark");
        break;
    }
    /* At most 40 characters, cut on a character boundary. */
    const char *p = t;
    int g = 0;
    size_t cut = 0;
    while (*p) {
        const char *q = p;
        utf8_next(&q);
        if (++g > 40) break;
        p = q;
        cut = (size_t)(p - t);
    }
    if (*p) {                                  /* too long: end with … */
        g = 0; p = t; cut = 0;
        while (*p && g < 39) { const char *q = p; utf8_next(&q); p = q; g++; cut = (size_t)(p - t); }
        snprintf(out, n, "%.*s\xE2\x80\xA6", (int)cut, t);
    } else snprintf(out, n, "%s", t);
    return (int)strlen(out);
}

/* ── writing the document back ───────────────────────────────────── */

static void hhmm_out(jout *o, int h, int m)
{
    char t[8];
    snprintf(t, sizeof t, "%02d:%02d", h, m);
    jw_str(o, t);
}

static void do_fields(const wish_do *d, jout *o, int resolved)
{
    jw_key(o, "type"); jw_str(o, DO_NAMES[d->type]);
    switch (d->type) {
    case A_FILL:
        if (d->emoji[0]) { jw_key(o, "emoji"); jw_str(o, d->emoji); }
        else { jw_key(o, "text"); jw_str(o, d->text); }
        jw_key(o, "seconds"); jw_int(o, d->seconds);
        break;
    case A_MESSAGE: case A_SPEAK:
        jw_key(o, "text"); jw_str(o, d->text); break;
    case A_REMIND:
        jw_key(o, "text"); jw_str(o, d->text);
        if (d->priv) { jw_key(o, "private"); jw_raw(o, "true"); }
        break;
    case A_SOUND: jw_key(o, "sound"); jw_str(o, SOUNDS[d->sound]); break;
    case A_BRIGHT: case A_VOLUME:
        jw_key(o, d->is_set ? "set_percent" : "change_percent"); jw_int(o, d->pct); break;
    case A_OPEN_APP:
        if (resolved) { jw_key(o, "app"); jw_str(o, d->app.id); }
        else { jw_key(o, "app_name"); jw_str(o, d->app.name); }
        break;
    case A_OPEN_SITE:
        if (resolved) { jw_key(o, "url"); jw_str(o, d->url); }
        else { jw_key(o, "site"); jw_str(o, d->site); }
        break;
    case A_LOOK: jw_key(o, "look"); jw_str(o, LOOKS[d->look]); break;
    case A_TEXT: case A_POINTER: jw_key(o, "size"); jw_str(o, SIZES[d->size]); break;
    case A_WALL:
        if (d->has_color) {
            char c[8]; snprintf(c, sizeof c, "#%06X", d->color & 0xFFFFFF);
            jw_key(o, "color"); jw_str(o, c);
        } else if (resolved) { jw_key(o, "theme"); jw_str(o, d->theme); }
        else { jw_key(o, "theme_name"); jw_str(o, d->theme_name); }
        break;
    case A_NIGHT: case A_DND:
        jw_key(o, "on"); jw_raw(o, d->on ? "true" : "false");
        if (d->has_until) { jw_key(o, "until"); hhmm_out(o, d->until_hh, d->until_mm); }
        break;
    case A_SPEAK_TIME: break;
    }
}

void wish_do_json(const wish_do *d, jout *o)
{
    jw_raw(o, "{");
    do_fields(d, o, 1);
    jw_raw(o, "}");
}

void wish_json(const wish *w, jout *o)
{
    const wish_when *x = &w->when;
    jw_raw(o, "{");
    jw_key(o, "v"); jw_int(o, 1);
    jw_key(o, "when"); jw_raw(o, "{");
    jw_key(o, "type"); jw_str(o, WHEN_NAMES[x->type]);
    switch (x->type) {
    case W_TYPED: jw_key(o, "word"); jw_str(o, x->word); break;
    case W_TIME:
        jw_key(o, "at"); hhmm_out(o, x->hh, x->mm);
        if (x->days != 0x7F) {
            jw_key(o, "days"); jw_raw(o, "[");
            int first = 1;
            for (int i = 0; i < 7; i++)
                if (x->days & (1u << i)) { if (!first) jw_raw(o, ","); jw_str(o, DAYS[i]); first = 0; }
            jw_raw(o, "]");
        }
        break;
    case W_EVERY: case W_IDLE: jw_key(o, "minutes"); jw_int(o, x->minutes); break;
    case W_APP: jw_key(o, "app_name"); jw_str(o, x->app.name); break;
    case W_BATTERY: jw_key(o, "percent"); jw_int(o, x->percent); break;
    case W_SENSOR:
        jw_key(o, "sensor"); jw_str(o, SENSORS[x->sensor]);
        if (x->sensor == S_COMPASS) {
            jw_key(o, "heading"); jw_str(o, HEADINGS[x->heading]);
            jw_key(o, "tolerance_deg"); jw_int(o, x->tol);
        } else if (x->sensor == S_TILT) { jw_key(o, "orientation"); jw_str(o, ORIENTS[x->orient]); }
        else { jw_key(o, x->lux_above ? "above_lux" : "below_lux"); jw_int(o, x->lux); }
        break;
    }
    jw_raw(o, "}");
    jw_key(o, "do"); jw_raw(o, "[");
    for (int i = 0; i < w->n; i++) {
        if (i) jw_raw(o, ",");
        jw_raw(o, "{");
        do_fields(&w->d[i], o, 0);
        jw_raw(o, "}");
    }
    jw_raw(o, "]}");
}
