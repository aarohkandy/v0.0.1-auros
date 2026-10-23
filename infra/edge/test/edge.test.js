import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { handle, waitPage } from "../src/edge.js";
import { Gate, parseCaps } from "../src/gate.js";

const D = "auros.example";
const env = { DOMAIN: D, GH_REPO: "own/rep", PAGES_ORIGIN: "https://own.github.io/rep/", OPS_TOKEN: "owner-secret", HALT_TOKEN: "halt-secret" };
const T0 = Date.parse("2026-11-01T10:00:00Z");

function world() {
  const m = new Map();
  const gate = new Gate({ get: async (k) => m.get(k), put: async (k, v) => void m.set(k, structuredClone(v)) }, parseCaps("0,25,100,300,0"));
  const seen = [];
  const pagesFetch = async (u) => { seen.push(u); return new Response("<html>site</html>", { status: 200, headers: { "content-type": "text/html" } }); };
  const call = (path, { host = `get.${D}`, method = "GET", headers = {}, body, now = T0 } = {}) =>
    handle(new Request(`https://${host}${path}`, { method, headers, body }), env, gate, pagesFetch, now);
  const as = (tok, path, body, now = T0) => call(path, { method: "POST", headers: { authorization: `Bearer ${tok}` }, body: JSON.stringify(body ?? {}), now });
  return { gate, call, as, seen };
}

test("closed until the owner opens wave 1: a plain page, not counted", async () => {
  const { call, gate } = world();
  const r = await call("/");
  assert.equal(r.status, 503);
  assert.equal(r.headers.get("x-auros-gate"), "closed");
  assert.equal((await gate.state(T0)).today, 0);
});

test("wave 1: 25 people get the installer, the 26th is told to come back tomorrow, tomorrow it works", async () => {
  const { call, as } = world();
  assert.equal((await as("owner-secret", "/_ops/wave", { wave: 1 })).status, 200);
  for (let i = 0; i < 25; i++) {
    const r = await call("/");
    assert.equal(r.status, 302, `person ${i + 1}`);
    assert.equal(r.headers.get("location"), "https://github.com/own/rep/releases/latest/download/AurOS-Setup.exe");
  }
  const full = await call("/");
  assert.equal(full.status, 503);
  assert.equal(full.headers.get("x-auros-gate"), "full");
  const wait = Number(full.headers.get("retry-after"));
  assert.ok(wait >= 60 && wait <= 86400, `retry-after ${wait}`);
  assert.equal((await call("/", { now: T0 + 86400e3 })).status, 302);
});

test("only a whole-file GET is counted: a Range request, a resumed download and a HEAD are not", async () => {
  const { call, as, gate } = world();
  await as("owner-secret", "/_ops/wave", { wave: 1 });
  for (let i = 0; i < 40; i++) {
    assert.equal((await call("/", { headers: { range: "bytes=0-0" } })).status, 302);
    assert.equal((await call("/AurOS-Setup.exe", { method: "HEAD" })).status, 302);
  }
  assert.equal((await gate.state(T0)).today, 0);
});

test("a download in progress is never cut off: the pieces and the checksums ignore the cap and the pause", async () => {
  const { call, as } = world();
  await as("owner-secret", "/_ops/wave", { wave: 1 });
  await as("halt-secret", "/_ops/pause", { reason: "test" });
  assert.equal((await call("/")).status, 503);
  const p = await call("/download/v0.1.0-abc1234/auros-desktop.img.gz.003");
  assert.equal(p.status, 302);
  assert.equal(p.headers.get("location"), "https://github.com/own/rep/releases/download/v0.1.0-abc1234/auros-desktop.img.gz.003");
  assert.equal((await call("/SHA256SUMS")).headers.get("location"), "https://github.com/own/rep/releases/latest/download/SHA256SUMS");
  assert.equal((await call("/pieces.txt")).status, 302);
});

test("the second responder can pause, never resume, open a wave or post a status; the owner can", async () => {
  const { call, as } = world();
  await as("owner-secret", "/_ops/wave", { wave: 2 });
  assert.equal((await as("halt-secret", "/_ops/pause", { reason: "something odd" })).status, 200);
  const st = await (await call("/_ops/state")).json();
  assert.equal(st.paused, true); assert.match(st.reason, /second responder: something odd/);
  assert.equal((await call("/")).headers.get("x-auros-gate"), "paused");
  for (const p of ["/_ops/resume", "/_ops/wave", "/_ops/status"]) assert.equal((await as("halt-secret", p, { wave: 3, results: [] })).status, 403, p);
  assert.equal((await as("owner-secret", "/_ops/resume")).status, 200);
  assert.equal((await call("/")).status, 302);
});

test("no token or a wrong one is refused, and nothing changes", async () => {
  const { call, as } = world();
  assert.equal((await call("/_ops/pause", { method: "POST", body: "{}" })).status, 401);
  assert.equal((await as("wrong", "/_ops/pause")).status, 401);
  assert.equal((await as("", "/_ops/wave", { wave: 4 })).status, 401);
  assert.equal((await (await call("/_ops/state")).json()).paused, false);
});

