/* floorstest.c -- the shell's EASY floors, measured on the real looks.
 *
 *   text      nothing she reads is under 18 px, at any size she can choose
 *             (the shell's one font set, opened at every text scale)
 *   targets   every control she presses is 44 px or more on its short side, at 1024x600
 *             and the sizes above it, in each of the four looks: dock cells, rail rows,
 *             taskbar rows and tray, tiles, the buttons in a window's title bar
 *             (every layout publishes them: shell_layout.targets)
 *   bounds    a window is placed wholly on the screen, in every look, whatever size it
 *             asked for and wherever it asked to be (OBS-REAL-IMAGE)
 *   waiting   the words on a card that is waiting for its program ("Still starting. This computer is
 *             taking a while.") are drawn whole and inside the card, in every look, at 1024x600 and up, at
 *             every text size (FLOORS: WAIT_DUMP=1, WK=<scale index>, WS=<size index> list what was drawn)
 *   quiet     the hint and a window's subtitle, as painted, clear 7:1 on the Light look
 *             (the shell draws them at part strength; the ink the eye gets is what counts)
 *
 *   cc -O2 -std=gnu11 -o /tmp/floorstest tools/floorstest.c src/aurshell/draw.c \
 *      src/aurshell/shellcommon.c src/aurshell/anim.c src/aurshell/foot.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      -I src/aurshell -I src/common -lm && /tmp/floorstest     (repository root)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "../src/aurshell/shell.h"
#include "../src/aurshell/foot.h"
#include "../src/common/font.h"

static int fail = 0, checked = 0;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-72s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

static const float SCALES[] = { 0.80f, 1.00f, 1.50f, 2.00f };

/* Font metrics vary by face: an 18 px em need not have a 21 px
 * ascent-plus-descent. Check the size the shell actually requested. */
static int tall_enough(const font *f) { return f && font_size(f) >= 18.f; }

static void text_floor(void)
{
    printf("\ntext: no word under 18 px, at any size\n");
    for (size_t k = 0; k < sizeof SCALES / sizeof *SCALES; k++) {
        shell_ctx c; memset(&c, 0, sizeof c);
        theme_t t = {0};
        shell_theme_load(&c, &t);
        c.text_scale = SCALES[k];
        shell_fonts f; memset(&f, 0, sizeof f);
        shell_fonts_load(&f, &c);
        char nm[96];
        snprintf(nm, sizeof nm, "text %.0f%%: every font the shell draws with is 18 px or more", (double)(SCALES[k] * 100.f));
        ok(nm, tall_enough(f.mid) && tall_enough(f.label) && tall_enough(f.small) && tall_enough(f.tiny) &&
               tall_enough(f.dmid) && tall_enough(f.big) && tall_enough(f.huge));
        shell_fonts_free(&f);
    }
}

/* ── a look with apps and windows, as the real shell would hold them ───────────────────── */
typedef struct { shell_ctx c; const shell_layout *L; surface *s; surface *app; shell_fonts f; } rig;

static const char *NAMES[] = { "Internet", "Your files", "Music and video", "Photos", "Letters",
                               "Spreadsheets", "Notes", "Printers", "Calculator", "Documents",
                               "Zip files", "Get more programs" };

static const char *waiting_sub;     /* set: the one window is a card still waiting for its program, with this line */
static int none_in_use;             /* set: no window is the one in use (so none is painted over by the one that is) */
static const char *light_conf;       /* set: the shell.conf of the Light look; unset: the built-in colours */

