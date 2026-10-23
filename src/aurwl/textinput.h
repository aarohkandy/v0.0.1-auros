/* textinput.h — zwp_text_input_manager_v3, so the compositor can tell a
 * password field from any other.
 *
 * WHY aurwl SPEAKS THIS PROTOCOL AT ALL. It exists for input methods, and
 * AurOS has none. It is here for one reason: docs/AI.md §3.5 says Wishes must
 * not match what she types into a password field, and the only honest way a
 * compositor learns that a field is a password field is that the program
 * says so. GTK (so Firefox) tells a compositor the purpose of the field it
 * is typing into through exactly this protocol -- but only if the compositor
 * has first said "enter" to it, which a compositor that does not speak it
 * never does.
 *
 * WHAT IT DOES AND DOES NOT DO. It tracks, for the program that has the
 * keyboard, whether a text input is enabled and what it declared its
 * content to be. It never sends preedit or commit_string (there is no input
 * method), never stores the surrounding text a client offers (that is what
 * she has typed around the cursor), and has no effect on the keys a client
 * receives: they still arrive through wl_keyboard as before.
 *
 * LINKED ONLY WITH WISHES (wishes="on"; D14). aurwl.c reaches it through
 * weak references, so an image without Wishes does not advertise the
 * protocol and its programs behave exactly as before. */
#ifndef AUROS_TEXTINPUT_H
#define AUROS_TEXTINPUT_H

struct wl_display;
struct wl_client;
struct wl_global;
struct wl_resource;

enum { TI_UNKNOWN,       /* the focused program has no text input enabled */
       TI_PLAIN,         /* one is enabled and says it is ordinary text   */
       TI_SENSITIVE };   /* one is enabled and says password, PIN, hidden,
                          * sensitive, a terminal, or something we do not know */

void textinput_init(struct wl_display *d);
void textinput_shutdown(void);

/* The keyboard focus is now `surface` of `client` (both NULL: nobody). Sends
 * leave to the old one and enter to the new one. */
void textinput_focus(struct wl_client *client, struct wl_resource *surface);

/* What the program with the keyboard has declared (above). */
int  textinput_state(void);

/* For tests and the fuzzer: the global (to number it in the registry), and
 * whether anything is left behind once every program has gone (text
 * inputs, a remembered focus). */
extern struct wl_global *textinput_g;
int  textinput_leftovers(void);
/* The enabled text input's raw content type (0 if none): diagnostics only. */
int  textinput_peek(unsigned *hint, unsigned *purpose);

#endif
