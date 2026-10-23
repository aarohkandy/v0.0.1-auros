/* wishuitest.c — Wishes, the part she sees (docs/AI.md §4, §7.6, §7.7, §7.9).
 *
 * The engine is played by the other end of a socketpair, so every line
 * the panel writes is read back word for word and every answer is one
 * this test chose. The clock is the test's, so "20 seconds" and "15
 * seconds" take no time. It includes the two files rather than adding a
 * way in for itself.
 *
 *   cc -O2 -std=gnu11 -o /tmp/wishuitest tools/wishuitest.c \
 *      src/aurshell/foot.c src/aurshell/run.c src/aurshell/draw.c \
 *      src/aurshell/shellcommon.c src/aurshell/anim.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      src/common/png.c -Isrc/aurshell -lm && /tmp/wishuitest
 *
 * Every check here was shown to fail with the thing it checks broken:
 * docs/handoff/streams/W2.md lists the mutations.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../src/aurshell/wish.c"
#include "../src/aurshell/wishover.c"
#include "../src/aurshell/foot.h"
#include "../src/common/png.h"

static int checked, failed;
static void ok(const char *what, int good)
{
    checked++;
    if (!good) failed++;
    printf("  %-66s %s\n", what, good ? "ok" : "FAIL");
}

static uint64_t T = 5000000;
static uint64_t test_now(void) { return T; }

/* The compositor's half, as the shell sees it: what it was handed. */
static char got_words[10][33];
static int  got_nw = -1;
int wishhook_set_triggers(const char *const *words, int n_words,
                          const char *const *apps, int n_apps)
{
    (void)apps; (void)n_apps;
    got_nw = n_words;
    for (int i = 0; i < n_words && i < 10; i++)
        snprintf(got_words[i], sizeof got_words[i], "%s", words[i]);
    return 0;
}

static int eng = -1;                  /* the engine's end */

static int spawned;
static char spawned_url[300];
static int stub_spawn(struct shell_ctx_s *c, const char *blob, int n_args)
{
    (void)c;
    spawned++;
    /* the tokens: sh -c <script> <url> */
    const char *p = blob;
    for (int i = 0; i < n_args - 1 && *p; i++) p += strlen(p) + 1;
    snprintf(spawned_url, sizeof spawned_url, "%s", p);
    return 0;
}

static void plug(void)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv) < 0) { perror("socketpair"); exit(2); }
    if (K.fd >= 0) close(K.fd);
    K.fd = sv[0];
    K.in_n = K.out_n = 0;
    memset(K.pend, 0, sizeof K.pend);
    if (eng >= 0) close(eng);
    eng = sv[1];
}

/* Everything the panel wrote since the last call. */
static char wrote[16384];
static const char *drain(void)
{
    size_t n = 0;
    for (;;) {
        ssize_t k = recv(eng, wrote + n, sizeof wrote - 1 - n, MSG_DONTWAIT);
        if (k <= 0) break;
        n += (size_t)k;
    }
    wrote[n] = 0;
    return wrote;
}

static void say(shell_ctx *c, const char *line)
{
    char b[8192];
    snprintf(b, sizeof b, "%s\n", line);
    if (send(eng, b, strlen(b), MSG_NOSIGNAL) < 0) perror("send");
    wish_step(c);
}

/* The id of the last request with this op, from what was written. */
static int last_id(const char *op)
{
    char want[48];
    snprintf(want, sizeof want, "{\"op\":\"%s\",\"id\":", op);
    const char *p = NULL, *q = wrote;
    while ((q = strstr(q, want)) != NULL) { p = q; q++; }
    return p ? atoi(p + strlen(want)) : -1;
}

static void fresh(shell_ctx *c)
{
    memset(c, 0, sizeof *c);
    theme_t t = {0};
    shell_theme_load(c, &t);
    c->text_scale = 1.f;
    c->foot_hover = -1;
    c->screen_w = 1366;
    c->screen_h = 768 - foot_height(c);
    c->allow_settings = 1;
    W.consent = 1;
    W.req_id = 0;
    W.last_ms = 0;
    W.last_text[0] = 0;
    W.undo_until = 0;
    O.fill = 0; O.n = 0;
    plug();
}

static void type(shell_ctx *c, const char *s)
{
    for (; *s; s++) {
        c->key_text[0] = *s; c->key_text[1] = 0;
        wish_key(c, 30);
    }
    c->key_text[0] = 0;
}

