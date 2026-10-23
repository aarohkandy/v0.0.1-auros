/* wishtest.c — the safety core of Wishes, attacked (docs/AI.md §7.1).
 *
 * Every test is named after what it stops. tools/wishtest.sh builds this
 * with ASan + UBSan, runs it, and then proves each check can fail: it
 * removes one check at a time from a copy of src/aurwish and requires
 * this program to go red.
 *
 *   cc -std=gnu11 -I src/aurwish tools/wishtest.c src/aurwish/{json,text,wish,store}.c \
 *      src/common/theme.c -lm -o /tmp/wishtest && /tmp/wishtest
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "json.h"
#include "text.h"
#include "wish.h"
#include "store.h"

static int fails, passes;
static const char *cur;
#define CHECK(c) do { if (c) passes++; else { fails++; \
    fprintf(stderr, "FAIL %s:%d %s: %s\n", __FILE__, __LINE__, cur, #c); } } while (0)

static char tmpdir[256], apps[300], themes[300];
static wish_env ENV;

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(text, f);
    fclose(f);
}

static void setup(void)
{
    snprintf(tmpdir, sizeof tmpdir, "%s/wishtest-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(tmpdir)) { perror("mkdtemp"); exit(2); }
    snprintf(apps, sizeof apps, "%s/apps", tmpdir);
    snprintf(themes, sizeof themes, "%s/themes", tmpdir);
    mkdir(apps, 0700);
    mkdir(themes, 0700);
    char p[600];
    snprintf(p, sizeof p, "%s/firefox.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=Firefox\nExec=firefox %u\nStartupWMClass=firefox\n");
    snprintf(p, sizeof p, "%s/org.videolan.VLC.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=VLC media player\nExec=vlc\n");
    snprintf(p, sizeof p, "%s/hidden.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=Hidden Thing\nNoDisplay=true\n");
    snprintf(p, sizeof p, "%s/evil.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=Bank\xE2\x80\xAE" "evil\n");
    snprintf(p, sizeof p, "%s/calc1.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=Calculator\n");
    snprintf(p, sizeof p, "%s/calc2.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=Calculator\n");
    snprintf(p, sizeof p, "%s/sandstone.theme", themes);
    put(p, "theme_name=\"Sandstone\"\ntheme_variant=\"light\"\n");
    snprintf(p, sizeof p, "%s/nocturne.theme", themes);
    put(p, "theme_name=\"Nocturne\"\n");
    ENV.apps_dirs = apps;
    ENV.themes_dir = themes;
    ENV.sensors = 1u << S_COMPASS;
    ENV.fg = 0xF3F7FD;                 /* nocturne's light text */
}

/* Parses `json` as a wish with her words `said`. 0 if accepted. */
static wish W;
static wish_err E;
static int parse(const char *json, const char *said)
{
    wish_env env = ENV;
    env.said = said;
    return wish_parse_text(json, strlen(json), &env, &W, &E);
}
#define OK(j, s)      CHECK(parse(j, s) == 0)
#define REFUSED(j, s) CHECK(parse(j, s) != 0)

static const char *POTATO =
    "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"potato\"},"
    "\"do\":[{\"type\":\"fill_screen\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"seconds\":3}]}";
static const char *COMPASS =
    "{\"v\":1,\"when\":{\"type\":\"sensor\",\"sensor\":\"compass\",\"heading\":\"north\","
    "\"tolerance_deg\":20},\"do\":[{\"type\":\"brightness\",\"set_percent\":100}]}";

/* A wish with one action `act` and trigger `when`. */
static char buf[16384];
static const char *mk(const char *when, const char *act)
{
    snprintf(buf, sizeof buf, "{\"v\":1,\"when\":%s,\"do\":[%s]}", when, act);
    return buf;
}

/* ── JSON ─────────────────────────────────────────────────────────── */

static int jparse(const char *s, size_t n)
{
    static char mem[1 << 20];
    jarena a = { mem, sizeof mem, 0, NULL };
    return json_parse(&a, &WISH_LIMITS, s, n) ? 0 : -1;
}
#define J_OK(s)  CHECK(jparse(s, sizeof(s) - 1) == 0)
#define J_BAD(s) CHECK(jparse(s, sizeof(s) - 1) != 0)

static void test_json_deep_nesting(void)
{
    cur = __func__;
    static char d[20001];
    memset(d, '[', 10000); memset(d + 10000, ']', 10000);
    CHECK(jparse(d, 20000) != 0);              /* no stack overflow, refused */
    J_OK("[[[[]]]]");
    J_BAD("[[[[[[]]]]]]");                     /* depth 6 > 5 */
}

static void test_json_huge_string(void)
{
    cur = __func__;
    size_t n = 2u << 20;
    char *s = malloc(n + 3);
    s[0] = '"'; memset(s + 1, 'a', n); s[n + 1] = '"'; s[n + 2] = 0;
    CHECK(jparse(s, n + 2) != 0);
    free(s);
    char s2[1100];                             /* under the byte limit, over the string limit */
    s2[0] = '"'; memset(s2 + 1, 'b', 1050); s2[1051] = '"';
    CHECK(jparse(s2, 1052) != 0);
}

