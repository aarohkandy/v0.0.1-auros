/* wishgallery.c — see wishgallery.h. */
#include <stdio.h>
#include <string.h>

#include "wishgallery.h"

/* ── the blanks ──────────────────────────────────────────────────── */

/* Fifteen of the friendly emoji both sides agree on (docs/AI.md §7.3;
 * the engine's list is src/aurwish/text.c, and the test checks these
 * against it). */
static const wg_opt PIC[] = {
    { "Potato",    "\xF0\x9F\xA5\x94", "potatoes" },
    { "Apple",     "\xF0\x9F\x8D\x8E", "apples" },
    { "Cake",      "\xF0\x9F\x8D\xB0", "cakes" },
    { "Coffee",    "\xE2\x98\x95",     "cups of coffee" },
    { "Dog",       "\xF0\x9F\x90\xB6", "dogs" },
    { "Cat",       "\xF0\x9F\x90\xB1", "cats" },
    { "Heart",     "\xE2\x9D\xA4\xEF\xB8\x8F", "hearts" },
    { "Flower",    "\xF0\x9F\x8C\xB8", "flowers" },
    { "Sunflower", "\xF0\x9F\x8C\xBB", "sunflowers" },
    { "Star",      "\xE2\xAD\x90",     "stars" },
    { "Rainbow",   "\xF0\x9F\x8C\x88", "rainbows" },
    { "Party",     "\xF0\x9F\x8E\x89", "party poppers" },
};

/* Words she might type: four letters or more, and none of the common
 * words, money words or login words the engine refuses (§7.4). */
static const wg_opt WORD[] = {
    { "potato",   "potato",   "potato" },
    { "flowers",  "flowers",  "flowers" },
    { "sunshine", "sunshine", "sunshine" },
    { "birthday", "birthday", "birthday" },
    { "kitten",   "kitten",   "kitten" },
    { "puppy",    "puppy",    "puppy" },
    { "garden",   "garden",   "garden" },
    { "biscuit",  "biscuit",  "biscuit" },
};

/* Between 8 in the morning and 8 at night: sounds, speech and screens
 * from a clock are held in the quiet hours (§7.4), and a wish that is
 * held every time she makes it is a wish that looks broken. */
static const wg_opt TIME[] = {
    { "8:00 in the morning",  "08:00", "8:00 am" },
    { "9:00 in the morning",  "09:00", "9:00 am" },
    { "10:00 in the morning", "10:00", "10:00 am" },
    { "Noon",                 "12:00", "12:00 noon" },
    { "1:00 in the afternoon","13:00", "1:00 pm" },
    { "3:00 in the afternoon","15:00", "3:00 pm" },
    { "4:00 in the afternoon","16:00", "4:00 pm" },
    { "6:00 in the evening",  "18:00", "6:00 pm" },
    { "8:00 in the evening",  "20:00", "8:00 pm" },
};

/* A reminder is the one thing that is allowed in the quiet hours (§7.4
 * change 4), so it may be early or late. */
static const wg_opt TIMER[] = {
    { "7:00 in the morning",  "07:00", "7:00 am" },
    { "8:00 in the morning",  "08:00", "8:00 am" },
    { "9:00 in the morning",  "09:00", "9:00 am" },
    { "10:00 in the morning", "10:00", "10:00 am" },
    { "Noon",                 "12:00", "12:00 noon" },
    { "3:00 in the afternoon","15:00", "3:00 pm" },
    { "6:00 in the evening",  "18:00", "6:00 pm" },
    { "9:00 in the evening",  "21:00", "9:00 pm" },
};

static const wg_opt REMIND[] = {
    { "Take my pills",      "Take my pills",      "Take my pills" },
    { "Call my daughter",   "Call my daughter",   "Call my daughter" },
    { "Drink some water",   "Drink some water",   "Drink some water" },
    { "Have a cup of tea",  "Have a cup of tea",  "Have a cup of tea" },
    { "Go for a walk",      "Go for a walk",      "Go for a walk" },
    { "Water the plants",   "Water the plants",   "Water the plants" },
    { "Feed the cat",       "Feed the cat",       "Feed the cat" },
    { "Time to eat",        "Time to eat",        "Time to eat" },
};

static const wg_opt SAYING[] = {
    { "Good morning",       "Good morning",       "Good morning" },
    { "Good afternoon",     "Good afternoon",     "Good afternoon" },
    { "Time for lunch",     "Time for lunch",     "Time for lunch" },
    { "Time for a break",   "Time for a break",   "Time for a break" },
};