/* ── 1. the JSON the socket carries ──────────────────────────────── */

static void json(void)
{
    printf("\nthe socket's JSON\n");
    char out[256];
    const char *L = "{\"a\":\"x\\\"y\\u00e9\\ud83e\\udd54\",\"n\":42,\"b\":true,"
                    "\"o\":{\"name\":\"BBC\"},\"arr\":[\"p\",\"q\"]}";
    const char *E = L + strlen(L);
    js_getstr(L, E, "a", out, sizeof out);
    ok("a string with an escaped quote, e-acute and a surrogate pair decodes",
       !strcmp(out, "x\"y\xC3\xA9\xF0\x9F\xA5\x94"));
    ok("numbers and booleans read", js_int(L, E, "n", 0) == 42 && js_bool(L, E, "b", 0) == 1);
    const char *ve, *o = js_get(L, E, "o", &ve);
    ok("a nested object is reached", o && js_getstr(o, ve, "name", out, sizeof out) == 0 &&
                                     !strcmp(out, "BBC"));
    /* A key that only appears INSIDE a string is not a key. */
    const char *I = "{\"text\":\"\\\"kind\\\":\\\"wish\\\"\",\"kind\":\"cannot\"}";
    js_getstr(I, I + strlen(I), "kind", out, sizeof out);
    ok("a key written inside a string value is not matched", !strcmp(out, "cannot"));
    const char *C = "{\"t\":\"a\\u0007b\\nc\"}";
    js_getstr(C, C + strlen(C), "t", out, sizeof out);
    ok("control characters arrive as spaces", !strcmp(out, "a b c"));
    const char *R = "{\"t\":\"a\x07" "b\x1b[2Jc\"}";     /* raw bell and escape, not escaped */
    js_getstr(R, R + strlen(R), "t", out, sizeof out);
    ok("a raw bell or escape in a string arrives as a space", !strcmp(out, "a b [2Jc"));
    const char *B = "{\"t\":\"unterminated}";
    ok("a malformed line yields nothing", js_get(B, B + strlen(B), "t", NULL) == NULL);
    char q[64];
    js_quote(q, sizeof q, "say \"hi\"\\\n");
    ok("her words are quoted safely for the engine", !strcmp(q, "\"say \\\"hi\\\"\\\\\\u000a\""));
}

/* ── 2. asking ───────────────────────────────────────────────────── */