static void test_json_strictness(void)
{
    cur = __func__;
    J_BAD("{\"a\":1,\"a\":2}");                /* duplicate key: which wins? neither */
    J_BAD("{\"a\":1} x");                      /* trailing garbage */
    J_BAD("\xEF\xBB\xBF{}");                   /* BOM */
    J_BAD("{\"a\":3.0}");                      /* not a whole number */
    J_BAD("{\"a\":1e1}");
    J_BAD("{\"a\":99999999999999999999}");
    J_BAD("{\"a\":-0}");
    J_BAD("{\"a\":01}");
    J_BAD("{\"a\":NaN}");
    J_BAD("{\"a\":Infinity}");
    J_BAD("{a:1}");
    J_BAD("{\"a\":1,}");
    J_BAD("[1,]");
    J_BAD("{\"a\":'x'}");
    J_BAD("{\"a\":tru}");
    J_BAD("");
    J_BAD("{\"a\":\"\\x41\"}");
    J_OK("{\"a\":-5,\"b\":[true,false,null],\"c\":\"\\u00e9\"}");
    J_BAD("{\"c\":\"line\\nbreak\"}");          /* no newline in anything she reads */
}

static void test_json_bad_unicode(void)
{
    cur = __func__;
    J_BAD("\"\\ud800\"");                      /* lone high surrogate */
    J_BAD("\"\\udc00\"");                      /* lone low surrogate */
    J_BAD("\"\\ud800\\u0041\"");
    J_BAD("\"\xC0\xAF\"");                     /* overlong '/' */
    J_BAD("\"\xE0\x80\xAF\"");
    J_BAD("\"\xED\xA0\x80\"");                 /* surrogate as UTF-8 */
    J_BAD("\"\xF4\x90\x80\x80\"");             /* > U+10FFFF */
    J_BAD("\"\xE2\x82\"");                     /* truncated */
    J_BAD("\"\xFF\"");
    J_BAD("\"\\u0000\"");                      /* NUL that would cut a C string */
    J_BAD("\"\x01\"");                         /* raw control */
    J_BAD("\"\\u001b[2J\"");                   /* a terminal escape, escaped */
    J_BAD("\"\\u0085\"");                      /* C1 control */
    J_OK("\"\\ud83e\\udd54\"");                /* a potato, as a surrogate pair */
}

/* ── the owner's two acceptance tests (§7) ─────────────────────────── */

static void test_owner_potato(void)
{
    cur = __func__;
    OK(POTATO, "fill my screen with potatoes when I type potato");
    CHECK(W.when.type == W_TYPED && !strcmp(W.when.word, "potato"));
    CHECK(W.n == 1 && W.d[0].type == A_FILL && W.d[0].seconds == 3);
    char s[1024], t[96];
    wish_sentence(&W, s, sizeof s);
    CHECK(!strcmp(s, "Every time you type the word *potato*, your screen fills with "
                     "\xF0\x9F\xA5\x94 for *3 seconds*. To stop it, press *Stop this wish*, "
                     "or switch it off in *My wishes*."));
    wish_title(&W, t, sizeof t);
    CHECK(!strcmp(t, "\xF0\x9F\xA5\x94 on the screen when you type potato"));
}

static void test_owner_compass(void)
{
    cur = __func__;
    OK(COMPASS, "when my laptop points north make the screen brighter");
    CHECK(W.when.type == W_SENSOR && W.when.sensor == S_COMPASS && W.when.heading == 0);
    CHECK(W.d[0].type == A_BRIGHT && W.d[0].is_set && W.d[0].pct == 100);
    char s[1024];
    wish_sentence(&W, s, sizeof s);
    CHECK(strstr(s, "When the computer points *north*, the screen brightness goes to *100%*") == s);
    unsigned keep = ENV.sensors;
    ENV.sensors = 0;
    REFUSED(COMPASS, "x");
    CHECK(E.missing && !strcmp(E.missing, "compass"));   /* §7.14: cannot, missing compass */
    ENV.sensors = keep;
}

/* ── §7.1/§7.3: the model's words never become hers ────────────────── */

static void test_model_title_and_summary_refused(void)
{
    cur = __func__;
    REFUSED("{\"v\":1,\"title\":\"Potatoes!\",\"when\":{\"type\":\"now\"},"
            "\"do\":[{\"type\":\"speak_time\"}]}", "x");
    REFUSED("{\"v\":1,\"summary\":\"Harmless\",\"when\":{\"type\":\"now\"},"
            "\"do\":[{\"type\":\"speak_time\"}]}", "x");
    OK("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"speak_time\"}]}", "x");
}

static void test_fake_support_text(void)
{
    cur = __func__;
    const char *said = "show me a nice message";
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":"
               "\"Your computer is infected. Call Microsoft support on 0800 123 4567\"}"), said);
    /* Even when she said every word, the scam words stay out. */
    const char *all = "your computer is infected call microsoft support";
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":"
               "\"Your computer is infected\"}"), all);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":"
               "\"Call Microsoft\"}"), all);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":"
               "\"Your Apple ID\"}"), "your apple id");
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":"
               "\"Enter your password\"}"), "enter your password");
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":"
               "\"The police called\"}"), "the police called");
}

static void test_text_not_her_words(void)
{
    cur = __func__;
    REFUSED(mk("{\"type\":\"time\",\"at\":\"09:00\"}", "{\"type\":\"remind\",\"text\":"
               "\"Send your PIN to Bob\"}"), "remind me to call mum");
    OK(mk("{\"type\":\"time\",\"at\":\"09:00\"}", "{\"type\":\"remind\",\"text\":"
          "\"It's time to take your pills\"}"), "remind me to take my pills at 9");
    CHECK(!strcmp(W.d[0].text, "It's time to take your pills"));
    /* No free text at all without her words. */
    REFUSED(mk("{\"type\":\"time\",\"at\":\"09:00\"}", "{\"type\":\"remind\",\"text\":\"pills\"}"), NULL);
}

