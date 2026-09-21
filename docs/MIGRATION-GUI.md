# The migration GUI: design

**Status: DESIGN. Nothing here is built.** Written 2026-09-20 against `auros-installer` `main`
(`4b944cb`) and `origin/agent/label-seam` (`1714893`). It answers SYSTEM-REVIEW §2.24, H20 and N7.
Section 4 holds DRAFT text for DECISIONS.md and BLOCKED.md. **The owner applies it.** This
branch does not edit either file.

Conventions: **[source]** marks a fact with a citation. **[measured]** marks something run for this
document (Appendix A). **[judgment]** marks an opinion, and an opinion can be wrong.

---

## 0. The problem, in one paragraph

D4 is binding: *"If a task the product promises requires a command line, it gets a GUI or it stops
being a promise"* (SPEC §11.1). The migration is that task. `auros-migrate` does not proceed until
the operator types `--i-understand-programs-do-not-migrate` and `--cloud-files=hydrate|skip`
(`cmd/auros-migrate/main.go:81-84`, `:165-168`, `:519`). That is worse than it first looks. The
flag has to be on the command line **before the program starts**, so on every run after the first,
the acknowledgement is typed *before* the list it acknowledges has been printed. `auros-web`'s
`migration/01-windows-programs.md:32-34` promises that the installer *"shows it to you, and makes you
acknowledge it. You find out on the screen in front of you"*. In practice a flag does the opposite.
That is H20, and it is why this is a safety item and not only a UX item.

---

## 1. Windows side: which GUI technology

### 1.1 The constraints, with where each one comes from

| # | Constraint | Source |
|---|---|---|
| K1 | One static exe, Windows 10/11, **amd64 and 386**, no runtime to install | D35 (*"Windows 10/11 support on both amd64 and 386 is unchanged"*); task brief |
| K2 | Unsigned for now | BLOCKED B3 |
| K3 | Builds with the pinned toolchain, Go 1.25.14 | D35 |
| K4 | **The GUI gets no path to the system disk** | SAFETY.md, "The invariant" and "Rules" 1 and 7; `internal/safety/wall_test.go` |
| K5 | **No external module dependencies** | `wall_test.go` (in `TestWall_ForbiddenImports`): *"the module has no external dependencies and must not acquire one"*; `go.mod` has no `require` |
| K6 | Nothing is written under the migration user's profile on C: | Gate 3's USN check (`testharness/gate3/noise.go` on `fix/gate3-real-cli`): *"the MIGRATION user's profile is not [excluded], because that is exactly where a tool that wrote scratch files to the system disk would put them"* |
| K7 | Runs on the cohort: 2012–2015 laptops (SAFETY.md, phase 6) | SAFETY.md |

K5 and K6 decide most of this, and the brief did not name either. Each is explained below.

**K5, and why the wall cannot see a dependency.** `walkGoFiles` and `TestWall_SyscallIsConstantsOnly`
parse only `.go` files **under the repository root** (`wall_test.go`, `repoRoot` and the walk). Code
in a third-party module sits in the module cache and never passes through the wall. So any GUI
library that calls Win32 brings its own `syscall.NewLazyDLL` surface into the shipped exe, and no
test looks at it. That is the exact bypass `TestWall_ForbiddenImports`'s doc comment describes
(*"syscall.NewLazyDLL plus `unsafe` is the ability to call any Win32 export … from any package"*),
coming in through `go.mod` and not through a file.

**K6, and why "the GUI writes nothing to C:" is testable.** Gate 3 compares the NTFS change
journal before and after each run against a published noise list. The migration user's
`AppData\Local` is covered only for Windows' own subtrees (`noise.go` rules for `auros-gate3`). A
toolkit that unpacks itself or caches data under `%LOCALAPPDATA%` fails Gate 3 by construction,
and it should.

### 1.2 The candidates

