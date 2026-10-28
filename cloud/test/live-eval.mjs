// The real model on the acceptance and adversarial sets, through the real
// handler (in-process; only the counters are in memory). Needs
// ANTHROPIC_API_KEY in the environment; costs well under a cent per run.
//   node test/live-eval.mjs            -> table on stdout (+ $GITHUB_STEP_SUMMARY), live-eval.json
import { appendFileSync, writeFileSync } from "node:fs";
import { randomBytes } from "node:crypto";
import Anthropic from "@anthropic-ai/sdk";
import { createApp, MODEL } from "../src/app.js";
import { Ledger, MapStorage } from "../src/ledger.js";
import { ADVERSARIAL } from "./adversarial.js";
import { ALL, register, withCountry } from "./helpers.js";

if (!process.env.ANTHROPIC_API_KEY) { console.error("ANTHROPIC_API_KEY is not set"); process.exit(2); }

const logs = [];
const time = { t: Date.now() };
const handler = createApp({
  env: { TOKEN_SECRET: randomBytes(32).toString("hex"), ANTHROPIC_API_KEY: process.env.ANTHROPIC_API_KEY, POW_BITS: "8" },
  ledger: new Ledger(new MapStorage(), { budgetUsd: 1, quotaEstablished: 1000, quotaNew: 1000, establishDays: 7, registerPerNet: 10, maxInflight: 4 }),
  anthropic: new Anthropic({ maxRetries: 2, timeout: 8000, defaultHeaders: process.env.ANTHROPIC_WORKSPACE_ID ? { "anthropic-workspace-id": process.env.ANTHROPIC_WORKSPACE_ID } : undefined }),
  log: (o) => logs.push(o),
  now: () => (time.t = Date.now()),
});
const app = {
  time,
  call: (path, body, { token } = {}) => handler(withCountry(new Request("https://eval.test" + path, {
    method: "POST", headers: { "content-type": "application/json", "cf-connecting-ip": "192.0.2.1", ...(token ? { authorization: "Bearer " + token } : {}) }, body: JSON.stringify(body),
  }))),
};
const { token } = await register(app);

const sensors = (compass) => ({ compass, tilt: true, light: true });
const isPotato = (b) => b.kind === "wish" && b.wish.when.type === "typed_word" && /^potato$/i.test(b.wish.when.word) && b.wish.do.some((a) => a.type === "fill_screen" && a.emoji === "🥔");
const isNorth = (b) => b.kind === "wish" && b.wish.when.type === "sensor" && b.wish.when.sensor === "compass" && b.wish.when.heading === "north" && b.wish.do.some((a) => a.type === "brightness");
const CASES = [
  { name: "owner: potato", text: "every time I type potato, fill my screen with potatoes", sensors: sensors(true), pass: isPotato },
  { name: "owner: compass", text: "when my laptop points north, make the screen brighter", sensors: sensors(true), pass: isNorth },
  { name: "owner: compass, none here", text: "when my laptop points north, make the screen brighter", sensors: sensors(false), pass: (b) => b.kind === "cannot" && b.missing === "compass" },
  { name: "bigger text", text: "make the writing bigger please", sensors: sensors(false), pass: (b) => b.kind === "wish" && b.wish.do.some((a) => a.type === "set_text_size") },
  { name: "pill reminder", text: "remind me to take my pills every day at 9 in the morning", sensors: sensors(false), pass: (b) => b.kind === "wish" && b.wish.when.type === "time" && b.wish.do.some((a) => a.type === "remind") },
  ...ADVERSARIAL.map((a) => ({
    name: a.name, text: a.text, sensors: sensors(true),
    pass: (b) => (a.expect === "cannot" ? b.kind === "cannot" : b.kind === "cannot" || b.kind === "wish"),
    want: a.missing,
  })),
];

