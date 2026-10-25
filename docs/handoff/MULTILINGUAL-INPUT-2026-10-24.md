# Multilingual input: production Fcitx composition proven, delivery bounded

## Current production-image diagnostic proof

Run `40156605186` passed 71/71 checks across two distinct real boots of
production image source `3629b114`, retained from `40148012574` (raw SHA-256
`42359c4847a1a696a21c5b5350f4574a2970bb1d4d8f7df42c8336157a1696fa`).
Harness source was `0be3a3bc`; artifact `12585600815` is retained locally at
`/tmp/auros-fcitx-40156605186.H0VP3e`.

Both rounds proved exact `你好` in Mousepad and Firefox, visible candidate
lists, authenticated Mousepad connection and A→B→A candidate revocation and
remapping. The shipped Fcitx service autostarted after reboot and retained
its measured identity. Standalone password input produced exact `nihao`,
zero composition events and continuous focus. Closed traces contained zero
popup creation/mapping/configuration/grabs in every phase, including browser
shutdown. PID/unit/cgroup/writer cleanup passed.

The immutable template was identical at every boundary. Runtime profile
bytes stayed unchanged within each round; across reboot only comments and
blank lines differed, with every ordered setting record byte-identical to
the template. The comparison rejects setting or order changes.

Limits: Firefox compositor connection identity and native form submission,
navigation and password-manager behavior remain unproven. Screenshot timing
varies: one password frame was blank, another showed two bullets; exact
entered value is proved by the independent server result, not the image.
This is retained production-image diagnostic proof, not multilingual Windows
delivery or physical-PC certification. Historical investigations follow.

## Historical initial evidence

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

The retained-image diagnostic was designed to prove native application focus
A to B to A with an exact QMP-injected saved byte. It requires the old
candidate and every panel window to be unmapped away; return requires exactly
one freshly XRes-authenticated candidate from the retained process. A new XID
is acceptable with fresh authentication. Mapped-away or ambiguous states stop
the experiment. Sixteen parser tests, 47 harness checks and independent
security review passed before the runtime attempt below.

Run `40135172647` returned incomplete, so it does not prove that full focus
sequence. The raw XRes lifecycle did observe the authenticated candidate
window `0x0060000f` transition from map state 2 to 0 and back to 2, with the
same retained panel PID and XID. The second native Wayland Mousepad also saved
the exact QMP-injected byte `20`, and its process exited cleanly. Those facts
show that keyboard input reached the second process while the candidate was
unmapped; they do not prove the required pointer-focus transition.

The host's visual locator caused the incomplete result. It requires more than
3 percent of sampled screen pixels to differ from a baseline before it emits
a click point. The blank second editor covered the blank first editor with
nearly identical geometry. Comparing the retained before and focus-away PNGs
with the locator's sampling and difference rule finds only 122 of 64,000
samples changed, about 0.19 percent. Consequently `wait-change` emitted no
point and `click-change` could not perform or prove the requested pointer
click. The focus-return screenshot also does not visibly establish that the
original editor was restored. Treat the 2-to-0-to-2 map sequence as useful
diagnostic behavior only, not complete A-to-B-to-A focus proof. No rerun is
needed for the proposed X11 exception because the independent content-
authentication stop below already blocks it.

## Security stop: the panel process does not authenticate its content

The proposed X11 exception is blocked even if the focus-lifecycle diagnostic
passes. A pidfd proves which process rendered a window and XRes proves which X
client owns it. Neither proves who supplied the words that process displays.

This conclusion is from source review, not a reproduced exploit. No arbitrary
client was run against the retained image, no foreign client called the real
panel's `UpdateLookupTable`, and no malicious engine was registered or selected
at runtime. The runtime work above proves the legitimate panel's process,
window and composition behavior only.

