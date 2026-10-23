# When something is broken: five playbooks

For the owner, the second responder, and any agent. Commands that only read
(`monitor.py`, `gh release list`, `waves.py state`) were run against today's
repository. The commands that change something public (demote a release, delete
the signed index, pause) were run on a throwaway prerelease by the drill
(`drill.yml`, results in `docs/ops/drills.md`), not on the real release or the
real `apt` tag. Where a safeguard the plan promises is **not built yet**, the
playbook says so and gives what works today. Target times are for a person who
has this page open.

**You are probably here because** the monitor opened a GitHub issue labelled
`monitor` and assigned it to you (tools/ops/monitor.py; docs/ops/MONITOR.md),
or someone wrote to say AurOS lost their files. If it is about her files or
Windows, go straight to playbook 5 and do not read anything else first.

## Before anything: the same five lines every time

```sh
cd <your checkout of the repository>           # any: gh + python3 are all you need
R=$(sh tools/repo.sh)                          # owner/name, e.g. aarohkandy/v0.0.1-auros
gh auth status                                 # logged in as the owner (or the second responder)?
python3 -I tools/ops/monitor.py --deep         # what is broken, right now, with the reason
gh release list -R "$R" -L 8                   # what is published; the one marked Latest is the download
```

Pausing and resuming the public download (**needs the owner's domain and
edge Worker**, `docs/ops/DOMAIN.md`; before that there is no gate and the
equivalent is "demote the release", below):

```sh
export AUROS_DOMAIN=<the domain>               # or EDGE_URL=https://get.<the domain>
OPS_TOKEN=<owner token> python3 -I tools/ops/waves.py pause "why, in one line"
HALT_TOKEN=<second responder token> python3 -I tools/ops/waves.py pause "why"   # pausing is all this token can do
python3 -I tools/ops/waves.py state            # public; shows paused and the reason
OPS_TOKEN=<owner token> python3 -I tools/ops/waves.py resume
```

While paused, people who press **Get AurOS** see "AurOS is paused for a little
while ... your computer is not affected". Installs already under way keep
fetching their pieces; those are never paused.

**Severity.** Sev-1: someone may have lost files or Windows (playbook 5), or the
update key may have leaked (playbook 3b). Pause first, understand second. Sev-2:
the download or the updates are broken for everyone. Sev-3: Wishes, the website.

---

## 1. The download is broken
*Monitor: `download`, `pieces` or `site` is down. Target: someone knows in 15 minutes
(the monitor), downloads paused or fixed in 30.*

**See which it is.** The issue's first line says; the same line from the shell:

```sh
python3 -I tools/ops/monitor.py --deep
```

| It says | It means | Do |
|---|---|---|
| `.../latest/download/AurOS-Setup.exe answered 404` | no full (non-pre) release is the latest | 1a |
| `answered 503` with the gate's page | not a fault: the wave is full or closed | nothing; `waves.py state` |
| `hashes to ..., SHA256SUMS says ...` | the published file is not the one that was tested | **2** (a bad release), after 1c |
| `pieces.txt ... 404`, or `piece ... answered 200 to a Range request` | an image piece is missing or the host ignores Range | 1b |
| `answered nothing (no answer)` for every address | GitHub (or your connection) is down | 1c |

**1a. Nothing is "Latest".** A release is the public download only after the
promote step made it so (docs/RELEASE.md).

```sh
gh release list -R "$R" -L 8                                   # which tags exist, which is Latest, which are pre-releases
gh release view <TAG> -R "$R" --json isPrerelease,assets --jq '{pre:.isPrerelease, files:[.assets[].name]}'
# the last release that passed everything is the right one:
gh release edit <TAG> -R "$R" --prerelease=false --latest      # make it the download again
python3 -I tools/ops/monitor.py --deep                         # download: OK, with the hash compared
```

**1b. A piece is missing** (the release has fewer `auros-desktop.img.gz.NNN` than
`pieces.txt` lists):

```sh
gh release view <TAG> -R "$R" --json assets --jq '.assets[].name' | sort
gh run list -R "$R" --workflow build.yml -L 5                  # the run that built it
gh run download <RUN-ID> -R "$R" -n auros-pieces -D /tmp/pieces
gh release upload <TAG> -R "$R" --clobber /tmp/pieces/auros-desktop.img.gz.00N
```
Then run the monitor with `--deep` again. (The artifacts expire after 14 days; if they
have, rebuild with a commit whose subject says `[release]`.)

