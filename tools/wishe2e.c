/* wishe2e.c — the owner's potato, end to end, with nothing faked but the
 * internet and the compositor.
 *
 * What runs for real: the panel and overlays (src/aurshell/wish*.c), the
 * socket, and `aurwish` itself (stream W1's engine, test build), including
 * its proof of work, its curl, its validation of what comes back, the
 * sentence it writes, wishes.json, its triggers and its actions. What is
 * stood in: the Wishes service (tools/wishe2e-cloud.py: it answers from a
 * table) and the compositor (the typed word is handed over where
 * src/aurwl/wishhook.c would hand it).
 *
 * tools/wishe2e.sh builds and starts everything, then runs this.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/aurshell/wish.c"
#include "../src/aurshell/wishover.c"
#include "../src/aurshell/wishgallery.c"
#include "../src/aurshell/foot.h"

/* This scenario exercises the fill-screen action, not local theme changes.
 * Keep the unlinked Settings seam fail-closed if an unexpected event arrives. */
int settings_theme_current(char *out, size_t n)
{
    if (out && n) out[0] = 0;
    return -1;
}
int settings_theme_validate(const char *id) { (void)id; return 0; }
int settings_theme_apply(shell_ctx *c, const char *id)
{
    (void)c; (void)id;
    return 0;
}

static int checked, failed;
static void ok(const char *what, int good)
{
    checked++;
    if (!good) failed++;
    printf("  %-74s %s\n", what, good ? "ok" : "FAIL");
    fflush(stdout);
}

/* The compositor's half (src/aurwl/wishhook.h). */
static char got_words[10][33];
static int  got_nw = -1;
static char q_word[33];
void wishhook_set(const char *const *words, int nwords, const char *const *apps, int napps)
{
    (void)apps; (void)napps;
    got_nw = nwords;
    for (int i = 0; i < nwords && i < 10; i++)
        snprintf(got_words[i], sizeof got_words[i], "%s", words[i]);
}
int wishhook_take_word(char out[33])
{
    if (!q_word[0]) return 0;
    snprintf(out, 33, "%s", q_word);
    q_word[0] = 0;
    return 1;
}
int wishhook_take_app(char out[128]) { (void)out; return 0; }

static uint64_t rnow(void) { return real_now_ms(); }

static shell_ctx C;

/* Keep the shell's loop turning (without a compositor) for up to `ms`,
 * until `done` says so. */
static int pump(int ms, int (*done)(void))
{
    uint64_t t0 = rnow();
    for (;;) {
        wish_step(&C);
        if (done && done()) return 1;
        if (rnow() - t0 >= (uint64_t)ms) return done ? done() : 0;
        usleep(20000);
    }
}

static int connected(void)   { return K.fd >= 0 && W.consent >= 0; }
static int on_consent(void)  { return W.page == WP_CONSENT; }
static int on_confirm(void)  { return W.page == WP_CONFIRM; }
static int on_wait(void)     { return W.page == WP_WAIT; }
static int on_asking(void)   { return W.page == WP_ASK; }
static int have_rows(void)   { return W.n_rows >= 1; }
static int on_done(void)     { return W.page == WP_DONE; }
static int have_words(void)  { return got_nw == 1; }
static int filling(void)     { return wishover_fill_on(); }
static int not_filling(void) { return !wishover_fill_on(); }
static int listed(void)      { return W.page == WP_MINE; }
static int bigger(void)      { return C.text_scale > 1.25f; }
static int undo_there(void)  { return wish_undo_offered(); }
static int normal(void)      { return C.text_scale > 0.95f && C.text_scale < 1.05f; }
static int night_on(void)    { return C.night_light == 1; }
static int night_off(void)   { return C.night_light == 0; }

static wish_target *find(wish_target *t, int n, int kind, int arg)
{
    for (int i = 0; i < n; i++) if (t[i].kind == kind && (arg < 0 || t[i].arg == arg)) return &t[i];
    return NULL;
}
static int press(int kind, int arg)
{
    wish_view v = view_now(&C);
    wish_target t[WISH_MAX_TARGETS];
    int n = wish_targets(&C, C.screen_w, C.screen_h, &v, t, WISH_MAX_TARGETS);
    wish_target *x = find(t, n, kind, arg);
    if (!x) return 0;
    wish_click(&C, x->r.x + 3, x->r.y + 3);
    return 1;
}
static int press_item(int idx)
{
    for (int tries = 0; tries < 12; tries++) {
        if (press(WT_ITEM, idx)) return 1;
        if (!press(WT_MORE, -1)) return 0;
    }
    return 0;
}

