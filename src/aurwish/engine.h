/* engine.h — triggers, limits and actions, with no I/O of its own.
 *
 * Everything that happens in the world arrives as a call (a word was
 * typed, the battery moved, a second passed) carrying the time; everything
 * the engine does leaves through engine_io. So the whole runtime can be
 * driven by tools/wishtest-engine.c with fake sources and a fake clock,
 * and what is tested is exactly what runs. */
#ifndef AURWISH_ENGINE_H
#define AURWISH_ENGINE_H

#include "store.h"

typedef struct {
    /* One line to every connected shell; returns how many received it. */
    int  (*emit)(void *ctx, const char *line);
    /* The engine's own actions (§3.7). Each returns -1 on failure. */
    int  (*bright_get)(void *ctx);
    int  (*bright_set)(void *ctx, int pct);
    int  (*vol_get)(void *ctx);
    int  (*vol_set)(void *ctx, int pct);
    int  (*sound)(void *ctx, int sound);
    int  (*say)(void *ctx, const char *text);
    void *ctx;
    /* "Forget everything" / Wishes turned off (§7.12): the daemon tells
     * the cloud and deletes the token. May be NULL. */
    int  (*forget)(void *ctx);
} engine_io;

/* §7.6 reasons the shell reports. */
enum { H_PASSWORD = 1, H_CALL = 2, H_CAMERA = 4, H_MIC = 8, H_SHARE = 16,
       H_FULLSCREEN = 32, H_LOCK = 64 };

typedef struct engine engine;

/* Takes ownership of nothing; `st` and `env` must outlive it. `path` is
 * where to save (NULL = never save, for tests that only look). */
engine *engine_new(store *st, const char *path, wish_env *env, const engine_io *io);
void    engine_free(engine *e);

/* Sources. `now` is seconds since the epoch; local time follows TZ. */
void engine_tick(engine *e, long long now);           /* at least once a second */
void engine_startup(engine *e, long long now);        /* once per login */
void engine_typed(engine *e, const char *word, long long now);
void engine_app(engine *e, const char *app, long long now);
void engine_input(engine *e, long long now);
void engine_hold(engine *e, unsigned reasons, long long now);        /* from the shell */
void engine_hold_local(engine *e, unsigned reasons);                 /* found by the daemon */
void engine_battery(engine *e, int present, int percent, int plugged, long long now);
void engine_usb(engine *e, long long now);
void engine_compass(engine *e, double heading, long long now);
void engine_tilt(engine *e, const char *orientation, long long now);
void engine_light(engine *e, double lux, long long now);
void engine_sensors(engine *e, unsigned have);        /* what this computer has */

/* One request line from a shell (§3.7) that the engine answers itself:
 * everything except wish/ask/cancel, which go to the cloud. Writes the
 * reply line (no newline) and returns 0, or returns 1 when the line is a
 * cloud op the caller must handle, or -1 when the connection should be
 * closed (not JSON, not an object). Lines without an id get no reply
 * (reply[0] == 0). */
int  engine_request(engine *e, const char *line, size_t len, char *reply, size_t n,
                    long long now);

/* After the cloud answered: validate whatever came as if an attacker wrote
 * it (§7.1) and write the reply for request `id`. All text in the reply is
 * the device's own.
 *   OFFER_WISH    `doc` is a wish document      → wish | cannot
 *   OFFER_CANNOT  `tag` is the cloud's `missing`; `doc` its alt_wish or NULL
 *                                               → cannot (with alt when valid)
 *   OFFER_HELP    `tag` is help_page_id or NULL; `doc` an optional wish
 *                                               → help */
enum { OFFER_WISH, OFFER_CANNOT, OFFER_HELP };
void engine_offer(engine *e, long long id, int how, const char *tag, const jv *doc,
                  const char *said, int voice, char *reply, size_t n, long long now);

/* 1 when the daemon should wake every second (a sensor, idle or held
 * thing is waiting), 0 when a slow tick will do. */
int  engine_wants_fast(const engine *e, long long now);
/* What sensors, battery or charger events any wish is waiting for. */
unsigned engine_wants(const engine *e);
enum { WANT_COMPASS = 1, WANT_TILT = 2, WANT_LIGHT = 4, WANT_BATTERY = 8, WANT_USB = 16 };

/* The trigger list the compositor may match (§3.7 "triggers"). */
void engine_triggers_line(engine *e, char *out, size_t n);

/* Lines the engine itself writes (for tests and main.c). */
const char *engine_cannot_text(const char *missing);

#endif
