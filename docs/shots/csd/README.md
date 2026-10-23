# One set of window buttons (stream C)

Real programs on the real shell (`tools/wltest-apps` harness: `aurshell --png --with-app`,
Firefox 158 and GTK 4's widget factory), before and after `c7e0b6d`, at 1366x768 and
1024x600, in each look. Firefox and GTK programs draw their own title bar; the shell used to
draw a second one above it.

- `before-*`: two bars, two sets of minimise / maximise / close (taskbar, dock), or a masthead
  over the program's own bar (rail).
- `after-*`: the program's own bar only. Like Windows: minimise, maximise, close. Like a Mac:
  minimise, close. Big and simple / One thing at a time: no window buttons (the footer's
  "Close this" is the way out). A program that asks to fill the screen (Firefox does on a small
  screen) is maximised in Like Windows.