static int service_calls(const char *path)
{
    FILE *f = fopen(path, "r");
    int n = 0, ch;
    if (!f) return 0;
    while ((ch = fgetc(f)) != EOF) if (ch == '\n') n++;
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    const char *service_log = argc > 1 ? argv[1] : "/tmp/wishe2e-service.log";
    memset(&C, 0, sizeof C);
    theme_t t = {0};
    shell_theme_load(&C, &t);
    C.text_scale = 1.f;
    C.foot_hover = -1;
    C.screen_w = 1366;
    C.screen_h = 768 - foot_height(&C);
    C.allow_settings = 1;
    snprintf(C.layout_id, sizeof C.layout_id, "%s", "rail");

    printf("the potato, end to end (real engine, stand-in service)\n");
    printf("\n");
    ok("the panel finds the engine and learns it has not been agreed to yet",
       pump(10000, connected) && W.consent == 0);

    C.wish_open = WISH_OPEN_ASK;
    wish_opened(&C);
    ok("not agreed to yet: the panel opens on the page that says what is sent where", W.page == WP_CONSENT);
    press(WT_CONSENT_YES, -1);
    ok("she agrees; the engine is told and the question comes up", pump(5000, on_asking) && W.consent == 1);
    const char *said = "Fill my screen with potatoes when I type potato";
    for (const char *p = said; *p; p++) { C.key_text[0] = *p; C.key_text[1] = 0; wish_key(&C, 30); }
    C.key_text[0] = 0;
    wish_key(&C, 28);
    ok("she types her wish and presses Enter: thinking", W.page == WP_WAIT && !strcmp(W.said, said));
    ok("the engine answers with its own sentence (this includes its proof of work)",
       pump(60000, on_confirm) && strstr(W.sentence, "potato") && strstr(W.sentence, "*3 seconds*") &&
       !strcmp(W.said, said));
    printf("    the engine says: %s\n", W.sentence);
    ok("and the service was asked exactly once", service_calls(service_log) == 1);

    press(WT_YES, -1);
    ok("Yes: the wish is on", pump(10000, on_done) && !wish_undo_offered());
    ok("the engine hands the compositor the word to listen for", pump(5000, have_words) && !strcmp(got_words[0], "potato"));

    /* She types the word somewhere. */
    snprintf(q_word, sizeof q_word, "potato");
    ok("she types potato: the screen fills with potatoes", pump(15000, filling) && !strcmp(O.emoji, "\xF0\x9F\xA5\x94"));
    /* The engine lets one wish put cards up only once a minute (docs/AI.md
     * section 7.4), so there is one firing to look at: it is stopped from
     * the potatoes themselves. (The first key ending a fill is tested,
     * with a stand-in engine, in tools/wishuitest.c.) */
    wish_target ot[24];
    int on = wishover_targets(&C, C.screen_w, C.screen_h, ot, 24), stop = -1;
    for (int i = 0; i < on; i++) if (ot[i].kind == WOT_STOP) stop = i;
    ok("the potatoes say whose wish it is and offer Stop this wish", stop >= 0 && O.wish[0]);
    if (stop >= 0) { wishover_click(&C, ot[stop].r.x + 3, ot[stop].r.y + 3); wishover_take_release(); }
    ok("Stop this wish takes the potatoes away", pump(500, not_filling));
    pump(1500, 0);
    snprintf(q_word, sizeof q_word, "potato");
    ok("after Stop this wish, typing potato does nothing", !pump(4000, filling));

    /* My wishes. */
    C.wish_open = WISH_OPEN_MINE;
    wish_opened(&C);
    ok("My wishes lists it, switched off", pump(5000, have_rows) && listed() && W.n_rows == 1 &&
       !W.rows[0].on && strstr(W.rows[0].sentence, "potato"));
    press(WT_REMOVE, 0);
    press(WT_REMOVE_YES, 0);
    pump(2000, 0);
    k_send(OP_LIST, "list", NULL);
    pump(3000, 0);
    ok("Remove it: nothing left", W.n_rows == 0);

    /* The gallery: no internet involved. */
    C.wish_open = WISH_OPEN_ASK;
    wish_opened(&C);
    int before = service_calls(service_log);
    press(WT_GALLERY, -1);
    press_item(0);                                         /* Make the words bigger */
    ok("a wish pressed from the list is checked by the engine, not sent anywhere",
       pump(8000, on_confirm) && strstr(W.sentence, "bigger") && service_calls(service_log) == before);
    press(WT_YES, -1);
    ok("Yes: the shell makes the words bigger, and the engine is told", pump(8000, bigger));
    ok("with Keep this? and Undo my last wish ready", pump(5000, undo_there) && wishover_cards() >= 2);
    C.wish_open = 0;
    wish_target ut[24];
    int un = wishover_targets(&C, C.screen_w, C.screen_h, ut, 24), ui = -1;
    for (int i = 0; i < un; i++) if (ut[i].kind == WOT_UNDOWISH) ui = i;
    ok("the Undo card is there", ui >= 0);
    if (ui >= 0) { wishover_click(&C, ut[ui].r.x + 3, ut[ui].r.y + 3); wishover_take_release(); }
    ok("Undo my last wish: the engine puts the words back", pump(8000, normal));

    /* Another built-in wish, through the same real engine, reaches the
     * completed-frame colour adapter and carries its previous state back
     * through the engine's ordinary Undo path. */
    C.wish_open = WISH_OPEN_ASK;
    wish_opened(&C);
    before = service_calls(service_log);
    press(WT_GALLERY, -1);
    press_item(7);                                        /* Switch the night light on */
    ok("the built-in night-light wish is checked locally",
       pump(8000, on_confirm) && strstr(W.sentence, "warm night colours") &&
       service_calls(service_log) == before);
    press(WT_YES, -1);
    ok("Yes: the real engine switches the completed display path to warm colours",
       pump(8000, night_on) && C.night_frame != NULL);
    wish_undo(&C);
    ok("Undo: the real engine restores the previous night-light state",
       pump(8000, night_off));
    nightlight_fini(&C);

    printf("\n%d checks, %d failed\n", checked, failed);
    return failed ? 1 : 0;
}