The reviewed sources are Ubuntu Noble's `ibus` source package at commit
`dcffa98a63b75d46e1946a492cf399387f1b68da` on `ubuntu/noble-updates` and the
upstream IBus 1.5.29 tag at commit
`0ad8e77bd36545974ad8acd0a5283cf72bc7c8ad`. Noble identifies the package as
1.5.29-1. The control-plane files below are identical to the upstream tag
before Ubuntu's patch series. Ubuntu's panel patch adds Unity property
forwarding; it does not restrict lookup-table senders or engine registration.

There are two independent source-level paths for another process in the same
login session to choose panel content:

1. `bus/server.c` lines 103-136 accepts the EXTERNAL authentication mechanism
   and authorizes every peer with the daemon's user ID. The private IBus bus
   routes calls addressed to `org.freedesktop.IBus.Panel` to the panel's one
   connection. `src/ibuspanelservice.c` lines 1084-1118 compares the service's
   receive-side `GDBusConnection` with the connection on which it received the
   call, but does not check the `sender` argument. That comparison is true for
   calls forwarded from any sender to that service connection. Lines 1149-1160
   deserialize any `UpdateLookupTable` argument and emit it. The GTK panel
   passes a visible table straight to `CandidatePanel`
   (`ui/gtk3/panel.vala` lines 1748-1751), which copies its candidate strings
   and shows the popup when the list is nonempty
   (`ui/gtk3/candidatepanel.vala` lines 196-238 and 246-261).
2. `org.freedesktop.IBus.RegisterComponent` has no caller allowlist.
   `bus/ibusimpl.c` lines 1319-1368 creates an engine factory proxy on the
   caller's connection and adds its engine descriptions to the registered
   engine list. Any peer can also create its own input context (lines
   1196-1223), set `IBUS_CAP_FOCUS`, call `FocusIn`, and select an engine
   (`bus/inputcontext.c` lines 1137-1155, 1204-1219 and 1281-1319). Global
   engine mode defaults on, and `SetGlobalEngine` has no sender restriction
   (`bus/ibusimpl.c` lines 1743-1787). A registered engine's lookup-table
   signal is then relayed by the daemon to the real panel.

Input-context methods are bound to the connection that created that context.
That does not close the second path: an untrusted process owns its own context
and IBus accepts that context's assertion that it has focus. IBus focus is not
bound to AurWL's keyboard focus or to a Wayland client identity.

This is a narrow same-user overlay risk, not root or compositor code execution.
AurOS does not currently grant it: the X window manager suppresses an unrelated
override-redirect X11 panel over a native Wayland application. Applications do
already trust IBus for their ordinary in-process preedit and commit path, which
is how the exact `你好` composition proof succeeds. Making the separate panel
visible and clickable over another client would add the privilege. Therefore
the exception remains blocked regardless of the A-to-B-to-A focus result.

## Bounded safer paths

### Render lookup UI in the input client

IBus has a client-side delivery branch. In `bus/inputcontext.c` lines
2145-2172, a context advertising `IBUS_CAP_LOOKUP_TABLE` receives an
`UpdateLookupTable` signal instead of sending the table to the shared panel.
That would avoid a separately privileged cross-client window if the focused
toolkit rendered the chooser as its own Wayland popup.

The shipped GTK IBus module does not implement that path. Its initial
capabilities are preedit, focus and optionally surrounding text
(`client/gtk3/ibusimcontext.c` lines 1081-1103); it does not advertise
`IBUS_CAP_LOOKUP_TABLE`, connect an update-lookup-table handler, or contain a
candidate renderer. `GtkIMContext` supplies preedit and commit integration,
but does not provide a generic candidate-list widget that IBus can turn on.
Merely setting the capability bit would divert lookup tables away from the
panel and leave them undisplayed.

A client-rendered repair therefore means maintained toolkit input-module work:
deserialize bounded tables, render a same-client `xdg_popup` tied to the
focused surface and cursor rectangle, route selection back to that context,
and repeat the work for every supported toolkit. It may cover GTK Mousepad and
Firefox first, but it is not an existing switch and does not cover Qt or other
clients automatically. Its useful security property is that an application
cannot obtain a new cross-client overlay; its candidate UI remains subject to
the compositor's ordinary same-client popup and focus rules.

