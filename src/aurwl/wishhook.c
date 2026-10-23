/* wishhook.c — see wishhook.h. */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "wishhook.h"
#include "textinput.h"

/* Weak: a build that links wishhook.c without textinput.c (the unit test)
 * has no way to know, and then every program is "unknown". */
int textinput_state(void) __attribute__((weak));

#define MAX_WORDS 10
#define MAX_APPS  20
#define WORD_MAX  32
#define QUEUE     8

/* evdev key codes (linux/input-event-codes.h), spelled out so this file
 * needs no kernel header. */
enum { K_ESC = 1, K_BACKSPACE = 14, K_TAB = 15, K_ENTER = 28, K_LCTRL = 29, K_LSHIFT = 42,
       K_RSHIFT = 54, K_LALT = 56, K_SPACE = 57, K_CAPS = 58, K_RCTRL = 97, K_KPENTER = 96,
       K_RALT = 100, K_LMETA = 125, K_RMETA = 126, K_COMPOSE = 127 };

static char  words[MAX_WORDS][WORD_MAX + 1];
static int   nwords;
static char  apps[MAX_APPS][128];
static int   napps;

/* The word being typed. `bad` means it is not a plain run of letters we
 * could match (a digit, an apostrophe, an accented letter, a cursor
 * movement happened): it cannot be reported, whatever it spells. */
static char  buf[WORD_MAX + 1];
static int   len, bad;
static int   was_sensitive;      /* the last key was typed into a secret field */

static char  qwords[QUEUE][WORD_MAX + 1];
static int   qw_head, qw_n;
static char  qapps[QUEUE][128];
static int   qa_head, qa_n;

static void wipe(void)
{
    /* Not memset: a compiler may drop a memset of a buffer it thinks is
     * dead. */
    volatile char *p = buf;
    for (size_t i = 0; i < sizeof buf; i++) p[i] = 0;
    len = 0;
}

void wishhook_set(const char *const *w, int nw, const char *const *a, int na)
{
    nwords = napps = 0;
    for (int i = 0; w && i < nw && nwords < MAX_WORDS; i++) {
        const char *s = w[i];
        size_t l = s ? strlen(s) : 0;
        int ok = l >= 4 && l <= WORD_MAX;
        for (size_t j = 0; ok && j < l; j++) ok = s[j] >= 'a' && s[j] <= 'z';
        if (ok) { memcpy(words[nwords], s, l + 1); nwords++; }
    }
    for (int i = 0; a && i < na && napps < MAX_APPS; i++) {
        const char *s = a[i];
        size_t l = s ? strlen(s) : 0;
        if (l > 0 && l < sizeof apps[0]) { memcpy(apps[napps], s, l + 1); napps++; }
    }
    wipe();
    bad = 0;
    /* A new list is a new question: nothing found under the old one is
     * reported under it. */
    qw_n = qw_head = 0;
    qa_n = qa_head = 0;
}

int wishhook_take_word(char out[33])
{
    if (!qw_n) return 0;
    memcpy(out, qwords[qw_head], WORD_MAX + 1);
    qw_head = (qw_head + 1) % QUEUE;
    qw_n--;
    return 1;
}

int wishhook_take_app(char out[128])
{
    if (!qa_n) return 0;
    memcpy(out, qapps[qa_head], 128);
    qa_head = (qa_head + 1) % QUEUE;
    qa_n--;
    return 1;
}

int wishhook_buffered(void) { return len; }

void wishhook_reset(void)
{
    wipe();
    bad = 0;
    was_sensitive = 0;
}

static void end_of_word(void)
{
    if (!bad && len) {              /* registered words are all 4+ letters (wishhook_set) */
        buf[len] = 0;
        for (int i = 0; i < nwords; i++)
            if (!strcmp(buf, words[i])) {
                if (qw_n < QUEUE) {
                    memcpy(qwords[(qw_head + qw_n) % QUEUE], buf, WORD_MAX + 1);
                    qw_n++;
                }
                break;
            }
    }
    wipe();
    bad = 0;
}

/* WHAT A FIELD TELLS US, AND WHAT IT DOES NOT. A program that sets the
 * purpose of a field (GTK: GtkEntry.input-purpose; Firefox: the HTML input
 * type) tells the compositor through text-input-v3, and tools/wishtest-gtk
 * proves GTK3 does: password and PIN arrive as purpose 8/9 with the hidden
 * and sensitive hints. But a plain GtkEntry with visibility turned off -- how
 * most GTK3 password boxes are made -- says NOTHING: purpose normal, no hint.
 * So the field's own word is not enough, and two more rules apply:
 *
 *   - programs whose job is secrets, or that ask for one in a dialog, never
 *     get matched, whatever their fields say;
 *   - a window whose title says it is asking for a password, a passphrase or
 *     a sign-in is not matched either (this also covers a browser tab on a
 *     login page).
 *
 * And a program that has announced no field at all is trusted only if it is
 * an ordinary one; a browser, or a program that will not say who it is,
 * is not. All matched as case-insensitive substrings. */