static void asking(void)
{
    printf("\nasking, and the one sentence she answers\n");
    shell_ctx c;
    fresh(&c);
    c.wish_open = WISH_OPEN_ASK;
    wish_opened(&c);
    ok("the panel opens on the question", W.page == WP_ASK);

    type(&c, "Fill my screen with \"potatoes\" when I type potato");
    wish_key(&c, 28);
    drain();
    ok("Enter sends one wish line with her words quoted",
       strstr(wrote, "{\"op\":\"wish\",") &&
       strstr(wrote, "\"text\":\"Fill my screen with \\\"potatoes\\\" when I type potato\"") != NULL);
    ok("and the page says it is thinking", W.page == WP_WAIT);
    int id = last_id("wish");

    W.page = WP_ASK; W.box_focus = 1;
    wish_key(&c, 28);
    drain();
    ok("one request at a time: pressing again sends nothing", !strstr(wrote, "\"op\":\"wish\""));
    W.page = WP_WAIT;

    char line[512];
    snprintf(line, sizeof line,
             "{\"id\":%d,\"kind\":\"wish\",\"pending\":\"p7\",\"title\":\"Potatoes\","
             "\"sentence\":\"Every time you type *potato*, your screen fills with potatoes for *3 seconds*.\"}", id);
    say(&c, line);
    ok("the engine's own sentence brings the confirmation", W.page == WP_CONFIRM &&
       strstr(W.sentence, "*potato*") && !strcmp(W.pending, "p7"));

    /* No default: Enter on the confirmation presses nothing. */
    drain();
    wish_key(&c, 28);
    drain();
    ok("Enter with nothing chosen does not say yes", !strstr(wrote, "confirm") && W.page == WP_CONFIRM);

    /* Escape is No, never Yes. */
    wish_key(&c, 1);
    drain();
    ok("Escape on the confirmation is No, thanks (discard)",
       strstr(wrote, "{\"op\":\"discard\"") && strstr(wrote, "\"pending\":\"p7\"") && W.page == WP_ASK);

    /* Again, and this time Yes. */
    W.page = WP_CONFIRM;
    snprintf(W.pending, sizeof W.pending, "p8");
    wish_view v = view_now(&c);
    wish_target t[WISH_MAX_TARGETS];
    int n = wish_targets(&c, c.screen_w, c.screen_h, &v, t, WISH_MAX_TARGETS);
    int yes = -1, no = -1;
    for (int i = 0; i < n; i++) { if (t[i].kind == WT_YES) yes = i; if (t[i].kind == WT_NO) no = i; }
    ok("Yes and No are the same size", yes >= 0 && no >= 0 &&
       t[yes].r.w == t[no].r.w && t[yes].r.h == t[no].r.h);
    wish_click(&c, t[yes].r.x + 5, t[yes].r.y + 5);
    drain();
    ok("pressing Yes confirms that pending wish", strstr(wrote, "{\"op\":\"confirm\"") &&
       strstr(wrote, "\"pending\":\"p8\""));
    snprintf(line, sizeof line, "{\"id\":%d,\"kind\":\"done\"}", last_id("confirm"));
    say(&c, line);
    ok("a wish that ran now is Done, with Undo for ten minutes",
       W.page == WP_DONE && wish_undo_offered());
    T += 10 * 60 * 1000 + 1;
    wish_step(&c);
    ok("and Undo is gone after ten minutes", !wish_undo_offered());

    /* §7.9: the same words within 30 s are not sent again. */
    fresh(&c);
    c.wish_open = WISH_OPEN_ASK;
    wish_opened(&c);
    type(&c, "Make the words bigger");
    wish_key(&c, 28);
    drain();
    snprintf(line, sizeof line, "{\"id\":%d,\"kind\":\"cannot\",\"missing\":\"other\",\"text\":\"No.\"}", last_id("wish"));
    say(&c, line);
    go(WP_ASK);
    snprintf(W.text, sizeof W.text, "Make the words bigger");
    T += 5000;
    submit(&c);
    drain();
    ok("the same words again within 30 seconds are not sent", !strstr(wrote, "\"op\":\"wish\""));
    T += 30000;
    go(WP_ASK);
    snprintf(W.text, sizeof W.text, "Make the words bigger");
    submit(&c);
    drain();
    ok("after 30 seconds they are", strstr(wrote, "\"op\":\"wish\"") != NULL);
    cancel_request();

    /* No consent: her words are held, and nothing is sent. */
    W.consent = 0;
    go(WP_ASK);
    W.last_ms = 0;
    snprintf(W.text, sizeof W.text, "Make the screen darker");
    submit(&c);
    drain();
    ok("with Wishes not agreed to, nothing is sent: the consent page comes first",
       !strstr(wrote, "\"op\":\"wish\"") && W.page == WP_CONSENT);
    W.consent = 1;

    /* Under three characters is not sent. */
    go(WP_ASK);
    snprintf(W.text, sizeof W.text, "hm");
    W.last_ms = 0;
    submit(&c);
    drain();
    ok("two letters are not sent, and she is told why", !strstr(wrote, "\"op\":\"wish\"") && W.hint[0]);

    /* The device answers these itself (§7.9). */
    go(WP_ASK);
    snprintf(W.text, sizeof W.text, "Turn off wishes!");
    submit(&c);
    drain();
    ok("\"turn off wishes\" is done on the device", strstr(wrote, "\"op\":\"all_off\"") &&
       !strstr(wrote, "\"op\":\"wish\"") && W.page == WP_DONE);
    go(WP_ASK);
    snprintf(W.text, sizeof W.text, "Stop");
    wishover_fill(&c, "\xF0\x9F\xA5\x94", NULL, 5, "Potatoes", "w1");
    submit(&c);
    drain();
    ok("\"stop\" clears the screen and sends nothing", !wishover_fill_on() &&
       !c.wishfill_open && !strstr(wrote, "\"op\":\"wish\""));
}

/* ── 3. waiting, and what goes wrong ─────────────────────────────── */

