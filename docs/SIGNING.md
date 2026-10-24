# Signing AurOS-Setup.exe

> **2026-10-23, the owner (D12): sign it, through SignPath Foundation.**
> SignPath Foundation gives free code-signing certificates to open-source
> projects; the key stays in their hardware, and the signature is made from
> our GitHub Actions run. It is not a Microsoft service (D7's "no Microsoft
> services" still holds). **Until the certificate exists the installer ships
> unsigned, exactly as before, and the clicks in the first table are what
> she meets.** Everything below the first table is ready and switched off:
> the day the owner adds the repository variables and secret in section 4,
> the next `[release]` signs both layers.

Files: `.github/workflows/release.yml` (job `sign`),
`.github/workflows/sign-installer.yml` (shared two-stage signer),
`build/aurbridge-stub` (rebuilds the outer around the signed inner),
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

## 3. How both signatures get onto the download

```
build unsigned wizard + outer
  -> sign wizard (manual approval)
  -> verify it is the built wizard plus Authenticode
  -> rebuild the kernel32-only outer around those exact signed bytes
  -> sign outer (manual approval)
  -> verify both signatures and the embedded bytes
  -> publish -> windows + e2e + site -> promote
```

`release.yml` calls the shared `sign-installer.yml` workflow:

1. Signing is on when `SIGNPATH_ORGANIZATION_ID` exists. The job then requires
   `SIGNPATH_API_TOKEN` and `SIGNPATH_INNER_ARTIFACT_CONFIGURATION_SLUG`; a
   missing inner configuration is a hard failure, never an outer-only signed
   release. With the organization variable absent, the unsigned path remains
   available for testing as a prerelease. Public `[latest]` promotion requires
   this exact publish's `signed=true`; missing or false signature state fails
   promotion explicitly and leaves the prerelease intact.
2. The shared signer accepts only an installer built in the same workflow run.
   Ordinary releases enter through a `[release]` build; personalized packaging
   uses the same signer and verification chain.
   It checks that the full commit in `build-info.txt`, `GITHUB_SHA`, and the
   checked-out source are identical, and that the build's outer resource 300
   is byte-identical to its accompanying unsigned wizard.
3. The first SignPath request uploads only `aurbridge-wizard.exe`, uses the
   inner artifact configuration, and waits for manual approval. The returned
   file passes `tools/signed-check.sh`: public trust, the SignPath Foundation
   subject, and an Authenticode digest proving only signing fields changed.
4. `build/aurbridge-stub` then builds only the freestanding kernel32 bootstrap
   from that signed wizard. It recomputes the baked child SHA-256, embeds the
   signed bytes as resource 300, checks the manifest, imports and extracted
   resource, and is the same helper used by the ordinary `build/aurbridge`
   path.
5. The rebuilt unsigned outer is a second same-run artifact and a second
   SignPath request, again requiring approval. Its result must be the rebuilt
   outer plus Authenticode only. Resource 300 is extracted again, compared
   byte for byte with the signed wizard, and its signature is verified.
6. `AurOS-Setup-signing-provenance.txt` records the exact source commit, build
   run, hashes of the original and signed wizard, the build's original outer,
   the rebuilt unsigned outer and signed outer, plus the verified
   relationships. Personalized packages also retain their original unsigned
   package manifest and build-info file; their hashes are bound into the
   signing provenance after validating the manifest against the exact unsigned
   installer. The manifest remains an unsigned-build record, not a claim that
   image downloads are ready. It is published beside the installer and covered by
   `SHA256SUMS`. Windows and e2e then test the final signed outer.

If either approval, configuration, signature, source binding, rebuild, byte
comparison or upload fails, nothing is published. There is no downgrade after
signing has been switched on.

`tools/signed-check.sh UNSIGNED SIGNED` verifies each service response.
`tools/signtest.sh` uses a throwaway CA to test the same two-stage order without
a paid service: sign the wizard, rebuild, sign the outer, extract resource 300,
and compare every transition. It also keeps the existing negative checks for
tampering, a different validly signed file, no signature, a wrong signer and
an untrusted signer.

## 4. What only the owner can do (in this order)

1. Enable two-factor sign-in on GitHub and SignPath for every team member.
2. Apply at `https://signpath.org/apply` using
   `docs/legal/signpath-application.md`.
3. In the AurOS SignPath project, link the predefined GitHub.com trusted build
   system and create two artifact configurations. Both use the same issued
   certificate and a signing policy that requires manual approval:
   - `wizard`: signs only `aurbridge-wizard.exe`;
   - `default` (or another chosen outer slug): signs only `AurOS-Setup.exe`.
4. Create a CI submitter token. In GitHub Actions secrets set
   `SIGNPATH_API_TOKEN`; in variables set `SIGNPATH_ORGANIZATION_ID` and the
   required `SIGNPATH_INNER_ARTIFACT_CONFIGURATION_SLUG` (for example
   `wizard`). Optional variables are `SIGNPATH_PROJECT_SLUG` (default
   `auros`), `SIGNPATH_SIGNING_POLICY_SLUG` (shared default
   `release-signing`), stage-specific policy slugs, and
   `SIGNPATH_OUTER_ARTIFACT_CONFIGURATION_SLUG` (default `default`).
5. Tell the lead. The next `[release]` produces two requests; approve the
   wizard request and then the rebuilt outer request.

Inner artifact configuration:

```xml
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file path="aurbridge-wizard.exe" product-name="AurOS">
      <authenticode-sign/>
    </pe-file>
  </zip-file>
</artifact-configuration>
```

Outer artifact configuration:

```xml
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file path="AurOS-Setup.exe" product-name="AurOS">
      <authenticode-sign/>
    </pe-file>
  </zip-file>
</artifact-configuration>
```

Set `product-version` too when the first signed release version is fixed; both
configurations must restrict it to the same release version.

## 5. What the build does regardless

- `build/aurbridge` signs both layers only if signing material is configured by the
  environment (the paths `build/sign` has always had: a PKCS#11 token, a
  PKCS#12 file, or a PEM pair). In CI none is, so the installer from
  `build.yml` is **always unsigned**; the two signatures are added afterwards,
  by SignPath, in `sign-installer.yml`. `AUROS_RELEASE=1` does not refuse an unsigned
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
