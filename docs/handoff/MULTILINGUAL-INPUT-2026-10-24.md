# Multilingual input: proven composition, unfinished candidate UI

## Evidence

Candidate `c1ba6ee5`, profile run `40131473294`, passed exact Pinyin
composition in Mousepad and Firefox, followed by a reboot and the same checks
again. Both saved Mousepad files contain exactly `你好`; Firefox's submitted
request is checked independently. The profile workflow, artifact publication
and retained-image pruning all passed.

Evidence is in the `profiles-40131473294` prerelease. In particular:
`multilingual-ibus-evidence.tar.gz`, `multilingual-report.txt`, and four
`ibus-round*-candidate.png` screenshots. The image's source and bytes are
bound to `multilingual-image-v1.json` in the immutable Actions artifact.

The screenshots expose a separate usability gap. Mousepad shows inline
preedit, Firefox's screenshot is blank, and neither proves a visible
candidate chooser. A technical IBus notification remains on screen. The
host waits approximately two seconds between typing and its next screenshot
request, so immediate capture alone does not explain the Firefox image.
Do not describe this as complete multilingual usability.

## Diagnosis and rejected shortcuts

Noble's IBus 1.5.29 starts its GTK panel in Wayland mode without the input
method mode expected by the panel. This produces its desktop-session
notification. The installed version speaks input-method-v1/input-panel-v1;
the reviewed IBus 1.5.32 source adds input-method-v2 and also requires
virtual-keyboard-v1. AurWL does not currently provide either integration.
Its existing text-input-v3 path is tied to the Wishes build and lacks the
state relay needed for a complete compositor input-method implementation.

Disabling the panel, pretending to be GNOME, or merely changing startup
flags would hide symptoms or break the proven input path. Forcing an X11
panel alone is insufficient: the existing compositor deliberately suppresses
unrelated override-redirect popups, including a separate input panel over a
native Wayland application. Keep that general security rule.

## Next experiment, before product changes

Use the retained image and an explicitly diagnostic harness to establish
whether the installed X11 panel retains its launched process ID, whether
XRes 1.2 reports that exact server-derived ID, and how its candidate windows
map and position over a native Wayland application. Record errors, process
ancestry, absolute geometry and screenshots. Do not infer identity from
`_NET_WM_PID` or declare an invisible chooser usable.

If the experiment supports it, a narrowly authorized candidate surface may
be feasible. The proposed trust boundary is a sanitized, directly launched
panel child owned by the shell/compositor, retained process identity and
server-derived XRes identity. Executable path plus a user-service cgroup is
insufficient because the user service can be overridden. A candidate must
bind to the application focused when it maps, never take keyboard focus,
hide on focus loss or panel death, and have bounded geometry/window/query
counts. All unresolved identities remain hidden. This is a design under
investigation, not implemented security behavior.

Native input-method-v2 is a larger alternative: it needs an IBus upgrade,
secure inherited Wayland connection, bounded text state, keyboard grabs and
trusted virtual-keyboard relay, popup placement, crash/restart cleanup and
new adversarial protocol tests. It is not a one-flag repair on this image.

Any product repair requires a fresh image and certification. A retained-image
experiment always records separate image-source and test-source identities
and remains diagnostic.
