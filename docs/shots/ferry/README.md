# The Ferry screens (stream C)

Her files, as the first-start panel and Settings say it (docs/FERRY.md), rendered by
`tools/journeytest.sh` (the shipping paint code, the real Light theme, Atkinson). Every
look at both sizes is rendered and its words checked; these are the taskbar look at 1366x768
and the rail look at 1024x600 (the panel is opaque paper, so the looks differ only in the band
and the theme).

| file | screen | when |
|---|---|---|
| `*-4-whose` | Whose files? | Windows has more than one person (`result=choose`); one button per person, hers first |
| `*-5-bringing` | Bringing your files across | while it runs: the newest plain line (never a file's name) |
| `*-6-empty` | No files to bring across | `result=empty`: calm, not an error |
| `*-7-stick-plug` | Plug in a USB stick | Settings > Put Windows back > Save my files to a USB stick first; finds the stick by itself |
| `*-8-stick-pick` | Which USB stick? | one button per stick, by name and size |
| `*-9-stick-saving` | Saving your files | progress line; leave the stick in |
| `*-10-stick-done` | Your files are on the USB stick | safe to take it out; says Windows is still not put back |
| `*-11-blocked` | Windows was not put back | Put Windows back could not save her files in Windows (`why=toomany/locked/noroom/dirty/nodrive/some`) |
