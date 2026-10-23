# Owner setup: every click and secret only the repo owner can do

The agents can write code, push, run CI and publish releases. They cannot
open the repository's Settings pages, so everything below needs the
owner, once per repository. **No secret value is ever stored in this
repository** — this file only says what each secret is, where it goes,
and how to make it.

Replace `OWNER/REPO` with the repository (today:
`aarohkandy/v0.0.1-auros`, branch `main`).

## 1. Turn on the website (done for aarohkandy/v0.0.1-auros)

Checked 2026-10-23 05:25 UTC with `gh api repos/aarohkandy/v0.0.1-auros/pages`:
`build_type: workflow` (Source: GitHub Actions), and the `github-pages`
environment lets `main` deploy. Only a future move needs these clicks again:


1. `https://github.com/OWNER/REPO/settings/pages` → **Build and deployment** →
   **Source: GitHub Actions**.
2. Nothing else while `main` is the default branch: the `github-pages`
   environment GitHub creates trusts the default branch. Only if a deploy
   fails with "Branch main is not allowed to deploy to github-pages":
   `https://github.com/OWNER/REPO/settings/environments` → **github-pages** →
   **Deployment branches and tags** → add `main`.
3. The site goes live on the next release (the release workflow deploys it),
   at `https://OWNER.github.io/REPO/`.

## 2. The branch we push to is the default (done for aarohkandy/v0.0.1-auros)

`https://github.com/OWNER/REPO/settings` → **Default branch** → the branch
everyone pushes to (`main`). Why: GitHub only shows a "Run workflow" button
for workflows on the default branch, and the Pages environment trusts the
default branch. In `aarohkandy/v0.0.1-auros` it already is `main`
(checked 2026-10-23); only a future move needs this again.

## 3. Workflow permissions (check once)

`https://github.com/OWNER/REPO/settings/actions` → **Workflow permissions**:
"Read and write permissions" is not required (each workflow asks for exactly
what it needs), but **"Allow GitHub Actions to create and approve pull
requests" can stay off**. If a release job fails with "Resource not
accessible by integration", this page is where to look.

## 4. Secrets (Settings → Secrets and variables → Actions → New repository secret)

`https://github.com/OWNER/REPO/settings/secrets/actions`

| Secret name | What it is | When it's needed | How to make it |
|---|---|---|---|
| `AUROS_APT_SIGNING_KEY` | the private half of AurOS's own update-signing key (OpenPGP). Computers running AurOS trust only updates sealed with it. **Not Microsoft, not a certificate, free.** | **before the first public release**: computers installed from a release made without it never get AurOS's own fixes | §4a below, about 5 minutes on your Mac |
| `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID`, `ANTHROPIC_API` (or `ANTHROPIC_API_KEY`), optional `WISHES_ALERT_URL` | the Wishes cloud service (AI) | **set (2026-10-23)**; the Anthropic key must be scoped to a workspace | §7 below |

### 4a. `AUROS_APT_SIGNING_KEY`, step by step (once, about 5 minutes)

This is the key that seals AurOS's own updates (stream S5,
`docs/security/UPDATES.md`). Make it on **your own computer**, never on a
shared or cloud machine. Do it **before the first public release**.

**Option A (recommended): one paste on your Mac.** Open Terminal and
paste this whole block. It installs GnuPG if it is missing (through
Homebrew, which it also installs if needed; it may ask for your Mac
password), makes the key in a throwaway folder, stores the private half
straight into the repository secret with `gh` (already logged in on your
Mac; the key never passes through a web page or the clipboard), saves
both halves in `~/Desktop/AurOS-update-key/` for the USB stick, and
prints the fingerprint.

```sh
command -v gpg >/dev/null || {
  command -v brew >/dev/null || /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  eval "$( (/opt/homebrew/bin/brew shellenv || /usr/local/bin/brew shellenv) 2>/dev/null)"
  brew install gnupg; }
D=~/Desktop/AurOS-update-key; mkdir -p "$D" && cd "$D" && export GNUPGHOME="$(mktemp -d)" &&
gpg --batch --passphrase '' --quick-gen-key 'AurOS updates' ed25519 sign 3y &&
gpg --armor --export-secret-keys > auros-apt-signing-key.PRIVATE.asc &&
gpg --armor --export > auros-archive-keyring.asc &&
gh secret set AUROS_APT_SIGNING_KEY -R aarohkandy/v0.0.1-auros < auros-apt-signing-key.PRIVATE.asc &&
gpg --list-keys --with-colons | awk -F: '/^fpr:/{print "FINGERPRINT: " $10; exit}'; rm -rf "$GNUPGHOME"
```

Then: write down the **FINGERPRINT** line (40 letters and digits), copy
the folder `AurOS-update-key` from your Desktop to a USB stick, put the
stick somewhere safe, and delete the folder from the Desktop (Move to
Trash, then Empty Trash). Go to step 5 below.

**Option B: by hand, with the web page.** You need GnuPG: on Linux it is
already there (`gpg --version`); on a Mac, `brew install gnupg` (or GPG
Suite).

