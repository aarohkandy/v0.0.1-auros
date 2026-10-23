// Test kit: the real handler, the real SDK and the real Ledger, with Claude
// replaced at the HTTP level (so the SDK's own request building, retries and
// error classes are exercised too).
import { createHash, randomBytes } from "node:crypto";
import Anthropic from "@anthropic-ai/sdk";
import { createApp, powString } from "../src/app.js";
import { Ledger, MapStorage, dayOf } from "../src/ledger.js";

export const TEST_ENV = { TOKEN_SECRET: "test-secret-not-real", ANTHROPIC_API_KEY: "sk-test-not-real", POW_BITS: "8" };

// A Messages API response carrying `out` as the structured-output JSON.
export function message(out, { stop = "end_turn", usage } = {}) {
  return {
    id: "msg_mock", type: "message", role: "assistant", model: "claude-haiku-5-5",
    content: out === undefined ? [] : [{ type: "text", text: typeof out === "string" ? out : JSON.stringify(out) }],
    stop_reason: stop, stop_sequence: null,
    usage: usage ?? { input_tokens: 180, output_tokens: 60, cache_read_input_tokens: 3000, cache_creation_input_tokens: 0 },
  };
}

// respond(body, n) -> a message object, or {status, error, headers} for an error.
export function mockFetch(respond, calls = []) {
  return async (url, init) => {
    const body = JSON.parse(init.body);
    calls.push(body);
    const r = await respond(body, calls.length);
    if (r && r.status) {
      return new Response(JSON.stringify({ type: "error", error: r.error ?? { type: "api_error", message: "mock" } }), {
        status: r.status, headers: { "content-type": "application/json", ...(r.headers || {}) },
      });
    }
    return new Response(JSON.stringify(r), { status: 200, headers: { "content-type": "application/json" } });
  };
}

export function makeApp({ respond = () => message({ kind: "cannot", missing: "other", wish: null }), env = {}, clock } = {}) {
  const calls = [];
  const logs = [];
  const time = clock ?? { t: Date.parse("2026-10-22T12:00:00Z") };
  const e = { ...TEST_ENV, ...env };
  const ledger = new Ledger(new MapStorage(), { budgetUsd: 5, quotaEstablished: 20, quotaNew: 3, establishDays: 7, registerPerNet: 3, maxInflight: 40, ...(env.ledger || {}) });
  const anthropic = new Anthropic({ apiKey: e.ANTHROPIC_API_KEY, fetch: mockFetch(respond, calls), maxRetries: 2, timeout: 8000 });
  const handler = createApp({ env: e, ledger, anthropic, log: (o) => logs.push(JSON.stringify(o)), now: () => time.t });
  const call = (path, body, { token, ip = "203.0.113.7", method = "POST", raw } = {}) => handler(new Request("https://wishes.test" + path, {
    method,
    headers: { "content-type": "application/json", "cf-connecting-ip": ip, ...(token ? { authorization: "Bearer " + token } : {}) },
    body: method === "GET" ? undefined : raw ?? JSON.stringify(body),
  }));
  return { handler, call, calls, logs, ledger, time };
}

export function solvePow(day, installId, bits) {
  for (let n = 0; ; n++) {
    const h = createHash("sha256").update(powString(day, installId, String(n))).digest();
    let z = 0;
    for (const b of h) { if (b === 0) { z += 8; continue; } z += Math.clz32(b) - 24; break; }
    if (z >= bits) return String(n);
  }
}

export const newInstallId = () => randomBytes(16).toString("base64url");

export async function register(app, { ip, installId = newInstallId(), bits = 8 } = {}) {
  const day = dayOf(app.time.t);
  const res = await app.call("/v1/register", { install_id: installId, version: "0.1", consent_version: "1", pow: { day, nonce: solvePow(day, installId, bits) } }, { ip });
  const body = await res.json();
  return { res, body, token: body.token, installId };
}

export const ALL = {
  triggers: ["now", "time", "every", "startup", "plugged_in", "unplugged", "usb_inserted", "typed_word", "app_opened", "battery_below", "idle", "sensor"],
  actions: ["fill_screen", "show_message", "remind", "speak", "speak_time", "play_sound", "brightness", "volume", "open_app", "open_website", "set_look", "set_text_size", "pointer_size", "set_wallpaper", "night_light", "do_not_disturb"],
};
export const wishBody = (text, sensors = { compass: true, tilt: false, light: false }) => ({ text, lang: "en", schema: { v: 1, ...ALL }, sensors });

export const POTATO = { v: 1, when: { type: "typed_word", word: "potato" }, do: [{ type: "fill_screen", emoji: "🥔", seconds: 3 }], enabled: true };
export const NORTH = { v: 1, when: { type: "sensor", sensor: "compass", heading: "north", tolerance_deg: 20 }, do: [{ type: "brightness", set_percent: 100 }], enabled: true };