static void test_filler_cannot_negate(void)
{
    cur = __func__;
    const char *said = "remind me to take my pills";
    REFUSED(mk("{\"type\":\"time\",\"at\":\"09:00\"}",
               "{\"type\":\"remind\",\"text\":\"Do not take your pills\"}"), said);
    REFUSED(mk("{\"type\":\"time\",\"at\":\"09:00\"}",
               "{\"type\":\"remind\",\"text\":\"Never take your pills\"}"), said);
    REFUSED(mk("{\"type\":\"time\",\"at\":\"09:00\"}",
               "{\"type\":\"remind\",\"text\":\"Don't take your pills\"}"), said);
}

static void test_phone_numbers(void)
{
    cur = __func__;
    const char *said = "call 0800 123 456 and 12345";
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"call 0800 123 456\"}"), said);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"call 12345\"}"), said);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"call 1-2-3-4-5\"}"),
            "call 1 2 3 4 5");
    OK(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"call 0800\"}"), said);
}

static void test_addresses_in_text(void)
{
    cur = __func__;
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"visit example.com\"}"),
            "visit example.com");
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"go to www now\"}"),
            "go to www now");
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"mail bob@x\"}"),
            "mail bob@x");
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"https x\"}"),
            "https x");
}

static void test_money_amounts(void)
{
    cur = __func__;
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"send 50 dollars\"}"),
            "send 50 dollars");
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"send $50\"}"), "send $50");
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"send \xE2\x82\xAC" "50\"}"),
            "send \xE2\x82\xAC" "50");
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"buy bitcoin\"}"), "buy bitcoin");
    /* "pay the gas bill" is a reminder she needs. */
    OK(mk("{\"type\":\"time\",\"at\":\"10:00\"}", "{\"type\":\"remind\",\"text\":\"Pay the gas bill\"}"),
       "remind me to pay the gas bill");
}

static void test_unicode_tricks(void)
{
    cur = __func__;
    const char *said = "hello there";
    /* bidi override: "ereht olleh" shown as something else */
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"hello \xE2\x80\xAEthere\"}"), said);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"hel\xE2\x80\x8Blo\"}"), said);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"hello\xE2\x81\xA0there\"}"), said);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"hello\xEF\xBB\xBF\"}"), said);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"hello\xE2\x80\xA8there\"}"), said);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"hello \xF3\xA0\x80\x81\"}"), said);
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"hello \xEE\x80\x80\"}"), said);
    /* Greek capital alpha inside a Latin word: "Apple" that is not */
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"\xCE\x91pple pie\"}"),
            "\xCE\x91pple pie");
    /* Zalgo: accents stacked over everything else on the screen */
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":"
               "\"he\xCC\x81\xCC\x81\xCC\x81llo there\"}"), "he\xCC\x81\xCC\x81\xCC\x81llo there");
    /* the bold marker of the sentence */
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"hello *there*\"}"), said);
    /* decomposed and composed are the same word (NFC) */
    OK(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"cafe\xCC\x81 time\"}"),
       "remind me about the caf\xC3\xA9");
    CHECK(!strcmp(W.d[0].text, "caf\xC3\xA9 time"));
    /* curly quotes become straight, and are the same word */
    OK(mk("{\"type\":\"now\"}", "{\"type\":\"show_message\",\"text\":\"don\xE2\x80\x99t forget\"}"),
       "say don't forget");
}

static void test_emoji_is_one_friendly_emoji(void)
{
    cur = __func__;
    const char *w = "{\"type\":\"now\"}";
    char a[200];
    static const char *const BAD[] = {
        "\xF0\x9F\xA5\x94\xF0\x9F\xA5\x94",                    /* two potatoes */
        "\xF0\x9F\xA5\x94" "a",                                /* potato and a letter */
        "a", "",
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", /* ZWJ family */
        "\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7",                    /* a flag */
        "\xF0\x9F\xA5\x94\xE2\x80\x8D",                        /* potato + ZWJ */
        "\xE2\x80\x8D\xF0\x9F\xA5\x94",
        "\xF0\x9F\x94\xAB",                                    /* a gun: not friendly */
        "\xF0\x9F\x92\x80",                                    /* a skull */
        "\xF0\x9F\x8F\xBD",                                    /* a skin tone alone */
        NULL };
    for (int i = 0; BAD[i]; i++) {
        snprintf(a, sizeof a, "{\"type\":\"fill_screen\",\"emoji\":\"%s\",\"seconds\":2}", BAD[i]);
        REFUSED(mk(w, a), "x");
    }
    OK(mk(w, "{\"type\":\"fill_screen\",\"emoji\":\"\xE2\x9D\xA4\",\"seconds\":2}"), "x");
    CHECK(!strcmp(W.d[0].emoji, "\xE2\x9D\xA4\xEF\xB8\x8F"));   /* canonical heart */
    REFUSED(mk(w, "{\"type\":\"fill_screen\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"text\":\"hi\",\"seconds\":2}"), "hi");
}

