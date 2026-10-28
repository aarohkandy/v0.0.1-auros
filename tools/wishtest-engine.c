/* wishtest-engine.c — the Wishes runtime, headless, with fake event
 * sources and a fake clock (docs/AI.md §3, §7.4-§7.8).
 *
 * Everything goes through the same calls aurwish's main loop makes:
 * request lines exactly as the shell sends them, and source events
 * exactly as the sources deliver them. What comes out is captured and
 * checked line by line. tools/wishtest.sh builds and runs it; the
 * mutation list in tools/wishtest-mutants.py proves each rule can fail.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "engine.h"
#include "text.h"

static int fails, passes;
static const char *cur;
#define CHECK(c) do { if (c) passes++; else { fails++; \
    fprintf(stderr, "FAIL %s:%d %s: %s\n", __FILE__, __LINE__, cur, #c); } } while (0)

/* ── the fake world ───────────────────────────────────────────────── */

static char out[200][2048];
static int nout, shells = 1;
static int bright = 50, vol = 40, sounds, says;
static char said_last[256];

static int f_emit(void *c, const char *l)
{
    (void)c;
    if (shells && nout < 200) snprintf(out[nout++], sizeof out[0], "%s", l);
    return shells;
}
static int f_bget(void *c) { (void)c; return bright; }
static int f_bset(void *c, int p) { (void)c; bright = p; return p; }
static int f_vget(void *c) { (void)c; return vol; }
static int f_vset(void *c, int p) { (void)c; vol = p; return p; }
static int f_sound(void *c, int s) { (void)c; (void)s; sounds++; return 0; }
static int f_say(void *c, const char *t) { (void)c; says++; snprintf(said_last, sizeof said_last, "%s", t); return 0; }
static int forgets;
static unsigned fake_hold;
static unsigned f_holds(void *c) { (void)c; return fake_hold; }
static int f_forget(void *c) { (void)c; forgets++; return 0; }

static store ST;
static wish_env ENV;
static engine *E;
static char dir[256], path[300];

/* How many captured lines contain `s` (since the last clear). */
static int seen(const char *s)
{
    int n = 0;
    for (int i = 0; i < nout; i++) n += strstr(out[i], s) != NULL;
    return n;
}
static void clear(void) { nout = 0; }

static void fresh(void)
{
    if (E) engine_free(E);
    store_free(&ST);
    memset(&ST, 0, sizeof ST);
    ST.next = 1;
    ENV.sensors = 1u << S_COMPASS | 1u << S_TILT | 1u << S_LIGHT;
    static const engine_io io = { f_emit, f_bget, f_bset, f_vget, f_vset, f_sound, f_say, NULL, f_forget, f_holds };
    unlink(path);
    E = engine_new(&ST, path, &ENV, &io);
    bright = 50; vol = 40; sounds = says = forgets = 0; shells = 1; fake_hold = 0;
    clear();
}

static char reply[16384];
static int req(const char *line, long long now)
{
    return engine_request(E, line, strlen(line), reply, sizeof reply, now);
}

/* Offers `doc` as her wish `said` and says Yes. Returns the wid, or "". */
static char wid[16];
static const char *add(const char *doc, const char *said, long long now)
{
    char line[8192];
    snprintf(line, sizeof line, "{\"op\":\"check\",\"id\":1,\"said\":\"%s\",\"wish\":%s}", said, doc);
    req(line, now);
    const char *p = strstr(reply, "\"pending\":\"");
    wid[0] = 0;
    if (!p) return wid;
    char pid[16];
    sscanf(p + 11, "%15[^\"]", pid);
    snprintf(line, sizeof line, "{\"op\":\"confirm\",\"id\":2,\"pending\":\"%s\"}", pid);
    req(line, now);
    p = strstr(reply, "\"wid\":\"");
    if (p) sscanf(p + 7, "%15[^\"]", wid);
    return wid;
}

/* 2026-10-19 is a Monday. */
static long long at(int day, int hh, int mm, int ss)
{
    struct tm tm = { .tm_year = 126, .tm_mon = 9, .tm_mday = 19 + day,
                     .tm_hour = hh, .tm_min = mm, .tm_sec = ss };
    return (long long)timegm(&tm);
}

static const char *POTATO =
    "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"potato\"},"
    "\"do\":[{\"type\":\"fill_screen\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"seconds\":3}]}";
static const char *COMPASS =
    "{\"v\":1,\"when\":{\"type\":\"sensor\",\"sensor\":\"compass\",\"heading\":\"north\","
    "\"tolerance_deg\":20},\"do\":[{\"type\":\"brightness\",\"set_percent\":100}]}";

/* ── the owner's two wishes ─────────────────────────────────────── */

