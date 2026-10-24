// Public custom-build control plane. It accepts only recipe-v1 data and never
// calls the Wishes model. GitHub and signing credentials stay behind this API.

import recipeSchema from "../../src/customize/recipe-v1.schema.json" with { type: "json" };
import { dayOf, DAY_MS } from "./ledger.js";

const enc = new TextEncoder();
const RECIPE_LIMIT = 16 * 1024;
const ZIP_LIMIT = 64 * 1024;
const JOB_MS = 24 * 60 * 60 * 1000;
const RECEIPT = /^[A-Za-z0-9_-]{32}$/;
const JOB_ID = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
const SHA = /^[0-9a-f]{40}$/;
const REPO = /^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$/;
const HASH = /^[0-9a-f]{64}$/;
const RECIPE_ID = /^sha256:[0-9a-f]{64}$/;
const TAG = /^v[0-9A-Za-z.+-]+-[0-9a-f]{7}$/;
const CHALLENGE = /^([A-Za-z0-9_-]{22})\.([1-9][0-9]{9,12})\.([0-9a-f]{64})\.([A-Za-z0-9_-]{43})$/;

const b64 = (bytes) => btoa(String.fromCharCode(...new Uint8Array(bytes))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const hash = async (value) => b64(await crypto.subtle.digest("SHA-256", enc.encode(value)));
const hex = (bytes) => [...new Uint8Array(bytes)].map((x) => x.toString(16).padStart(2, "0")).join("");
const sha256Hex = async (value) => hex(await crypto.subtle.digest("SHA-256", enc.encode(value)));
const endOfDay = (now) => Date.parse(dayOf(now)) + 2 * DAY_MS;
async function hmac(secret, value) {
  const key = await crypto.subtle.importKey("raw", enc.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  return b64(await crypto.subtle.sign("HMAC", key, enc.encode(value)));
}
function constantEqual(a, b) {
  if (a.length !== b.length) return false;
  let different = 0;
  for (let i = 0; i < a.length; i++) different |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return different === 0;
}
function leadingZeroBits(bytes) {
  let bits = 0;
  for (const byte of bytes) {
    if (byte === 0) { bits += 8; continue; }
    return bits + Math.clz32(byte) - 24;
  }
  return bits;
}
const netOf = (ip) => {
  if (!ip) return "unknown";
  if (ip.includes(".") && !ip.includes(":")) return ip.split(".").slice(0, 3).join(".");
  const [head, tail = ""] = ip.toLowerCase().split("::");
  const h = head ? head.split(":") : [];
  const t = tail ? tail.split(":") : [];
  return [...h, ...Array(Math.max(0, 8 - h.length - t.length)).fill("0"), ...t].slice(0, 4).map((x) => x.replace(/^0+(?=.)/, "")).join(":");
};

async function readLimited(request, limit) {
  const declared = Number(request.headers.get("content-length") || 0);
  if (declared > limit || !request.body) throw new Error("recipe_too_large");
  const reader = request.body.getReader();
  const chunks = [];
  let size = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.byteLength;
    if (size > limit) { await reader.cancel(); throw new Error("recipe_too_large"); }
    chunks.push(value);
  }
  const raw = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) { raw.set(chunk, offset); offset += chunk.byteLength; }
  return raw;
}

function storedZipFile(zip, expectedName) {
  const view = new DataView(zip.buffer, zip.byteOffset, zip.byteLength);
  const u16 = (at) => view.getUint16(at, true);
  const u32 = (at) => view.getUint32(at, true);
  let eocd = -1;
  for (let at = zip.length - 22; at >= Math.max(0, zip.length - 65_557); at--) {
    if (u32(at) === 0x06054b50) { eocd = at; break; }
  }
  if (eocd < 0 || u16(eocd + 4) !== 0 || u16(eocd + 6) !== 0 || u16(eocd + 8) !== 1 || u16(eocd + 10) !== 1) throw new Error("delivery artifact ZIP layout");
  const central = u32(eocd + 16);
  if (central + 46 > zip.length || u32(central) !== 0x02014b50) throw new Error("delivery artifact ZIP central directory");
  const flags = u16(central + 8);
  const method = u16(central + 10);
  const compressed = u32(central + 20);
  const uncompressed = u32(central + 24);
  const nameLength = u16(central + 28);
  const extraLength = u16(central + 30);
  const commentLength = u16(central + 32);
  const local = u32(central + 42);
  const name = new TextDecoder("utf-8", { fatal: true }).decode(zip.slice(central + 46, central + 46 + nameLength));
  if ((flags & 1) || method !== 0 || compressed !== uncompressed || uncompressed <= 0 || uncompressed > RECIPE_LIMIT || name !== expectedName || central + 46 + nameLength + extraLength + commentLength > zip.length) throw new Error("delivery artifact ZIP entry");
  if (local + 30 > zip.length || u32(local) !== 0x04034b50 || (u16(local + 6) & 1) || u16(local + 8) !== 0) throw new Error("delivery artifact ZIP local entry");
  const localName = u16(local + 26);
  const localExtra = u16(local + 28);
  const localFilename = new TextDecoder("utf-8", { fatal: true }).decode(zip.slice(local + 30, local + 30 + localName));
  const data = local + 30 + localName + localExtra;
  if (localFilename !== expectedName || data + uncompressed > central) throw new Error("delivery artifact ZIP bounds");
  return zip.slice(data, data + uncompressed);
}