**1c. GitHub is down.** https://www.githubstatus.com. Nothing of ours is wrong and
nothing can be fixed from here. Pause the download so nobody starts something that
cannot finish (`waves.py pause "GitHub is down"`), and resume when
`python3 -I tools/ops/monitor.py` is green. (A mirror, `WP-60`, is not built yet.)

**Without the gate** (before the domain exists): to stop strangers getting a broken
file, take the release off "Latest": `gh release edit <TAG> -R "$R" --prerelease`.
The button then fails to download instead of delivering the broken file. The website
that is already published keeps saying "Get AurOS".

---

## 2. A bad release: the installer or the image may harm someone
*A test found it late, or a person reported it. Target: nobody new gets it in 15 minutes,
a fixed release in hours.*

**First, stop the bleeding (5 minutes).**

```sh
OPS_TOKEN=... python3 -I tools/ops/waves.py pause "bad release <TAG>: <one line>"
gh release edit <TAG> -R "$R" --prerelease                     # off "Latest"; the button stops delivering it
gh api repos/$R/releases/tags/<TAG> --jq '.assets[] | select(.name|test("Setup|SHA256")) | [.name,.download_count]|@tsv'
```
The last line is how many times the installer was downloaded in total (GitHub's own
count; the only count there is until the field numbers, WP-73, exist).

**If the harm is to her files or Windows, you are in playbook 5 as well.**

**Then fix forward.** There is no way to recall a file from a computer. A fix is a
new release: commit the fix, push with a subject containing `[release] [latest]`
(`docs/RELEASE.md`: build, every test, e2e on that exact file, then promote). Check:

```sh
gh run list -R "$R" -L 5
python3 -I tools/ops/monitor.py --deep                         # the new file and its SHA256SUMS agree
OPS_TOKEN=... python3 -I tools/ops/waves.py resume             # only when you are sure
```

**Computers that already installed the bad release** take the fix through the update
channel (playbook 3, "a fix"): a fixed `auros-*` package with a higher version.

**Not built yet** (WP-04a/b): a signed `withdrawn.txt` that makes an old installer say
"get the new one". Until it exists an old copy of the exe on someone's desktop still
installs the old image.

---

## 3. The update channel is broken or compromised
*Monitor: `channel` is down; or a package broke a computer; or the signing key may be
known to someone else. Target: updates halted in 10 minutes.*

The channel is the assets of the prerelease tag `apt` (docs/security/UPDATES.md).
Every installed computer asks it daily and installs what is signed.

**3a. A published update is broken (computers misbehave, or the monitor's `channel` is
down because the index does not verify).**

Halt now. **Not built yet:** the `[apt-halt]` switch (WP-29). What works today is
taking the signed index away, so computers find nothing new and install nothing.
The drill ran exactly these three deletions on a throwaway tag, never on the real `apt`
tag (docs/ops/drills.md):

```sh
gh release view apt -R "$R" --json assets --jq '.assets[].name' | tee /tmp/apt-assets.txt     # keep this list
for f in InRelease Release Release.gpg; do gh release delete-asset apt "$f" -R "$R" -y; done
python3 -I tools/ops/monitor.py                                  # channel: now reports 404: that is the halt working
```

Fix forward (computers never go back; apt refuses an older version): put the fix in
the repository, release it (`[release] [latest]`), and the channel publishes the new
version by itself after the promote. For a fix that cannot wait for a full release:
a commit whose subject says `[apt <TAG>]` publishes that prerelease's packages
(docs/security/UPDATES.md, "Publishing"). It signs, uploads the packages first and the
signed index last, reads everything back and has a real apt fetch it. Then:

```sh
python3 -I tools/ops/monitor.py                                  # channel: OK
```

**3b. The signing key may have leaked, or someone else published to the channel
(Sev-1).** Whoever has that key can install anything, as root, on every computer
that has not yet moved to a new key.

