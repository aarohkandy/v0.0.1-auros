# Signing AurOS-Setup.exe

> **2026-10-23, the owner (D12): sign it, through SignPath Foundation.**
> SignPath Foundation gives free code-signing certificates to open-source
> projects; the key stays in their hardware, and the signature is made from
> our GitHub Actions run. It is not a Microsoft service (D7's "no Microsoft
> services" still holds). **Until the certificate exists the installer ships
> unsigned, exactly as before, and the clicks in the first table are what
> she meets.** Everything below the first table is ready and switched off:
> the day the owner adds one repository variable and one secret (section 4),
> the next `[release]` is signed.

Files: `.github/workflows/release.yml` (jobs `signgate`, `sign`),
`tools/signed-check.sh` and `tools/authenticode.py` (the checks on what comes
back), `tools/signtest.sh` (proves each check fails when it should),
`website/code-signing.html` (the policy page SignPath requires),
`docs/legal/signpath-application.md` (what the owner pastes to apply).

---

## 1. What she sees, and what to click

### Unsigned (today)

An unsigned `.exe` from the internet meets up to four questions. None of
them is an error.

| Where | What appears | What to click |
|---|---|---|
| **Edge, while downloading** | "AurOS-Setup.exe isn't commonly downloaded. Make sure you trust AurOS-Setup.exe before you open it." | **…** then **Keep**; in the box that follows, **Show more** then **Keep anyway** |
| **Chrome, while downloading** | "Suspicious download blocked" / "This file isn't commonly downloaded" | **Download suspicious file** |
| **Opening it** (any browser) | The blue "Windows protected your PC" panel. Only *Don't run* is visible | **More info**, then **Run anyway** |
| **Permission** | "Do you want to allow this app from an unknown publisher to make changes to your device?" | **Yes** |

**Smart App Control.** Some Windows 11 PCs (clean installs since 22H2)
have Smart App Control on. It blocks an unsigned program outright, with
no *Run anyway*: "Smart App Control blocked an app that may be unsafe".
On those PCs it has to be switched off first: **Start → Settings →
Privacy & security → Windows Security → App & browser control → Smart
App Control settings → Off**. Since the April 2026 Windows update it
can be switched back on later from the same page (on some PCs the
October 2026 update broke switching it back on;
`docs/research/every-machine-2026-10.md` §5). Windows 10 has no Smart
App Control.

The blue panel and Edge's warning fade as more people download the same
file (SmartScreen reputation is per file for an unsigned program), so a
new `AurOS-Setup.exe` starts again from nothing.

### Signed (after the certificate)

What changes, said without promising more than is known:

- The administrator question names a publisher ("SignPath Foundation")
  instead of "unknown publisher".
- SmartScreen and Edge judge a signed file by its certificate's history as
  well as the file's, so a new release should start with less suspicion than
  an unsigned one. **It may still ask** until enough people have opened it;
  the table above stays on the website until a real-PC test says otherwise.
- Smart App Control allows a program signed with a certificate from a
  trusted authority, so the "switch it off first" step should go away.
  **Expected, not yet seen on a real PC** (the owner's real-PC campaign,
  `docs/PLAN-END-TO-END.md`, is where it gets tested). Until then the
  advanced page keeps the instructions.
- When it is proven, the website's "no signature" sentences
  (`website/advanced.html`, the front door's three-click strip) are rewritten
  by stream D, and `tools/site/check.mjs` is taught the new claim.

---

## 2. What SignPath Foundation requires, and where AurOS stands

From `signpath.org/terms` (read 2026-10-23; re-read it when applying, the
rules change). **"Ready"** means it is true in this repository today.

| They require | AurOS | State |
|---|---|---|
| An OSI-approved licence, no commercial dual-licensing, no proprietary code | MIT (`LICENSE`). Fonts are OFL; the Ubuntu packages in the image are their own licences and are not part of the signed file. No dual licence. | ready |
| Actively maintained, already released in the form to be signed | prereleases exist from `[release]` (first: `v0.1.0-618726b`) | ready |
| Functionality described on the download page | the front door says what AurOS Setup does | ready |
| Not malware, not potentially unwanted software | see the application text: what Setup changes is listed and every change is undoable | ready |
| Not a hacking tool: may not circumvent the system's security | Setup reads Windows through its documented interfaces, sets a one-shot boot entry and (once supported, D13) pauses BitLocker with Windows' own call; Secure Boot stays on (Canonical's signed shim). It disables nothing. | ready (state it plainly in the application) |
| The signing team owns the repository and development | `aarohkandy/v0.0.1-auros`, owned and maintained by the applicant | ready |
| Only binaries built from our own source, by a verifiable automated build | `build.yml` builds on GitHub-hosted runners; build scripts and workflows are in the repository | ready |
| A "Code signing policy" page linked from the home page, with the attribution line, team roles and a privacy statement | `website/code-signing.html`, linked from every page's footer | ready |
| Named roles: authors, reviewers, approvers | one person holds all three today (`@aarohkandy`), written on the policy page; add people there as they join | ready; **grow it** |
| Multi-factor authentication for every team member, on GitHub and on SignPath | not checkable from here | **owner**: section 4, step 1 |
| File metadata restrictions: product name = project name, one product version per release | `src/aurbridge/aurbridge-info.rc`: ProductName "AurOS", ProductVersion `AUROS_VER_STR` (0.1.0, one value per build) | ready |
| Every release approved by hand before signing | the SignPath signing policy (section 4, step 5) requires approval; the `sign` job waits for it | **owner**: policy setting |
| Installer announces what it changes and says how to uninstall | the wizard's pages say what will happen; "Put Windows back" removes AurOS | ready |
| Privacy: say what is sent where, or "will not transfer any information to other networked systems" unless asked | on the policy page: only the image download the person asked for, from GitHub | ready |

## 3. How the signature gets onto the file

```
build (unsigned AurOS-Setup.exe) -> tests -> signgate -> sign -> publish -> windows + e2e + site -> promote
                                              |            \- waits for the owner's approval in SignPath
                                              \- skipped, cleanly, until the variable and the secret exist
```

`release.yml` job by job:

1. **`signgate`** decides, in seconds, with no secret printed: signing is
   on only if the repository variable `SIGNPATH_ORGANIZATION_ID` and the
   secret `SIGNPATH_API_TOKEN` both exist. Otherwise `sign` is skipped and
   the unsigned path runs exactly as it always has.
2. **`sign`** (only when on):
   - refuses to run unless the build was made in this same run
     (`[release]`); a `[publish RUN-ID]` of an older build cannot be signed,
     because SignPath must be able to tie the file to the build that made
     it. (With signing on, such a publish fails with a message saying to
     use `[release]`, rather than quietly publishing unsigned.)
   - uploads `AurOS-Setup.exe` alone as a workflow artifact (SignPath only
     accepts an artifact stored by the run it is submitted from);
   - submits it with `signpath/github-action-submit-signing-request`, pinned
     to a commit, and **waits** (up to 4 hours) for the approver;
   - downloads the signed file and runs `tools/signed-check.sh` on it.
3. **`tools/signed-check.sh UNSIGNED SIGNED`** fails the run unless all of:
   - `osslsigncode verify` accepts the signature against the system's
     trusted roots (SignPath's certificate chains to a public authority);
   - the signer's name is `SignPath Foundation` (`AUROS_SIGN_SUBJECT`
     overrides, for tests);
   - the Authenticode digest of the signed file equals the unsigned file's
     (`tools/authenticode.py`): the signed file is the built file plus a
     signature, byte for byte nothing else (the checksum field and the
     certificate table are the only parts a signature may change). The
     signature alone would be just as valid on a different file. (osslsigncode
     prints a "message digest" of its own that is not this number; it is
     deliberately not compared, because this check is stronger: it covers
     every byte Authenticode defines, and osslsigncode's verify already
     proved the signature covers the file as signed.)
4. **`publish`** uses the signed file when `sign` ran, records both hashes in
   the release notes (the unsigned one the build made, the signed one people
   download), puts the signed one in `SHA256SUMS`, and everything after it
   (`windows`, `e2e`, `promote`) works on the signed file: the Windows job
   requires `Get-AuthenticodeSignature` to say `Valid`, and `e2e` is handed
   the signed file's hash, so the hours-long journey is run on the file
   people get.
5. If signing is on and `sign` fails or is not approved in time, **nothing
   is published**. There is no fallback to unsigned once the owner has
   switched signing on.

`tools/signtest.sh` (19 checks, in CI on every build) proves each check in
(3) refuses its own failure, with a throwaway CA standing in for SignPath's:
a signed file changed afterwards, a **validly signed different file**, an
unsigned file sent back as signed, an already-signed build, a wrong signer
name, a signer no public authority vouches for; and that a file whose length
is not a multiple of 8 (signing pads it) is accepted. Each check was
removed in turn on CI and the matching test went red (digest comparison,
signer name, signature verification, the "already signed" input); the
"returned file is signed" line is also caught by the signature check, so it
is a clearer message rather than a separate gate.

## 4. What only the owner can do (in this order)

1. **Two-factor sign-in on GitHub** for `aarohkandy` (GitHub → Settings →
   Password and authentication → Two-factor authentication), and on
   SignPath when the account exists. SignPath requires it of everyone in the
   team.
2. **Apply**: `https://signpath.org/apply`. The text to paste is in
   `docs/legal/signpath-application.md`; it has the facts, the answers to
   the questions their review asks, and what to fill in. Use the real
   contact address there.
3. **When they approve**: sign in to SignPath, create the project **AurOS**
   (slug `auros`), and:
   - add the predefined trusted build system **GitHub.com** and link it to
     the project;
   - create the artifact configuration (the default one) as below;
   - create the signing policy `release-signing` for the certificate they
     issue, with **manual approval required** and the owner as approver;
   - create a user for CI with the *submitter* role on that policy and make
     an API token for it.
4. **In GitHub** (`https://github.com/aarohkandy/v0.0.1-auros/settings/secrets/actions`
   and `.../settings/variables/actions`): the **secret** `SIGNPATH_API_TOKEN`
   (the token) and the **variable** `SIGNPATH_ORGANIZATION_ID` (SignPath →
   Organization → the ID). Optional variables if the slugs differ:
   `SIGNPATH_PROJECT_SLUG` (default `auros`), `SIGNPATH_SIGNING_POLICY_SLUG`
   (default `release-signing`).
5. Tell the lead. The next `[release]` signs. The owner approves the request
   in SignPath (they send an email); the run waits for it.

The artifact configuration (SignPath → Project → Artifact configurations →
default → paste; `upload-artifact` makes a ZIP, so the root is a zip file):

```xml
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file path="AurOS-Setup.exe" product-name="AurOS">
      <authenticode-sign/>
    </pe-file>
  </zip-file>
</artifact-configuration>
```

`product-name="AurOS"` is the file-metadata restriction SignPath requires;
set `product-version` as well once a version is chosen for the first signed
release (it must be the same value in every build of that release).

## 5. What the build does regardless

- `build/aurbridge` signs only if signing material is configured by the
  environment (the paths `build/sign` has always had: a PKCS#11 token, a
  PKCS#12 file, or a PEM pair). In CI none is, so the installer from
  `build.yml` is **always unsigned**; the signature is added afterwards, by
  SignPath, in `release.yml`. `AUROS_RELEASE=1` does not refuse an unsigned
  installer.
- `tools/signtest.sh` keeps both paths honest: the optional local signing
  (a throwaway CA; SHA-256, replace-not-stack, one changed byte breaks it)
  and the checks on a signed file coming back.
- The release workflow publishes an unsigned file with one neutral line in
  the notes and the clicks above, or a signed file with its policy link.
  Its Windows job prints what `Get-AuthenticodeSignature` says.

## 6. What would help without a certificate

- **One installer for longer.** Every rebuild of `AurOS-Setup.exe` is a
  new hash with no reputation. Today each build bakes the address and
  hash of its own image in, so a new image means a new installer. An
  installer that could take a newer image without being rebuilt would
  keep its reputation; that is a design change for the installer's
  owners, not a build setting.
- **The website shows the clicks**, with pictures, before she meets
  them.

---

## 7. The other signature: Secure Boot

This is a separate question with a much happier answer, and it is worth
saying why it is already solved.

AurOS boots through **Canonical's `shimx64.efi.dualsigned`** and their
signed `grubx64.efi`, copied out of the image by `build/mkimage` — not
rebuilt. Those binaries carry Microsoft's signature already, so AurOS
boots on a machine with Secure Boot enabled without anybody submitting
anything to anybody. `tools/loadertest.sh` proves it against
`OVMF_CODE_4M.ms.fd` with Microsoft's own keys enrolled and Secure Boot
enforcing.

**What "dualsigned" means, because this file used to get it wrong.**
Ubuntu's `shimx64.efi.dualsigned` carries two signatures: Canonical's
own and **Microsoft's, through the Microsoft Corporation UEFI CA 2011**
(`sbverify --list` shows exactly those two). It is *not* signed with
Microsoft's 2023 third-party key. The 2011 CA is the one old firmware
trusts, which is the whole reason the chain is borrowed rather than
built: a distribution applying to Microsoft on its own today gets a
2023-signed shim, which would not boot on exactly the old PCs this
product exists for.

The other side of the same fact: **a PC that lists Microsoft's 2023
third-party key and not the 2011 one refuses this shim**, and so does a
Secured-core PC that ships with third-party keys switched off. Neither
is left to find out at the restart. The installer reads the firmware's
`db` (and `dbx`) from Windows before it changes anything and asks
whether it trusts the key the carried shim is signed with -- a name
`build/aurbridge` reads off the shim itself, so a newer shim is checked
for its own key. See `docs/AURBRIDGE.md`, "Secure Boot stays on".

If AurOS ever needs its own shim — to carry its own vendor certificate,
so that kernel modules could be signed by us — that is a submission to
the Microsoft UEFI signing service through a shim review, it takes
months, and it does not remove the 2011 problem. There is no reason to
start it, and this paragraph exists so that nobody starts it by
accident.

---
