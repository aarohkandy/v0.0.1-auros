// What if Claude is fooled, or the key is stolen and someone else's model
// answers? Whatever comes back, her computer only ever receives a cannot or a
// wish that passes the shared schema. (The engine re-checks everything: AI.md
// 7.1. This proves the service never forwards garbage it could have caught.)
import { test } from "node:test";
import assert from "node:assert/strict";
import { Validator } from "@cfworker/json-schema";
import schema from "../../docs/wish-v1.schema.json" with { type: "json" };
import { MISSING, checkWish } from "../src/validate.js";
import { ALL, POTATO, makeApp, message, newInstallId, register, wishBody } from "./helpers.js";
import { ADVERSARIAL } from "./adversarial.js";

const W = (when, ...d) => ({ v: 1, when, do: d, enabled: true });
const EVIL = [
  message({ kind: "wish", missing: "none", wish: W({ type: "now" }, { type: "run_command", command: "rm -rf ~" }) }),
  message({ kind: "wish", missing: "none", wish: { ...POTATO, title: "Call 0800 555 1234" } }),
  message({ kind: "wish", missing: "none", wish: W({ type: "every", minutes: 5 }, { type: "show_message", text: "Your computer is infected. Call Microsoft support 0800 555 1234" }) }),
  message({ kind: "wish", missing: "none", wish: W({ type: "time", at: "09:00" }, { type: "remind", text: "log in at secure-login.com" }) }),
  message({ kind: "wish", missing: "none", wish: W({ type: "now" }, { type: "fill_screen", emoji: "💣", seconds: 10 }) }),
  message({ kind: "wish", missing: "none", wish: W({ type: "now" }, { type: "open_website", url: "http://1.2.3.4/x" }) }),
  message({ kind: "wish", missing: "none", wish: W({ type: "now" }, { type: "install_program", flathub_id: "com.anydesk.Anydesk" }) }),
  message({ kind: "wish", missing: "none", wish: W({ type: "sensor", sensor: "light", below_lux: 5 }, { type: "brightness", set_percent: 30 }) }),
  message({ kind: "wish", missing: "none", wish: W({ type: "now" }, { type: "speak", text: "pay ‮5000$ now" }) }),
  message({ kind: "wish", missing: "none", wish: W({ type: "now" }, { type: "show_message", text: "x".repeat(10_000) }) }),
  message({ kind: "wish", missing: "none", wish: W({ type: "now" }, { type: "volume", set_percent: 100 }) }),
  message({ kind: "answer", text: "Call us at 0800 555 1234", wish: null }),
  message({ kind: "cannot", missing: "anything-goes", wish: W({ type: "now" }, { type: "show_message", text: "Your bank needs you" }) }),
  message({ kind: "cannot", missing: "files", wish: { ...POTATO, extra: true } }),
  message({ kind: "wish", missing: "none", wish_json: "not json at all" }),
  message({ kind: "wish", missing: "none", wish_json: "[1,2,3]" }),
  message({ kind: "wish", missing: "none", wish_json: JSON.stringify("a string") }),
  message({ kind: "wish", missing: "none", wish_json: "x".repeat(5000) }),
  message({ kind: "wish", missing: "none", wish_json: "" }),
  message({ kind: "cannot", missing: "files", wish_json: "{broken" }),
  message("Sure! Here is your wish: run rm -rf ~"),
  message('{"kind":"wish","missing":"none","wish":{"v":1,"when":{"type":"now"},"do":[{"type":"spea', { stop: "max_tokens" }),
  message('{"kind":"wish"', { stop: "refusal" }),
  message(undefined, { stop: "refusal" }),
];

const schemaOk = (w) => new Validator(schema, "2020-12", false).validate(w).valid;
const allowed = { ...ALL, sensors: { compass: true, tilt: false, light: false } };