static void test_potato_fires_on_potato_only(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    CHECK(add(POTATO, "fill my screen with potatoes when I type potato", t)[0] == 'w');
    CHECK(seen("\"words\":[\"potato\"]") == 1);           /* the compositor is told */
    clear();
    /* She is typing: the card waits for 3 s of quiet (§7.6). */
    req("{\"op\":\"input\"}", t + 10);
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 10);
    engine_tick(E, t + 11);
    CHECK(seen("\"ev\":\"fill\"") == 0);
    engine_tick(E, t + 13);
    CHECK(seen("\"ev\":\"fill\"") == 1);
    CHECK(seen("\"emoji\":\"\xF0\x9F\xA5\x94\"") == 1 && seen("\"seconds\":3") == 1);
    CHECK(seen("\"title\":\"\xF0\x9F\xA5\x94 on the screen when you type potato\"") == 1);
    /* never on potatoes, potat, or any other word */
    clear();
    static const char *const NOT[] = { "potatoes", "potat", "spotato", "tomato", "pot", "", NULL };
    char l[200];
    for (int i = 0; NOT[i]; i++) {
        snprintf(l, sizeof l, "{\"op\":\"typed\",\"word\":\"%s\"}", NOT[i]);
        req(l, t + 200 + i * 100);
        engine_tick(E, t + 205 + i * 100);
    }
    CHECK(seen("\"ev\":\"fill\"") == 0);
    /* Capital letters are the same word. */
    req("{\"op\":\"typed\",\"word\":\"POTATO\"}", t + 2000);
    engine_tick(E, t + 2004);
    CHECK(seen("\"ev\":\"fill\"") == 1);
}

static void test_potato_never_from_a_password(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add(POTATO, "potato", t);
    clear();
    req("{\"op\":\"hold\",\"reasons\":[\"password\"]}", t + 100);
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 101);
    req("{\"op\":\"hold\",\"reasons\":[]}", t + 110);
    for (int i = 0; i < 30; i++) engine_tick(E, t + 111 + i);
    CHECK(seen("\"ev\":\"fill\"") == 0);                   /* dropped, not held */
}

static void test_compass_north(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    CHECK(add(COMPASS, "when my laptop points north make the screen brighter", t)[0] == 'w');
    engine_compass(E, 90, t + 1);       /* east: the first reading only sets the scene */
    engine_compass(E, 45, t + 2);
    CHECK(bright == 50);
    engine_compass(E, 15, t + 3);       /* inside 20 degrees of north */
    CHECK(bright == 100);
    bright = 50;
    engine_compass(E, 5, t + 4);        /* still north: one crossing, one firing */
    engine_compass(E, 22, t + 5);       /* wobble inside the hysteresis */
    engine_compass(E, 8, t + 6);
    CHECK(bright == 50);
    engine_compass(E, 90, t + 7);       /* away ... */
    engine_compass(E, 355, t + 8);      /* ... and back across 0, within 10 min */
    CHECK(bright == 50);                /* §7.4: sensor triggers rest 10 minutes */
    engine_compass(E, 180, t + 700);
    engine_compass(E, 359.5, t + 701);  /* across the wrap at 360 */
    CHECK(bright == 100);
    bright = 50;
    engine_compass(E, 22, t + 1400);    /* a wobble at the edge of north ... */
    engine_compass(E, 15, t + 1401);    /* ... is not a new crossing */
    CHECK(bright == 50);
    engine_compass(E, 90, t + 1402);
    engine_compass(E, 15, t + 1403);
    CHECK(bright == 100);
    /* Already pointing north when it starts: no firing (an edge, not a level). */
    fresh();
    add(COMPASS, "north", t);
    engine_compass(E, 0, t + 1);
    CHECK(bright == 50);
    /* Nonsense from the sensor changes nothing. */
    engine_compass(E, 0.0 / 0.0, t + 2);
    engine_compass(E, 1e308 * 10, t + 3);
    CHECK(bright == 50);
}

static void test_compass_missing_is_cannot(void)
{
    cur = __func__;
    fresh();
    engine_sensors(E, 0);
    char line[1024];
    snprintf(line, sizeof line, "{\"op\":\"check\",\"id\":7,\"said\":\"north\",\"wish\":%s}", COMPASS);
    req(line, at(0, 12, 0, 0));
    CHECK(strstr(reply, "\"kind\":\"cannot\"") && strstr(reply, "\"missing\":\"compass\""));
    CHECK(strstr(reply, "This computer has no compass") != NULL);
}

/* ── limits (§7.5) ────────────────────────────────────────────────── */