```sh
# 1. halt, as in 3a (delete InRelease, Release, Release.gpg from the apt release)
# 2. see who put what there, and when
gh api repos/$R/releases/tags/apt --jq '.assets[] | [.name,.uploader.login,.updated_at]|@tsv'
gh api "repos/$R/events" --jq '.[] | [.created_at,.type,.actor.login]|@tsv' | head -30
# 3. the old key must stop signing: delete the secret now, then rotate
gh secret delete AUROS_APT_SIGNING_KEY -R "$R"
# 4. a new key: docs/OWNER-SETUP.md 4a (Option A), with the uid "AurOS updates <year>"
```
Then follow **docs/security/UPDATES.md, "If the key leaked"** to the letter: the one
update that tells computers to trust the new key must be signed by both keys, and
carries a keyring holding the new key only. Put both private keys in the secret for
that publish: `cat old.PRIVATE.asc new.PRIVATE.asc | gh secret set AUROS_APT_SIGNING_KEY -R "$R"`;
then, after it, only the new one. If the *GitHub account* may be taken over, first:
change the password, sign out all sessions, revoke every token and deploy key
(`gh auth status`; Settings -> Applications; Settings -> Developer settings), check
`Settings -> Security log`, and look at the last commits: `git log --format='%h %an %ae %s' -20`.
Rotating the key does not help if the attacker can still push.

**Say so plainly** to anyone who asks: what you know, what you did, and that the
channel was halted at HH:MM. A published notice to installed computers (WP-76) is not
built yet.

---

## 4. Wishes: a cost spike, or it is down
*Monitor: `wishes` is down; or the phone alert "half of today's budget"; or Anthropic's
bill. Target: spending stopped in 10 minutes.* Details of every knob: docs/CLOUD.md section 5.

**Fastest first** (each is independent):

1. Anthropic Console -> Settings -> Workspaces -> *AurOS Wishes* -> Limits: lower the
   monthly limit to what has been spent. Instant; nothing else can then spend more.
