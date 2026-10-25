# Operating personalized installer delivery

Personalized delivery accepts one bounded `desktop` recipe, builds it through
the repository's reviewed `customize.yml` workflow, requires both SignPath
signatures, and returns a direct `.exe` only after the cloud service verifies
the workflow and its immutable delivery evidence. It makes no model call.

The public API is **disabled by default**. `BUILD_REQUESTS_ENABLED = "0"` in
`cloud/wrangler.toml` is the authoritative switch. Keep it off until every gate
in [Activation](#activation) has passed.

## Trust boundary

The browser can choose only the recipe-v1 `desktop` base and allowed `look`
and `theme` values. It cannot choose a Git ref, workflow, repository, command,
package, URL, or signing setting. The Worker recompiles the raw recipe and
selects an existing successful `main` base build whose artifacts and exact
prerelease tag still exist. It dispatches `.github/workflows/customize.yml` at
that server-verified tag.

A result becomes `download-ready` only when all of these still agree:

- the uniquely correlated workflow run and its source commit;
- successful `package`, `sign / sign`, `certify / e2e (base)`,
  `certify / custom-certification`, and `publish` jobs;
- the same-run signed-installer artifact;
- the release `.exe` and bounded JSON manifest;
- the same manifest preserved as the immutable, same-run
  `auros-custom-delivery` Actions artifact;
- the release asset's canonical identity, byte count, and GitHub SHA-256
  digest against the immutable manifest.

The first delivery for an exact base and recipe runs a real Windows installation
certification and can take hours. Repeated matching requests reuse its verified
installer. Do not advertise instant first-time delivery; a public rollout needs
pre-certified common combinations or a measured shorter certification path.

Readiness is cached for at most 15 seconds and then revalidated. A missing or
changed asset revokes readiness on the next check. This is a check-time
guarantee: a release administrator could replace an asset after the final
status check but before a person's click. Keep release write access tightly
limited and use GitHub's audit log when investigating an incident.

## GitHub App

Create a dedicated GitHub App and install it only on
`aarohkandy/v0.0.1-auros`. Webhooks are not used. Grant only these repository
permissions:

| Permission | Access | Why |
|---|---|---|
| Actions | Read and write | Read runs, jobs, and artifacts; dispatch the fixed customization workflow |
| Contents | Read | Read commits, tags, prereleases, and release-asset metadata |
| Metadata | Read | GitHub's required repository metadata access |

The App does not publish release assets. The same-run `publish` job uses its
bounded GitHub Actions token with `contents: write` after signing and all
source checks pass.

Record the App ID and repository installation ID as deployment variables
`GITHUB_APP_ID` and `GITHUB_APP_INSTALLATION_ID`. Store the App private key as
the Worker secret `GITHUB_APP_PRIVATE_KEY`. The service accepts GitHub's
standard PKCS#1 PEM (`BEGIN RSA PRIVATE KEY`) or PKCS#8 PEM
(`BEGIN PRIVATE KEY`), with real newlines or `\n` escapes. Keep the PEM in a
secret manager; never commit it, paste it into a workflow variable, include it
in a support message, or pass it as a command-line argument.

The Worker also requires two unrelated, randomly generated secrets of at
least 32 characters:

- `BUILD_NETWORK_SECRET`, for pseudonymous network quota keys;
- `BUILD_CHALLENGE_SECRET`, for one-use proof-of-work challenges.

Generate and store each independently. Never reuse `TOKEN_SECRET`, an
Anthropic key, a GitHub token, or either SignPath credential.

`.github/workflows/cloud.yml` reads these three values from like-named GitHub
Actions secrets and provisions them as Worker secrets. It reads the two IDs
from like-named GitHub Actions variables and passes them as Worker deployment
variables. Secret values reach `wrangler secret put` only on stdin with shell
tracing disabled; only their names may appear in logs.

This wiring is deliberately two-stage. With `BUILD_REQUESTS_ENABLED = "0"`, a
deployment stages any supplied custom-delivery settings while the API remains
closed. Before a deployment with the switch set to `"1"`, the workflow
requires all five GitHub settings and verifies that all three Worker secret
names already exist. It also validates the secret lengths, positive decimal
IDs, RSA private key, exact HTTPS origin, and fixed repository. A missing or invalid
value, an unreadable secret list, or an unstaged Worker secret fails the
workflow before the enabled version is deployed. On the later enabled run, the
secrets are refreshed before `wrangler deploy`. During first activation,
following the activation order keeps the prior Worker disabled during that
refresh. Subsequent enabled deployments may refresh an already enabled Worker;
reapplying unchanged values is harmless. An intentional
`BUILD_CHALLENGE_SECRET` rotation invalidates unexpired five-minute challenges,
and changing `BUILD_NETWORK_SECRET` changes the pseudonymous network quota keys.
Schedule either rotation accordingly. This means the first configured
deployment must remain disabled.

The secret-name check proves only that the three names were staged on the
Worker. The disabled live-route checks in Activation step 8 are the separate
proof that the reviewed disabled version and migration were actually deployed.
Removing a GitHub secret while custom delivery is disabled does not delete the
corresponding Worker secret; rotate it with a new value or remove it through
the Cloudflare incident process.

## SignPath and publication

Custom delivery has no unsigned fallback. `SIGNPATH_ORGANIZATION_ID` must be
configured, and the shared signer requires `SIGNPATH_API_TOKEN` plus
`SIGNPATH_INNER_ARTIFACT_CONFIGURATION_SLUG`. The project, policy, inner and
outer configuration requirements are in [SIGNING.md](SIGNING.md). Both manual
approvals must complete successfully. If signing is absent, skipped, refused,
or times out, `publish` does not run and the API never returns a download.

Before activation, prove one custom run through the live SignPath project.
Confirm that its run name is `customize <request UUID>`, its required jobs are
exactly `package`, `sign / sign`, `certify / e2e (base)`,
`certify / custom-certification`, and `publish`, and it retains:

- `auros-setup-signed`, 30 days;
- `auros-custom-delivery`, 30 days, containing exactly the final manifest;
- `auros-custom-unsigned`, 7 days, for diagnosis only.

The base `build.yml` run must still have one unexpired `auros-installer` and
one unexpired `auros-base-metadata` artifact. Both are retained for 30 days.
Keep at least one fully green, released base younger than 30 days. Rebuild and
release a base before expiry; do not delete its tag, prerelease, or required
artifacts while requests may use it.

Base discovery reads at most 50 recent releases, considers at most five that
have the complete canonical AurOS release asset set, and asks for at most three
successful `build.yml` runs at each candidate's exact tag commit. Each run is
accepted only when it is a completed successful `main` push at that exact SHA
and has one unexpired `auros-installer` and `auros-base-metadata` artifact.
This is at most 26 GitHub API requests per lookup. Ambiguous runs, expired or
duplicate artifacts, runs with more than 100 artifacts, tag/SHA mismatches, and
candidates outside those explicit bounds fail closed.

### Pre-certify common recipes

An operator can run `.github/workflows/customize.yml` before a person asks for
a common combination. This saves that person's build, signing-approval, and
Windows-certification wait. It does not create or bypass a public receipt.
Finish pre-certification before advertising the combination or accepting public
requests for it; do not race an operator dispatch against a public submission.

1. Use the exact base run, source, and prerelease tag that the public service
   will select. A newer eligible base changes the delivery identity.
2. Generate a new lowercase UUID for `request_id` and retain it with the
   operator record. Never leave `request_id` empty. An empty/null identity
   cannot be correlated or adopted safely.
3. Dispatch `customize.yml` at that exact verified base tag with the strict
   canonical desktop recipe, the matching base build run ID, and that UUID.
4. Approve both SignPath requests. Wait for both certification jobs and
   `publish` to succeed. Confirm the immutable `auros-custom-delivery` artifact
   and the recipe-named release `.exe` and `.json` exist.
5. Within the immutable artifact's 30-day retention window, submit the same
   recipe once through the ordinary public flow. Its proof and network/global
   quota still apply. The service treats the release manifest only as a locator,
   verifies the original run, every gate, signed and immutable artifacts, tag,
   source, and current asset digest, then atomically records the canonical
   delivery and returns a normal 24-hour receipt. It must not dispatch another
   customization run.

Discovery for one exact base and recipe is serialized. A concurrent public
submission receives `existing_delivery_busy` and may retry through the normal
proof-and-quota flow; it cannot make a second discovery or dispatch while the
first submission is adopting or reserving that delivery.

This same path can recover a lost canonical Durable Object record while the
immutable artifact remains available. After that artifact expires, mutable
release assets alone cannot prove the original delivery. The occupied recipe
filenames then fail closed and must not be overwritten or rebuilt under a new
request identity. Partial assets, an invalid UUID, a foreign or failed run, or
any evidence mismatch also fail closed and consume the already-admitted quota.

## Configuration, quotas, and receipts

Set `BUILD_ALLOWED_ORIGIN` to the one exact HTTPS website origin, with no path.
It controls browser CORS and is not authentication. The one-use proof, atomic
quotas, and receipt protect the API independently.

The conservative defaults are:

| Setting | Default | Accepted range or meaning |
|---|---:|---|
| `BUILD_REQUESTS_PER_NET` | 1/day | 1–3 per IPv4 `/24` or IPv6 `/64` |
| `BUILD_REQUESTS_GLOBAL` | 5/day | 1–20 total submissions |
| `BUILD_POW_BITS` | 16 | 12–24 leading zero bits |
| Build receipt lifetime | 24 hours | Fixed in the service |
| Proof challenge lifetime | 5 minutes | Fixed; bound to recipe and network, one use |

Keep the defaults for initial activation. Raise a limit only after examining
signing capacity, GitHub Actions use, failures, and abuse. A repeated exact
recipe/source request deduplicates its build, but it still consumes submission
quota so popular recipes cannot be used to exhaust receipt ownership for free.
Proof of work is only an abuse cost, not authentication. The atomic per-network
and global quotas are the actual bound on build and signing work. Sixteen bits
keeps the automatic browser step practical on slower computers while every
accepted proof remains recipe/network-bound and one use.

The response contains a random 192-bit receipt once. The browser must retain
it for status polling and send it as a bearer token. Only its SHA-256 hash is
stored. Anyone holding the receipt can see that job's status and eventual
download metadata; a lost receipt cannot be recovered.

Receipt authorization records expire after 24 hours. Separately, the Durable
Object retains one canonical record for each admitted exact base and recipe so
a later receipt reuses the original request, certification, and signatures.
That record has no automatic expiry. It contains the bounded recipe and ID,
source/base/original request/run identifiers, and, once verified, the exact
manifest plus the verified gate, Actions artifact, release, and asset
identities. That bounded record replaces the dependency on GitHub retaining
the completed run and artifacts forever; each status check still revalidates
the live release tag, source commit, manifest bytes, asset identities, size,
and digest. It never contains the receipt or a network identifier. Creating a
canonical record consumes the same atomic
submission quota as a new build; the fixed recipe catalogue also bounds the
number created for any base. Do not delete one while its release assets remain:
without the original evidence, that occupied recipe filename must fail closed
rather than sign a replacement under new provenance.

The service stores salted network-prefix hashes for quota counting, not raw
addresses. Daily counters can remain through the end of the following UTC day;
challenge-consumption markers remain until challenge expiry. No name, email,
free-text request, raw IP address, or Wishes content is part of a build record.
Cloudflare and GitHub retain their own service metadata under their policies.

## Activation

Do these in order. Do not activate a partly configured service.

1. Leave `BUILD_REQUESTS_ENABLED = "0"`.
2. Install the least-privilege GitHub App above on the one repository. Add its
   App ID as the GitHub Actions variable `GITHUB_APP_ID`, its repository
   installation ID as `GITHUB_APP_INSTALLATION_ID`, and its private key as the
   GitHub Actions secret `GITHUB_APP_PRIVATE_KEY`. Confirm only the setting
   names, without printing their values.
3. Add independent GitHub Actions secrets `BUILD_NETWORK_SECRET` and
   `BUILD_CHALLENGE_SECRET`, each with at least 32 random characters.
4. Set the exact `BUILD_ALLOWED_ORIGIN`; confirm the repository remains fixed
   as `BUILD_GITHUB_REPOSITORY` and the browser cannot supply either value.
   Confirm the existing repository variable `AUROS_WISHES_URL` is the Worker's
   exact HTTPS origin, or that its documented `workers.dev` default is correct.
5. Complete [SIGNING.md](SIGNING.md), including two-factor authentication and
   manual approval, then prove a signed custom run and its publish evidence.
6. Confirm a recent successful `main` base run, its exact-source prerelease,
   and both unexpired 30-day base artifacts.
7. From `cloud/`, run:

   ```sh
   npm ci --no-audit --no-fund
   npm test
   npm audit --omit=dev
   ```

   The tests must be green and the production audit must report zero known
   vulnerabilities. Review any full `npm audit` findings separately as
   described below.
8. Deploy the reviewed Worker and Durable Object migration while still
   disabled. This stages the three Worker secrets and two deployment
   variables. Confirm ordinary health checks pass and both build POST routes
   answer `503` with `builds_disabled`.
9. Change only `BUILD_REQUESTS_ENABLED` to `"1"`, review that diff, and deploy.
   The cloud workflow must pass the independent read-only build preflight. It
   sends only CORS `OPTIONS`; it creates no challenge, quota row, job, workflow
   dispatch, signing request, or model call.
10. After the enabled cloud deployment succeeds, run `pages.yml`. Pages derives
    the public site's exact origin, requires it to equal `BUILD_ALLOWED_ORIGIN`,
    repeats the read-only live preflight, and writes the enabled
    `build-service.json` only into the staged Pages artifact. The checked-in
    file remains disabled. If the Pages run raced the cloud deployment and
    failed its probe, rerun Pages; do not bypass the probe.
11. Open the deployed `request.html` and submit one bounded desktop recipe.
    Preserve its
    receipt, approve both SignPath requests, and verify the status progresses
    to `download-ready` only after both certification jobs and `publish`.
    Confirm the returned recipe ID,
    source/base IDs, exact `.exe` name, byte count, and SHA-256 against the
    immutable manifest, then verify Authenticode on a downloaded copy.

Any mismatch means activation failed. Disable the API before investigating.

## Disable and incident response

Set `BUILD_REQUESTS_ENABLED = "0"`, review the one-line change, and deploy it.
Let the same push's Pages workflow publish the disabled staged configuration;
if Pages did not run, dispatch it after the cloud deployment.
This blocks new challenges, submissions, and status reads. Workflows already
dispatched to GitHub continue independently, so inspect or cancel them in
Actions when necessary. Disabling the API does not delete an installer already
published to a prerelease.

For a suspected credential leak, also revoke or rotate the affected GitHub App
key, uninstall the App if needed, and revoke the SignPath submitter token.
Remove a suspect release asset through the repository's incident process, then
check that the API no longer reports it ready after the 15-second cache lease.

## Dependency audit note (2026-10-24)

A full `npm audit` reports three moderate entries on one development-only
chain: `autocannon@8.0.0` → `hyperid@3.3.0` → `uuid@8.3.2`. The advisory is a
missing buffer bounds check in UUID v3/v5/v6 when a caller supplies a buffer.
`autocannon` is used only by the local/CI load-test command and is not imported
into the deployed Worker. `npm audit --omit=dev` reports zero production
vulnerabilities. This makes the current public-request exposure low, while the
unsupported dependency and CI toolchain still need maintenance.

Do not apply the audit tool's suggested forced downgrade to
`autocannon@2.0.1`. Prefer replacing the load generator with a small maintained
tool or built-in Node harness, or upgrading once `autocannon` removes the
vulnerable chain. A package override to `uuid>=11.1.1` is acceptable only after
the full load test proves `hyperid` compatibility. No dependency was changed
as part of this runbook.