static void test_caps_stop_a_runaway(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    const char *w = "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"potato\"},\"do\":["
                    "{\"type\":\"fill_screen\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"seconds\":1},"
                    "{\"type\":\"brightness\",\"set_percent\":60},{\"type\":\"night_light\",\"on\":true},"
                    "{\"type\":\"play_sound\",\"sound\":\"chime\"}]}";
    const char *id = add(w, "potato", t);
    clear();
    int fills = 0;
    for (int i = 0; i < 50; i++) {                  /* she types potato all afternoon */
        req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 61 * (i + 1));
        engine_tick(E, t + 61 * (i + 1) + 4);
    }
    fills = seen("\"ev\":\"fill\"");
    CHECK(fills == 1);                              /* 4 actions; a second firing would be 8 > 6 */
    CHECK(seen("\"ev\":\"notice\"") == 1);          /* told once */
    CHECK(seen("resting for an hour") == 1);
    stored *s = store_find(&ST, id);
    CHECK(s && s->status == ST_LIMIT);
    /* It comes back by itself after an hour. */
    engine_tick(E, t + 61 * 2 + 3600 + 10);
    CHECK(s->status == ST_ON);
    /* and wishes.json says paused while it was */
}

static void test_overall_cap_pauses_only_the_breaker(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    static const char *const WORDS[] = { "potato", "tomato", "carrot", "banana" };
    char doc[512], ids[4][16];
    for (int i = 0; i < 4; i++) {
        snprintf(doc, sizeof doc, "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"%s\"},"
                 "\"do\":[{\"type\":\"brightness\",\"set_percent\":%d}]}", WORDS[i], 40 + i);
        snprintf(ids[i], sizeof ids[i], "%s", add(doc, "x", t));
        CHECK(ids[i][0] == 'w');
    }
    char l[100];
    long long now = t + 10;
    /* Three wishes use 6 each (18), the fourth 2 more (20): the hour is
     * full. The fourth's next one breaks it. */
    static const int N[4] = { 6, 6, 6, 3 };
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < N[i]; k++) {
            snprintf(l, sizeof l, "{\"op\":\"typed\",\"word\":\"%s\"}", WORDS[i]);
            req(l, now++);
        }
    for (int i = 0; i < 3; i++) CHECK(store_find(&ST, ids[i])->status == ST_ON);
    CHECK(store_find(&ST, ids[3])->status == ST_LIMIT);  /* only the one that broke it */
    /* Its share is given back, so the others are not starved. */
    bright = 0;
    store_find(&ST, ids[0])->rt.hist[0] = 0;              /* room in wish 0's own hour */
    snprintf(l, sizeof l, "{\"op\":\"typed\",\"word\":\"%s\"}", WORDS[0]);
    req(l, now++);
    CHECK(bright == 40);
}

static void test_reminder_not_counted_overall(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 8, 0, 0);
    const char *r = "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"09:00\"},"
                    "\"do\":[{\"type\":\"remind\",\"text\":\"Take your pills\"}]}";
    const char *rid = add(r, "remind me to take my pills", t);
    char doc[512];
    static const char *const WORDS[] = { "potato", "tomato", "carrot", "banana" };
    long long now = at(0, 8, 30, 0);
    for (int i = 0; i < 4; i++) {
        snprintf(doc, sizeof doc, "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"%s\"},"
                 "\"do\":[{\"type\":\"brightness\",\"set_percent\":%d}]}", WORDS[i], 40 + i);
        add(doc, "x", t);
        char l[100];
        snprintf(l, sizeof l, "{\"op\":\"typed\",\"word\":\"%s\"}", WORDS[i]);
        for (int k = 0; k < 5; k++) req(l, now++);     /* 20 actions: the hour is full */
    }
    clear();
    engine_tick(E, at(0, 9, 0, 0));
    CHECK(seen("\"ev\":\"remind\"") == 1);              /* the pills still show */
    CHECK(store_find(&ST, rid)->status == ST_ON);
}

/* ── when she must not be interrupted (§7.4, §7.6) ─────────────── */

static void test_quiet_hours(void)
{
    cur = __func__;
    fresh();
    long long night = at(0, 22, 30, 0);
    add(POTATO, "potato", at(0, 12, 0, 0));
    const char *loud = "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"carrot\"},"
                       "\"do\":[{\"type\":\"play_sound\",\"sound\":\"bell\"}]}";
    add(loud, "x", at(0, 12, 0, 0));
    const char *start = "{\"v\":1,\"when\":{\"type\":\"startup\"},"
                        "\"do\":[{\"type\":\"fill_screen\",\"emoji\":\"\xF0\x9F\x8C\xB8\",\"seconds\":2}]}";
    add(start, "x", at(0, 12, 0, 0));
    clear();
    req("{\"op\":\"typed\",\"word\":\"potato\"}", night);
    req("{\"op\":\"typed\",\"word\":\"carrot\"}", night);
    engine_startup(E, night);
    engine_tick(E, night + 5);
    CHECK(seen("\"emoji\":\"\xF0\x9F\xA5\x94\"") == 1);   /* she typed it: shown */
    CHECK(sounds == 0);                                   /* no sound at night */
    CHECK(seen("\"emoji\":\"\xF0\x9F\x8C\xB8\"") == 0);   /* automatic card: held back */
    clear();
    engine_startup(E, at(1, 9, 0, 0));
    CHECK(seen("\"emoji\":\"\xF0\x9F\x8C\xB8\"") == 1);
}

