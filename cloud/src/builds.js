// Public custom-build control plane. It accepts only recipe-v1 data and never
// calls the Wishes model. GitHub and signing credentials stay behind this API.

import recipeSchema from "../../src/customize/recipe-v1.schema.json" with { type: "json" };
import { dayOf, DAY_MS } from "./ledger.js";

const enc = new TextEncoder();
const RECIPE_LIMIT = 16 * 1024;
const ZIP_LIMIT = 64 * 1024;
const JOB_MS = 24 * 60 * 60 * 1000;
const DISCOVERY_MS = 60 * 1000;
const RECEIPT = /^[A-Za-z0-9_-]{32}$/;
const JOB_ID = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
const SHA = /^[0-9a-f]{40}$/;
const REPO = /^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$/;
const HASH = /^[0-9a-f]{64}$/;
const RECIPE_ID = /^sha256:[0-9a-f]{64}$/;
const TAG = /^v[0-9A-Za-z.+-]+-[0-9a-f]{7}$/;
const CHALLENGE = /^([A-Za-z0-9_-]{22})\.([1-9][0-9]{9,12})\.([0-9a-f]{64})\.([A-Za-z0-9_-]{43})$/;
const JSON_NUMBER = Symbol("strict JSON number");
const DELIVERY_GATES = ["package", "sign / sign", "certify / e2e (base)", "certify / custom-certification", "publish"];
const BASE_RELEASE_ASSETS = ["AurOS-Setup.exe", "SHA256SUMS", "pieces.txt", "image.sha256", "image.bytes"];
const BASE_RELEASE_LIMIT = 50;
const BASE_CANDIDATE_LIMIT = 5;
const BASE_RUN_LIMIT = 3;

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
  const ws = () => { while (" \t\r\n".includes(text[at] || "\0")) at++; };
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
    return { [JSON_NUMBER]: m[0], value: n };
  };
  const result = value(); ws();
  if (at !== text.length) throw new Error("invalid_json");
  return result;
}

const integer = (value) => value?.[JSON_NUMBER] && /^[1-9][0-9]*$/.test(value[JSON_NUMBER]) ? value.value : null;