function sameBytes(a, b) {
  if (a.length !== b.length) return false;
  let different = 0;
  for (let i = 0; i < a.length; i++) different |= a[i] ^ b[i];
  return different === 0;
}

function strictJson(text) {
  let at = 0;
  const ws = () => { while (/\s/.test(text[at] || "")) at++; };
  const string = () => {
    const start = at++;
    let escaped = false;
    while (at < text.length) {
      const c = text[at++];
      if (!escaped && c === '"') {
        try { return JSON.parse(text.slice(start, at)); } catch { throw new Error("invalid_json"); }
      }
      if (!escaped && c === "\\") escaped = true; else escaped = false;
      if (!escaped && c.charCodeAt(0) < 0x20) throw new Error("invalid_json");
    }
    throw new Error("invalid_json");
  };
  const value = (depth = 0) => {
    if (depth > 8) throw new Error("recipe_too_deep");
    ws();
    if (text[at] === '"') return string();
    if (text[at] === "{") {
      at++; ws(); const out = Object.create(null);
      if (text[at] === "}") { at++; return out; }
      for (;;) {
        ws(); if (text[at] !== '"') throw new Error("invalid_json");
        const key = string();
        if (Object.hasOwn(out, key)) throw new Error("duplicate_field");
        ws(); if (text[at++] !== ":") throw new Error("invalid_json");
        out[key] = value(depth + 1); ws();
        const c = text[at++];
        if (c === "}") return out;
        if (c !== ",") throw new Error("invalid_json");
      }
    }
    if (text[at] === "[") {
      at++; ws(); const out = [];
      if (text[at] === "]") { at++; return out; }
      for (;;) {
        out.push(value(depth + 1)); ws();
        const c = text[at++];
        if (c === "]") return out;
        if (c !== ",") throw new Error("invalid_json");
      }
    }
    for (const [word, result] of [["true", true], ["false", false], ["null", null]]) {
      if (text.startsWith(word, at)) { at += word.length; return result; }
    }
    const m = /^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?/.exec(text.slice(at));
    if (!m) throw new Error("invalid_json");
    at += m[0].length;
    const n = Number(m[0]);
    if (!Number.isFinite(n)) throw new Error("invalid_json_number");
    return { __jsonNumber: m[0], value: n };
  };
  const result = value(); ws();
  if (at !== text.length) throw new Error("invalid_json");
  return result;
}

const integer = (value) => value?.__jsonNumber && /^[1-9][0-9]*$/.test(value.__jsonNumber) ? value.value : null;

function deliveryManifest(raw, expected) {
  if (!(raw instanceof Uint8Array) || raw.byteLength > RECIPE_LIMIT) throw new Error("delivery manifest too large");
  let text;
  try { text = new TextDecoder("utf-8", { fatal: true }).decode(raw); } catch { throw new Error("delivery manifest encoding"); }
  const m = strictJson(text);
  if (!exact(m, ["schema", "version", "state", "request_id", "source", "base", "recipe", "installer"], ["schema", "version", "state", "request_id", "source", "base", "recipe", "installer"])) throw new Error("delivery manifest fields");
  if (m.schema !== "org.auros.custom-delivery" || m.version?.__jsonNumber !== "1" || m.state !== "download-ready" || m.request_id !== expected.requestId) throw new Error("delivery manifest identity");
  if (!exact(m.source, ["repository", "commit", "workflow_run_id"], ["repository", "commit", "workflow_run_id"]) || m.source.repository !== expected.repository || m.source.commit !== expected.sourceSha || integer(m.source.workflow_run_id) !== expected.runId) throw new Error("delivery manifest source");
  if (!exact(m.base, ["build_run_id", "tag", "image_sha256", "image_bytes"], ["build_run_id", "tag", "image_sha256", "image_bytes"]) || integer(m.base.build_run_id) !== expected.baseRunId || !TAG.test(m.base.tag) || !HASH.test(m.base.image_sha256) || integer(m.base.image_bytes) === null) throw new Error("delivery manifest base");
  if (!exact(m.recipe, ["id", "base", "resolved_preferences"], ["id", "base", "resolved_preferences"]) || m.recipe.id !== expected.recipeId || m.recipe.base !== "desktop") throw new Error("delivery manifest recipe");
  const prefs = m.recipe.resolved_preferences;
  if (!exact(prefs, ["look", "theme"], ["look", "theme"]) || prefs.look !== expected.resolved.look || prefs.theme !== expected.resolved.theme) throw new Error("delivery manifest preferences");
  if (!exact(m.installer, ["name", "url", "sha256", "bytes"], ["name", "url", "sha256", "bytes"]) || m.installer.name !== expected.installerName || m.installer.url !== expected.installerUrl || !HASH.test(m.installer.sha256) || integer(m.installer.bytes) !== expected.installerBytes) throw new Error("delivery manifest installer");
  return { download: { name: m.installer.name, url: m.installer.url, sha256: m.installer.sha256, bytes: integer(m.installer.bytes), manifest_url: expected.manifestUrl }, baseTag: m.base.tag };
}

