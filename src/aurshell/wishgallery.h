/* wishgallery.h — wishes she can make by pressing, with no internet
 * (docs/AI.md §1.6 and §7 change 9).
 *
 * "Wishes she has already made run with no network at all." The same
 * has to be true of MAKING one for the common cases: when the service is
 * resting for the day, or the Wi-Fi is down, she can still press her way
 * to "remind me at 9: take my pills". About twenty templates; each is a
 * line she presses, then up to two blanks she fills by pressing again
 * (which picture, which word, what time, which message).
 *
 * What comes out is an ordinary v1 wish document plus the sentence she
 * would have said, both built from fixed tables below -- nothing she
 * typed, nothing from the cloud. It goes to the engine as op "check",
 * which checks it exactly as it checks a wish from the cloud and writes
 * its own sentence for the confirmation. A template the engine would
 * refuse is a bug in this table: tools/wishgallerytest.c runs every
 * option of every blank through the engine's own validator.
 *
 * Pure data and string building: no drawing, no socket, no clock.
 */
#ifndef AUROS_WISHGALLERY_H
#define AUROS_WISHGALLERY_H

#include <stddef.h>

#define WG_MAX_SLOTS 2

typedef struct {
    const char *label;      /* what she sees on the button                */
    const char *value;      /* what goes into the wish document           */
    const char *say;        /* what goes into her sentence                */
} wg_opt;

typedef struct {
    const char   *ask;      /* "Which picture?"                           */
    const wg_opt *opts;
    int           n;
    int           pic;      /* the values are emoji: draw them            */
} wg_slot;

typedef struct {
    const char *pick;       /* the line she presses                       */
    int         n_slots;
    int         slot[WG_MAX_SLOTS];   /* indexes for wg_slot_get()        */
} wg_tmpl;

int            wg_count(void);
const wg_tmpl *wg_get(int i);
const wg_slot *wg_slot_get(int id);

/* Her sentence and the wish document for template `t` with option
 * choice[k] for its k-th blank. 0 on success, -1 if anything is out of
 * range or does not fit. */
int wg_build(int t, const int *choice, char *said, size_t sn,
             char *doc, size_t dn);

#endif
