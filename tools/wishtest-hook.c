/* wishtest-hook.c — the typed-word matcher (src/aurwl/wishhook.c) under test.
 *
 * The matcher is the one piece of Wishes that sees what she types, so the
 * tests are about what it must NOT do as much as what it must: report a
 * registered word typed whole, and nothing else, ever -- and keep nothing.
 * `aurwl_key_utf8` is replaced by a US-QWERTY table so keys can be "typed".
 *
 *   cc -std=gnu11 -I src/aurwl tools/wishtest-hook.c src/aurwl/wishhook.c -o /tmp/wh && /tmp/wh
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "wishhook.h"
#include "textinput.h"

static int fails, passes, utf8_calls;
static const char *cur;
#define CHECK(c) do { if (c) passes++; else { fails++; \
    fprintf(stderr, "FAIL %s:%d %s: %s\n", __FILE__, __LINE__, cur, #c); } } while (0)

/* What the compositor knows about the focused program, set by each test. */
static int ti_now = TI_PLAIN;
static const char *app_now = "org.gnome.gedit";
int textinput_state(void) { return ti_now; }
aurwl_win *aurwl_focus(const aurwl *c) { (void)c; return (aurwl_win *)&ti_now; }
const char *aurwl_win_app_id(const aurwl_win *w) { (void)w; return app_now; }
static const char *title_now = "Untitled";
static int x11_now;
int aurwl_win_is_x11(const aurwl_win *w) { (void)w; return x11_now; }
const char *aurwl_win_title(const aurwl_win *w) { (void)w; return title_now; }

/* char -> (evdev code, shift) for the keys these tests press */
static const struct { char ch; int code, shift; } MAP[] = {
    {'q',16,0},{'w',17,0},{'e',18,0},{'r',19,0},{'t',20,0},{'y',21,0},{'u',22,0},{'i',23,0},{'o',24,0},{'p',25,0},
    {'a',30,0},{'s',31,0},{'d',32,0},{'f',33,0},{'g',34,0},{'h',35,0},{'j',36,0},{'k',37,0},{'l',38,0},
    {'z',44,0},{'x',45,0},{'c',46,0},{'v',47,0},{'b',48,0},{'n',49,0},{'m',50,0},
    {'1',2,0},{'2',3,0},{'3',4,0},{'0',11,0},{' ',57,0},{',',51,0},{'.',52,0},{'\'',40,0},{'-',12,0},
    {'!',2,1},{'?',53,1},{'(',10,1},{')',11,1},{'I',23,1},{';',39,0},{'/',53,0},{'_',12,1},
    {'Q',16,1},{'P',25,1},{'O',24,1},{'T',20,1},{'A',30,1},{'\0',0,0} };
static int shift_down;
static int stub_code_to_char(int code, int shift, char *out)
{
    for (int i = 0; MAP[i].ch; i++)
        if (MAP[i].code == code && MAP[i].shift == shift) { out[0] = MAP[i].ch; out[1] = 0; return 1; }
    if (code >= 59 && code <= 68) return 0;                 /* function keys: no text */
    return 0;
}
int aurwl_key_utf8(const aurwl *c, uint32_t code, char *out, size_t n)
{
    (void)c; (void)n;
    utf8_calls++;
    out[0] = 0;
    if (code == 200) { strcpy(out, "\xC3\xA9"); return 2; }  /* an accented e */
    return stub_code_to_char((int)code, shift_down, out);
}

enum { ENTER = 28, TAB = 15, BKSP = 14, LEFT = 105, HOME = 102, LCTRL = 29, ESC = 1, LSHIFT = 42 };

static void key(int code) { wishhook_key(NULL, (uint32_t)code, 1); wishhook_key(NULL, (uint32_t)code, 0); }
static void type(const char *s)
{
    for (; *s; s++) {
        for (int i = 0; MAP[i].ch; i++)
            if (MAP[i].ch == *s) {
                if (MAP[i].shift) wishhook_key(NULL, LSHIFT, 1);
                shift_down = MAP[i].shift;
                key(MAP[i].code);
                shift_down = 0;
                if (MAP[i].shift) wishhook_key(NULL, LSHIFT, 0);
                break;
            }
    }
}

static int got(const char *w)
{
    char o[33];
    int n = 0;
    while (wishhook_take_word(o)) n += !strcmp(o, w) || !w[0];
    return n;
}
static int any_word(void) { char o[33]; return wishhook_take_word(o); }