static void test_lengths(void)
{
    cur = __func__;
    const char *w = "{\"type\":\"now\"}";
    char said[600], act[700], t[400];
    memset(t, 0, sizeof t);
    for (int i = 0; i < 24; i++) t[i] = 'a' + (i % 2);
    snprintf(said, sizeof said, "write %s", t);
    snprintf(act, sizeof act, "{\"type\":\"fill_screen\",\"text\":\"%s\",\"seconds\":2}", t);
    OK(mk(w, act), said);                        /* 24 letters */
    t[24] = 'a';
    snprintf(said, sizeof said, "write %s", t);
    snprintf(act, sizeof act, "{\"type\":\"fill_screen\",\"text\":\"%s\",\"seconds\":2}", t);
    REFUSED(mk(w, act), said);                   /* 25 */
    /* 141 characters of her own words, on a note */
    char words[400] = "";
    for (int i = 0; i < 47; i++) strcat(words, "ab ");
    words[141] = 0;
    snprintf(act, sizeof act, "{\"type\":\"show_message\",\"text\":\"%s\"}", words);
    REFUSED(mk(w, act), "ab");
    words[140] = 0;
    snprintf(act, sizeof act, "{\"type\":\"show_message\",\"text\":\"%s\"}", words);
    OK(mk(w, act), "ab");
}

