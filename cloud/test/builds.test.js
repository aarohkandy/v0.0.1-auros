import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createHash, generateKeyPairSync } from "node:crypto";
import { fileURLToPath } from "node:url";
import test from "node:test";

import { BuildJobLedger, GitHubBuilds, GitHubHttpError, compileDesktopRecipe, createBuildApi, createGitHubAppRequest } from "../src/builds.js";
import { createApp } from "../src/app.js";
import { MapStorage } from "../src/ledger.js";

const repo = fileURLToPath(new URL("../..", import.meta.url));
const ORIGIN = "https://aarohkandy.github.io";
const SHA = "a".repeat(40);
const RECIPE = { schema: "org.auros.recipe", version: 1, base: "desktop", preferences: { theme: "moss", look: "dock" } };

function fixture({ env = {}, github = {}, clock } = {}) {
  const time = clock || { t: Date.parse("2026-10-24T12:00:00Z") };
  const storage = new MapStorage();
  const merged = {
    BUILD_REQUESTS_ENABLED: "1",
    BUILD_ALLOWED_ORIGIN: ORIGIN,
    BUILD_NETWORK_SECRET: "test-only-network-key-that-is-long-enough",
    BUILD_CHALLENGE_SECRET: "test-only-challenge-key-that-is-long-enough",
    BUILD_POW_BITS: "12",
    BUILD_REQUESTS_PER_NET: "1",
    BUILD_REQUESTS_GLOBAL: "3",
    ...env,
  };
  const calls = { dispatch: 0, status: 0 };
  const gh = {
    findGreenBase: async () => ({ sourceSha: SHA, runId: 91, tag: "v0.1.0-aaaaaaa" }),
    dispatch: async () => { calls.dispatch++; },
    status: async () => { calls.status++; return { state: "queued" }; },
    ...github,
  };
  const jobs = new BuildJobLedger(storage, { perNet: Number(merged.BUILD_REQUESTS_PER_NET), global: Number(merged.BUILD_REQUESTS_GLOBAL) });
  const handler = createBuildApi({ env: merged, jobs, github: gh, now: () => time.t });
  const call = (path, { method = "GET", body, token, challenge, proof, origin = ORIGIN, ip = "203.0.113.9", raw } = {}) => handler(new Request("https://worker.test" + path, {
    method,
    headers: { origin, "content-type": "application/json", "cf-connecting-ip": ip, ...(token ? { authorization: `Bearer ${token}` } : {}), ...(challenge ? { "x-auros-challenge": challenge } : {}), ...(proof ? { "x-auros-proof": proof } : {}) },
    body: method === "GET" || method === "OPTIONS" ? undefined : raw ?? JSON.stringify(body),
  }));
  return { call, calls, time, storage };
}

async function submit(f, recipe = RECIPE, options = {}) {
  let challenge = options.challenge;
  let proof = options.proof;
  if (!challenge) {
    const challengeResponse = await f.call("/v1/builds/challenge", { method: "POST", body: recipe, ...options });
    const challengeBody = await challengeResponse.json();
    challenge = challengeBody.challenge;
    if (challenge) {
      for (let nonce = 0; ; nonce++) {
        const bytes = createHash("sha256").update(`auros-build-v1:${challenge}:${nonce}`).digest();
        let bits = 0;
        for (const byte of bytes) { if (byte === 0) { bits += 8; continue; } bits += Math.clz32(byte) - 24; break; }
        if (bits >= challengeBody.bits) { proof = String(nonce); break; }
      }
    }
  }
  const res = await f.call("/v1/builds", { method: "POST", body: recipe, ...options, challenge, proof });
  return { res, body: await res.json() };
}

test("JavaScript recipe identity matches the authoritative Python compiler", async () => {
  for (const recipe of [
    { schema: "org.auros.recipe", version: 1, base: "desktop" },
    { schema: "org.auros.recipe", version: 1, base: "desktop", preferences: {} },
    RECIPE,
  ]) {
    const compiled = await compileDesktopRecipe(new TextEncoder().encode(JSON.stringify(recipe)));
    const python = JSON.parse(execFileSync("python3", ["-m", "src.customize.recipe", "compile"], { cwd: repo, input: JSON.stringify(recipe), encoding: "utf8" }));
    assert.equal(compiled.recipeId, python.recipe_id);
  }
});