| Candidate | Maintained? | Licence (checked precisely) | windows/386 | No runtime install | Wall (K4/K5/K6) | Verdict |
|---|---|---|---|---|---|---|
| **Raw Win32, stdlib `syscall` only** (our own `internal/gui`) | Win32 user32/comctl32 are stable OS ABIs [judgment] | n/a (our code; OS DLLs) | **Yes [measured]** | Yes, the DLLs ship with Windows | **Inside the wall.** Every proc it binds is a literal in our repo and can be allowlisted by a test (§1.4) | **Recommended** |
| `golang.org/x/sys/windows` as the binding layer | Active; pushed 2026-09-18 [source: GitHub API `repos/golang/sys`] | BSD-3-Clause [source: GitHub API licence field] | Yes (x/sys supports it) | Yes | Breaks K5. Also **x/sys `v0.48.0`, the latest, `requires go >= 1.26.0`** [measured: `go get` under Go 1.25.14 refused it], so taking it means pinning an old x/sys or reopening D35. It has few user32 GUI wrappers, so we would still write most bindings ourselves [judgment] | Not worth a dependency |
| **lxn/walk** (+ lxn/win) | **Dormant: last commit on the default branch 2021-01-12** [source: GitHub API `repos/lxn/walk/commits`]; lxn/win's latest resolves to a 2021-02-18 pseudo-version [measured]. A fork, `tailscale/walk`, was last committed 2026-07-02 [source: GitHub API] | BSD-3-Clause-style, 3 clauses, *"Copyright (c) 2010 The Walk Authors"* (GitHub reports `NOASSERTION` because the text is not byte-identical to SPDX) [source: `repos/lxn/walk/license`]. Compatible with a proprietary product if the notice is reproduced in the binary distribution (clause 2). Pulls in `gopkg.in/Knetic/govaluate.v3`, MIT, **archived** [source: GitHub API `repos/Knetic/govaluate`] | Yes: **4.8 MB (386) / 5.1 MB (amd64) stripped** for a three-widget window [measured] | Yes, but **requires a Common Controls 6 manifest** (*"Walk requires Common Controls 6 … you must put an appropriate application manifest file either next to your executable or embedded as a resource"* [source: lxn/walk README]) | **Breaks K4/K5.** lxn/win binds `ShellExecuteW`, `SHBrowseForFolderW`, `DragQueryFileW` and others in `shell32.go` [source: `repos/lxn/win/contents/shell32.go`]. That is the ability to launch programs, from outside the wall | Rejected |
| **Native wizard via `PropertySheetW` (PSH_WIZARD97/AERO)** | OS component | n/a | Yes | Yes | Same as raw Win32; it *is* raw Win32 with dialog templates built in memory | **Fold into the recommendation as a variant**, see §1.3 |
| **WebView2** (e.g. `jchv/go-webview2`, MIT [source: licence file]; `wailsapp/wails`, MIT [source: GitHub API]) | Active | MIT | Runtime ships x86 builds [source: MS "Distribute…", `MicrosoftEdgeWebView2RuntimeInstaller{X64/X86/ARM64}`] | **No guarantee on Windows 10.** Microsoft: *"The vast majority of Windows 10 devices have the WebView2 Runtime installed already … A small number of Windows 10 devices don't"*, and it recommends shipping the bootstrapper or redirecting users to download it [source: learn.microsoft.com/…/webview2/concepts/distribution]. Offline, update-disconnected school laptops are the likeliest to be in that small number [judgment]. The Fixed Version runtime *"binaries are over 250 MB"* [same source] | Also creates a user data folder on startup, next to the exe by default [source: …/concepts/user-data-folder]. A second, multi-process browser runtime runs inside the trust boundary of the most dangerous program the company ships [judgment] | Rejected: fails K1 on exactly the cohort |
| **Tk via `modernc.org/tk9.0`** (cgo-free) | Active; v1.80.0 published 2026-08-28 [source: pkg.go.dev] | BSD-3-Clause [source: pkg.go.dev] | Listed, but *"Specific to windows/386: … This target is no more a first-class supported one"* [source: pkg.go.dev/modernc.org/tk9.0, "Supported targets"] | Embedded, but *"At runtime, the application extracts these internal libraries to the user's cache directory"* [same source] | **Fails K4/K6 outright.** Extracting DLLs to the user's cache directory is a write to the system disk in phase 1, before the wall, where the invariant says nothing may be written. Gate 3 would catch it | Rejected |
| Tk via `visualfc/atk` | Pushed 2026-06-06 [source: GitHub API] | **LGPL-2.1** [source: GitHub API]. Static linking into a proprietary exe brings LGPL relinking obligations [judgment; ask counsel before relying on it] | Needs a Tcl/Tk runtime | No | n/a | Rejected |
| **Fyne** | Very active; v2.8.1, pushed 2026-09-18 [source: GitHub API; measured `go list -m`] | BSD-3-Clause [source: GitHub API] | Needs cgo: `CGO_ENABLED=0 GOOS=windows GOARCH=386` fails with *"build constraints exclude all Go files in …/go-gl/gl/v2.1/gl"* [measured]. Fyne's own setup requires *"a C compiler (to connect with system graphics drivers)"* [source: docs.fyne.io/started/quick] | Needs an OpenGL driver. Field reports of *"WGL: The driver does not appear to support OpenGL"* on Windows 10 with **Intel HD Graphics 3000**, a 2011-era integrated GPU inside K7's window [source: fyne-io/fyne#4033, #2700, #2959] | Breaks K5 (large dependency tree, cgo toolchain in the release path) | Rejected |
| `rodrigocfd/windigo` | Active; pushed 2026-09-04 [source: GitHub API] | MIT [source: GitHub API] | Not stated in the README [source: README fetched 2026-09-20] | Yes; *"CGo is not used, just native syscalls"* [source: README] | Breaks K5 and has the same wall-invisibility problem as walk [judgment] | Rejected; worth reading as a reference for struct layouts |

### 1.3 Recommendation

**Raw Win32 through the standard library's `syscall`, in a new package `internal/gui`, with no
third-party module. The wizard is a single top-level window that swaps child panels, not
`PropertySheetW`.** [judgment, reasons below]

- It is the only option that satisfies K1–K7 together, and the only one where every DLL entry point
  the GUI can reach is a string literal in this repository, which lets a test hold it to an
  allowlist (§1.4).
- **Size [measured]:** the spike (window, ListView, checkbox, button, message loop, `fmt`) is
  **1.51 MB for windows/386 and 1.61 MB for windows/amd64**, stripped (`-trimpath -ldflags="-s -w"`,
  Go 1.25.14, `CGO_ENABLED=0`). walk for a comparable window is 4.8 / 5.1 MB.
- **Why not PropertySheet.** It gives the standard Back/Next/Cancel frame for free. But it needs
  `DLGTEMPLATE` structures built by hand in memory, and its page-change notifications fight the
  one thing this wizard must control: **Next is enabled by our state machine, not by the page.**
  A plain window with one panel per phase is fewer structures to get right and easier to drive from
  UI Automation in Gate 3 [judgment]. If the owner prefers the native Aero wizard look, the page
  model in §2 maps one-to-one onto property-sheet pages, so the switch is cheap later.
- **The manifest.** Common Controls 6 (the visual styles) and per-monitor DPI awareness need an
  `RT_MANIFEST` resource. Go links `.syso` "system object files" found in a package directory
  [source: `go help filetype`, Go 1.25.14]. `tc-hib/go-winres` (0BSD [source: GitHub API]) writes
  *"files named `rsrc_windows_*.syso` that `go build` automatically embeds"* [source: its README].
  It is a **build-time tool only** and is never linked, so K5 holds. Commit the two `.syso` files
  (386, amd64) next to `cmd/auros-migrate/main.go`, with a CI check that regenerates them and diffs.
  The same resource carries `requestedExecutionLevel` (§2.8).
- **Unsigned (B3):** nothing here needs signing. SmartScreen's "More info → Run anyway" is the
  first screen a user sees either way, and the GUI changes nothing about that. It does not make B3
  worse.

### 1.4 How the wall is kept intact (K4) once a GUI package exists

`internal/gui` needs `unsafe` (pointers to Win32 structs) and `syscall.NewLazyDLL`. Today both are
allowed only in `internal/winenv` (plus `internal/sysdisk` for syscall). **Adding a package to those
lists is, per the test's own comment, "a change to the safety architecture"**. So the widening
comes with a strictly narrower rule for the new package:

1. **`TestWall_GUIProcAllowlist` (new).** Parse `internal/gui/*.go`. Every `NewLazyDLL` argument must
   be a string literal in `{user32.dll, comctl32.dll, gdi32.dll, uxtheme.dll}`, plus `kernel32.dll`
   for exactly `GetModuleHandleW`. Every `NewProc` argument must be a literal on an explicit list
   (window, message, control, paint and font calls). **`shell32`, `ole32`, `comdlg32`, `advapi32` and
   `ntdll` are banned by name.** They hold `ShellExecuteW`, `SHFileOperationW`, the common file
   dialogs (which write MRU/recent-item state under the profile), and the registry. A non-literal
   argument fails the test. The test also asserts that it saw at least one `NewProc` (D34: it must
   be watched failing).
2. **`internal/gui` imports nothing from this module and nothing of `os`, `os/exec`, `io/fs`,
   `path/filepath`.** It receives plain values (strings, counts, a `[]string` of program names) and
   returns user choices through callbacks. The proof types (`safety.Destination`,
   `safety.VerifiedArchive`) never enter it. `cmd/auros-migrate` does the wiring. The rule goes into
   `TestWall_ForbiddenImports` as a new `sealed`-style set.
3. **No free-text path and no folder picker.** The destination page lists only what
   `safety.Resolver.Choose` returns, and shows what it rejected and why (`DescribeRejections`). That
   removes the "typed `C:\…`" and junction scenarios (G01, G03) from the GUI entirely. The resolver
   still runs, because the CLI keeps `--dest`.
4. `unsafe` is widened to `internal/gui` **only**. `internal/gui` is added to `mayCallWin32` in
   `TestWall_SyscallIsConstantsOnly`. Rule 1 is what makes that acceptable.

---

## 2. The screens

One engine, two front ends. First, extract the body of `run()` in `cmd/auros-migrate/main.go` into
`internal/migrate` (a driver that emits events and asks for decisions through an interface). Then
the CLI and the GUI are both thin views of the same `safety.Machine`. **Gate 3's 120 CLI runs keep
testing the engine the GUI uses** (§5). The mode (DRY RUN / COMMIT) sits in the title bar and in a
coloured band on every page, per SAFETY.md rule 6.

| Page | Phase (SAFETY.md) | Shows | Next is enabled when | Cancel does |
|---|---|---|---|---|
| **Welcome** | none | What will happen, in order; mode; *"nothing on this computer changes until your files exist twice and are checked"* | always | exits |
| **1 Inventory** | INVENTORY | Folders found (`KnownFolders`), file count and size (`measure`), the three "one restart" facts (`printFirmware`). **If OneDrive placeholders exist:** two radio buttons, *Download them now (N GB, slow)* / *Leave them in OneDrive*, **with neither pre-selected** | measuring has finished **and**, if placeholders exist, a radio is chosen. **"Leave them in OneDrive" is not offered until N5/H10 lands** (the `skip` value is accepted and ignored today, §2.21). The GUI must not inherit a lie; until then the page says the files will be downloaded | exits; nothing written anywhere |
| **2 Disclosure** | DISCLOSE | The programs list (§2.1), then *what does come across* and *what does not* | the coverage rule in §2.1 is met **and** the box is ticked | exits |
| **3 Destination** | DESTINATION | Qualifying volumes from `Resolver.Choose` as a radio list (label, size, free vs needed), rejected ones greyed with the reason, the junction note if it applies. A **Rescan** button for a drive plugged in now | one qualifying volume is selected | exits; nothing written |
| **4 Copy** | COPY | Progress bar, files/total, bytes, quarantined count (from `copyengine.Progress`) | automatic on success | confirms, cancels the context, and shows the **Abort** page |
| **5 Verify** | VERIFY | Progress; then `report.Describe()` and the quarantine list by name | unresolved count is zero. Otherwise the only button is **Stop** (SAFETY.md: *"the default is to stop"*; there is no release path, C5) | Abort page |
| **6 Finish / Arm** | ARM | **Dry run:** "Done. N files now exist in two places" with the measured volume comparison. **Commit build:** the planned steps (BitLocker suspend, BootNext or firmware-menu fallback), then **Restart now**, a separate explicit click | n/a | commit: before *Restart now* nothing has crossed. After it, the existing unwind applies |
| **Abort** | any | What stopped and why; the run log path on the destination; **"Nothing was written to this computer's disk" only if `dest.OnSystemVolume()` was just measured false** (rule 7), otherwise nothing is said | n/a | exits |

Threading: the engine runs in a goroutine. Progress reaches the UI thread by `PostMessageW` to a
private `WM_APP` message, and the UI thread alone touches windows [judgment: the standard Win32
pattern]. Closing the window counts as Cancel. The `runtime.LockOSThread` message loop is in the
spike.

### 2.1 The disclosure page: how "scrolled the whole list" is enforced (H20)

The list is a `SysListView32` in report view, one row per program name, sorted. The checkbox
*"I have read all N. None of them will be on the new computer."* starts **disabled**.

1. **Coverage, not "reached the bottom".** A `coverage` bitmap has one bit per row. A 100 ms
   `WM_TIMER` reads `LVM_GETTOPINDEX` and `LVM_GETCOUNTPERPAGE` and marks every row currently on
   screen. The checkbox is enabled only when **every row has been on screen at least once**.
   Pressing End or dragging the thumb to the bottom marks the first and last pages and **not the
   middle**, so it does not count. That is the muscle-memory path H20 describes. Polling, rather
   than scroll notifications, means the wheel, the keyboard, the scrollbar and a screen reader moving
   focus row by row all count the same way [judgment]. Implemented and tested in the spike
   (`coverage.go`, `coverage_test.go`, including the End-key case).
2. **Next needs both:** coverage complete **and** the box ticked. Unticking disables Next again.
3. **A list that fits on one page is read by being shown.** Coverage is complete on the first tick.
   An empty list is complete, and the page then says "No installed programs were found" rather than
   hiding the page.
4. **Recorded, and bound to the list.** The run log `2-disclose` event records `ack_via: "gui"`,
   `rows`, `rows_seen`, `seconds_on_page`, and **`list_sha256`**, a hash of the sorted names shown.
   An acknowledgement is then evidence about *this* list, not a boolean. The CLI path records
   `ack_via: "cli-flag"`, so the difference H20 cares about stays visible afterwards.
5. **Not done, on purpose [judgment]:** a minimum dwell time, and typing the count back. Both punish
   the careful reader without stopping the careless one. Coverage is the property that matters: the
   eyes had the chance to pass every name.
6. The text under the list comes from the same place as the CLI's `printDisclosure`, **after N3 is
   fixed**. Today that function says *"Wi-Fi networks, printers … do come across"*, which
   SYSTEM-REVIEW N3 says is claimed and not built. The GUI must not copy that sentence while it is
   false.

### 2.2 Elevation

Phases 1–5 need no administrator rights. ARM runs `manage-bde` and sets `BootNext`, which do. **The
`VerifiedArchive` cannot cross a process boundary**, and that is the point of it. So a "relaunch
elevated for phase 6" design would have to re-verify in the new process or serialise the proof, and
serialising the proof defeats the type. The resolution [judgment]: the **dry-run build** (the only
one that exists until Gate 3 passes; ARM is behind `auros_arm_enabled`) embeds `asInvoker`. The
**commit build** embeds `requireAdministrator`, so the single process is elevated from the first
screen, the UAC prompt appears once at launch, and the proof never leaves the process.

---

## 3. Linux side: what is still command-line

`cmd/auros-restore` (on `agent/label-seam`) runs as a systemd **user** unit on the first graphical
login (`packaging/systemd/auros-restore.service`). It posts a desktop notification over the session
bus (`internal/deskbus`) and writes a report file. The base installs it from a pinned artifact
(`auros-base/build/45-restore.sh`; the pin is filled). The happy path and the archive-damaged path
are already zero-terminal. What is not:

| # | Situation | What happens today | Terminal needed? |
|---|---|---|---|
| L1 | **Two or more backups attached** (`ErrAmbiguous`, exit 4) | `findRefusal` returns **before** the notification code, and under `--quiet` (how the unit runs it) the message reaches **only the journal**. No pop-up, no desktop report | **Yes.** The only way to choose is `auros-restore --archive DIR` |
| L2 | **A drive is attached with no backup on it** (exit 7) | Same early return: the careful *"do not wipe it — tell whoever set this computer up"* text goes to the journal only | No command, but the user is never told |
| L3 | **Backup plugged in after login** | No archive found, exit 0, no stamp; the unit retries **at the next login** | No, but nothing tells the user to sign out and back in [judgment: a notification saying so is enough] |
| L4 | **Wi-Fi and printers** (B19) | Saved but not switched on. *"An administrator running `auros-restore` as root does write the keyfiles"* (BLOCKED B19) | **Yes, root at a terminal.** Already recorded as a degradation in B19; its privileged half waits for a design review |
| L5 | `--force`, `--dry-run`, `--list` | Operator and support tools | Not promises, so not a D4 violation [judgment] |

Fix for L1–L3, small and reusing what exists: send the find refusals through the same
`deskbus.Send` + `WriteReport` path the verify refusal already uses. For L1, the notification names
the backups found and says to unplug all but one and sign out and back in. No chooser UI is needed,
because unplugging is the chooser [judgment]. L4 stays with B19.

---

## 4. What must be recorded today: DRAFT text for the owner

SYSTEM-REVIEW N7: *"The schedule item is the GUI; the defect is that the deviation is unrecorded.
Record it today, build it later."* D13 is the pattern: say what the spec requires, what we do
instead, why, and flag it for the human. **Numbers are placeholders.** BLOCKED.md's "ID collision
note" shows parallel branches already collide, so assign them at merge.

> ### ⟪DRAFT — for the owner to apply; not in DECISIONS.md⟫
>
> ## D4x — The migration is command-line only today; D4 is not met for it · 2026-09-20 · AGENT · **deviation, flagged for the human**
> D4 (HUMAN, binding) says a task the product promises either gets a GUI or stops being a promise.
> The migration is the product's central promise, and `auros-migrate` and `auros-restore` are
> command-line programs. The operator must type `--i-understand-programs-do-not-migrate` and
> `--cloud-files=hydrate|skip` before the Windows program will copy anything. This was not written
> down anywhere (SYSTEM-REVIEW §2.24). This entry writes it down.
>
> **It is worse than a UX gap.** A flag is typed before the program runs, so on every run after the
> first, the acknowledgement comes *before* the list it acknowledges (H20). The site says the
> installer "shows it to you, and makes you acknowledge it". Today that is true only in the letter.
>
> **Until the GUI exists:** a migration is run by our staff or a trained operator, never by a
> customer, and no page may describe it as something a customer runs themselves. The design is
> `docs/MIGRATION-GUI.md`: raw Win32, stdlib only, no new dependency, the wall extended by a proc
> allowlist, and a disclosure page that cannot be acknowledged until every row has been on screen.
> Gate 3 is not blocked by this. It tests the engine through the CLI, and the engine is what the GUI
> will drive.
>
> Also still command-line on the Linux side: choosing between two attached backups, and switching
> migrated Wi-Fi and printers on (B19).

> ### ⟪DRAFT — for the owner to apply; not in BLOCKED.md⟫
>
> ## B2x — The migration GUI is not built, so D4 is unmet for the migration · OPEN · blocks customer self-service, not Gate 3
> Needs engineering time (≈3–4 weeks, `docs/MIGRATION-GUI.md` §5), not money and not a human
> action. Nothing is purchased. B3 (signing) is independent: the GUI does not make SmartScreen
> better or worse.
> *Workaround:* operator-run migrations only (D4x). *Decision needed from the owner:* whether the
> CLI's `--i-understand-programs-do-not-migrate` stays in the shipped exe once the GUI exists (§6,
> Q1).

