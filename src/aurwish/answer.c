/* answer.c — see answer.h. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "answer.h"
#include "json.h"

const char *answer_error_text(const char *code)
{
    static const struct { const char *c, *t; } T[] = {
        { "offline", "Wishes needs the internet, and this computer is not online right now. "
                     "The wishes you already made still work." },
        { "busy", "That is taking too long. Please try again in a moment." },
        { "limit", "You've made lots of wishes today \xE2\x80\x94 more tomorrow." },
        { "resting", "Wishes are resting for a little while." },
        { "refused", "That could not be sent. Try fewer words." },
        { "consent", "Wishes is switched off. Say yes to the short question first." },
        { "broken", "Something went wrong with Wishes. Nothing has changed." },
        { NULL, "Something went wrong with Wishes. Nothing has changed." } };
    int i = 0;
    while (T[i].c && (!code || strcmp(T[i].c, code))) i++;
    return T[i].t;
}

void answer_error(long long id, const char *code, int retry_after, char *r, size_t n)
{
    static const char *const KNOWN[] = { "offline", "busy", "limit", "resting", "refused",
                                         "consent", "broken", NULL };
    int ok = 0;
    for (int i = 0; code && KNOWN[i]; i++) ok |= !strcmp(code, KNOWN[i]);
    if (!ok) code = "broken";
    jout o = { r, n, 0, 0 };
    r[0] = 0;
    jw_raw(&o, "{"); jw_key(&o, "id"); jw_int(&o, id);
    jw_key(&o, "kind"); jw_str(&o, "error");
    jw_key(&o, "error"); jw_str(&o, code);
    jw_key(&o, "text"); jw_str(&o, answer_error_text(code));
    if (retry_after > 0) { jw_key(&o, "retry_after"); jw_int(&o, retry_after > 3600 ? 3600 : retry_after); }
    jw_raw(&o, "}");
}

static int page_name_ok(const char *s)
{
    size_t l = strlen(s);
    if (l < 1 || l > 64 || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= '0' && s[0] <= '9'))) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-')) return 0;
    return 1;
}

void answer_reply(engine *e, const cloud_result *r, const char *said, int voice,
                  const char *help_dir, char *reply, size_t n, long long now)
{
    if (r->http == 0) { answer_error(r->id, r->err ? r->err : "offline", 0, reply, n); return; }
    if (r->http == 429) { answer_error(r->id, "limit", r->retry_after, reply, n); return; }
    if (r->http == 503) { answer_error(r->id, "resting", r->retry_after, reply, n); return; }
    if (r->http == 400) { answer_error(r->id, "refused", 0, reply, n); return; }
    if (r->http != 200) { answer_error(r->id, "broken", 0, reply, n); return; }

    static char mem[96 * 1024];
    static const jlimits L = { 64 * 1024, 5, 400, 1024 };
    jarena a = { mem, sizeof mem, 0, NULL };
    const jv *o = json_parse(&a, &L, r->body, r->len);
    if (!o || o->type != J_OBJ) { answer_error(r->id, "broken", 0, reply, n); return; }

    if (r->kind == CLOUD_WISH) {
        const jv *k = jget(o, "kind");
        const char *kind = k && k->type == J_STR ? k->s : "";
        if (!strcmp(kind, "wish"))
            engine_offer(e, r->id, OFFER_WISH, NULL, jget(o, "wish"), said, voice, reply, n, now);
        else if (!strcmp(kind, "cannot")) {
            const jv *m = jget(o, "missing");
            engine_offer(e, r->id, OFFER_CANNOT, m && m->type == J_STR ? m->s : NULL,
                         jget(o, "alt_wish"), said, voice, reply, n, now);
        } else answer_error(r->id, "broken", 0, reply, n);
        return;
    }
    /* /v1/ask */
    const jv *p = jget(o, "help_page_id");
    char path[300];
    const char *page = NULL;
    if (p && p->type == J_STR && page_name_ok(p->s)) {
        snprintf(path, sizeof path, "%s/%s.html", help_dir ? help_dir : "/usr/share/auros/help", p->s);
        if (access(path, R_OK) == 0) page = p->s;
    }
    engine_offer(e, r->id, OFFER_HELP, page, jget(o, "wish"), said, voice, reply, n, now);
}