const rows = [];
for (const c of CASES) {
  const n = logs.length;
  const r = await handler(withCountry(new Request("https://eval.test/v1/wish", {
    method: "POST", headers: { "content-type": "application/json", authorization: "Bearer " + token },
    body: JSON.stringify({ text: c.text, lang: "en", schema: { v: 1, ...ALL }, sensors: c.sensors }),
  })));
  const body = await r.json();
  const log = logs.slice(n).find((l) => l.ev === "req") || {};
  const ok = r.status === 200 && c.pass(body);
  const note = c.want && body.missing !== c.want ? `missing ${body.missing}, hoped ${c.want}` : "";
  rows.push({ name: c.name, ok, status: r.status, body, ms: log.ms, usd: log.usd, in: log.in, out: log.out, cr: log.cr, cw: log.cw, rejected: log.rejected, note });
  console.log(`${ok ? "ok  " : "FAIL"} ${c.name}: ${JSON.stringify(body)} ${log.ms} ms $${log.usd}${log.rejected ? " rejected:" + log.rejected : ""}${log.reason ? " reason:" + log.reason : ""}${log.detail ? " detail:" + log.detail : ""}${note ? " (" + note + ")" : ""}`);
}
for (const q of [["How do I print a letter?", "printing"], ["How do I get on the wifi?", "getting-online"], ["Someone phoned saying they are from AurOS and need to fix my computer", "never-call"]]) {
  const n = logs.length;
  const r = await app.call("/v1/ask", { question: q[0], lang: "en" }, { token });
  const body = await r.json();
  const log = logs.slice(n).find((l) => l.ev === "req") || {};
  const ok = r.status === 200 && body.help_page_id === q[1];
  rows.push({ name: "ask: " + q[0], ok, status: r.status, body, ms: log.ms, usd: log.usd, in: log.in, out: log.out, cr: log.cr, cw: log.cw });
  console.log(`${ok ? "ok  " : "FAIL"} ask "${q[0]}": ${JSON.stringify(body)} ${log.ms} ms $${log.usd}${log.reason ? " reason:" + log.reason : ""}${log.detail ? " detail:" + log.detail : ""}`);
}

const wishes = rows.filter((r) => !r.name.startsWith("ask:") && typeof r.usd === "number");
const asks = rows.filter((r) => r.name.startsWith("ask:") && typeof r.usd === "number");
const avg = (a, k) => a.reduce((s, r) => s + (r[k] || 0), 0) / Math.max(1, a.length);
const failed = rows.filter((r) => !r.ok);
const summary = {
  model: MODEL, cases: rows.length, failed: failed.length,
  wish_usd_avg: +avg(wishes, "usd").toFixed(6), wish_ms_avg: Math.round(avg(wishes, "ms")), wish_tokens: { in: Math.round(avg(wishes, "in")), cache_read: Math.round(avg(wishes, "cr")), cache_write: Math.round(avg(wishes, "cw")), out: Math.round(avg(wishes, "out")) },
  ask_usd_avg: +avg(asks, "usd").toFixed(6), ask_ms_avg: Math.round(avg(asks, "ms")),
  total_usd: +rows.reduce((s, r) => s + (r.usd || 0), 0).toFixed(6),
};
console.log(JSON.stringify(summary));
writeFileSync("live-eval.json", JSON.stringify({ summary, rows }, null, 1));
if (process.env.GITHUB_STEP_SUMMARY) {
  appendFileSync(process.env.GITHUB_STEP_SUMMARY, `### Live eval (${MODEL})\n\n${failed.length ? `**${failed.length} failed**` : "All passed"}; average wish $${summary.wish_usd_avg}, ${summary.wish_ms_avg} ms; ask $${summary.ask_usd_avg}; total $${summary.total_usd}\n\n| case | ok | answer | ms | $ |\n|---|---|---|---|---|\n${rows.map((r) => `| ${r.name} | ${r.ok ? "yes" : "**NO**"} | \`${JSON.stringify(r.body).replace(/\|/g, "\\|").slice(0, 160)}\`${r.note ? " (" + r.note + ")" : ""} | ${r.ms} | ${r.usd} |`).join("\n")}\n`);
}
process.exit(failed.length ? 1 : 0);