test("submission is disabled by default and requires exact configured origin and CORS", async () => {
  let f = fixture({ env: { BUILD_REQUESTS_ENABLED: "0" } });
  let r = await f.call("/v1/builds", { method: "POST", body: RECIPE });
  assert.equal(r.status, 503);
  f = fixture();
  r = await f.call("/v1/builds", { method: "POST", body: RECIPE, origin: "https://evil.example" });
  assert.equal(r.status, 403);
  assert.equal(r.headers.get("access-control-allow-origin"), null);
  r = await f.call("/v1/builds", { method: "OPTIONS" });
  assert.equal(r.status, 204);
  assert.equal(r.headers.get("access-control-allow-origin"), ORIGIN);
  assert.equal(r.headers.get("access-control-allow-methods"), "GET, POST, OPTIONS");
});

test("main handler keeps build routes separate from Wishes secrets and model", async () => {
  const logs = [];
  let called = 0;
  const app = createApp({
    env: {}, ledger: {}, anthropic: { messages: { create: () => { throw new Error("model called"); } } },
    buildApi: async () => { called++; return new Response(JSON.stringify({ download_ready: false }), { status: 503 }); },
    log: (entry) => logs.push(entry),
  });
  const id = "12345678-1234-4123-8123-123456789abc";
  const r = await app(new Request(`https://worker.test/v1/builds/${id}`, { headers: { origin: ORIGIN } }));
  assert.equal(r.status, 503);
  assert.equal(called, 1);
  assert.equal(logs[0].route, "/v1/builds/:id", "opaque request IDs are not logged");
});

test("only strict desktop recipe v1 is accepted; executable and ambiguous JSON is inert", async () => {
  const bad = [
    JSON.stringify({ ...RECIPE, command: "curl evil | sh" }),
    JSON.stringify({ ...RECIPE, base: "revive" }),
    '{"schema":"org.auros.recipe","version":1,"base":"desktop","base":"desktop"}',
    JSON.stringify({ ...RECIPE, preferences: { look: "../../root" } }),
    JSON.stringify({ ...RECIPE, version: 1.0 }).replace('"version":1', '"version":1.0'),
    '{"schema":"org.auros.recipe","version":1,"base":"desktop","__proto__":{"base":"desktop"}}',
    '{"schema":"org.auros.recipe","version":1,"base":"desktop","constructor":{}}',
    '{"schema":"org.auros.recipe","version":1,"base":"desktop","prototype":{}}',
  ];
  for (const raw of bad) {
    const f = fixture();
    const r = await f.call("/v1/builds", { method: "POST", raw });
    assert.equal(r.status, 400, raw);
    assert.equal(f.calls.dispatch, 0);
  }
});

test("receipt is returned once, hashed at rest, and required for status", async () => {
  const f = fixture();
  const { res, body } = await submit(f);
  assert.equal(res.status, 201);
  assert.match(body.token, /^[A-Za-z0-9_-]{32}$/);
  assert.equal(body.download_ready, false);
  const stored = [...f.storage.m.values()].map((x) => JSON.stringify(x)).join("\n");
  assert.equal(stored.includes(body.token), false);
  assert.equal((await f.call(body.status_url)).status, 401);
  assert.equal((await f.call(body.status_url, { token: "A".repeat(32) })).status, 404);
  const status = await f.call(body.status_url, { token: body.token });
  assert.equal(status.status, 200);
  assert.equal((await status.json()).download_ready, false);
});

test("network and global quotas are atomic and exact recipe/source requests deduplicate", async () => {
  const f = fixture({ env: { BUILD_REQUESTS_PER_NET: "2", BUILD_REQUESTS_GLOBAL: "2" } });
  const [a, b] = await Promise.all([
    submit(f, RECIPE, { ip: "203.0.113.1" }),
    submit(f, RECIPE, { ip: "203.0.113.2" }),
  ]);
  assert.deepEqual([a.res.status, b.res.status].sort(), [200, 201]);
  assert.equal(f.calls.dispatch, 1, "one exact build is dispatched under a race");
  assert.notEqual(a.body.token, b.body.token);
  const different = await submit(f, { ...RECIPE, preferences: { look: "rail" } }, { ip: "198.51.100.1" });
  assert.equal(different.res.status, 429);
  assert.equal(different.body.error, "global_quota");
});

