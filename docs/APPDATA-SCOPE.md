# AppData scope: what the Windows tool copies, and what it does with what it cannot copy

**Status:** proposal. It needs a decision (§9), not code. Nothing below is implemented except the
section headed *Already done*.
**Closes:** SYSTEM-REVIEW §2.20 and C5 when decided. §6 item 4 (*"Whether the Windows tool completes on
a real machine"*) stays open until someone points the tool at a real profile.
**Reads with:** `auros-installer/SAFETY.md` phases 4–5, DECISIONS.md D14 and D15, SPEC §4.2.

---

## The problem, in one paragraph

`buildSources()` (`cmd/auros-migrate/main.go:399-406`) adds RoamingAppData and LocalAppData whole, and
`copyengine/plan.go` has no exclusion list. Anything the engine cannot read becomes a quarantine
record. `safety/arm.go` will not mint a `VerifiedArchive` while any record is unresolved, and
`quarantine.Set.Waive` has no caller in production code. Each piece is correct on its own. Put
together, they mean one file the user cannot read, anywhere in AppData, stops the run for good, and the
user has no way to fix it. A live LocalAppData always has some: a browser's open profile database, a
cache an updater holds open, or a directory whose ACL shuts the user out.

## Already done (branch `agent/appdata-junctions`, auros-installer)

The half that needed no decision:

- **Links are no longer quarantined.** Junctions, symbolic links and app execution aliases are pointers,
  not data. Go (1.23+) reports a junction as `ModeIrregular` and a symlink as `ModeSymlink`, and never
  as a directory, so the walk never went into them. What was wrong was the verdict. They were
  quarantine records, so the three legacy junctions every profile has (`Application Data`, `History`,
  `Temporary Internet Files`) were enough to stop a run on their own. Now each one appears in the
  report as **"not copied: link, not data"**, with its target. A reparse point counts as a link only if
  its tag has the name-surrogate bit (`IsReparseTagNameSurrogate`, MS-FSCC §2.1.2.1) or is
  `IO_REPARSE_TAG_APPEXECLINK`. OneDrive cloud-file tags are not links, and an unreadable tag is not a
  link. Both still go to quarantine.
- **Gate 3 can now see this.** `testharness/gen` creates real junctions, each with a deny-list ACE on
  the junction itself, plus one plain directory nobody can list, all in the corpus's LocalAppData.
  **Once the base image is rebuilt, clean runs will stop at the wall on that directory, and that red
  result is correct.** It stays red until one of the options below lands.

What that does **not** fix: locked or in-use files, directories the user cannot read, and gigabytes of
cache. Fixing those means deciding what the product promises.

## What the product actually promises about AppData

SPEC §4.2 says *files, browser profiles, Wi-Fi credentials, printers and accounts* migrate. D15 narrows
browser profiles to **bookmarks and history** for Chrome and Edge, and says cookies, passwords and
payment data do not migrate. Nothing in SPEC, DECISIONS or the customer-facing
`migration/20-files.md` promises *the rest of AppData*. So the wholesale copy is not a promise we are
keeping. It is the implementation copying more than the promise. Everything beyond the browser profiles
costs time, destination space and quarantine risk, and gets the user nothing on Linux: the programs
that read that data do not migrate (§4.2).

## The options

### A. Scope AppData to an allow-list of known-valuable subtrees

Copy Documents, Desktop, Pictures, Downloads, Music and Videos whole, as now. Take nothing from AppData
except named subtrees. For example: each Chromium profile's `Bookmarks` and `History`, and a Firefox or
Thunderbird profile directory. Each entry on the list is tied to a restore step on the Linux side that
actually uses it.

- **Pros.** It matches the promise exactly: we copy what we say migrates and nothing else. Almost all of
  the locked, denied and cache material is never walked, so it cannot be quarantined. The run gets far
  smaller and faster on a USB stick. The disclosure becomes a list the user can read.
- **Cons.** Anything off the list is gone after the wipe: an unfamiliar app's saves, a game's
  `%APPDATA%` saves, a local-only notes app. The list has to be maintained, and it is only as good as
  our knowledge of what schools run. It also does nothing for locked files *inside* the list (Chrome
  running with `History` open), or for locked files in Documents (a Word document left open).
- **§4.2 disclosure.** It gets more honest, but it adds a sentence we do not say today. The pre-run
  screen has to state the scope as an exclusion: *"From your app settings folder (AppData) we copy only
  browser bookmarks, history and Firefox/Thunderbird profiles. Everything else in it, including saved
  games and other programs' settings, is not copied."* Not saying that would be the §4.2 failure in a
  new form. The site copy needs the same sentence.

### B. Add an explicit, user-visible waive step

Keep the scope as it is. After VERIFY, if the unresolved count is not zero, show the user the exact list
grouped by reason ("in use", "you do not have permission", ...). Let them waive the records (per item
or per group), then close the program and retry. Each waiver is recorded in the run log and in the
archive, and the wall opens only when every record is either resolved or waived.

- **Pros.** SAFETY.md phase 5 already describes this: *"the user sees the exact list and decides. We
  do not decide for them, and the default is to stop."* D14 promises *"these N files were in use and
  were skipped"*. This is the only option that also fixes a locked file in **Documents**, which neither
  A nor C touches. `Set.Waive` already exists and is tested.
- **Cons.** On a wholesale AppData copy the list will run to hundreds or thousands of entries,
  mostly cache paths no teacher can judge. That teaches people to click "waive all", and a waive-all
  button in front of the wipe is exactly the habit the wall exists to prevent. It is also a new
  interactive UI step in a tool whose other prompts are simple.
- **§4.2 disclosure.** Waived files have to appear three times: on the screen before the wipe, in the
  run log on the destination, and on the Linux desktop's restore count (phase 7: *"a discrepancy is
  shown, never swallowed"*). The restore count then reads "N of M files, K waived by you" instead of
  "N of N". That is honest, but it is a visible downgrade of the headline claim.

### C. Skip caches by known patterns

Keep the wholesale copy, but leave out a deny-list of known cache paths: `*\Cache\*`, `Code Cache`,
`GPUCache`, `INetCache`, `Temp`, `CrashDumps`, `D3DSCache`, package caches, and so on.

- **Pros.** It is the smallest code change. It cuts bytes a lot. It keeps "we copy all your AppData"
  roughly true.
- **Cons.** A deny-list is open-ended. Every app we have not heard of is copied whole, and its locked
  files still stop the run. It does not help at all with a locked profile database, and that is the
  single most likely record (the browser is open). Pattern matching on the name only is how data gets
  lost: a folder called `Cache` inside Documents is somebody's work. And it makes the disclosure the
  vaguest of the three.
- **§4.2 disclosure.** It needs a sentence we cannot make specific: *"We skip temporary and cache files
  in AppData."* No user can check that, so it is the one kind of claim §4.2 exists to stop us making.

## Recommendation

**A, with B kept only for what is left.** C should not be used as the mechanism.

1. **Scope AppData to an allow-list (A)**, tied one-to-one to what §4.2 and D15 already say migrates.
   That removes nearly all of the quarantine cases, rather than asking the user to approve them.
   Put the exclusion sentence on the pre-run screen and on the site in the same change.
2. **Add the waive step (B), for the few records A leaves.** Those are locked files in the
   user folders or in an allow-listed profile. Before offering a waiver, first ask the user to close
   the named program and retry, because that fixes most cases. Waive each record by name. Do not
   offer "waive all" for more than a small count; the number is a §9 call and 20 is a starting point.
   Show every waiver on the Linux desktop count.
3. **Do not ship C.** Where caches sit inside an allow-listed subtree (Firefox's `cache2`), exclude them
   from **that entry** of the list. That is an exclusion we can name to the user, not a pattern.

Why this order: A shrinks B's list from thousands of entries to a handful, and a handful of entries is
the only size of list a person can judge honestly. B alone becomes waive-all. A alone still stops on a
Word document left open.

**Decision needed (§9):** the allow-list's first contents, the exact wording of the exclusion sentence,
and the cap on waiving in bulk.

## What only a real Windows run can confirm

This is SYSTEM-REVIEW §6 item 4. It takes one VM with a real, used profile, and none of the following
has been observed:

- That the legacy junctions come back from `FindFirstFileW` with `IO_REPARSE_TAG_MOUNT_POINT` and are
  reported, not quarantined. On Linux CI this is tested with POSIX symlinks only. The Windows branch
  (`ModeIrregular` → tag) has been cross-compiled and vetted but never run.
- How many records a real LocalAppData produces once the links are gone, and how they split between
  locked, denied and other. That number decides between A and A+B.
- **OneDrive files in Documents.** Go reports every reparse point that is not a link, not dedup and not
  a socket as `ModeIrregular`. That includes cloud-file tags, and `plan.go` quarantines non-regular
  entries as "unsupported type". If files that are hydrated or pinned under a sync root keep their
  cloud tag (we believe they do; it is not verified), then every OneDrive file is a quarantine record
  today. That would be a larger hard stop than AppData. It is separate from this decision, and it is
  related to §2.21.