test("pausing twice keeps the first reason; a bad wave number is refused", async () => {
  const { call, as } = world();
  await as("owner-secret", "/_ops/pause", { reason: "first" });
  await as("owner-secret", "/_ops/pause", { reason: "second" });
  assert.match((await (await call("/_ops/state")).json()).reason, /first/);
  for (const w of [-1, 5, 1.5, "1", null]) assert.equal((await as("owner-secret", "/_ops/wave", { wave: w })).status, 400, String(w));
});

test("the open wave with no cap lets everyone in", async () => {
  const { call, as } = world();
  await as("owner-secret", "/_ops/wave", { wave: 4 });
  for (let i = 0; i < 400; i++) assert.equal((await call("/")).status, 302);
  assert.equal((await (await call("/_ops/state")).json()).cap, null);
});

test("only AurOS's own files and tags are passed on; nothing else is a way to anywhere", async () => {
  const { call } = world();
  for (const p of ["/download/../../x/y", "/download/a/b/c", "/download/a%2F..%2Fb/c", "/download/tag/", "/etc/passwd", "/AurOS-Setup.exe/x", "/evil.exe", "/download/tag/x y"]) {
    assert.equal((await call(p)).status, 404, p);
  }
  const u = await call("/apt/InRelease", { host: `updates.${D}` });
  assert.equal(u.headers.get("location"), "https://github.com/own/rep/releases/download/apt/InRelease");
  assert.equal((await call("/apt-beta/Packages", { host: `updates.${D}` })).status, 302);
  for (const p of ["/other/InRelease", "/apt/", "/apt/a/b", "/v1/InRelease"]) assert.equal((await call(p, { host: `updates.${D}` })).status, 404, p);
  assert.equal((await call("/", { host: "evil.example" })).status, 404);
  assert.equal((await call("/", { method: "DELETE" })).status, 405);
});

test("www goes to the plain name; the website is the Pages site passed through; status shows its page", async () => {
  const { call, seen } = world();
  const w = await call("/guide.html?x=1", { host: `www.${D}` });
  assert.equal(w.status, 301); assert.equal(w.headers.get("location"), `https://${D}/guide.html?x=1`);
  assert.equal(await (await call("/", { host: D })).text(), "<html>site</html>");
  await call("/assets/site.css", { host: D });
  await call("/", { host: `status.${D}` });
  assert.deepEqual(seen, ["https://own.github.io/rep/", "https://own.github.io/rep/assets/site.css", "https://own.github.io/rep/status.html"]);
  for (const p of ["/..%2f..%2fx", "/a b", "/a%00b"]) assert.ok((await call(p, { host: D })).status >= 400, p);
});

test("status.json: the monitor's latest result is served while fresh, the shipped one when it is stale", async () => {
  const { call, as, seen } = world();
  const fresh = { checked_at: new Date(T0 - 300e3).toISOString(), ok: true, results: [{ check: "site", state: "ok", detail: "secret detail", url: "x" }] };
  assert.equal((await as("owner-secret", "/_ops/status", fresh)).status, 200);
  const j = await (await call("/status.json", { host: D })).json();
  assert.deepEqual(j.results, [{ check: "site", state: "ok" }]); // only what the page needs: no detail, no URLs
  assert.equal(seen.length, 0);
  await call("/status.json", { host: D, now: T0 + 25 * 3600e3 });
  assert.equal(seen.length, 1);
});

test("what she reads when the door is shut has no jargon and a way back", () => {
  for (const why of ["closed", "full", "paused"]) {
    const t = waitPage(why, T0, "https://auros.example/");
    assert.equal(t.status, 503);
  }
  return Promise.all(["closed", "full", "paused"].map(async (why) => {
    const text = (await waitPage(why, T0, "https://auros.example/").text()).replace(/<style>.*?<\/style>|<[^>]+>/gs, " ");
    assert.doesNotMatch(text, /\b(wave|cap|limit|rate|server|error|503|worker|quota)\b/i, why);
    assert.match(text, /Back to the AurOS page/);
  }));
});

test("the Cloudflare config template renders with every blank filled", () => {
  const t = readFileSync(new URL("../wrangler.toml.in", import.meta.url), "utf8");
  const out = t.replaceAll("@DOMAIN@", D).replaceAll("@GH_REPO@", "own/rep").replaceAll("@PAGES_ORIGIN@", "https://own.github.io/rep/");
  assert.doesNotMatch(out, /@[A-Z_]+@/);
  for (const h of [D, `www.${D}`, `get.${D}`, `updates.${D}`, `status.${D}`]) assert.match(out, new RegExp(`pattern = "${h.replaceAll(".", "\\.")}", custom_domain = true`));
  assert.doesNotMatch(out.replace(/^#.*$/gm, ""), /workers_dev = true|github\.com/);
});
