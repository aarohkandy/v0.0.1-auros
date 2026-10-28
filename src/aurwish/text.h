/* text.h — what words a wish may put in front of her (docs/AI.md §7.3).
 *
 * The cloud is untrusted (§7.1). Free text in a wish -- the words on a
 * card, the words the computer says -- is the one place model output
 * reaches her verbatim, so it must be made only of HER words: every word
 * must appear in what she asked for, apart from a few harmless fillers.
 * That one rule is what stops "Your computer is infected, call 0800..."
 * being written onto her screen by a model somebody talked round.
 *
 * Input is assumed to be valid UTF-8 without control characters: it has
 * already been through json.c. Everything here refuses rather than
 * repairs. */
#ifndef AURWISH_TEXT_H
#define AURWISH_TEXT_H

#include <stddef.h>

/* Decode one code point of known-valid UTF-8; advances *s. 0 at the end. */
unsigned utf8_next(const char **s);

/* Canonical composition for the letters v1 accepts (Latin-1 range):
 * "e" + U+0301 becomes "é". -1 if it does not fit. */
int text_nfc(const char *in, char *out, size_t n);

/* Grapheme clusters, counted the way v1 needs: a combining mark, a
 * variation selector, a skin tone, or anything joined by U+200D does not
 * start a new one. */
int text_graphemes(const char *s);

/* §7.3 free text. `said` is her own wish text. Writes the normalised
 * text to out. NULL when it may be shown; otherwise a short reason
 * (for logs and tests, never for her). */
const char *text_free(const char *t, const char *said, int max_graphemes,
                      size_t max_bytes, char *out, size_t n);

/* The shipped emoji allowlist. Returns the canonical spelling (with
 * U+FE0F where the emoji needs it) or NULL. */
const char *emoji_allowed(const char *e);

/* §7.4 trigger word: 4-32 letters a-z (any case), not a stop word, not a
 * money, bank or login word. Writes it lowercased. NULL when allowed. */
const char *word_rule(const char *w, char out[33]);

/* The shipped site list (§7.2: open_website names a site, never a URL).
 * Returns 0 and fills name/url when `site` names one. */
int site_find(const char *site, const char **name, const char **url);

/* A name from a file on this computer (a program's Name=, a theme's
 * name): strict UTF-8, no controls, bidi, zero-width or '*', at most
 * max_g graphemes. 1 when it may be shown. */
int text_label_ok(const char *s, int max_g);

/* Her own typing, before it is sent: strict UTF-8, no control characters
 * (a new line included), no surrogates or noncharacters. 1 when valid. */
int text_valid(const char *s);

/* Case-insensitive compare after folding (ASCII and Latin-1). */
int text_eq(const char *a, const char *b);

#endif