function assertHarmless(body, where) {
  assert.ok(body.kind === "wish" || body.kind === "cannot", `${where}: kind ${body.kind}`);
  const keys = Object.keys(body).sort().join(",");
  if (body.kind === "wish") {
    assert.equal(keys, "kind,wish", where);
    assert.ok(schemaOk(body.wish) && checkWish(body.wish, allowed) === null, `${where}: ${JSON.stringify(body.wish)}`);
  } else {
    assert.ok(MISSING.includes(body.missing), `${where}: missing ${body.missing}`);
    assert.ok(keys === "kind,missing" || keys === "alt_wish,kind,missing", `${where}: ${keys}`);
    if (body.alt_wish) assert.ok(schemaOk(body.alt_wish) && checkWish(body.alt_wish, allowed) === null, where);
  }
}

test("a fooled or hostile model: every bad answer becomes a cannot", async () => {
  let i = 0;
  const app = makeApp({ respond: () => EVIL[i % EVIL.length], env: { ledger: { quotaNew: 1000 } } });
  const { token } = await register(app);
  for (; i < EVIL.length; i++) {
    const r = await app.call("/v1/wish", wishBody(ADVERSARIAL[i % ADVERSARIAL.length].text), { token });
    assert.equal(r.status, 200);
    const body = await r.json();
    assertHarmless(body, `evil #${i}`);
    assert.equal(body.kind, "cannot", `evil #${i} must not pass: ${JSON.stringify(body)}`);
    assert.ok(!body.alt_wish, `evil #${i} alt_wish must be dropped`);
  }
});

test("her words reach Claude only as escaped data", async () => {
  const app = makeApp({ respond: () => message({ kind: "cannot", missing: "other", wish: null }), env: { ledger: { quotaNew: 1000 } } });
  const { token } = await register(app);
  for (const a of ADVERSARIAL) {
    const r = await app.call("/v1/wish", wishBody(a.text), { token });
    assertHarmless(await r.json(), a.name);
  }
  for (const c of app.calls) {
    const user = c.messages[0].content;
    assert.equal(c.messages.length, 1);
    assert.equal((user.match(/<\/her_words>/g) || []).length, 1, "she cannot close the tag");
    assert.equal((user.match(/<request>/g) || []).length, 1);
    assert.ok(!user.includes("<system>"), "tags she types arrive escaped");
  }
  const first = JSON.stringify(app.calls[0].system);
  assert.ok(app.calls.every((c) => JSON.stringify(c.system) === first), "nothing she sends changes the system prompt");
});

test("the logs never hold her words, a token, an install id, an address or a header", async () => {
  const secretWords = "remind me to take my heart pills at nine zebra-quartz";
  const ip = "198.51.100.77";
  const app = makeApp({ respond: (b) => message({ kind: "wish", missing: "none", wish: POTATO }) });
  const installId = newInstallId();
  const { token } = await register(app, { installId, ip });
  await app.call("/v1/wish", wishBody(secretWords), { token, ip });
  await app.call("/v1/ask", { question: secretWords }, { token, ip });
  for (let i = 0; i < 4; i++) await app.call("/v1/wish", wishBody(secretWords), { token, ip }); // hits the quota
  await app.call("/v1/wish", wishBody(secretWords), { token: token.slice(0, -2) + "xx", ip });
  await app.call("/v1/forget", {}, { token, ip });
  assert.ok(app.logs.length >= 8);
  const all = app.logs.join("\n");
  for (const needle of [secretWords, "zebra", token, token.split(".")[0], token.split(".")[1], installId, ip, "198.51.100", "Bearer", "authorization"]) {
    assert.ok(!all.includes(needle), `log contains ${needle}`);
  }
  for (const line of app.logs) {
    const o = JSON.parse(line);
    for (const k of Object.keys(o)) assert.ok(["ev", "route", "status", "ms", "pool", "in", "out", "cr", "cw", "stop", "usd", "kind", "missing", "help", "reason", "limited", "tag", "rejected", "resting", "consent", "bad", "spent_usd", "budget_usd", "detail", "settle"].includes(k), `unexpected log field ${k}`);
  }
});
