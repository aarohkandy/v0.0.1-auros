/* fuzz_wish.c -- the Wishes engine against the cloud, a socket and her home.
 *
 * src/aurwish/ (stream W1's files; S3 only fuzzes them) reads three
 * things nobody here wrote: wish documents from the cloud ("treat it as
 * if an attacker wrote it", docs/AI.md 7.1), request lines from any
 * program that can reach its unix socket, and ~/.local/share/auros/
 * wishes.json, which every program she runs can rewrite. One harness,
 * four doors, picked by the first byte:
 *
 *   0  json_parse() under the wish limits; every string of an accepted
 *      tree is walked.
 *   1  wish_parse_text() -- the validator -- with her words taken from
 *      the input ("SAID\n" then the document). An accepted wish must
 *      also make a sentence and a title, and what wish_json() writes for
 *      it must itself be accepted: a wish the engine saves and cannot
 *      read back is a wish that silently stops.
 *   2  store_load() of the input as wishes.json, store_recheck(), then
 *      store_save() and store_load() again: what it saved it must load,
 *      with the same number of wishes.
 *   4  answer_reply(): what the cloud said (any HTTP status, any body, either
 *      endpoint) turned into what the shell is told. The reply must always be
 *      one JSON object that starts {"id":7, and names a kind we know.
 *   5  cloud_clean_text(): her words before they leave the computer. What
 *      comes out is valid UTF-8 with no run of five digits and no address.
 *   3  engine_request(): the input is request lines, fed one by one to an
 *      engine over an empty store, a second apart, with a tick between.
 *
 * Fixture (apps, themes) as tools/wishtest.c builds it.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../../src/aurwish/engine.h"
#include "../../src/aurwish/answer.h"
#include "../../src/aurwish/cloud.h"
#include "../../src/aurwish/text.h"

static char dir[256], apps[300], themes[300], wpath[320], spath[320];
static wish_env ENV;

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) abort();
    fputs(text, f);
    fclose(f);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    snprintf(dir, sizeof dir, "%s/fuzz-wish-XXXXXX", base);
    if (!mkdtemp(dir)) abort();
    snprintf(apps, sizeof apps, "%s/apps", dir);
    snprintf(themes, sizeof themes, "%s/themes", dir);
    snprintf(wpath, sizeof wpath, "%s/w/wishes.json", dir);
    snprintf(spath, sizeof spath, "%s/w/saved.json", dir);
    char p[600];
    snprintf(p, sizeof p, "%s/w", dir);
    if (mkdir(apps, 0700) || mkdir(themes, 0700) || mkdir(p, 0700)) abort();
    snprintf(p, sizeof p, "%s/firefox.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=Firefox\nExec=firefox %u\nStartupWMClass=firefox\n");
    snprintf(p, sizeof p, "%s/org.videolan.VLC.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=VLC media player\nExec=vlc\n");
    snprintf(p, sizeof p, "%s/calc1.desktop", apps);
    put(p, "[Desktop Entry]\nType=Application\nName=Calculator\n");
    snprintf(p, sizeof p, "%s/sandstone.theme", themes);
    put(p, "theme_name=\"Sandstone\"\ntheme_variant=\"light\"\n");
    snprintf(p, sizeof p, "%s/nocturne.theme", themes);
    put(p, "theme_name=\"Nocturne\"\n");
    ENV.apps_dirs = apps;
    ENV.themes_dir = themes;
    ENV.sensors = 1u << S_COMPASS | 1u << S_LIGHT;
    ENV.fg = 0xF3F7FD;
    setenv("TZ", "UTC", 1);
    return 0;
}

/* store_load() sets an unreadable file aside rather than deleting it
 * (it is hers); here, every input would leave one. */
static void clean_store_dir(void)
{
    char d[320];
    snprintf(d, sizeof d, "%s/w", dir);
    DIR *dp = opendir(d);
    if (!dp) return;
    struct dirent *de;
    while ((de = readdir(dp))) {
        if (de->d_name[0] == '.') continue;
        char p[700];
        snprintf(p, sizeof p, "%s/%s", d, de->d_name);
        unlink(p);
    }
    closedir(dp);
}

static void walk(const jv *v)
{
    for (; v; v = v->next) {
        if (v->key) (void)strlen(v->key);
        if (v->type == J_STR) (void)strlen(v->s);
        walk(v->kid);
    }
}