static void make(rig *r, const char *look, int W, int H, float scale, int napps, int nwins, int csd0)
{
    memset(&r->c, 0, sizeof r->c);
    theme_t t = {0};
    if (light_conf && theme_load(&t, light_conf) < 0) { fprintf(stderr, "cannot read %s\n", light_conf); exit(2); }
    shell_theme_load(&r->c, &t);
    char shf[96];
    snprintf(shf, sizeof shf, "shells/%s.shell", look);
    if (shell_archetype_load(&r->c, shf) < 0) { fprintf(stderr, "cannot read %s -- run from the repository root\n", shf); exit(2); }
    r->c.text_scale = scale;
    r->c.focus = r->c.hover = r->c.foot_hover = -1;
    r->c.mouse_x = r->c.mouse_y = -1;
    r->c.screen_w = W;
    r->c.screen_h = H - foot_height(&r->c);
    r->L = shell_layout_by_id(r->c.layout_id);
    if (r->s) { surface_free(r->s); r->s = NULL; }
    if (!r->app) { r->app = surface_new(900, 600); for (int i = 0; i < r->app->w * r->app->h; i++) r->app->px[i] = 0xFF12AB34u; }
    r->s = surface_new(W, H);
    for (int i = 0; i < W * H; i++) r->s->px[i] = 0xFF000000u | r->c.bg;     /* the desktop under it, as the real shell lays it */
    shell_fonts_free(&r->f);
    memset(&r->f, 0, sizeof r->f);
    shell_fonts_load(&r->f, &r->c);
    for (int i = 0; i < napps; i++) {
        app_entry *a = &r->c.apps[i];
        memset(a, 0, sizeof *a);
        snprintf(a->name, sizeof a->name, "%s", NAMES[i % 12]);
        a->icon = ICON_WINDOW; a->tint = r->c.accent; a->pinned = i < 7;
        if (i == napps - 1) a->role = APP_ROLE_STORE;
    }
    r->c.n_apps = napps;
    for (int i = 0; i < nwins; i++) {
        win_entry *e = &r->c.wins[i];
        memset(e, 0, sizeof *e);
        snprintf(e->title, sizeof e->title, "A program %d", i + 1);
        snprintf(e->subtitle, sizeof e->subtitle, "A page title");
        e->app = i < napps ? i : -1;
        e->content = r->app;
        e->wid = (uint32_t)(7 + i);
        e->csd = (i == 0) ? csd0 : 0;
        if (waiting_sub) {                       /* no window yet: the card the shell shows while it waits */
            e->wid = 0; e->content = NULL; e->starting = 1; e->csd = 0;
            snprintf(e->title, sizeof e->title, "%s", "Internet");
            if (e->app >= 0) snprintf(r->c.apps[e->app].hint, sizeof r->c.apps[e->app].hint, "%s", "Websites, news and shopping");
            snprintf(e->subtitle, sizeof e->subtitle, "%s", waiting_sub);
        }
    }
    r->c.n_wins = nwins;
    r->c.focus = (nwins && !none_in_use) ? 0 : -1;
    if (r->L->init) r->L->init(&r->c);
    for (int i = 0; i < nwins; i++) if (r->L->present) r->L->present(&r->c, i);
    if (r->L->step) for (int i = 0; i < 80; i++) r->L->step(&r->c, 0.05f);
    surface body = *r->s; body.h = r->c.screen_h;
    draw_track_reset();
    r->L->paint(&r->c, &body, &r->f, NULL);
}

/* The ratio of the darkest pixel in a drawn word's box to the lightest: what the eye gets of it. */
static double lum8(uint32_t p)
{
    double v[3] = { (double)((p >> 16) & 0xFF), (double)((p >> 8) & 0xFF), (double)(p & 0xFF) };
    for (int i = 0; i < 3; i++) { v[i] /= 255.0; v[i] = v[i] <= 0.03928 ? v[i] / 12.92 : pow((v[i] + 0.055) / 1.055, 2.4); }
    return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
}
static double word_ratio(const surface *s, const shell_text_rec *t)
{
    double lo = 2, hi = -1;
    for (int y = t->y; y < t->y + t->h; y++)
        for (int x = t->x; x < t->x + t->w; x++) {
            if (x < 0 || y < 0 || x >= s->w || y >= s->h) continue;
            double l = lum8(s->px[y * s->stride + x]);
            if (l < lo) lo = l;
            if (l > hi) hi = l;
        }
    return hi < 0 ? 0 : (hi + 0.05) / (lo + 0.05);
}

