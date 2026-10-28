# Ferry — the first-boot migration engine

> The moment AurOS earns or loses the user's trust. They agreed to
> replace Windows because we promised their files would be waiting on
> the other side. Ferry is the code that keeps that promise — and, just
> as importantly, the code that tells the truth about the parts of it
> nobody can keep.

Ferry runs once, on the first boot of a freshly installed AurOS, while
the user's old Windows partition is still sitting intact on the same
disk. It mounts that partition **read-only**, reads the user's data and
settings off it, and lands them in the new home directory. Then it shows
a report of everything that came across and everything that could not —
because an honest "we could not bring your Chrome passwords without your
old password" is worth more than a silent gap the user discovers a week
later.

The authoritative specification is
[`docs/research/migration.md`](research/migration.md); this document is
the design and the honest limitations. Where they disagree, the spec
wins and this file is the bug.

---

## Governing rules

These are not aspirations. Each is enforced in code and checked by the
test suite, and each maps to a hard requirement of the product.

1. **The way in only ever reads the Windows drive.** The partition is
   mounted read-only (`mount_ro` passes `ro` as both a driver option and
   a mount flag, then checks the kernel's own `/proc/mounts` options
   column and unmounts again if it disagrees), sources are opened
   `O_RDONLY`, and no link on the Windows side is ever followed. The test
   suite snapshots every file on the source volume before the stages run
   and asserts not one byte, size or mtime changed after. **The single
   exception is the way back** (below): when she asks to put Windows
   back, her new AurOS files are first copied into Windows. It is the
   only caller of `mount_rw` (a test checks that), it uses ntfs-3g
   without `force`, and a hibernated or dirty drive is refused first.

2. **A hibernated or dirty volume is refused, out loud, with the fix.**
   Not because we might corrupt it — the way in never writes to it — but because
   a hibernated volume is *stale*: the newest copy of the user's work is
   inside `hiberfil.sys`, not in the files. Importing from it copies
   yesterday's spreadsheet and calls it today's. Windows 8+ "Fast
   Startup" makes an ordinary shutdown produce exactly this state, so it
   is the common case, not the rare one. Every importing stage calls
   `require_clean_volume` first and exits `3` with a plain-language
   remedy if the volume is not clean. `ferry detect` is the one
   exception: it *reports* the condition instead of refusing, because
   its whole job is to give the UI the sentence that explains why.

3. **Every operation is idempotent and resumable.** First boot can be
   interrupted by a flat battery or an impatient user. A re-run must not
   duplicate, must not skip, must not overwrite. Stages record their own
   completion (`ferry run` skips finished ones), and within a stage the
   copier (`ferry-copy`) recognises a file already brought across (same
   size and mtime) and writes through a hidden `.ferrypart-*` temporary
   that is renamed into place only after a batch has been flushed to the
   disk, so a half-copy never exists at the destination; leftovers of a
   cut-off run are removed on the next one.

4. **Never overwrite a destination without recording it, and never write
   as root into her home.** If something already sits where an imported
   file would go, Ferry keeps *both* — the Windows copy lands beside it as
   `name (from Windows).ext`, then `(from Windows 2)` — via
   `renameat2(RENAME_NOREPLACE)`, and writes the row to the manifest and
   the report. Root opens the Windows folders and Ferry's own records,
   then `ferry-copy` becomes her (`setresuid`, checked) before it touches
   her home, so a symlink she planted at `~/Pictures` gains nothing. No
   path that comes out of the registry may contain `..` or pass through a
   link (`path_ci` refuses both), and `user-dirs.dirs` is read as her and
   obeyed only inside her home.

5. **Progress is honest and in plain words.** Every folder is walked
   first, so the total is known before the first byte moves, and the bar
   is by bytes (plus a fixed cost per file), not by file count. About once
   a second the copier says one plain line (`Copying your Pictures: 1,200
   of 5,000 files. About 4 minutes left.`; `Saving your Pictures in
   Windows: …` on the way back) on `$FERRY_PLAIN_FD`, and a JSON line in
   `$FERRY_STATE/state/<stage>.progress` for anything that wants numbers.
   `ferry run --plain` / `ferry back --plain` print only those sentences;
   everything else goes to `$FERRY_STATE/ferry.log`. No file name ever
   reaches her screen.