static void trouble(void)
{
    printf("\nwaiting, and what goes wrong\n");
    shell_ctx c;
    fresh(&c);
    c.wish_open = WISH_OPEN_ASK;
    wish_opened(&c);
    type(&c, "Remind me to take my pills at 9");
    wish_key(&c, 28);
    drain();
    int id = last_id("wish");
    T += 19000;
    wish_step(&c);
    ok("at 19 seconds it is still waiting", W.page == WP_WAIT);
    T += 1500;
    wish_step(&c);
    drain();
    char want[64];
    snprintf(want, sizeof want, "{\"op\":\"cancel\",\"id\":%d}", id);
    ok("at 20 seconds it gives up, and says cancel for that request",
       W.page == WP_ERROR && !strcmp(ERRS[W.err].code, "slow") && strstr(wrote, want));
    char line[256];
    snprintf(line, sizeof line, "{\"id\":%d,\"kind\":\"wish\",\"pending\":\"late\",\"sentence\":\"x\"}", id);
    say(&c, line);
    ok("a late answer to a cancelled request changes nothing", W.page == WP_ERROR);

    static const char *const CODES[] = { "offline", "busy", "resting", "limit", "refused", "broken" };
    for (int i = 0; i < 6; i++) {
        go(WP_ASK);
        W.last_ms = 0;
        snprintf(W.text, sizeof W.text, "Make the screen brighter %d", i);
        submit(&c);
        drain();
        snprintf(line, sizeof line, "{\"id\":%d,\"kind\":\"error\",\"error\":\"%s\"}", last_id("wish"), CODES[i]);
        say(&c, line);
        char w[80];
        snprintf(w, sizeof w, "error %s is one plain line of its own", CODES[i]);
        ok(w, W.page == WP_ERROR && !strcmp(ERRS[W.err].code, CODES[i]) && ERRS[W.err].line[0]);
    }
    go(WP_ASK);
    W.last_ms = 0;
    snprintf(W.text, sizeof W.text, "Make the screen darker");
    submit(&c);
    drain();
    snprintf(line, sizeof line, "{\"id\":%d,\"kind\":\"error\",\"error\":\"consent\"}", last_id("wish"));
    say(&c, line);
    ok("no consent yet: the consent page, not an error", W.page == WP_CONSENT);
    wish_target t = { .kind = WT_CONSENT_YES };
    act(&c, &t);
    drain();
    ok("Yes sends consent", strstr(wrote, "{\"op\":\"consent\"") && strstr(wrote, "\"yes\":true"));
    snprintf(line, sizeof line, "{\"id\":%d,\"kind\":\"done\"}", last_id("consent"));
    say(&c, line);
    drain();
    ok("and then her wish goes, without her typing it again",
       strstr(wrote, "\"text\":\"Make the screen darker\"") && W.page == WP_WAIT);

    /* The engine goes away mid-request. */
    close(eng); eng = -1;
    wish_step(&c);
    ok("the engine stopping says so at once, not after 20 seconds",
       W.page == WP_ERROR && !strcmp(ERRS[W.err].code, "broken") && K.fd < 0);

    /* A line over 4096 bytes closes the connection (§3.7). */
    plug();
    char *big = malloc(6000);
    memset(big, 'a', 5998); big[5998] = '\n'; big[5999] = 0;
    if (send(eng, big, 5999, MSG_NOSIGNAL) < 0) perror("send");
    free(big);
    wish_step(&c);
    ok("a line longer than 4096 bytes closes the connection", K.fd < 0);
}

/* ── 4. what a wish puts on the screen ──────────────────────────── */

