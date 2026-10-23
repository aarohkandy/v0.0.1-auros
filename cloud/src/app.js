// The Wishes service (docs/AI.md section 5 as amended by section 7): a plain
// fetch handler, so the same code runs on Cloudflare Workers (src/worker.js)
// and under Node (tests, dev-server.mjs).
import Anthropic from "@anthropic-ai/sdk";
import { ASK_OUTPUT, MAX_TEXT, MISSING, WISH_OUTPUT, checkRequest, checkWish } from "./validate.js";
import { ASK_SYSTEM, WISH_SYSTEM, askMessage, wishMessage } from "./prompts.js";
import { DAY_MS, dayOf } from "./ledger.js";

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
    powBits: n("POW_BITS", 21),
    banned: new Set(String(env.BANNED || "").split(/[\s,]+/).filter(Boolean)),
  };
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

export function createApp({ env, ledger, anthropic, log = (o) => console.log(JSON.stringify(o)), now = Date.now }) {
  const cfg = config(env);
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
    const r = await ledger.register({ tid, net: netOf(request.headers.get("cf-connecting-ip")), now: t });
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

  // Token, cost estimate, quota, pool and slot; then Claude. Returns {res}
  // (a ready error) or {out, settle}: settle(gotWish) must be called once.
  async function guarded(tid, route, system, user, schema, m) {
    if (!env.ANTHROPIC_API_KEY) { m.reason = "no_key"; return { res: busy(m) }; }
    const estUsd = ((system.length / 3 + user.length) * PRICE.in + MAX_TOKENS[route] * PRICE.out) / 1e6;
    if (estUsd > MAX_COST_USD) { m.reason = "cost"; return { res: busy(m) }; }
    const t = now();
    const a = await ledger.admit({ tid, route, estUsd, now: t });
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
      const s = await ledger.settle({ tid, lease: a.lease, qk: a.qk, sk: a.sk, estUsd, usd, answered, gotWish, now: now() });
      m.usd = +usd.toFixed(6);
      if (s.alert) {
        log({ ev: "budget_half", spent_usd: +s.total.toFixed(2), budget_usd: cfg.budgetUsd });
        if (env.ALERT_URL) await fetch(env.ALERT_URL, { method: "POST", body: `AurOS Wishes has used half of today's budget ($${s.total.toFixed(2)} of $${cfg.budgetUsd}).` }).catch(() => {}); // once a day
      }
    };
    let r;
    try {
      r = await callClaude(route, system, user, schema, m);
    } catch (e) {
      m.reason = upstreamReason(e);
      // What the owner must fix (a refused key, a bad request): Anthropic's own
      // message, which describes the request's shape, never its content.
      if (["auth", "bad_request", "not_found", "error"].includes(m.reason)) m.detail = `${e?.status ?? ""} ${e?.error?.error?.type ?? e?.name ?? ""} ${String(e?.error?.error?.message ?? e?.message ?? "").slice(0, 200)}`.trim();
      await settle(false, 0, false); // Claude never answered: the slot, the estimate and her request come back
      return { res: busy(m, m.reason === "spend_limit" ? secondsToMidnight(t) : undefined) };
    }
    return { out: r.out, settle: (gotWish) => settle(true, r.usd, gotWish) };
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
    const body = await readJson(request);
    if (!checkRequest("wish", body)) return json(400, { error: body && typeof body.text === "string" && [...body.text].length > MAX_TEXT ? "too_long" : "request" });
    const g = await guarded(tid, "wish", WISH_SYSTEM, wishMessage(body), WISH_OUTPUT, m);
    if (g.res) return g.res;
    const allowed = { triggers: body.schema.triggers, actions: body.schema.actions, sensors: body.sensors };
    const o = g.out;
    let res;
    if (o && o.kind === "wish" && o.wish) {
      const bad = checkWish(o.wish, allowed);
      if (!bad) res = { kind: "wish", wish: o.wish };
      else { m.rejected = bad; res = { kind: "cannot", missing: bad === "sensor" ? o.wish.when.sensor : "other" }; }
    } else {
      const missing = o && MISSING.includes(o.missing) ? o.missing : "other";
      res = { kind: "cannot", missing };
      if (o && o.wish) {
        const bad = checkWish(o.wish, allowed);
        if (bad) m.rejected = "alt_" + bad; else res.alt_wish = o.wish;
      }
    }
    m.kind = res.kind;
    m.missing = res.missing;
    await g.settle(res.kind === "wish");
    return json(200, res);
  }

  async function ask(request, m) {
    const tid = await authed(request);
    if (!tid) return json(401, { error: "token" });
    const body = await readJson(request);
    if (!checkRequest("ask", body)) return json(400, { error: body && typeof body.question === "string" && [...body.question].length > MAX_TEXT ? "too_long" : "request" });
    const g = await guarded(tid, "ask", ASK_SYSTEM, askMessage(body), ASK_OUTPUT, m);
    if (g.res) return g.res;
    const o = g.out;
    const res = { help_page_id: o && typeof o.help_page_id === "string" && o.help_page_id !== "none" && ASK_OUTPUT.properties.help_page_id.enum.includes(o.help_page_id) ? o.help_page_id : null };
    if (o && o.wish) {
      const bad = checkWish(o.wish, null);
      if (bad) m.rejected = bad; else res.wish = o.wish;
    }
    m.help = res.help_page_id;
    await g.settle(false);
    return json(200, res);
  }

  async function forget(request) {
    const tid = await authed(request);
    if (!tid) return json(401, { error: "token" });
    await ledger.forget({ tid, now: now() });
    return json(204, null);
  }

  return async function fetchHandler(request) {
    const t0 = now();
    const path = new URL(request.url).pathname;
    const m = { ev: "req", route: path };
    let res;
    try {
      if (!env.TOKEN_SECRET) { m.reason = "no_secret"; res = busy(m); }
      else if (path === "/v1/health" && request.method === "GET") res = json(200, { ok: true });
      else if (request.method !== "POST") res = json(path.startsWith("/v1/") ? 405 : 404, { error: "method" });
      else if (path === "/v1/register") res = await register(request, m);
      else if (path === "/v1/wish") res = await wish(request, m);
      else if (path === "/v1/ask") res = await ask(request, m);
      else if (path === "/v1/forget") res = await forget(request);
      else res = json(404, { error: "path" });
    } catch (e) {
      m.reason = "crash:" + (e && e.name);
      res = busy(m);
    }
    m.status = res.status;
    m.ms = now() - t0;
    log(m); // counts, latency, tokens and cost only: never her words, a token, an id or an address
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