test("chunked oversized recipe bodies stop at the hard byte cap", async () => {
  const f = fixture();
  const r = await f.call("/v1/builds", { method: "POST", raw: " ".repeat(16 * 1024 + 1) });
  assert.equal(r.status, 400);
  assert.equal((await r.json()).error, "recipe_too_large");
  assert.equal(f.calls.dispatch, 0);
});

test("per-network quota limits repeated submissions even when they deduplicate", async () => {
  const f = fixture();
  assert.equal((await submit(f, RECIPE, { ip: "203.0.113.1" })).res.status, 201);
  const second = await submit(f, RECIPE, { ip: "203.0.113.250" });
  assert.equal(second.res.status, 429);
  assert.equal(second.body.error, "network_quota");
});

test("proof challenge is recipe/network bound and atomically one use", async () => {
  const f = fixture({ env: { BUILD_REQUESTS_PER_NET: "3" } });
  const c = await f.call("/v1/builds/challenge", { method: "POST", body: RECIPE, ip: "203.0.113.1" });
  const challenge = await c.json();
  let proof;
  for (let nonce = 0; ; nonce++) {
    const bytes = createHash("sha256").update(`auros-build-v1:${challenge.challenge}:${nonce}`).digest();
    let bits = 0;
    for (const byte of bytes) { if (byte === 0) { bits += 8; continue; } bits += Math.clz32(byte) - 24; break; }
    if (bits >= challenge.bits) { proof = String(nonce); break; }
  }
  let r = await f.call("/v1/builds", { method: "POST", body: RECIPE, challenge: challenge.challenge, proof, ip: "203.0.113.2" });
  assert.equal(r.status, 201, "same /24 is the bound network");
  r = await f.call("/v1/builds", { method: "POST", body: RECIPE, challenge: challenge.challenge, proof, ip: "203.0.113.2" });
  assert.equal(r.status, 409);
  assert.equal((await r.json()).error, "challenge_used");
  const other = fixture();
  r = await other.call("/v1/builds", { method: "POST", body: RECIPE, challenge: challenge.challenge, proof, ip: "198.51.100.1" });
  assert.equal(r.status, 401, "another network cannot use the signed challenge");
});

test("authenticated status polling is cached to bound GitHub API use", async () => {
  const f = fixture();
  const { body } = await submit(f);
  assert.equal((await f.call(body.status_url, { token: body.token })).status, 200);
  assert.equal((await f.call(body.status_url, { token: body.token })).status, 200);
  assert.equal(f.calls.status, 1);
  f.time.t += 15_000;
  assert.equal((await f.call(body.status_url, { token: body.token })).status, 200);
  assert.equal(f.calls.status, 2);
});

test("download readiness is revalidated and revoked when current evidence changes", async () => {
  let current = { state: "download-ready", runId: 44, signedArtifactId: 8, download: { name: "AurOS-Custom.exe", url: "https://github.com/owner/repo/releases/download/v0.1.0/AurOS-Custom.exe", sha256: "e".repeat(64), bytes: 1234 } };
  const f = fixture({ github: { status: async () => { f.calls.status++; return current; } } });
  const { body } = await submit(f);
  let status = await (await f.call(body.status_url, { token: body.token })).json();
  assert.equal(status.download_ready, true);
  current = { state: "failed", runId: 44, failure: "delivery_manifest_invalid" };
  status = await (await f.call(body.status_url, { token: body.token })).json();
  assert.equal(status.download_ready, true, "ready evidence is cached only for the bounded poll lease");
  assert.equal(f.calls.status, 1);
  f.time.t += 15_000;
  status = await (await f.call(body.status_url, { token: body.token })).json();
  assert.equal(status.download_ready, false);
  assert.equal(status.state, "failed");
  assert.equal(status.download, null);
  assert.equal(f.calls.status, 2);
});

test("concurrent status polls claim one GitHub lookup and failures back off", async () => {
  let release;
  let calls = 0;
  const gate = new Promise((resolve) => { release = resolve; });
  const f = fixture({ github: { status: async () => { calls++; await gate; throw new Error("network"); } } });
  const { body } = await submit(f);
  const polls = Array.from({ length: 8 }, () => f.call(body.status_url, { token: body.token }));
  await new Promise((resolve) => setTimeout(resolve, 0));
  release();
  const responses = await Promise.all(polls);
  assert.equal(calls, 1);
  assert.equal(responses.filter((r) => r.status === 503).length, 1);
  assert.equal((await f.call(body.status_url, { token: body.token })).status, 200, "cached state is served during error backoff");
  assert.equal(calls, 1);
});