static const char *const NEVER_APPS[] = {
    "keepass", "bitwarden", "1password", "pinentry", "polkit", "seahorse", "gcr", "keyring",
    "password", "wallet", "login", "auth", "askpass", "ssh-ask", "gnome-initial", "unlock", NULL };
static const char *const BROWSER_APPS[] = {
    "firefox", "chrom", "brave", "vivaldi", "opera", "edge", "epiphany", "falkon", "midori",
    "librewolf", "browser", NULL };
static const char *const SECRET_TITLES[] = {
    "password", "passphrase", "passcode", "authenticat", "log in", "login", "sign in", "signin",
    "sign-in", "unlock", "credential", "verification code", "security code", "one-time",
    "two-factor", "2-factor", "recovery key", "secret", NULL };

static int has_any(const char *text, const char *const *list)
{
    char low[160];
    size_t i = 0;
    if (!text) return 0;
    for (; text[i] && i + 1 < sizeof low; i++) low[i] = (char)tolower((unsigned char)text[i]);
    low[i] = 0;
    for (int k = 0; list[k]; k++)
        if (strstr(low, list[k])) return 1;
    return 0;
}

/* docs/AI.md §3.5: no matching in a field that says it holds a secret, nor
 * in a program or window that is asking for one. */
static int sensitive_now(aurwl *c)
{
    aurwl_win *w = aurwl_focus(c);
    const char *app = aurwl_win_app_id(w);
    if (has_any(app, NEVER_APPS) || has_any(aurwl_win_title(w), SECRET_TITLES)) return 1;
    int st = textinput_state ? textinput_state() : TI_UNKNOWN;
    if (st == TI_SENSITIVE) return 1;
    if (st == TI_PLAIN) return 0;
    if (!app || !*app) return 1;                  /* a program that will not say who it is */
    return has_any(app, BROWSER_APPS);
}

void wishhook_key(aurwl *c, uint32_t code, int pressed)
{
    if (!nwords || !pressed) return;              /* nothing registered: do not even look */
    if (sensitive_now(c)) {                       /* a secret: drop what we hold, hold nothing */
        wipe();
        bad = 1;
        was_sensitive = 1;
        return;
    }
    if (was_sensitive) { wipe(); bad = 0; was_sensitive = 0; }
    switch (code) {
    case K_LSHIFT: case K_RSHIFT: case K_LCTRL: case K_RCTRL: case K_LALT: case K_RALT:
    case K_LMETA: case K_RMETA: case K_CAPS: case K_COMPOSE:
        return;                                    /* modifiers type nothing */
    case K_ENTER: case K_KPENTER: case K_TAB:
        end_of_word();
        return;
    case K_BACKSPACE:
        if (len > 0) { buf[--len] = 0; }
        else bad = 1;                              /* deleting something we did not see */
        return;
    case K_ESC:
        wipe();
        bad = 0;
        return;
    default:
        break;
    }
    char t[16];
    int n = aurwl_key_utf8(c, code, t, sizeof t);
    if (n <= 0) {                                  /* arrows, Home, a Ctrl chord, ... */
        wipe();
        bad = 1;                                   /* the next letters may be mid-word */
        return;
    }
    if (n == 1 && isalpha((unsigned char)t[0])) {
        if (len >= WORD_MAX) { bad = 1; return; }
        buf[len++] = (char)tolower((unsigned char)t[0]);
        buf[len] = 0;
        return;
    }
    /* A delimiter ends the word; anything else is part of one we will
     * not match ("potato1", "potato's", "potatoé"). */
    if (n == 1 && (isspace((unsigned char)t[0]) ||
                   (ispunct((unsigned char)t[0]) && t[0] != '\'' && t[0] != '-' && t[0] != '_'))) {
        end_of_word();
        return;
    }
    bad = 1;
}

void wishhook_app(const char *app_id)
{
    if (!napps || !app_id || !*app_id) return;
    for (int i = 0; i < napps; i++)
        if (!strcasecmp(app_id, apps[i])) {
            if (qa_n < QUEUE) {
                snprintf(qapps[(qa_head + qa_n) % QUEUE], 128, "%s", app_id);
                qa_n++;
            }
            return;
        }
}
