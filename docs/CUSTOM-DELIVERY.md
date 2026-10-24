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

The current `.github/workflows/cloud.yml` does **not** provision these three
Worker secrets or the two GitHub App variables. Enabling the flag before a
reviewed deployment-wiring change lands is incomplete and must fail closed.
When that change is made, secret values must reach `wrangler secret put` on
stdin with shell tracing disabled, following the existing `TOKEN_SECRET`
pattern. Only secret names belong in logs or documentation.

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
2. Install the least-privilege GitHub App above on the one repository. Confirm
   its IDs and private key are in the deployment secret path, without printing
   their values.
3. Add independent `BUILD_NETWORK_SECRET` and `BUILD_CHALLENGE_SECRET` values
   through the same reviewed secret path.
4. Set the exact `BUILD_ALLOWED_ORIGIN`; confirm the repository remains fixed
   as `BUILD_GITHUB_REPOSITORY` and the browser cannot supply either value.
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
   disabled. Confirm ordinary health checks pass and both build POST routes
   answer `503` with `builds_disabled`.
9. Change only `BUILD_REQUESTS_ENABLED` to `"1"`, review that diff, and deploy.
10. Submit one bounded desktop recipe from the configured origin. Preserve its
    receipt, approve both SignPath requests, and verify the status progresses
    to `download-ready` only after both certification jobs and `publish`.
    Confirm the returned recipe ID,
    source/base IDs, exact `.exe` name, byte count, and SHA-256 against the
    immutable manifest, then verify Authenticode on a downloaded copy.

Any mismatch means activation failed. Disable the API before investigating.

## Disable and incident response

Set `BUILD_REQUESTS_ENABLED = "0"`, review the one-line change, and deploy it.
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
