/* watchtest.c — the room left on the computer, said once and calmly.
 *
 * She gets about 28 GB. The first she would know of it filling up is a program that will not
 * save. watch.c now puts one card up when nine tenths is used, in her words, and pressing it
 * opens a page of the help (foot.c, "help:computer-full") that says what to do. What matters:
 *
 *   - it is said at the line and not before, and once (not every minute);
 *   - it is said again only after the room has been made (below 85%) and filled again;
 *   - a computer that cannot say how full it is says nothing;
 *   - the words are hers (no sizes, no "disk"), and the page it opens is on the computer;
 *   - the page opens from its name, and only from a name that is letters and hyphens.
 *
 *   cc -O2 -std=gnu11 -o /tmp/watchtest tools/watchtest.c src/aurshell/watch.c \
 *      src/aurshell/notify.c src/aurshell/osd.c src/aurshell/power.c src/aurshell/run.c \
 *      src/aurshell/foot.c src/aurshell/draw.c src/aurshell/shellcommon.c src/aurshell/anim.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      -DFOOT_HELP_PAGES='"rootfs/usr/share/auros/help/index.html"' \
 *      -I src/aurshell -I src/common -lm $(pkg-config --cflags --libs dbus-1) && /tmp/watchtest
 *
 * Run from the repository root.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/aurshell/shell.h"
#include "../src/aurshell/watch.h"
#include "../src/aurshell/notify.h"
#include "../src/aurshell/foot.h"

static int fails, checks;
static void ok(const char *what, int cond)
{
    checks++;
    if (!cond) fails++;
    printf("  %-72s %s\n", what, cond ? "ok" : "FAIL");
}

static char spawned[600];
static int spawn_n;
static int spawn_hook(shell_ctx *c, const char *blob, int n)
{
    (void)c;
    spawn_n = n;
    /* the last of the NUL-separated words: what the browser is told to open */
    const char *p = blob;
    for (int i = 0; i < n - 1; i++) p += strlen(p) + 1;
    snprintf(spawned, sizeof spawned, "%s", p);
    return 0;
}

int main(void)
{
    shell_ctx c;
    memset(&c, 0, sizeof c);

    printf("how full\n");
    int here = watch_disk_used("/");
    ok("a real place is a percent between 0 and 100", here >= 0 && here <= 100);
    ok("a place that is not there is not a guess", watch_disk_used("/no/such/place/at/all") == -1);

    printf("\nsaid at the line, once, and again only after room was made\n");
    ok("nothing is kept to begin with", notify_kept() == 0);
    ok("half full says nothing", !watch_disk_check(&c, 50) && notify_kept() == 0);
    ok("just under the line says nothing", !watch_disk_check(&c, WATCH_DISK_FULL - 1) && notify_kept() == 0);
    ok("a computer that cannot tell says nothing", !watch_disk_check(&c, -1) && notify_kept() == 0);
    ok("at the line it says so, in one card", watch_disk_check(&c, WATCH_DISK_FULL) == 1 && notify_kept() == 1);
    {
        /* Pressing the card (not its OK) asks the host for the page: the host opens it. */
        c.screen_w = 1366; c.screen_h = 700; c.text_scale = 1.f;
        notify_view v; notify_view_now(&c, &v);
        notify_geom g; notify_layout(c.screen_w, c.screen_h + v.foot_h, &v, &g);
        char id[32] = "";
        ok("pressing the card asks for the help page, by name",
           g.card.w > 0 && notify_click(&c, g.card.x + 6, g.card.y + 6) &&
           notify_take_open(id, sizeof id) && !strcmp(id, "help:" WATCH_DISK_HELP));
    }
    ok("...and the minute after, fuller, it does not say it again",
       !watch_disk_check(&c, 95) && !watch_disk_check(&c, WATCH_DISK_FULL) && notify_kept() == 1);
    ok("...nor after a little room is made but not much",
       !watch_disk_check(&c, WATCH_DISK_REARM) && !watch_disk_check(&c, WATCH_DISK_FULL) && notify_kept() == 1);
    ok("...but after plenty is made and it fills up again, it does",
       !watch_disk_check(&c, WATCH_DISK_REARM - 5) && watch_disk_check(&c, 93) == 1 && notify_kept() == 2);

    printf("\nwhat it says\n");
    const char *sum = notify_card_summary(0), *body = notify_card_body(0);
    ok("it says the computer is getting full", strstr(sum, "getting full") != NULL);
    ok("...and where to press", strstr(body, "Press here") != NULL);
    static const char *const NOT[] = { "disk", "Disk", "storage", "partition", "GB", "MB", "drive", "%", "error", NULL };
    int clean = 1;
    for (int i = 0; NOT[i]; i++) if (strstr(sum, NOT[i]) || strstr(body, NOT[i])) clean = 0;
    ok("...in her words: no sizes, no disk, no percent, no alarm", clean);
    ok("it is calm: not the kind of warning that is never put away",
       !notify_card_sticky(0));
    char page[200];
    snprintf(page, sizeof page, "rootfs/usr/share/auros/help/%s.html", WATCH_DISK_HELP);
    ok("the page it opens is on the computer", access(page, R_OK) == 0);

    printf("\nopening the page\n");
    c.spawn = spawn_hook;
    foot_open_help_page(&c, WATCH_DISK_HELP);
    ok("a browser is started with the page, in a new window of its own", spawn_n == 4 &&
       !strncmp(spawned, "file://", 7) &&
       strlen(spawned) > strlen("/computer-full.html") &&
       !strcmp(spawned + strlen(spawned) - strlen("/computer-full.html"), "/computer-full.html"));
    spawn_n = 0; spawned[0] = 0;
    foot_open_help_page(&c, "../../etc/passwd");
    foot_open_help_page(&c, "Computer-Full");
    foot_open_help_page(&c, "a b");
    foot_open_help_page(&c, "");
    ok("a name that is not letters and hyphens opens nothing", spawn_n == 0);

    printf("\nwatchtest: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