static void test_ranges(void)
{
    cur = __func__;
    const char *now = "{\"type\":\"now\"}";
    REFUSED(mk(now, "{\"type\":\"fill_screen\",\"emoji\":\"\xE2\xAD\x90\",\"seconds\":0}"), "x");
    REFUSED(mk(now, "{\"type\":\"fill_screen\",\"emoji\":\"\xE2\xAD\x90\",\"seconds\":11}"), "x");
    REFUSED(mk(now, "{\"type\":\"fill_screen\",\"emoji\":\"\xE2\xAD\x90\",\"seconds\":\"3\"}"), "x");
    OK(mk(now, "{\"type\":\"fill_screen\",\"emoji\":\"\xE2\xAD\x90\",\"seconds\":10}"), "x");
    REFUSED(mk(now, "{\"type\":\"brightness\",\"set_percent\":29}"), "x");    /* §7.4 floor 30 */
    REFUSED(mk(now, "{\"type\":\"brightness\",\"set_percent\":101}"), "x");
    OK(mk(now, "{\"type\":\"brightness\",\"set_percent\":30}"), "x");
    REFUSED(mk(now, "{\"type\":\"brightness\",\"set_percent\":50,\"change_percent\":5}"), "x");
    REFUSED(mk(now, "{\"type\":\"brightness\"}"), "x");
    REFUSED(mk(now, "{\"type\":\"brightness\",\"change_percent\":0}"), "x");
    REFUSED(mk(now, "{\"type\":\"brightness\",\"change_percent\":-51}"), "x");
    OK(mk(now, "{\"type\":\"brightness\",\"change_percent\":-50}"), "x");
    REFUSED(mk(now, "{\"type\":\"volume\",\"set_percent\":81}"), "x");        /* never above 80 */
    REFUSED(mk(now, "{\"type\":\"volume\",\"set_percent\":19}"), "x");        /* §7.4 floor 20 */
    OK(mk(now, "{\"type\":\"volume\",\"set_percent\":80}"), "x");
    REFUSED(mk("{\"type\":\"every\",\"minutes\":4}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"every\",\"minutes\":1441}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"idle\",\"minutes\":0}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"idle\",\"minutes\":241}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"battery_below\",\"percent\":4}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"battery_below\",\"percent\":51}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    OK(mk("{\"type\":\"battery_below\",\"percent\":20}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"sensor\",\"sensor\":\"compass\",\"heading\":\"north\",\"tolerance_deg\":4}",
               "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"sensor\",\"sensor\":\"compass\",\"heading\":\"up\"}",
               "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"time\",\"at\":\"24:00\"}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"time\",\"at\":\"9:00\"}", "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"time\",\"at\":\"09:00\",\"days\":[\"mon\",\"mon\"]}",
               "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"time\",\"at\":\"09:00\",\"days\":[]}",
               "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    REFUSED(mk("{\"type\":\"time\",\"at\":\"09:00\",\"days\":[\"someday\"]}",
               "{\"type\":\"brightness\",\"set_percent\":50}"), "x");
    /* 1-4 actions */
    REFUSED("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[]}", "x");
    REFUSED("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"speak_time\"},{\"type\":\"speak_time\"},"
            "{\"type\":\"speak_time\"},{\"type\":\"speak_time\"},{\"type\":\"speak_time\"}]}", "x");
    REFUSED("{\"v\":2,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"speak_time\"}]}", "x");
    REFUSED("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"speak_time\"}],\"enabled\":1}", "x");
}

static void test_wrong_types_and_unknown_fields(void)
{
    cur = __func__;
    const char *now = "{\"type\":\"now\"}";
    REFUSED("[]", "x");
    REFUSED("\"wish\"", "x");
    REFUSED("{\"v\":\"1\",\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"speak_time\"}]}", "x");
    REFUSED("{\"v\":1,\"when\":\"now\",\"do\":[{\"type\":\"speak_time\"}]}", "x");
    REFUSED("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":{\"type\":\"speak_time\"}}", "x");
    REFUSED("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[\"speak_time\"]}", "x");
    REFUSED(mk(now, "{\"type\":\"speak_time\",\"command\":\"rm -rf ~\"}"), "x");
    REFUSED(mk("{\"type\":\"now\",\"exec\":\"sh\"}", "{\"type\":\"speak_time\"}"), "x");
    REFUSED(mk(now, "{\"type\":\"night_light\",\"on\":\"yes\"}"), "x");
    REFUSED(mk(now, "{\"type\":\"night_light\",\"on\":1}"), "x");
    static const char *const EVIL[] = { "run_command", "delete_file", "send_email", "buy",
                                        "set_password", "disable_firewall", "type_keys", NULL };
    char a[200];
    for (int i = 0; EVIL[i]; i++) {
        snprintf(a, sizeof a, "{\"type\":\"%s\"}", EVIL[i]);
        REFUSED(mk(now, a), "x");
        CHECK(!strcmp(E.missing, "other"));
    }
    REFUSED(mk("{\"type\":\"on_email\"}", "{\"type\":\"speak_time\"}"), "x");
}

static void test_scam_install(void)
{
    cur = __func__;
    REFUSED(mk("{\"type\":\"now\"}", "{\"type\":\"install_program\",\"flathub_id\":\"com.anydesk.Anydesk\"}"), "x");
    CHECK(E.missing && !strcmp(E.missing, "install"));
}

static void test_trigger_action_matrix(void)
{
    cur = __func__;
    const char *typed = "{\"type\":\"typed_word\",\"word\":\"potato\"}";
    const char *every = "{\"type\":\"every\",\"minutes\":60}";
    const char *time9 = "{\"type\":\"time\",\"at\":\"09:00\"}";
    const char *now = "{\"type\":\"now\"}";
    const char *said = "say potato";
    REFUSED(mk(typed, "{\"type\":\"remind\",\"text\":\"potato\"}"), said);   /* remind: time only */
    OK(mk(time9, "{\"type\":\"remind\",\"text\":\"potato\"}"), said);
    REFUSED(mk(now, "{\"type\":\"remind\",\"text\":\"potato\"}"), said);
    REFUSED(mk(typed, "{\"type\":\"speak\",\"text\":\"potato\"}"), said);    /* speak: time/now */
    REFUSED(mk(every, "{\"type\":\"speak_time\"}"), said);
    REFUSED(mk(time9, "{\"type\":\"volume\",\"set_percent\":50}"), said);    /* volume: now */
    REFUSED(mk(every, "{\"type\":\"set_look\",\"look\":\"dock\"}"), said);   /* settings: now */
    REFUSED(mk(time9, "{\"type\":\"set_text_size\",\"size\":\"bigger\"}"), said);
    REFUSED(mk(time9, "{\"type\":\"set_wallpaper\",\"theme_name\":\"Sandstone\"}"), said);
    REFUSED(mk(time9, "{\"type\":\"pointer_size\",\"size\":\"bigger\"}"), said);
    REFUSED(mk(time9, "{\"type\":\"open_website\",\"site\":\"BBC\"}"), said);     /* site: now */
    OK(mk(now, "{\"type\":\"open_website\",\"site\":\"BBC\"}"), said);
    REFUSED(mk(typed, "{\"type\":\"open_app\",\"app_name\":\"Firefox\"}"), said); /* app: now/time */
    OK(mk(time9, "{\"type\":\"open_app\",\"app_name\":\"Firefox\"}"), said);
    REFUSED(mk("{\"type\":\"sensor\",\"sensor\":\"compass\",\"heading\":\"north\"}",
               "{\"type\":\"brightness\",\"change_percent\":10}"), said);    /* set only */
    OK(mk(now, "{\"type\":\"brightness\",\"change_percent\":10}"), said);
    REFUSED(mk(time9, "{\"type\":\"brightness\",\"change_percent\":10}"), said);
    OK(mk(typed, "{\"type\":\"night_light\",\"on\":true}"), said);
    /* one outward action at most */
    REFUSED("{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"open_app\",\"app_name\":\"Firefox\"},"
            "{\"type\":\"open_website\",\"site\":\"BBC\"}]}", said);
}

static void test_quiet_hours_at_validation(void)
{
    cur = __func__;
    const char *said = "chime";
    REFUSED(mk("{\"type\":\"time\",\"at\":\"22:00\"}", "{\"type\":\"play_sound\",\"sound\":\"chime\"}"), said);
    REFUSED(mk("{\"type\":\"time\",\"at\":\"07:59\"}", "{\"type\":\"fill_screen\",\"emoji\":\"\xE2\xAD\x90\",\"seconds\":2}"), said);
    OK(mk("{\"type\":\"time\",\"at\":\"08:00\"}", "{\"type\":\"play_sound\",\"sound\":\"chime\"}"), said);
    OK(mk("{\"type\":\"time\",\"at\":\"22:00\"}", "{\"type\":\"remind\",\"text\":\"chime\"}"), said);
    OK(mk("{\"type\":\"time\",\"at\":\"22:00\"}", "{\"type\":\"night_light\",\"on\":true}"), said);
    REFUSED(mk("{\"type\":\"every\",\"minutes\":15}", "{\"type\":\"play_sound\",\"sound\":\"bell\"}"), said);
    OK(mk("{\"type\":\"every\",\"minutes\":30}", "{\"type\":\"play_sound\",\"sound\":\"bell\"}"), said);
}

static void test_trigger_words(void)
{
    cur = __func__;
    const char *a = "{\"type\":\"fill_screen\",\"emoji\":\"\xE2\xAD\x90\",\"seconds\":2}";
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"pot\"}", a), "x");          /* < 4 */
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"password\"}", a), "x");     /* login */
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"bank\"}", a), "x");
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"paypal\"}", a), "x");
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"that\"}", a), "x");         /* stop word */
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"potato!\"}", a), "x");
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"po tato\"}", a), "x");
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"p\xC3\xB6tato\"}", a), "x");
    REFUSED(mk("{\"type\":\"typed_word\",\"word\":\"abcdefghijklmnopqrstuvwxyzabcdefg\"}", a), "x");
    OK(mk("{\"type\":\"typed_word\",\"word\":\"Potato\"}", a), "x");
    CHECK(!strcmp(W.when.word, "potato"));
}