static void setup(void)
{
    ti_now = TI_PLAIN;
    x11_now = 0;
    app_now = "org.gnome.gedit";
    title_now = "Untitled";
    const char *w[] = { "potato", "carrot" };
    const char *a[] = { "firefox", "org.videolan.VLC" };
    wishhook_set(w, 2, a, 2);
    wishhook_reset();
    while (any_word()) {}
}

static void test_potato_whole_word(void)
{
    cur = __func__;
    setup();
    type("potato ");
    CHECK(got("potato") == 1);
    type("potato.");           CHECK(got("potato") == 1);
    type("potato");  key(ENTER); CHECK(got("potato") == 1);
    type("potato");  key(TAB);   CHECK(got("potato") == 1);
    type("(potato)");          CHECK(got("potato") == 1);
    type("I like potato, ok"); CHECK(got("potato") == 1);
    type("POTATO ");           /* shifted letters are letters */
    wishhook_reset();
    type("Potato ");           CHECK(got("potato") == 1);
    type("potato");            CHECK(any_word() == 0);           /* not finished: nothing yet */
    type(" ");                 CHECK(got("potato") == 1);
}

static void test_never_on_other_words(void)
{
    cur = __func__;
    setup();
    static const char *const NOT[] = { "potatoes ", "potat ", "spotato ", "potatopotato ", "tomato ",
                                       "pot ", "potato1 ", "potato's ", "potato-salad ", "potato_x ",
                                       "1potato ", "-potato ", "carrots ", "carro ", "" };
    for (int i = 0; NOT[i][0]; i++) { type(NOT[i]); CHECK(any_word() == 0); }
    /* an accented letter makes it a different word */
    type("potato"); wishhook_key(NULL, 200, 1); wishhook_key(NULL, 200, 0); type(" ");
    CHECK(any_word() == 0);
    /* 33 letters: far too long to be any registered word */
    type("potatopotatopotatopotatopotatopota ");
    CHECK(any_word() == 0);
}

static void test_edits_and_movement(void)
{
    cur = __func__;
    setup();
    type("potatx"); key(BKSP); type("o "); CHECK(got("potato") == 1);    /* fixed a typo */
    type("potat"); key(BKSP); key(BKSP); type("ato "); CHECK(got("potato") == 1);
    type("potato"); key(LEFT); type(" ");       /* the cursor moved: do not trust it */
    CHECK(any_word() == 0);
    type("potato"); key(LEFT); type("potato ");   /* ... and what follows is mid-word, too */
    CHECK(any_word() == 0);
    type(" potato "); CHECK(got("potato") == 1);   /* until a word ends */
    type("potato"); key(HOME); type(" ");
    CHECK(any_word() == 0);
    type("po"); key(LCTRL); type("tato ");      /* a lone modifier is not typing */
    CHECK(got("potato") == 1);
    type("potato"); wishhook_key(NULL, LCTRL, 1); key(30); wishhook_key(NULL, LCTRL, 0);   /* Ctrl+A */
    type(" ");
    CHECK(any_word() == 0);
    type("potato"); key(ESC); type(" ");
    CHECK(any_word() == 0);
    key(BKSP); type("potato "); CHECK(any_word() == 0);                 /* deleting what we never saw */
    type(" potato "); CHECK(got("potato") == 1);                         /* a space starts afresh */
    type("pota"); wishhook_reset(); type("to ");                          /* a click or a new field */
    CHECK(any_word() == 0);
    wishhook_reset(); type("potato "); CHECK(got("potato") == 1);
}

static void test_keeps_nothing(void)
{
    cur = __func__;
    setup();
    type("potato");
    CHECK(wishhook_buffered() == 6);
    type(" ");
    CHECK(got("potato") == 1);
    CHECK(wishhook_buffered() == 0);                       /* cleared at the end of every word */
    type("hunter2 secret");
    CHECK(wishhook_buffered() <= 6);
    wishhook_reset();
    CHECK(wishhook_buffered() == 0);                       /* ... and on a click */
    type("abc"); key(ENTER);
    CHECK(wishhook_buffered() == 0);
    /* the queue never holds anything but registered words */
    type("hunter secret password "); CHECK(any_word() == 0);
    /* with nothing registered the matcher does not even look at the keys */
    wishhook_set(NULL, 0, NULL, 0);
    utf8_calls = 0;
    type("potato potato potato ");
    CHECK(utf8_calls == 0 && any_word() == 0);
}