static const wg_opt NOTE[] = {
    { "Have a lovely day",     "Have a lovely day",     "Have a lovely day" },
    { "Remember to smile",     "Remember to smile",     "Remember to smile" },
    { "Time for a stretch",    "Time for a stretch",    "Time for a stretch" },
    { "Drink some water",      "Drink some water",      "Drink some water" },
};

static const wg_opt BATTERY[] = {
    { "Plug in the charger",   "Plug in the charger",   "Plug in the charger" },
    { "Time to plug in",       "Time to plug in",       "Time to plug in" },
};

static const wg_opt SOUND[] = {
    { "Birdsong",     "birds",  "birdsong" },
    { "A chime",      "chime",  "a chime" },
    { "A gentle bell","gentle", "a gentle bell" },
};

enum { S_PIC, S_WORD, S_TIME, S_TIMER, S_REMIND, S_SAYING, S_NOTE, S_BATTERY, S_SOUND, S_N };

#define SLOT(a, o, p) { a, o, (int)(sizeof o / sizeof o[0]), p }
static const wg_slot SLOTS[S_N] = {
    [S_PIC]     = SLOT("Which picture?",         PIC,     1),
    [S_WORD]    = SLOT("Which word?",            WORD,    0),
    [S_TIME]    = SLOT("What time?",             TIME,    0),
    [S_TIMER]   = SLOT("What time?",             TIMER,   0),
    [S_REMIND]  = SLOT("What should it say?",    REMIND,  0),
    [S_SAYING]  = SLOT("What should it say?",    SAYING,  0),
    [S_NOTE]    = SLOT("What should it say?",    NOTE,    0),
    [S_BATTERY] = SLOT("What should it say?",    BATTERY, 0),
    [S_SOUND]   = SLOT("Which sound?",           SOUND,   0),
};

/* ── the templates ───────────────────────────────────────────────── *
 * `say` is the sentence she would have said, `doc` the wish. "@1" and
 * "@2" are her first and second blank: the option's `say` in the first,
 * its `value` in the second. */
typedef struct {
    wg_tmpl     pub;
    const char *say;
    const char *doc;
} gtmpl;

#define NOW(...)  "{\"v\":1,\"when\":{\"type\":\"now\"},\"do\":[" __VA_ARGS__ "]}"

