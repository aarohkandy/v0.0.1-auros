/* answer.h — from what the cloud said to what the shell is told.
 *
 * Pure: no network, no clock, no files except one stat() of a help page.
 * Everything arriving here is hostile (docs/AI.md §7.1), so the only
 * things taken from it are: whether it is a wish document (which the
 * engine then checks completely), a `missing` word (which must be one we
 * know), a help page name (which must be a plain name of a file that
 * exists), and a retry delay (which is clamped). Every sentence in the
 * reply is the device's. */
#ifndef AURWISH_ANSWER_H
#define AURWISH_ANSWER_H

#include <stddef.h>
#include "cloud.h"
#include "engine.h"

/* `help_dir` is /usr/share/auros/help (NULL = that). */
void answer_reply(engine *e, const cloud_result *r, const char *said, int voice,
                  const char *help_dir, char *reply, size_t n, long long now);

/* The device's sentence for an error code ("offline", "busy", "limit",
 * "resting", "refused", "consent", "broken"). */
const char *answer_error_text(const char *code);
/* {"id":N,"kind":"error","error":"…","text":"…"[,"retry_after":S]} */
void answer_error(long long id, const char *code, int retry_after, char *reply, size_t n);

#endif