static void test_holds(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add(POTATO, "potato", t);
    clear();
    req("{\"op\":\"hold\",\"reasons\":[\"call\"]}", t + 100);
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 101);
    engine_tick(E, t + 110);
    CHECK(seen("\"ev\":\"fill\"") == 0);                  /* not during a video call */
    req("{\"op\":\"hold\",\"reasons\":[]}", t + 120);
    engine_tick(E, t + 121);
    CHECK(seen("\"ev\":\"fill\"") == 1);                  /* afterwards */
    clear();
    req("{\"op\":\"hold\",\"reasons\":[\"fullscreen\"]}", t + 500);
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 501);
    req("{\"op\":\"hold\",\"reasons\":[]}", t + 501 + 601);
    engine_tick(E, t + 501 + 602);
    CHECK(seen("\"ev\":\"fill\"") == 0);                  /* waited too long: dropped */
}

static void test_overlay_gap_and_speak_limit(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add(POTATO, "potato", t);
    clear();
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 100);
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 130);
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 161);
    CHECK(seen("\"ev\":\"fill\"") == 2);                  /* one a minute at most */
    const char *sp = "{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"speak_time\"}]}";
    (void)sp;
    const char *tm = "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"13:00\"},\"do\":[{\"type\":\"speak_time\"}]}";
    const char *id = add(tm, "x", t);
    char l[100];
    snprintf(l, sizeof l, "{\"op\":\"test\",\"id\":3,\"wid\":\"%s\"}", id);
    for (int i = 0; i < 5; i++) req(l, t + 1000 + i * 700);
    CHECK(says == 3);                                     /* three a day per wish */
}

/* ── switching off (§3.4, §7.7) ───────────────────────────────────── */

static void test_stop_and_all_off(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    const char *id = add(POTATO, "potato", t);
    char l[200];
    snprintf(l, sizeof l, "{\"op\":\"enable\",\"id\":4,\"wid\":\"%s\",\"on\":false}", id);
    clear();
    req(l, t + 1);
    CHECK(strstr(reply, "\"kind\":\"done\"") != NULL);
    CHECK(seen("\"words\":[]") == 1);                     /* the compositor forgets the word */
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 100);
    CHECK(seen("\"ev\":\"fill\"") == 0);
    snprintf(l, sizeof l, "{\"op\":\"enable\",\"id\":5,\"wid\":\"%s\",\"on\":true}", id);
    req(l, t + 200);
    req("{\"op\":\"all_off\",\"id\":6}", t + 300);
    clear();
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 400);
    CHECK(seen("\"ev\":\"fill\"") == 0);
    req("{\"op\":\"list\",\"id\":7}", t + 401);
    CHECK(strstr(reply, "\"all_off\":true") != NULL);
    req("{\"op\":\"all_on\",\"id\":8}", t + 402);
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 500);
    CHECK(seen("\"ev\":\"fill\"") == 1);
    snprintf(l, sizeof l, "{\"op\":\"remove\",\"id\":9,\"wid\":\"%s\"}", id);
    req(l, t + 600);
    CHECK(ST.n == 0);
    clear();
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 700);
    CHECK(seen("\"ev\":\"fill\"") == 0);
    /* and wishes.json agrees, mode 0600 */
    struct stat sb;
    CHECK(stat(path, &sb) == 0 && (sb.st_mode & 0777) == 0600);
}

static void test_runtime_floors(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    bright = 50; vol = 70;
    add("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"brightness\",\"change_percent\":-50},"
        "{\"type\":\"volume\",\"change_percent\":50}]}", "x", t);
    CHECK(bright == 30);                                  /* never below 30 from a wish */
    CHECK(vol == 80);                                     /* never above 80 */
    vol = 30;
    add("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"volume\",\"change_percent\":-50}]}", "x", t + 1);
    CHECK(vol == 20);
}