6. **What could not migrate is a feature, not an apology.** The final
   report reads from a ledger every stage writes as it works, and states
   the unconditional truths (Chromium secrets, EFS, DRM, drivers…)
   whether or not this particular machine hit them — because "we didn't
   try" and "it can't be done" are different sentences and the user
   deserves the true one.

---

## The findings that shape everything

From the research (`migration.md`), because they explain why Ferry is
shaped the way it is — and what the owner decided about them:

- **DPAPI is the gatekeeper.** Windows encrypts user secrets — browser
  passwords, cookies, saved credentials — with a key derived from the
  user's *Windows logon password*. Without that password they are
  unrecoverable from an offline disk.

- **Two things escape DPAPI's user layer — and Ferry deliberately takes
  neither.** Wi-Fi keys (machine DPAPI) and Firefox's saved logins
  (`key4.db`) could be brought across. **Saved passwords never cross**:
  Ferry does not copy or decrypt any password, key or card, whoever could
  have made it work. Her current Wi-Fi comes across through the
  installer's own visible tick box (D10), not here.

- **Chromium passwords need the old Windows password**, and are not
  attempted for the same reason. Chromium *bookmarks* are plain JSON and
  do come.

- **Firefox 67+ ignores a profile that is merely marked default.** Each
  install has its own `[Install<hash>]` section; without one, Firefox
  makes a new empty profile (found by running Firefox 158). See
  "Her browser" below.

---

## Architecture

Ferry is POSIX shell plus a handful of small C helpers, matching the
house style (`src/aurora/aurora`, `src/common/theme.c`): dense comments
that explain *why*, `_`-prefixed locals because busybox `ash` has no
lexical scope, no dependency beyond busybox and awk. `install.sh` is the
one list of what ships.

```
  ferry                orchestrator: mounts, runs stages in spec order,
                       resumes, writes the report and the done-signal
  ferry-detect         READ-ONLY discovery -> JSON for the UI
  ferry-wifi       (1) lists her Wi-Fi networks by name (never a key)
  ferry-settings   (2) timezone/locale/keyboard/wallpaper/…
  ferry-files      (3) known folders -> XDG dirs (placeholder-aware)
  ferry-firefox    (4) her Firefox profile, and Edge/Chrome/Brave bookmarks
  ferry-back           THE WAY BACK: her AurOS files -> Windows
  ferry-report         the "what could NOT migrate" report (+ --plain)
  ferry-hive           registry access: hivex if present, else our reader
  ferry-common.sh      the shared rules: mounts, volume health, paths,
                       registry / known-folder logic, as_her, the signal
  ferry-copy           (C) the copier, one process for all folders
  ferry-winattr        (C) NTFS-attribute / OneDrive-placeholder classifier
  ferry-hivepeek       (C) a from-scratch read-only `regf` hive parser
  ferry-chromemarks    (C) Chromium Bookmarks JSON -> bookmarks.html
  ferry-ffhash         (C) the name Firefox gives an install in profiles.ini
  data/windowsZones.tab  CLDR Windows-timezone -> IANA mapping
  data/klid.tab          Windows keyboard-layout-id -> XKB mapping
```

The order of the stages is load-bearing and comes straight from the spec
(§8): **WiFi first**, because getting online is what makes every later
re-sync — mail, browser accounts, OneDrive — possible at all.

### Why C where it is

Everything that *can* be shell *is* shell, because shell is auditable by
the people who audit this project. What cannot be:

- **Copying fast and as her.** The shell loop this replaced cost ~26 ms
  per file (5,000 photos took 266 s on homebase; `ferry-copy` takes 3.6 s
  for the same tree plus ten 20 MB videos). It is one process: plan the
  totals, drop to her uid, copy through temporaries, flush in batches.
- **Parsing a registry hive.** A `regf` file is a binary B-tree. `hivex`
  is preferred when installed; `hivepeek.c` is the dependency-free
  fallback. Read-only, bounds-checked, every list walk has a visit
  budget, and a hostile cell size cannot overflow (it used to negate
  `INT32_MIN`).
- **Classifying an NTFS file** as real data or a cloud placeholder
  (`winattr.c`; see "The OneDrive trap").
- **Reading Chromium's bookmarks** (`chromemarks.c`): JSON from a disk we
  did not build, parsed as her, with caps and escaping.
- **Naming a Firefox install** (`ffhash.c`): CityHash64 of the UTF-16
  folder holding the binary, checked against a real Firefox.