const exact = (value, allowed, required = []) => value && typeof value === "object" && !Array.isArray(value) &&
  Object.keys(value).every((x) => allowed.includes(x)) && required.every((x) => Object.hasOwn(value, x));

export async function compileDesktopRecipe(raw) {
  if (!(raw instanceof Uint8Array) || raw.byteLength > RECIPE_LIMIT) throw new Error("recipe_too_large");
  let text;
  try { text = new TextDecoder("utf-8", { fatal: true }).decode(raw); } catch { throw new Error("invalid_encoding"); }
  const value = strictJson(text);
  if (!exact(value, ["schema", "version", "base", "preferences"], ["schema", "version", "base"])) throw new Error("recipe_fields");
  if (value.schema !== "org.auros.recipe" || value.version?.__jsonNumber !== "1") throw new Error("recipe_contract");
  if (value.base !== "desktop") throw new Error("desktop_only");
  const preferences = value.preferences ?? {};
  if (!exact(preferences, ["look", "theme"])) throw new Error("preference_fields");
  const looks = recipeSchema.properties.preferences.properties.look.enum;
  const themes = recipeSchema.properties.preferences.properties.theme.enum;
  if (preferences.look !== undefined && !looks.includes(preferences.look)) throw new Error("preference_look");
  if (preferences.theme !== undefined && !themes.includes(preferences.theme)) throw new Error("preference_theme");
  const recipe = { schema: "org.auros.recipe", version: 1, base: "desktop" };
  if (value.preferences !== undefined) {
    recipe.preferences = {};
    if (preferences.look !== undefined) recipe.preferences.look = preferences.look;
    if (preferences.theme !== undefined) recipe.preferences.theme = preferences.theme;
  }
  // JSON.stringify's replacer would omit nested keys, so construct the exact
  // Python sort_keys representation explicitly for this closed schema.
  const prefs = recipe.preferences;
  const stable = `{"base":"desktop"${prefs === undefined ? "" : `,"preferences":{${[
    prefs.look === undefined ? "" : `"look":${JSON.stringify(prefs.look)}`,
    prefs.theme === undefined ? "" : `"theme":${JSON.stringify(prefs.theme)}`,
  ].filter(Boolean).join(",")}}`},"schema":"org.auros.recipe","version":1}`;
  return { recipe, canonical: stable, recipeId: "sha256:" + await sha256Hex(stable) };
}