static void overlays(void)
{
    printf("\nwhat a wish puts on the screen\n");
    shell_ctx c;
    fresh(&c);
    say(&c, "{\"ev\":\"fill\",\"n\":1,\"wid\":\"w1\",\"title\":\"Potatoes\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"seconds\":60}");
    ok("a fill comes up and takes the keyboard like a panel", wishover_fill_on() && SHELL_PANEL_OPEN(&c));
    ok("asked for 60 seconds, it is held to 10", O.until - O.t0 == 10000);
    ok("the first key ends it and is used up", wishover_key(&c, 30) == 1 && !wishover_fill_on() &&
       !c.wishfill_open);
    ok("the next key is not taken", wishover_key(&c, 30) == 0);

    say(&c, "{\"ev\":\"fill\",\"n\":2,\"wid\":\"w1\",\"title\":\"Potatoes\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"seconds\":3}");
    ok("a press anywhere ends it and is used up", wishover_click(&c, 700, 400) == 1 && !wishover_fill_on());
    ok("and its release is used up too", wishover_take_release() == 1 && wishover_take_release() == 0);

    say(&c, "{\"ev\":\"fill\",\"n\":3,\"wid\":\"w1\",\"title\":\"Potatoes\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"seconds\":3}");
    T += 3001;
    wish_step(&c);
    ok("left alone, it goes by itself after its seconds", !wishover_fill_on() && !c.wishfill_open);

    say(&c, "{\"ev\":\"fill\",\"n\":4,\"wid\":\"w1\",\"title\":\"Potatoes\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"seconds\":3}");
    wish_target ot[24];
    int on = wishover_targets(&c, c.screen_w, c.screen_h, ot, 24);
    int stop = -1;
    for (int i = 0; i < on; i++) if (ot[i].kind == WOT_STOP) stop = i;
    ok("the fill has a Stop this wish button at least 48 px", stop >= 0 &&
       ot[stop].r.h >= 48 && ot[stop].r.w >= 48);
    drain();
    wishover_click(&c, ot[stop].r.x + 4, ot[stop].r.y + 4);
    drain();
    ok("one press on it turns that wish off", strstr(wrote, "{\"op\":\"enable\"") &&
       strstr(wrote, "\"wid\":\"w1\"") && strstr(wrote, "\"on\":false"));
    wishover_take_release();

    say(&c, "{\"ev\":\"message\",\"n\":5,\"wid\":\"w4\",\"title\":\"Good morning\",\"text\":\"Have a lovely day\"}");
    ok("a card does not take the keyboard", wishover_cards() == 1 && !SHELL_PANEL_OPEN(&c) &&
       wishover_key(&c, 30) == 0);
    say(&c, "{\"ev\":\"message\",\"n\":6,\"wid\":\"w4\",\"title\":\"Good morning\",\"text\":\"Again\"}");
    ok("one card per wish: a new firing replaces it", wishover_cards() == 1 &&
       !strcmp(O.cards[0].text, "Again"));
    on = wishover_targets(&c, c.screen_w, c.screen_h, ot, 24);
    ogeom g;
    olayout(&c, c.screen_w, c.screen_h, &g);
    int full = c.screen_h + foot_height(&c);
    ok("a card is at most half the screen each way", g.card[0].w <= c.screen_w / 2 &&
       g.card[0].h <= full / 2);
    {
        /* The worst case: her biggest text on the smallest screen, with a
         * reminder that has three buttons. */
        shell_ctx s2 = c;
        s2.screen_w = 1024; s2.text_scale = 2.f; s2.screen_h = 600 - foot_height(&s2);
        wishover_card(&s2, WO_REMIND, "Take my pills with a glass of water and sit down", "Pills", "w9", NULL, 0);
        ogeom g2;
        olayout(&s2, s2.screen_w, s2.screen_h, &g2);
        int fh = s2.screen_h + foot_height(&s2), bad = 0;
        for (int i = 0; i < O.n; i++)
            if (g2.card[i].h > fh / 2 || g2.card[i].w > s2.screen_w / 2) bad++;
        if (bad)
            for (int i = 0; i < O.n; i++)
                printf("    card %d is %dx%d on a %dx%d screen\n", i, g2.card[i].w, g2.card[i].h,
                       s2.screen_w, fh);
        ok("at 1024x600 and 200% text no card is over half the screen", !bad);
        wish_target t2[24];
        int n2 = wishover_targets(&s2, s2.screen_w, s2.screen_h, t2, 24), small = 0;
        for (int i = 0; i < n2; i++) if (t2[i].r.h < 48 || t2[i].r.w < 48) small++;
        ok("and every button on it is still at least 48 px", n2 > 0 && !small);
        drop_card(O.n - 1);
    }
    ok("a press outside the card goes to what is underneath", wishover_click(&c, 5, 5) == 0);
    int close_i = -1;
    for (int i = 0; i < on; i++) if (ot[i].kind == WOT_CLOSE) close_i = i;
    ok("Close this note closes it", close_i >= 0 &&
       wishover_click(&c, ot[close_i].r.x + 3, ot[close_i].r.y + 3) == 1 && wishover_cards() == 0);
    wishover_take_release();

    say(&c, "{\"ev\":\"remind\",\"n\":7,\"wid\":\"w2\",\"title\":\"Pills\",\"text\":\"Take my pills\",\"for\":\"9:00 am\"}");
    on = wishover_targets(&c, c.screen_w, c.screen_h, ot, 24);
    int snooze = -1;
    for (int i = 0; i < on; i++) if (ot[i].kind == WOT_SNOOZE) snooze = i;
    drain();
    ok("a reminder offers Remind me in 10 minutes", snooze >= 0 &&
       wishover_click(&c, ot[snooze].r.x + 3, ot[snooze].r.y + 3) == 1);
    drain();
    ok("which asks the engine to snooze that wish", strstr(wrote, "{\"op\":\"snooze\"") &&
       strstr(wrote, "\"wid\":\"w2\""));
    wishover_take_release();

    /* Every card a wish made can stop it. */
    say(&c, "{\"ev\":\"remind\",\"n\":8,\"wid\":\"w2\",\"title\":\"Pills\",\"text\":\"Take my pills\"}");
    on = wishover_targets(&c, c.screen_w, c.screen_h, ot, 24);
    stop = -1;
    for (int i = 0; i < on; i++) if (ot[i].kind == WOT_STOP) stop = i;
    ok("a reminder card has Stop this wish too", stop >= 0);
    wishover_clear(&c);
}