test("dispatch main movement and upstream failures fail closed", async () => {
  for (const github of [
    { findGreenBase: async () => { throw new Error("none"); } },
    { findGreenBase: async () => ({ sourceSha: "bad", runId: 1 }) },
  ]) {
    const f = fixture({ github });
    const { res, body } = await submit(f);
    assert.equal(res.status, 503);
    assert.equal(body.download_ready, false);
  }
});

test("ambiguous dispatch keeps one correlation ID and reconciles without redispatch", async () => {
  let accepted;
  let statusCalls = 0;
  const f = fixture({ github: {
    dispatch: async (request) => { f.calls.dispatch++; accepted = request; throw new Error("timeout after accept"); },
    status: async ({ requestId, sourceSha }) => {
      statusCalls++;
      assert.equal(requestId, accepted.requestId);
      assert.equal(sourceSha, accepted.sourceSha);
      return statusCalls === 1 ? { state: "not-found" } : { state: "running", runId: 77 };
    },
  } });
  const { res, body } = await submit(f);
  assert.equal(res.status, 202);
  assert.equal(body.state, "dispatch-uncertain");
  let r = await f.call(body.status_url, { token: body.token });
  assert.equal((await r.json()).state, "dispatch-uncertain");
  f.time.t += 15_000;
  r = await f.call(body.status_url, { token: body.token });
  assert.equal((await r.json()).state, "running");
  assert.equal(f.calls.dispatch, 1, "an uncertain accepted request is never blindly sent twice");
});

test("definitive GitHub dispatch rejection is terminal, not an ambiguous outbox item", async () => {
  const f = fixture({ github: { dispatch: async () => { throw new GitHubHttpError(422); } } });
  const { res, body } = await submit(f);
  assert.equal(res.status, 502);
  assert.equal(body.error, "dispatch_rejected");
  assert.equal(f.calls.status, 0);
});

test("status never makes unsigned, skipped-sign, stale, or foreign runs ready", async () => {
  for (const verdict of [
    { state: "queued" },
    { state: "running", runId: 1 },
    { state: "failed", runId: 1, failure: "foreign_run" },
    { state: "failed", runId: 1, failure: "signed_gate_failed" },
    { state: "failed", runId: 1, failure: "signed_artifact_missing" },
  ]) {
    const f = fixture({ github: { status: async () => verdict } });
    const { body } = await submit(f);
    const r = await f.call(body.status_url, { token: body.token });
    const status = await r.json();
    assert.equal(status.download_ready, false);
    assert.equal(status.download, null);
    assert.equal(status.state, verdict.state);
  }
});

function storedZip(name, contents) {
  const filename = new TextEncoder().encode(name);
  const data = new TextEncoder().encode(contents);
  const local = new Uint8Array(30 + filename.length + data.length);
  const lv = new DataView(local.buffer);
  lv.setUint32(0, 0x04034b50, true); lv.setUint16(4, 20, true); lv.setUint16(8, 0, true);
  lv.setUint32(18, data.length, true); lv.setUint32(22, data.length, true); lv.setUint16(26, filename.length, true);
  local.set(filename, 30); local.set(data, 30 + filename.length);
  const central = new Uint8Array(46 + filename.length);
  const cv = new DataView(central.buffer);
  cv.setUint32(0, 0x02014b50, true); cv.setUint16(4, 20, true); cv.setUint16(6, 20, true); cv.setUint16(10, 0, true);
  cv.setUint32(20, data.length, true); cv.setUint32(24, data.length, true); cv.setUint16(28, filename.length, true); cv.setUint32(42, 0, true);
  central.set(filename, 46);
  const end = new Uint8Array(22);
  const ev = new DataView(end.buffer);
  ev.setUint32(0, 0x06054b50, true); ev.setUint16(8, 1, true); ev.setUint16(10, 1, true); ev.setUint32(12, central.length, true); ev.setUint32(16, local.length, true);
  return new Blob([local, central, end]);
}