The classifier and the hive reader accept a test seam (`FERRY_WINATTR_MAP`, `user.*` xattrs,
`FERRY_HIVE_BACKEND`) so the whole engine can be tested on an ordinary
ext4 tree with no NTFS image and no root — which is exactly what
`tests/run.sh` does.

---

## What migrates

### Wi-Fi (`ferry-wifi`) — listed, never copied

Parses every `ProgramData/Microsoft/Wlansvc/.../*.xml` and lists the
networks Windows knew **by name** in the report ("each will ask for its
password once"). It writes no NetworkManager file and decrypts nothing:
saved passwords never cross (above). Her *current* network comes across
through the installer's tick box. Hidden networks, hex-only SSIDs and
enterprise (802.1X) networks are all listed.

### User files (`ferry-files`) — the one that must not lie

Copies Desktop, Documents, Downloads, Pictures, Music and Videos to the
XDG directories, through one `ferry-copy` run so the progress she sees
covers all six. Two things make this more than a `cp -r`:

**It resolves redirection.** It never assumes `\Users\<name>\Documents`.
It reads each folder's real location from
`HKCU\…\Explorer\User Shell Folders` in `NTUSER.DAT` (falling back to
`Shell Folders`), expands the `%TOKENS%`, and — critically — handles the
OneDrive "back up your folders" case, where Documents/Desktop/Pictures
have been silently moved into the sync root. A folder redirected onto a
drive letter that is not part of the migration is reported as
`off-volume`, never quietly replaced with an empty default.

> Token expansion is done with shell string surgery, not `sed`, on
> purpose: the replacement is a Windows path full of backslashes, and
> `%USERPROFILE%` → `C:\Users\Bob` through `sed` yields `C:SERSBOB`
> because `sed` reads the `\U` as an uppercase command. That bug would
> have silently emptied every imported folder. The comment in
> `ferry-common.sh` exists so nobody reintroduces it.

**The OneDrive trap.** See its own section below.

Idempotent, resumable, and collision-safe as described in the governing
rules. Windows junk (`desktop.ini`, `Thumbs.db`) is filtered so the "what
came across" count stays trustworthy. Names longer than 255 bytes are
shortened at a character boundary and reported; odd names (newlines,
tabs, quotes, leading dashes, CJK) come across exactly.

### The OneDrive trap — the #1 invisible data-loss path

OneDrive Files-On-Demand leaves files that look completely normal from
Linux: right name, right size in `ls`, right modification time. They
contain **nothing** — the bytes are on a server, and the directory entry
is an NTFS reparse point with tag `IO_REPARSE_TAG_CLOUD (0x9000001A)` and
the `OFFLINE` / `RECALL_ON_DATA_ACCESS` attributes set. **The tag alone
does not mean "not here"**: OneDrive keeps its cloud tag on files that
*are* on the computer ("Always keep on this device"), and an earlier
version of this classifier skipped every file of a OneDrive-backed
Documents folder — Windows 11's default — because of it. Only the
attributes say whether the bytes are here. Copying one
yields a stub or an I/O error, *not* the file. On a migration that is
silent, invisible data loss: the filename appears in the new Documents
folder and the user believes the photo is there.

So **Ferry never copies a file it has not classified.** `winattr.c` is
the only thing allowed to say "hydrated", and it is built to refuse the
optimistic answer:

```
  OFFLINE / RECALL_ON_* attr set ........... placeholder   (report, don't copy)
  EFS-encrypted attr set ................... encrypted     (report, can't read)
  name-surrogate reparse (junction/link) ... reparse       (report, don't follow)
  size > 0 but zero blocks allocated ....... suspect       (read it: any non-zero byte
                                                            and it is real; all zeros
                                                            and it was never here)
  size == 0 ................................ hydrated       (legitimately empty)
  blocks allocated ......................... hydrated       (bytes are on disk)
  no attribute source, can't tell .......... unknown        (report, don't assume)
```

The block-count fallback is the safety net for when no attribute is
readable: a file that claims bytes but owns zero disk blocks is holding
none, which is exactly the shape of a placeholder. A file *with* blocks
cannot be online-only, so its bytes are genuinely present. Every
placeholder is listed in the report **by name and by the size it would
have been**, with the one-line fix ("right-click in OneDrive → Always
keep on this device, then re-run"). Nothing is ever silently skipped and
nothing is ever silently copied as a stub.

### Her browser (`ferry-firefox`)

Two sources, one destination (Firefox on AurOS):

- **Her Windows Firefox profile** is copied (caches, locks,
  `compatibility.ini` and every password/card file are dropped).
- **Her Edge / Chrome / Brave bookmarks** (plain JSON; the browser's
  passwords never come) become the `bookmarks.html` Firefox imports; her
  main browser's bar is Firefox's toolbar.

What was found by running Firefox 158 (`tests/realfirefox.sh`):

1. Profiles live in `~/.config/mozilla/firefox` (Firefox 147+), or in
   `~/.mozilla/firefox` when that is all there is. Ferry uses the XDG
   folder if it exists, else the old one.
2. A profile marked `Default=1` is **ignored**; Firefox makes a new empty
   one and writes `[Install<hash>]`. So Ferry writes
   `[Install<hash>] Default=<profile>` (and `[<hash>]` in installs.ini).
   `<hash>` is CityHash64 of the UTF-16 folder of the firefox binary
   (`/usr/lib/firefox` → `4F96D1932A9F858E`).
3. `bookmarks.html` in a never-run profile is imported on first start, once.
4. `[ProfileN]` numbers must be contiguous or Firefox stops reading.

Policy: Firefox is pointed at the profile from Windows only if she has not
used Firefox here (or the profile is one an earlier Ferry run made); if
she has, or it is open, nothing is switched and her bookmarks wait in her
Home with plain steps. Windows' own profiles.ini and installs.ini are
never copied.

### Settings (`ferry-settings`)

Timezone, locale, keyboard layout, display scale, wallpaper, default
browser, and mapped-drive reporting. Everything is *suggested* — written
where the desktop reads it and echoed in the report so the welcome screen
can offer "we set your timezone to Europe/London — change it?" — never
silently forced. Display scale in particular is only ever a hint.

The hard part is the timezone, and it is the reason `data/windowsZones.tab`
ships with Ferry. Windows stores a `TimeZoneKeyName` of its own invention
("GMT Standard Time"); everything on Linux wants an IANA id
("Europe/London"). The table is the CLDR `windowsZones.xml` territory-001
default mapping, so the conversion works with no network. Keyboard layout
maps the Windows KLID to XKB via `data/klid.tab`, with a language-id
fallback so an unlisted KLID still lands on the right language.

### Discovery (`ferry-detect`)

Read-only, unmounts everything it mounts, and emits JSON for the UI: the
Windows version and build (correctly calling build ≥ 22000 "Windows 11"
even though the registry still says "Windows 10"), the volume health, the
registry backend in use, the WiFi count, and per-profile the resolved
known folders (with file counts and sizes), the browsers found, and the
mail/credential inventory. A `--human` mode prints the same facts for a
person at a terminal. A locked BitLocker volume is a finding, not a
silent skip.

---

## What cannot migrate — stated honestly

This list is printed at the end of *every* run, whether or not the
machine hit each item, because the honesty is the product.

| Not migrated | Why | What the user can do |
|---|---|---|
| Chromium (Chrome/Edge/Brave) passwords & cookies | wrapped in your Windows-login DPAPI key | re-enter, or sign back into the browser and re-sync; a future build can prompt for the old password |
| Windows Credential Manager, RDP creds, app tokens | encrypted to your Windows login, unrecoverable offline | re-enter in each app |
| OneDrive online-only files | live on the server, not the disk | hydrate in OneDrive, or sign in to OneDrive on Linux |
| EFS-encrypted files | need your Windows encryption certificate | decrypt in Windows first, then re-run |
| Files on a locked BitLocker volume | unreadable until unlocked | unlock in Windows, then re-run |
| DRM media (Kindle, some iTunes/Audible) | licensed to the old machine | re-download from the vendor |
| Installed Windows programs | are Windows binaries | reinstall the Linux equivalent (Forge's app map) |
| Print & device drivers | hardware/OS-specific | Linux supplies its own |
| Windows Hello PIN / biometrics | sealed to this PC's TPM | set up again |
| Outlook `.ost` / Windows Mail store | server caches | re-add the account; it re-syncs |
| Saved passwords of any kind (Firefox, Chrome, Edge, Wi-Fi, Windows) | never copied or decrypted, on purpose | websites and networks ask once |

---

## Success means what it says

`result=ok` (from `answer.sh`) means every folder she has was brought
across and at least one file came; it carries `files=N`. Everything else
is a different word, and none of them is "your files are here":

| exit | `result=` | meaning |
|---|---|---|
| 0, files=0 | `empty` | there was nothing on the Windows side to bring |
| 2 | `partial` | something did not come: an online-only OneDrive file, a folder Windows kept on another drive or a network share, a locked file (all listed by name in her report) |
| 3 | `refused` | Windows was hibernated or dirty; nothing was read |
| 4 | `failed`, `code=4` | not enough room on this computer (refused before the first byte; a first import must fit with at least 1 GiB or 5 % of the disk left over for AurOS itself; a resumed one is not re-tested) |
| 6 | `choose` | several Windows accounts and no way to tell whose files; `users=Bob,Harold`; the reply is `import:NAME` |
| 124 | `failed`, `code=124` | stopped after the 12-hour cap in `answer.sh`; pressing it again carries on |
| 1 | `failed` | nothing could be brought |

**Whose files** is settled once, before any stage (`choose_user`):
`--user NAME`, else the only account, else the only one that has files of
its own (her report says `From the Windows account "Bob"`), else exit 6.
Ferry never guesses between two people who both have files.

A stage that was only partly done is recorded as failed, not done, so a
retry (after she has made the online-only files available) redoes it
instead of skipping it and saying all is well.

**No one-hour limit, and a stopped copy resumes.** The unit has
`TimeoutStartSec=infinity`; `answer.sh` bounds the import and the copy
back by `timeout 43200`. A copy that is killed (power, restart, the cap)
leaves complete files and hidden `.ferrypart-*` temporaries; the next run
removes the temporaries and recognises what is already there (same size
and time), so nothing is copied twice (tested by killing the copier
part-way).

---

## The way back — her new files go into Windows first

"Put Windows back" removes AurOS, and with it everything she saved there
since she began. So `answer.sh`'s `putback` request runs `ferry back`
**before anything is armed** (the restore itself runs later, from a
separate staging environment that cannot see her decrypted home):

```
  C:\Users\<her>\From AurOS\Documents | Pictures | Desktop | Downloads | Music | Videos
                              Read me.txt   (what is there, and what could not be copied)
```

- the same copier (`ferry-copy --to-windows`), so the same promises: as
  her, no link followed, nothing in Windows overwritten (`name (from
  AurOS).ext`), no half file ever visible, plain progress;
- names are made safe for Windows (`: * ? " < > |` and control characters
  → `_`, no trailing dot/space, `CON`/`NUL`/`COM1`… prefixed, bytes that
  are not text replaced), and names that differ only in capitals are kept
  apart (Windows sees one name);
- the free space is checked before a byte moves: not enough room copies
  **nothing** (exit 4) and says how much is needed;
- the drive is opened for writing only for this (`mount_rw`, ntfs-3g, as
  her uid); a hibernated or dirty drive is refused first (exit 3);
- which Windows user: the one `ferry-files` recorded
  (`$FERRY_STATE/winuser`), else the only one, else `C:\Users\Public`.

Exit codes: `0` all saved · `2` some could not be · `3` Windows was not
shut down properly · `4` not enough room · `5` the drive could not be
opened · `1` failed. `answer.sh` arms the restart only on `0`; any other
answers `result=failed` with a plain `note=` and `code=N`, and Windows is
not put back.

**The handover volume (BitLocker PCs, and later every PC).** Windows' own
drive cannot be written from AurOS when it is locked, so the way back can
instead be a handover: `ferry back` (unchanged entry point) looks for a
volume labelled `AUROSHANDOV` (an exFAT label holds 11 characters), or one
named by `FERRY_HANDOVER_DEV` / `ferry back --to DEV`, and when there is one
it writes her files there instead of into Windows. The put-back step makes
the volume (`ferry handover --need` says how many bytes it must hold: the
files in whole 128 KiB clusters, the manifest, DONE and its neighbours, the
`received.tsv` Windows adds, and 1 MiB), attaches it, and afterwards
detaches it; `ferry back` leaves it **unmounted**. The copier runs as the
AurOS user (it is mounted with that user's uid/gid by root; nothing about the
loop device needs to be readable by her). The layout is a contract between
Ferry and the Windows consumer:

```
  AurOS-handover/version            1
  AurOS-handover/manifest.tsv       #ferry-handover<TAB>1, #aur-user, #created,
                                    optional #windows-user; then one row per file:
                                    FOLDER  PATH  SIZE  SHA256  MTIME   (tab-separated)
  AurOS-handover/manifest.sha256    sha256 of manifest.tsv
  AurOS-handover/files/<FOLDER>/<PATH>
  AurOS-handover/DONE               written LAST: ok, files=N, bytes=B, at=...
  AurOS-handover/received.tsv       written by WINDOWS (PATH, sha256, ok), never by Ferry
```

`PATH` is already a legal Windows name (illegal characters replaced, no
trailing dot or space, `CON`/`NUL`/`COM1`… prefixed, names that differ only
in capitals made distinct), so the consumer renames nothing. **DONE is the
only word for complete**, and it is written only after: every file was copied
through a hidden temporary, the volume was flushed and UNMOUNTED, mounted
again READ-ONLY (so nothing is served from memory), and every file read back
from it matched the SHA-256 taken from her disk while it was copied. A file
damaged on its way to the disk is found while AurOS is still there. Resumable:
run it again and what is in place is recognised (and re-hashed from her disk
for the manifest); a half-written file is never visible; DONE is removed at
the start of every run. `tests/verify-handover.sh VOLUME` is the Windows side's
checks in shell (the reference for the PowerShell: DONE, manifest hash,
sizes and hashes, no stray files, legal names), and `tests/mkhandover.sh DIR`
builds a fixture full of awkward names.

**A USB stick** ("Save my files to a USB stick first"): `ferry stick list`
names the sticks she could use (one per line: device, a plain name and size,
bytes, file system; exFAT, FAT or NTFS only) and `ferry stick DEVICE
[--plain]` saves onto one with the same copier and the same read-back, in a
folder she can open anywhere: `From AurOS/<Folder>/...`, `MANIFEST.tsv` and
`Read me.txt`. No DONE (nothing consumes it), kind=stick in the signal.

**How the desktop asks** (`src/aurshell/welcome.c`, `rootfs/usr/lib/auros/answer.sh`):
`import:NAME` answers "whose files?" (`result=choose`, `users=...`); the answer to it
is `request=import`, and the desktop reads only an answer newer than its own request
(the old `choose` is still on disk until the new one is written). `stick:/dev/NAME` asks
to save her files on that stick: aurfirst lets through only letters and digits after
`/dev/`, answer.sh checks the name is on `ferry stick list` as it is then, and runs
`ferry stick DEVICE --plain` as root with its plain lines in `/run/auros-answer/stick.log`.
The answer is `request=stick`, `result=ok|partial|empty|failed`, `files=N`, and for a
failure `why=noroom|unopened|nostick`. The desktop lists the sticks itself with
`ferry stick list` (it only reads). Put Windows back answers a refusal with `why=`
(`toomany`, `locked`, `noroom`, `dirty`, `nodrive`, `some`), and the desktop opens the
screen that says why and offers the stick. Saving to a stick does not unlock Put Windows
back: it asks again, and checks again, whenever she presses it.

Exit codes and the signal are those of `ferry back`: 0 saved and checked, 2
some not (or a folder that is a shortcut), 4 not enough room, 5 the volume
would not open, 1 failed or what was read back did not match. Proven under
the unit's real sandbox on real exFAT and FAT loop volumes (tests/sandbox.sh,
scenario 5).

**The completion signal** (for the desktop, which asks "Can you see your
photos?" only after a copy has finished): `ferry run` and `ferry back`
remove `/run/auros-answer/ferry.done` when they start and write it, whole,
when they end:

```
  kind=import|back
  result=ok|partial|refused|noroom|unopened|chooseuser|failed
  files=<brought across so far, from the manifest>
  at=<UTC time>
```

Its absence means "not finished". While running, the newest line of
`/run/auros-answer/import.log` (import) or `putback.log` (back) is the
plain sentence to show.

Proven by `tests/sandbox.sh` scenario 3: `answer.sh`'s `putback` under
every property of `auros-answer.service`, on a real NTFS volume — the
photo saved in AurOS is on Windows byte for byte, Windows' own files are
not overwritten, everything else on the volume is unchanged, and with no
room or a hibernated drive nothing is armed.

---

## State, resume, and the manifest

Everything Ferry knows lives under `$FERRY_STATE` (default
`/var/lib/ferry`) and survives a reboot on purpose:

```
  state/<stage>.status     running | done | failed (+ timestamp)
  state/<stage>.progress   latest JSON progress line, for the UI
  report.d/<stage>.tsv     the ledger: category, severity, item, detail
                           (<stage>-copy.tsv: the copier's, rewritten each run)
  ferry.log                Ferry's own chatter when run with --plain
  winuser                  the Windows user her files came from
  manifest.tsv             every destination byte written, incl. collisions
  suggested.conf           settings suggestions for the welcome UI
```

`ferry run` after a crash reads the status files, skips finished stages,
and re-runs a partial one — which copies only what is missing, because
`ferry-copy` recognises files already brought across. The manifest is the single source of truth
for "what did we create", including the files we declined to overwrite;
"never overwrite without recording" is enforced by making the recording
the only way to write.

---

## Usage

```
  ferry detect [--device DEV | --root DIR] [--human]   find Windows (read-only)
  ferry run [--auto] [--plain]                          mount, import, report
  ferry back [--plain]                                  the way back (see above)
  ferry wifi | files | firefox | settings [opts]        one stage
  ferry report [--json | --brief]                       the report
  ferry status                                          where a resume picks up
  ferry unmount                                         release the ro mount
```

Common options (every stage): `--root DIR` (an already-mounted volume),
`--device DEV`, `--user NAME` (which Windows profile; Ferry refuses to
guess between two real accounts), `--home DIR`, `--owner USER`,
`--dry-run`, `--json`, `--progress FILE`, `--force`.

---

## Testing

Ferry cannot be tested against a real Windows disk in CI, so
`tests/mkfakewin.sh` builds a synthetic NTFS-like tree containing every
trap the real world has — a Documents folder redirected into OneDrive,
cloud placeholders mixed with hydrated files (one that keeps the cloud
tag), a Videos folder on a second drive, a wrong-case `users` directory,
odd file names, a destination collision, real Edge/Chrome `Bookmarks` —
and `tests/mkhive.py` writes real binary `regf` hives.

```
  make; sh tests/run.sh       the suite as a user (read-only guarantee,
                              safety gate, hostile volume and home, odd
                              names, progress, plain words, bookmarks,
                              the way back)
  sh tests/asroot.sh          the same as root, in a docker container:
                              root never writes into her home, a symlink
                              she planted gains nothing
  sh tests/mutants.sh         puts each bug back and checks the NAMED test
                              goes red (also `asroot.sh mutants`)
  sh tests/insystemd.sh       inside auros-answer.service's real sandbox,
                              on real NTFS volumes: the import, and
                              "Put Windows back" with the photo check
  sh tests/infirefox.sh       Mozilla's Firefox 158 reads the result
  sh tests/bench.sh 5000 10   speed on a large fixture
  sh fuzz/run.sh 120          libFuzzer + ASan/UBSan (clang)
```

## Known limitations

- **Saved passwords never come across** — by decision, not by gap.
- **The way back needs the Windows drive to be openable.** A BitLocker
  drive that is locked cannot be written by ntfs-3g, so "Put Windows
  back" is stopped (exit 5) rather than removing AurOS with her files in
  it. The desktop needs a way to offer "save them to a memory stick
  instead" or "put Windows back anyway" — that is a decision for it.
- **Paths longer than 260 characters** that AurOS allows are saved but
  Explorer cannot open them.
- **`ffhash` vouches only for install folders up to 32 characters**
  (`/usr/lib/firefox`, `/opt/firefox`); a longer one is refused and her
  bookmarks wait in her Home instead.
- **Mail (.pst → MBOX) and the application inventory are not
  implemented** as importing stages. `ferry-detect` counts `.pst`/`.ost`
  and reports them; conversion and the app-equivalence list are Forge's
  data and a future stage.
- **The timezone table uses CLDR territory-001 defaults.** Windows keeps
  one name where IANA splits many zones by country ("Central Standard
  Time" is Chicago in the US but Mexico City in Mexico); the default is
  right for the large majority, and the value is shown for the user to
  confirm, never forced.
- **The hive reader skips LOG replay and checksum verification.** By
  design (a Fast-Startup hive is readable and Ferry refuses dirty volumes
  anyway), but it means a setting changed in the final seconds before an
  unclean shutdown, and living only in the transaction LOG, is not seen.
