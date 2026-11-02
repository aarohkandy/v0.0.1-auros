// The Wishes service (docs/AI.md section 5 as amended by section 7): a plain
// fetch handler, so the same code runs on Cloudflare Workers (src/worker.js)
// and under Node (tests, dev-server.mjs).
import { deploymentIdentity, smokeLogIdentity } from "./deployment-identity.js";
import Anthropic from "@anthropic-ai/sdk";
import { ASK_OUTPUT, MAX_TEXT, MISSING, WISH_OUTPUT, checkRequest, checkWish, parseWish } from "./validate.js";
import { ASK_SYSTEM, WISH_SYSTEM, askMessage, wishMessage } from "./prompts.js";
import { DAY_MS, dayOf } from "./ledger.js";
import { CONFIGURE_MAX_TEXT, analyzeConfigureRequest, planFromSelection, unsupportedPlan } from "./configure.js";

// Haiku-class per AI.md 7.10: thinking off so the fixed max_tokens all goes to
// the answer. Changing the model means changing PRICE and re-running
// test/live-eval.mjs; it is deliberately not a setting.
export const MODEL = "claude-haiku-5-5";
const PRICE = { in: 0.10, write: 0.125, read: 0.01, out: 0.50 }; // USD per million tokens, prompts under 100k
const MAX_TOKENS = { wish: 400, ask: 300 };
const MAX_COST_USD = 0.01; // a request estimated above this is refused (AI.md 7.10)
const MAX_BODY = 8 * 1024;
const DEADLINE_MS = 15_000; // the computer gives up at 20 s
const TOKEN_DAYS = 90;

const TEXT = {
  limit: "You've made lots of wishes today — more tomorrow",
  resting: "Wishes are resting for a little while",
};

const enc = new TextEncoder();
const b64 = (bytes) => btoa(String.fromCharCode(...new Uint8Array(bytes))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const unb64 = (s) => Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0));
const json = (status, body, headers = {}) => new Response(body === null ? null : JSON.stringify(body), {
  status,
  headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store", ...headers },
});
const secondsToMidnight = (now) => Math.max(60, Math.ceil((Date.parse(dayOf(now)) + DAY_MS - now) / 1000));

export function config(env) {
  const n = (name, dflt) => (env[name] === undefined || env[name] === "" ? dflt : Number(env[name]));
  return {
    budgetUsd: n("DAILY_BUDGET_USD", 5),
    quotaEstablished: n("QUOTA_ESTABLISHED", 20),
    quotaNew: n("QUOTA_NEW", 3),
    establishDays: n("ESTABLISH_DAYS", 7),
    registerPerNet: n("REGISTER_PER_NET", 3),
    maxInflight: n("MAX_INFLIGHT", 40),
    ledgerMs: n("LEDGER_TIMEOUT_MS", 6000),
    powBits: n("POW_BITS", 21),
    banned: new Set(String(env.BANNED || "").split(/[\s,]+/).filter(Boolean)),
    countries: countries(env.ALLOW_COUNTRIES),
  };
}

// Where Wishes is offered (docs/legal/COMPLIANCE.md AI-6). ISO 3166 codes,
// comma-separated; "UK" is read as GB; "*" means everywhere; unset means the
// default below. The country is request.cf.country, set by Cloudflare's edge (a
// client cannot forge it); an unknown country ("XX", Tor "T1") or none is refused.
export const DEFAULT_COUNTRIES = "US,CA,AU"; // GB is added back (one variable) once PRI-6, a UK representative, is settled
function countries(v) {
  const list = String(v === undefined || String(v).trim() === "" ? DEFAULT_COUNTRIES : v)
    .toUpperCase().split(/[\s,]+/).filter(Boolean).map((c) => (c === "UK" ? "GB" : c));
  return list.includes("*") ? null : new Set(list);
}

// IPv4 -> its /24, IPv6 -> its /64: the unit registrations are counted in.
export function netOf(ip) {
  if (!ip) return "unknown";
  if (ip.includes(".") && !ip.includes(":")) return ip.split(".").slice(0, 3).join(".");
  const [head, tail = ""] = ip.toLowerCase().split("::");
  const h = head ? head.split(":") : [];
  const t = tail ? tail.split(":") : [];
  const full = [...h, ...Array(Math.max(0, 8 - h.length - t.length)).fill("0"), ...t];
  return full.slice(0, 4).map((x) => x.replace(/^0+(?=.)/, "")).join(":");
}