/* Painted with the text log on, then every drawn copy of `word` is measured (the least readable counts). */
static void quiet(const char *look, int nwins, const char *word)
{
    rig R; memset(&R, 0, sizeof R);
    shell_text_log_n = 0; shell_text_log_on = 1;
    none_in_use = nwins > 0;                     /* a window she is not using is the dimmer one */
    make(&R, look, 1366, 768, 1.f, 12, nwins, 0);
    none_in_use = 0;
    shell_text_log_on = 0;
    double worst = 99; int seen = 0;
    for (int i = 0; i < shell_text_log_n; i++)
        if (!strcmp(shell_text_log[i].s, word)) { double r = word_ratio(R.s, &shell_text_log[i]); seen++; if (r < worst) worst = r; }
    char nm[120];
    snprintf(nm, sizeof nm, "%s, %d program(s) open, none in use: \"%s\" is 7:1 as painted (%.1f:1, drawn %d time(s))", look, nwins, word, worst, seen);
    ok(nm, seen > 0 && worst >= 7.0);
}

static const struct { int w, h; } SIZES[] = { { 1024, 600 }, { 1366, 768 }, { 1920, 1080 } };

static int inside_screen(rect r, int W, int H) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= W && r.y + r.h <= H; }

/* What is open, and what is on the desk. */
enum { S_WINS, S_MENU, S_FIND, S_HOME, S_LIST, S_PAGES, S_BUSY, S_MANY };
static const char *SCEN[] = { "three programs open", "the programs menu open", "the finder open",
                              "nothing open (Home)", "the list of programs open", "thirty programs, paged",
                              "five unpinned programs running beside the favourites", "eight windows open" };

static void press_what(rig *R, const char *prefix)     /* press the first published control whose name starts so */
{
    shell_target B[96];
    int nb = R->L->targets(&R->c, B, 96);
    for (int i = 0; i < nb; i++)
        if (!strncmp(B[i].what, prefix, strlen(prefix))) {
            R->L->click(&R->c, B[i].r.x + B[i].r.w / 2, B[i].r.y + B[i].r.h / 2);
            if (R->L->step) for (int j = 0; j < 80; j++) R->L->step(&R->c, 0.05f);
            return;
        }
}

/* Every kind-0 rect is 44 px on its short side and on the screen. */
static void floors(const char *look, int scen)
{
    for (size_t sz = 0; sz < sizeof SIZES / sizeof *SIZES; sz++)
        for (size_t k = 0; k < sizeof SCALES / sizeof *SCALES; k++) {
            static rig R;
            make(&R, look, SIZES[sz].w, SIZES[sz].h, SCALES[k], scen == S_PAGES ? 30 : 12,
                 (scen == S_HOME || scen == S_LIST || scen == S_MENU || scen == S_PAGES) ? 0 : scen == S_BUSY ? 9 : scen == S_MANY ? 8 : 3, 0);
            /* S_MENU: with nothing open the taskbar opens the programs menu by itself (no button to guess). */
            if (scen == S_FIND)  press_what(&R, "the All my programs cell");
            if (scen == S_LIST) {                                     /* rail: the second-last row of Home */
                shell_target B[96];
                int nb = R.L->targets(&R.c, B, 96), last = -1;
                for (int i = 0; i < nb; i++) if (!strcmp(B[i].what, "a row of Home")) last = i;
                if (last > 0) {
                    R.L->click(&R.c, B[last - 1].r.x + B[last - 1].r.w / 2, B[last - 1].r.y + B[last - 1].r.h / 2);
                    if (R.L->step) for (int j = 0; j < 80; j++) R.L->step(&R.c, 0.05f);
                }
            }
            shell_target T[96];
            int n = R.L->targets(&R.c, T, 96);
            char nm[200];
            int small = 0, off = 0, press = 0, wins = 0, woff = 0;
            for (int i = 0; i < n; i++) {
                if (T[i].kind == TARGET_WINDOW) {          /* a window is placed wholly on the screen (OBS-REAL-IMAGE) */
                    wins++;
                    if (!inside_screen(T[i].r, SIZES[sz].w, R.c.screen_h) && !woff++)
                        printf("      first: a window is at %d,%d %dx%d, off the %dx%d screen\n",
                               T[i].r.x, T[i].r.y, T[i].r.w, T[i].r.h, SIZES[sz].w, R.c.screen_h);
                    continue;
                }
                if (T[i].kind != TARGET_PRESS) continue;
                press++;
                int shorter = T[i].r.w < T[i].r.h ? T[i].r.w : T[i].r.h;
                if (shorter < 44 && !small++)
                    printf("      first: %s is %dx%d\n", T[i].what, T[i].r.w, T[i].r.h);
                if (!inside_screen(T[i].r, SIZES[sz].w, R.c.screen_h) && !off++)
                    printf("      first: %s is at %d,%d %dx%d, off the %dx%d screen\n", T[i].what,
                           T[i].r.x, T[i].r.y, T[i].r.w, T[i].r.h, SIZES[sz].w, R.c.screen_h);
            }
            snprintf(nm, sizeof nm, "%s %dx%d text %.0f%%, %s: %d controls, each 44 px+ and on the screen",
                     look, SIZES[sz].w, SIZES[sz].h, (double)(SCALES[k] * 100.f), SCEN[scen], press);
            ok(nm, press > 0 && !small && !off);
            if (wins) {
                snprintf(nm, sizeof nm, "%s %dx%d text %.0f%%, %s: %d window(s), each wholly on the screen",
                         look, SIZES[sz].w, SIZES[sz].h, (double)(SCALES[k] * 100.f), SCEN[scen], wins);
                ok(nm, !woff);
            }
            if (getenv("FLOORS_DUMP"))                          /* FLOORS_DUMP=1: every rectangle, for reading */
                for (int q = 0; q < n; q++)
                    printf("        %-36s %5d,%-5d %4dx%-4d %s\n", T[q].what, T[q].r.x, T[q].r.y, T[q].r.w, T[q].r.h,
                           T[q].kind == TARGET_WINDOW ? "(window)" : "");
        }
}