/* ── 5. what only the shell does ────────────────────────────────── */

static void shell_actions(void)
{
    printf("\nwhat only the shell does\n");
    shell_ctx c;
    fresh(&c);
    char home[] = "/tmp/wishuitest.home";
    setenv("HOME", home, 1);
    mkdir(home, 0700);
    drain();
    say(&c, "{\"ev\":\"shell\",\"n\":42,\"wid\":\"\",\"title\":\"Bigger words\","
            "\"do\":{\"type\":\"set_text_size\",\"size\":\"bigger\"}}");
    drain();
    ok("the words get bigger", c.text_scale > 1.29f && c.text_scale < 1.31f && c.text_changed);
    ok("and the engine is told, with what would put it back",
       strstr(wrote, "{\"op\":\"did\",\"n\":42,\"ok\":true,\"prev\":{\"type\":\"set_text_size\",\"size\":\"normal\"}}") != NULL);
    ok("Keep this? is asked", wishover_cards() == 1 && O.cards[0].keep_what == WO_KEEP_TEXT);
    T += 14000;
    wish_step(&c);
    ok("at 14 seconds nothing has changed back", c.text_scale > 1.29f);
    T += 1500;
    wish_step(&c);
    ok("with no answer after 15 seconds it goes back", c.text_scale > 0.99f && c.text_scale < 1.01f &&
       wishover_cards() == 0);

    drain();
    c.spawn = stub_spawn;
    say(&c, "{\"ev\":\"shell\",\"n\":43,\"do\":{\"type\":\"open_website\",\"url\":\"http://example.com\"}}");
    drain();
    ok("an address that is not https is refused and nothing starts",
       strstr(wrote, "\"n\":43,\"ok\":false") != NULL && spawned == 0);
    say(&c, "{\"ev\":\"shell\",\"n\":44,\"do\":{\"type\":\"open_website\",\"url\":\"https://a.example/\\\"x\"}}");
    drain();
    ok("an address with a quote in it is refused and nothing starts",
       strstr(wrote, "\"n\":44,\"ok\":false") != NULL && spawned == 0);
    say(&c, "{\"ev\":\"shell\",\"n\":47,\"do\":{\"type\":\"open_website\",\"url\":\"https://www.bbc.co.uk/news\"}}");
    drain();
    ok("a plain https address opens, as the last word of the command",
       strstr(wrote, "\"n\":47,\"ok\":true") != NULL && spawned == 1 &&
       !strcmp(spawned_url, "https://www.bbc.co.uk/news"));
    say(&c, "{\"ev\":\"shell\",\"n\":45,\"do\":{\"type\":\"install_program\",\"flathub_id\":\"x\"}}");
    drain();
    ok("an action the shell does not do is refused, not faked", strstr(wrote, "\"n\":45,\"ok\":false") != NULL);
    /* A shells directory with a file that is NOT one of the four looks:
     * only the name check stands between it and being applied. */
    char sdir[] = "/tmp/wishtest.shells";
    mkdir(sdir, 0700);
    FILE *sf = fopen("/tmp/wishtest.shells/evil.shell", "w");
    if (sf) { fputs("shell_name=\"evil\"\n", sf); fclose(sf); }
    sf = fopen("/tmp/wishtest.shells/rail.shell", "w");
    if (sf) { fputs("shell_name=\"rail\"\n", sf); fclose(sf); }
    setenv("AUROS_SHELLS", sdir, 1);
    snprintf(c.layout_id, sizeof c.layout_id, "%s", "taskbar");
    say(&c, "{\"ev\":\"shell\",\"n\":46,\"do\":{\"type\":\"set_look\",\"look\":\"evil\"}}");
    drain();
    ok("a look that is not one of the four is refused, even if its file exists",
       strstr(wrote, "\"n\":46,\"ok\":false") != NULL && !c.want_reload);
    say(&c, "{\"ev\":\"shell\",\"n\":48,\"do\":{\"type\":\"set_look\",\"look\":\"rail\"}}");
    drain();
    ok("one of the four is applied, and Keep this? is asked", strstr(wrote, "\"n\":48,\"ok\":true") != NULL &&
       c.want_reload && wishover_cards() == 1 && O.cards[0].keep_what == WO_KEEP_LOOK);

    say(&c, "{\"ev\":\"triggers\",\"words\":[\"potato\",\"pills\"],\"apps\":[]}");
    ok("the trigger words are handed to the compositor", got_nw == 2 &&
       !strcmp(got_words[0], "potato") && !strcmp(got_words[1], "pills"));
}