---

## 5. Plan, sized [judgment: estimates for one engineer who knows this repo]

| Phase | Work | Effort | Exit check |
|---|---|---|---|
| **P0** | Owner applies §4 | 30 min | the entries exist |
| **P1** | Extract `internal/migrate` from `cmd/auros-migrate/main.go`: events out, decisions in (placeholder choice, disclosure ack, destination pick, "stop at quarantine"). The CLI becomes one implementation. **No behaviour change** | 2–3 days | Gate 3 suite unchanged and green; `TestWall_*` green |
| **P2** | `internal/gui` primitives + `TestWall_GUIProcAllowlist` + import sealing + manifest `.syso` via go-winres, with a regenerate-and-diff CI step | 3–4 days | allowlist test watched failing (D34): add `shell32` and see red |
| **P3** | The pages of §2, threading, Cancel/Abort, the disclosure coverage gate, run-log fields | 5–7 days | manual run on a Windows 10 386 VM and on Windows 11 |
| **P4** | Gate 3 GUI legs (below) | 3–5 days | new rows green on `windows-latest` |
| **P5** | Linux L1–L3 notifications | 1–2 days | restore tests for exit 4/7 assert a notification attempt and a report file |
| P6 (after N3/N5) | enable "Leave them in OneDrive"; share the corrected disclosure text | ½ day | S-row for skip passes via the GUI |