/* ── the words on a card that is waiting for its program ───────────────────────────────────────
 * "Still starting. This computer is taking a while." and "This is still not open. You can close this
 * and try again." are longer than anything that sat on that card before. At the smallest panel and the
 * largest words, in every look that draws the card: is the line drawn WHOLE (not cut to "..." by the
 * look, not run past the edge of the card or the screen)? */
static const char *WAITING[] = { "Starting\xE2\x80\xA6", "Still starting. This computer is taking a while.",
                                 "This is still not open. You can close this and try again.", "This did not start" };

static void waiting_card(const char *look)
{
    for (size_t sz = 0; sz < sizeof SIZES / sizeof *SIZES; sz++)
        for (size_t k = 0; k < sizeof SCALES / sizeof *SCALES; k++) {
            /* A kiosk with nine allowed apps at the biggest words on the smallest panel has no stage at all
             * (locked.c: the strip of apps takes the screen, and the app's name stands alone): there is no
             * card to hold any words, "Starting..." included. Said, not measured. */
            if (!strcmp(look, "locked") && SIZES[sz].w == 1024 && SCALES[k] >= 2.f) {
                printf("    locked 1024x600 text 200%%: no stage for any words on a kiosk of nine apps (not measured)\n");
                continue;
            }
            for (size_t w = 0; w < sizeof WAITING / sizeof *WAITING; w++) {
                static rig R; memset(&R, 0, sizeof R);
                shell_text_log_n = 0; shell_text_log_on = 1;
                waiting_sub = WAITING[w];
                make(&R, look, SIZES[sz].w, SIZES[sz].h, SCALES[k], strcmp(look, "locked") ? 12 : 9, 1, 0);   /* a kiosk: nine allowed apps is its worst (locked.c) */
                waiting_sub = NULL;
                shell_text_log_on = 0;
                shell_target T[96];
                int n = R.L->targets ? R.L->targets(&R.c, T, 96) : 0;
                rect card = { 0, 0, SIZES[sz].w, R.c.screen_h };           /* where it has to end: its card, else the screen */
                for (int i = 0; i < n; i++) if (T[i].kind == TARGET_WINDOW) { card = T[i].r; break; }
                if (getenv("WAIT_DUMP") && k == (size_t)(getenv("WK") ? atoi(getenv("WK")) : 1) && sz == (size_t)(getenv("WS") ? atoi(getenv("WS")) : 1)) printf("   [%s] nwins=%d focus=%d %d log entries, card %d,%d %dx%d\n", look, R.c.n_wins, R.c.focus, shell_text_log_n, card.x, card.y, card.w, card.h);
                if (getenv("WAIT_DUMP") && k == (size_t)(getenv("WK") ? atoi(getenv("WK")) : 1) && sz == (size_t)(getenv("WS") ? atoi(getenv("WS")) : 1))
                    for (int i = 0; i < shell_text_log_n; i++) printf("        [%s] %d,%d %dx%d \"%s\"\n", look, shell_text_log[i].x, shell_text_log[i].y, shell_text_log[i].w, shell_text_log[i].h, shell_text_log[i].s);
                /* A line that was wrapped is drawn as several pieces, in order: join them back. */
                const char *want = WAITING[w];
                size_t wl = strlen(want);
                int whole = 0, cut = 0, over = 0;
                for (int i = 0; i < shell_text_log_n; i++) {
                    size_t pos = 0; int pieces = 0, bad = 0;
                    for (int j = i; j < shell_text_log_n && pos < wl; j++) {
                        const shell_text_rec *t = &shell_text_log[j];
                        size_t l = strlen(t->s);
                        if (strncmp(want + pos, t->s, l)) break;
                        if (t->x + t->w > card.x + card.w || t->x + t->w > SIZES[sz].w || t->y + t->h > card.y + card.h) bad = 1;
                        pos += l; pieces++;
                        if (want[pos] == ' ') pos++;
                    }
                    if (pos >= wl) { whole++; over += bad; i += pieces - 1; }
                    else if (pieces == 0 && !strncmp(shell_text_log[i].s, want, wl < 12 ? wl : 12)) cut++;
                    else if (pieces && !strncmp(shell_text_log[i].s, want, 12 < wl ? 12 : wl)) cut++;
                }
                char nm[220];
                snprintf(nm, sizeof nm, "%s %dx%d text %.0f%%: \"%.28s%s\" is drawn whole, inside its card (%d whole, %d elided in a bar, %d past the edge)",
                         look, SIZES[sz].w, SIZES[sz].h, (double)(SCALES[k] * 100.f), WAITING[w], strlen(WAITING[w]) > 28 ? "..." : "",
                         whole, cut, over);
                ok(nm, whole > 0 && !over);                 /* an elided copy in a title bar or a taskbar button is the look's own */
            }
        }
}