static void test_sites_not_urls(void)
{
    cur = __func__;
    const char *now = "{\"type\":\"now\"}";
    OK(mk(now, "{\"type\":\"open_website\",\"site\":\"bbc news\"}"), "x");
    CHECK(!strcmp(W.d[0].url, "https://www.bbc.co.uk/news") && !strcmp(W.d[0].site_name, "BBC"));
    REFUSED(mk(now, "{\"type\":\"open_website\",\"url\":\"https://evil.example\"}"), "x");
    REFUSED(mk(now, "{\"type\":\"open_website\",\"site\":\"evil.example\"}"), "x");
    CHECK(E.missing && !strcmp(E.missing, "site"));
    REFUSED(mk(now, "{\"type\":\"open_website\",\"site\":\"https://www.bbc.co.uk/news\"}"), "x");
}

static void test_programs_are_hers(void)
{
    cur = __func__;
    const char *now = "{\"type\":\"now\"}";
    OK(mk(now, "{\"type\":\"open_app\",\"app_name\":\"firefox\"}"), "x");
    CHECK(!strcmp(W.d[0].app.id, "firefox") && !strcmp(W.d[0].app.label, "Firefox"));
    OK(mk(now, "{\"type\":\"open_app\",\"app_name\":\"VLC\"}"), "x");        /* last part of the id */
    CHECK(!strcmp(W.d[0].app.id, "org.videolan.VLC"));
    REFUSED(mk(now, "{\"type\":\"open_app\",\"app_name\":\"Chrome\"}"), "x");
    CHECK(!strcmp(E.missing, "app"));
    REFUSED(mk(now, "{\"type\":\"open_app\",\"app_name\":\"Hidden Thing\"}"), "x");   /* NoDisplay */
    REFUSED(mk(now, "{\"type\":\"open_app\",\"app_name\":\"Calculator\"}"), "x");     /* two of them */
    REFUSED(mk(now, "{\"type\":\"open_app\",\"app_name\":\"evil\"}"), "x");   /* a Name= with bidi */
    REFUSED(mk(now, "{\"type\":\"open_app\",\"app_name\":\"../../bin/sh\"}"), "x");
    OK(mk("{\"type\":\"app_opened\",\"app_name\":\"Firefox\"}",
          "{\"type\":\"fill_screen\",\"emoji\":\"\xE2\xAD\x90\",\"seconds\":2}"), "x");
}

static void test_wallpaper(void)
{
    cur = __func__;
    const char *now = "{\"type\":\"now\"}";
    REFUSED(mk(now, "{\"type\":\"set_wallpaper\",\"color\":\"#FFFFFF\"}"), "x");   /* white under white text */
    OK(mk(now, "{\"type\":\"set_wallpaper\",\"color\":\"#102040\"}"), "x");
    char s[1024];
    wish_sentence(&W, s, sizeof s);
    CHECK(strstr(s, "plain *dark blue*") != NULL);
    REFUSED(mk(now, "{\"type\":\"set_wallpaper\",\"color\":\"red\"}"), "x");
    REFUSED(mk(now, "{\"type\":\"set_wallpaper\",\"color\":\"#12345G\"}"), "x");
    OK(mk(now, "{\"type\":\"set_wallpaper\",\"theme_name\":\"sandstone\"}"), "x");
    CHECK(!strcmp(W.d[0].theme, "sandstone") && !strcmp(W.d[0].theme_label, "Sandstone"));
    REFUSED(mk(now, "{\"type\":\"set_wallpaper\",\"theme_name\":\"../../etc/passwd\"}"), "x");
    REFUSED(mk(now, "{\"type\":\"set_wallpaper\",\"theme_name\":\"Pink\"}"), "x");
    CHECK(!strcmp(E.missing, "theme"));
    REFUSED(mk(now, "{\"type\":\"set_wallpaper\",\"color\":\"#102040\",\"theme_name\":\"Sandstone\"}"), "x");
}

/* Every trigger and every action renders: a sentence, never "(null)",
 * bold marks balanced, and a title of at most 40 characters. */