static const gtmpl T[] = {
    { { "Make the words bigger", 0, {0} },
      "Make the words bigger",
      NOW("{\"type\":\"set_text_size\",\"size\":\"bigger\"}") },
    { { "Make the words much bigger", 0, {0} },
      "Make the words much bigger",
      NOW("{\"type\":\"set_text_size\",\"size\":\"biggest\"}") },
    { { "Make the words normal size again", 0, {0} },
      "Make the words normal size again",
      NOW("{\"type\":\"set_text_size\",\"size\":\"normal\"}") },
    { { "Make the screen brighter", 0, {0} },
      "Make the screen brighter",
      NOW("{\"type\":\"brightness\",\"set_percent\":100}") },
    { { "Make the screen dimmer", 0, {0} },
      "Make the screen dimmer",
      NOW("{\"type\":\"brightness\",\"set_percent\":50}") },
    { { "Turn the sound up", 0, {0} },
      "Turn the sound up",
      NOW("{\"type\":\"volume\",\"set_percent\":70}") },
    { { "Turn the sound down", 0, {0} },
      "Turn the sound down",
      NOW("{\"type\":\"volume\",\"set_percent\":30}") },
    { { "Switch the night light on", 0, {0} },
      "Switch the night light on",
      NOW("{\"type\":\"night_light\",\"on\":true}") },
    { { "Switch the night light off", 0, {0} },
      "Switch the night light off",
      NOW("{\"type\":\"night_light\",\"on\":false}") },

    { { "Remind me at a time", 2, { S_TIMER, S_REMIND } },
      "Remind me at @1: @2",
      "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"@1\"},\"do\":[{\"type\":\"remind\",\"text\":\"@2\"}]}" },
    { { "Say something to me at a time", 2, { S_TIME, S_SAYING } },
      "Say @2 to me at @1",
      "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"@1\"},\"do\":[{\"type\":\"speak\",\"text\":\"@2\"}]}" },
    { { "Show me a message when I switch on", 1, { S_NOTE } },
      "Show me a message when I switch on: @1",
      "{\"v\":1,\"when\":{\"type\":\"startup\"},\"do\":[{\"type\":\"show_message\",\"text\":\"@1\"}]}" },
    { { "Play a sound when I switch on", 1, { S_SOUND } },
      "Play @1 when I switch on",
      "{\"v\":1,\"when\":{\"type\":\"startup\"},\"do\":[{\"type\":\"play_sound\",\"sound\":\"@1\"}]}" },
    { { "Fill my screen with pictures when I type a word", 2, { S_PIC, S_WORD } },
      "Fill my screen with @1 when I type @2",
      "{\"v\":1,\"when\":{\"type\":\"typed_word\",\"word\":\"@2\"},\"do\":[{\"type\":\"fill_screen\",\"emoji\":\"@1\",\"seconds\":3}]}" },
    { { "Fill my screen with pictures at a time", 2, { S_PIC, S_TIME } },
      "Fill my screen with @1 at @2",
      "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"@2\"},\"do\":[{\"type\":\"fill_screen\",\"emoji\":\"@1\",\"seconds\":3}]}" },
    { { "Play a gentle sound every hour", 0, {0} },
      "Play a gentle sound every hour",
      "{\"v\":1,\"when\":{\"type\":\"every\",\"minutes\":60},\"do\":[{\"type\":\"play_sound\",\"sound\":\"gentle\"}]}" },
    { { "Dim the screen at a time", 1, { S_TIME } },
      "Make the screen dimmer at @1",
      "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"@1\"},\"do\":[{\"type\":\"brightness\",\"set_percent\":40}]}" },
    { { "Make the screen brighter at a time", 1, { S_TIME } },
      "Make the screen brighter at @1",
      "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"@1\"},\"do\":[{\"type\":\"brightness\",\"set_percent\":100}]}" },
    { { "Switch the night light on at a time", 1, { S_TIME } },
      "Switch the night light on at @1",
      "{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"@1\"},\"do\":[{\"type\":\"night_light\",\"on\":true}]}" },
    { { "Tell me when the battery is low", 1, { S_BATTERY } },
      "Tell me when the battery is low: @1",
      "{\"v\":1,\"when\":{\"type\":\"battery_below\",\"percent\":20},\"do\":[{\"type\":\"show_message\",\"text\":\"@1\"}]}" },
};
#define N_T ((int)(sizeof T / sizeof T[0]))

int wg_count(void) { return N_T; }
const wg_tmpl *wg_get(int i) { return (i >= 0 && i < N_T) ? &T[i].pub : NULL; }
const wg_slot *wg_slot_get(int id) { return (id >= 0 && id < S_N) ? &SLOTS[id] : NULL; }

/* `pat` with @1 and @2 replaced. */
static int fill(const char *pat, const wg_opt *o1, const wg_opt *o2, int say,
                char *out, size_t n)
{
    size_t o = 0;
    for (const char *p = pat; *p; p++) {
        const wg_opt *o_ = NULL;
        if (p[0] == '@' && p[1] == '1') o_ = o1;
        else if (p[0] == '@' && p[1] == '2') o_ = o2;
        if (p[0] == '@' && (p[1] == '1' || p[1] == '2')) {
            if (!o_) return -1;
            const char *v = say ? o_->say : o_->value;
            size_t l = strlen(v);
            if (o + l + 1 >= n) return -1;
            memcpy(out + o, v, l);
            o += l;
            p++;
            continue;
        }
        if (o + 2 >= n) return -1;
        out[o++] = *p;
    }
    out[o] = 0;
    return 0;
}

int wg_build(int t, const int *choice, char *said, size_t sn, char *doc, size_t dn)
{
    if (t < 0 || t >= N_T) return -1;
    const wg_opt *o[WG_MAX_SLOTS] = { NULL, NULL };
    for (int k = 0; k < T[t].pub.n_slots; k++) {
        const wg_slot *s = wg_slot_get(T[t].pub.slot[k]);
        if (!s || !choice || choice[k] < 0 || choice[k] >= s->n) return -1;
        o[k] = &s->opts[choice[k]];
    }
    if (fill(T[t].say, o[0], o[1], 1, said, sn) < 0) return -1;
    if (fill(T[t].doc, o[0], o[1], 0, doc, dn) < 0) return -1;
    return 0;
}
