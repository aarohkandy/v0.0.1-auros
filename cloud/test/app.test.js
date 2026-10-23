import { test } from "node:test";
import assert from "node:assert/strict";
import { ALL, NORTH, POTATO, makeApp, message, newInstallId, register, solvePow, wishBody } from "./helpers.js";
import { netOf } from "../src/app.js";
import { DAY_MS, dayOf } from "../src/ledger.js";

const wishOut = (wish) => message({ kind: "wish", missing: "none", wish });

test("the owner's two examples come back as wishes", async () => {
  const app = makeApp({ respond: (b) => wishOut(b.messages[0].content.includes("potato") ? POTATO : NORTH) });
  const { token } = await register(app);
  let r = await app.call("/v1/wish", wishBody("every time I type potato fill my screen with potatoes"), { token });
  assert.equal(r.status, 200);
  assert.deepEqual(await r.json(), { kind: "wish", wish: POTATO });
  r = await app.call("/v1/wish", wishBody("when my laptop points north make the screen brighter"), { token });
  assert.deepEqual(await r.json(), { kind: "wish", wish: NORTH });
});

test("a compass wish on a computer without a compass is cannot/compass", async () => {
  const app = makeApp({ respond: () => wishOut(NORTH) });
  const { token } = await register(app);
  const r = await app.call("/v1/wish", wishBody("when my laptop points north make the screen brighter", { compass: false }), { token });
  assert.deepEqual(await r.json(), { kind: "cannot", missing: "compass" });
});

test("every endpoint but register needs a valid token", async () => {
  const app = makeApp();
  const forged = "A".repeat(22) + "." + "B".repeat(43);
  for (const path of ["/v1/wish", "/v1/ask", "/v1/forget"]) {
    for (const token of [undefined, forged, "nonsense"]) {
      const r = await app.call(path, path === "/v1/ask" ? { question: "how do I print?" } : wishBody("hello"), { token });
      assert.equal(r.status, 401, `${path} with ${token}`);
    }
  }
  assert.equal(app.calls.length, 0, "Claude is never called without a token");
  for (const raw of ["", "{", "x".repeat(20_000), JSON.stringify({ text: "x".repeat(400) })]) {
    assert.equal((await app.call("/v1/wish", null, { raw })).status, 401, "the token is checked before the body");
  }
});

test("register needs the proof of work, for today", async () => {
  const app = makeApp({ env: { POW_BITS: "12" } });
  const id = newInstallId();
  const day = dayOf(app.time.t);
  let r = await app.call("/v1/register", { install_id: id, consent_version: "1", pow: { day, nonce: "1" } });
  if (r.status === 400) assert.equal((await r.json()).error, "pow"); // nonce 1 is (almost surely) not a proof
  const old = dayOf(app.time.t - 3 * DAY_MS);
  r = await app.call("/v1/register", { install_id: id, consent_version: "1", pow: { day: old, nonce: solvePow(old, id, 12) } });
  assert.equal(r.status, 400);
  assert.deepEqual(await r.json(), { error: "pow", day, bits: 12 });
  r = await app.call("/v1/register", { install_id: id, consent_version: "1", pow: { day, nonce: solvePow(day, id, 12) } });
  assert.equal(r.status, 200);
  assert.match((await r.json()).token, /^[A-Za-z0-9_-]{22}\.[A-Za-z0-9_-]{43}$/);
  r = await app.call("/v1/register", { install_id: id, pow: { day, nonce: "1" } });
  assert.equal(r.status, 400, "consent_version is required");
});

test("register: 3 a day per /24 and per /64, then 429", async () => {
  const app = makeApp();
  for (let i = 0; i < 3; i++) assert.equal((await register(app, { ip: `198.51.100.${i + 1}` })).res.status, 200);
  const { res: r, body: b } = await register(app, { ip: "198.51.100.200" });
  assert.equal(r.status, 429);
  assert.equal(b.text, "You've made lots of wishes today — more tomorrow");
  assert.ok(b.retry_after > 0);
  assert.equal((await register(app, { ip: "198.51.101.1" })).res.status, 200, "another /24 is another network");
  for (let i = 0; i < 3; i++) assert.equal((await register(app, { ip: `2001:db8:1:2::${i + 1}` })).res.status, 200);
  assert.equal((await register(app, { ip: "2001:db8:1:2:ffff::9" })).res.status, 429);
  app.time.t += DAY_MS;
  assert.equal((await register(app, { ip: "198.51.100.9" })).res.status, 200, "a new day");
});