**Total ≈ 15–23 engineer-days.** N7 called it "genuinely large". It is medium [judgment], because the
engine exists and the GUI only drives it.

### Which Gate 3 tests change

- **Unchanged:** all F-, G-, S-rows (`testharness/gate3/scenario.go`). They drive the CLI, and after
  P1 the CLI is the same engine. `installerArgs` (`runner_windows.go:268`) keeps passing
  `--i-understand-programs-do-not-migrate` unless the owner answers Q1 by removing the flag, in which
  case it moves behind a harness build tag and the gate3 README says the harness binary differs by
  exactly that.
- **New, a "U" family, run on the shipped GUI binary and driven through UI Automation** (Windows
  PowerShell's `System.Windows.Automation`; nothing added to the exe, per SAFETY.md rule 8's "no
  exported off switch"):
  - U01: the disclosure checkbox is disabled on load. Invoking it through UIA fails.
  - U02: End key / thumb-drag to bottom leaves it disabled. Scrolling page by page enables it.
  - U03: full dry run through every page; the **USN invariant applies unchanged, and the noise list
    must not grow**. That is the mechanical form of K4/K6 for the GUI itself.
  - U04: the destination page never lists the system volume, or a junction onto it (mirror G01/G03).
  - U05: window closed mid-copy → Abort page; USN clean; run log on the destination records the abort.
  - U06: with placeholders present, Next is disabled until a choice is made (mirror the S-row).
  - U07: the run log's `2-disclose` event carries `ack_via=gui`, `rows_seen == rows`, `list_sha256`.
- **Ratio rule** (SAFETY.md rule 2, abort ≥ happy): U01, U02, U04, U05 are refusals and U03 is the
  happy path, so the family keeps the ratio.

---

## 6. Open questions for the owner

- **Q1.** Once the GUI ships, does the CLI's disclosure flag stay in the customer exe (as the operator
  and support path, with `ack_via=cli-flag` logged) or move behind a build tag? Recommendation
  [judgment]: keep it, logged. Removing it makes Gate 3's binary differ from the shipped one, which is
  the thing D27's USN design avoids.
