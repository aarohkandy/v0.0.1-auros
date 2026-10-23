#!/usr/bin/env python3
"""wishtest-gtk.py -- a GTK3 program with every kind of text field, moving the
cursor between them while tools/wishtest-gtk (the compositor) watches what the
program tells it. Run under that harness, not by hand.

    wishtest-gtk.py LOGFILE

Appends "FOCUS <name> <milliseconds>" each time a field takes the keyboard
focus; the compositor-side harness looks at the text-input state shortly
after and compares it with what that field should be.
"""
import sys, time
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib

log = open(sys.argv[1], "a", buffering=1)


def entry(purpose=None, hidden=False, name=""):
    e = Gtk.Entry()
    e.set_placeholder_text(name)
    if hidden:
        e.set_visibility(False)
    if purpose is not None:
        e.set_input_purpose(purpose)
    return e


fields = [
    ("plain", entry(name="plain")),
    ("hidden", entry(hidden=True, name="hidden")),          # GtkEntry visibility=False: what a password box is
    ("plain", None),
    ("pin", entry(Gtk.InputPurpose.PIN, name="pin")),
    ("email", entry(Gtk.InputPurpose.EMAIL, name="email")),
    ("textview", None),
    ("password", entry(Gtk.InputPurpose.PASSWORD, name="password")),
    ("search", Gtk.SearchEntry()),
    ("plain", None),
]
win = Gtk.Window(title="wishtest-gtk")
box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
widgets = {}
for name, w in fields:
    if w is None:
        w = widgets.get(name) or (Gtk.TextView() if name == "textview" else None)
    if w is not None and name not in widgets:
        widgets[name] = w
        box.pack_start(w, False, False, 4)
win.add(box)
win.set_default_size(300, 300)
win.connect("destroy", Gtk.main_quit)
win.show_all()

order = [n for n, _ in fields]
state = {"i": 0}


def step():
    i = state["i"]
    if i >= len(order):
        log.write("DONE %d\n" % int(time.monotonic() * 1000))
        GLib.timeout_add(500, Gtk.main_quit)
        return False
    name = order[i]
    widgets[name].grab_focus()
    log.write("FOCUS %s %d\n" % (name, int(time.monotonic() * 1000)))
    state["i"] += 1
    return True


GLib.timeout_add(1800, lambda: (GLib.timeout_add(1500, step), False)[1])
Gtk.main()