test("netOf: /24 and /64", () => {
  assert.equal(netOf("203.0.113.77"), "203.0.113");
  assert.equal(netOf("2001:db8::1"), "2001:db8:0:0");
  assert.equal(netOf("2001:0db8:0001:0002:0003::"), "2001:db8:1:2");
  assert.equal(netOf("::1"), "0:0:0:0");
  assert.equal(netOf(null), "unknown");
});

test("a new token gets 3 wishes a day; an established one 20; asks are counted apart", async () => {
  const app = makeApp({ respond: () => wishOut(POTATO) });
  const { token } = await register(app);
  for (let i = 0; i < 3; i++) assert.equal((await app.call("/v1/wish", wishBody("potato " + i), { token })).status, 200);
  const r = await app.call("/v1/wish", wishBody("potato again"), { token });
  assert.equal(r.status, 429);
  assert.equal((await r.json()).text, "You've made lots of wishes today — more tomorrow");
  assert.equal(app.calls.length, 3, "the 4th never reaches Claude");
  assert.equal((await app.call("/v1/ask", { question: "how do I print?" }, { token })).status, 200, "ask has its own quota");
  // 7 days later and with a wish received, the token is established: 20 a day.
  app.time.t += 7 * DAY_MS;
  assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 200); // this wish establishes it
  for (let i = 0; i < 19; i++) assert.equal((await app.call("/v1/wish", wishBody("potato " + i), { token })).status, 200, `wish ${i}`);
  assert.equal((await app.call("/v1/wish", wishBody("potato 21"), { token })).status, 429);
});

test("a request Claude never answered is given back to her quota", async () => {
  let fail = true;
  const app = makeApp({ respond: () => (fail ? { status: 400, error: { type: "invalid_request_error", message: "bad" } } : wishOut(POTATO)) });
  const { token } = await register(app);
  for (let i = 0; i < 5; i++) assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 503);
  fail = false;
  for (let i = 0; i < 3; i++) assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 200);
});

test("input over 300 characters or a body over 8 KB is refused before Claude", async () => {
  const app = makeApp();
  const { token } = await register(app);
  let r = await app.call("/v1/wish", wishBody("a".repeat(301)), { token });
  assert.equal(r.status, 400);
  assert.equal((await r.json()).error, "too_long");
  assert.equal((await app.call("/v1/wish", wishBody("ü".repeat(300)), { token })).status, 200, "300 characters is fine");
  r = await app.call("/v1/wish", null, { token, raw: JSON.stringify({ ...wishBody("hi"), pad: "x".repeat(9000) }) });
  assert.equal(r.status, 400);
  r = await app.call("/v1/ask", { question: "q".repeat(301) }, { token });
  assert.equal(r.status, 400);
  r = await app.call("/v1/wish", null, { token, raw: "{not json" });
  assert.equal(r.status, 400);
  assert.equal(app.calls.length, 1);
});

test("the budget pools: new tokens stop at 20 %, established and ask are untouched", async () => {
  const app = makeApp({
    respond: () => message({ kind: "wish", missing: "none", wish: POTATO }, { usage: { input_tokens: 1_000_000, output_tokens: 0 } }), // $0.10 a call
    env: { DAILY_BUDGET_USD: "1", ledger: { budgetUsd: 1, quotaNew: 100 } },
  });
  const { token } = await register(app);
  let ok = 0;
  let r;
  for (let i = 0; i < 10; i++) { r = await app.call("/v1/wish", wishBody("potato"), { token }); if (r.status === 200) ok++; else break; }
  assert.equal(ok, 2, "20 % of $1 = two $0.10 calls");
  assert.equal(r.status, 503);
  const b = await r.json();
  assert.equal(b.text, "Wishes are resting for a little while");
  assert.ok(b.retry_after > 0 && r.headers.get("retry-after"));
  assert.equal((await app.call("/v1/ask", { question: "how do I print?" }, { token })).status, 200, "ask has its own pool");
  assert.ok(app.logs.some((l) => l.includes('"budget_half"')) === false, "$0.30 of $1 is under half");
});