static int emit(void *c, const char *l) { (void)c; (void)strlen(l); return 1; }
static int get50(void *c) { (void)c; return 50; }
static int set_ok(void *c, int v) { (void)c; (void)v; return 0; }
static int say_ok(void *c, const char *t) { (void)c; (void)strlen(t); return 0; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!dir[0]) LLVMFuzzerInitialize(NULL, NULL);
    if (size < 1 || size > 256 * 1024) return 0;
    int mode = data[0] & 7;   /* 0-3 as before; 4 and 5 are the cloud's half */
    const char *in = (const char *)data + 1;
    size_t n = size - 1;

    if (mode == 0) {
        static char mem[1 << 20];
        jarena a = { mem, sizeof mem, 0, NULL };
        walk(json_parse(&a, &WISH_LIMITS, in, n));
        return 0;
    }

    if (mode == 1) {
        const char *nl = memchr(in, '\n', n);
        if (!nl) return 0;
        char said[1024];
        size_t sl = (size_t)(nl - in);
        if (sl >= sizeof said) return 0;
        memcpy(said, in, sl); said[sl] = 0;
        wish_env env = ENV;
        env.said = said;
        static wish w, w2;
        wish_err err;
        if (wish_parse_text(nl + 1, n - sl - 1, &env, &w, &err) != 0) return 0;
        char s1[2048], s2[2048], t[512];
        if (wish_sentence(&w, s1, sizeof s1) < 0 || wish_title(&w, t, sizeof t) < 0) {
            fprintf(stderr, "an accepted wish has no sentence or title\n");
            abort();
        }
        static char jb[64 * 1024];
        jout o = { jb, sizeof jb, 0, 0 };
        wish_json(&w, &o);
        if (o.bad) { fprintf(stderr, "an accepted wish would not serialise\n"); abort(); }
        if (wish_parse_text(jb, o.len, &env, &w2, &err) != 0) {
            fprintf(stderr, "wish_json() wrote a wish wish_parse() refuses: %s (%s)\n%.*s\n",
                    err.why ? err.why : "?", err.where, (int)o.len, jb);
            abort();
        }
        if (wish_sentence(&w2, s2, sizeof s2) < 0 || strcmp(s1, s2)) {
            fprintf(stderr, "a saved wish reads back as a different sentence:\n%s\n%s\n", s1, s2);
            abort();
        }
        return 0;
    }

    if (mode == 5) {
        static char tmp[4096], out[1400];
        if (n >= sizeof tmp) return 0;
        memcpy(tmp, in, n); tmp[n] = 0;
        int cps = cloud_clean_text(tmp, out, sizeof out);
        char lg[16];
        (void)cloud_lang(tmp, lg, sizeof lg);
        if (strlen(lg) < 2 || strlen(lg) > 12) { fprintf(stderr, "a language tag of the wrong length\n"); abort(); }
        if (cps < 0) return 0;
        if (!text_valid(out)) { fprintf(stderr, "cleaned text is not valid text\n"); abort(); }
        int run = 0;
        for (const char *q = out; *q; q++) {
            run = (*q >= '0' && *q <= '9') ? run + 1 : 0;
            if (run >= 5) { fprintf(stderr, "five digits survived cleaning: %s\n", out); abort(); }
        }
        return 0;
    }

    if (mode == 4) {
        if (n < 3) return 0;
        static store st4;
        memset(&st4, 0, sizeof st4);
        st4.next = 1;
        wish_env env4 = ENV;
        engine_io io4 = { emit, get50, set_ok, get50, set_ok, set_ok, say_ok, NULL };
        engine *e = engine_new(&st4, NULL, &env4, &io4);
        if (!e) abort();
        static const int CODES[] = { 0, 200, 200, 200, 200, 400, 401, 429, 503, 500, 302, -1 };
        static char body[256 * 1024], reply[16384];
        size_t bl = n - 3;
        memcpy(body, in + 3, bl); body[bl] = 0;
        cloud_result r;
        memset(&r, 0, sizeof r);
        r.id = 7;
        r.kind = (unsigned char)in[0] & 1 ? CLOUD_ASK : CLOUD_WISH;
        r.http = CODES[(unsigned char)in[1] % (sizeof CODES / sizeof CODES[0])];
        r.err = "offline";
        r.body = body;
        r.len = bl;
        r.retry_after = (signed char)in[2];
        for (int round = 0; round < 3; round++) {          /* offers pile up and rotate */
            answer_reply(e, &r, "please show a kind note when I type potato", round & 1, dir, reply, sizeof reply, 1761200000 + round);
            if (strncmp(reply, "{\"id\":7,", 8) || strstr(reply, "\"kind\":") == NULL) {
                fprintf(stderr, "a malformed reply to the shell: %.200s\n", reply);
                abort();
            }
            static char mem[1 << 18];
            jarena a = { mem, sizeof mem, 0, NULL };
            static const jlimits L = { 65536, 8, 2000, 4096 };
            const jv *o = json_parse(&a, &L, reply, strlen(reply));
            if (!o || o->type != J_OBJ) { fprintf(stderr, "a reply that is not one JSON object: %.200s\n", reply); abort(); }
            const jv *k = jget(o, "kind");
            static const char *const KINDS[] = { "wish", "cannot", "help", "error", NULL };
            int known = 0;
            for (int i = 0; k && k->type == J_STR && KINDS[i]; i++) known |= !strcmp(k->s, KINDS[i]);
            if (!known) { fprintf(stderr, "an unknown kind of reply: %.200s\n", reply); abort(); }
        }
        engine_free(e);
        return 0;
    }

    clean_store_dir();
    if (mode == 2) {
        FILE *f = fopen(wpath, "wb");
        if (!f) abort();
        if (n && fwrite(in, 1, n, f) != n) abort();
        fclose(f);
        static store st, st2;
        (void)store_load(&st, wpath, &ENV);
        (void)store_recheck(&st, &ENV);
        if (store_save(&st, spath) == 0) {
            if (store_load(&st2, spath, &ENV) != 0 || st2.n != st.n) {
                fprintf(stderr, "store_save() wrote %d wishes, store_load() read %d\n", st.n, st2.n);
                abort();
            }
            store_free(&st2);
        }
        store_free(&st);
        return 0;
    }

    /* mode 3: request lines */
    static store st;
    memset(&st, 0, sizeof st);
    st.next = 1;
    wish_env env = ENV;
    engine_io io = { emit, get50, set_ok, get50, set_ok, set_ok, say_ok, NULL };
    engine *e = engine_new(&st, NULL, &env, &io);
    if (!e) abort();
    long long now = 1761200000;               /* 2025-10-23 */
    engine_startup(e, now);
    char reply[8192];
    const char *p = in, *end = in + n;
    while (p < end) {
        const char *q = memchr(p, '\n', (size_t)(end - p));
        if (!q) q = end;
        (void)engine_request(e, p, (size_t)(q - p), reply, sizeof reply, now);
        (void)strlen(reply);
        engine_tick(e, ++now);
        p = q + 1;
    }
    char line[4096];
    engine_triggers_line(e, line, sizeof line);
    engine_free(e);
    store_free(&st);
    return 0;
}
