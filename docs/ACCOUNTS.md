# Accounts: who signs in, and where the first password comes from

Written 2026-09-21. Internal. Nothing here is a customer claim.

**Owner decision (2026-09-21, HUMAN):** user accounts come from the recipe plus a first-boot unit. A
recipe declares the accounts, with no passwords or secrets. A first-boot oneshot in the base image
creates them. The first secret comes from a per-school enrolment/install bundle. The goal is 180
identical laptops where nobody types anything per machine.

Why this is needed at all: KDE's first-boot wizard is suppressed (auros-base `a5a8f75`). Without the
wizard, nothing creates an account, and every non-kiosk laptop boots to a sign-in screen with nobody
on it. That commit's detector called this the `FIRST-BOOT ACCOUNT GAP`.

Each open question is marked **[OWNER]** below and collected in §9. The parts that don't depend on one
are built (§8).

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

**How it gets onto the laptop [OWNER — delivery mechanism, not built]:** the recommendation is that
`tools/make-install-media.sh` takes `--enrolment FILE` and embeds the file in the ISO's kickstart.
bootc-image-builder accepts `[customizations.installer.kickstart] contents` for `anaconda-iso`. Its
README: *"Users can include kickstart file content that will be added to an ISO build"*. The kickstart
would get a `%post` that writes the file mode 0600 into the target's `/etc`. Before building this,
check three things:

1. bib's README doesn't say whether `%post` is allowed. It only says not to repeat `ostreecontainer`.
   It also says kickstart can't be combined with `[customizations.user]`, which we don't use.
2. Whether `%post` for an ostreecontainer install runs chrooted into the deployment, so that `/etc` is
   the deployed `/etc`.
3. Whether anaconda copies the kickstart, and so the hash, to `/root/anaconda-ks.cfg` on the target.
   If it does, the first-boot script must also delete that copy.

Either way, **the ISO file itself becomes secret-bearing.** It's a per-school object that should be
handled like a key, and the one-line `dd` instruction should say so.

**What a school admin actually does:**

1. Receives the USB stick(s) from us, plus the initial admin password. The password travels separately
   from the stick: a sealed sheet or a phone call, never email with the stick.
2. On each laptop: plugs in the stick and chooses it in the boot menu. That boot-key press is the one
   per-machine action, and it was already there. The install is unattended.
3. Takes the stick out when the laptop restarts. The laptop starts at a sign-in screen listing
   "School IT" and "Pupil". The admin signs in with the password from the sheet.

No terminal is involved, and nothing is typed per machine beyond choosing the boot device.

**Who makes the password [OWNER]:** the recommendation is that Auros generates a random passphrase per
school, hashes it (yescrypt `$y$` or SHA-512 `$6$`) into the bundle, and hands the passphrase over out
of band. A school-chosen password would have to reach us, or reach a hashing tool, and both are worse.

## 3. Changing passwords later (GUI)

- **open:** the admin is in `wheel`, the polkit admin identity there. System Settings → Users
  (`kcm_users`) changes any account's password after the admin types their own. No terminal.
- **managed / locked — [OWNER, D4 conflict]:** both modes hide `kcm_users` for **everyone**
  (`policy/{managed,locked}/kdeglobals/20-control-module-restrictions.ini`). So on those machines there
  is currently **no GUI to change any password, including your own**. That breaks D4.1 for a task a
  school will certainly need. The options:
  - (a) Unhide `kcm_users` in `managed`. polkit `AUTH_ADMIN` still guards changes to other accounts,
    and a pupil can change only their own. This is the recommendation for `managed`.
  - (b) In `locked`, passwords change only by re-enrolment, which means a fresh bundle and a reinstall,
    and that gets written into `locked/description.md`.
  - (c) An Auros "change password" page that goes through polkit.

  Not changed here. It's a policy-mode decision, not an accounts one.
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

**[OWNER] Force a change at first sign-in?** `chage -d 0` (man chage: *"If the LAST_DAY is set to 0
the user is forced to change his password on the next log on"*). Off by default. Plasma Login's
handling of an expired password is unverified, and getting it wrong locks the admin out of all 180
laptops at once.

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

Residual risks, stated: the ISO carries the hash (§2). A copy may land in `/root/anaconda-ks.cfg`
(§2, check 3). One weak passphrase is crackable offline from any one laptop's shadow file and then
opens all of them (§3).

## 7. What is not verified

- **Not booted.** Nothing here has run on a VM. The one behaviour a VM must show is that when the
  display manager is skipped, tty1's getty shows the issue text. Until then, the on-screen claim rests
  on the man pages cited above.
- `make-install-media.sh` doesn't take a bundle yet (§2), so no laptop can get a password today. A
  managed or open laptop installed today boots to the on-screen failure in §5.6. That is the intended
  honest state, not a regression: it is the old GAP, now visible to the person at the machine.

## 8. What was built

- **auros-recipes `agent/accounts`:** the schema field and rules, compile to
  `/usr/lib/auros/accounts.json`, `explain`, both validators, the examples (school: `school-it` +
  `pupil`; workstation: `maintainer`), regenerated Containerfiles, tests, and prove-red A01–A03.
- **auros-base `agent/accounts`:** merges `agent/first-boot` (a5a8f75), then adds
  `desktop/accounts/{auros-accounts, auros-accounts.service, 50-auros-accounts.conf}`, the build
  install and assertions, the detector reporting `recipe`, `tests/accounts.test.sh`, and four prove-red
  mutations.

## 9. Owner decisions, in one place

| # | Decision | Recommendation | Blocks |
|---|---|---|---|
| A1 | Bundle delivery: `make-install-media.sh --enrolment FILE` → kickstart `%post` | yes, after the three checks in §2 | any laptop having a password |
| A2 | Who creates the first password | Auros generates it per school and hands it over separately | A1 |
| A3 | Pupil (`user`) accounts: shared password from the bundle, or locked until an admin sets one | from the bundle | per-machine typing |
| A4 | GUI password change on managed/locked (`kcm_users` hidden) | (a) unhide in managed; (b) re-enrol in locked, documented | D4.1 |
| A5 | Admin group on managed/locked is `aurosadmin`, not `wheel` | confirm | — |
| A6 | Force a password change at first sign-in | no, until verified on a VM | — |
| A7 | One initial hash shared across a fleet (rotation = per machine or reinstall) | accept for now, state it to customers | — |