static void test_every_sentence(void)
{
    cur = __func__;
    static const char *const WH[] = {
        "{\"type\":\"now\"}", "{\"type\":\"typed_word\",\"word\":\"potato\"}",
        "{\"type\":\"time\",\"at\":\"09:30\",\"days\":[\"mon\",\"wed\",\"fri\"]}",
        "{\"type\":\"time\",\"at\":\"13:05\",\"days\":[\"mon\",\"tue\",\"wed\",\"thu\",\"fri\"]}",
        "{\"type\":\"every\",\"minutes\":90}", "{\"type\":\"every\",\"minutes\":120}",
        "{\"type\":\"startup\"}", "{\"type\":\"app_opened\",\"app_name\":\"VLC media player\"}",
        "{\"type\":\"battery_below\",\"percent\":15}", "{\"type\":\"plugged_in\"}",
        "{\"type\":\"unplugged\"}", "{\"type\":\"usb_inserted\"}", "{\"type\":\"idle\",\"minutes\":1}",
        "{\"type\":\"sensor\",\"sensor\":\"compass\",\"heading\":\"west\"}", NULL };
    static const char *const DO[] = {
        "{\"type\":\"fill_screen\",\"emoji\":\"\xF0\x9F\x8C\xB8\",\"seconds\":1}",
        "{\"type\":\"fill_screen\",\"text\":\"Hello Rose\",\"seconds\":5}",
        "{\"type\":\"show_message\",\"text\":\"Hello Rose\"}",
        "{\"type\":\"remind\",\"text\":\"Hello Rose\",\"private\":true}",
        "{\"type\":\"speak\",\"text\":\"Hello Rose\"}", "{\"type\":\"speak_time\"}",
        "{\"type\":\"play_sound\",\"sound\":\"birds\"}", "{\"type\":\"brightness\",\"set_percent\":60}",
        "{\"type\":\"brightness\",\"change_percent\":-20}", "{\"type\":\"volume\",\"change_percent\":10}",
        "{\"type\":\"open_app\",\"app_name\":\"Firefox\"}", "{\"type\":\"open_website\",\"site\":\"Wikipedia\"}",
        "{\"type\":\"set_look\",\"look\":\"tiles\"}", "{\"type\":\"set_text_size\",\"size\":\"biggest\"}",
        "{\"type\":\"set_wallpaper\",\"theme_name\":\"Nocturne\"}", "{\"type\":\"night_light\",\"on\":false}",
        "{\"type\":\"pointer_size\",\"size\":\"biggest\"}",
        "{\"type\":\"do_not_disturb\",\"on\":true,\"until\":\"07:00\"}", NULL };
    int rendered = 0;
    for (int i = 0; WH[i]; i++)
        for (int j = 0; DO[j]; j++) {
            if (parse(mk(WH[i], DO[j]), "hello rose") != 0) continue;
            char s[1024], t[200];
            int n = wish_sentence(&W, s, sizeof s);
            wish_title(&W, t, sizeof t);
            rendered++;
            CHECK(n > 20 && (size_t)n < sizeof s - 1);
            CHECK(!strstr(s, "(null)") && !strstr(t, "(null)"));
            int stars = 0;
            for (char *p = s; *p; p++) stars += *p == '*';
            CHECK(stars % 2 == 0);
            CHECK(text_graphemes(t) <= 40);
            /* and the document survives a trip through wishes.json */
            char a[8192], b[8192];
            jout oa = { a, sizeof a, 0, 0 }, ob = { b, sizeof b, 0, 0 };
            wish_json(&W, &oa);
            wish x = W;
            CHECK(parse(a, "hello rose") == 0);
            wish_json(&W, &ob);
            CHECK(!strcmp(a, b));
            (void)x;
        }
    CHECK(rendered >= 60);
}

/* ── wishes.json ─────────────────────────────────────────────────── */

static void test_store_roundtrip_and_mode(void)
{
    cur = __func__;
    char path[600];
    snprintf(path, sizeof path, "%s/wishes.json", tmpdir);
    store st;
    memset(&st, 0, sizeof st);
    st.next = 1;
    CHECK(parse(POTATO, "potato") == 0);
    stored *a = store_add(&st, &W, "fill my screen with potatoes when I type potato", 0, 1000);
    CHECK(parse(COMPASS, "north") == 0);
    stored *b = store_add(&st, &W, "north", 1, 2000);
    CHECK(a && b && !strcmp(a->id, "w1") && !strcmp(b->id, "w2"));
    b->status = ST_OFF;
    CHECK(store_save(&st, path) == 0);
    struct stat sb;
    CHECK(stat(path, &sb) == 0 && (sb.st_mode & 0777) == 0600);
    char nw[700];
    snprintf(nw, sizeof nw, "%s.new", path);
    CHECK(access(nw, F_OK) != 0);               /* no temporary left behind */
    store st2;
    CHECK(store_load(&st2, path, &ENV) == 0);
    CHECK(st2.n == 2 && st2.next == 3);
    CHECK(st2.s[0]->ok && st2.s[0]->status == ST_ON && !strcmp(st2.s[0]->w.when.word, "potato"));
    CHECK(st2.s[1]->ok && st2.s[1]->status == ST_OFF && st2.s[1]->by_voice);
    store_free(&st);
    store_free(&st2);
}

static void test_store_corrupt_never_blocks(void)
{
    cur = __func__;
    char path[600];
    snprintf(path, sizeof path, "%s/corrupt.json", tmpdir);
    static const char *const GARBAGE[] = { "", "{", "null", "{\"v\":1}", "{\"v\":2,\"wishes\":[]}",
                                           "\x00\x01\x02", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]", NULL };
    for (int i = 0; GARBAGE[i]; i++) {
        put(path, GARBAGE[i]);
        store st;
        int r = store_load(&st, path, &ENV);
        CHECK(r == 1 && st.n == 0);             /* starts, with nothing */
        CHECK(access(path, F_OK) != 0);         /* moved aside, not deleted */
        store_free(&st);
    }
    /* far too big */
    FILE *f = fopen(path, "w");
    fputs("{\"v\":1,\"wishes\":[],\"pad\":\"", f);
    for (int i = 0; i < 300000; i++) fputc('a', f);
    fputs("\"}", f);
    fclose(f);
    store st;
    CHECK(store_load(&st, path, &ENV) == 1 && st.n == 0);
    store_free(&st);
    /* a symlink planted where the file should be is not followed */
    char target[700];
    snprintf(target, sizeof target, "%s/elsewhere", tmpdir);
    put(target, "{\"v\":1,\"wishes\":[]}");
    unlink(path);
    if (symlink(target, path) == 0) {
        CHECK(store_load(&st, path, &ENV) == 0 && st.n == 0);
        store_free(&st);
        unlink(path);
    }
}