/* ── 6. every target, every page, every size ─────────────────────── */

static void targets(void)
{
    printf("\nevery button is big enough, on the screen, and alone\n");
    static const struct { int w, h; } RES[] = { { 1024, 600 }, { 1366, 768 }, { 1920, 1080 } };
    static const float SCALES[] = { 0.8f, 1.0f, 1.5f, 2.0f };
    int bad = 0, pages = 0;
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 4; k++)
            for (int p = 0; p < WP_N; p++)
                for (int var = 0; var < 2; var++) {
                    shell_ctx c;
                    memset(&c, 0, sizeof c);
                    c.text_scale = SCALES[k];
                    c.screen_w = RES[r].w;
                    c.screen_h = RES[r].h - foot_height(&c);
                    wish_view v;
                    memset(&v, 0, sizeof v);
                    v.page = p;
                    v.n_examples = var ? 6 : 3;
                    v.voice = 1;
                    v.has_dest = var; v.has_help = 1; v.has_offer = var;
                    v.can_retry = 1; v.now_wish = var;
                    v.n_rows = var ? 9 : 2; v.more = var; v.removing = var ? 0 : -1;
                    v.said_len = var ? 290 : 40; v.sent_len = var ? 200 : 90;
                    wish_target t[WISH_MAX_TARGETS];
                    int n = wish_targets(&c, c.screen_w, c.screen_h, &v, t, WISH_MAX_TARGETS);
                    pages++;
                    for (int i = 0; i < n; i++) {
                        rect a = t[i].r;
                        int small = a.w < 48 || a.h < 48;
                        int off = a.x < 0 || a.y < 0 || a.x + a.w > c.screen_w ||
                                  a.y + a.h > c.screen_h;
                        int over = 0;
                        for (int j = 0; j < n; j++) {
                            rect b = t[j].r;
                            if (j == i) continue;
                            if (a.x < b.x + b.w && b.x < a.x + a.w &&
                                a.y < b.y + b.h && b.y < a.y + a.h) over = 1;
                        }
                        if ((small || off || over) && bad < 8)
                            printf("    page %d at %dx%d text %.0f%%: target %d (%d) %dx%d at %d,%d%s%s%s\n",
                                   p, RES[r].w, RES[r].h, (double)(SCALES[k] * 100.f), i, t[i].kind,
                                   a.w, a.h, a.x, a.y, small ? " SMALL" : "", off ? " OFF" : "",
                                   over ? " OVERLAP" : "");
                        if (small || off || over) bad++;
                    }
                    /* The answers are always there. */
                    if (p == WP_CONFIRM) {
                        int y = 0, nn = 0;
                        for (int i = 0; i < n; i++) { if (t[i].kind == WT_YES) y++; if (t[i].kind == WT_NO) nn++; }
                        if (y != 1 || nn != 1) { bad++; printf("    confirm without Yes and No\n"); }
                    }
                }
    char w[96];
    snprintf(w, sizeof w, "%d pages: every target >= 48 px, on screen, not overlapping", pages);
    ok(w, bad == 0);
}

/* ── 7. the emoji ───────────────────────────────────────────────── */