static void test_registered_list_rules(void)
{
    cur = __func__;
    const char *w[] = { "pot", "Potato", "potato!", "p\xC3\xB6tato", "okay", "abcdefghijklmnopqrstuvwxyzabcdefg",
                        "w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8", "w9", "wa", "wb", "wc", "wd" };
    wishhook_set(w, 19, NULL, 0);
    wishhook_reset();
    type("pot potato ok "); CHECK(any_word() == 0);        /* none of those is a valid trigger word */
    type("okay ");           CHECK(got("okay") == 1);
    /* more than ten are not kept */
    const char *many[] = { "aaaa","bbbb","cccc","dddd","eeee","ffff","gggg","hhhh","iiii","jjjj","kkkk","llll" };
    wishhook_set(many, 12, NULL, 0);
    type("kkkk "); CHECK(any_word() == 0);
    type("jjjj "); CHECK(got("jjjj") == 1);
    /* a new list replaces the old and empties the queue */
    setup();
    type("potato ");
    const char *w2[] = { "carrot" };
    wishhook_set(w2, 1, NULL, 0);
    CHECK(any_word() == 0);
}

static void test_password_fields(void)
{
    cur = __func__;
    setup();
    /* The program says this is a password: a registered word typed into it is nobody's business. */
    ti_now = TI_SENSITIVE;
    type("potato ");
    CHECK(any_word() == 0);
    CHECK(wishhook_buffered() == 0);                    /* ... and nothing is held */
    type("pot");
    CHECK(wishhook_buffered() == 0);
    /* She tabs to an ordinary field: what was typed in the password is not carried over. */
    ti_now = TI_PLAIN;
    type("ato ");
    CHECK(any_word() == 0);
    type("potato ");
    CHECK(got("potato") == 1);                           /* an ordinary field works again */
    /* And the other way: halfway through a word in an ordinary field, the field turns secret. */
    type("pot");
    ti_now = TI_SENSITIVE;
    type("a");
    CHECK(wishhook_buffered() == 0);
    ti_now = TI_PLAIN;
    type("to ");
    CHECK(any_word() == 0);
    /* A click or a change of focus while secret does not make it ordinary. */
    ti_now = TI_SENSITIVE;
    wishhook_reset();
    type("potato ");
    CHECK(any_word() == 0);
}

static void test_unknown_means_hold_for_browsers(void)
{
    cur = __func__;
    setup();
    ti_now = TI_UNKNOWN;
    static const char *const HOLD[] = { "firefox", "org.mozilla.firefox", "Firefox-esr", "chromium",
                                        "google-chrome", "org.gnome.Epiphany", "brave-browser",
                                        "org.keepassxc.KeePassXC", "pinentry-gnome3",
                                        "polkit-gnome-authentication-agent-1", "", NULL };
    for (int i = 0; HOLD[i]; i++) {
        app_now = HOLD[i];
        type("potato ");
        CHECK(any_word() == 0);
    }
    app_now = NULL;                                       /* a program that will not say who it is */
    type("potato ");
    CHECK(any_word() == 0);
    static const char *const ALLOW[] = { "org.gnome.gedit", "xterm", "org.libreoffice.LibreOffice", "mpv", NULL };
    for (int i = 0; ALLOW[i]; i++) {
        app_now = ALLOW[i];
        wishhook_reset();
        type("potato ");
        CHECK(got("potato") == 1);
    }
    /* A browser that has announced an ordinary field is trusted about it. */
    app_now = "firefox";
    ti_now = TI_PLAIN;
    wishhook_reset();
    type("potato ");
    CHECK(got("potato") == 1);
    /* ... and one that announced a secret is not, whoever it is. */
    app_now = "org.gnome.gedit";
    ti_now = TI_SENSITIVE;
    type("potato ");
    CHECK(any_word() == 0);
}

/* GTK3 says nothing for a plain entry with visibility off: it arrives as an
 * ordinary field. The window and the program have to say it instead. */
static void test_dialogs_that_ask_for_secrets(void)
{
    cur = __func__;
    setup();
    ti_now = TI_PLAIN;                                  /* what GTK3 reports for an invisible entry */
    static const char *const TITLES[] = { "Authentication Required", "Enter password for \"work\"",
                                          "Unlock Keyring", "Sign in to Google", "Log in - Example",
                                          "PASSPHRASE needed", "Enter your verification code",
                                          "Two-Factor Setup", "Wi-Fi network authentication", NULL };
    for (int i = 0; TITLES[i]; i++) {
        title_now = TITLES[i];
        type("potato ");
        CHECK(any_word() == 0);
    }
    title_now = "Untitled";
    wishhook_reset();
    type("potato ");
    CHECK(got("potato") == 1);                          /* an ordinary title: fine */
    title_now = "Shopping list - Notes";
    wishhook_reset();
    type("potato ");
    CHECK(got("potato") == 1);
    static const char *const APPS[] = { "polkit-gnome-authentication-agent-1", "org.keepassxc.KeePassXC",
                                        "pinentry-gtk-2", "org.gnome.seahorse.Application", "ssh-askpass",
                                        "gcr-prompter", "org.gnome.Keyring", NULL };
    for (int i = 0; APPS[i]; i++) {
        app_now = APPS[i];
        ti_now = TI_PLAIN;                              /* even a field that says "ordinary" */
        type("potato ");
        CHECK(any_word() == 0);
    }
    app_now = "firefox";                                /* a browser's announced ordinary field is trusted ... */
    title_now = "My notes - Mozilla Firefox";
    wishhook_reset();
    type("potato ");
    CHECK(got("potato") == 1);
    title_now = "Sign in - Example Bank - Mozilla Firefox";   /* ... unless the page says it is a sign-in */
    type("potato ");
    CHECK(any_word() == 0);
}

