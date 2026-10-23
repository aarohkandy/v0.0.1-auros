/* wish.h — the wish document v1, compiled in (docs/AI.md §2, §7).
 *
 * The only description of a wish she ever reads is the one this file
 * writes (§1.2, §7.3). Nothing the cloud sends is shown as a sentence:
 * it is parsed into the struct below, every field checked, and the
 * sentence is rebuilt from the struct. Anything not understood is a
 * refusal -- never a default, never "best effort". */
#ifndef AURWISH_WISH_H
#define AURWISH_WISH_H

#include <stddef.h>
#include "json.h"

enum { W_NOW, W_TYPED, W_TIME, W_EVERY, W_STARTUP, W_APP, W_BATTERY,
       W_PLUGGED, W_UNPLUGGED, W_USB, W_IDLE, W_SENSOR, W_N };
enum { S_COMPASS, S_TILT, S_LIGHT };
enum { A_FILL, A_MESSAGE, A_REMIND, A_SPEAK, A_SOUND, A_BRIGHT, A_VOLUME,
       A_OPEN_APP, A_OPEN_SITE, A_LOOK, A_TEXT, A_WALL, A_NIGHT,
       A_SPEAK_TIME, A_POINTER, A_DND, A_N };
enum { SND_CHIME, SND_BELL, SND_BIRDS, SND_APPLAUSE, SND_GENTLE, SND_N };

#define WISH_MAX_DO   4
#define WISH_TEXT     600      /* bytes of one free text, after checks */

/* An installed program, as the device found it (never as the model
 * named it). */
typedef struct {
    char name[64];     /* what the model asked for (kept for re-checks) */
    char id[128];      /* desktop id without ".desktop" */
    char wmclass[128]; /* StartupWMClass, if any */
    char label[64];    /* its Name=, checked: what she reads */
} wish_app;

typedef struct {
    int type;
    char word[33];               /* typed_word, lowercase a-z */
    int hh, mm;                  /* time */
    unsigned days;               /* time: bit 0 = Monday; 0x7F every day */
    int minutes;                 /* every, idle */
    wish_app app;                /* app_opened */
    int percent;                 /* battery_below */
    int sensor, heading, tol, orient;
    int lux, lux_above;          /* light: lux threshold; 1 = above_lux */
} wish_when;

typedef struct {
    int type;
    char emoji[32];
    char text[WISH_TEXT];
    int seconds;                 /* 0 when absent */
    int sound;
    int is_set, pct;             /* brightness/volume: set_percent or change */
    wish_app app;                /* open_app */
    char site[64], site_name[64], url[160];
    int look;                    /* 0 taskbar 1 dock 2 rail 3 tiles */
    int size;                    /* 0 normal 1 bigger 2 biggest */
    int has_color; unsigned color;
    char theme_name[64], theme[64], theme_label[64];
    int on;
    int has_until, until_hh, until_mm;
    int priv;                    /* remind: show privately */
} wish_do;

typedef struct {
    wish_when when;
    int n;
    wish_do d[WISH_MAX_DO];
} wish;

/* What the device knows that the cloud does not. */
typedef struct {
    const char *said;        /* her own words, for §7.3; NULL refuses free text */
    unsigned    sensors;     /* 1<<S_COMPASS | ... : what this computer has */
    const char *apps_dirs;   /* colon-separated; NULL = the XDG defaults */
    const char *themes_dir;  /* NULL = /usr/share/auros/themes */
    unsigned    fg;          /* her theme's text colour, for §7.4 contrast */
} wish_env;

typedef struct {
    const char *missing;     /* compass tilt light app theme site install other */
    const char *why;         /* for logs and tests; never shown to her */
    char where[40];          /* which field */
} wish_err;

/* 0 when the document is a wish this device will run. */
int wish_parse(const jv *doc, const wish_env *env, wish *out, wish_err *err);
int wish_parse_text(const char *json, size_t len, const wish_env *env,
                    wish *out, wish_err *err);

/* The device's own sentence (§1.2, §7.9): when, what, how long, how to
 * stop. '*' toggles bold. And a title of at most 40 characters. */
int wish_sentence(const wish *w, char *out, size_t n);
int wish_title(const wish *w, char *out, size_t n);

/* The document, as the model would write it (for wishes.json). */
void wish_json(const wish *w, jout *o);
/* One action as the shell receives it: resolved (desktop id, URL, theme
 * id), never a name the model wrote. */
void wish_do_json(const wish_do *d, jout *o);

/* What kind of thing an action is, for the runtime's rules. */
int wish_do_overlay(int type);   /* fill, message, remind */
int wish_do_sound(int type);     /* play_sound, speak, speak_time */
int wish_do_outward(int type);   /* open_app, open_website */

/* The parse limits for one wish document. */
extern const jlimits WISH_LIMITS;

const char *wish_when_name(int i);    /* the v1 trigger names, for the cloud's schema field */
const char *wish_do_name(int i);      /* ... and action names */
const char *wish_sound_name(int s);
const char *wish_look_id(int l);

#endif
