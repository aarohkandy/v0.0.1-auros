# Accounts: who signs in, and where the first password comes from

Written 2026-09-21. Internal. Nothing here is a customer claim.

**Owner decision (2026-09-21, HUMAN):** user accounts come from the recipe plus a first-boot unit. A
recipe declares the accounts, with no passwords or secrets. A first-boot oneshot in the base image
creates them. The first secret comes from a per-school enrolment/install bundle. The goal is 180
identical laptops where nobody types anything per machine.

Why this is needed at all: KDE's first-boot wizard is suppressed (auros-base `a5a8f75`). Without the
wizard, nothing creates an account, and every non-kiosk laptop boots to a sign-in screen with nobody
on it. That commit's detector called this the `FIRST-BOOT ACCOUNT GAP`.

**Owner answers (2026-09-21):** A1 yes, A2 Auros generates, A4 Users page for `aurosadmin` on
managed/locked, A6 yes for every account. A1, A2 and A4 are built (§2, §3, §8). A6 is built and
**switched off**, because the graphical login screen can't handle an expired password (§5).

**Owner decisions (2026-09-21, second round), built:** (1) the forced first change is a mandatory
"choose your password" step in the first-run flow instead (§5.1); (2) on locked a pupil may change
their own password in the GUI and nothing else (§3). A3, A5 and A7 are still open (§9).

---

## 1. The recipe field (built)

```yaml
policy: managed
accounts:
  - { name: school-it, role: admin, display_name: School IT }
  - { name: pupil,     role: user,  display_name: Pupil }
```

| Rule | Where it is enforced |
|---|---|
| Required for every policy except `kiosk`. Refused under `kiosk`. | schema, conditional `allOf` |
| 1 to 20 accounts, and at least one `admin` | schema, `minItems`/`maxItems`/`contains` |
| `name`: `^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$`, 2–32 characters | schema |
| `name` may not be a system or policy name: `root`, `adm`, `wheel`, `users`, `aurosadmin`, `auroskiosk`, `aurosprobe`, `sddm`, `plasmalogin`, `systemd-*`… | schema, `not enum` / `not pattern` |
| Two accounts may not share a `name` | `src/validate.ts` and `schema/validate.py` (`uniqueItems` compares whole objects) |
| `role` is `admin` or `user`. There is no third role. | schema enum |
| `display_name` is a `line` with no `:` or `,` (the passwd GECOS field splits on both) | schema |
| No secret of any kind. A `password`, `token` or `secret` key inside an account is refused with the "No credential of any kind" sentence, not the generic "unknown key" one | schema `refusals/secrets` applied to each item. `schemaRefusals` and `validate.py` now look up by-name refusals on the object where they fired |

The compiler writes `/usr/lib/auros/accounts.json` (`{"schema":1,"accounts":[…]}`, sorted by name,
so reordering the list builds the same image) through the same base64 `files` path it uses for
branding. A kiosk gets no file. `explain` lists each account.

## 2. Where the first secret comes from

The two candidates:

| | Enrolment bundle on the install media | First-boot prompt shown only to the admin |
|---|---|---|
| Terminal needed (D4) | no | no |
| Typing per machine | **none** | a password on every one of 180 laptops |
| Verdict | **chosen** | rejected: it fails the owner's goal as stated |

**What the bundle is:** one root-only file that ends up at `/etc/auros/enrolment/accounts.secret` on
the installed laptop. It has one line per account, `name:<crypt(3) hash>`. That is the input format of
`chpasswd -e` (man chpasswd: *"-e, --encrypted Supplied passwords are in encrypted form"*). It holds a
hash, never a cleartext password, so even the bundle doesn't contain the password itself.

**How it gets onto the laptop (A1, built):**
`tools/make-install-media.sh <digest ref> --enrolment FILE`.

- FILE must be mode 0600 or 0400, with `name:<crypt(3) hash>` lines (`#` comments are allowed). A
  value that isn't a `$…` hash is refused as a plain password. The script never hashes and never
  echoes one. A refusal names the line number, not the line.