- **Q2.** The native Aero wizard look (PropertySheet) or a plain panelled window (§1.3)? Either fits
  the page model.

---

## Appendix A: the spike

Built on `homebase` in `golang:1.25.14-alpine` (D35's toolchain), `--memory=4g`, in
`~/work/migration-gui-spike`. **The directory was removed afterwards.** Nothing was added to
`auros-installer`.

Results [measured]:
```
go vet ./... ; go test ./...                     ok  auros-gui-spike
GOOS=windows GOARCH=386   go vet .               OK
GOOS=windows GOARCH=amd64 go vet .               OK
spike-386.exe    1,507,328 bytes   (CGO_ENABLED=0, -trimpath -ldflags="-s -w")
spike-amd64.exe  1,605,120 bytes
walk-386.exe     4,800,000 bytes   (lxn/walk c389da54e794 + lxn/win a377121e959e + x/sys v0.36.0)
walk-amd64.exe   5,133,824 bytes
fyne v2.8.1, windows/386, CGO_ENABLED=0 → "build constraints exclude all Go files in …/go-gl/gl/v2.1/gl"
x/sys@latest = v0.48.0 → "requires go >= 1.26.0 (running go 1.25.14)"
```
The spike was **cross-compiled only, not run on Windows**. It proves the toolchain path, the struct
layouts compiling on both word sizes, and the coverage logic, not the pixels. Running it is P3's
first step.

`coverage.go` (untagged, so it is tested on Linux):
```go
package main

// coverage records which rows of the disclosure list have actually been on
// screen. The acknowledgement is enabled only when every row has been seen.
// Jumping to the end (End key, dragging the thumb) marks only what was drawn,
// so it does not count as having read the middle.
type coverage struct {
	seen   []bool
	unseen int
}

func newCoverage(n int) *coverage {
	return &coverage{seen: make([]bool, n), unseen: n}
}

// mark records rows [top, top+perPage) as seen, clamped to the list.
func (c *coverage) mark(top, perPage int) {
	if top < 0 {
		top = 0
	}
	for i := top; i < top+perPage && i < len(c.seen); i++ {
		if !c.seen[i] {
			c.seen[i] = true
			c.unseen--
		}
	}
}

func (c *coverage) complete() bool { return c.unseen == 0 }
```

`coverage_test.go`:
```go
package main

import "testing"

func TestCoverage(t *testing.T) {
	c := newCoverage(30)
	c.mark(0, 10)  // first page
	c.mark(20, 10) // End key: jumped to the last page
	if c.complete() {
		t.Fatal("jumping to the end must not count as reading rows 10-19")
	}
	c.mark(10, 10)
	if !c.complete() {
		t.Fatal("every row has been on screen")
	}
	if e := newCoverage(0); !e.complete() {
		t.Fatal("an empty list is trivially read")
	}
	s := newCoverage(5)
	s.mark(-3, 100) // bogus control answers are clamped, not a panic
	if !s.complete() {
		t.Fatal("a list that fits on one page is read by being shown")
	}
}
```

`gui_windows.go` (the disclosure page; stdlib only). Note the DLLs it binds: `user32`, `comctl32`,
and `kernel32` for `GetModuleHandleW` alone. That is the allowlist of §1.4 in miniature.
```go
//go:build windows

package main

import (
	"fmt"
	"runtime"
	"syscall"
	"unsafe"
)

var (
	user32   = syscall.NewLazyDLL("user32.dll")
	comctl32 = syscall.NewLazyDLL("comctl32.dll")
	kernel32 = syscall.NewLazyDLL("kernel32.dll")

	pRegisterClassExW  = user32.NewProc("RegisterClassExW")
	pCreateWindowExW   = user32.NewProc("CreateWindowExW")
	pDefWindowProcW    = user32.NewProc("DefWindowProcW")
	pGetMessageW       = user32.NewProc("GetMessageW")
	pIsDialogMessageW  = user32.NewProc("IsDialogMessageW")
	pTranslateMessage  = user32.NewProc("TranslateMessage")
	pDispatchMessageW  = user32.NewProc("DispatchMessageW")
	pPostQuitMessage   = user32.NewProc("PostQuitMessage")
	pSendMessageW      = user32.NewProc("SendMessageW")
	pEnableWindow      = user32.NewProc("EnableWindow")
	pSetTimer          = user32.NewProc("SetTimer")
	pDestroyWindow     = user32.NewProc("DestroyWindow")
	pLoadCursorW       = user32.NewProc("LoadCursorW")
	pInitCommonCtrlsEx = comctl32.NewProc("InitCommonControlsEx")
	pGetModuleHandleW  = kernel32.NewProc("GetModuleHandleW")
)

const (
	wsOverlappedWindow = 0x00CF0000
	wsVisible          = 0x10000000
	wsChild            = 0x40000000
	wsBorder           = 0x00800000
	wsVScroll          = 0x00200000
	wsTabStop          = 0x00010000
	wsDisabled         = 0x08000000
	bsAutoCheckbox     = 3
	lvsReport          = 0x0001
	lvsNoColumnHeader  = 0x4000
	lvmFirst           = 0x1000
	lvmGetTopIndex     = lvmFirst + 39
	lvmGetCountPerPage = lvmFirst + 40
	lvmInsertItemW     = lvmFirst + 77
	lvmInsertColumnW   = lvmFirst + 97
	lvifText           = 0x1
	lvcfWidth          = 0x2
	wmDestroy          = 0x0002
	wmCommand          = 0x0111
	wmTimer            = 0x0113
	bmGetCheck         = 0x00F0
	iccListView        = 0x1
	idCheck            = 101
	idNext             = 102
	colorWindow        = 5
	idcArrow           = 32512
	cwUseDefault       = ^uintptr(0x7FFFFFFF) // (int)0x80000000
)

type wndClassExW struct {
	cbSize        uint32
	style         uint32
	lpfnWndProc   uintptr
	cbClsExtra    int32
	cbWndExtra    int32
	hInstance     uintptr
	hIcon         uintptr
	hCursor       uintptr
	hbrBackground uintptr
	lpszMenuName  *uint16
	lpszClassName *uint16
	hIconSm       uintptr
}

type msgW struct {
	hwnd     uintptr
	message  uint32
	wParam   uintptr
	lParam   uintptr
	time     uint32
	x, y     int32
	lPrivate uint32
}

type lvColumnW struct {
	mask       uint32
	fmt        int32
	cx         int32
	pszText    *uint16
	cchTextMax int32
	iSubItem   int32
	iImage     int32
	iOrder     int32
	cxMin      int32
	cxDefault  int32
	cxIdeal    int32
}

type lvItemW struct {
	mask       uint32
	iItem      int32
	iSubItem   int32
	state      uint32
	stateMask  uint32
	pszText    *uint16
	cchTextMax int32
	iImage     int32
	lParam     uintptr
	iIndent    int32
	iGroupID   int32
	cColumns   uint32
	puColumns  uintptr
	piColFmt   uintptr
	iGroup     int32
}

var (
	list, check, next uintptr
	cov               *coverage
)

func u16(s string) *uint16 { p, _ := syscall.UTF16PtrFromString(s); return p }

func send(h uintptr, m uint32, w, l uintptr) uintptr {
	r, _, _ := pSendMessageW.Call(h, uintptr(m), w, l)
	return r
}

func child(parent, hinst uintptr, class, text string, style uintptr, x, y, w, h int32, id uintptr) uintptr {
	r, _, _ := pCreateWindowExW.Call(0, uintptr(unsafe.Pointer(u16(class))), uintptr(unsafe.Pointer(u16(text))),
		wsChild|wsVisible|style, uintptr(x), uintptr(y), uintptr(w), uintptr(h), parent, id, hinst, 0)
	return r
}

func wndProc(hwnd uintptr, msg uint32, wParam, lParam uintptr) uintptr {
	switch msg {
	case wmTimer:
		// Polling, not scroll notifications: wheel, keyboard, thumb drag and a
		// screen reader moving focus all end up here the same way.
		top := int(int32(send(list, lvmGetTopIndex, 0, 0)))
		per := int(int32(send(list, lvmGetCountPerPage, 0, 0)))
		cov.mark(top, per+1) // +1: a partly visible last row
		if cov.complete() {
			pEnableWindow.Call(check, 1)
		}
		return 0
	case wmCommand:
		switch wParam & 0xFFFF {
		case idCheck:
			on := send(check, bmGetCheck, 0, 0) == 1
			pEnableWindow.Call(next, boolPtr(on && cov.complete()))
		case idNext:
			pDestroyWindow.Call(hwnd)
		}
		return 0
	case wmDestroy:
		pPostQuitMessage.Call(0)
		return 0
	}
	r, _, _ := pDefWindowProcW.Call(hwnd, uintptr(msg), wParam, lParam)
	return r
}

func boolPtr(b bool) uintptr {
	if b {
		return 1
	}
	return 0
}

func main() {
	runtime.LockOSThread()
	programs := make([]string, 60)
	for i := range programs {
		programs[i] = fmt.Sprintf("Synthetic program %02d", i+1)
	}
	cov = newCoverage(len(programs))

	icc := [2]uint32{8, iccListView}
	pInitCommonCtrlsEx.Call(uintptr(unsafe.Pointer(&icc)))
	hinst, _, _ := pGetModuleHandleW.Call(0)
	cursor, _, _ := pLoadCursorW.Call(0, idcArrow)
	wc := wndClassExW{
		lpfnWndProc: syscall.NewCallback(wndProc), hInstance: hinst, hCursor: cursor,
		hbrBackground: colorWindow + 1, lpszClassName: u16("AurosSpike"),
	}
	wc.cbSize = uint32(unsafe.Sizeof(wc))
	pRegisterClassExW.Call(uintptr(unsafe.Pointer(&wc)))

	title := fmt.Sprintf("These %d programs will not come across", len(programs))
	win, _, _ := pCreateWindowExW.Call(0, uintptr(unsafe.Pointer(u16("AurosSpike"))), uintptr(unsafe.Pointer(u16(title))),
		wsOverlappedWindow|wsVisible, cwUseDefault, cwUseDefault, 520, 460, 0, 0, hinst, 0)

	list = child(win, hinst, "SysListView32", "", wsBorder|wsVScroll|wsTabStop|lvsReport|lvsNoColumnHeader, 10, 10, 480, 300, 0)
	col := lvColumnW{mask: lvcfWidth, cx: 450}
	send(list, lvmInsertColumnW, 0, uintptr(unsafe.Pointer(&col)))
	for i, p := range programs {
		it := lvItemW{mask: lvifText, iItem: int32(i), pszText: u16(p)}
		send(list, lvmInsertItemW, 0, uintptr(unsafe.Pointer(&it)))
	}
	check = child(win, hinst, "BUTTON", fmt.Sprintf("I have read all %d. None of them will be on the new computer.", len(programs)),
		wsTabStop|wsDisabled|bsAutoCheckbox, 10, 320, 480, 24, idCheck)
	next = child(win, hinst, "BUTTON", "Next >", wsTabStop|wsDisabled, 390, 360, 100, 30, idNext)
	pSetTimer.Call(win, 1, 100, 0)

	var m msgW
	for {
		r, _, _ := pGetMessageW.Call(uintptr(unsafe.Pointer(&m)), 0, 0, 0)
		if int32(r) <= 0 {
			break
		}
		if d, _, _ := pIsDialogMessageW.Call(win, uintptr(unsafe.Pointer(&m))); d != 0 {
			continue
		}
		pTranslateMessage.Call(uintptr(unsafe.Pointer(&m)))
		pDispatchMessageW.Call(uintptr(unsafe.Pointer(&m)))
	}
	fmt.Printf("acknowledged after seeing %d/%d rows\n", len(programs)-cov.unseen, len(programs))
}
```

## Appendix B: sources

- Microsoft, *Distribute your app and the WebView2 Runtime*: https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution (page updated 2026-09-14)
- Microsoft, *Manage user data folders*: https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/user-data-folder
- lxn/walk README: https://github.com/lxn/walk. Licence and last commit via `gh api repos/lxn/walk{,/license,/commits}`, 2026-09-20
- lxn/win `shell32.go`: https://github.com/lxn/win/blob/master/shell32.go
- modernc.org/tk9.0 on pkg.go.dev ("Supported targets", "Frequently Asked Questions"): https://pkg.go.dev/modernc.org/tk9.0
- Fyne quick start: https://docs.fyne.io/started/quick/. OpenGL failures on old Intel graphics: https://github.com/fyne-io/fyne/issues/4033, https://github.com/fyne-io/fyne/issues/2700, https://github.com/fyne-io/fyne/issues/2959
- rodrigocfd/windigo README: https://github.com/rodrigocfd/windigo
- tc-hib/go-winres README: https://github.com/tc-hib/go-winres
- `go help filetype` (Go 1.25.14), on `.syso`
- GitHub REST API (`gh api repos/<owner>/<repo>`), 2026-09-20, for licence SPDX fields, `archived` and `pushed_at` of: lxn/walk, lxn/win, tailscale/walk, fyne-io/fyne, golang/sys, wailsapp/wails, jchv/go-webview2, webview/webview_go, visualfc/atk, rodrigocfd/windigo, tc-hib/go-winres, Knetic/govaluate