2. Budget to zero, 2 minutes (every wish is then answered "resting" and costs nothing):
   ```sh
   sed -i.bak 's/^DAILY_BUDGET_USD = ".*"/DAILY_BUDGET_USD = "0"/' cloud/wrangler.toml && rm cloud/wrangler.toml.bak
   git commit -am "Wishes: budget to zero while we look [cloud]" && git push origin HEAD:main   # cloud.yml deploys it
   ```
   (`cloud/src/ledger.js`: a pool with no budget answers 503 "budget"; her computer says
   "resting". The monitor's `wishes` check stays green: the service is answering.)
3. Switch it off completely: Cloudflare dashboard -> Workers & Pages -> `auros-wishes`
   -> Settings -> Domains & Routes -> disable workers.dev. Her computers say "Wishes is
   not available"; nothing else is affected.

**See what is happening** (needs `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` in your environment):

```sh
cd cloud && npm ci --silent && npx wrangler tail auros-wishes --format json | head -50
# each line: ev, status, reason ("budget", "rate_limit", "busy", "auth", ...), limited, tag, cost
```
Abuse (`limited:"register"` or `"quota"` with a `tag`): add the tag to `BANNED` in
`cloud/wrangler.toml` and push; raise `POW_BITS` by 2 for four times the work.

**An outage on Anthropic's side** (`reason:"overloaded"`, `"upstream_5xx"`): nothing to
do, https://status.anthropic.com; wishes she already made keep working (they run on her
computer). **A leaked key:** rotate it (docs/CLOUD.md section 5, "Rotate the Anthropic
key") and delete the old one in the Console.

**Not measured yet:** the share of requests answered "resting" (it needs a counter the
Worker does not publish; ask stream CL). Until then `wrangler tail` is the only view.

---

## 5. Someone says she lost her files, or Windows will not start (Sev-1)
*An email, an issue, a message. Target: downloads paused in 15 minutes, the owner has
read it within the hour, a human reply to her the same day.*

**1. Pause the public download.** Any suspected data loss pauses it by rule
(docs/ops/LAUNCH.md); do it by hand now and look afterwards:

```sh
OPS_TOKEN=... python3 -I tools/ops/waves.py pause "possible data loss, case <short id>"
```
Without the gate: `gh release edit <TAG> -R "$R" --prerelease`.

**2. Tell her what to do, in these words, by the same channel she used**
(never a phone call; AurOS never phones anyone, `docs/SUPPORT.md`):

> We are sorry, and we are looking at this today. Please do these things and nothing else:
> switch the computer off and leave it off. Do not reinstall Windows, do not press
> "repair" or "reset this PC", and do not run anything a website tells you to. Keep the
> memory stick if you used one. If you can, tell us what the screen said last, and the
> short code on it. Your files are very probably still there: a program that moves a
> disk's layout does not erase what is inside it. We will write to you again by tonight.

**3. What the owner or a helper can do, in this order (each step is safer than the next).**
Everything below is read-only until step 5, and step 5 is done on a *copy*:

```sh
# a Linux computer (the homebase box, or any live USB) with the affected disk attached as /dev/sdX, read-only:
lsblk -o NAME,SIZE,FSTYPE,LABEL,PARTTYPENAME /dev/sdX
sudo sgdisk --print /dev/sdX                    # the partition table as it is; compare with the capture below
sudo blockdev --setro /dev/sdX                  # from here the kernel refuses writes to it
# image the whole disk first (to another disk with room), so every later try can be repeated:
sudo ddrescue -d -r3 /dev/sdX /mnt/big/her-disk.img /mnt/big/her-disk.map
```
What AurOS keeps to put things back: the capture of her partition table, the whole EFI
partition and each Windows volume's boot sector is in `journal.json` and the recovery
payload (docs/AURBRIDGE.md, "The recovery payload" and "Putting it back"). **Put Windows
back** restores them from the staging environment, and is the first thing to try on a
computer that still starts AurOS (Settings -> Put Windows back). Her files in Windows are
copied into Windows first by the "Bring my files" step only once WP-27 lands: **not built**.

Only then, and only on the *copy*: `testdisk /log her-disk.img` (partition recovery),
`photorec` (files by content). **Never** run `ntfsfix`, `chkdsk /f` or `ntfsresize`
on her real disk first: they write.

**Not built yet:** the "AurOS Rescue" stick a helper can boot to copy her files off and run
Put Windows back (WP-82), and `docs/ops/SEV1.md`. Until then, the steps above, by a person.

**4. Write it down the same day:** what she ran, when, the verdict code, the build
(`AurOS-Setup.exe --report` output if she can run it), what you did, and what came back.
Put it in a private issue, not a public one. If it is a real defect it becomes a test
that fails first (`docs/issues/`), and the wave stays paused until that test is green.

**5. Resume only the owner, only after** the cause is understood and a test proves it
cannot repeat: `OPS_TOKEN=... python3 -I tools/ops/waves.py resume`.

**6. Keep the record, and mind what is said (LIA-5; written without a lawyer, D15).** If she
lost something real, what we write now may be read later by a stranger:

- **Preserve everything the day it is reported:** her message or issue thread (never delete or
  edit it, or any email in the thread), the release tag and its `SHA256SUMS`, the build commit,
  the Actions run of that build and its logs (artifacts expire after 14 days, installer artifacts
  after 30, logs after 90: download the run's evidence, `gh run download <id>`, into a private
  place the same day).
- **The first message says what is known and what to do** (step 2) and nothing about whose fault
  it is, in either direction ("it was your computer", "it was our bug"). It does not ask her
  to sign, agree to, or release anything.
- **Never make help depend on a release or a promise.** If the owner later decides to pay or
  refund anything, that is the owner's decision alone: a short, plain optional "no admission"
  release, time to think about it, no gag on her speaking, and the insurer's written consent
  first if there is insurance (`docs/legal/COMPLIANCE.md` LIA-7). An unconditional payment is
  not a way to end a claim; do not offer one on the spot.
- **The stop rules are the ones in `docs/ops/LAUNCH.md`.** Do not argue with them to resume.
- What she sends us (a log, a photo of her screen) is covered by the privacy page: kept only as
  long as needed, normally no more than 12 months; keep the thread in the private issue, not a
  public one.

---

## After any incident

- Close the monitor's issue (it closes itself when the check is green; if it was a human
  report, close it by hand with what you found).
- Write three lines in `docs/ops/drills.md` under "Real incidents": what happened, how long
  to notice, how long to fix, and what would have made it faster. Fix that thing.
- If a playbook above was wrong or missing a step, fix it in this file in the same commit.

Rehearsal: `drill.yml` runs playbooks 1 and 3a against a throwaway prerelease and reports how
long detection and recovery took (`gh workflow run drill.yml`; results in `docs/ops/drills.md`).