int main(void)
{
    printf("the shell's floors: 18 px words, 44 px targets, windows on the screen\n");
    text_floor();
    printf("\ntargets: 44 px, on the screen, in each look\n");
    floors("taskbar", S_WINS); floors("taskbar", S_MENU); floors("taskbar", S_MANY);
    floors("dock", S_WINS);    floors("dock", S_MANY);    floors("dock", S_FIND);   floors("dock", S_HOME);   floors("dock", S_BUSY);
    floors("rail", S_WINS);    floors("rail", S_MANY);   floors("rail", S_HOME);   floors("rail", S_LIST);
    floors("tiles", S_WINS);   floors("tiles", S_MANY);   floors("tiles", S_HOME);  floors("tiles", S_PAGES);
    printf("\nwaiting card: the words fit, at 1024x600 and the biggest words, in every look\n");
    waiting_card("rail"); waiting_card("taskbar"); waiting_card("dock"); waiting_card("tiles"); waiting_card("workbench"); waiting_card("locked");
    /* The Light look, rendered the way contrast-test.sh renders it (aurora, from the repository root). */
    char dir[64], cmd[600];
    snprintf(dir, sizeof dir, "/tmp/floorstest.%d", (int)getpid());
    snprintf(cmd, sizeof cmd,
             "mkdir -p %s/st && echo sandstone > %s/st/theme && tail -n +2 themes/templates/shell.conf.tpl | "
             "AURORA_THEMES=themes AURORA_TEMPLATES=themes/templates AURORA_STATE=%s/st AURORA_CACHE=%s/c "
             "./src/aurora/aurora render > %s/light.conf", dir, dir, dir, dir, dir);
    if (system(cmd) != 0) { fprintf(stderr, "could not render the Light look\n"); return 2; }
    char conf[100]; snprintf(conf, sizeof conf, "%s/light.conf", dir);
    light_conf = conf;
    printf("\nquiet words: 7:1 as painted, on the Light look\n");
    quiet("taskbar", 0, "Nothing open yet.");
    quiet("taskbar", 1, "A page title");
    quiet("dock", 1, "A page title");
    light_conf = NULL;
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    printf("\n%d checks, %d failed\n", checked, fail);
    return fail ? 1 : 0;
}