function apiFixture({ run, jobs = [], artifacts = [], releases = [], assetBody = "", evidenceZip = new Blob() }) {
  const seen = [];
  const request = async (url, init = {}) => {
    seen.push({ url, init });
    if (url.includes("/git/ref/heads/main")) return new Response(JSON.stringify({ object: { sha: SHA } }), { status: 200 });
    if (url.includes("/commits/")) return new Response(JSON.stringify({ sha: SHA }), { status: 200 });
    if (url.includes("/actions/workflows/build.yml/runs")) return new Response(JSON.stringify({ workflow_runs: run ? [run] : [] }), { status: 200 });
    if (url.includes("/actions/workflows/customize.yml/runs")) return new Response(JSON.stringify({ workflow_runs: run ? [run] : [] }), { status: 200 });
    if (url.includes("/jobs")) return new Response(JSON.stringify({ jobs }), { status: 200 });
    if (url.includes("/actions/artifacts/")) return new Response(evidenceZip, { status: 200 });
    if (url.includes("/artifacts")) return new Response(JSON.stringify({ artifacts }), { status: 200 });
    if (url.includes("/releases?")) return new Response(JSON.stringify(releases), { status: 200 });
    if (url.includes("/releases/assets/")) return new Response(assetBody, { status: 200 });
    if (url.includes("/dispatches")) return new Response(null, { status: 204 });
    throw new Error(url);
  };
  return { github: new GitHubBuilds({ repository: "owner/repo", request }), seen };
}

test("GitHub lookup pins the exact successful main push and dispatches fixed workflow/ref", async () => {
  const base = { id: 7, head_sha: SHA, head_branch: "main", event: "push", status: "completed", conclusion: "success", path: ".github/workflows/build.yml" };
  const tag = "v0.1.0-aaaaaaa";
  const artifacts = [{ name: "auros-installer", expired: false }, { name: "auros-base-metadata", expired: false }];
  const f = apiFixture({ run: base, artifacts, releases: [{ tag_name: tag, prerelease: true, draft: false }] });
  assert.deepEqual(await f.github.findGreenBase(), { sourceSha: SHA, runId: 7, tag });
  const id = "12345678-1234-4123-8123-123456789abc";
  await f.github.dispatch({ requestId: id, recipe: "{}", baseRunId: 7, sourceSha: SHA, tag });
  const sent = f.seen.find((x) => x.url.includes("dispatches"));
  assert.deepEqual(JSON.parse(sent.init.body), { ref: tag, inputs: { recipe: "{}", base_build_run_id: "7", request_id: id } });
});

test("GitHub App request accepts the App's RSA key and never forwards auth to asset storage", async () => {
  const { privateKey } = generateKeyPairSync("rsa", { modulusLength: 2048 });
  const pem = privateKey.export({ format: "pem", type: "pkcs1" });
  const calls = [];
  const fetcher = async (url, init) => {
    calls.push({ url: String(url), init });
    if (String(url).includes("/access_tokens")) return new Response(JSON.stringify({ token: "installation-token", expires_at: "2026-10-24T13:00:00Z" }), { status: 201 });
    if (String(url) === "https://api.github.com/asset") return new Response(null, { status: 302, headers: { location: "https://release-assets.githubusercontent.com/signed" } });
    return new Response("manifest", { status: 200 });
  };
  const request = createGitHubAppRequest({ appId: "1", installationId: "2", privateKey: pem, fetcher, now: () => Date.parse("2026-10-24T12:00:00Z") });
  const r = await request("https://api.github.com/asset", { headers: { accept: "application/octet-stream" }, followSigned: true });
  assert.equal(await r.text(), "manifest");
  assert.equal(calls[1].init.headers.authorization, "Bearer installation-token");
  assert.equal("authorization" in calls[2].init.headers, false, "installation token is not sent to signed asset storage");
});