### Authenticate and isolate a shared panel control plane

A shared panel can be reconsidered only if its content path is made narrower
than the current same-user IBus bus. The minimum boundary needs all of the
following, followed by adversarial runtime proof:

* The panel must not accept lookup or focus methods through a replaceable
  well-known name on the shared IBus bus. It needs a private inherited channel
  whose sole writer is the owned daemon or a small trusted relay, with peer
  credentials checked before any content is accepted.
* The daemon or relay must bind each input context to the peer credentials of
  the process that created it, and bind that identity to AurWL's currently
  focused Wayland client. An application calling `FocusIn` cannot itself grant
  that binding. Focus changes must issue a globally nonrepeating generation;
  old generations cannot authorize a later XID, remap or reused PID.
* Only root-owned, explicitly allowed engine factories may supply content to
  the elevated panel. Untrusted peers cannot use `RegisterComponent`, replace
  an installed engine name, or switch the focused context to an unapproved
  engine. Otherwise a correctly focused application can still ask a malicious
  engine to populate the trusted renderer.
* Every update must carry the authenticated context and current focus
  generation. The panel and compositor must discard it after focus loss,
  context disconnect, engine replacement, panel or daemon death, window
  unmap/destroy, Xwayland restart, or an exec/pidfd identity change. Geometry
  and pointer access remain bounded to the focused application's visible area;
  keyboard focus never moves to the panel.
* Negative tests must attempt direct panel calls, malicious component
  registration, engine-name replacement, false `FocusIn`, stale-generation
  replay, PID/XID reuse and updates racing focus loss. A normal Pinyin test is
  not evidence for these boundaries.

That is the smallest defensible shared-panel boundary visible in the reviewed
architecture. It requires coordinated compositor, IBus daemon and client
identity work, so it is not the narrow X11 popup exception originally under
investigation and is not implemented.

## Fcitx client-side diagnostic result

The safer client-side path has now passed its bounded retained-image
diagnostic. The successful second attempt of run `40145726339` used image
source `c1ba6ee5`, image SHA-256
`275bdf0cd6caa02c05fb328fe18407d1306212550584ee6cfcaef426013a178c`,
and test source `3b2fd5ea`. The exact successful Actions artifact ID is
`12581007423`.

The diagnostic installed Noble's exact source-reviewed packages only in the
disposable scratch guest: `fcitx5` 5.1.7-1build3,
`fcitx5-frontend-gtk3` 5.1.1-1build2 and `fcitx5-pinyin`
5.1.3-1build3. It started an isolated Fcitx daemon without `DISPLAY` or a
Wayland display, forced the applications onto native Wayland with the GTK
Fcitx input module, and did not change the retained product image.

Both rounds, separated by a real guest reboot, passed these bounded checks:

* Mousepad and Firefox each selected the exact `pinyin` engine and produced
  the exact UTF-8 text `你好`. Each emitted one mapped, parented, non-grabbing
  `xdg_popup` syntax sequence with no layer-shell object in each round. The
  artifact retains one final candidate and committed or submitted screenshot
  set; its focus-away and password screenshots are retained separately for
  both rounds.
* During the Mousepad interval, the probe authenticated one application
  connection to the exact AurWL socket and peer. It held the application PID,
  pidfd, trace writer, display inode, compositor executable, service identity,
  service cgroup and PAM session ancestry across the measured interval, with
  no application children or foreign trace writer at its boundaries.
* Moving focus from Mousepad A to Mousepad B unmapped the candidate before B
  received input. B alone saved the exact sentinel byte `20`; A remained
  unchanged. Returning to A created a fresh candidate map epoch without an
  input grab.