export class BuildJobLedger {
  constructor(storage, cfg) { this.s = storage; this.cfg = cfg; this.chain = Promise.resolve(); }
  serial(fn) { const p = this.chain.then(fn); this.chain = p.catch(() => {}); return p; }
  async val(key, now) { const e = await this.s.get(key); return e && e.exp > now ? e.v : undefined; }
  admit({ netHash, challengeId, challengeExpires, now }) {
    return this.serial(async () => {
      const challengeKey = `build-challenge:${challengeId}`;
      if (challengeExpires <= now || await this.val(challengeKey, now)) return { status: 409, reason: "challenge_used" };
      await this.s.put(challengeKey, { v: true, exp: challengeExpires });
      const day = dayOf(now);
      const nk = `build-net:${day}:${netHash}`;
      const net = (await this.val(nk, now)) ?? 0;
      if (net >= this.cfg.perNet) return { status: 429, reason: "network_quota" };
      await this.s.put(nk, { v: net + 1, exp: endOfDay(now) });
      const gk = `build-global:${day}`;
      const global = (await this.val(gk, now)) ?? 0;
      if (global >= this.cfg.global) return { status: 429, reason: "global_quota" };
      await this.s.put(gk, { v: global + 1, exp: endOfDay(now) });
      return { status: 200 };
    });
  }
  reserve({ id, tokenHash, sourceSha, baseRunId, baseTag, recipeId, canonical, now }) {
    return this.serial(async () => {
      const dk = `build-dedupe:${sourceSha}:${recipeId}`;
      const existingId = await this.val(dk, now);
      if (existingId) {
        const existing = await this.val(`build-job:${existingId}`, now);
        if (existing) {
          existing.owners.push(tokenHash);
          await this.s.put(`build-job:${existingId}`, { v: existing, exp: existing.expiresAt });
          return { status: 200, deduplicated: true, job: existing };
        }
      }
      const job = { id, owners: [tokenHash], sourceSha, baseRunId, baseTag, recipeId, canonical, state: "dispatching", dispatchAttempts: 1, runId: null, signedArtifactId: null, download: null, nextCheckAt: now, createdAt: now, expiresAt: now + JOB_MS, failure: null };
      await this.s.put(`build-job:${id}`, { v: job, exp: job.expiresAt });
      await this.s.put(dk, { v: id, exp: job.expiresAt });
      return { status: 201, deduplicated: false, job };
    });
  }
  claimPoll({ id, tokenHash, now }) {
    return this.serial(async () => {
      const job = await this.val(`build-job:${id}`, now);
      if (!job || !job.owners.includes(tokenHash)) return null;
      if (job.state === "failed" || job.nextCheckAt > now) return { job, claimed: false };
      job.nextCheckAt = now + 15_000;
      await this.s.put(`build-job:${id}`, { v: job, exp: job.expiresAt });
      return { job, claimed: true };
    });
  }
  update({ id, patch, now }) {
    return this.serial(async () => {
      const job = await this.val(`build-job:${id}`, now);
      if (!job) return null;
      Object.assign(job, patch);
      await this.s.put(`build-job:${id}`, { v: job, exp: job.expiresAt });
      return job;
    });
  }
  async sweep(now) {
    const dead = [];
    for (const [key, entry] of await this.s.list()) if (!entry || entry.exp <= now) dead.push(key);
    if (dead.length) await this.s.delete(dead);
    return dead.length;
  }
}