test("the owner is told once a day at half the budget", async () => {
  const app = makeApp({
    respond: () => message({ kind: "wish", missing: "none", wish: POTATO }, { usage: { input_tokens: 1_000_000, output_tokens: 0 } }), // $0.10 a call
    env: { ledger: { budgetUsd: 1, quotaNew: 100, quotaEstablished: 100 } },
  });
  const { token } = await register(app);
  await app.call("/v1/wish", wishBody("potato"), { token });
  app.time.t += 7 * DAY_MS;
  for (let i = 0; i < 9; i++) await app.call("/v1/wish", wishBody("potato"), { token });
  assert.equal(app.logs.filter((l) => l.includes('"budget_half"')).length, 1);
});

test("Anthropic 429 and 529 are retried with backoff, then a friendly 503", async () => {
  for (const status of [429, 529]) {
    const app = makeApp({ respond: () => ({ status, headers: { "retry-after-ms": "5" }, error: { type: status === 429 ? "rate_limit_error" : "overloaded_error", message: "busy" } }) });
    const { token } = await register(app);
    const r = await app.call("/v1/wish", wishBody("potato"), { token });
    assert.equal(r.status, 503);
    assert.equal((await r.json()).text, "Wishes are resting for a little while");
    assert.equal(app.calls.length, 3, `${status}: one try and two retries`);
    assert.ok(app.logs.at(-1).includes(status === 429 ? '"rate_limit"' : '"overloaded"'));
  }
  let n = 0;
  const app = makeApp({ respond: () => (++n < 3 ? { status: 529, headers: { "retry-after-ms": "5" } } : wishOut(POTATO)) });
  const { token } = await register(app);
  assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 200, "a retry that succeeds is invisible to her");
});

test("the account's spend limit reads as resting, with retry after midnight", async () => {
  const app = makeApp({ respond: () => ({ status: 429, error: { type: "rate_limit_error", message: "limit", details: { error_code: "enforced_spend_limit_reached" } } }) });
  const { token } = await register(app);
  const r = await app.call("/v1/wish", wishBody("potato"), { token });
  assert.equal(r.status, 503);
  assert.ok(app.logs.at(-1).includes('"spend_limit"'));
});

test("too many requests at once: the slot limit answers 503 without calling Claude", async () => {
  let release;
  const gate = new Promise((r) => { release = r; });
  const app = makeApp({ respond: async () => { await gate; return wishOut(POTATO); }, env: { ledger: { maxInflight: 2, quotaNew: 100 } } });
  const { token } = await register(app);
  const slow = [app.call("/v1/wish", wishBody("a"), { token }), app.call("/v1/wish", wishBody("b"), { token })];
  await new Promise((r) => setTimeout(r, 20));
  const third = await app.call("/v1/wish", wishBody("c"), { token });
  assert.equal(third.status, 503);
  release();
  assert.deepEqual((await Promise.all(slow)).map((r) => r.status), [200, 200]);
  assert.equal((await app.call("/v1/wish", wishBody("d"), { token })).status, 200, "slots come back");
});

test("forget ends the token", async () => {
  const app = makeApp({ respond: () => wishOut(POTATO) });
  const { token } = await register(app);
  assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 200);
  assert.equal((await app.call("/v1/forget", {}, { token })).status, 204);
  assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 401);
  const keys = [...app.ledger.s.m.keys()];
  assert.ok(!keys.some((k) => k.startsWith("tok:") || k.startsWith("q:")), keys.join(","));
});

test("tokens expire after 90 days and registering again keeps her standing", async () => {
  const app = makeApp({ respond: () => wishOut(POTATO) });
  const installId = newInstallId();
  const { token } = await register(app, { installId });
  app.time.t += 91 * DAY_MS;
  assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 401);
  const again = await register(app, { installId, ip: "192.0.2.1" });
  assert.equal(again.token, token, "same install, same token");
  assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 200);
});

test("a banned token is refused", async () => {
  const base = makeApp();
  const installId = newInstallId();
  const { token } = await register(base, { installId });
  for (let i = 0; i < 4; i++) await base.call("/v1/wish", wishBody("x"), { token });
  const tag = JSON.parse(base.logs.find((l) => l.includes('"quota"'))).tag;
  assert.match(tag, /^[0-9a-f]{12}$/);
  const app = makeApp({ env: { BANNED: `aaaaaaaaaaaa, ${tag}` } });
  assert.equal((await register(app, { installId })).token, token, "registered here too: only the ban can refuse it");
  assert.equal((await app.call("/v1/wish", wishBody("x"), { token })).status, 401);
  assert.equal(app.calls.length, 0);
});