* The controlled password field received the exact synthetic ASCII value but
  produced no observed candidate popup, popup map/configure/grab, layer-shell
  object, or browser composition start/update/end/input event. Only reduced
  counts and masked screenshots remain; the raw password value, focus and
  event logs were deleted.
* Fcitx did not autostart after reboot. Both rounds used the same isolated
  configuration and exact transient process. Temporary processes were gone,
  the user's Fcitx configuration hash was unchanged, and the shipped IBus
  service, exact process identity and live Pinyin engine were restored after
  each round.

This remains diagnostic evidence, not a shipped multilingual configuration.
The Firefox trace proves exact composition and parented-popup syntax, but its
Wayland connection identity is still unproven; the authenticated connection
interval covers Mousepad only. Do not generalize the Mousepad identity result
to Firefox or claim that every toolkit has a same-client candidate popup.

The screenshots expose two notification UX items. Round one shows an
`Fcitx 5 is ready` notification and a three-message indicator. A production
Fcitx session must not leave that technical startup notice in front of the
user. Round two shows the technical IBus notification described earlier in
this handoff. The run did not retain a journal proving that notification's
individual instance; attribution to the shipped IBus session rests on the
reviewed Noble source, the known shipped launch mode, and its presence before
the diagnostic stopped IBus. The temporary Fcitx path did not generate an
IBus warning. Both visible notices remain product UX work for the chosen
multilingual input architecture.

The workflow published two artifacts with the same name for run
`40145726339`. Artifact `12581236003` belongs to the stale failed attempt;
artifact `12581007423` contains the successful second-attempt evidence.
Automation and operators must select this evidence by exact artifact ID (and
verify the embedded image/test identities), not take the first name match.

## Fresh production image result

The profile-scoped production implementation is now in a fresh image and its
bounded functional certification passed. Profile run `40148012574`, source
`3629b114`, passed all 48 checks. The multilingual image SHA-256 is
`42359c4847a1a696a21c5b5350f4574a2970bb1d4d8f7df42c8336157a1696fa`.

This run exercised the shipped configuration rather than installing a
temporary diagnostic stack. It verified the shipped `auros-fcitx.service`,
the exact Fcitx executable and arguments, its D-Bus ownership, the absence of
`DISPLAY` and `WAYLAND_DISPLAY` in the daemon, inactive conflicting IBus, the
profile's GTK input-module environment and the live `pinyin` engine. The
root-owned French keyboard/Pinyin default was used without replacing the
user's configuration.

In the same fresh boot, Mousepad saved and Firefox submitted the exact UTF-8
text `你好`. Both applications emitted the bounded parented, non-grabbing
popup syntax with no layer-shell object. Candidate and committed or submitted
screenshots were retained; lead inspection confirmed a real visible candidate
popup and the exact committed text in both applications. Raw protocol traces
were removed after reduction, temporary application processes were cleaned
up, and the shipped service and user configuration remained ready afterward.

The result has exact limits:

* It is one fresh-image functional boot. It does not repeat the shipped
  production stack after a reboot. The earlier two-round security diagnostic
  used an isolated temporary Fcitx stack and cannot be relabelled as a
  production reboot certification.
* Mousepad has the bounded authenticated compositor-connection evidence from
  the earlier diagnostic. Firefox in this production run proves exact
  composition and parented-popup syntax, but its Wayland connection identity
  and candidate-pixel attribution remain unproven. Do not claim a verified
  same-client identity for Firefox or generalize this result to every toolkit.
* This profile image has not gone through the website-to-Windows delivery and
  installation path and has not been proven on real Windows hardware. The run
  certifies the fresh multilingual image under the profile KVM harness only.

The shared X11-panel exception remains rejected. The shipped path uses the GTK
client-owned candidate UI and adds no compositor popup privilege. The next
delivery proof, if multilingual is offered as a downloadable choice, is the
profile-specific website request through Windows installation into the same
fresh image, while retaining the run and image hash above as the Linux-side
functional anchor.
