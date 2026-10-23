/* notifytest.c — when a program has something to tell her, does she see it?
 *
 * Nothing in this image implemented org.freedesktop.Notifications,
 * which is the one way a Linux application has of saying anything
 * outside its own window. Every such call came back ServiceUnknown and
 * the application shrugged: the USB stick said nothing when it was
 * safe to remove, the browser said nothing when a download finished,
 * the store said nothing when it was done.
 *
 * This drives the real thing over a REAL SESSION BUS, started here,
 * with a real client (gdbus) -- not by calling the handlers
 * directly, because half of what can go wrong is in the marshalling
 * and in what happens when a message is the wrong shape.
 *
 * And most of it is about not trusting the sender. Every string in a
 * notification comes from another program: it may be enormous, may be
 * twelve lines, may be full of control characters, may be the markup
 * the specification allows and this shell does not draw. A card is a
 * fixed shape on HER screen and no program gets to decide how much of
 * it to take.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

#include <dbus/dbus.h>

#include "../src/aurshell/notify.h"
#include "../src/aurshell/osd.h"
#include "../src/aurshell/foot.h"

/* The harness reads the cards the way the screen does: through the
 * geometry contract and the queue, not by counting. */
static int fail = 0, checked = 0;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-58s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

/* ── a bus of our own ───────────────────────────────────────────── */

static char bus_addr[512];
static pid_t bus_pid = -1;

static int start_bus(void)
{
    char cfg[] = "/tmp/auros-notifytest-XXXXXX";
    int d = mkstemp(cfg);
    if (d < 0) return -1;
    dprintf(d,
        "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
        " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
        "<busconfig><type>session</type>"
        "<listen>unix:tmpdir=/tmp</listen>"
        "<policy context=\"default\">"
        "<allow send_destination=\"*\" eavesdrop=\"true\"/>"
        "<allow eavesdrop=\"true\"/><allow own=\"*\"/>"
        "</policy></busconfig>\n");
    close(d);

    int p[2];
    if (pipe(p) < 0) { unlink(cfg); return -1; }
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); unlink(cfg); return -1; }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], 1);
        execlp("dbus-daemon", "dbus-daemon", "--config-file", cfg,
               "--print-address", "--nofork", (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    size_t n = 0;
    while (n + 1 < sizeof bus_addr) {
        ssize_t r = read(p[0], bus_addr + n, 1);
        if (r <= 0) break;
        if (bus_addr[n] == '\n') break;
        n++;
    }
    bus_addr[n] = 0;
    close(p[0]);
    unlink(cfg);
    if (!n) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); return -1; }
    bus_pid = pid;
    setenv("DBUS_SESSION_BUS_ADDRESS", bus_addr, 1);
    return 0;
}

static void stop_bus(void)
{
    if (bus_pid > 0) { kill(bus_pid, SIGTERM); waitpid(bus_pid, NULL, 0); }
}

/* A real client, over the real bus.
 *
 * Forked and NOT waited for here. dbus-send --print-reply blocks until
 * the server replies, and the server only replies when the shell pumps
 * -- so waiting for the client before pumping is a deadlock, and the
 * first version of this file wrote one. The caller pumps while the
 * client is in flight, which is what the real main loop does. */
static const char *hints = "{}";

static pid_t send_notify_async(const char *app, unsigned replaces,
                               const char *summary, const char *body,
                               int expire)
{
    /* gdbus, not dbus-send. dbus-send cannot build an a{sv} at all --
     * "Unknown type variant" -- so it cannot make a well-formed Notify
     * call, and a harness whose client is broken proves nothing about
     * the server. Found by running the two side by side against a
     * server that was working the whole time. */
    char rid[32], exp[32];
    snprintf(rid, sizeof rid, "%u", replaces);
    snprintf(exp, sizeof exp, "%d", expire);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        execlp("gdbus", "gdbus", "call", "--session",
               "--dest", "org.freedesktop.Notifications",
               "--object-path", "/org/freedesktop/Notifications",
               "--method", "org.freedesktop.Notifications.Notify",
               app, rid, "", summary, body, "[]", hints, exp, (char *)NULL);
        _exit(127);
    }
    return pid;
}