function response(status, body, cors = {}) {
  return new Response(body === null ? null : JSON.stringify(body), { status, headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store", ...cors } });
}

export function createBuildApi({ env, jobs, github, now = Date.now }) {
  const enabled = env.BUILD_REQUESTS_ENABLED === "1";
  const origin = String(env.BUILD_ALLOWED_ORIGIN || "");
  const perNet = Number(env.BUILD_REQUESTS_PER_NET || 1);
  const global = Number(env.BUILD_REQUESTS_GLOBAL || 5);
  const powBits = Number(env.BUILD_POW_BITS || 20);
  const configured = /^https:\/\/[A-Za-z0-9.-]+(?::[0-9]+)?$/.test(origin) &&
    String(env.BUILD_NETWORK_SECRET || "").length >= 32 && String(env.BUILD_CHALLENGE_SECRET || "").length >= 32 &&
    Number.isInteger(perNet) && perNet >= 1 && perNet <= 3 &&
    Number.isInteger(global) && global >= 1 && global <= 20 && Number.isInteger(powBits) && powBits >= 12 && powBits <= 24;
  const corsFor = (request) => request.headers.get("origin") === origin ? { "access-control-allow-origin": origin, vary: "Origin" } : {};
  const authorizeOrigin = (request) => request.headers.get("origin") === origin;
  const receipt = () => b64(crypto.getRandomValues(new Uint8Array(24)));
  return async function buildApi(request) {
    const url = new URL(request.url);
    const cors = corsFor(request);
    if (request.method === "OPTIONS") {
      if (!enabled || !configured || !authorizeOrigin(request)) return response(403, { error: "builds_disabled" });
      return new Response(null, { status: 204, headers: { ...cors, "access-control-allow-methods": "GET, POST, OPTIONS", "access-control-allow-headers": "authorization, content-type, x-auros-challenge, x-auros-proof", "access-control-max-age": "600" } });
    }
    if (!enabled || !configured) return response(503, { error: "builds_disabled", download_ready: false }, cors);
    if (!authorizeOrigin(request)) return response(403, { error: "origin", download_ready: false });
    if (url.pathname === "/v1/builds/challenge" && request.method === "POST") {
      let raw;
      try { raw = await readLimited(request, RECIPE_LIMIT); }
      catch (e) { return response(400, { error: e.message }, cors); }
      let compiled;
      try { compiled = await compileDesktopRecipe(raw); } catch (e) { return response(400, { error: e.message }, cors); }
      const network = netOf(request.headers.get("cf-connecting-ip"));
      const netHash = await hash("network:" + env.BUILD_NETWORK_SECRET + ":" + network);
      const id = b64(crypto.getRandomValues(new Uint8Array(16)));
      const expires = now() + 5 * 60_000;
      const recipeHex = compiled.recipeId.slice(7);
      const payload = `${id}.${expires}.${recipeHex}.${netHash}`;
      const token = `${id}.${expires}.${recipeHex}.${await hmac(env.BUILD_CHALLENGE_SECRET, payload)}`;
      return response(200, { challenge: token, expires_at: new Date(expires).toISOString(), bits: powBits, proof_prefix: "auros-build-v1:" }, cors);
    }
    if (url.pathname === "/v1/builds" && request.method === "POST") {
      let raw;
      try { raw = await readLimited(request, RECIPE_LIMIT); }
      catch (e) { return response(400, { error: e.message, download_ready: false }, cors); }
      let compiled;
      try { compiled = await compileDesktopRecipe(raw); } catch (e) { return response(400, { error: e.message, download_ready: false }, cors); }
      const network = netOf(request.headers.get("cf-connecting-ip"));
      const netHash = await hash("network:" + env.BUILD_NETWORK_SECRET + ":" + network);
      const challenge = CHALLENGE.exec(request.headers.get("x-auros-challenge") || "");
      const proof = request.headers.get("x-auros-proof") || "";
      if (!challenge || !/^[0-9]{1,16}$/.test(proof) || challenge[3] !== compiled.recipeId.slice(7)) return response(401, { error: "challenge", download_ready: false }, cors);
      const expires = Number(challenge[2]);
      const payload = `${challenge[1]}.${challenge[2]}.${challenge[3]}.${netHash}`;
      const expected = await hmac(env.BUILD_CHALLENGE_SECRET, payload);
      const proofHash = new Uint8Array(await crypto.subtle.digest("SHA-256", enc.encode(`auros-build-v1:${challenge[0]}:${proof}`)));
      if (expires <= now() || !constantEqual(expected, challenge[4]) || leadingZeroBits(proofHash) < powBits) return response(401, { error: "challenge", download_ready: false }, cors);
      const admitted = await jobs.admit({ netHash, challengeId: challenge[1], challengeExpires: expires, now: now() });
      if (admitted.status === 429) return response(429, { error: admitted.reason, download_ready: false }, cors);
      if (admitted.status === 409) return response(409, { error: admitted.reason, download_ready: false }, cors);
      let base;
      try { base = await github.findGreenBase(); } catch { return response(503, { error: "base_unavailable", download_ready: false }, cors); }
      if (!SHA.test(base.sourceSha) || !Number.isInteger(base.runId) || base.runId <= 0 || !TAG.test(base.tag || "") || !base.tag.endsWith(`-${base.sourceSha.slice(0, 7)}`)) return response(503, { error: "base_unavailable", download_ready: false }, cors);
      const token = receipt();
      const id = crypto.randomUUID();
      const tokenHash = await hash("receipt:" + token);
      const reserved = await jobs.reserve({ id, tokenHash, sourceSha: base.sourceSha, baseRunId: base.runId, baseTag: base.tag, recipeId: compiled.recipeId, canonical: compiled.canonical, now: now() });
      const job = reserved.job;
      if (!reserved.deduplicated) {
        try {
          await github.dispatch({ requestId: job.id, recipe: compiled.canonical, baseRunId: base.runId, sourceSha: base.sourceSha, tag: base.tag });
          await jobs.update({ id: job.id, patch: { state: "queued" }, now: now() });
        } catch (error) {
          if (error instanceof GitHubHttpError && error.status >= 400 && error.status < 500) {
            await jobs.update({ id: job.id, patch: { state: "failed", failure: `dispatch_rejected_${error.status}` }, now: now() });
            return response(502, { error: "dispatch_rejected", download_ready: false }, cors);
          }
          // workflow_dispatch returns no run ID. A timeout can mean GitHub
          // accepted it, so preserve the same correlation ID and reconcile;
          // never blindly dispatch a second signing job.
          await jobs.update({ id: job.id, patch: { state: "dispatch-uncertain", failure: null }, now: now() });
          return response(202, { id: job.id, token, recipe_id: job.recipeId, state: "dispatch-uncertain", status_url: `/v1/builds/${job.id}`, download_ready: false }, cors);
        }
      }
      return response(reserved.status, { id: job.id, token, recipe_id: job.recipeId, state: reserved.deduplicated ? job.state : "queued", status_url: `/v1/builds/${job.id}`, download_ready: false }, cors);
    }
    const match = /^\/v1\/builds\/([^/]+)$/.exec(url.pathname);
    if (match && request.method === "GET") {
      if (!JOB_ID.test(match[1])) return response(404, { error: "not_found", download_ready: false }, cors);
      const auth = /^Bearer ([A-Za-z0-9_-]+)$/.exec(request.headers.get("authorization") || "");
      if (!auth || !RECEIPT.test(auth[1])) return response(401, { error: "receipt", download_ready: false }, cors);
      const claimed = await jobs.claimPoll({ id: match[1], tokenHash: await hash("receipt:" + auth[1]), now: now() });
      if (!claimed) return response(404, { error: "not_found", download_ready: false }, cors);
      let { job } = claimed;
      if (claimed.claimed) {
        let verdict;
        const submitted = strictJson(job.canonical);
        const resolved = { look: submitted.preferences?.look || "rail", theme: submitted.preferences?.theme || "sandstone" };
        try { verdict = await github.status({ requestId: job.id, sourceSha: job.sourceSha, baseRunId: job.baseRunId, recipeId: job.recipeId, resolved }); }
        catch { return response(503, { error: "status_unavailable", state: job.state, download_ready: false }, cors); }
        const allowed = new Set(["not-found", "queued", "running", "failed", "download-ready"]);
        if (!verdict || !allowed.has(verdict.state)) return response(503, { error: "status_invalid", state: job.state, download_ready: false }, cors);
        const state = verdict.state === "not-found" ? (job.state === "dispatch-uncertain" ? "dispatch-uncertain" : "queued") : verdict.state;
        job = await jobs.update({ id: job.id, patch: { state, runId: verdict.runId ?? job.runId, signedArtifactId: verdict.signedArtifactId ?? null, download: verdict.download ?? null, failure: verdict.failure ?? null, nextCheckAt: now() + 15_000 }, now: now() });
      }
      return response(200, { id: job.id, recipe_id: job.recipeId, state: job.state, failure: job.failure, download_ready: job.state === "download-ready", download: job.state === "download-ready" ? job.download : null, expires_at: new Date(job.expiresAt).toISOString() }, cors);
    }
    return response(404, { error: "path", download_ready: false }, cors);
  };
}

export class GitHubBuilds {
  constructor({ repository, request }) {
    if (!REPO.test(repository)) throw new Error("invalid GitHub repository");
    this.repository = repository;
    this.request = request;
  }
  async api(path, init = {}) {
    const r = await this.request(`https://api.github.com/repos/${this.repository}${path}`, init);
    if (!r.ok) throw new GitHubHttpError(r.status);
    return r.status === 204 ? null : r.json();
  }
  async findGreenBase() {
    const q = new URLSearchParams({ branch: "main", event: "push", status: "success", per_page: "10" });
    const found = await this.api(`/actions/workflows/build.yml/runs?${q}`);
    const releases = await this.api("/releases?per_page=20");
    const runs = (found.workflow_runs || []).filter((x) => SHA.test(x.head_sha || "") && x.head_branch === "main" && x.event === "push" && x.status === "completed" && x.conclusion === "success" && x.path === ".github/workflows/build.yml");
    for (const run of runs) {
      const matching = releases.filter((release) => release.prerelease === true && release.draft !== true && TAG.test(release.tag_name || "") && release.tag_name.endsWith(`-${run.head_sha.slice(0, 7)}`));
      if (matching.length !== 1) continue;
      const release = matching[0];
      const [commit, artifacts] = await Promise.all([this.api(`/commits/${encodeURIComponent(release.tag_name)}`), this.api(`/actions/runs/${run.id}/artifacts?per_page=100`)]);
      if (commit.sha !== run.head_sha) continue;
      const required = ["auros-installer", "auros-base-metadata"];
      if (!required.every((name) => (artifacts.artifacts || []).filter((x) => x.name === name && !x.expired).length === 1)) continue;
      return { sourceSha: run.head_sha, runId: run.id, tag: release.tag_name };
    }
    throw new Error("exact green released base unavailable");
  }
  async dispatch({ requestId, recipe, baseRunId, sourceSha, tag }) {
    if (!JOB_ID.test(requestId) || !SHA.test(sourceSha) || !Number.isInteger(baseRunId) || !TAG.test(tag || "") || !tag.endsWith(`-${sourceSha.slice(0, 7)}`)) throw new GitHubHttpError(409);
    let commit;
    try { commit = await this.api(`/commits/${encodeURIComponent(tag)}`); }
    catch (error) { throw error instanceof GitHubHttpError ? error : new GitHubHttpError(503); }
    if (commit.sha !== sourceSha) throw new GitHubHttpError(409);
    await this.api("/actions/workflows/customize.yml/dispatches", { method: "POST", body: JSON.stringify({ ref: tag, inputs: { recipe, base_build_run_id: String(baseRunId), request_id: requestId } }) });
  }
  async status({ requestId, sourceSha, baseRunId, recipeId, resolved }) {
    const q = new URLSearchParams({ event: "workflow_dispatch", per_page: "20" });
    const listed = await this.api(`/actions/workflows/customize.yml/runs?${q}`);
    const matches = (listed.workflow_runs || []).filter((x) => x.display_title === `customize ${requestId}`);
    if (matches.length === 0) return { state: "not-found" };
    if (matches.length !== 1 || matches[0].head_sha !== sourceSha || matches[0].event !== "workflow_dispatch" || matches[0].path !== ".github/workflows/customize.yml") return { state: "failed", failure: "foreign_run" };
    const run = matches[0];
    if (run.status !== "completed") return { state: "running", runId: run.id };
    if (run.conclusion !== "success") return { state: "failed", runId: run.id, failure: "workflow_failed" };
    const [jobs, artifacts] = await Promise.all([this.api(`/actions/runs/${run.id}/jobs?per_page=100`), this.api(`/actions/runs/${run.id}/artifacts?per_page=100`)]);
    const named = (name) => (jobs.jobs || []).filter((x) => x.name === name);
    if (named("package").length !== 1 || named("package")[0].conclusion !== "success" || named("sign / sign").length !== 1 || named("sign / sign")[0].conclusion !== "success" || named("publish").length !== 1 || named("publish")[0].conclusion !== "success") return { state: "failed", runId: run.id, failure: "delivery_gate_failed" };
    const signed = (artifacts.artifacts || []).filter((x) => x.name === "auros-setup-signed" && !x.expired && x.workflow_run?.id === run.id);
    if (signed.length !== 1) return { state: "failed", runId: run.id, failure: "signed_artifact_missing" };
    if (!RECIPE_ID.test(recipeId || "")) return { state: "failed", runId: run.id, failure: "recipe_identity_invalid" };
    const suffix = recipeId.slice("sha256:".length);
    const manifestName = `AurOS-Custom-${suffix}.json`;
    const installerName = `AurOS-Custom-${suffix}.exe`;
    const immutable = (artifacts.artifacts || []).filter((x) => x.name === "auros-custom-delivery" && !x.expired && x.workflow_run?.id === run.id);
    if (immutable.length !== 1 || !Number.isInteger(immutable[0].id) || immutable[0].id <= 0 || !Number.isInteger(immutable[0].size_in_bytes) || immutable[0].size_in_bytes <= 0 || immutable[0].size_in_bytes > ZIP_LIMIT) return { state: "failed", runId: run.id, failure: "delivery_evidence_missing" };
    const releases = await this.api("/releases?per_page=20");
    const candidates = releases.filter((release) => (release.assets || []).some((asset) => asset.name === manifestName));
    if (candidates.length !== 1) return { state: "failed", runId: run.id, failure: "delivery_manifest_missing" };
    const release = candidates[0];
    const manifestAssets = (release.assets || []).filter((x) => x.name === manifestName);
    const installerAssets = (release.assets || []).filter((x) => x.name === installerName);
    if (manifestAssets.length !== 1 || installerAssets.length !== 1) return { state: "failed", runId: run.id, failure: "delivery_assets_ambiguous" };
    const manifestAsset = manifestAssets[0];
    const installerAsset = installerAssets[0];
    const expectedUrl = `https://github.com/${this.repository}/releases/download/${release.tag_name}/${installerName}`;
    const expectedManifestUrl = `https://github.com/${this.repository}/releases/download/${release.tag_name}/${manifestName}`;
    const expectedAssetApi = `https://api.github.com/repos/${this.repository}/releases/assets/${manifestAsset.id}`;
    const expectedInstallerApi = `https://api.github.com/repos/${this.repository}/releases/assets/${installerAsset.id}`;
    if (!TAG.test(release.tag_name || "") || release.draft === true || release.prerelease !== true ||
        !Number.isInteger(manifestAsset.id) || manifestAsset.id <= 0 || manifestAsset.url !== expectedAssetApi ||
        !Number.isInteger(manifestAsset.size) || manifestAsset.size <= 0 || manifestAsset.size > RECIPE_LIMIT ||
        manifestAsset.browser_download_url !== expectedManifestUrl ||
        !Number.isInteger(installerAsset.id) || installerAsset.id <= 0 || installerAsset.url !== expectedInstallerApi ||
        !Number.isInteger(installerAsset.size) || installerAsset.size <= 0 || installerAsset.browser_download_url !== expectedUrl ||
        !/^sha256:[0-9a-f]{64}$/.test(installerAsset.digest || "")) {
      return { state: "failed", runId: run.id, failure: "delivery_asset_invalid" };
    }
    let raw;
    try {
      const evidenceApi = `https://api.github.com/repos/${this.repository}/actions/artifacts/${immutable[0].id}/zip`;
      if (immutable[0].archive_download_url !== evidenceApi) throw new Error("delivery evidence URL invalid");
      const [releaseResponse, evidenceResponse] = await Promise.all([
        this.request(manifestAsset.url, { headers: { accept: "application/octet-stream" }, followSigned: true }),
        this.request(immutable[0].archive_download_url, { headers: { accept: "application/octet-stream" }, followSigned: true }),
      ]);
      if (!releaseResponse.ok || !evidenceResponse.ok) throw new Error("manifest download failed");
      raw = await readLimited(releaseResponse, RECIPE_LIMIT);
      const zip = await readLimited(evidenceResponse, ZIP_LIMIT);
      if (!sameBytes(raw, storedZipFile(zip, manifestName))) throw new Error("mutable release manifest differs from immutable run evidence");
      const parsed = deliveryManifest(raw, { requestId, repository: this.repository, sourceSha, runId: run.id, baseRunId, recipeId, resolved, installerName, installerUrl: expectedUrl, installerBytes: installerAsset.size, manifestUrl: manifestAsset.browser_download_url });
      if (release.tag_name !== parsed.baseTag) throw new Error("release tag mismatch");
      if (installerAsset.digest !== `sha256:${parsed.download.sha256}`) throw new Error("release installer digest mismatch");
      const download = parsed.download;
      return { state: "download-ready", runId: run.id, signedArtifactId: signed[0].id, download };
    } catch {
      return { state: "failed", runId: run.id, failure: "delivery_manifest_invalid" };
    }
  }
}

export class GitHubHttpError extends Error {
  constructor(status) { super(`GitHub ${status}`); this.name = "GitHubHttpError"; this.status = status; }
}

function pemBytes(pem) {
  const text = String(pem).replace(/\\n/g, "\n");
  const body = text.replace(/-----[^-]+-----/g, "").replace(/\s/g, "");
  if (!body) throw new Error("GitHub App private key is missing");
  const der = Uint8Array.from(atob(body), (c) => c.charCodeAt(0));
  if (!text.includes("BEGIN RSA PRIVATE KEY")) return der;
  const length = (n) => n < 128 ? Uint8Array.of(n) : (() => {
    const bytes = [];
    while (n) { bytes.unshift(n & 255); n >>>= 8; }
    return Uint8Array.of(0x80 | bytes.length, ...bytes);
  })();
  const item = (tag, bytes) => Uint8Array.of(tag, ...length(bytes.length), ...bytes);
  const version = Uint8Array.of(0x02, 0x01, 0x00);
  const rsa = Uint8Array.of(0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00);
  return item(0x30, Uint8Array.of(...version, ...rsa, ...item(0x04, der)));
}

export function createGitHubAppRequest({ appId, installationId, privateKey, fetcher = fetch, now = Date.now }) {
  let cached;
  const header = b64(enc.encode(JSON.stringify({ alg: "RS256", typ: "JWT" })));
  const appJwt = async () => {
    const issued = Math.floor(now() / 1000) - 30;
    const payload = b64(enc.encode(JSON.stringify({ iat: issued, exp: issued + 540, iss: String(appId) })));
    const unsigned = `${header}.${payload}`;
    const key = await crypto.subtle.importKey("pkcs8", pemBytes(privateKey), { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["sign"]);
    return `${unsigned}.${b64(await crypto.subtle.sign("RSASSA-PKCS1-v1_5", key, enc.encode(unsigned)))}`;
  };
  const installationToken = async () => {
    if (!/^[1-9][0-9]*$/.test(String(appId)) || !/^[1-9][0-9]*$/.test(String(installationId))) throw new Error("GitHub App identifiers are invalid");
    if (cached && cached.expires > now() + 60_000) return cached.token;
    const jwt = await appJwt();
    const r = await fetcher(`https://api.github.com/app/installations/${installationId}/access_tokens`, { method: "POST", headers: { authorization: `Bearer ${jwt}`, accept: "application/vnd.github+json", "user-agent": "auros-build-control", "x-github-api-version": "2022-11-28" } });
    if (!r.ok) throw new Error(`GitHub App token ${r.status}`);
    const body = await r.json();
    if (typeof body.token !== "string" || !body.expires_at) throw new Error("GitHub App token response invalid");
    cached = { token: body.token, expires: Date.parse(body.expires_at) };
    return cached.token;
  };
  return async (url, init = {}) => {
    const { followSigned, ...requestInit } = init;
    const first = await fetcher(url, { ...requestInit, redirect: "manual", headers: { accept: "application/vnd.github+json", "content-type": "application/json", "user-agent": "auros-build-control", "x-github-api-version": "2022-11-28", authorization: `Bearer ${await installationToken()}`, ...(init.headers || {}) } });
    if (!followSigned || ![301, 302, 303, 307, 308].includes(first.status)) return first;
    const location = first.headers.get("location");
    const target = new URL(location || "https://invalid.invalid");
    if (target.protocol !== "https:" || !(target.hostname === "github.com" || target.hostname.endsWith(".githubusercontent.com"))) throw new Error("GitHub asset redirect host invalid");
    return fetcher(target.toString(), { method: "GET", redirect: "error", headers: { "user-agent": "auros-build-control" } });
  };
}