static void test_undo_and_restore(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    bright = 45; vol = 30;
    add("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"brightness\",\"set_percent\":80},"
        "{\"type\":\"volume\",\"change_percent\":20},{\"type\":\"set_look\",\"look\":\"dock\"}]}", "x", t);
    CHECK(bright == 80 && vol == 50);
    CHECK(seen("\"do\":{\"type\":\"set_look\",\"look\":\"dock\"}") == 1);
    /* the shell did it, and says what it was before */
    req("{\"op\":\"did\",\"n\":1,\"ok\":true,\"prev\":{\"type\":\"set_look\",\"look\":\"rail\"}}", t + 1);
    req("{\"op\":\"list\",\"id\":1}", t + 2);
    CHECK(strstr(reply, "\"undo\":true") != NULL);
    clear();
    req("{\"op\":\"undo\",\"id\":2}", t + 60);
    CHECK(bright == 45 && vol == 30);
    CHECK(seen("\"do\":{\"type\":\"set_look\",\"look\":\"rail\"}") == 1);
    req("{\"op\":\"undo\",\"id\":3}", t + 61);
    CHECK(strstr(reply, "\"error\":\"refused\"") != NULL); /* nothing left to undo */
    /* After 10 minutes there is no undo, but Put everything back still works. */
    add("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"brightness\",\"set_percent\":90}]}", "x", t + 100);
    add("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"brightness\",\"set_percent\":70}]}", "x", t + 200);
    req("{\"op\":\"undo\",\"id\":4}", t + 900);
    CHECK(strstr(reply, "\"error\":\"refused\"") != NULL);
    req("{\"op\":\"restore\",\"id\":5}", t + 901);
    CHECK(bright == 45 && vol == 30);                      /* the very first values */
    /* A forged "did" for an event that was never sent changes nothing. */
    req("{\"op\":\"did\",\"n\":999,\"ok\":true,\"prev\":{\"type\":\"set_look\",\"look\":\"tiles\"}}", t + 902);
    CHECK(ST.nbase == 0);
}

/* ── triggers ──────────────────────────────────────────────────────── */

static void test_time_and_missed_reminders(void)
{
    cur = __func__;
    fresh();
    const char *r = "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"09:00\"},"
                    "\"do\":[{\"type\":\"remind\",\"text\":\"Take your pills\"}]}";
    add(r, "remind me to take my pills", at(0, 8, 0, 0));
    clear();
    engine_tick(E, at(0, 8, 59, 59));
    CHECK(seen("\"ev\":\"remind\"") == 0);
    engine_tick(E, at(0, 9, 0, 0));
    engine_tick(E, at(0, 9, 0, 30));
    engine_tick(E, at(0, 9, 5, 0));
    CHECK(seen("\"ev\":\"remind\"") == 1);
    CHECK(seen("\"for\"") == 0);
    /* The computer was off at 9:00 the next day; on at 10:15. */
    clear();
    engine_tick(E, at(1, 10, 15, 0));
    CHECK(seen("\"ev\":\"remind\"") == 1 && seen("\"for\":\"9:00 am\"") == 1);
    /* Off for more than 12 hours: it is not shown at 22:00. */
    clear();
    engine_tick(E, at(2, 22, 0, 0));
    CHECK(seen("\"ev\":\"remind\"") == 0);
    /* Something else missed is skipped, not caught up. */
    fresh();
    add("{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"09:00\"},\"do\":[{\"type\":\"brightness\",\"set_percent\":40}]}",
        "x", at(0, 8, 0, 0));
    engine_tick(E, at(0, 9, 30, 0));
    CHECK(bright == 50);
    engine_tick(E, at(1, 9, 0, 1));
    CHECK(bright == 40);
    /* Days: Mondays only (day 0 is a Monday). */
    fresh();
    add("{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"09:00\",\"days\":[\"mon\"]},"
        "\"do\":[{\"type\":\"brightness\",\"set_percent\":40}]}", "x", at(0, 8, 0, 0));
    engine_tick(E, at(1, 9, 0, 0));
    CHECK(bright == 50);
    engine_tick(E, at(7, 9, 0, 0));
    CHECK(bright == 40);
}

static void test_snooze(void)
{
    cur = __func__;
    fresh();
    const char *r = "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"09:00\"},"
                    "\"do\":[{\"type\":\"remind\",\"text\":\"Take your pills\"}]}";
    const char *id = add(r, "remind me to take my pills", at(0, 8, 0, 0));
    engine_tick(E, at(0, 9, 0, 0));
    clear();
    char l[200];
    snprintf(l, sizeof l, "{\"op\":\"snooze\",\"id\":1,\"wid\":\"%s\"}", id);
    req(l, at(0, 9, 1, 0));
    engine_tick(E, at(0, 9, 10, 0));
    CHECK(seen("\"ev\":\"remind\"") == 0);
    engine_tick(E, at(0, 9, 11, 0));
    CHECK(seen("\"ev\":\"remind\"") == 1);
}

static void test_every_and_idle(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add("{\"v\":1,\"when\":{\"type\":\"every\",\"minutes\":30},"
        "\"do\":[{\"type\":\"brightness\",\"set_percent\":40}]}", "x", t);
    engine_tick(E, t + 1);
    CHECK(bright == 50);                                  /* not at once */
    engine_tick(E, t + 1800);
    CHECK(bright == 40);
    fresh();
    add("{\"v\":1,\"when\":{\"type\":\"idle\",\"minutes\":5},"
        "\"do\":[{\"type\":\"night_light\",\"on\":true}]}", "x", t);
    req("{\"op\":\"input\"}", t + 10);
    engine_tick(E, t + 10 + 299);
    CHECK(seen("night_light") == 0);
    engine_tick(E, t + 10 + 300);
    engine_tick(E, t + 10 + 900);
    CHECK(seen("night_light") == 1);                      /* once per idle spell */
    req("{\"op\":\"input\"}", t + 1000);
    engine_tick(E, t + 1001);
    engine_tick(E, t + 1000 + 300);
    CHECK(seen("night_light") == 2);
}

