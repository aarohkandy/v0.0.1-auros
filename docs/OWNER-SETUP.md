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

## 5. Not required, by the owner's decision

- **No code-signing certificate, no Azure, no Microsoft account.** The installer
  is unsigned by design; the website shows people the two extra clicks.

## 6. When you move to a new repository

See `docs/MOVING.md`: one `git clone` brings every file; then redo sections
1, 2 and 4 on the new repository, and push once to rebuild the release.
