/* wishgallerytest.c — the wishes she can make by pressing
 * (src/aurshell/wishgallery.c, docs/AI.md §7 change 9).
 *
 * The one thing that matters about a template is that the engine ACCEPTS
 * every wish it can produce: a gallery that offers a wish the device then
 * refuses is worse than no gallery. So every option of every blank, in
 * every combination, is built and handed to the engine's own validator
 * (src/aurwish/wish.c, stream W1) with the sentence she would have said.
 *
 *   cc -std=gnu11 -g -O1 -fsanitize=address,undefined -I src/aurwish \
 *      tools/wishgallerytest.c src/aurwish/{json,text,wish,store}.c -lm \
 *      -o /tmp/wishgallerytest && /tmp/wishgallerytest
 *   (tools/wishgallerytest.sh does that, then breaks the table five ways.)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/aurshell/wishgallery.c"
#include "wish.h"

static int checked, failed;
static void ok(const char *what, int good)
{
    checked++;
    if (!good) failed++;
    printf("  %-72s %s\n", what, good ? "ok" : "FAIL");
}

static int accepts(const char *doc, const char *said, char *why, size_t whyn)
{
    wish_env env;
    memset(&env, 0, sizeof env);
    env.said = said;
    wish w;
    wish_err err;
    memset(&err, 0, sizeof err);
    int rc = wish_parse_text(doc, strlen(doc), &env, &w, &err);
    if (rc != 0) {
        snprintf(why, whyn, "%s (%s at %s)", err.why ? err.why : "?", err.missing ? err.missing : "?", err.where);
        return 0;
    }
    char sent[600];
    if (wish_sentence(&w, sent, sizeof sent) < 0 || !sent[0]) {
        snprintf(why, whyn, "no sentence");
        return 0;
    }
    return 1;
}

int main(void)
{
    printf("the gallery\n");
    int n = wg_count();
    ok("about twenty templates", n >= 18 && n <= 24);

    /* The engine is alive and strict: a stop word is refused. */
    char why[200];
    ok("the validator refuses 'love' as a typed word (so a pass means something)",
       !accepts("{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"love\"},\"do\":[{\"type\":\"fill_screen\",\"emoji\":\"\\u2764\",\"seconds\":3}]}",
                "Fill my screen with hearts when I type love", why, sizeof why));
    ok("and refuses text that is not in what she said",
       !accepts("{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"09:00\"},\"do\":[{\"type\":\"remind\",\"text\":\"Take my pills\"}]}",
                "Remind me at 9:00 am: Water the plants", why, sizeof why));

    int combos = 0, refused = 0, sentenceless = 0;
    char first_why[300] = "";
    int bad_chars = 0, too_long = 0;
    for (int t = 0; t < n; t++) {
        const wg_tmpl *tp = wg_get(t);
        int total = 1;
        for (int k = 0; k < tp->n_slots; k++) total *= wg_slot_get(tp->slot[k])->n;
        for (int c = 0; c < total; c++) {
            int choice[WG_MAX_SLOTS] = { 0, 0 }, rest = c;
            for (int k = tp->n_slots - 1; k >= 0; k--) {
                int sn = wg_slot_get(tp->slot[k])->n;
                choice[k] = rest % sn;
                rest /= sn;
            }
            char said[300], doc[700];
            if (wg_build(t, choice, said, sizeof said, doc, sizeof doc) < 0) { refused++; continue; }
            combos++;
            if (strpbrk(said, "\"\\") || strchr(said, '@')) bad_chars++;
            if (strlen(said) > 140) too_long++;
            if (strchr(doc, '@')) { refused++; if (!first_why[0]) snprintf(first_why, sizeof first_why, "%s: a blank was left in the document", tp->pick); continue; }
            if (!accepts(doc, said, why, sizeof why)) {
                refused++;
                if (!first_why[0]) snprintf(first_why, sizeof first_why, "%s [%s] -> %s | %s", tp->pick, said, why, doc);
            }
        }
    }
    printf("    %d wishes built from the table\n", combos);
    if (first_why[0]) printf("    first refusal: %s\n", first_why);
    char w[100];
    snprintf(w, sizeof w, "the engine accepts all %d wishes the gallery can build", combos);
    ok(w, combos > 100 && refused == 0);
    ok("no sentence has a quote, a backslash or an unfilled blank", bad_chars == 0);
    ok("no sentence is longer than 140 characters", too_long == 0);
    (void)sentenceless;

    /* Her choices are checked: nothing out of range builds. */
    char said[300], doc[700];
    int bad_choice[2] = { 999, 0 };
    ok("a choice out of range builds nothing", wg_build(9, bad_choice, said, sizeof said, doc, sizeof doc) < 0);
    int neg[2] = { -1, 0 };
    ok("a negative choice builds nothing", wg_build(9, neg, said, sizeof said, doc, sizeof doc) < 0);
    int zero[2] = { 0, 0 };
    ok("a template that does not exist builds nothing", wg_build(n, zero, said, sizeof said, doc, sizeof doc) < 0);
    ok("a document that will not fit builds nothing", wg_build(9, zero, said, sizeof said, doc, 20) < 0);

    /* The quiet hours: nothing from a clock that makes a sound, a voice
     * or a screen is offered before 8 or after 8 in the evening. */
    const wg_slot *tm = wg_slot_get(2);
    int quiet_ok = 1;
    for (int i = 0; i < tm->n; i++) {
        int hh = atoi(tm->opts[i].value);
        if (hh < 8 || hh > 20) quiet_ok = 0;
    }
    ok("the times offered for sounds, speech and screens are 8 am to 8 pm", quiet_ok && strlen(tm->opts[0].value) == 5);

    /* Every blank has something to press, and every button has a name. */
    int named = 1;
    for (int s = 0; s < 9; s++) {
        const wg_slot *sl = wg_slot_get(s);
        if (!sl || sl->n < 2 || sl->n > 12 || !sl->ask) { named = 0; continue; }
        for (int i = 0; i < sl->n; i++)
            if (!sl->opts[i].label || !sl->opts[i].label[0] || strlen(sl->opts[i].label) > 40) named = 0;
    }
    ok("every blank has 2 to 12 options, each with a short name", named);
    int pick_ok = 1;
    for (int t = 0; t < n; t++) if (!wg_get(t)->pick || strlen(wg_get(t)->pick) > 60) pick_ok = 0;
    ok("every template's line is at most 60 characters", pick_ok);

    printf("\n%d checks, %d failed\n", checked, failed);
    return failed ? 1 : 0;
}