- After the pull it reads the image's own `/usr/lib/auros/accounts.json` and refuses when:
  - the image declares accounts and no `--enrolment` was given,
  - FILE names an account the image doesn't declare ("is this the right school's file?"),
  - no declared admin gets a hash,
  - `--enrolment` is given for an image with no accounts (a kiosk).
- The hashes go into bib's `[customizations.installer.kickstart] contents` as a base64 payload in a
  `%post --erroronfail`, which writes `/etc/auros/enrolment/accounts.secret` mode 0600. That is where
  `auros-accounts` reads it. bib's README: once a kickstart is given, *"No other kickstart commands are
  added by bootc-image-builder"* except `ostreecontainer`. So our kickstart also carries the
  unattended-install lines from the README's example (`text --non-interactive`, `zerombr`, `clearpart
  --all`, `autopart --noswap --type=lvm --fstype=xfs`, `network`), plus `reboot --eject`.
- The three checks from the earlier draft:
  1. `%post` allowed: the README only forbids repeating `ostreecontainer`. **Not proven on an ISO.**
  2. `%post` lands in the deployed `/etc`: **not verified on a VM.** If it doesn't, the first boot
     shows the §5.6 on-screen failure. It fails loudly, not silently.
  3. Anaconda's kickstart copies: `auros-accounts` now deletes `/root/anaconda-ks.cfg` and
     `/root/original-ks.cfg` when they contain the enrolment `%post`, on every exit path.
- **The ISO is secret-bearing.** The script's final message says so, and says to keep the sticks apart
  from the password sheet.

**Making FILE (A2, built):** `tools/make-enrolment.sh --out FILE school-it pupil`, run by Auros
staff on the Linux build machine.

- **One password per account.** A single shared password would give every pupil the IT password.
- Each password is 20 characters from a 31-letter alphabet with no look-alikes, grouped in fives
  (about 99 bits, from `/dev/urandom`).
- The passwords are printed once, to a terminal. A stdout that isn't a terminal is refused before
  anything is made.
- Hashing uses `openssl passwd -6 -stdin`: SHA-512 crypt `$6$`, with the password on stdin and never
  in argv. Only `name:hash` lines are written, mode 0600, and an existing file is never overwritten.
  macOS's LibreSSL can't make `$6$`, and the script says so.
- **Why `$6$` and not yescrypt:**
  - `chpasswd -e` stores the value as given (man chpasswd: *"Supplied passwords are in encrypted
    form"*). pam_unix then verifies it through libxcrypt, which supports both `$6$` (sha512crypt) and
    `$y$` (yescrypt, Fedora's default for new passwords, crypt(5)).
  - Stock OpenSSL can make `$6$`. Yescrypt would need `mkpasswd` from another package.
  - At ~99 random bits, the hash's work factor isn't what protects the password.
  - Measured on Ubuntu 24.04's libxcrypt: an `openssl passwd -6` hash verifies against its password
    and rejects a wrong one. That wasn't re-measured on a Fedora 44 image, because the build machine's
    disk is full.

**What a school admin actually does:**

1. Receives the USB stick(s) from us, plus the initial admin password. The password travels separately
   from the stick: a sealed sheet or a phone call, never email with the stick.
2. On each laptop: plugs in the stick and chooses it in the boot menu. That boot-key press is the one
   per-machine action, and it was already there. The install is unattended.
3. Takes the stick out when the laptop restarts. The laptop starts at a sign-in screen listing
   "School IT" and "Pupil". The admin signs in with the password from the sheet.

No terminal is involved, and nothing is typed per machine beyond choosing the boot device.

**Who makes the password (A2, owner: Auros):** see "Making FILE" above. The school never chooses the
first password, and Auros keeps no copy: it exists on the handover sheet only.

## 3. Changing passwords later (GUI)

- **open:** the admin is in `wheel`, the polkit admin identity there. System Settings → Users
  (`kcm_users`) changes any account's password after the admin types their own. No terminal.
- **Every mode but kiosk: a person may change their OWN password (owner decision 2, built).**
  - accountsservice checks `org.freedesktop.accounts.change-own-password` for `SetPassword` /
    `SetPasswordMode` when the caller's uid is the target's, and `user-administration` for anyone
    else (src/user.c `user_set_password`). Upstream default: `auth_admin`
    (data/org.freedesktop.accounts.policy.in). KDE's Users page calls exactly that `SetPassword`
    with a crypt(3) hash (plasma-workspace kcms/users/src/user.cpp `User::setPassword`).
  - `policy/common/root/etc/polkit-1/rules.d/10-auros-own-password.rules` answers **YES** for that
    one id, only for a subject in an active local session (a person at the seat; a daemon with no
    session keeps `auth_admin`). locked and managed leave the id out of their guarded set so it
    reaches that file; kiosk still refuses it.
  - Honest cost: no current-password prompt. Someone at an unlocked, unattended session can change
    that person's password; IT resets it. `AUTH_SELF` would ask, but `pkcheck` answers 2 for both
    `auth_self` and `auth_admin`, so it could not be proven on a machine. **[OWNER: acceptable?]**
- **locked (owner decision 2, built):** the Users page is shown to **everyone** (`policy/locked`
  no longer ships `/etc/kde5rc` or the env script). For a pupil it can only change their own
  password: `user-administration` and `change-own-user-data` (name, picture) stay `NO`;
  `aurosadmin` gets `AUTH_ADMIN`.
- **managed (A4, built):** System Settings → Users (`kcm_users`) is shown to `aurosadmin`
  members only.
  - **Hidden by default.** `kcm_users` moved out of the immutable `[KDE Control Module Restrictions][$i]`
    group in `/etc/xdg/kdeglobals`, into `policy/managed/root/etc/kde5rc` as a per-key
    immutable `kcm_users[$i]=false`.
    - It has to be per key: KConfig reads `/etc/kde5rc` first, so a group-level `[$i]` there would lock
      the group before kdeglobals added its pages.
    - The file name is literal in KF6 (D25).
  - **Shown to admins.** `/etc/xdg/plasma-workspace/env/50-auros-admin-users-page.sh` sets
    `KDE_SKIP_KDERC=1` only when `id -nG` contains `aurosadmin`. kconfig.cpp checks that variable at
    runtime and then skips `/etc/kde5rc`. startplasma sources these env scripts before it passes the
    environment to D-Bus and systemd.
  - **The lock is polkit.** `org.freedesktop.accounts.*` except `change-own-password` is
    `AUTH_ADMIN` on managed for anyone, members included. A pupil can
    un-hide the page by setting the variable in their own session, just as they could escape any
    kdeglobals restriction with `XDG_CONFIG_DIRS`, and it gets them nothing.
  - A managed pupil still changes their own password (the first-run step opens the page with
    `KDE_SKIP_KDERC=1`), but has no menu entry for it afterwards. **[OWNER: show the page on managed
    too, as on locked?]**
- **Proven by:**
  - `20-policy.test.sh` evaluates the real rule files (node, polkit's first-answer-wins order): a pupil
    at the seat gets `yes` for own password on locked/managed/open and not with no session; kiosk
    refuses; locked refuses a pupil `user-administration` and `change-own-user-data`, gives
    `aurosadmin` `auth_admin`.
  - B5 `a_suite_accounts`: `user-administration` 1 on locked, not 0 on managed, `open` the control;
    and `change-own-password` must be exactly 2 for the sessionless probe (1 = the mode's deny ate the
    pupil's only door; 0 = granted without a session).
  - B12: `AUROS_MODE_B12_USERS=own-password` on locked. For a pupil the page must open,
    `change-own-password` must be 0 in the session, `user-administration` exactly 1. For
    `aurosadmin`, `user-administration` must be answerable. managed keeps `aurosadmin`.
    `b12-modes.test.sh` drives pupil and admin in both modes. B12 now reads pkcheck 2 as the
    challenge (it read only 3, so an admin's prompt would have failed as an "error").
  - **Not yet seen on a VM:** any of it in a real Plasma session.
- **Fleet-wide rotation [OWNER, honest limit]:** each laptop keeps its own `/etc/shadow`. Changing the
  admin password on 180 machines means 180 changes, or a new bundle and a reinstall. There's no central
  directory, and a fleet console is out of scope here. Every laptop enrolled from one bundle shares one
  initial hash, so one leaked password opens all of them until each is changed.

## 4. Kiosk

No human account. Recipes can't declare one (the schema refuses it). The base's `auroskiosk` sysusers
account runs the session. On a kiosk, the first-boot script deletes any stray enrolment file unused,
creates nothing, and writes its done-marker.

## 5. The first-boot unit (built: auros-base `desktop/accounts/`)

`auros-accounts.service`: `Type=oneshot`, `RemainAfterExit=yes`,
`ConditionPathExists=!/var/lib/auros/accounts.done`, `Before=display-manager.service
systemd-user-sessions.service`, `WantedBy=multi-user.target`. Flags are cited from man systemd.unit and
systemd.service in the unit file. It uses a marker, not `ConditionFirstBoot=`, because that condition
is tied to an unpopulated `/etc`, and the installer has already populated `/etc`.

`/usr/libexec/auros/auros-accounts`:

1. Reads `/usr/lib/auros/policy-mode`. On `kiosk` it does what §4 says.
2. Re-validates every entry of `accounts.json`, because it runs as root and a name reaches `useradd`.
3. Creates each missing account with `useradd -m -c <display_name> <name>` (man useradd `-m`, `-c`). An
   existing account with uid ≥ 1000 is left alone, which makes the script idempotent. A name that
   belongs to a system uid is refused.
4. Adds each admin with `usermod -a -G <group>` (man usermod `-a`, `-G`). The group is **`wheel` on
   open and `aurosadmin` on managed/locked**. The brief said "admin in wheel". On managed/locked,
   `aurosadmin` is the only group their sudoers and polkit rules honour. Putting the admin in `wheel`
   there would also trigger upstream wheel-only polkit rules that the lockdown doesn't expect (see
   `policy/lib/assert-lib.sh` on `22-ublue-rebase-systemd.rules`). **[OWNER: confirm]**
5. If the enrolment file exists, it must be owned by root with mode 0600 or 0400. Otherwise it is
   deleted unused and the run fails. The script takes lines only for declared accounts whose value
   starts with `$`. Cleartext is ignored and never printed. It pipes the pairs to `chpasswd -e` on
   **stdin**, never `useradd -p`, which man useradd flags as visible to anyone listing processes. It
   then **deletes the file on every path**, success or failure.
6. Counts the declared accounts with a usable hash, meaning `getent shadow` isn't empty, `!…` or `*…`.
   **If that count is zero, it fails on screen:**
   - it writes the reason to `/etc/issue.d/50-auros-accounts.issue` (agetty prints `/etc/issue.d`),
   - sends `plymouth display-message --text=…`,
   - writes `/run/auros/accounts-failed`, which a `display-manager.service.d` drop-in checks through
     `ExecCondition=`. That skips an empty graphical sign-in screen without marking the unit failed.
     man systemd.unit says drop-ins for an alias are loaded.

   It writes no done-marker, so the next boot tries again.
7. On success it writes `/var/lib/auros/accounts.done` and removes any old failure notice.

A `user` account with no hash in the bundle is created locked, and the script says so in the journal.
An admin can set its password later (§3). **[OWNER: should pupil accounts get a shared class password
from the bundle, or should they stay locked until an admin sets one?]** The recommendation is the
bundle, because otherwise it's per-machine typing again.

**Force a change at first sign-in (A6: owner said yes; built, SWITCHED OFF).** With
`AUROS_EXPIRE_FIRST_PASSWORD=1` in the unit's environment, `auros-accounts` runs `chage -d 0` on every
account it creates, after `chpasswd -e`. The order matters, because chpasswd resets the last-change
day. man chage: *"If the LAST_DAY is set to 0 the user is forced to change his password on the next
log on"*. The shipped unit has that line commented out, and a test fails if it's switched on.

**Why it's off: Plasma Login (`plasmalogin`) can't finish a sign-in with an expired password.** From
plasma-login-manager master:

- `PamHandle::acctMgmt()` sees `PAM_NEW_AUTHTOK_REQD` and calls
  `pam_chauthtok(PAM_CHANGE_EXPIRED_AUTHTOK)`.
- The helper then asks the daemon for a three-prompt change request (current, new, repeat).
- `Display::slotRequestChanged()` only answers requests of one prompt (the password) or two (user and
  password). With three, it does nothing, so the change is never answered and the sign-in never
  completes.
- Expired-password support exists only on an unmerged `expired-password` branch: one 2023 commit
  porting SDDM PR #776.

A TTY `login` would handle it, but that's a terminal (D4). Switching it on as things stand would lock
every account on every laptop out of the graphical sign-in at the same moment.

### 5.1 Choose your password at first sign-in (owner decision 1, built)

Expiry stays off. Instead:

1. **Marker, root's.** When `auros-accounts` sets first passwords from the enrolment file, it writes
   `/var/lib/auros/choose-password/<name>` (dir 0755, file 0600 root) holding the SHA-256 of the
   hash it set, never the hash. Accounts with no first password get none.
2. **Cleared only by a real change.** `auros-password-chosen.path` (`PathChanged=/etc/shadow`, enabled)
   starts `auros-password-chosen`, which removes a marker only when that account's shadow hash no
   longer matches. The user can't delete or forge it; the only way through is to set a password.
3. **The step.** User unit `auros-choose-password.service`
   (`ConditionPathExists=/var/lib/auros/choose-password/%u`, `TimeoutStartSec=infinity`) runs at every
   sign-in until the marker is gone: a `kdialog` explains, then `KDE_SKIP_KDERC=1 kcmshell6 kcm_users`
   opens KDE's Users page on the signed-in account ("Change Password", polkit as in §3). Closing it
   without choosing loops back. `auros-first-run.service` is `After=` it, so the taskbar layout and
   the welcome wizard come only after. The build installs `kdialog` if missing and dies without
   `kdialog` or `kcmshell6`.
4. **Honest limits.** Plasma itself is up behind the dialog; a pupil can ignore it and use the bare
   desktop, but it returns at every sign-in until done. An admin resetting the password also clears
   the step. Accounts first given a password later by an admin are not asked. Setting the same
   password again counts (a new salt makes a new hash).
5. **Proven by** `accounts.test.sh` (marker content and modes, clearing red/green, the step ending
   only once root clears it) and `40-windows-feel.test.sh` (install/tool checks). Not seen on a VM,
   including whether `PathChanged=` fires on shadow-utils' rename of `/etc/shadow`.

If plasma-login-manager merges expired-password support, A6's one line can be switched on instead,
after a VM proves it.

## 6. Security properties

| Property | How it holds | Test |
|---|---|---|
| No secret in the public recipe | schema has no field; secrets refusal inside each account | recipes `refusals.test.ts`, `refusals.test.py` |
| No secret in the image | `40-windows-feel.sh` dies if `/etc/auros/enrolment` exists at build time | `40-windows-feel.test.sh` `accounts.build` |
| Secret file root-only | anything but uid 0 with mode 0600/0400 is deleted unused and the run fails | `accounts.test.sh` |
| Deleted after use | `forget_bundle` on every exit path | `accounts.test.sh`; prove-red mutation |
| Never cleartext | only `$…` values reach `chpasswd -e` | `accounts.test.sh`; prove-red mutation |
| Never in logs or argv | stdin only; no `set -x`; tests assert the fake hash never appears in output | `accounts.test.sh` |
| Only declared accounts get a password | undeclared names in the bundle are ignored | `accounts.test.sh` |

Residual risks, stated: the ISO carries the hash (§2). Anaconda's kickstart copies in `/root` hold it
between install and first boot, root-only, and are deleted then (§2, check 3). One weak passphrase is crackable offline from any one laptop's shadow file and then
opens all of them (§3).

## 7. What is not verified

- **Not booted.** Nothing here has run on a VM. The one behaviour a VM must show is that when the
  display manager is skipped, tty1's getty shows the issue text. Until then, the on-screen claim rests
  on the man pages cited above.
- **No enrolled ISO has been built or installed.** Three things are unproven: that bib accepts our
  kickstart with `--rootfs xfs`, that the unattended install still completes, and that `%post` writes
  into the deployed `/etc`. Before any school gets a stick, one enrolled ISO must be installed on a VM
  and signed into.
- The Users page appearing in an `aurosadmin` Plasma session (§3) has only been checked against stubs.
- `plasmalogin` and expired passwords (§5) was established by reading the source, not by booting it.

## 8. What was built

- **auros-recipes `agent/accounts`:** the schema field and rules, compile to
  `/usr/lib/auros/accounts.json`, `explain`, both validators, the examples (school: `school-it` +
  `pupil`; workstation: `maintainer`), regenerated Containerfiles, tests, and prove-red A01–A03.
- **auros-base `agent/accounts`:** merges `agent/first-boot` (a5a8f75), then adds
  `desktop/accounts/{auros-accounts, auros-accounts.service, 50-auros-accounts.conf}`, the build
  install and assertions, the detector reporting `recipe`, `tests/accounts.test.sh`, and four prove-red
  mutations (7b81e93).
- **auros-base `agent/accounts`, 0ff7c22 (owner answers):**
  - `make-install-media.sh --enrolment` (A1)
  - `tools/make-enrolment.sh` (A2)
  - `kcm_users` via `/etc/kde5rc` plus the aurosadmin env script, B5 `a_suite_accounts`, B12
    `AUROS_MODE_B12_USERS` (A4)
  - `chage -d 0` behind `AUROS_EXPIRE_FIRST_PASSWORD`, off (A6)
  - the kickstart-copy scrub
  - tests: `make-enrolment.test.sh` is new. `make-install-media`, `accounts`, `20-policy`,
    `assert-lib` and `b12-modes` gained cases.
  - 8 new prove-red mutations (`ACCT:`), all caught. The full prove-red run on Linux caught 115 of 115 after 666168d re-anchored one older mutation.
  - `run-all` passes 29/29 on macOS and on Linux, and Linux also checks each printed password against
    its hash with real OpenSSL.
- **auros-base `agent/accounts`, e737f1c (owner decisions 1 and 2):** `10-auros-own-password.rules`,
  locked's guarded-set exemption and visible Users page, the choose-your-password marker, root-side
  clearing (`auros-password-chosen` + `.path`), the session step (`auros-choose-password` + user unit),
  B5/B12 own-password assertions, and 9 prove-red `OWNPW:` mutations. `run-all` 29/29 on macOS and
  Linux; full prove-red on Linux 124/124.

## 9. Owner decisions, in one place

| # | Decision | Recommendation | Blocks |
|---|---|---|---|
| A1 | Bundle delivery: `make-install-media.sh --enrolment FILE` → kickstart `%post` | **YES (owner). Built; not VM-verified** | — |
| A2 | Who creates the first password | **Auros, per school (owner). Built: `make-enrolment.sh`, one password per account** | — |
| A3 | Pupil (`user`) accounts: shared password from the bundle, or locked until an admin sets one | from the bundle | per-machine typing |
| A4 | GUI password change on managed/locked (`kcm_users` hidden) | **Users page for aurosadmin only on managed; everyone on locked, own password only (owner). Built.** Open: show it to managed pupils too? | — |
| A5 | Admin group on managed/locked is `aurosadmin`, not `wheel` | confirm | — |
| A6 | Force a password change at first sign-in | **YES (owner). Expiry built, OFF (plasmalogin, §5); the first-run "choose your password" step is built instead (§5.1).** Open: own-password with no current-password prompt, OK? | — |
| A7 | One initial hash shared across a fleet (rotation = per machine or reinstall) | accept for now, state it to customers | — |