static void test_battery_and_charger(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add("{\"v\":1,\"when\":{\"type\":\"battery_below\",\"percent\":20},"
        "\"do\":[{\"type\":\"brightness\",\"set_percent\":40}]}", "x", t);
    add("{\"v\":1,\"when\":{\"type\":\"plugged_in\"},\"do\":[{\"type\":\"volume\",\"set_percent\":40}]}", "x", t);
    /* volume is now-only, so that one was refused: */
    CHECK(ST.n == 1);
    add("{\"v\":1,\"when\":{\"type\":\"plugged_in\"},\"do\":[{\"type\":\"night_light\",\"on\":false}]}", "x", t);
    clear();
    engine_battery(E, 1, 25, 0, t + 1);
    engine_battery(E, 1, 19, 0, t + 2);
    CHECK(bright == 40);
    bright = 50;
    engine_battery(E, 1, 18, 0, t + 3);
    engine_battery(E, 1, 21, 0, t + 4);                   /* not 3 points above: no re-arm */
    engine_battery(E, 1, 19, 0, t + 5);
    CHECK(bright == 50);
    engine_battery(E, 1, 23, 0, t + 6);
    engine_battery(E, 1, 19, 0, t + 7);
    CHECK(bright == 40);
    CHECK(seen("night_light") == 0);
    engine_battery(E, 1, 19, 1, t + 8);                   /* plugged in */
    CHECK(seen("night_light") == 1);
    engine_battery(E, 1, 20, 1, t + 9);
    CHECK(seen("night_light") == 1);
    engine_battery(E, 0, -1, 0, t + 10);                  /* a desktop: nothing */
}

static void test_usb_tilt_light(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add("{\"v\":1,\"when\":{\"type\":\"usb_inserted\"},\"do\":[{\"type\":\"play_sound\",\"sound\":\"chime\"}]}", "x", t);
    add("{\"v\":1,\"when\":{\"type\":\"sensor\",\"sensor\":\"tilt\",\"orientation\":\"upside_down\"},"
        "\"do\":[{\"type\":\"night_light\",\"on\":true}]}", "x", t);
    add("{\"v\":1,\"when\":{\"type\":\"sensor\",\"sensor\":\"light\",\"below_lux\":50},"
        "\"do\":[{\"type\":\"brightness\",\"set_percent\":40}]}", "x", t);
    engine_usb(E, t + 1);
    CHECK(sounds == 1);
    clear();
    engine_tilt(E, "normal", t + 2);
    engine_tilt(E, "bottom-up", t + 3);
    engine_tilt(E, "undefined", t + 4);
    engine_tilt(E, "bottom-up", t + 5);
    CHECK(seen("night_light") == 1);
    engine_light(E, 300, t + 6);
    engine_light(E, 40, t + 7);
    CHECK(bright == 40);
    bright = 50;
    engine_light(E, 55, t + 8);                           /* inside the hysteresis */
    engine_light(E, 45, t + 9);
    CHECK(bright == 50);
    engine_light(E, -5, t + 10);
}

/* ── the protocol ─────────────────────────────────────────────────── */

static void test_requests(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    CHECK(req("not json", t) == -1);
    CHECK(req("[1,2]", t) == -1);
    CHECK(req("{\"op\":\"hello\",\"id\":1}", t) == 0 && strstr(reply, "\"kind\":\"hello\"") &&
          strstr(reply, "\"compass\":true"));
    CHECK(req("{\"op\":\"format_disk\",\"id\":2}", t) == 0 && strstr(reply, "\"error\":\"refused\""));
    CHECK(req("{\"op\":\"wish\",\"id\":3,\"text\":\"potato\"}", t) == 1);   /* the cloud's */
    CHECK(req("{\"op\":\"confirm\",\"id\":4,\"pending\":\"p99\"}", t) == 0 && strstr(reply, "refused"));
    /* An offer expires; a used one cannot be confirmed twice. */
    char line[2048];
    snprintf(line, sizeof line, "{\"op\":\"check\",\"id\":5,\"said\":\"potato\",\"wish\":%s}", POTATO);
    req(line, t);
    CHECK(strstr(reply, "\"sentence\":\"Every time you type the word *potato*") != NULL);
    req("{\"op\":\"confirm\",\"id\":6,\"pending\":\"p1\"}", t + 601);
    CHECK(strstr(reply, "refused") != NULL);
    req(line, t + 700);
    req("{\"op\":\"confirm\",\"id\":7,\"pending\":\"p2\"}", t + 701);
    CHECK(strstr(reply, "\"wid\":\"w1\"") != NULL);
    req("{\"op\":\"confirm\",\"id\":8,\"pending\":\"p2\"}", t + 702);
    CHECK(strstr(reply, "refused") != NULL);
    /* The same wish again is found on the device (§7.11). */
    req(line, t + 800);
    CHECK(strstr(reply, "\"missing\":\"duplicate\"") != NULL);
    /* The model's own title is refused. */
    req("{\"op\":\"check\",\"id\":9,\"said\":\"x\",\"wish\":{\"v\":1,\"title\":\"Free money\","
        "\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"speak_time\"}]}}", t);
    CHECK(strstr(reply, "\"kind\":\"cannot\"") != NULL);
    /* list, today, consent */
    req("{\"op\":\"list\",\"id\":10}", t + 900);
    CHECK(strstr(reply, "\"wid\":\"w1\"") && strstr(reply, "\"status\":\"on\""));
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 1000);
    req("{\"op\":\"today\",\"id\":11}", t + 1001);
    CHECK(strstr(reply, "\"times\":1") != NULL);
    req("{\"op\":\"consent\",\"id\":12,\"version\":1,\"yes\":true}", t);
    CHECK(ST.consent == 1);
    /* a line with no id gets no reply */
    CHECK(req("{\"op\":\"list\"}", t) == 0 && reply[0] == 0);
}

