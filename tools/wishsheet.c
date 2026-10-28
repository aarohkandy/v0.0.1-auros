/* wishsheet.c — every screen of Wishes, as pictures, through the real
 * paint path (docs/AI.md §4; docs/shots/wishes/).
 *
 * Like tools/setsheet.c it reaches the panel's private state by
 * including wish.c, rather than cutting a door into shipping code for a
 * camera. The look behind the panel, the band in front of it and the
 * overlays are the shell's own functions.
 *
 *   cc -O2 -std=gnu11 -o /tmp/wishsheet tools/wishsheet.c \
 *      src/aurshell/wishover.c src/aurshell/foot.c src/aurshell/run.c \
 *      src/aurshell/apps.c src/aurshell/draw.c src/aurshell/shellcommon.c \
 *      src/aurshell/anim.c src/aurshell/layouts/[a-z]*.c src/common/theme.c \
 *      src/common/font.c src/common/png.c src/common/wall.c -Isrc/aurshell -lm
 *   /tmp/wishsheet <shell.conf> <shells/rail.shell> <outdir> [w h [scale]]
 *
 * Writes <outdir>/<look>-<w>x<h>-<state>.png for every state.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/aurshell/wish.c"
#include "../src/aurshell/wishgallery.c"
#include "../src/aurshell/foot.h"
#include "../src/common/png.h"
#include "../src/common/wall.h"

static uint64_t fake_ms = 1000000;
static uint64_t fake_now(void) { return fake_ms; }

static void reset(void)
{
    memset(W.text, 0, sizeof W.text);
    W.said[0] = W.sentence[0] = W.title[0] = W.pending[0] = 0;
    W.dest[0] = W.help_page[0] = W.note[0] = W.hint[0] = 0;
    W.req_id = 0; W.undo_until = 0; W.removing = -1; W.first_row = 0;
    W.shown_once = 0; W.all_off = 0; W.voice = 0; W.sel = -1; W.hover = -1;
    W.n_rows = 0;
}

static void rows(int n)
{
    static const char *const S[] = {
        "Every time you type *potato*, your screen fills with potatoes for *3 seconds*.",
        "Every day at *9:00 am*, a note says *Take my pills* until you close it.",
        "When this computer starts, it plays *birdsong*.",
        "Every day at *8:00 am*, this computer says *good morning*.",
        "Every hour, a *gentle* sound plays, from 8 am to 9 pm.",
        "When the battery is below *20%*, the screen dims to *40%*.",
        "Every Sunday at *4:00 pm*, Internet opens *the video call*.",
    };
    W.n_rows = n;
    for (int i = 0; i < n; i++) {
        snprintf(W.rows[i].wid, sizeof W.rows[i].wid, "w%d", i + 1);
        snprintf(W.rows[i].sentence, sizeof W.rows[i].sentence, "%s", S[i % 7]);
        W.rows[i].on = (i != 2);
        W.rows[i].paused = 0;
    }
}

typedef struct { const char *name; void (*set)(shell_ctx *c); } state;

static const char *POTATO_SAID = "Fill my screen with potatoes when I type potato";
static const char *POTATO_SENT =
    "Every time you type the word *potato*, your screen fills with potatoes "
    "for *3 seconds*. Any key or click makes them go. It keeps happening "
    "until you turn it off in *My wishes*.";

static void s_consent(shell_ctx *c) { (void)c; W.consent = 0; go(WP_CONSENT); }
static void s_ask(shell_ctx *c) { (void)c; go(WP_ASK); }
static void s_typed(shell_ctx *c) { (void)c; go(WP_ASK);
    snprintf(W.text, sizeof W.text, "%s", "Remind me to call my daughter on Sundays at 4"); }
static void s_short(shell_ctx *c) { (void)c; go(WP_ASK); snprintf(W.text, sizeof W.text, "hm");
    snprintf(W.hint, sizeof W.hint, "%s", "Tell me a little more about what you would like."); }
static void s_listen(shell_ctx *c) { (void)c; W.v_t0 = fake_ms - 4000; go(WP_LISTEN); }
static void s_hearing(shell_ctx *c) { (void)c; go(WP_HEARING); }
static void s_wait(shell_ctx *c) { (void)c; snprintf(W.said, sizeof W.said, "%s", POTATO_SAID); go(WP_WAIT); }
static void s_confirm(shell_ctx *c) { (void)c;
    snprintf(W.said, sizeof W.said, "%s", POTATO_SAID);
    snprintf(W.sentence, sizeof W.sentence, "%s", POTATO_SENT);
    snprintf(W.pending, sizeof W.pending, "p7");
    snprintf(W.title, sizeof W.title, "Potatoes");
    go(WP_CONFIRM); }
static void s_confirm_site(shell_ctx *c) { (void)c;
    snprintf(W.said, sizeof W.said, "%s", "Open the news");
    snprintf(W.sentence, sizeof W.sentence, "%s",
             "*Now*, Internet opens *BBC News* in a new window. This happens once.");
    snprintf(W.dest, sizeof W.dest, "BBC News");
    snprintf(W.pending, sizeof W.pending, "p8");
    go(WP_CONFIRM); }
static void s_cannot(shell_ctx *c) { (void)c;
    snprintf(W.said, sizeof W.said, "%s", "When my laptop points north, make the screen brighter");
    snprintf(W.note, sizeof W.note, "%s",
             "This computer has no compass, so it can't tell which way it is pointing.");
    go(WP_CANNOT); }
static void s_answer(shell_ctx *c) { (void)c;
    snprintf(W.help_page, sizeof W.help_page, "printing");
    snprintf(W.pending, sizeof W.pending, "p9");
    snprintf(W.sentence, sizeof W.sentence, "%s", "*Now*, the printer settings open.");
    go(WP_ANSWER); }
static void s_done(shell_ctx *c) { (void)c;
    snprintf(W.sentence, sizeof W.sentence, "%s", POTATO_SENT);
    snprintf(W.title, sizeof W.title, "Potatoes");
    go(WP_DONE); }
static void s_done_now(shell_ctx *c) { (void)c;
    snprintf(W.sentence, sizeof W.sentence, "%s", "The words on this computer are now *bigger*.");
    snprintf(W.title, sizeof W.title, "Bigger words");
    W.undo_until = fake_ms + 600000;
    go(WP_DONE); }
static void s_offline(shell_ctx *c) { (void)c; set_error("offline", NULL); }
static void s_limit(shell_ctx *c) { (void)c; set_error("limit", NULL); }
static void s_broken(shell_ctx *c) { (void)c; set_error("broken", NULL); }
static void s_nomic(shell_ctx *c) { (void)c; set_error("nomic", NULL); }
static void s_mine(shell_ctx *c) { (void)c; rows(4); go(WP_MINE); }
static void s_mine_many(shell_ctx *c) { (void)c; rows(7); go(WP_MINE); }
static void s_mine_remove(shell_ctx *c) { (void)c; rows(4); go(WP_MINE); W.removing = 1; }
static void s_mine_empty(shell_ctx *c) { (void)c; rows(0); go(WP_MINE); }
static void s_mine_alloff(shell_ctx *c) { (void)c; rows(1); W.all_off = 1; go(WP_MINE); }
static void s_today(shell_ctx *c) { (void)c;
    W.n_today = 3;
    snprintf(W.today[0].title, sizeof W.today[0].title, "Potatoes"); W.today[0].times = 3;
    snprintf(W.today[1].title, sizeof W.today[1].title, "Pills at 9"); W.today[1].times = 1;
    snprintf(W.today[2].title, sizeof W.today[2].title, "Good morning"); W.today[2].times = 1;
    W.first_row = 0; go(WP_TODAY); }
static void s_pick_list(shell_ctx *c) { (void)c; W.g_stage = -1; W.g_first = 0; go(WP_PICK); }
static void s_pick_pic(shell_ctx *c) { (void)c; W.g_tmpl = 13; W.g_stage = 0; W.g_first = 0; go(WP_PICK); }
static void s_pick_word(shell_ctx *c) { (void)c; W.g_tmpl = 13; W.g_stage = 1; W.g_first = 0; go(WP_PICK); }
static void s_pick_time(shell_ctx *c) { (void)c; W.g_tmpl = 9; W.g_stage = 0; W.g_first = 0; go(WP_PICK); }

static const state STATES[] = {
    { "01-consent", s_consent }, { "02-ask", s_ask }, { "03-ask-typed", s_typed },
    { "04-ask-short", s_short }, { "05-listen", s_listen }, { "06-hearing", s_hearing },
    { "07-wait", s_wait }, { "08-confirm", s_confirm }, { "09-confirm-site", s_confirm_site },
    { "10-cannot", s_cannot }, { "11-answer", s_answer }, { "12-done", s_done },
    { "13-done-now", s_done_now }, { "14-offline", s_offline }, { "15-limit", s_limit },
    { "16-broken", s_broken }, { "17-nomic", s_nomic }, { "18-mine", s_mine },
    { "19-mine-many", s_mine_many }, { "20-mine-remove", s_mine_remove },
    { "21-mine-empty", s_mine_empty }, { "27-mine-alloff", s_mine_alloff }, { "22-pick-list", s_pick_list },
    { "23-pick-picture", s_pick_pic }, { "24-pick-word", s_pick_word },
    { "25-pick-time", s_pick_time }, { "26-today", s_today },
};
#define NSTATES ((int)(sizeof STATES / sizeof STATES[0]))

static int clipped_total;
static int save(const char *dir, const char *look, int w, int h, const char *name, surface *s)
{
    char path[512];
    /* Words cut off while painting this picture: the layout is there so
     * that there are none (tools/wishpainttest.sh). */
    int cut = wish_clipped() + wishover_clipped();
    if (cut) {
        printf("CLIPPED %s %dx%d %s: %d (\"%s\")\n", look, w, h, name, cut, wish_clip_what());
        clipped_total += cut;
    }
    snprintf(path, sizeof path, "%s/%s-%dx%d-%s.png", dir, look, w, h, name);
    uint32_t *o = malloc((size_t)w * h * sizeof *o);
    if (!o) return -1;
    for (int i = 0; i < w * h; i++) o[i] = s->px[i] & 0xFFFFFFu;
    int rc = png_write_rgb(path, o, w, h);
    free(o);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: wishsheet <shell.conf> <x.shell> <outdir> [w h [scale]]\n");
        return 2;
    }
    int W_ = argc > 5 ? atoi(argv[4]) : 1366, H_ = argc > 5 ? atoi(argv[5]) : 768;
    float scale = argc > 6 ? (float)atof(argv[6]) : 1.f;
    now_ms = fake_now;
    wishover_set_clock(fake_now);

    theme_t t = {0};
    if (theme_load(&t, argv[1]) < 0) { fprintf(stderr, "no %s\n", argv[1]); return 1; }
    shell_ctx c;
    memset(&c, 0, sizeof c);
    shell_theme_load(&c, &t);
    if (shell_archetype_load(&c, argv[2]) < 0) { fprintf(stderr, "no %s\n", argv[2]); return 1; }
    c.allow_install = c.allow_settings = c.allow_theme_change = c.allow_network = 1;
    c.text_scale = scale;
    c.foot_hover = -1; c.hover = -1; c.focus = -1;
    c.mouse_x = c.mouse_y = -1;
    shell_seed_apps(&c);
    shell_scan_apps(&c);
    const shell_layout *L = shell_layout_by_id(c.layout_id);
    c.screen_w = W_;
    c.screen_h = H_ - foot_height(&c);
    if (L->init) L->init(&c);
    shell_fonts f;
    shell_fonts_load(&f, &c);

    surface *wall = surface_new(W_, H_), *s = surface_new(W_, H_);
    uint32_t *tmp = malloc((size_t)W_ * H_ * sizeof *tmp);
    wall_render(tmp, W_, H_, &t);
    for (int i = 0; i < W_ * H_; i++) wall->px[i] = 0xFF000000u | tmp[i];
    free(tmp);

    char look[32];
    snprintf(look, sizeof look, "%s", c.layout_id);
    char sc[16];
    snprintf(sc, sizeof sc, "%s", scale > 1.01f ? "-big" : "");

    /* The home, with Make a wish on it. */
    #define HOME() do { surface body = *s; body.h = c.screen_h; \
        draw_track_reset(); L->paint(&c, &body, &f, wall); } while (0)
    HOME();
    foot_paint(&c, s, &f);
    char nm[64];
    snprintf(nm, sizeof nm, "00-home%s", sc);
    save(argv[3], look, W_, H_, nm, s);

    /* Every page of the panel. */
    c.wish_open = WISH_OPEN_ASK;
    W.consent = 1;
    for (int i = 0; i < NSTATES; i++) {
        reset();
        W.consent = 1;
        STATES[i].set(&c);
        HOME();
        wish_paint(&c, s, &f);
        foot_paint(&c, s, &f);
        snprintf(nm, sizeof nm, "%s%s", STATES[i].name, sc);
        save(argv[3], look, W_, H_, nm, s);
    }
    c.wish_open = 0;

    /* What a wish puts on the screen, over her home. */
    wishover_fill(&c, "\xF0\x9F\xA5\x94", NULL, 3, "Potatoes", "w1");
    fake_ms += 400;
    HOME(); foot_paint(&c, s, &f); wishover_paint(&c, s, &f);
    snprintf(nm, sizeof nm, "30-fill-potato%s", sc);
    save(argv[3], look, W_, H_, nm, s);
    wishover_clear(&c);

    wishover_card(&c, WO_REMIND, "Take my pills", "Pills at 9", "w2", NULL, 0);
    HOME(); foot_paint(&c, s, &f); wishover_paint(&c, s, &f);
    snprintf(nm, sizeof nm, "31-remind%s", sc);
    save(argv[3], look, W_, H_, nm, s);
    wishover_clear(&c);

    wishover_card(&c, WO_MESSAGE, "Have a lovely day", "Good morning", "w4", NULL, 0);
    HOME(); foot_paint(&c, s, &f); wishover_paint(&c, s, &f);
    snprintf(nm, sizeof nm, "32-message%s", sc);
    save(argv[3], look, W_, H_, nm, s);
    wishover_clear(&c);

    wishover_keep(&c, WO_KEEP_TEXT, "1.00", "Bigger words");
    HOME(); foot_paint(&c, s, &f); wishover_paint(&c, s, &f);
    snprintf(nm, sizeof nm, "33-keep-this%s", sc);
    save(argv[3], look, W_, H_, nm, s);
    {   /* She presses Keep it: that card goes. */
        wish_target kt[24];
        int kn = wishover_targets(&c, c.screen_w, c.screen_h, kt, 24);
        for (int i = 0; i < kn; i++)
            if (kt[i].kind == WOT_KEEP) { wishover_click(&c, kt[i].r.x + 3, kt[i].r.y + 3); break; }
        wishover_take_release();
    }

    W.undo_until = fake_ms + 600000;
    wishover_card(&c, WO_UNDO, "It is done. If it is not what you wanted, you can put it back.",
                  "Bigger words", NULL, NULL, 0);
    HOME(); foot_paint(&c, s, &f); wishover_paint(&c, s, &f);
    snprintf(nm, sizeof nm, "34-undo%s", sc);
    save(argv[3], look, W_, H_, nm, s);

    printf("%s %dx%d text %.0f%%: %d pictures in %s\n", look, W_, H_,
           (double)(scale * 100.f), NSTATES + 6, argv[3]);
    return (clipped_total && getenv("WISHSHEET_STRICT")) ? 3 : 0;
}