test("ask returns a help page id and only a one-off wish", async () => {
  const bigger = { v: 1, when: { type: "now" }, do: [{ type: "set_text_size", size: "bigger" }], enabled: true };
  let out = { help_page_id: "printing", wish: null };
  const app = makeApp({ respond: () => message(out) });
  const { token } = await register(app);
  assert.deepEqual(await (await app.call("/v1/ask", { question: "how do I print?" }, { token })).json(), { help_page_id: "printing" });
  out = { help_page_id: "none", wish: bigger };
  assert.deepEqual(await (await app.call("/v1/ask", { question: "the words are too small" }, { token })).json(), { help_page_id: null, wish: bigger });
  out = { help_page_id: "made-up-page", wish: POTATO };
  assert.deepEqual(await (await app.call("/v1/ask", { question: "x" }, { token })).json(), { help_page_id: null }, "unknown page and non-now wish dropped");
});

test("health, unknown paths and methods", async () => {
  const app = makeApp();
  assert.equal((await app.call("/v1/health", null, { method: "GET" })).status, 200);
  assert.equal((await app.call("/v1/wish", null, { method: "GET" })).status, 405);
  assert.equal((await app.call("/nope", {})).status, 404);
  const noSecret = makeApp({ env: { TOKEN_SECRET: "" } });
  assert.equal((await noSecret.call("/v1/register", {})).status, 503);
});

test("the request to Claude: fixed model, max_tokens, cached system prompt, structured output", async () => {
  const app = makeApp({ respond: () => wishOut(POTATO) });
  const { token } = await register(app);
  await app.call("/v1/wish", wishBody("zebra-quartz potato"), { token });
  await app.call("/v1/wish", wishBody("something else entirely"), { token });
  await app.call("/v1/ask", { question: "how do I print?" }, { token });
  const [a, b, c] = app.calls;
  assert.equal(a.model, "claude-haiku-5-5");
  assert.equal(a.max_tokens, 400);
  assert.equal(c.max_tokens, 300);
  assert.deepEqual(a.thinking, { type: "disabled" });
  assert.deepEqual(a.system[0].cache_control, { type: "ephemeral" });
  assert.equal(JSON.stringify(a.system), JSON.stringify(b.system), "system prompt identical between requests: cacheable");
  assert.equal(a.output_config.format.type, "json_schema");
  assert.ok(!JSON.stringify(a.system).includes("zebra-quartz"), "her words are never in the system prompt");
  assert.ok(a.messages[0].content.includes("zebra-quartz"));
  assert.deepEqual(Object.keys(a.output_config.format.schema.properties), ["kind", "missing", "wish_json"]);
});

test("kill switch: a budget of 0 answers every wish and ask with the plain 503, no Claude, no counters", async () => {
  const app = makeApp({ respond: () => wishOut(POTATO), env: { DAILY_BUDGET_USD: "0", ledger: { budgetUsd: 0 } } });
  const { res, token } = await register(app); // registering still works (the live check needs it)
  assert.equal(res.status, 200);
  for (const [path, body] of [["/v1/wish", wishBody("every time I type potato")], ["/v1/ask", { question: "how do I print?" }], ["/v1/wish", null], ["/v1/ask", null]]) {
    const r = await app.call(path, body, { token, raw: body ? undefined : "garbage" });
    assert.equal(r.status, 503, path);
    const b = await r.json();
    assert.equal(b.text, "Wishes are resting for a little while");
    assert.ok(b.retry_after >= 3600 && r.headers.get("retry-after"));
  }
  assert.equal(app.calls.length, 0, "Claude is never called");
  const keys = [...app.ledger.s.m.keys()].filter((k) => k.startsWith("q:") || k.startsWith("s:"));
  assert.deepEqual(keys, [], "no quota or spend counter is touched");
  const logged = app.logs.map((l) => JSON.parse(l)).filter((l) => l.route === "/v1/wish" || l.route === "/v1/ask");
  assert.equal(logged.length, 4);
  for (const l of logged) {
    assert.equal(l.reason, "off");
    assert.ok(!("in" in l) && !("out" in l) && !("usd" in l) && !("est" in l), "no tokens, no cost: " + JSON.stringify(l));
  }
  assert.equal((await app.call("/v1/wish", wishBody("x"))).status, 401, "a stranger still gets 401");
  assert.deepEqual(await (await app.call("/v1/health", null, { method: "GET" })).json(), { ok: true, wishes: "resting" });
  assert.equal((await app.call("/v1/forget", {}, { token })).status, 204, "forget works while resting");
});