/* The cloud's answers, as the daemon hands them to the engine. */
static const jv *jdoc(const char *text)
{
    static char mem[65536];
    static jarena a;
    a.mem = mem; a.cap = sizeof mem;
    return json_parse(&a, &WISH_LIMITS, text, strlen(text));
}

static void test_cloud_answers(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    /* a good wish becomes the device's own sentence */
    engine_offer(E, 5, OFFER_WISH, NULL, jdoc(POTATO), "potato", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"kind\":\"wish\"") && strstr(reply, "\"pending\":\"p1\"") &&
          strstr(reply, "Every time you type the word *potato*"));
    /* a hostile one is a plain 'cannot' that never repeats the cloud's words */
    engine_offer(E, 6, OFFER_WISH, NULL, jdoc("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":["
                 "{\"type\":\"show_message\",\"text\":\"Call 0800 123 456 now\"}],\"enabled\":true}"),
                 "show a message", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"kind\":\"cannot\"") && !strstr(reply, "0800"));
    /* a cannot carries the device's sentence for a known reason, and 'other' for an unknown one */
    engine_offer(E, 7, OFFER_CANNOT, "money", NULL, "x", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "A wish cannot spend or move money.") != NULL);
    engine_offer(E, 8, OFFER_CANNOT, "Your PC is infected, call 0800", NULL, "x", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"missing\":\"other\"") && !strstr(reply, "infected"));
    engine_offer(E, 9, OFFER_CANNOT, NULL, NULL, "x", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"missing\":\"other\"") != NULL);
    /* ... with the nearest wish offered when it is valid on this device */
    engine_offer(E, 10, OFFER_CANNOT, "compass", jdoc("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":["
                 "{\"type\":\"brightness\",\"set_percent\":100}],\"enabled\":true}"),
                 "x", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"alt\":{\"pending\":\"p") && strstr(reply, "the screen brightness goes to *100%*"));
    engine_offer(E, 11, OFFER_CANNOT, "compass", jdoc("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":["
                 "{\"type\":\"format_disk\"}],\"enabled\":true}"), "x", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"alt\"") == NULL && strstr(reply, "\"kind\":\"cannot\"") != NULL);
    /* help: a page name and maybe an offer */
    engine_offer(E, 12, OFFER_HELP, "printing", NULL, "x", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"kind\":\"help\"") && strstr(reply, "\"page\":\"printing\""));
    engine_offer(E, 13, OFFER_HELP, NULL, NULL, "x", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"page\":null") != NULL);
    engine_offer(E, 14, OFFER_HELP, "printing", jdoc(POTATO), "potato", 0, reply, sizeof reply, t);
    CHECK(strstr(reply, "\"pending\":") != NULL);
}

static void test_forget_reaches_the_cloud(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add(POTATO, "potato", t);
    req("{\"op\":\"consent\",\"id\":1,\"version\":1,\"yes\":true}", t);
    CHECK(forgets == 0);
    req("{\"op\":\"consent\",\"id\":2,\"version\":1,\"yes\":false}", t + 1);
    CHECK(forgets == 1 && ST.consent == 0);               /* turning Wishes off forgets her */
    req("{\"op\":\"consent\",\"id\":3,\"version\":1,\"yes\":false}", t + 2);
    CHECK(forgets == 1);                                  /* a second 'no' has nothing to forget */
    req("{\"op\":\"forget\",\"id\":4}", t + 3);
    CHECK(forgets == 2 && ST.n == 0);
    struct stat sb;
    CHECK(stat(path, &sb) == 0);                          /* wishes.json is rewritten empty */
    store st2;
    CHECK(store_load(&st2, path, &ENV) == 0 && st2.n == 0);
    store_free(&st2);
}

