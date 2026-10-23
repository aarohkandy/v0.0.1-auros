// Load test: the real handler behind a real HTTP server, Claude replaced by a
// local mock that answers in 0.6-1.4 s and can enforce an Anthropic-style
// requests-per-minute limit. autocannon plays 1,000 people.
//   node test/load.mjs           full run (~2 minutes)
//   node test/load.mjs --quick   CI run (~40 s)
import http from "node:http";
import { appendFileSync } from "node:fs";
import autocannon from "autocannon";
import Anthropic from "@anthropic-ai/sdk";
import { createApp } from "../src/app.js";
import { Ledger, MapStorage } from "../src/ledger.js";
import { ALL, POTATO, message, register } from "./helpers.js";

const quick = process.argv.includes("--quick");

function listen(handler) {
  const server = http.createServer(handler);
  server.keepAliveTimeout = 60_000;
  // backlog 4096: Linux allows it; macOS caps it at 128, so run the 1,000-at-once bursts on Linux.
  return new Promise((resolve) => server.listen(0, "127.0.0.1", 4096, () => resolve(server)));
}

// Mock Anthropic. rpm: null = unlimited, else a token bucket like the API's
// (refills continuously; a minute's allowance is not available as one burst).
async function mockAnthropic(state) {
  return listen((req, res) => {
    let raw = "";
    req.on("data", (c) => (raw += c));
    req.on("end", () => {
      state.calls++;
      if (state.rpm) {
        const now = Date.now();
        state.tokens = Math.min(state.burst, state.tokens + ((now - state.last) / 60_000) * state.rpm);
        state.last = now;
        if (state.tokens < 1) {
          state.limited++;
          res.writeHead(429, { "content-type": "application/json", "retry-after": "1" });
          return res.end(JSON.stringify({ type: "error", error: { type: "rate_limit_error", message: "mock rpm" } }));
        }
        state.tokens -= 1;
      }
      setTimeout(() => {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify(message({ kind: "wish", missing: "none", wish: POTATO })));
      }, 600 + Math.random() * 800);
    });
  });
}

async function service({ maxInflight, mockPort }) {
  const time = { t: Date.now() };
  const handler = createApp({
    env: { TOKEN_SECRET: "load-test", ANTHROPIC_API_KEY: "sk-load-not-real", POW_BITS: "4" },
    ledger: new Ledger(new MapStorage(), { budgetUsd: 1000, quotaEstablished: 1000, quotaNew: 1000, establishDays: 7, registerPerNet: 1e9, maxInflight }),
    anthropic: new Anthropic({ apiKey: "sk-load-not-real", baseURL: `http://127.0.0.1:${mockPort}`, maxRetries: 2, timeout: 8000 }),
    log: () => {},
    now: () => (time.t = Date.now()),
  });
  const server = await listen(async (req, res) => {
    const chunks = [];
    for await (const c of req) chunks.push(c);
    const r = await handler(new Request(`http://127.0.0.1${req.url}`, { method: req.method, headers: req.headers, body: req.method === "GET" ? undefined : Buffer.concat(chunks) }));
    res.writeHead(r.status, Object.fromEntries(r.headers));
    res.end(Buffer.from(await r.arrayBuffer()));
  });
  const tokens = [];
  const app = { time, call: (path, body) => handler(new Request("http://x" + path, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(body) })) };
  for (let i = 0; i < 1000; i++) tokens.push((await register(app, { bits: 4 })).token);
  return { server, tokens };
}

async function scenario(name, { users, rate, maxInflight, rpm }) {
  const up = { calls: 0, limited: 0, rpm, burst: rpm ? Math.max(1, Math.ceil(rpm / 60)) : 0, tokens: rpm ? Math.ceil(rpm / 60) : 0, last: Date.now() };
  const mock = await mockAnthropic(up);
  const { server, tokens } = await service({ maxInflight, mockPort: mock.address().port });
  const body = JSON.stringify({ text: "every time I type potato fill my screen with potatoes", lang: "en", schema: { v: 1, ...ALL }, sensors: {} });
  const lat = { 200: [], 429: [], 503: [], other: [] };
  let i = 0;
  const t0 = Date.now();
  const inst = autocannon({
    url: `http://127.0.0.1:${server.address().port}/v1/wish`,
    method: "POST",
    connections: Math.min(users, 1000),
    amount: users,
    overallRate: rate || undefined,
    timeout: 30,
    headers: { "content-type": "application/json" },
    requests: [{ setupRequest: (req) => ({ ...req, body, headers: { ...req.headers, authorization: "Bearer " + tokens[i++ % tokens.length] } }) }],
  });
  const errs = {};
  inst.on("reqError", (e) => (errs[e.code || e.message] = (errs[e.code || e.message] || 0) + 1));
  inst.on("response", (client, status, bytes, ms) => (lat[status] ?? lat.other).push(Math.round(ms)));
  const result = await inst;
  const wall = (Date.now() - t0) / 1000;
  server.close(); mock.close();
  server.closeAllConnections?.(); mock.closeAllConnections?.();
  const pct = (a, p) => (a.length ? a.sort((x, y) => x - y)[Math.min(a.length - 1, Math.floor((p / 100) * a.length))] : "-");
  const row = {
    scenario: name, users, "max in flight": maxInflight, "Anthropic RPM": rpm ?? "unlimited",
    "wish (200)": lat[200].length, "resting (503)": lat[503].length, "limit (429)": lat[429].length, errors: lat.other.length + result.errors + result.timeouts,
    "200 p50/p99 ms": `${pct(lat[200], 50)}/${pct(lat[200], 99)}`, "503 p50/p99 ms": `${pct(lat[503], 50)}/${pct(lat[503], 99)}`,
    "Claude calls (429s)": `${up.calls} (${up.limited})`, "wall s": wall.toFixed(1),
  };
  console.log(JSON.stringify(row), Object.keys(errs).length ? JSON.stringify(errs) : "");
  return row;
}

const rows = [];
// 1. A burst: 1,000 people press Wish in the same second, slot limit as deployed.
rows.push(await scenario("burst, deployed limits", { users: 1000, maxInflight: 40, rpm: 1000 }));
// 2. The same burst with no slot limit: what Anthropic's own limit does.
rows.push(await scenario("burst, no slot limit", { users: 1000, maxInflight: 100000, rpm: 1000 }));
// 3. The same 1,000 people spread over a minute (a busy evening).
rows.push(await scenario("1,000 spread over a minute", { users: quick ? 300 : 1000, rate: 16, maxInflight: 40, rpm: 1000 }));
// 4. Headroom: a higher Anthropic tier (Build, 5,000 RPM) and slots to match.
if (!quick) rows.push(await scenario("burst, Build tier", { users: 1000, maxInflight: 150, rpm: 5000 }));

const cols = Object.keys(rows[0]);
const table = `| ${cols.join(" | ")} |\n|${cols.map(() => "---").join("|")}|\n${rows.map((r) => `| ${cols.map((c) => r[c] ?? "").join(" | ")} |`).join("\n")}\n`;
console.log("\n" + table);
if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, `### Load test (mocked Claude)\n\n${table}`);
const bad = rows.filter((r) => r.errors > 0);
process.exit(bad.length ? 1 : 0);