/* Pump the shell while the client talks to it, then collect it. */
static int send_notify(shell_ctx *c, const char *app, unsigned replaces,
                       const char *summary, const char *body, int expire)
{
    pid_t pid = send_notify_async(app, replaces, summary, body, expire);
    if (pid < 0) return -1;
    for (int i = 0; i < 500; i++) {
        notify_pump(c);
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        struct timespec t = { 0, 10 * 1000 * 1000 };
        nanosleep(&t, NULL);
    }
    kill(pid, SIGKILL); waitpid(pid, NULL, 0);
    return -1;                                   /* it never answered */
}

/* Pump until the count changes, or we give up. The client is another
 * process, so this is a real round trip. */
static int pump_until(shell_ctx *c, int want, int ms)
{
    for (int i = 0; i < ms / 10; i++) {
        notify_pump(c);
        if (notify_showing() == want) return 1;
        struct timespec t = { 0, 10 * 1000 * 1000 };
        nanosleep(&t, NULL);
    }
    return notify_showing() == want;
}

static const char *note_summary(int i) { return notify_card_summary(i); }
static const char *note_body(int i)    { return notify_card_body(i); }

static notify_geom geom_now(shell_ctx *c)
{
    notify_view v; notify_view_now(c, &v);
    notify_geom g; notify_layout(c->screen_w, c->screen_h + v.foot_h, &v, &g);
    return g;
}
static int press(shell_ctx *c, rect r)
{
    return r.w > 0 && notify_click(c, r.x + r.w / 2, r.y + r.h / 2);
}

/* Put the card away the way she would: OK. */
static void clear_all(shell_ctx *c)
{
    if (notify_list_open()) press(c, geom_now(c).close);
    if (notify_showing()) press(c, geom_now(c).ok);
    notify_pump(c);
}