function deliveryManifest(raw, expected) {
  if (!(raw instanceof Uint8Array) || raw.byteLength > RECIPE_LIMIT) throw new Error("delivery manifest too large");
  let text;
  try { text = new TextDecoder("utf-8", { fatal: true }).decode(raw); } catch { throw new Error("delivery manifest encoding"); }
  const m = strictJson(text);
  if (!exact(m, ["schema", "version", "state", "request_id", "source", "base", "recipe", "installer"], ["schema", "version", "state", "request_id", "source", "base", "recipe", "installer"])) throw new Error("delivery manifest fields");
  if (m.schema !== "org.auros.custom-delivery" || m.version?.[JSON_NUMBER] !== "1" || m.state !== "download-ready" || m.request_id !== expected.requestId) throw new Error("delivery manifest identity");
  if (!exact(m.source, ["repository", "commit", "workflow_run_id"], ["repository", "commit", "workflow_run_id"]) || m.source.repository !== expected.repository || m.source.commit !== expected.authoritySha || integer(m.source.workflow_run_id) !== expected.runId) throw new Error("delivery manifest source");
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
  if (value.schema !== "org.auros.recipe" || value.version?.[JSON_NUMBER] !== "1") throw new Error("recipe_contract");
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
  reserve({ id, tokenHash, authoritySha, baseSha, baseRunId, baseTag, recipeId, canonical, discoveryId = null, now }) {
    return this.serial(async () => {
      const dk = `build-dedupe:${authoritySha}:${recipeId}`;
      const existingId = await this.val(dk, now);
      if (existingId) {
        const existing = await this.val(`build-job:${existingId}`, now);
        if (existing) {
          existing.owners.push(tokenHash);
          await this.s.put(`build-job:${existingId}`, { v: existing, exp: existing.expiresAt });
          return { status: 200, deduplicated: true, job: existing };
        }
      }
      const deliveryKey = `build-delivery:${authoritySha}:${recipeId}`;
      const saved = await this.s.get(deliveryKey);
      const delivery = saved?.v;
      if (delivery && (delivery.authoritySha !== authoritySha || delivery.baseSha !== baseSha || delivery.baseRunId !== baseRunId || delivery.baseTag !== baseTag || delivery.recipeId !== recipeId || delivery.canonical !== canonical || !JOB_ID.test(delivery.requestId || ""))) throw new Error("canonical delivery identity conflict");
      const discoveryKey = `build-discovery:${authoritySha}:${recipeId}`;
      if (!delivery) {
        const discovery = await this.val(discoveryKey, now);
        if (!discoveryId || discovery?.id !== discoveryId) throw new Error("custom delivery discovery lease lost");
      }
      const reused = Boolean(delivery);
      const job = { id, owners: [tokenHash], authoritySha, baseSha, baseRunId, baseTag, recipeId, canonical, deliveryRequestId: delivery?.requestId ?? id, state: reused ? "queued" : "dispatching", dispatchAttempts: reused ? 0 : 1, runId: delivery?.runId ?? null, signedArtifactId: null, download: null, nextCheckAt: now, createdAt: now, expiresAt: now + JOB_MS, failure: null };
      await this.s.put(`build-job:${id}`, { v: job, exp: job.expiresAt });
      await this.s.put(dk, { v: id, exp: job.expiresAt });
      if (!delivery) await this.s.put(deliveryKey, { v: { authoritySha, baseSha, baseRunId, baseTag, recipeId, canonical, requestId: id, runId: null, evidence: null, createdAt: now } });
      if (!delivery) await this.s.delete(discoveryKey);
      return { status: reused ? 200 : 201, deduplicated: reused, canonicalReuse: reused, job };
    });
  }
  beginDiscovery({ authoritySha, recipeId, id, now }) {
    return this.serial(async () => {
      const delivery = (await this.s.get(`build-delivery:${authoritySha}:${recipeId}`))?.v;
      if (delivery) return { state: "existing" };
      const key = `build-discovery:${authoritySha}:${recipeId}`;
      if (await this.val(key, now)) return { state: "busy" };
      await this.s.put(key, { v: { id }, exp: now + DISCOVERY_MS });
      return { state: "owner" };
    });
  }
  releaseDiscovery({ authoritySha, recipeId, id, now }) {
    return this.serial(async () => {
      const key = `build-discovery:${authoritySha}:${recipeId}`;
      const discovery = await this.val(key, now);
      if (discovery?.id !== id) return false;
      await this.s.delete(key);
      return true;
    });
  }
  deliveryForRecipe({ recipeId }) {
    return this.serial(async () => {
      const found = [];
      for (const [key, entry] of await this.s.list()) {
        if (key.startsWith("build-delivery:") && entry?.v?.recipeId === recipeId) found.push(entry.v);
      }
      found.sort((a, b) => (b.createdAt || 0) - (a.createdAt || 0));
      return found[0] ?? null;
    });
  }
  delivery({ authoritySha, recipeId }) {
    return this.serial(async () => (await this.s.get(`build-delivery:${authoritySha}:${recipeId}`))?.v ?? null);
  }
  adoptDelivery({ authoritySha, baseSha, baseRunId, baseTag, recipeId, canonical, requestId, runId, evidence, discoveryId, now }) {
    return this.serial(async () => {
      if (!SHA.test(authoritySha || "") || !SHA.test(baseSha || "") || !Number.isInteger(baseRunId) || baseRunId <= 0 || !TAG.test(baseTag || "") || !RECIPE_ID.test(recipeId || "") || !JOB_ID.test(requestId || "") || !Number.isInteger(runId) || runId <= 0 || typeof canonical !== "string") return { adopted: false, conflict: true };
      const encoded = JSON.stringify(evidence);
      const gatesExact = Array.isArray(evidence?.gates) && evidence.gates.length === DELIVERY_GATES.length && evidence.gates.every((name, at) => name === DELIVERY_GATES[at]);
      if (!evidence || encoded.length > RECIPE_LIMIT * 2 || evidence.requestId !== requestId || evidence.runId !== runId || evidence.authoritySha !== authoritySha || evidence.baseSha !== baseSha || evidence.baseRunId !== baseRunId || evidence.releaseTag !== baseTag || !gatesExact || !Number.isInteger(evidence.signedArtifactId) || evidence.signedArtifactId <= 0 || !Number.isInteger(evidence.immutableArtifactId) || evidence.immutableArtifactId <= 0 || !Number.isInteger(evidence.releaseId) || evidence.releaseId <= 0 || !Number.isInteger(evidence.manifestAssetId) || evidence.manifestAssetId <= 0 || !Number.isInteger(evidence.installerAssetId) || evidence.installerAssetId <= 0 || !/^sha256:[0-9a-f]{64}$/.test(evidence.installerDigest || "") || typeof evidence.manifest !== "string") return { adopted: false, conflict: true };
      const key = `build-delivery:${authoritySha}:${recipeId}`;
      const discoveryKey = `build-discovery:${authoritySha}:${recipeId}`;
      const discovery = await this.val(discoveryKey, now);
      if (!discoveryId || discovery?.id !== discoveryId) return { adopted: false, conflict: true };
      const value = { authoritySha, baseSha, baseRunId, baseTag, recipeId, canonical, requestId, runId, evidence, createdAt: now };
      const existing = (await this.s.get(key))?.v;
      if (existing) {
        await this.s.delete(discoveryKey);
        return JSON.stringify(existing) === JSON.stringify({ ...value, createdAt: existing.createdAt }) ? { adopted: false, conflict: false } : { adopted: false, conflict: true };
      }
      await this.s.put(key, { v: value });
      await this.s.delete(discoveryKey);
      return { adopted: true, conflict: false };
    });
  }
  saveDelivery({ id, runId, evidence }) {
    return this.serial(async () => {
      const jobEntry = await this.s.get(`build-job:${id}`);
      const job = jobEntry?.v;
      if (!job) return false;
      const key = `build-delivery:${job.authoritySha}:${job.recipeId}`;
      const entry = await this.s.get(key);
      const delivery = entry?.v;
      if (!delivery || delivery.requestId !== job.deliveryRequestId) return false;
      if (runId !== undefined && runId !== null) {
        if (!Number.isInteger(runId) || runId <= 0 || (delivery.runId !== null && delivery.runId !== runId)) return false;
        delivery.runId = runId;
      }
      if (evidence !== undefined && evidence !== null) {
        const encoded = JSON.stringify(evidence);
        if (encoded.length > RECIPE_LIMIT * 2) return false;
        if (delivery.evidence !== null && JSON.stringify(delivery.evidence) !== encoded) return false;
        delivery.evidence = evidence;
      }
      await this.s.put(key, { v: delivery });
      return true;
    });
  }
  abandonDelivery({ id }) {
    return this.serial(async () => {
      const job = (await this.s.get(`build-job:${id}`))?.v;
      if (!job) return false;
      const key = `build-delivery:${job.authoritySha}:${job.recipeId}`;
      const delivery = (await this.s.get(key))?.v;
      if (!delivery || delivery.requestId !== job.deliveryRequestId || delivery.runId !== null || delivery.evidence !== null) return false;
      await this.s.delete(key);
      return true;
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
    for (const [key, entry] of await this.s.list()) {
      const canonical = key.startsWith("build-delivery:") && entry && entry.exp === undefined;
      if (!canonical && (!entry || !Number.isFinite(entry.exp) || entry.exp <= now)) dead.push(key);
    }
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
  const powBits = Number(env.BUILD_POW_BITS || 16);
  const staticConfigured = /^https:\/\/[A-Za-z0-9.-]+(?::[0-9]+)?$/.test(origin) &&
    String(env.BUILD_NETWORK_SECRET || "").length >= 32 && String(env.BUILD_CHALLENGE_SECRET || "").length >= 32 &&
    Number.isInteger(perNet) && perNet >= 1 && perNet <= 3 &&
    Number.isInteger(global) && global >= 1 && global <= 20 && Number.isInteger(powBits) && powBits >= 12 && powBits <= 24;
  let githubConfiguration;
  const configured = () => staticConfigured &&
    (githubConfiguration ??= githubAppConfigured(env));
  const corsFor = (request) => request.headers.get("origin") === origin ? { "access-control-allow-origin": origin, vary: "Origin" } : {};
  const authorizeOrigin = (request) => request.headers.get("origin") === origin;
  const receipt = () => b64(crypto.getRandomValues(new Uint8Array(24)));
  return async function buildApi(request) {
    const url = new URL(request.url);
    const cors = corsFor(request);
    if (request.method === "OPTIONS") {
      if (!enabled || !await configured() || !authorizeOrigin(request)) return response(403, { error: "builds_disabled" });
      return new Response(null, { status: 204, headers: { ...cors, "access-control-allow-methods": "GET, POST, OPTIONS", "access-control-allow-headers": "authorization, content-type, x-auros-challenge, x-auros-proof", "access-control-max-age": "600" } });
    }
    if (!enabled || !await configured()) return response(503, { error: "builds_disabled", download_ready: false }, cors);
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
      try { base = await github.findGreenBase(); }
      catch {
        const prior = await jobs.deliveryForRecipe({ recipeId: compiled.recipeId });
        if (prior) base = { authoritySha: prior.authoritySha, baseSha: prior.baseSha, runId: prior.baseRunId, tag: prior.baseTag };
        else return response(503, { error: "base_unavailable", download_ready: false }, cors);
      }
      if (!SHA.test(base.authoritySha) || !SHA.test(base.baseSha) || !Number.isInteger(base.runId) || base.runId <= 0 || !TAG.test(base.tag || "") || !base.tag.endsWith(`-${base.baseSha.slice(0, 7)}`)) return response(503, { error: "base_unavailable", download_ready: false }, cors);
      const discoveryId = crypto.randomUUID();
      const discoveryLease = await jobs.beginDiscovery({ authoritySha: base.authoritySha, recipeId: compiled.recipeId, id: discoveryId, now: now() });
      if (discoveryLease.state === "busy") return response(503, { error: "existing_delivery_busy", download_ready: false }, cors);
      if (discoveryLease.state === "owner") {
        let discovered;
        const submitted = compiled.recipe;
        const resolved = { look: submitted.preferences?.look || "rail", theme: submitted.preferences?.theme || "sandstone" };
        try { discovered = await github.discoverVerifiedDelivery({ authoritySha: base.authoritySha, baseSha: base.baseSha, baseRunId: base.runId, baseTag: base.tag, recipeId: compiled.recipeId, resolved }); }
        catch {
          await jobs.releaseDiscovery({ authoritySha: base.authoritySha, recipeId: compiled.recipeId, id: discoveryId, now: now() });
          return response(503, { error: "existing_delivery_unavailable", download_ready: false }, cors);
        }
        if (discovered.state === "invalid") {
          await jobs.releaseDiscovery({ authoritySha: base.authoritySha, recipeId: compiled.recipeId, id: discoveryId, now: now() });
          return response(409, { error: "existing_delivery_invalid", failure: discovered.failure, download_ready: false }, cors);
        }
        if (discovered.state === "unavailable") {
          await jobs.releaseDiscovery({ authoritySha: base.authoritySha, recipeId: compiled.recipeId, id: discoveryId, now: now() });
          return response(503, { error: "existing_delivery_unavailable", failure: discovered.failure, download_ready: false }, cors);
        }
        if (discovered.state === "download-ready") {
          const adopted = await jobs.adoptDelivery({ authoritySha: base.authoritySha, baseSha: base.baseSha, baseRunId: base.runId, baseTag: base.tag, recipeId: compiled.recipeId, canonical: compiled.canonical, requestId: discovered.evidence?.requestId, runId: discovered.runId, evidence: discovered.evidence, discoveryId, now: now() });
          if (!adopted || adopted.conflict) {
            await jobs.releaseDiscovery({ authoritySha: base.authoritySha, recipeId: compiled.recipeId, id: discoveryId, now: now() });
            return response(409, { error: "existing_delivery_conflict", download_ready: false }, cors);
          }
        } else if (discovered.state !== "absent") {
          await jobs.releaseDiscovery({ authoritySha: base.authoritySha, recipeId: compiled.recipeId, id: discoveryId, now: now() });
          return response(503, { error: "existing_delivery_unavailable", download_ready: false }, cors);
        }
      }
      const token = receipt();
      const id = crypto.randomUUID();
      const tokenHash = await hash("receipt:" + token);
      let reserved;
      try { reserved = await jobs.reserve({ id, tokenHash, authoritySha: base.authoritySha, baseSha: base.baseSha, baseRunId: base.runId, baseTag: base.tag, recipeId: compiled.recipeId, canonical: compiled.canonical, discoveryId: discoveryLease.state === "owner" ? discoveryId : null, now: now() }); }
      catch {
        await jobs.releaseDiscovery({ authoritySha: base.authoritySha, recipeId: compiled.recipeId, id: discoveryId, now: now() });
        return response(409, { error: "existing_delivery_conflict", download_ready: false }, cors);
      }
      const job = reserved.job;
      if (!reserved.deduplicated) {
        try {
          await github.dispatch({ requestId: job.id, recipe: compiled.canonical, authoritySha: base.authoritySha, baseSha: base.baseSha, baseRunId: base.runId, baseTag: base.tag });
          await jobs.update({ id: job.id, patch: { state: "queued" }, now: now() });
        } catch (error) {
          if (error instanceof GitHubHttpError && error.status >= 400 && error.status < 500) {
            await jobs.abandonDelivery({ id: job.id });
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
        const delivery = await jobs.delivery({ authoritySha: job.authoritySha, recipeId: job.recipeId });
        if (!delivery || delivery.requestId !== job.deliveryRequestId || delivery.authoritySha !== job.authoritySha || delivery.baseSha !== job.baseSha || delivery.baseRunId !== job.baseRunId || delivery.baseTag !== job.baseTag || delivery.canonical !== job.canonical) {
          job = await jobs.update({ id: job.id, patch: { state: "failed", failure: "canonical_delivery_missing", download: null }, now: now() });
          return response(200, { id: job.id, recipe_id: job.recipeId, state: job.state, failure: job.failure, download_ready: false, download: null, expires_at: new Date(job.expiresAt).toISOString() }, cors);
        }
        try { verdict = await github.status({ requestId: delivery.requestId, authoritySha: job.authoritySha, baseSha: job.baseSha, baseRunId: job.baseRunId, recipeId: job.recipeId, resolved, expectedRunId: delivery.runId, evidence: delivery.evidence }); }
        catch {
          if (job.state === "download-ready") job = await jobs.update({ id: job.id, patch: { state: "queued", download: null, failure: null }, now: now() });
          return response(503, { error: "status_unavailable", state: job.state, download_ready: false }, cors);
        }
        const allowed = new Set(["not-found", "queued", "running", "failed", "download-ready"]);
        if (!verdict || !allowed.has(verdict.state)) return response(503, { error: "status_invalid", state: job.state, download_ready: false }, cors);
        if ((verdict.runId || verdict.evidence) && !await jobs.saveDelivery({ id: job.id, runId: verdict.runId, evidence: verdict.evidence })) {
          verdict = { state: "failed", runId: verdict.runId, failure: "canonical_delivery_conflict" };
        }
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
    const main = await this.api("/git/ref/heads/main");
    const authoritySha = main?.object?.sha;
    if (!SHA.test(authoritySha || "")) throw new Error("main authority unavailable");
    const releases = await this.api(`/releases?per_page=${BASE_RELEASE_LIMIT}`);
    const candidates = (releases || []).filter((release) => {
      const assets = release.assets || [];
      return release.prerelease === true && release.draft !== true && TAG.test(release.tag_name || "") &&
        BASE_RELEASE_ASSETS.every((name) => assets.filter((asset) => asset.name === name).length === 1);
    }).slice(0, BASE_CANDIDATE_LIMIT);
    for (const release of candidates) {
      const commit = await this.api(`/commits/${encodeURIComponent(release.tag_name)}`);
      if (!SHA.test(commit.sha || "") || !release.tag_name.endsWith(`-${commit.sha.slice(0, 7)}`)) continue;
      const q = new URLSearchParams({ branch: "main", event: "push", status: "success", head_sha: commit.sha, per_page: String(BASE_RUN_LIMIT) });
      const found = await this.api(`/actions/workflows/build.yml/runs?${q}`);
      if (!Number.isInteger(found.total_count) || found.total_count < 1 || found.total_count > BASE_RUN_LIMIT) continue;
      const runs = (found.workflow_runs || []).filter((x) => Number.isInteger(x.id) && x.id > 0 && x.head_sha === commit.sha && x.head_branch === "main" && x.event === "push" && x.status === "completed" && x.conclusion === "success" && x.path === ".github/workflows/build.yml");
      const eligible = [];
      for (const run of runs) {
        const artifacts = await this.api(`/actions/runs/${run.id}/artifacts?per_page=100`);
        const required = ["auros-installer", "auros-base-metadata"];
        if (Number.isInteger(artifacts.total_count) && artifacts.total_count <= 100 && required.every((name) => (artifacts.artifacts || []).filter((x) => x.name === name && !x.expired).length === 1)) eligible.push(run);
      }
      if (eligible.length === 1) return { authoritySha, baseSha: commit.sha, runId: eligible[0].id, tag: release.tag_name };
    }
    throw new Error("exact green released base unavailable");
  }
  async dispatch({ requestId, recipe, authoritySha, baseSha, baseRunId, baseTag }) {
    if (!JOB_ID.test(requestId) || !SHA.test(authoritySha) || !SHA.test(baseSha) || !Number.isInteger(baseRunId) || !TAG.test(baseTag || "") || !baseTag.endsWith(`-${baseSha.slice(0, 7)}`)) throw new GitHubHttpError(409);
    let main, commit;
    try { [main, commit] = await Promise.all([this.api("/git/ref/heads/main"), this.api(`/commits/${encodeURIComponent(baseTag)}`)]); }
    catch (error) { throw error instanceof GitHubHttpError ? error : new GitHubHttpError(503); }
    if (main?.object?.sha !== authoritySha || commit.sha !== baseSha) throw new GitHubHttpError(409);
    await this.api("/actions/workflows/customize.yml/dispatches", { method: "POST", body: JSON.stringify({ ref: "main", inputs: { recipe, base_build_sha: baseSha, base_build_tag: baseTag, base_build_run_id: String(baseRunId), request_id: requestId } }) });
  }
  async discoverVerifiedDelivery({ authoritySha, baseSha, baseRunId, baseTag, recipeId, resolved }) {
    if (!SHA.test(authoritySha || "") || !SHA.test(baseSha || "") || !Number.isInteger(baseRunId) || baseRunId <= 0 || !TAG.test(baseTag || "") || !RECIPE_ID.test(recipeId || "")) throw new Error("invalid discovery identity");
    const suffix = recipeId.slice("sha256:".length);
    const manifestName = `AurOS-Custom-${suffix}.json`;
    const installerName = `AurOS-Custom-${suffix}.exe`;
    const release = await this.api(`/releases/tags/${encodeURIComponent(baseTag)}`);
    const manifests = (release.assets || []).filter((asset) => asset.name === manifestName);
    const installers = (release.assets || []).filter((asset) => asset.name === installerName);
    if (manifests.length === 0 && installers.length === 0) return { state: "absent" };
    if (manifests.length !== 1 || installers.length !== 1) return { state: "invalid", failure: "existing_delivery_assets_invalid" };
    const manifestAsset = manifests[0];
    const expectedApi = `https://api.github.com/repos/${this.repository}/releases/assets/${manifestAsset.id}`;
    const expectedUrl = `https://github.com/${this.repository}/releases/download/${baseTag}/${manifestName}`;
    if (!Number.isInteger(release.id) || release.id <= 0 || release.tag_name !== baseTag || release.prerelease !== true || release.draft === true || !Number.isInteger(manifestAsset.id) || manifestAsset.id <= 0 || manifestAsset.url !== expectedApi || manifestAsset.browser_download_url !== expectedUrl || !Number.isInteger(manifestAsset.size) || manifestAsset.size <= 0 || manifestAsset.size > RECIPE_LIMIT) return { state: "invalid", failure: "existing_delivery_assets_invalid" };
    let locator;
    try {
      const response = await this.request(expectedApi, { headers: { accept: "application/octet-stream" }, followSigned: true });
      if (!response.ok) throw new Error("manifest download failed");
      const raw = await readLimited(response, RECIPE_LIMIT);
      locator = strictJson(new TextDecoder("utf-8", { fatal: true }).decode(raw));
    } catch {
      return { state: "invalid", failure: "existing_delivery_locator_invalid" };
    }
    const requestId = locator?.request_id;
    const runId = integer(locator?.source?.workflow_run_id);
    if (!JOB_ID.test(requestId || "") || runId === null) return { state: "invalid", failure: "existing_delivery_locator_invalid" };
    const verdict = await this.status({ requestId, authoritySha, baseSha, baseRunId, recipeId, resolved, expectedRunId: runId, expectedBaseTag: baseTag });
    if (verdict.state === "download-ready") return verdict;
    if (["queued", "running", "not-found"].includes(verdict.state)) return { state: "unavailable", failure: "existing_delivery_not_ready" };
    return { state: "invalid", failure: verdict.failure || "existing_delivery_invalid" };
  }
  async status({ requestId, authoritySha, baseSha, baseRunId, recipeId, resolved, expectedRunId = null, evidence = null, expectedBaseTag = null }) {
    let run;
    let signed = [];
    let immutable = [];
    if (evidence) {
      const gatesExact = Array.isArray(evidence.gates) && evidence.gates.length === DELIVERY_GATES.length && evidence.gates.every((name, at) => name === DELIVERY_GATES[at]);
      if (evidence.requestId !== requestId || evidence.runId !== expectedRunId || evidence.authoritySha !== authoritySha || evidence.baseSha !== baseSha || evidence.baseRunId !== baseRunId || !Number.isInteger(evidence.signedArtifactId) || evidence.signedArtifactId <= 0 || !Number.isInteger(evidence.immutableArtifactId) || evidence.immutableArtifactId <= 0 || !gatesExact) return { state: "failed", failure: "canonical_evidence_invalid" };
      run = { id: evidence.runId, display_title: `customize ${requestId}`, head_sha: authoritySha, head_branch: "main", event: "workflow_dispatch", path: ".github/workflows/customize.yml", status: "completed", conclusion: "success" };
    } else if (expectedRunId !== null) {
      if (!Number.isInteger(expectedRunId) || expectedRunId <= 0) return { state: "failed", failure: "foreign_run" };
      run = await this.api(`/actions/runs/${expectedRunId}`);
    } else {
      const q = new URLSearchParams({ event: "workflow_dispatch", per_page: "20" });
      const listed = await this.api(`/actions/workflows/customize.yml/runs?${q}`);
      const matches = (listed.workflow_runs || []).filter((x) => x.display_title === `customize ${requestId}`);
      if (matches.length === 0) return { state: "not-found" };
      if (matches.length !== 1) return { state: "failed", failure: "foreign_run" };
      run = matches[0];
    }
    if (run.id !== (expectedRunId ?? run.id) || run.display_title !== `customize ${requestId}` || run.head_sha !== authoritySha || run.head_branch !== "main" || run.event !== "workflow_dispatch" || run.path !== ".github/workflows/customize.yml") return { state: "failed", runId: run.id, failure: "foreign_run" };
    if (run.status !== "completed") return { state: "running", runId: run.id };
    if (run.conclusion !== "success") return { state: "failed", runId: run.id, failure: "workflow_failed" };
    if (!evidence) {
      const [jobs, artifacts] = await Promise.all([this.api(`/actions/runs/${run.id}/jobs?per_page=100`), this.api(`/actions/runs/${run.id}/artifacts?per_page=100`)]);
      const named = (name) => (jobs.jobs || []).filter((x) => x.name === name);
      if (DELIVERY_GATES.some((name) => named(name).length !== 1 || named(name)[0].conclusion !== "success")) return { state: "failed", runId: run.id, failure: "delivery_gate_failed" };
      signed = (artifacts.artifacts || []).filter((x) => x.name === "auros-setup-signed" && !x.expired && x.workflow_run?.id === run.id);
      if (signed.length !== 1) return { state: "failed", runId: run.id, failure: "signed_artifact_missing" };
      immutable = (artifacts.artifacts || []).filter((x) => x.name === "auros-custom-delivery" && !x.expired && x.workflow_run?.id === run.id);
      if (immutable.length !== 1 || !Number.isInteger(immutable[0].id) || immutable[0].id <= 0 || !Number.isInteger(immutable[0].size_in_bytes) || immutable[0].size_in_bytes <= 0 || immutable[0].size_in_bytes > ZIP_LIMIT) return { state: "failed", runId: run.id, failure: "delivery_evidence_missing" };
    }
    if (!RECIPE_ID.test(recipeId || "")) return { state: "failed", runId: run.id, failure: "recipe_identity_invalid" };
    const suffix = recipeId.slice("sha256:".length);
    const manifestName = `AurOS-Custom-${suffix}.json`;
    const installerName = `AurOS-Custom-${suffix}.exe`;
    const exactTag = evidence?.releaseTag || expectedBaseTag;
    const releases = exactTag ? [await this.api(`/releases/tags/${encodeURIComponent(exactTag)}`)] : await this.api("/releases?per_page=20");
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
    if (!Number.isInteger(release.id) || release.id <= 0 || !TAG.test(release.tag_name || "") || (expectedBaseTag !== null && release.tag_name !== expectedBaseTag) || release.draft === true || release.prerelease !== true ||
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
      const [releaseResponse, commit] = await Promise.all([
        this.request(manifestAsset.url, { headers: { accept: "application/octet-stream" }, followSigned: true }),
        this.api(`/commits/${encodeURIComponent(release.tag_name)}`),
      ]);
      if (!releaseResponse.ok || commit.sha !== baseSha) throw new Error("manifest or release source invalid");
      raw = await readLimited(releaseResponse, RECIPE_LIMIT);
      if (evidence) {
        if (evidence.requestId !== requestId || evidence.runId !== run.id || evidence.releaseId !== release.id || evidence.releaseTag !== release.tag_name || evidence.manifestAssetId !== manifestAsset.id || evidence.installerAssetId !== installerAsset.id || evidence.installerDigest !== installerAsset.digest || typeof evidence.manifest !== "string") throw new Error("canonical delivery evidence identity");
        if (!sameBytes(raw, enc.encode(evidence.manifest))) throw new Error("mutable release manifest differs from canonical evidence");
      } else {
        const evidenceApi = `https://api.github.com/repos/${this.repository}/actions/artifacts/${immutable[0].id}/zip`;
        if (immutable[0].archive_download_url !== evidenceApi) throw new Error("delivery evidence URL invalid");
        const evidenceResponse = await this.request(immutable[0].archive_download_url, { headers: { accept: "application/octet-stream" }, followSigned: true });
        if (!evidenceResponse.ok) throw new Error("delivery evidence download failed");
        const zip = await readLimited(evidenceResponse, ZIP_LIMIT);
        if (!sameBytes(raw, storedZipFile(zip, manifestName))) throw new Error("mutable release manifest differs from immutable run evidence");
      }
      const parsed = deliveryManifest(raw, { requestId, repository: this.repository, authoritySha, runId: run.id, baseRunId, recipeId, resolved, installerName, installerUrl: expectedUrl, installerBytes: installerAsset.size, manifestUrl: manifestAsset.browser_download_url });
      if (release.tag_name !== parsed.baseTag) throw new Error("release tag mismatch");
      if (installerAsset.digest !== `sha256:${parsed.download.sha256}`) throw new Error("release installer digest mismatch");
      const download = parsed.download;
      const canonicalEvidence = evidence ?? { requestId, runId: run.id, authoritySha, baseSha, baseRunId, gates: DELIVERY_GATES, signedArtifactId: signed[0].id, immutableArtifactId: immutable[0].id, releaseId: release.id, releaseTag: release.tag_name, manifestAssetId: manifestAsset.id, installerAssetId: installerAsset.id, installerDigest: installerAsset.digest, manifest: new TextDecoder("utf-8", { fatal: true }).decode(raw) };
      return { state: "download-ready", runId: run.id, signedArtifactId: signed[0]?.id ?? null, download, evidence: canonicalEvidence };
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

async function githubAppConfigured(env) {
  if (!/^[1-9][0-9]*$/.test(String(env.GITHUB_APP_ID || "")) ||
      !/^[1-9][0-9]*$/.test(String(env.GITHUB_APP_INSTALLATION_ID || ""))) return false;
  const key = String(env.GITHUB_APP_PRIVATE_KEY || "").replace(/\\n/g, "\n").trim();
  if (!/^-----BEGIN (RSA PRIVATE KEY|PRIVATE KEY)-----\s+[A-Za-z0-9+/=\s]+\s+-----END \1-----$/.test(key)) return false;
  try {
    await crypto.subtle.importKey("pkcs8", pemBytes(key), { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["sign"]);
    return true;
  } catch {
    return false;
  }
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