test("kill switch: one variable turns it back on", async () => {
  const app = makeApp({ respond: () => wishOut(POTATO), env: { DAILY_BUDGET_USD: "5" } });
  const { token } = await register(app);
  assert.equal((await app.call("/v1/wish", wishBody("potato"), { token })).status, 200);
  assert.deepEqual(await (await app.call("/v1/health", null, { method: "GET" })).json(), { ok: true, wishes: "on" });
  for (const bad of ["", "abc", "-1"]) { // an unreadable value must not switch it on by accident
    const a = makeApp({ respond: () => wishOut(POTATO), env: { DAILY_BUDGET_USD: bad } });
    const t = (await register(a)).token;
    const expected = bad === "" ? 200 : 503;
    assert.equal((await a.call("/v1/wish", wishBody("potato"), { token: t })).status, expected, `"${bad}"`);
  }
});

test("country gate: US, GB (UK), CA, AU by default; register, wish and ask only; forget and health open", async () => {
  const app = makeApp({ respond: () => wishOut(POTATO), env: { ledger: { registerPerNet: 1000, quotaNew: 1000 } } });
  const { token } = await register(app);
  const ok = (c) => app.call("/v1/wish", wishBody("potato"), { token, country: c });
  for (const c of ["US", "GB", "CA", "AU", "us", "gb"]) assert.equal((await ok(c)).status, 200, c);
  for (const c of ["DE", "FR", "IN", "XX", "T1", "", null]) {
    const r = await ok(c);
    assert.equal(r.status, 403, String(c));
    assert.deepEqual(await r.json(), { error: "region", text: "Wishes is not available in your country yet." });
  }
  assert.equal((await app.call("/v1/ask", { question: "how do I print?" }, { token, country: "DE" })).status, 403);
  const reg = await app.call("/v1/register", { install_id: newInstallId(), consent_version: "1", pow: { day: dayOf(app.time.t), nonce: "1" } }, { country: "DE", ip: "192.0.2.50" });
  assert.equal(reg.status, 403, "a blocked country cannot even register");
  assert.equal((await app.call("/v1/health", null, { method: "GET", country: "DE" })).status, 200);
  assert.equal((await app.call("/v1/forget", {}, { token, country: "DE" })).status, 204, "anyone may delete her data");
  const logs = app.logs.join("\n");
  assert.ok(!/"DE"|"FR"|"IN"|country/.test(logs), "the country itself is not logged");
});

test("country gate: ALLOW_COUNTRIES configures it; * opens everywhere; Cloudflare's own value wins", async () => {
  const us = makeApp({ respond: () => wishOut(POTATO), env: { ALLOW_COUNTRIES: "US" } });
  const { token } = await register(us);
  assert.equal((await us.call("/v1/wish", wishBody("potato"), { token, country: "US" })).status, 200);
  assert.equal((await us.call("/v1/wish", wishBody("potato"), { token, country: "GB" })).status, 403);
  const uk = makeApp({ respond: () => wishOut(POTATO), env: { ALLOW_COUNTRIES: "UK, ca" } });
  const t2 = (await register(uk, { country: "GB" })).token;
  assert.equal((await uk.call("/v1/wish", wishBody("potato"), { token: t2, country: "GB" })).status, 200, "UK is read as GB");
  assert.equal((await uk.call("/v1/wish", wishBody("potato"), { token: t2, country: "US" })).status, 403);
  const all = makeApp({ respond: () => wishOut(POTATO), env: { ALLOW_COUNTRIES: "*" } });
  const t3 = (await register(all, { country: null })).token;
  assert.equal((await all.call("/v1/wish", wishBody("potato"), { token: t3, country: "KP" })).status, 200);
  // On Workers the edge's request.cf.country is used, not a header a client could send.
  const req = new Request("https://wishes.test/v1/wish", { method: "POST", headers: { "cf-ipcountry": "US" }, body: "{}" });
  Object.defineProperty(req, "cf", { value: { country: "DE" } });
  assert.equal((await us.handler(req)).status, 403);
});

test("a refusal from Claude is a cannot, with no retry", async () => {
  const app = makeApp({ respond: () => message(undefined, { stop: "refusal" }) });
  const { token } = await register(app);
  const r = await app.call("/v1/wish", wishBody("anything"), { token });
  assert.deepEqual(await r.json(), { kind: "cannot", missing: "other" });
  assert.equal(app.calls.length, 1);
});