static void wait_ms(int ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

static int overlaps(rect a, rect b)
{
    return a.w > 0 && b.w > 0 && a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}
static int within(rect in, rect out)
{
    return in.x >= out.x && in.y >= out.y &&
           in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}
static int shorter(rect r) { return r.w < r.h ? r.w : r.h; }

/* Joining the bus is a HANDSHAKE now, driven by the frame loop, so
 * that a socket which accepts and then says nothing cannot stop the
 * desktop. It therefore does not finish inside one call -- which is
 * the point -- and a harness has to pump for it the way the shell
 * does. */
static int wait_owned(shell_ctx *c, int ms)
{
    for (int i = 0; i < ms / 10; i++) {
        if (notify_open()) return 1;
        notify_pump(c);
        struct timespec t = { 0, 10 * 1000 * 1000 };
        nanosleep(&t, NULL);
    }
    return notify_open();
}

static void fresh(shell_ctx *c)
{
    memset(c, 0, sizeof *c);
    theme_t t = {0};
    shell_theme_load(c, &t);
    c->text_scale = 1.f;
    c->foot_hover = -1;
    c->screen_w = 1024;
    c->screen_h = 600 - foot_height(c);
    /* Where "Big and simple" puts its clock: the strip's right end. */
    c->clock_r = (rect){ 1024 - 220, 0, 220, 46 };
}

int main(void)
{
    printf("\nWhen a program has something to tell her, does she see it?\n\n");

    /* ── the geometry, which needs no bus at all ────────────────── */
    printf("  where the card and the list go, on every screen and text size\n");
    {
        static const struct { int w, h; } RES[] = {
            { 1024, 600 }, { 1366, 768 }, { 1920, 1080 },
        };
        static const float K[] = { 1.0f, 1.25f, 1.6f, 2.0f };
        int cards = 0, lists = 0, small = 0, off = 0, clock = 0, band = 0,
            loose = 0, osd = 0, piles = 0;
        for (size_t r = 0; r < sizeof RES / sizeof RES[0]; r++)
            for (size_t k = 0; k < sizeof K / sizeof K[0]; k++)
                for (int where = 0; where < 4; where++)
                    for (int unseen = 1; unseen <= 3; unseen += 2)
                        for (int lines = 2; lines <= 4; lines += 2) {
            shell_ctx c; fresh(&c);
            c.text_scale = K[k];
            int sw = RES[r].w, sh = RES[r].h, fh = foot_height(&c);
            int body = sh - fh, bar = (int)(44 * K[k]);
            notify_view v; notify_view_now(&c, &v);
            v.text_scale = K[k];
            /* two lines of words, or four: the most a card ever has */
            v.unseen = unseen; v.kept = unseen;
            v.text_h = (int)(lines * 28.f * K[k]);
            /* 0: clock in the top strip (rail, dock); 1: the same,
             * card at the bottom (tiles); 2: clock in the bar at the
             * bottom (taskbar); 3: no clock at all. */
            v.clock = where == 2 ? (rect){ sw - 160, body - bar, 160, bar }
                    : where == 3 ? (rect){ 0, 0, 0, 0 }
                                 : (rect){ sw - 220, 0, 220, 46 };
            v.bottom = where == 1 || where == 2;
            rect screen = { 0, 0, sw, body };
            notify_geom g;
            notify_layout(sw, sh, &v, &g);
            cards++;
            if (shorter(g.ok) < NOTIFY_TARGET) small++;
            if (unseen > 1 && shorter(g.more) < NOTIFY_TARGET) small++;
            if (unseen == 1 && g.more.w) piles++;
            if (!within(g.card, screen)) off++;
            if (g.card.y + g.card.h > body) band++;
            if (overlaps(g.card, v.clock)) clock++;
            if (where != 3 && where != 2 && g.card.y < v.clock.y + v.clock.h) clock++;
            if (!within(g.ok, g.card) || (g.more.w && !within(g.more, g.card)) ||
                overlaps(g.ok, g.more)) loose++;
            /* With the clock in the bar, the strip above the bar is the
             * line's; the card must not be sitting in it. */
            if (where == 2 && g.card.y + g.card.h > v.clock.y - v.clock.h) osd++;

            for (int kept = 0; kept <= NOTIFY_KEEP; kept += 4)
              for (int first = 0; first <= kept; first += (kept / 2 > 0 ? kept / 2 : 1)) {
                v.list_open = 1; v.unseen = 0; v.kept = kept; v.first = first;
                notify_layout(sw, sh, &v, &g);
                lists++;
                /* Every message is reachable: pages cover [0, kept). */
                if (kept > 0 && g.first + g.rows < kept && !g.older.w) piles++;
                if (g.first > 0 && !g.newer.w) piles++;
                if (g.older.w && (shorter(g.older) < NOTIFY_TARGET || !within(g.older, g.list) ||
                    overlaps(g.older, g.close) || overlaps(g.older, g.newer))) loose++;
                if (g.newer.w && (shorter(g.newer) < NOTIFY_TARGET || !within(g.newer, g.list) ||
                    overlaps(g.newer, g.close))) loose++;
                if (g.card.w) piles++;               /* no card over the list */
                if (!within(g.list, screen) || g.list.y + g.list.h > body) off++;
                if (overlaps(g.list, v.clock)) clock++;
                if (shorter(g.close) < NOTIFY_TARGET) small++;
                if (!within(g.close, g.list)) loose++;
                if (kept > 0 && g.rows < 1) loose++;
                for (int i = 0; i < g.rows; i++) {
                    if (g.row[i].h < NOTIFY_TARGET) small++;
                    if (!within(g.row[i], g.list) || overlaps(g.row[i], g.close)) loose++;
                }
                v.list_open = 0;
            }
        }
        char w[96];
        snprintf(w, sizeof w, "%d cards and %d lists measured: every button %dpx+",
                 cards, lists, NOTIFY_TARGET);
        ok(w, cards > 0 && small == 0);
        ok("one card, never a pile: no second card, no list behind it", piles == 0);
        ok("on the screen, and never over the band", off == 0 && band == 0);
        ok("never over the clock or the strip it is in", clock == 0);
        ok("every button is inside what it belongs to", loose == 0);
        ok("with the clock in the bar, the line's strip is kept clear", osd == 0);
    }

    /* ── the real thing, over a real bus ────────────────────────── */
    printf("\n  a real program, on a real bus\n");
    if (start_bus() < 0) {
        printf("    (no dbus-daemon here to test against)              SKIP\n");
        goto done;
    }
    shell_ctx c; fresh(&c);
    ok("the shell claims org.freedesktop.Notifications",
       wait_owned(&c, 5000) == 1);
    ok("...and offers a file descriptor to wait on", notify_fd() >= 0);
    ok("nothing is showing before anything is sent", notify_showing() == 0);

    ok("a program's message is accepted",
       send_notify(&c, "Your files", 0, "The USB stick can be taken out",
                   "Everything has been written to it.", 8000) == 0);
    ok("...and appears on her screen", pump_until(&c, 1, 3000));

    /* Replacing: a download's percentage must not stack up four deep. */
    send_notify(&c, "Firefox", notify_card_id(0), "Downloading  50%", "report.pdf", 8000);
    pump_until(&c, 1, 1000);
    ok("the same message saying something new replaces itself",
       notify_showing() == 1 && notify_kept() == 1 &&
       !strcmp(notify_card_summary(0), "Downloading 50%"));

    /* ── NOTHING PILES UP ───────────────────────────────────────── */
    for (int i = 0; i < 5; i++) {
        char sm[64]; snprintf(sm, sizeof sm, "Message %d", i);
        send_notify(&c, "A program", 0, sm, "", 8000);
    }
    pump_until(&c, 6, 600);
    {
        notify_geom g = geom_now(&c);
        ok("six at once are ONE card", notify_showing() == 6 && g.card.w > 0);
        ok("...showing the newest", !strcmp(notify_card_summary(0), "Message 4"));
        ok("...with a \"6 messages\" button", g.more.w > 0);
        int before = notify_kept();
        ok("pressing OK puts the card away", press(&c, g.ok) && notify_showing() == 0 &&
                                             geom_now(&c).card.w == 0);
        ok("...and keeps every word for Messages", notify_kept() == before &&
                                                   !strcmp(notify_card_summary(0), "Message 4"));
        ok("pressing where there is no card does nothing",
           notify_click(&c, 2, c.screen_h - 2) == 0);
    }

    /* ── NOTHING VANISHES ───────────────────────────────────────────
     * It used to go after eight seconds, four at the least, and a
     * program could ask for one millisecond. */
    printf("\n  nothing goes away by itself\n");
    {
        hints = "{'urgency': <byte 2>}";
        send_notify(&c, "A program", 0, "Read me slowly", "", 1);
        hints = "{}";
        send_notify(&c, "A program", 0, "And me", "", 1);
        for (int i = 0; i < 450; i++) { notify_pump(&c); wait_ms(10); }
        notify_geom g = geom_now(&c);
        ok("a card asked to last 1 ms is still there 4.5 s later",
           notify_showing() == 2 && g.card.w > 0 && g.more.w > 0);

        osd_say("Picture saved in your Pictures folder.");
        wait_ms(3500);
        ok("an on-screen message is still there 3.5 s later", osd_visible());
        osd_press();
        ok("...and goes at her next press", !osd_visible());
        osd_show(OSD_VOLUME, 45);
        wait_ms(1600);
        ok("the sound line is still there 1.6 s later", osd_visible());
        osd_press();
        clear_all(&c);
    }

    /* ── THE CLOCK OPENS MESSAGES ─────────────────────────────────── */
    printf("\n  Messages, from the clock\n");
    {
        send_notify(&c, "Firefox", 0, "Download finished", "report.pdf", 8000);
        pump_until(&c, 1, 2000);
        int kept = notify_kept();
        rect k = c.clock_r;
        ok("pressing the clock is taken", press(&c, k));
        ok("...and opens Messages", notify_list_open());
        ok("...which is seeing them: the card goes", notify_showing() == 0 &&
                                                     geom_now(&c).card.w == 0);
        notify_geom g = geom_now(&c);
        ok("...newest first", g.rows > 0 && g.first == 0 &&
           !strcmp(notify_card_summary(0), "Download finished"));
        /* Nothing vanishes: what does not fit on a page is one press
         * (Older) away, and the page it was on is one press back. */
        int seen_rows = 0, pages = 0;
        while (pages < NOTIFY_KEEP + 2) {
            notify_geom p = geom_now(&c);
            seen_rows += p.rows;
            pages++;
            if (!p.older.w) break;
            press(&c, p.older);
        }
        ok("every message is reachable by Older, over more than one page",
           seen_rows == kept && pages > 1);
        while (geom_now(&c).newer.w && pages-- > 0) press(&c, geom_now(&c).newer);
        ok("...and Newer comes back to the newest", notify_list_first() == 0 &&
           !geom_now(&c).newer.w);
        g = geom_now(&c);
        ok("a row that is not a program does nothing but stay",
           press(&c, g.row[0]) && notify_list_open());
        ok("Close Messages closes it", press(&c, g.close) && !notify_list_open());
        press(&c, k);
        ok("a press anywhere else closes it too, and is not passed on",
           notify_click(&c, 2, c.screen_h - 2) == 1 && !notify_list_open());
        ok("a second press on the clock closes it",
           press(&c, k) && notify_list_open() && press(&c, k) && !notify_list_open());
    }

    /* ── REPLACE IN PLACE ─────────────────────────────────────────── */
    printf("\n  the volume and a download change the card they have\n");
    {
        clear_all(&c);
        hints = "{'x-canonical-private-synchronous': <'volume'>}";
        send_notify(&c, "Volume", 0, "Volume 40%", "", 8000);
        pump_until(&c, 1, 2000);
        int kept = notify_kept();
        send_notify(&c, "Volume", 0, "Volume 45%", "", 8000);
        pump_until(&c, 1, 500);
        ok("a tagged notice replaces its own card", notify_showing() == 1 &&
           notify_kept() == kept && !strcmp(notify_card_summary(0), "Volume 45%"));
        hints = "{'x-dunst-stack-tag': <'volume'>}";
        send_notify(&c, "Another program", 0, "Volume 10%", "", 8000);
        pump_until(&c, 2, 1000);
        ok("...but not another program's with the same tag", notify_kept() == kept + 1);
        hints = "{}";

        clear_all(&c);
        unsigned id = 0;
        for (int i = 0; i < notify_kept(); i++)
            if (!strcmp(notify_card_summary(i), "Volume 45%")) id = notify_card_id(i);
        send_notify(&c, "Volume", id, "Volume 50%", "", 8000);
        pump_until(&c, 1, 2000);
        ok("one she has put away comes back, changed, by its id",
           notify_showing() == 1 && !strcmp(notify_card_summary(0), "Volume 50%") &&
           notify_card_id(0) == id);
        clear_all(&c);
    }

    /* ── "VLC IS READY" STILL OPENS VLC ───────────────────────────── */
    {
        notify_local_open("VLC media player is ready", "Press here to open it.",
                          "org.videolan.VLC");
        notify_geom g = geom_now(&c);
        char id[32] = "";
        rect body = { g.card.x + 4, g.card.y + 4, 40, 20 };
        ok("pressing the ready card opens the program",
           press(&c, body) && notify_take_open(id, sizeof id) &&
           !strcmp(id, "org.videolan.VLC") && notify_showing() == 0);
        notify_local_open("VLC media player is ready", "Press here to open it.",
                          "org.videolan.VLC");
        ok("...and OK only puts it away",
           press(&c, geom_now(&c).ok) && !notify_take_open(id, sizeof id));
    }

    /* ── what a program is not allowed to do to her screen ──────── */
    printf("\n  and what a program is NOT allowed to do to her screen\n");
    clear_all(&c);

    {
        char huge[2000];
        memset(huge, 'x', sizeof huge - 1); huge[sizeof huge - 1] = 0;
        ok("a two-thousand-character summary is accepted without crashing",
           send_notify(&c, "A program", 0, huge, huge, 8000) == 0);
        ok("...and becomes one card", pump_until(&c, 1, 3000));
        ok("...cut to what a card can hold",
           (int)strlen(note_summary(0)) < NOTIFY_SUMMARY &&
           (int)strlen(note_body(0)) < NOTIFY_BODY);
    }

    /* ── WHAT ACTUALLY ENDS UP ON THE CARD ──────────────────────
     *
     * Counting cards proves nothing about the text in them, and the
     * text is the whole notification. Every case below is a string a
     * real program sends. */
    clear_all(&c);
    {
        static const struct { const char *sent, *want, *why; } T[] = {
        { "Saved <report 2024>.pdf", "Saved <report 2024>.pdf",
          "angle brackets in a filename are hers, not markup" },
        { "Happy birthday <3", "Happy birthday <3",
          "an unclosed < does not eat the rest of the line" },
        { "Download <b>finished</b>", "Download finished",
          "real markup is removed, and only the markup" },
        { "A <a href=\"http://x\">link</a> here", "A link here",
          "a link becomes its words" },
        { "Two\nlines\there", "Two lines here",
          "newlines and tabs become one space" },
        { "Lots      of      space", "Lots of space",
          "padding cannot push the words off the end" },
        { "bell\x07 and null-ish\x01", "bell and null-ish",
          "control characters are not drawn as boxes" },
        };
        for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
            clear_all(&c);
            send_notify(&c, "A program", 0, T[k].sent, "", 8000);
            pump_until(&c, 1, 2000);
            char w[128];
            snprintf(w, sizeof w, "%s", T[k].why);
            if (strcmp(note_summary(0), T[k].want) != 0) {
                printf("    %-58s FAIL\n", w);
                printf("      sent: %s\n      want: %s\n      got : %s\n",
                       T[k].sent, T[k].want, note_summary(0));
                fail++; checked++;
            } else ok(w, 1);
        }
        /* A word in a language that is not English, cut short. It must
         * not be cut in the middle of a character: the font engine
         * would draw the halves as empty boxes. */
        clear_all(&c);
        char wide[1200];
        size_t o = 0;
        while (o + 3 < sizeof wide - 1) { memcpy(wide + o, "\xe6\x97\xa5", 3); o += 3; }
        wide[o] = 0;
        send_notify(&c, "A program", 0, wide, "", 8000);
        pump_until(&c, 1, 2000);
        const char *got = note_summary(0);
        int whole = 1;
        for (size_t i = 0; got[i]; ) {
            unsigned char ch = (unsigned char)got[i];
            size_t len = (ch >= 0xF0) ? 4 : (ch >= 0xE0) ? 3 : (ch >= 0xC0) ? 2 : 1;
            for (size_t j = 1; j < len; j++)
                if (!got[i + j]) { whole = 0; break; }
            i += len;
        }
        ok("a long line in her language is never cut through a letter",
           whole && got[0]);
    }
    /* ── MALFORMED CALLS, SENT WITH libdbus ─────────────────────
     *
     * Not with gdbus. gdbus introspects the server first, learns the
     * signature, and refuses to send a badly-typed call AT ALL -- so
     * the check that used to be here never reached the server's type
     * checks and would have passed identically if the server had
     * none. It was proving that gdbus validates its own arguments.
     *
     * A hostile or broken program does not introspect first. These
     * are built by hand and put on the wire. */
    {
        DBusError e; dbus_error_init(&e);
        DBusConnection *raw = dbus_connection_open_private(bus_addr, &e);
        ok("(a raw client can be opened)", raw != NULL);
        if (raw) {
            dbus_bus_register(raw, &e);
            struct { const char *what; int nargs; } BAD[] = {
                { "no arguments at all",        0 },
                { "the wrong type in place 2",  1 },
                { "a truncated argument list",  2 },
            };
            for (size_t k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
                DBusMessage *m = dbus_message_new_method_call(
                    "org.freedesktop.Notifications",
                    "/org/freedesktop/Notifications",
                    "org.freedesktop.Notifications", "Notify");
                const char *sv = "app";
                if (BAD[k].nargs >= 1)
                    dbus_message_append_args(m, DBUS_TYPE_STRING, &sv,
                                             DBUS_TYPE_INVALID);
                if (BAD[k].nargs >= 2) {
                    const char *nope = "this-should-be-a-number";
                    dbus_message_append_args(m, DBUS_TYPE_STRING, &nope,
                                             DBUS_TYPE_INVALID);
                }
                DBusPendingCall *pc = NULL;
                dbus_connection_send_with_reply(raw, m, &pc, 5000);
                dbus_connection_flush(raw);
                dbus_message_unref(m);

                int had = notify_showing();
                int got = 0;
                for (int i = 0; i < 400 && pc; i++) {
                    notify_pump(&c);
                    /* dispatch, not just read_write: a pending call is
                     * only marked complete when its reply is
                     * dispatched. */
                    dbus_connection_read_write_dispatch(raw, 0);
                    if (dbus_pending_call_get_completed(pc)) { got = 1; break; }
                    struct timespec t = { 0, 10 * 1000 * 1000 };
                    nanosleep(&t, NULL);
                }
                DBusMessage *r = (pc && got) ? dbus_pending_call_steal_reply(pc)
                                             : NULL;
                char w[110];
                snprintf(w, sizeof w, "%s is ANSWERED, not left hanging",
                         BAD[k].what);
                ok(w, r != NULL);
                snprintf(w, sizeof w, "...with an error, and no card appears");
                ok(w, r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR
                        && notify_showing() == had);
                if (r) dbus_message_unref(r);
                if (pc) dbus_pending_call_unref(pc);
            }
            dbus_connection_close(raw);
            dbus_connection_unref(raw);
        }
        dbus_error_free(&e);
    }

    /* The server must still be alive and serving after all of that. */
    ok("the shell is still serving after a malformed call",
       notify_fd() >= 0);
    ok("...and still accepts a good one",
       send_notify(&c, "Your files", 0, "Still here", "", 8000) == 0);

    /* ── THE SHELL MOVES BUS ────────────────────────────────────
     *
     * It starts on the private bus its unit makes and adopts logind's
     * a second or two later. A server left on the old bus is a server
     * nothing can find, and the move can be triggered from more than
     * one place -- the frame loop, or a spawn -- so notify_open() has
     * to notice for itself rather than trusting whoever moved to say
     * so. */
    printf("\n  and when the shell moves to logind's bus\n");
    {
        char first[512];
        snprintf(first, sizeof first, "%s", bus_addr);
        pid_t old = bus_pid;
        bus_pid = -1;
        ok("(a second bus can be started)", start_bus() == 0);
        ok("...and it is a different one", strcmp(first, bus_addr) != 0);

        /* start_bus() has already pointed the environment at it. */
        ok("the shell notices and re-announces itself there",
           wait_owned(&c, 5000) == 1);
        int had = notify_showing();
        ok("...and a program on the NEW bus can reach it",
           send_notify(&c, "Your files", 0, "On the new bus", "", 8000) == 0);
        ok("...and the message arrives", notify_showing() == had + 1);

        if (old > 0) { kill(old, SIGTERM); waitpid(old, NULL, 0); }
    }

    /* ── WHAT A SENDING PROGRAM MAY AND MAY NOT DECIDE ──────────
     *
     * A card that never goes away is the one thing a program must not
     * be able to put on her screen, and the flag that made it never
     * go away used to be read straight out of the sender's hints. */
    printf("\n  what a sending program may and may not decide\n");
    clear_all(&c);
    {
        hints = "{'urgency': <byte 2>}";
        send_notify(&c, "A program", 0, "I am very important", "", 1);
        pump_until(&c, 1, 2000);
        ok("a program may say its message is urgent", notify_showing() == 1);
        ok("...but it is not the computer's own warning", !notify_card_sticky(0));
        hints = "{}";
    }
    {
        clear_all(&c);
        notify_local("The battery is very low", "Please plug this computer in.");
        ok("the shell can say something that is its own", notify_showing() == 1 &&
                                                          notify_card_sticky(0));
        /* And a flood of programs talking must not push it out. */
        for (int i = 0; i < NOTIFY_KEEP + 3; i++) {
            char sm[48]; snprintf(sm, sizeof sm, "Download %d finished", i);
            send_notify(&c, "Firefox", 0, sm, "", 8000);
        }
        int survived = 0;
        for (int i = 0; i < notify_kept(); i++)
            if (notify_card_sticky(i)) survived = 1;
        ok("downloads by the dozen do not push out the battery warning",
           survived);
        ok("...and Messages keeps no more than it holds",
           notify_kept() <= NOTIFY_KEEP);
        clear_all(&c);
    }
    {
        /* A replaces_id naming nothing must not take an id that is
         * already somebody's. */
        /* Learn what the server is about to hand out, then ask to
         * replace THAT -- an id that does not exist yet. With the
         * defect the impostor is given it verbatim and the counter is
         * not advanced, so the very next message mints the same id
         * and two live cards share it. */
        clear_all(&c);
        send_notify(&c, "First", 0, "one", "", 8000);
        pump_until(&c, 1, 2000);
        unsigned next = notify_card_id(0) + 1;
        send_notify(&c, "Impostor", next, "I claim the next id", "", 8000);
        pump_until(&c, 2, 2000);
        send_notify(&c, "Third", 0, "three", "", 8000);
        pump_until(&c, 3, 2000);
        ok("a message replacing nothing becomes a new one",
           notify_showing() == 3 &&
           strcmp(notify_card_summary(0), notify_card_summary(1)) != 0);
        /* And it must not be given an id that is already somebody's.
         * A twin means CloseNotification closes the wrong card and
         * the real owner can never replace or close its own -- one
         * program quietly taking over another's notification. */
        int twins = 0;
        for (int i = 0; i < notify_kept(); i++)
            for (int j = 0; j < i; j++)
                if (notify_card_id(i) == notify_card_id(j)) twins++;
        ok("...with an id that is not already somebody else's", twins == 0);
        clear_all(&c);
    }
    printf("\n  the rest of what a program may ask\n");
    {
        clear_all(&c);
        send_notify(&c, "A program", 0, "Close me", "", 8000);
        pump_until(&c, 1, 2000);
        /* CloseNotification, by the id the server handed back. The id
         * is 1-based and monotonic, so the live one is findable by
         * closing each in turn until the card goes. */
        int closed = 0;
        for (unsigned id = 1; id < 64 && !closed; id++) {
            pid_t pid = fork();
            if (pid == 0) {
                int nul = open("/dev/null", O_WRONLY);
                if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
                char a[32]; snprintf(a, sizeof a, "%u", id);
                execlp("gdbus", "gdbus", "call", "--session",
                       "--dest", "org.freedesktop.Notifications",
                       "--object-path", "/org/freedesktop/Notifications",
                       "--method", "org.freedesktop.Notifications.CloseNotification",
                       a, (char *)NULL);
                _exit(127);
            }
            for (int i = 0; i < 200; i++) {
                notify_pump(&c);
                if (waitpid(pid, NULL, WNOHANG) == pid) break;
                struct timespec t = { 0, 10 * 1000 * 1000 }; nanosleep(&t, NULL);
            }
            if (!notify_showing()) closed = 1;
        }
        ok("a program can take its own message back off the screen", closed);
        ok("...and out of Messages", strcmp(notify_card_summary(0), "Close me") != 0);
    }
    {
        /* GetCapabilities must claim only what is true: "actions"
         * would put buttons in every notification that this shell
         * then would not draw. */
        char out[512] = {0};
        FILE *f = popen("gdbus call --session "
                        "--dest org.freedesktop.Notifications "
                        "--object-path /org/freedesktop/Notifications "
                        "--method org.freedesktop.Notifications.GetCapabilities "
                        "2>/dev/null", "r");
        pid_t pump = fork();
        if (pump == 0) { _exit(0); }
        for (int i = 0; i < 50; i++) { notify_pump(&c);
            struct timespec t = { 0, 10 * 1000 * 1000 }; nanosleep(&t, NULL); }
        if (f) { if (!fgets(out, sizeof out, f)) out[0] = 0; pclose(f); }
        if (pump > 0) waitpid(pump, NULL, 0);
        ok("it says it can show a body", strstr(out, "body") != NULL);
        ok("...and does NOT claim buttons it will not draw",
           strstr(out, "actions") == NULL);
    }

    notify_fini();
    ok("it gives the name up when it is done", notify_fd() < 0);
    stop_bus();

done:
    /* ── THE HOST'S HALF ──────────────────────────────────────────
     * The press that puts the on-screen line away must still reach
     * what she pressed, so main.c calls osd_press() on the way in and
     * never lets it decide `taken`. The touchpad's tap goes through
     * the card like a click does (it used to skip it, and a tap on OK
     * would have pressed whatever was under the card). Read from the
     * source, because main.c needs a screen to run. */
    printf("\n  the desktop's own wiring (src/aurshell/main.c)\n");
    {
        FILE *f = fopen("src/aurshell/main.c", "r");
        char line[512];
        int presses = 0, taking = 0, cards = 0, timer = 0;
        while (f && fgets(line, sizeof line, f)) {
            if (strstr(line, "osd_press()")) {
                presses++;
                if (strstr(line, "taken")) taking++;
            }
            if (strstr(line, "notify_click(&c")) cards++;
            if (strstr(line, "notify_step") || strstr(line, "osd_visible() ||")) timer++;
        }
        if (f) fclose(f);
        ok("every press puts the line away: click, tap and key", presses >= 3);
        ok("...and none of them is taken by it", presses && !taking);
        ok("a tap on the card is the card's, like a click", cards >= 2);
        ok("nothing is retired or repainted on a clock", f && !timer);
    }

    printf("\n");
    if (fail) {
        printf("%d of %d wrong. A program tried to tell her something and\n",
               fail, checked);
        printf("she did not find out.\n");
        return 1;
    }
    printf("%d checks: programs can say things, she can get rid of them,\n", checked);
    printf("and none of them can take over her screen.\n");
    return 0;
}