async function sha256(s) { return new Uint8Array(await crypto.subtle.digest("SHA-256", enc.encode(s))); }

function leadingZeroBits(bytes) {
  let bits = 0;
  for (const b of bytes) {
    if (b === 0) { bits += 8; continue; }
    return bits + Math.clz32(b) - 24;
  }
  return bits;
}

// The register proof of work: SHA-256 of this string must start with powBits zero bits.
export const powString = (day, installId, nonce) => `auros-register-v1:${day}:${installId}:${nonce}`;

async function readJson(request) {
  const declared = Number(request.headers.get("content-length") || 0);
  if (declared > MAX_BODY) return undefined;
  if (!request.body) return undefined;
  const reader = request.body.getReader();
  const chunks = [];
  let size = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.byteLength;
    if (size > MAX_BODY) { await reader.cancel(); return undefined; }
    chunks.push(value);
  }
  const all = new Uint8Array(size);
  let at = 0;
  for (const c of chunks) { all.set(c, at); at += c.byteLength; }
  try { return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(all)); } catch { return undefined; }
}

export function createApp({ env, ledger, anthropic, buildApi = null, log = (o) => console.log(JSON.stringify(o)), now = Date.now }) {
  const cfg = config(env);

  // The Durable Object is the one thing every wish waits on. When the platform
  // has trouble with it (a Cloudflare incident in Ashburn hung it for 12-30 s on
  // 2026-10-24) a request must not hang with it: give up after ledgerMs with a
  // clean 503 that says why in the log (crash:LedgerTimeout), well inside the
  // computer's 20 s patience.
  const ledgerCall = (what, p) => {
    let timer;
    const limit = new Promise((_, reject) => {
      timer = setTimeout(() => reject(Object.assign(new Error(`${what} took longer than ${cfg.ledgerMs} ms`), { name: "LedgerTimeout" })), cfg.ledgerMs);
    });
    const call = Promise.resolve(p);
    call.catch(() => {}); // a call that fails after we gave up is not an unhandled rejection
    return Promise.race([call, limit]).finally(() => clearTimeout(timer));
  };
  let keyPromise;
  const tokenKey = () => (keyPromise ??= crypto.subtle.importKey("raw", enc.encode(env.TOKEN_SECRET), { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]));
  const sign = async (s) => new Uint8Array(await crypto.subtle.sign("HMAC", await tokenKey(), enc.encode(s)));

  // Token = <tid>.<sig>. tid is derived from her install_id, so registering
  // again (every 90 days, silently) keeps her quota and her "established" mark;
  // the Ledger holds whether the token is alive, so /forget really ends it.
  async function tokenOf(request) {
    const m = /^Bearer ([A-Za-z0-9_-]{22})\.([A-Za-z0-9_-]{43})$/.exec(request.headers.get("authorization") || "");
    if (!m) return null;
    const ok = await crypto.subtle.verify("HMAC", await tokenKey(), unb64(m[2]), enc.encode("token:" + m[1]));
    return ok ? m[1] : null;
  }
  const tagOf = async (tid) => [...(await sha256("tag:" + tid)).slice(0, 6)].map((b) => b.toString(16).padStart(2, "0")).join("");

  // Wishes is offered only in the allowed countries. Register, wish, ask and
  // configure are refused elsewhere; forget and health are not (anyone may
  // delete her data).
  function inRegion(request) {
    if (cfg.countries === null) return true;
    // Only the edge's own value: a header a client could send is never read.
    // No value (not running on Cloudflare) fails closed.
    const c = typeof request.cf?.country === "string" ? request.cf.country.toUpperCase() : "";
    return c !== "" && cfg.countries.has(c);
  }

  // The kill switch (docs/CLOUD.md section 0): a daily budget of 0 answers every
  // wish, ask and configure request with the plain 503, before any body is read,
  // any counter is touched or Claude is called. Register, forget and health
  // still work.
  const off = () => !(cfg.budgetUsd > 0);

  async function register(request, m) {
    const body = await readJson(request);
    if (!checkRequest("register", body)) return json(400, { error: "request" });
    const t = now();
    const days = [dayOf(t - DAY_MS), dayOf(t), dayOf(t + DAY_MS)];
    const { day, nonce } = body.pow;
    if (!days.includes(day) || leadingZeroBits(await sha256(powString(day, body.install_id, nonce))) < cfg.powBits) {
      return json(400, { error: "pow", day: dayOf(t), bits: cfg.powBits });
    }
    const tid = b64(await sign("install:" + body.install_id)).slice(0, 22);
    const r = await ledgerCall("register", ledger.register({ tid, net: netOf(request.headers.get("cf-connecting-ip")), now: t }));
    if (!r.ok) {
      m.limited = "register";
      const retry = secondsToMidnight(t);
      return json(429, { retry_after: retry, text: TEXT.limit }, { "retry-after": String(retry) });
    }
    m.consent = body.consent_version;
    return json(200, { token: `${tid}.${b64(await sign("token:" + tid))}`, expires_at: new Date(r.exp).toISOString() });
  }

  async function callClaude(route, system, user, schema, m) {
    const msg = await anthropic.messages.create({
      model: MODEL,
      max_tokens: MAX_TOKENS[route],
      thinking: { type: "disabled" },
      system: [{ type: "text", text: system, cache_control: { type: "ephemeral" } }],
      messages: [{ role: "user", content: user }],
      output_config: { format: { type: "json_schema", schema } },
    }, { signal: AbortSignal.timeout(DEADLINE_MS) });
    const u = msg.usage || {};
    m.in = u.input_tokens; m.out = u.output_tokens; m.cr = u.cache_read_input_tokens; m.cw = u.cache_creation_input_tokens;
    m.stop = msg.stop_reason;
    const usd = ((u.input_tokens || 0) * PRICE.in + (u.cache_creation_input_tokens || 0) * PRICE.write +
      (u.cache_read_input_tokens || 0) * PRICE.read + (u.output_tokens || 0) * PRICE.out) / 1e6;
    let out = null;
    if (msg.stop_reason === "end_turn") {
      try { out = JSON.parse(msg.content.filter((b) => b.type === "text").at(-1)?.text); } catch { m.bad = "json"; }
    }
    return { out, usd };
  }

  // Token liveness, quota, budget, pool and slot. Even a cheap local refusal
  // goes through this admission so forgotten tokens and request floods do not
  // bypass the service's controls.
  async function admitted(tid, route, estUsd, m) {
    const t = now();
    const a = await ledgerCall("admit", ledger.admit({ tid, route, estUsd, now: t }));
    if (a.status === 401) return { res: json(401, { error: "token" }) };
    if (a.status === 429) {
      m.limited = "quota";
      m.tag = await tagOf(tid); // a pseudonym, so the owner can ban a token that hits its quota every day
      const retry = secondsToMidnight(t);
      return { res: json(429, { retry_after: retry, text: TEXT.limit }, { "retry-after": String(retry) }) };
    }
    if (a.status === 503) { m.reason = a.reason; return { res: busy(m, a.reason === "budget" ? secondsToMidnight(t) : undefined) }; }
    m.pool = a.pool;
    const settle = async (answered, usd, gotWish) => {
      m.usd = +usd.toFixed(6);
      let s;
      try {
        s = await ledgerCall("settle", ledger.settle({ tid, lease: a.lease, qk: a.qk, sk: a.sk, estUsd, usd, answered, gotWish, now: now() }));
      } catch (e) {
        // Claude has answered and been paid: bookkeeping trouble must not lose her
        // answer. The estimate stays reserved (the budget errs on the safe side)
        // and the slot frees itself after 60 s.
        m.settle = "failed";
        m.detail = String(e?.message ?? "").replace(/[^\x20-\x7e]/g, " ").slice(0, 160);
        return;
      }
      if (s.alert) {
        log({ ev: "budget_half", spent_usd: +s.total.toFixed(2), budget_usd: cfg.budgetUsd });
        if (env.ALERT_URL) await fetch(env.ALERT_URL, { method: "POST", body: `AurOS Wishes has used half of today's budget ($${s.total.toFixed(2)} of $${cfg.budgetUsd}).` }).catch(() => {}); // once a day
      }
    };
    return { settle, t };
  }

  // Admission, then Claude. Returns {res} (a ready error) or {out, settle}:
  // settle(gotWish) must be called once.
  async function guarded(tid, route, system, user, schema, m) {
    if (!env.ANTHROPIC_API_KEY) { m.reason = "no_key"; return { res: busy(m) }; }
    const estUsd = ((system.length / 3 + user.length) * PRICE.in + MAX_TOKENS[route] * PRICE.out) / 1e6;
    if (estUsd > MAX_COST_USD) { m.reason = "cost"; return { res: busy(m) }; }
    const a = await admitted(tid, route, estUsd, m);
    if (a.res) return a;
    let r;
    try {
      r = await callClaude(route, system, user, schema, m);
    } catch (e) {
      m.reason = upstreamReason(e);
      // What the owner must fix (a refused key, a bad request): Anthropic's own
      // message, which describes the request's shape, never its content.
      if (["auth", "bad_request", "not_found", "error"].includes(m.reason)) m.detail = `${e?.status ?? ""} ${e?.error?.error?.type ?? e?.name ?? ""} ${String(e?.error?.error?.message ?? e?.message ?? "").slice(0, 200)}`.trim();
      await a.settle(false, 0, false); // Claude never answered: the slot, the estimate and her request come back
      return { res: busy(m, m.reason === "spend_limit" ? secondsToMidnight(a.t) : undefined) };
    }
    return { out: r.out, settle: (gotWish) => a.settle(true, r.usd, gotWish) };
  }

  function busy(m, retry = 30 + Math.floor(Math.random() * 30)) {
    m.resting = true;
    return json(503, { retry_after: retry, text: TEXT.resting }, { "retry-after": String(retry) });
  }

  // The token is checked before the body is read: a stranger learns nothing
  // and costs nothing.
  async function authed(request) {
    const tid = await tokenOf(request);
    return tid && !(cfg.banned.size && cfg.banned.has(await tagOf(tid))) ? tid : null;
  }

  async function wish(request, m) {
    const tid = await authed(request);
    if (!tid) return json(401, { error: "token" });
    if (off()) { m.reason = "off"; return busy(m, 3600); }
    const body = await readJson(request);
    if (!checkRequest("wish", body)) return json(400, { error: body && typeof body.text === "string" && [...body.text].length > MAX_TEXT ? "too_long" : "request" });
    const g = await guarded(tid, "wish", WISH_SYSTEM, wishMessage(body), WISH_OUTPUT, m);
    if (g.res) return g.res;
    const allowed = { triggers: body.schema.triggers, actions: body.schema.actions, sensors: body.sensors };
    const o = g.out;
    const w = o ? parseWish(o.wish_json) : null;
    const bad = w ? checkWish(w, allowed) : o && o.wish_json ? "json" : null;
    if (bad) m.rejected = bad;
    let res;
    if (o && o.kind === "wish" && w && !bad) res = { kind: "wish", wish: w };
    else if (o && o.kind === "wish") res = { kind: "cannot", missing: bad === "sensor" && ["compass", "tilt", "light"].includes(w?.when?.sensor) ? w.when.sensor : "other" };
    else {
      res = { kind: "cannot", missing: o && MISSING.includes(o.missing) ? o.missing : "other" };
      if (w && !bad) res.alt_wish = w;
    }
    m.kind = res.kind;
    m.missing = res.missing;
    await g.settle(res.kind === "wish");
    return json(200, res);
  }

  async function ask(request, m) {
    const tid = await authed(request);
    if (!tid) return json(401, { error: "token" });
    if (off()) { m.reason = "off"; return busy(m, 3600); }
    const body = await readJson(request);
    if (!checkRequest("ask", body)) return json(400, { error: body && typeof body.question === "string" && [...body.question].length > MAX_TEXT ? "too_long" : "request" });
    const g = await guarded(tid, "ask", ASK_SYSTEM, askMessage(body), ASK_OUTPUT, m);
    if (g.res) return g.res;
    const o = g.out;
    const res = { help_page_id: o && typeof o.help_page_id === "string" && o.help_page_id !== "none" && ASK_OUTPUT.properties.help_page_id.enum.includes(o.help_page_id) ? o.help_page_id : null };
    const w = o ? parseWish(o.wish_json) : null;
    if (w) {
      const bad = checkWish(w, null);
      if (bad) m.rejected = bad; else res.wish = w;
    }
    m.help = res.help_page_id;
    await g.settle(false);
    return json(200, res);
  }

  async function configurePlan(request, m) {
    const tid = await authed(request);
    if (!tid) return json(401, { error: "token" });
    if (off()) { m.reason = "off"; return busy(m, 3600); }
    const body = await readJson(request);
    if (!checkRequest("configure", body)) {
      return json(400, { error: body && typeof body.text === "string" && [...body.text].length > CONFIGURE_MAX_TEXT ? "too_long" : "request" });
    }
    const analysis = analyzeConfigureRequest(body.text);
    const a = await admitted(tid, "configure", 0, m);
    if (a.res) return a.res;
    const plan = analysis.reason ? unsupportedPlan(analysis.reason) : planFromSelection(analysis.expected);
    m.kind = plan.status;
    m.missing = plan.reason;
    await a.settle(true, 0, false);
    return json(plan.status === "unsupported" ? 422 : 200, plan);
  }

  async function forget(request) {
    const tid = await authed(request);
    if (!tid) return json(401, { error: "token" });
    await ledgerCall("forget", ledger.forget({ tid, now: now() }));
    return json(204, null);
  }

  return async function fetchHandler(request) {
    const t0 = now();
    const path = new URL(request.url).pathname;
    const m = { ...smokeLogIdentity(env, request), ev: "req", route: path.startsWith("/v1/builds/") && path !== "/v1/builds/challenge" ? "/v1/builds/:id" : path };
    let res;
    try {
      if (buildApi && (path === "/v1/builds" || path.startsWith("/v1/builds/"))) res = await buildApi(request);
      else if (!env.TOKEN_SECRET) { m.reason = "no_secret"; res = busy(m); }
      else if (path === "/v1/health" && request.method === "GET") res = json(200, { ok: true, wishes: off() ? "resting" : "on", ...(deploymentIdentity(env) ? { deployment: deploymentIdentity(env) } : {}) });
      else if (request.method !== "POST") res = json(path.startsWith("/v1/") ? 405 : 404, { error: "method" });
      else if (["/v1/register", "/v1/wish", "/v1/ask", "/v1/configure/plan"].includes(path) && !inRegion(request)) {
        m.limited = "region"; // the country itself is not logged
        res = json(403, { error: "region", text: "Wishes is not available in your country yet." });
      }
      else if (path === "/v1/register") res = await register(request, m);
      else if (path === "/v1/wish") res = await wish(request, m);
      else if (path === "/v1/ask") res = await ask(request, m);
      else if (path === "/v1/configure/plan") res = await configurePlan(request, m);
      else if (path === "/v1/forget") res = await forget(request);
      else res = json(404, { error: "path" });
    } catch (e) {
      m.reason = "crash:" + (e && e.name);
      // Platform and storage errors say what broke ("Durable Object storage operation
      // exceeded timeout", "reset because its code was updated"); none carries her words.
      m.detail = `${e?.remote ? "remote " : ""}${e?.overloaded ? "overloaded " : ""}${e?.retryable ? "retryable " : ""}${String(e?.message ?? "").replace(/[^\x20-\x7e]/g, " ").slice(0, 160)}`.trim();
      res = busy(m);
    }
    m.status = res.status;
    m.ms = now() - t0;
    log(m); // Counts plus matching CI correlation only; never her words, auth token, install id or address.
    return res;
  };
}

// Upstream failures, sorted by what the owner has to do about them (CLOUD.md runbook).
function upstreamReason(e) {
  if (e instanceof Anthropic.APIUserAbortError || e?.name === "TimeoutError" || e?.name === "AbortError") return "timeout";
  if (e instanceof Anthropic.APIConnectionTimeoutError) return "timeout";
  if (e instanceof Anthropic.APIConnectionError) return "network";
  if (e instanceof Anthropic.RateLimitError) {
    return e.error?.error?.details?.error_code === "enforced_spend_limit_reached" ? "spend_limit" : "rate_limit";
  }
  if (e instanceof Anthropic.BadRequestError) {
    return /usage limits/i.test(e.message) ? "spend_limit" : "bad_request";
  }
  if (e instanceof Anthropic.AuthenticationError || e instanceof Anthropic.PermissionDeniedError) return "auth";
  if (e instanceof Anthropic.NotFoundError) return "not_found";
  if (e instanceof Anthropic.APIError && e.status === 529) return "overloaded";
  if (e instanceof Anthropic.InternalServerError) return "upstream_5xx";
  return "error";
}