1. Open Terminal and paste these lines (they make the key in a fresh,
   temporary folder, so nothing mixes with any key you already have, and
   leave two files in your home folder):

   ```sh
   cd ~ && export GNUPGHOME="$(mktemp -d)"
   gpg --batch --passphrase '' --quick-gen-key 'AurOS updates' ed25519 sign 3y
   gpg --list-keys --with-colons | awk -F: '/^fpr:/{print $10; exit}'
   gpg --armor --export-secret-keys > auros-apt-signing-key.PRIVATE.asc
   gpg --armor --export             > auros-archive-keyring.asc
   ```

   The third line prints the key's **fingerprint** (40 letters and digits).
   Write it down.
2. In the same Terminal, `pbcopy < auros-apt-signing-key.PRIVATE.asc`
   (the whole private key is now on the clipboard). Open
   https://github.com/aarohkandy/v0.0.1-auros/settings/secrets/actions →
   **New repository secret**. Name: `AUROS_APT_SIGNING_KEY`. Value: paste
   (it starts `-----BEGIN PGP PRIVATE KEY BLOCK-----` and ends with the
   matching `END` line). **Add secret.** Then `pbcopy < /dev/null` to
   empty the clipboard. (Another repository: its own
   `.../settings/secrets/actions` page.)
3. Copy both `.asc` files to a USB stick and put it somewhere safe. Anyone
   holding the PRIVATE file can send updates to every AurOS computer: treat
   it like the master key of a house. You need it again only to move to a
   new repository (put the same file into the new repository's secret)
   or to renew the key before it expires in 3 years.
4. Delete the copies from the computer: `rm -rf "$GNUPGHOME"
   auros-apt-signing-key.PRIVATE.asc` (keep `auros-archive-keyring.asc` if
   you like; it is the public half and is safe to share).

**Both options:**

- Your backup is now in `~/AurOS-update-key` on your Mac (only you can
  open it). **When you have a USB stick: copy that folder onto it, put
  the stick somewhere safe, then delete the folder from the Mac** (Move to
  Trash, Empty Trash). Until then the Mac is the only backup: if it is
  lost, AurOS computers can no longer be updated without a new install.

5. Tell an agent: "the update key is in, fingerprint XXXX". The agent
   pushes an `[apt]` commit; that run (`.github/workflows/aptrepo.yml`)
   prints the key's public half and fingerprint and publishes nothing. The
   agent commits the public half as
   `rootfs/usr/share/keyrings/auros-archive-keyring.asc` only if the
   fingerprint is the one you gave. From the next image on, AurOS
   computers trust updates sealed with it, and only those; every promoted
   release is then published to them automatically.

Nothing else is needed: no certificate, no company, no Microsoft account,
no payment.

Rules for every secret:
- Paste it only into that Settings page. Never into a file, an issue, a chat, or a commit.
- Keep an offline copy (a USB stick in a drawer) of the update-signing key: if it is lost,
  existing AurOS computers can no longer be updated without a new install.
- If the update-signing key may have leaked, tell an agent at once: whoever has it can
  install anything on every AurOS computer that has not yet moved to a new key. The way
  out is a new key, rotated in as `docs/security/UPDATES.md` "Key rotation" describes.

### 4b. Private security reports (1 minute, before launch)

`SECURITY.md` tells people to report problems privately through GitHub. That
is off until you switch it on: `https://github.com/OWNER/REPO/settings/security_analysis`
→ **Private vulnerability reporting** → **Enable**. (Checked 2026-10-23: off.)
Also put a security contact address in `SECURITY.md` where it says `[OWNER: ...]`.

## 5. Code signing through SignPath (decision D12; 20 minutes, in two sittings)

The pipeline is written and **off**; it turns on when you add one variable and
one secret. Full detail and the reasons: `docs/SIGNING.md` section 4. The
order:

1. **Two-factor sign-in on GitHub** (`aarohkandy`): GitHub → Settings →
   Password and authentication → Two-factor authentication. SignPath requires
   it of everyone on the team.
2. **Apply** at `https://signpath.org/apply`, pasting from
   `docs/legal/signpath-application.md` (fill the `[OWNER: ...]` spots: your
   contact email and the name you want shown). Check that
   `https://aarohkandy.github.io/v0.0.1-auros/code-signing.html` opens first.
   Then tell an agent "applied", so the policy page can say so.
3. **When they say yes**, in SignPath: create the project **AurOS** (slug
   `auros`), add the **GitHub.com** trusted build system and link it, paste
   the artifact configuration from `docs/SIGNING.md`, create the signing
   policy `release-signing` with **manual approval required** and you as the
   approver, and make an API token for a CI user with the *submitter* role.
4. **In GitHub**: Settings → Secrets and variables → Actions →
   *Secrets*: `SIGNPATH_API_TOKEN` (the token); *Variables*:
   `SIGNPATH_ORGANIZATION_ID` (SignPath → your organization's ID). Adding
   the variable is the switch; add the secret first.
5. Tell the lead. The next `[release]` waits for your approval in SignPath
   (they email you), up to 4 hours, then signs.

### 5b. Not required, by the owner's decision

- **No Azure and no Microsoft account**, and no paid code-signing certificate
  (D7 stands for these; D12 added free signing through SignPath Foundation,
  above).

## 6. When you move to a new repository

See `docs/MOVING.md`: one `git clone` brings every file; then redo sections
1, 2, 4 and 7 on the new repository, and push once to rebuild the release.

## 7. Wishes: the AI service (Anthropic + Cloudflare)

What it is: `docs/CLOUD.md`. It runs on Cloudflare (no server to look after)
and uses Anthropic's Claude. You pay Anthropic per wish (about a hundredth of
a cent) and nothing to Cloudflare until roughly 5,000 people a day use it
(then $5 a month). The workflow `.github/workflows/cloud.yml` deploys it
whenever the secrets below exist; without them it only runs the tests.

**Already done (2026-10-23):** `CLOUDFLARE_ACCOUNT_ID`, `CLOUDFLARE_API_TOKEN`
and the Anthropic key as `ANTHROPIC_API` are set, the service is live at
https://auros-wishes.aarohkandy.workers.dev. The workflow accepts the key
under either name, `ANTHROPIC_API` or `ANTHROPIC_API_KEY`; keep the one you have.

### 7a. Anthropic: its own workspace, with a spending limit (5 minutes, needed now)

The key you pasted belongs to the whole organization, and Anthropic refuses
it for this use ("not scoped to a workspace"). A workspace of its own is
what the contract asks for anyway: Wishes gets its own monthly limit, and
nothing else you use Anthropic for can be affected.

1. https://platform.claude.com → **Settings → Workspaces → Create workspace**.
   Name: `AurOS Wishes`.
2. Open it → **Limits** → set a **monthly spend limit** (start with **$50**;
   the service's own daily cap is $5, so this is the backstop).
3. In that workspace → **API keys → Create key**, name `cloud.yml`. Copy it.
4. https://github.com/aarohkandy/v0.0.1-auros/settings/secrets/actions →
   `ANTHROPIC_API` → **Update** → paste → **Update secret**.
5. Tell an agent "the Wishes key is in" (or: Actions → cloud → **Run
   workflow**). The run's summary shows the live checks and the cost per wish.
6. Delete the old organization key in the Console if nothing else uses it.

(Alternative to steps 3–4, if you want to keep the current key: copy the
workspace's id, `wrkspc_…`, and add it as a repository **variable** — Settings
→ Secrets and variables → Actions → **Variables** → `ANTHROPIC_WORKSPACE_ID`.)

### 7b. Anthropic: privacy paperwork (before Wishes is switched on for the public)

The contract (`docs/AI.md` §7.12) needs two things only you can ask for:

1. Anthropic's **Data Processing Addendum** (DPA) for your organization.
   Look for it in the Console's legal or privacy settings; if it is not there
   as a click-through, ask Anthropic through https://support.claude.com.
2. **Zero data retention** for the organization (same place: ask support;
   say it is for an operating system's assistant used by elderly people).
   Their answer decides the retention sentence in the consent text
   (`docs/CLOUD.md` §4).

Tell an agent the outcome of each; the consent text is updated from it.

### 7c. Cloudflare token, least privilege (only if you make a new one)

https://dash.cloudflare.com/profile/api-tokens → **Create token → Custom
token**. Permissions, all **Account** level, for your account only:
**Workers Scripts: Edit**, **Workers Tail: Read**, **Account Settings: Read**.
No zone permissions. Paste it into the `CLOUDFLARE_API_TOKEN` secret. The
account id is on the right of the Workers & Pages overview page
(`CLOUDFLARE_ACCOUNT_ID`).

### 7d. Optional: a phone alert at half the day's budget (3 minutes)

Install the free **ntfy** app on your phone and subscribe to a topic with a
long random name (for example `auros-wishes-` and 16 random letters). Add the
secret `WISHES_ALERT_URL` = `https://ntfy.sh/<that topic>`. Once a day, when
half the daily budget is used, your phone says so. The message holds a dollar
amount and nothing about anyone.

What the workflow does with these: it sets the Anthropic key on the Worker
through stdin (never printed), makes the token-signing secret once by itself
(nobody, including you, ever needs to see it), deploys, and checks the live
service. Rotation and outages: `docs/CLOUD.md` §5.

## 8. Your own address, and being told when something breaks (stream OPS)

The steps, in order, with the exact commands, are in `docs/ops/DOMAIN.md` ("What the owner does"): buy the
domain at Cloudflare, let the Cloudflare token touch it, set the secret `AUROS_OPS_TOKEN` and the variable
`AUROS_DOMAIN`, run the `edge` workflow, rebuild. **Nothing is installed from a build made before that.**

Until then the monitor already runs every 15 minutes against today's addresses and opens a GitHub issue
assigned to you if the Wishes service stops answering (`docs/ops/MONITOR.md`). Install the GitHub app on your
phone and turn on notifications for this repository so that issue reaches you; no other pager is set up until
you choose one. The playbooks for what to do are in `docs/ops/INCIDENTS.md`. What it will cost as it grows:
`docs/ops/COSTS.md`.