static void test_store_keeps_what_it_cannot_read(void)
{
    cur = __func__;
    char path[600];
    snprintf(path, sizeof path, "%s/mixed.json", tmpdir);
    put(path,
        "{\"v\":1,\"next\":5,\"wishes\":["
        "{\"id\":\"w1\",\"status\":\"on\",\"said\":\"potato\",\"wish\":"
        "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"potato\"},"
        "\"do\":[{\"type\":\"fill_screen\",\"emoji\":\"\xF0\x9F\xA5\x94\",\"seconds\":3}]}},"
        "{\"id\":\"w2\",\"status\":\"on\",\"said\":\"x\",\"wish\":{\"v\":2,\"when\":{\"type\":\"teleport\"}}},"
        "{\"id\":\"w3\",\"status\":\"on\",\"said\":\"remind me to call mum\",\"wish\":"
        "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"09:00\"},"
        "\"do\":[{\"type\":\"remind\",\"text\":\"Call Microsoft support now\"}]}},"
        "{\"id\":\"w1\",\"status\":\"on\",\"said\":\"x\",\"wish\":"
        "{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"speak_time\"}]}},"
        "{\"id\":\"w4\",\"status\":\"on\",\"said\":\"x\",\"wish\":"
        "{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[{\"type\":\"open_app\",\"app_name\":\"Gone App\"}]}}"
        "]}");
    store st;
    CHECK(store_load(&st, path, &ENV) == 0 && st.n == 5);
    CHECK(st.s[0]->ok && st.s[0]->status == ST_ON);
    CHECK(!st.s[1]->ok && st.s[1]->status == ST_NEWER);        /* kept, not run */
    CHECK(!st.s[2]->ok && st.s[2]->status == ST_BROKEN);       /* tampered text: never shown */
    CHECK(strcmp(st.s[3]->id, "w1") != 0);                      /* duplicate id renumbered */
    CHECK(!st.s[4]->ok && st.s[4]->status == ST_MISSING);
    CHECK(store_save(&st, path) == 0);
    FILE *f = fopen(path, "r");
    char all[8192] = "";
    size_t k = fread(all, 1, sizeof all - 1, f);
    all[k] = 0;
    fclose(f);
    CHECK(strstr(all, "{\"v\":2,\"when\":{\"type\":\"teleport\"}}") != NULL);   /* verbatim */
    CHECK(strstr(all, "paused:newer") != NULL);
    store_free(&st);
}

static void test_prev_values(void)
{
    cur = __func__;
    static char mem[65536];
    jarena a = { mem, sizeof mem, 0, NULL };
    static const jlimits L = { 4096, 4, 100, 512 };
    wish_do d;
    const char *good[] = { "{\"type\":\"set_look\",\"look\":\"rail\"}",
                           "{\"type\":\"set_wallpaper\",\"theme\":\"nocturne\"}",
                           "{\"type\":\"brightness\",\"set_percent\":10}",
                           "{\"type\":\"night_light\",\"on\":false}", NULL };
    const char *bad[] = { "{\"type\":\"open_app\",\"app\":\"firefox\"}",
                          "{\"type\":\"set_wallpaper\",\"theme\":\"../x\"}",
                          "{\"type\":\"brightness\",\"set_percent\":0}",
                          "{\"type\":\"set_look\",\"look\":\"rail\",\"x\":1}",
                          "{\"type\":\"volume\",\"set_percent\":101}", NULL };
    for (int i = 0; good[i]; i++)
        CHECK(store_parse_prev(json_parse(&a, &L, good[i], strlen(good[i])), &ENV, &d) == 0);
    for (int i = 0; bad[i]; i++)
        CHECK(store_parse_prev(json_parse(&a, &L, bad[i], strlen(bad[i])), &ENV, &d) != 0);
}

int main(void)
{
    setenv("TZ", "UTC", 1);
    setup();
    test_json_deep_nesting();
    test_json_huge_string();
    test_json_strictness();
    test_json_bad_unicode();
    test_owner_potato();
    test_owner_compass();
    test_model_title_and_summary_refused();
    test_fake_support_text();
    test_text_not_her_words();
    test_filler_cannot_negate();
    test_phone_numbers();
    test_addresses_in_text();
    test_money_amounts();
    test_unicode_tricks();
    test_emoji_is_one_friendly_emoji();
    test_lengths();
    test_ranges();
    test_wrong_types_and_unknown_fields();
    test_scam_install();
    test_trigger_action_matrix();
    test_quiet_hours_at_validation();
    test_trigger_words();
    test_sites_not_urls();
    test_programs_are_hers();
    test_wallpaper();
    test_every_sentence();
    test_store_roundtrip_and_mode();
    test_store_corrupt_never_blocks();
    test_store_keeps_what_it_cannot_read();
    test_prev_values();
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmpdir);
    if (system(cmd)) {}
    printf("wishtest: %d checks, %d failed\n", passes + fails, fails);
    return fails ? 1 : 0;
}
