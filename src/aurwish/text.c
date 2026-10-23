/* text.c — see text.h. The shipped lists live at the bottom. */
#include <string.h>
#include "text.h"

unsigned utf8_next(const char **sp)
{
    const unsigned char *s = (const unsigned char *)*sp;
    unsigned c = s[0];
    if (!c) return 0;
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    if (n == 2) c &= 0x1F; else if (n == 3) c &= 0x0F; else if (n == 4) c &= 0x07;
    for (int i = 1; i < n; i++) {
        if (!s[i]) { *sp += i; return 0xFFFD; }      /* truncated: never valid */
        c = (c << 6) | (s[i] & 0x3F);
    }
    *sp += n;
    return c;
}

static int put(char *d, unsigned c)
{
    if (c < 0x80) { d[0] = (char)c; return 1; }
    if (c < 0x800) { d[0] = (char)(0xC0 | c >> 6); d[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        d[0] = (char)(0xE0 | c >> 12); d[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        d[2] = (char)(0x80 | (c & 0x3F)); return 3;
    }
    d[0] = (char)(0xF0 | c >> 18); d[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    d[2] = (char)(0x80 | ((c >> 6) & 0x3F)); d[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

static int combining(unsigned c)
{
    return (c >= 0x300 && c <= 0x36F) || (c >= 0x1AB0 && c <= 0x1AFF) ||
           (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x20D0 && c <= 0x20FF) ||
           (c >= 0xFE20 && c <= 0xFE2F);
}

/* ponytail: composition covers base letter + one of seven accents into
 * Latin-1, which is French, Spanish, Portuguese, German and Italian.
 * Anything else stays decomposed, and decomposed text that is not in
 * her words is refused -- a safe failure. Full NFC tables when a
 * template language needs them. */
static unsigned compose(unsigned b, unsigned m)
{
    static const struct { unsigned m; const char *from, *to; } T[] = {
        { 0x300, "AEIOUaeiou", "\xC0\xC8\xCC\xD2\xD9\xE0\xE8\xEC\xF2\xF9" },
        { 0x301, "AEIOUYaeiouy", "\xC1\xC9\xCD\xD3\xDA\xDD\xE1\xE9\xED\xF3\xFA\xFD" },
        { 0x302, "AEIOUaeiou", "\xC2\xCA\xCE\xD4\xDB\xE2\xEA\xEE\xF4\xFB" },
        { 0x303, "ANOano", "\xC3\xD1\xD5\xE3\xF1\xF5" },
        { 0x308, "AEIOUaeiouy", "\xC4\xCB\xCF\xD6\xDC\xE4\xEB\xEF\xF6\xFC\xFF" },
        { 0x30A, "Aa", "\xC5\xE5" },
        { 0x327, "Cc", "\xC7\xE7" },
    };
    if (b > 0x7F) return 0;
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        if (T[i].m != m) continue;
        const char *p = strchr(T[i].from, (int)b);
        if (p) return (unsigned char)T[i].to[p - T[i].from];
    }
    return 0;
}

int text_nfc(const char *in, char *out, size_t n)
{
    size_t w = 0;
    unsigned prev = 0;
    size_t prev_at = 0;
    const char *s = in;
    for (unsigned c; (c = utf8_next(&s)); ) {
        unsigned k = prev ? compose(prev, c) : 0;
        if (k) { w = prev_at; c = k; }
        if (w + 4 >= n) return -1;
        prev_at = w;
        w += (size_t)put(out + w, c);
        prev = k ? 0 : c;
    }
    out[w] = 0;
    return (int)w;
}

static int joiner_like(unsigned c)
{
    return combining(c) || (c >= 0xFE00 && c <= 0xFE0F) ||       /* variation sel. */
           (c >= 0x1F3FB && c <= 0x1F3FF) || c == 0x200D ||       /* skin tone, ZWJ */
           (c >= 0xE0020 && c <= 0xE007F) || c == 0x20E3;         /* tags, keycap */
}

int text_graphemes(const char *s)
{
    int g = 0;
    unsigned prev = 0;
    for (unsigned c; (c = utf8_next(&s)); prev = c)
        if (!joiner_like(c) && prev != 0x200D) g++;
    return g;
}

static unsigned fold(unsigned c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x17F && !(c & 1)) return c + 1;
    if (c >= 0x391 && c <= 0x3A9) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

/* 1 Latin, 2 Greek, 3 Cyrillic, 0 not a letter. */
static int script(unsigned c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
    if (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) return 1;
    if (c >= 0x386 && c <= 0x3CE) return 2;
    if (c >= 0x400 && c <= 0x4FF) return 3;
    return 0;
}

static int is_digit(unsigned c) { return c >= '0' && c <= '9'; }
/* A byte that belongs to a word: ASCII letter or digit, or any byte of
 * a longer UTF-8 letter. */
static int alnum_byte(char b)
{
    unsigned char c = (unsigned char)b;
    return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c >= 0x80;
}
static int word_char(unsigned c) { return script(c) || is_digit(c) || combining(c) || c == '\''; }

/* Punctuation a card may carry. Not '*' (the sentence's bold marker),
 * not '@' or '/' (addresses), not currency, not '#' '+' '=' '<' '>'. */
static int punct_ok(unsigned c)
{
    /* c < 0x80 first: strchr() takes a char, and U+202E would arrive
     * as '.', U+E000 as the terminator it always finds. */
    return c == ' ' || (c && c < 0x80 && strchr(".,!?'\"-:;()&%", (int)c)) ||
           c == 0x2013 || c == 0x2014 || c == 0x2026;
}

int text_label_ok(const char *s, int max_g)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!*p) return 0;
    while (*p) {
        unsigned c = *p;
        int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 :
                (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!n) return 0;
        unsigned min = n == 2 ? 0x80 : n == 3 ? 0x800 : 0x10000;
        if (n > 1) c &= (0xFFu >> (n + 1));
        for (int i = 1; i < n; i++) {
            if ((p[i] & 0xC0) != 0x80) return 0;
            c = (c << 6) | (p[i] & 0x3F);
        }
        if (n > 1 && c < min) return 0;
        if (c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == '*') return 0;
        if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) return 0;
        if ((c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||
            (c >= 0x2060 && c <= 0x206F) || c == 0xFEFF || c == 0x061C ||
            c == 0x2028 || c == 0x2029 || c == 0x00AD || c == 0x034F ||
            (c >= 0xE0000 && c <= 0xE007F) || (c >= 0xE000 && c <= 0xF8FF) ||
            c >= 0xF0000 || (c >= 0xFDD0 && c <= 0xFDEF) || (c & 0xFFFE) == 0xFFFE)
            return 0;
        p += n;
    }
    return text_graphemes(s) <= max_g;
}

int text_eq(const char *a, const char *b)
{
    for (;;) {
        unsigned x = fold(utf8_next(&a)), y = fold(utf8_next(&b));
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* ── the shipped word lists ─────────────────────────────────────────── */

/* Words the cloud may add that are not hers. Deliberately NOT "not",
 * "don't", "never", "no": a filler must not be able to turn "take your
 * pills" into "do not take your pills". */
static const char *const FILLER[] = {
    "a", "an", "the", "to", "of", "for", "at", "in", "on", "and", "your",
    "you", "my", "me", "i", "it", "is", "it's", "time", "now", "please",
    "hello", "hi", "good", "morning", "afternoon", "evening", "reminder",
    "remember", NULL };

/* Words no card may carry, whoever said them (§7.3). Stems: "hack"
 * covers hacked, hacker, hacking. False positives ("applesauce") are a
 * refusal, which is the safe way to be wrong. */
static const char *const BLOCKED[] = {
    "virus", "infect", "hack", "polic", "bank", "microsoft", "apple",
    "auros", "support", "passw", "scam", "refund", "giftcard", NULL };

/* Money amounts on a card (§7.3). Currency symbols are already outside
 * the characters a card may carry; these are the amounts spelled out.
 * NOT "pay": "remind me to pay the gas bill" is a reminder she needs. */
static const char *const AMOUNT[] = {
    "dollar", "dollars", "euro", "euros", "pound", "pounds", "cent", "cents",
    "buck", "bucks", "usd", "eur", "gbp", "bitcoin", "btc", NULL };

/* Money words a trigger may not be (§7.4), on top of AMOUNT. */
static const char *const MONEY[] = {
    "crypto", "money", "cash", "payment", "paypal", "venmo", "zelle", "wire",
    "transfer", "invoice", "giftcard", NULL };

/* Bank and login words: a trigger on one of these would announce to
 * anyone watching the screen that she had just typed it. */
static const char *const LOGIN[] = {
    "bank", "banking", "account", "password", "passcode", "passphrase",
    "login", "logon", "signin", "username", "credit", "debit", "card",
    "cards", "wallet", "iban", "swift", "routing", "sortcode", "verify",
    "verification", "code", "secret", "private", "admin", "root", "sudo",
    "security", "social", NULL };

/* Common words: a trigger on one fires all day long. */
static const char *const STOP[] = {
    "that", "this", "with", "have", "from", "they", "what", "when", "your",
    "will", "would", "there", "their", "about", "which", "could", "other",
    "these", "those", "because", "people", "thing", "things", "think",
    "just", "like", "know", "time", "into", "some", "than", "then", "them",
    "were", "been", "only", "also", "more", "most", "very", "here", "well",
    "much", "even", "make", "made", "good", "want", "need", "said", "says",
    "going", "really", "yeah", "okay", "thanks", "thank", "please", "hello",
    "dear", "does", "doing", "done", "each", "every", "after", "before",
    "again", "still", "where", "while", "should", "shall", "must", "might",
    "over", "under", "back", "down", "come", "came", "take", "took", "give",
    "gave", "look", "find", "tell", "told", "today", "tomorrow", "yesterday",
    "year", "years", "week", "weeks", "day", "days", "home", "work", "please",
    "sure", "maybe", "never", "always", "something", "nothing", "anything",
    "everything", "someone", "anyone", "everyone", "best", "regards",
    "kind", "love", "lots", "many", "same", "such", "both", "into", "onto",
    "upon", "within", "without", "through", "around", "though", "although",
    NULL };

static int in_list(const char *const *l, const char *w)
{
    for (int i = 0; l[i]; i++) if (!strcmp(l[i], w)) return 1;
    return 0;
}

static int blocked(const char *w)
{
    for (int i = 0; BLOCKED[i]; i++)
        if (!strncmp(w, BLOCKED[i], strlen(BLOCKED[i]))) return 1;
    return 0;
}

/* Splits NFC'd text into folded words: "Don’t" -> "don't". Returns the
 * count, or -1 if a word mixes scripts or is too long. */
#define MAXW 80
#define WLEN 64
static int words(const char *s, char w[][WLEN], int max)
{
    int n = 0;
    char cur[WLEN];
    size_t cl = 0;
    int sc = 0;
    for (;;) {
        unsigned c = utf8_next(&s);
        if (c == 0x2019 || c == 0x2018) c = '\'';
        if (c && word_char(c)) {
            int k = script(c);
            if (k && sc && k != sc) return -1;     /* "Αpple": Greek A */
            if (k) sc = k;
            if (cl + 4 >= WLEN) return -1;         /* not a word anyone says */
            cl += (size_t)put(cur + cl, fold(c));
            continue;
        }
        /* A word ends. Apostrophes at its edges are quotes, not letters. */
        cur[cl] = 0;
        char *b = cur;
        while (*b == '\'') b++;
        size_t bl = strlen(b);
        while (bl && b[bl - 1] == '\'') b[--bl] = 0;
        if (bl) {
            if (n >= max) return -1;
            memcpy(w[n++], b, bl + 1);
        }
        cl = 0; sc = 0;
        if (!c) return n;
    }
}

const char *text_free(const char *t, const char *said, int max_g, size_t max_bytes,
                      char *out, size_t n)
{
    if (!t || !said) return "missing";
    if (text_nfc(t, out, n) < 0) return "too long";
    /* Straight quotes, so "don’t" and "don't" are one word and the card
     * shows what the confirmation showed. */
    {
        char tmp[1024];
        size_t w = 0;
        const char *s = out;
        for (unsigned c; (c = utf8_next(&s)); ) {
            if (c == 0x2018 || c == 0x2019) c = '\'';
            if (c == 0x201C || c == 0x201D) c = '"';
            if (w + 4 >= sizeof tmp) return "too long";
            w += (size_t)put(tmp + w, c);
        }
        tmp[w] = 0;
        if (w + 1 > n) return "too long";
        memcpy(out, tmp, w + 1);
    }
    if (!*out) return "empty";
    if (strlen(out) > max_bytes) return "too many bytes";
    if (text_graphemes(out) > max_g) return "too long";

    int marks = 0, any = 0;
    const char *s = out;
    for (unsigned c; (c = utf8_next(&s)); ) {
        /* After composition a letter keeps at most one loose accent;
         * more is text drawn over the text around it. */
        if (combining(c)) { if (++marks > 1) return "stacked accents"; continue; }
        marks = 0;
        if (!script(c) && !is_digit(c) && !punct_ok(c)) return "character not allowed";
        if (c != ' ') any = 1;
    }
    if (!any) return "empty";

    /* Five or more digits in a row, counting across the gaps people put
     * in phone numbers: "0800 123 456" is eleven. */
    int run = 0;
    for (const char *p = out; *p; p++) {
        if (*p >= '0' && *p <= '9') { if (++run >= 5) return "long number"; }
        else if (!strchr(" -.()", *p)) run = 0;
    }
    /* Addresses: "example.com", "www", "http". */
    for (const char *p = out + 1; *p; p++)
        if (*p == '.' && alnum_byte(p[-1]) && alnum_byte(p[1]))
            return "looks like an address";

    static char mine[MAXW][WLEN], hers[MAXW][WLEN];
    char said_nfc[1024];
    if (text_nfc(said, said_nfc, sizeof said_nfc) < 0) return "her text too long";
    int nm = words(out, mine, MAXW), nh = words(said_nfc, hers, MAXW);
    if (nm < 0) return "mixed scripts or a word too long";
    if (nh < 0) nh = 0;
    for (int i = 0; i < nm; i++) {
        const char *w = mine[i];
        if (!strcmp(w, "www") || !strcmp(w, "http") || !strcmp(w, "https"))
            return "looks like an address";
        if (blocked(w)) return "blocked word";
        if (in_list(AMOUNT, w)) return "money amount";
        if (in_list(FILLER, w)) continue;
        int found = 0;
        for (int j = 0; j < nh && !found; j++) found = !strcmp(w, hers[j]);
        if (!found) return "not her words";
    }
    return NULL;
}

const char *word_rule(const char *w, char out[33])
{
    size_t l = strlen(w);
    if (l < 4) return "shorter than 4 letters";
    if (l > 32) return "longer than 32 letters";
    for (size_t i = 0; i < l; i++) {
        char c = w[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c < 'a' || c > 'z') return "not a plain word";
        out[i] = c;
    }
    out[l] = 0;
    if (in_list(STOP, out)) return "too common";
    if (in_list(AMOUNT, out) || in_list(MONEY, out) || in_list(LOGIN, out) || blocked(out))
        return "money or login word";
    return NULL;
}

/* ── emoji (§7.3: about 60 friendly ones; the owner's list) ───────── */

static const char *const EMOJI[] = {
    /* food */
    "🥔", "🍎", "🍌", "🍓", "🍒", "🍉", "🍕", "🍰", "🎂", "🍪", "🍩", "🍦",
    "🥕", "🌽", "🍋", "☕", "🍫", "🥐", "🍇", "🍊",
    /* animals */
    "🐱", "🐶", "🐰", "🐻", "🐼", "🐸", "🐝", "🦋", "🐞", "🐢", "🐠", "🐬",
    "🦆", "🐧", "🦉", "🐴", "🐮", "🐷", "🐔",
    /* hearts */
    "❤️", "💛", "💚", "💙", "💜", "💖", "💕",
    /* flowers and trees */
    "🌸", "🌹", "🌻", "🌷", "🌼", "🍀", "🌳", "🍁",
    /* stars and weather */
    "⭐", "🌟", "✨", "💫", "☀️", "🌈", "☁️", "❄️", "☔", "🌙", "⛄",
    /* celebrations and cheer */
    "🎉", "🎈", "🎁", "🎊", "🥳", "🎵", "🎶", "🙂", "😊", "😄", "👍", "👏", "🔔",
    NULL };

const char *emoji_allowed(const char *e)
{
    if (!e) return NULL;
    for (int i = 0; EMOJI[i]; i++) {
        if (!strcmp(e, EMOJI[i])) return EMOJI[i];
        /* "❤" without its U+FE0F is the same emoji, spelled as text. */
        size_t l = strlen(EMOJI[i]);
        if (l > 3 && !strcmp(EMOJI[i] + l - 3, "\xEF\xB8\x8F") &&
            strlen(e) == l - 3 && !strncmp(e, EMOJI[i], l - 3))
            return EMOJI[i];
    }
    return NULL;
}

/* ── sites (§7.2: only these, or her bookmarks) ────────────────────── */

static const struct { const char *names, *url; } SITES[] = {
    { "Google|Google Search", "https://www.google.com/" },
    { "YouTube", "https://www.youtube.com/" },
    { "Wikipedia", "https://www.wikipedia.org/" },
    { "BBC|BBC News", "https://www.bbc.co.uk/news" },
    { "BBC Weather|Weather", "https://www.bbc.co.uk/weather" },
    { "Gmail|Google Mail", "https://mail.google.com/" },
    { "Outlook|Hotmail|Outlook Mail", "https://outlook.live.com/" },
    { "Yahoo|Yahoo Mail", "https://mail.yahoo.com/" },
    { "Facebook", "https://www.facebook.com/" },
    { "WhatsApp|WhatsApp Web", "https://web.whatsapp.com/" },
    { "Google Maps|Maps", "https://maps.google.com/" },
    { "Google Photos|Photos", "https://photos.google.com/" },
    { "Google Calendar|Calendar", "https://calendar.google.com/" },
    { "Netflix", "https://www.netflix.com/" },
    { "Spotify", "https://open.spotify.com/" },
    { "CNN", "https://www.cnn.com/" },
    { "The Guardian|Guardian", "https://www.theguardian.com/" },
    { "Wordle", "https://www.nytimes.com/games/wordle/index.html" },
    { "NHS", "https://www.nhs.uk/" },
    { "Zoom", "https://zoom.us/join" },
    { NULL, NULL } };

int site_find(const char *site, const char **name, const char **url)
{
    if (!site) return -1;
    for (int i = 0; SITES[i].names; i++) {
        const char *p = SITES[i].names;
        while (*p) {
            const char *bar = strchr(p, '|');
            size_t l = bar ? (size_t)(bar - p) : strlen(p);
            char one[64];
            if (l < sizeof one) {
                memcpy(one, p, l); one[l] = 0;
                if (text_eq(one, site)) {
                    /* The first name is the one she is shown. */
                    static char first[64];
                    const char *b0 = strchr(SITES[i].names, '|');
                    size_t l0 = b0 ? (size_t)(b0 - SITES[i].names) : strlen(SITES[i].names);
                    memcpy(first, SITES[i].names, l0); first[l0] = 0;
                    *name = first;
                    *url = SITES[i].url;
                    return 0;
                }
            }
            p += l;
            if (*p == '|') p++;
        }
    }
    return -1;
}