static void test_apps(void)
{
    cur = __func__;
    setup();
    char o[128];
    wishhook_app("firefox");
    CHECK(wishhook_take_app(o) && !strcmp(o, "firefox"));
    wishhook_app("Firefox");                 CHECK(wishhook_take_app(o));
    wishhook_app("org.videolan.VLC");        CHECK(wishhook_take_app(o));
    wishhook_app("chromium");                CHECK(!wishhook_take_app(o));   /* only registered ids */
    wishhook_app("");                        CHECK(!wishhook_take_app(o));
    wishhook_app(NULL);                      CHECK(!wishhook_take_app(o));
    wishhook_set(NULL, 0, NULL, 0);
    wishhook_app("firefox");                 CHECK(!wishhook_take_app(o));   /* nothing registered */
}

/* The two functions the shell actually calls (src/aurshell/wish.c). */
static void test_the_shells_calls(void)
{
    cur = __func__;
    const char *w[] = { "potato" };
    const char *a[] = { "firefox" };
    CHECK(wishhook_set_triggers(w, 1, a, 1) == 0);
    wishhook_reset();
    char kind[16], val[64];
    CHECK(wishhook_take(kind, sizeof kind, val, sizeof val) == 0);
    type("potato ");
    wishhook_app("firefox");
    CHECK(wishhook_take(kind, sizeof kind, val, sizeof val) == 1 && !strcmp(kind, "typed") && !strcmp(val, "potato"));
    CHECK(wishhook_take(kind, sizeof kind, val, sizeof val) == 1 && !strcmp(kind, "app") && !strcmp(val, "firefox"));
    CHECK(wishhook_take(kind, sizeof kind, val, sizeof val) == 0);
    char tiny[4];
    type("potato ");
    CHECK(wishhook_take(kind, 3, tiny, sizeof tiny) == 1 && !strcmp(tiny, "pot"));   /* truncated, never overrun */
    CHECK(wishhook_take(NULL, 0, NULL, 0) == 0);
}

static void test_queue_is_bounded(void)
{
    cur = __func__;
    setup();
    for (int i = 0; i < 100; i++) type("potato ");
    int n = 0;
    char o[33];
    while (wishhook_take_word(o)) n++;
    CHECK(n == 8);                                          /* a flood is bounded, not stored */
}

/* W06: an X program (Discord, Spotify, Zoom, xterm) cannot say it is a password field, and its class and
 * title look ordinary: whatever is typed into it is never matched, and what was held is dropped. */
static void test_x_programs_are_never_matched(void)
{
    cur = __func__;
    setup();
    x11_now = 1;
    app_now = "discord"; title_now = "Discord";              /* ordinary-looking, and not a browser */
    type("potato "); CHECK(any_word() == 0);
    type("pota"); wishhook_reset(); type("to "); CHECK(any_word() == 0);   /* the word is not assembled across a focus change either */
    ti_now = TI_PLAIN;                                       /* even if something claimed the field was plain */
    type("carrot"); key(ENTER); CHECK(any_word() == 0);
    x11_now = 0;                                             /* the same program as a Wayland one is matched: this is not a blanket off switch */
    type("potato "); CHECK(got("potato") == 1);
}

int main(void)
{
    test_potato_whole_word();
    test_never_on_other_words();
    test_edits_and_movement();
    test_keeps_nothing();
    test_registered_list_rules();
    test_password_fields();
    test_unknown_means_hold_for_browsers();
    test_dialogs_that_ask_for_secrets();
    test_apps();
    test_the_shells_calls();
    test_queue_is_bounded();
    test_x_programs_are_never_matched();
    printf("wishtest-hook: %d checks, %d failed\n", passes + fails, fails);
    return fails ? 1 : 0;
}
