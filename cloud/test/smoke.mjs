// End to end against a deployed service, the way the engine on her computer
// talks to it:  node test/smoke.mjs https://auros-wishes.<sub>.workers.dev
// Prints what it checked (never the token). With SMOKE_SECRETS_FILE set,
// writes the strings that must never appear in the service's logs there
// (one per line), for the CI log check.
import { createHash, randomBytes } from "node:crypto";
import { writeFileSync } from "node:fs";
import { powString } from "../src/app.js";
import { ALL } from "./helpers.js";

const base = (process.argv[2] || "").replace(/\/$/, "");
if (!/^https:\/\/|^http:\/\/127\.0\.0\.1:/.test(base)) { console.error("usage: node test/smoke.mjs https://<service>"); process.exit(2); }

let failed = 0;
const check = (ok, what, detail = "") => { console.log(`${ok ? "ok  " : "FAIL"} ${what}${detail ? " — " + detail : ""}`); if (!ok) failed++; };
const post = async (path, body, token) => {
  const t = Date.now();
  const r = await fetch(base + path, { method: "POST", headers: { "content-type": "application/json", ...(token ? { authorization: "Bearer " + token } : {}) }, body: JSON.stringify(body) });
  const text = await r.text();
  return { status: r.status, body: text ? JSON.parse(text) : null, ms: Date.now() - t };
};
const wish = (text, compass) => ({ text, lang: "en", schema: { v: 1, ...ALL }, sensors: { compass, tilt: false, light: false } });

const POTATO = "every time I type potato, fill my screen with potatoes";
const NORTH = "when my laptop points north, make the screen brighter";
const PRINT = "How do I print a letter?";

const health = await (await fetch(base + "/v1/health")).json().catch(() => ({}));
const expect = process.env.EXPECT || health.wishes; // "off" = resting (the kill switch), "on" = answering
const resting = expect === "off" || expect === "resting";
check(health.ok === true && health.wishes === (resting ? "resting" : "on"), "health", `wishes: ${health.wishes}, expected ${resting ? "resting" : "on"}`);
check((await post("/v1/wish", wish(POTATO, true))).status === 401, "a wish without a token is refused (401)");
check((await post("/v1/ask", { question: PRINT }, "A".repeat(22) + "." + "B".repeat(43))).status === 401, "a forged token is refused (401)");

const installId = randomBytes(16).toString("base64url");
const first = await post("/v1/register", { install_id: installId, version: "smoke", consent_version: "1", pow: { day: "2000-01-01", nonce: "0" } });
check(first.status === 400 && first.body.error === "pow", "register without proof of work is refused", `server day ${first.body?.day}, ${first.body?.bits} bits`);
const t0 = Date.now();
let nonce = 0;
for (;; nonce++) {
  const h = createHash("sha256").update(powString(first.body.day, installId, String(nonce))).digest();
  let z = 0;
  for (const b of h) { if (b === 0) { z += 8; continue; } z += Math.clz32(b) - 24; break; }
  if (z >= first.body.bits) break;
}
console.log(`     proof of work: ${nonce + 1} hashes, ${Date.now() - t0} ms on this machine`);
// A platform blip (the ledger is a Durable Object) answers 503 once in a while: try again twice, 5 s apart.
let reg;
for (let attempt = 1; attempt <= 3; attempt++) {
  reg = await post("/v1/register", { install_id: installId, version: "smoke", consent_version: "1", pow: { day: first.body.day, nonce: String(nonce) } });
  if (reg.status !== 503) break;
  console.log(`note: register answered 503 (attempt ${attempt}); ${attempt < 3 ? "trying again in 5 s" : "giving up"}`);
  if (attempt < 3) await new Promise((r) => setTimeout(r, 5000));
}
if (reg.status === 429) {
  console.log("note: this network has used its 3 registrations today; end-to-end part skipped");
  process.exit(failed ? 1 : 3);
}
check(reg.status === 200 && /^[\w-]{22}\.[\w-]{43}$/.test(reg.body.token), "register", `${reg.ms} ms, expires ${reg.body?.expires_at}`);
const token = reg.body.token;
if (process.env.SMOKE_SECRETS_FILE) writeFileSync(process.env.SMOKE_SECRETS_FILE, [token, token.split(".")[0], token.split(".")[1], installId, "potatoes", "points north", "print a letter"].join("\n") + "\n");

let r;
if (resting) {
  // The kill switch: a registered token's wish and question get the plain 503, no Claude behind it.
  r = await post("/v1/wish", wish(POTATO, true), token);
  check(r.status === 503 && r.body?.text === "Wishes are resting for a little while" && r.body.retry_after > 0, "resting: a registered token's wish -> 503", `${r.ms} ms ${JSON.stringify(r.body)}`);
  r = await post("/v1/ask", { question: PRINT, lang: "en" }, token);
  check(r.status === 503 && r.body?.text === "Wishes are resting for a little while", "resting: a registered token's question -> 503", `${r.ms} ms ${JSON.stringify(r.body)}`);
} else {
  r = await post("/v1/wish", wish(POTATO, true), token);
  const p = r.body?.wish;
  check(r.status === 200 && r.body.kind === "wish" && p.when.type === "typed_word" && /^potato$/i.test(p.when.word) && p.do.some((a) => a.type === "fill_screen" && a.emoji === "🥔"),
    "potato: typed_word potato -> fill_screen 🥔", `${r.ms} ms ${JSON.stringify(r.body)}`);
  r = await post("/v1/wish", wish(NORTH, true), token);
  const n = r.body?.wish;
  check(r.status === 200 && r.body.kind === "wish" && n.when.type === "sensor" && n.when.sensor === "compass" && n.when.heading === "north" && n.do.some((a) => a.type === "brightness"),
    "compass: sensor compass north -> brightness", `${r.ms} ms ${JSON.stringify(r.body)}`);
  r = await post("/v1/wish", wish(NORTH, false), token);
  check(r.status === 200 && r.body.kind === "cannot" && r.body.missing === "compass", "no compass: cannot/compass", `${r.ms} ms ${JSON.stringify(r.body)}`);
  r = await post("/v1/wish", wish(POTATO, true), token);
  check(r.status === 429 && r.body.text === "You've made lots of wishes today — more tomorrow", "a new token's 4th wish today: 429", JSON.stringify(r.body));
  r = await post("/v1/ask", { question: PRINT, lang: "en" }, token);
  check(r.status === 200 && r.body.help_page_id === "printing", "ask: printing help page", `${r.ms} ms ${JSON.stringify(r.body)}`);
}
r = await post("/v1/forget", {}, token);
check(r.status === 204, "forget");
r = await post("/v1/wish", wish(POTATO, true), token);
// While resting, liveness is not looked up (nothing is touched), so a forgotten token gets the same 503.
check(r.status === (resting ? 503 : 401), resting ? "after forget, still the plain 503 (nothing is looked up while resting)" : "after forget the token is gone (401)");

console.log(failed ? `${failed} check(s) failed` : "all checks passed");
process.exit(failed ? 1 : 0);