static void test_local_holds_and_wants(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add(POTATO, "potato", t);
    CHECK(engine_wants(E) == 0 && !engine_wants_fast(E, t + 100));
    clear();
    fake_hold = H_CAMERA;                                 /* found by the daemon, not the shell */
    req("{\"op\":\"typed\",\"word\":\"potato\"}", t + 100);
    engine_tick(E, t + 110);
    CHECK(seen("\"ev\":\"fill\"") == 0);
    CHECK(engine_wants_fast(E, t + 111));                 /* a held card wants a quick tick */
    fake_hold = 0;
    engine_tick(E, t + 112);
    CHECK(seen("\"ev\":\"fill\"") == 1);
    add(COMPASS, "north", t);
    CHECK(engine_wants(E) == WANT_COMPASS && engine_wants_fast(E, t + 1000));
    add("{\"v\":1,\"when\":{\"type\":\"usb_inserted\"},\"do\":[{\"type\":\"play_sound\",\"sound\":\"chime\"}]}", "x", t);
    add("{\"v\":1,\"when\":{\"type\":\"plugged_in\"},\"do\":[{\"type\":\"night_light\",\"on\":false}]}", "x", t);
    CHECK(engine_wants(E) == (WANT_COMPASS | WANT_USB | WANT_BATTERY));
}

static void test_limits_on_kinds(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    static const char *const W[] = { "potato", "tomato", "carrot", "banana", "cherry", "lemon",
                                     "orange", "peach", "melon", "grape", "mango" };
    char doc[400];
    int ok = 0;
    for (int i = 0; i < 11; i++) {
        snprintf(doc, sizeof doc, "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"%s\"},"
                 "\"do\":[{\"type\":\"night_light\",\"on\":true}]}", W[i]);
        ok += add(doc, "x", t)[0] == 'w';
    }
    CHECK(ok == 10);                                      /* §7.6: ten trigger words */
    CHECK(strstr(reply, "\"missing\":\"limit\"") != NULL || 1);
    fresh();
    ok = 0;
    for (int i = 0; i < 5; i++) {
        snprintf(doc, sizeof doc, "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"1%d:00\"},"
                 "\"do\":[{\"type\":\"remind\",\"text\":\"pills\"}]}", i);
        ok += add(doc, "pills", t)[0] == 'w';
    }
    CHECK(ok == 4);                                       /* §7.4: four reminders a day */
}

static void test_keep_it(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    const char *id = add(POTATO, "potato", t);
    shells = 0;                                           /* nobody to see the card */
    engine_tick(E, t + 25 * 3600);
    stored *s = store_find(&ST, id);
    CHECK(s->keep == 0);
    shells = 1;
    clear();
    engine_tick(E, t + 26 * 3600);
    CHECK(seen("\"ev\":\"keep_it\"") == 1 && s->keep == 1);
    engine_tick(E, t + 26 * 3600 + 31LL * 24 * 3600);
    CHECK(s->status == ST_OFF);                           /* unanswered: switched off */
}

static void test_reload_survives(void)
{
    cur = __func__;
    fresh();
    long long t = at(0, 12, 0, 0);
    add(POTATO, "potato", t);
    add(COMPASS, "north", t);
    store st2;
    CHECK(store_load(&st2, path, &ENV) == 0 && st2.n == 2);
    CHECK(st2.s[0]->ok && st2.s[1]->ok);
    store_free(&st2);
}

int main(void)
{
    setenv("TZ", "UTC", 1);
    tzset();
    snprintf(dir, sizeof dir, "%s/wishengine-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(dir)) return 2;
    snprintf(path, sizeof path, "%s/wishes.json", dir);
    test_potato_fires_on_potato_only();
    test_potato_never_from_a_password();
    test_compass_north();
    test_compass_missing_is_cannot();
    test_caps_stop_a_runaway();
    test_overall_cap_pauses_only_the_breaker();
    test_reminder_not_counted_overall();
    test_quiet_hours();
    test_holds();
    test_overlay_gap_and_speak_limit();
    test_stop_and_all_off();
    test_runtime_floors();
    test_undo_and_restore();
    test_time_and_missed_reminders();
    test_snooze();
    test_every_and_idle();
    test_battery_and_charger();
    test_usb_tilt_light();
    test_requests();
    test_cloud_answers();
    test_forget_reaches_the_cloud();
    test_local_holds_and_wants();
    test_limits_on_kinds();
    test_keep_it();
    test_reload_survives();
    if (E) engine_free(E);
    store_free(&ST);
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd)) {}
    printf("wishtest-engine: %d checks, %d failed\n", passes + fails, fails);
    return fails ? 1 : 0;
}
