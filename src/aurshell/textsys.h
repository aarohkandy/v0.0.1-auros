/* textsys.h — "Bigger" for the programs too, not only the desktop.
 *
 * The band's Bigger and Smaller wrote ~/.config/auros/text-scale and
 * nothing outside the shell ever read it: the bar, the menus and the
 * question grew, and Internet, the store and every document stayed
 * exactly as small as before -- while the Help panel said the buttons
 * change "the words everywhere" (people-walkthrough F-01).
 *
 * Her size is now also handed to the toolkit her programs use:
 *   - GNOME's text-scaling-factor (gsettings), which GTK on Wayland
 *     turns into its font resolution and which Firefox follows;
 *   - gtk-xft-dpi in her GTK 3 and 4 settings.ini, for a machine with
 *     no settings daemon to ask.
 * A program that is already open keeps its size until it is reopened
 * (or follows the change by itself, as GTK programs do).
 */
#ifndef AUROS_TEXTSYS_H
#define AUROS_TEXTSYS_H
#include <stddef.h>

/* The settings.ini half, into <home>/.config/gtk-{3,4}.0/. Replaces or
 * adds one line and keeps every other. 0 when both were written. */
int  textsys_write_ini(const char *home, float scale);

/* The command that sets the toolkit's scale: argv ends in `buf`, which
 * holds the number. Pure, so tests can read it. */
void textsys_gsettings_argv(float scale, char *buf, size_t n, const char *argv[6]);

/* Both, for her own account ($HOME). Cheap and never waits: the
 * command is started detached. */
void textsys_apply(float scale);

/* gtk-xft-dpi for a scale: 96 dpi in 1024ths, times the scale. */
int  textsys_xft_dpi(float scale);

/* WINDOW BUTTONS.
 *
 * Firefox and GTK programs draw their own title bar with their own
 * buttons, and the desktop draws none over them (session.c). So the
 * buttons they show must be exactly the ones this look's own windows
 * have, or one of them is dead: "Like Windows" has minimise, maximise and
 * close; "Like a Mac" has minimise and close; the two looks that have no
 * buttons on a window at all (Big and simple, One thing at a time) show
 * none, and "Close this" in the footer is the way out there.
 *
 * The string is GTK's gtk-decoration-layout: buttons after the colon sit
 * at the right. Pure, for the tests. Any look it does not know has none. */
const char *textsys_buttons_for(const char *layout_id);

/* Written to her GTK 3 and 4 settings.ini, and handed to GNOME's
 * button-layout (gsettings), which running GTK programs follow at once.
 * Cheap and never waits. */
int  textsys_write_buttons(const char *home, const char *layout_id);
void textsys_buttons(const char *layout_id);
#endif
