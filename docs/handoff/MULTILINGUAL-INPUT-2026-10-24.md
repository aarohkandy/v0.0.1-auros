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

## Retained-image experiment results

The diagnostic-only gate and typed `x11_panel_probe` dispatch option are now
implemented. A `[profiles:diagnostic]` push skips image builds only for the
explicit test/workflow/documentation allowlist. Mixed production changes and
renames out of production paths force normal profile builds. The gate was
verified on the actual push; no replacement image was built.

Run `40133525711` stopped before boot because the Actions downloader nested
the one validated artifact under its name. `7d7054b0` explicitly flattens
that single-artifact download and avoids publishing nonexistent boot evidence
when a preflight step failed.

Run `40133696393` then verified and restored the retained image and executed
the diagnostic. It established five seconds without a default panel, a
separately launched panel retaining PID 6199, termination of all three
temporary processes, and restoration of the shipped daemon with exact
arguments and the live Pinyin engine. The XRes inventory failed both before
and during preedit. The probe therefore stopped before canonical composition
and returned failure; no candidate identity/visibility claim is justified.
The published diagnostic records image source `c1ba6ee5` and test source
`7d7054b0` separately. Its small evidence archive contains the underlying
errors for the next repair.

Security review further requires per-window/map-generation XRes verification,
not cached trust by client resource base, because X IDs can be reused. Bind
to a focus epoch, revoke on every focus transition, and require a real
unmap/remap before rebinding. If the real panel remains mapped across focus
changes, a narrow popup exception is insufficient; do not relax those rules.

Run `40134236489` passed after correcting QueryExtension's wire offsets.
XRes 1.2 authenticated PID 6248's candidate window `0x0060000f`, mapped
override-redirect, 635 by 77 at (0,0). Default-panel quiescence, exact spawned
PID, complete transient-process cleanup, shipped IBus restoration, and both
canonical composition rounds passed. This proves identity, not chooser
visibility.

The next retained-image diagnostic proves native application focus A to B to
A with an exact QMP-injected saved byte. It requires the old candidate to
unmap and every panel window to be unmapped away; return requires exactly
one freshly XRes-authenticated candidate from the retained process. A new
XID is acceptable with fresh authentication. Mapped-away or ambiguous states
stop the experiment. Sixteen parser tests, 47 harness checks and independent
security review pass; runtime lifecycle evidence is still pending.