static void emoji(void)
{
    printf("\nthe emoji, from the font already on the computer\n");
    /* The decoder against the shell's own PNG writer: a picture in, the
     * same picture out. */
    uint32_t px[37 * 23];
    for (int i = 0; i < 37 * 23; i++) px[i] = (uint32_t)(i * 2654435761u) & 0xFFFFFFu;
    const char *p = "/tmp/wishuitest.png";
    png_write_rgb(p, px, 37, 23);
    FILE *f = fopen(p, "rb");
    static uint8_t buf[65536];
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    int w = 0, h = 0;
    uint32_t *d = png_decode(buf, n, &w, &h);
    int same = d && w == 37 && h == 23;
    for (int i = 0; same && i < 37 * 23; i++) if ((d[i] & 0xFFFFFFu) != px[i] || d[i] >> 24 != 255) same = 0;
    ok("a PNG the shell wrote decodes to the same pixels", same);
    free(d);
    /* Damage it: never a crash, never a picture of garbage. */
    int refused = 1;
    for (size_t at = 40; at < n; at += 7) {
        uint8_t keep = buf[at];
        buf[at] ^= 0x5A;
        uint32_t *bad = png_decode(buf, n, &w, &h);
        if (bad) {
            if (w != 37 || h != 23) refused = 0;
            free(bad);
        }
        buf[at] = keep;
    }
    ok("a damaged PNG is refused or decodes to the right size, never past its end", refused);
    int trunc_ok = 1;
    for (size_t cut = 8; cut < n; cut += (n / 23 + 1)) {
        /* An exact-size heap copy, so that a decoder reading one byte past
         * what it was given is caught by the sanitizer this is built with
         * rather than silently reading zeros from a bigger buffer. */
        uint8_t *tc = malloc(cut);
        memcpy(tc, buf, cut);
        uint32_t *r = png_decode(tc, cut, &w, &h);
        if (r) { trunc_ok = 0; free(r); }
        free(tc);
    }
    ok("a PNG cut short anywhere is refused without reading past its end", trunc_ok);

    /* The nastier cut: the chunk says it is whole, but the compressed
     * stream inside it stops early, so the inflater is the one that has
     * to notice. Chunk lengths are rewritten to match, with 12 bytes of
     * tail so the container looks intact. */
    size_t idat = 0;
    for (size_t i = 8; i + 12 < n; ) {
        size_t len = ((size_t)buf[i] << 24) | ((size_t)buf[i + 1] << 16) | ((size_t)buf[i + 2] << 8) | buf[i + 3];
        if (!memcmp(buf + i + 4, "IDAT", 4)) { idat = i; break; }
        i += 12 + len;
    }
    int inner_ok = idat != 0;
    size_t full = idat ? (((size_t)buf[idat] << 24) | ((size_t)buf[idat + 1] << 16) | ((size_t)buf[idat + 2] << 8) | buf[idat + 3]) : 0;
    for (size_t keep = 3; idat && keep < full; keep += (full / 17 + 1)) {
        size_t m = idat + 8 + keep + 12;
        uint8_t *tc = malloc(m);
        memcpy(tc, buf, idat + 8 + keep);
        memset(tc + idat + 8 + keep, 0, 12);
        tc[idat] = (uint8_t)(keep >> 24); tc[idat + 1] = (uint8_t)(keep >> 16);
        tc[idat + 2] = (uint8_t)(keep >> 8); tc[idat + 3] = (uint8_t)keep;
        uint32_t *r = png_decode(tc, m, &w, &h);
        if (r) { inner_ok = 0; free(r); }
        free(tc);
    }
    ok("a PNG whose compressed data stops early is refused, not read past", inner_ok);

    if (access(WISH_EMOJI_FONT, R_OK) != 0) {
        printf("  (no %s here: the font checks are skipped)\n", WISH_EMOJI_FONT);
        return;
    }
    int ew = 0, eh = 0, opaque = 0;
    const uint32_t *e = wishover_emoji("\xF0\x9F\xA5\x94", &ew, &eh);
    for (int i = 0; e && i < ew * eh; i++) if (e[i] >> 24 > 200) opaque++;
    ok("the potato comes out of Noto Color Emoji as a picture", e && ew > 64 && eh > 64 &&
       opaque > ew * eh / 4);
    const uint32_t *x = wishover_emoji("A", &ew, &eh);
    ok("a letter is not an emoji: nothing, not a box", x == NULL);
}

int main(void)
{
    now_ms = test_now;
    wishover_set_clock(test_now);
    setenv("XDG_RUNTIME_DIR", "/nonexistent-wishuitest", 1);
    printf("Wishes: what she sees and says\n");
    json();
    asking();
    trouble();
    overlays();
    shell_actions();
    targets();
    emoji();
    printf("\n%d checks, %d failed\n", checked, failed);
    return failed ? 1 : 0;
}