test("GitHub status binds correlation/source and requires package, sign, publish, and exact delivery manifest", async () => {
  const id = "12345678-1234-4123-8123-123456789abc";
  const recipeId = "sha256:" + "c".repeat(64);
  const suffix = recipeId.slice(7);
  const tag = "v0.1.0-aaaaaaa";
  const installerName = `AurOS-Custom-${suffix}.exe`;
  const manifestName = `AurOS-Custom-${suffix}.json`;
  const installerUrl = `https://github.com/owner/repo/releases/download/${tag}/${installerName}`;
  const run = { id: 44, display_title: `customize ${id}`, head_sha: SHA, head_branch: tag, event: "workflow_dispatch", path: ".github/workflows/customize.yml", status: "completed", conclusion: "success" };
  let f = apiFixture({ run: { ...run, head_sha: "b".repeat(40) } });
  assert.equal((await f.github.status({ requestId: id, sourceSha: SHA, baseRunId: 91, recipeId, resolved: { look: "dock", theme: "moss" } })).failure, "foreign_run");
  f = apiFixture({ run, jobs: [{ name: "package", conclusion: "success" }, { name: "sign", conclusion: "skipped" }] });
  assert.equal((await f.github.status({ requestId: id, sourceSha: SHA, baseRunId: 91, recipeId, resolved: { look: "dock", theme: "moss" } })).failure, "delivery_gate_failed");
  const jobs = [{ name: "package", conclusion: "success" }, { name: "sign / sign", conclusion: "success" }, { name: "publish", conclusion: "success" }];
  const assets = [
    { id: 10, name: manifestName, size: 900, url: "https://api.github.com/repos/owner/repo/releases/assets/10", browser_download_url: `https://github.com/owner/repo/releases/download/${tag}/${manifestName}` },
    { id: 11, name: installerName, size: 1234, digest: `sha256:${"e".repeat(64)}`, url: "https://api.github.com/repos/owner/repo/releases/assets/11", browser_download_url: installerUrl },
  ];
  const manifest = {
    schema: "org.auros.custom-delivery", version: 1, state: "download-ready", request_id: id,
    source: { repository: "owner/repo", commit: SHA, workflow_run_id: 44 },
    base: { build_run_id: 91, tag, image_sha256: "d".repeat(64), image_bytes: 123456 },
    recipe: { id: recipeId, base: "desktop", resolved_preferences: { look: "dock", theme: "moss" } },
    installer: { name: installerName, url: installerUrl, sha256: "e".repeat(64), bytes: 1234 },
  };
  const manifestText = JSON.stringify(manifest);
  const evidenceZip = storedZip(manifestName, manifestText);
  const artifacts = [
    { id: 8, name: "auros-setup-signed", expired: false, workflow_run: { id: 44 } },
    { id: 9, name: "auros-custom-delivery", expired: false, size_in_bytes: evidenceZip.size, archive_download_url: "https://api.github.com/repos/owner/repo/actions/artifacts/9/zip", workflow_run: { id: 44 } },
  ];
  f = apiFixture({ run, jobs, artifacts, releases: [{ tag_name: tag, prerelease: true, draft: false, assets }], assetBody: manifestText, evidenceZip });
  const ready = await f.github.status({ requestId: id, sourceSha: SHA, baseRunId: 91, recipeId, resolved: { look: "dock", theme: "moss" } });
  assert.equal(ready.state, "download-ready");
  assert.equal(ready.download.url, installerUrl);
  assert.equal(ready.download.sha256, "e".repeat(64));

  f = apiFixture({ run, jobs, artifacts, releases: [{ tag_name: tag, prerelease: true, draft: false, assets }], assetBody: JSON.stringify({ ...manifest, request_id: "22345678-1234-4123-8123-123456789abc" }), evidenceZip });
  assert.equal((await f.github.status({ requestId: id, sourceSha: SHA, baseRunId: 91, recipeId, resolved: { look: "dock", theme: "moss" } })).failure, "delivery_manifest_invalid");

  for (const digest of [undefined, "sha256:not-a-hash", `sha256:${"f".repeat(64)}`]) {
    const replaced = assets.map((asset) => asset.name === installerName ? { ...asset, digest } : asset);
    f = apiFixture({ run, jobs, artifacts, releases: [{ tag_name: tag, prerelease: true, draft: false, assets: replaced }], assetBody: manifestText, evidenceZip });
    assert.equal((await f.github.status({ requestId: id, sourceSha: SHA, baseRunId: 91, recipeId, resolved: { look: "dock", theme: "moss" } })).failure, digest?.startsWith("sha256:") && digest.length === 71 ? "delivery_manifest_invalid" : "delivery_asset_invalid");
  }
});
