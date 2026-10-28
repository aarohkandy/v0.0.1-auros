// Pulls the service's own log lines out of `wrangler tail --format json`
// output (pretty-printed JSON objects, one after another), so CI can check
// them:  node test/tail-logs.mjs tail.json service-logs.txt
import { readFileSync, writeFileSync } from "node:fs";

const raw = readFileSync(process.argv[2], "utf8");
const events = [];
let depth = 0, start = -1, inStr = false, esc = false;
for (let i = 0; i < raw.length; i++) {
  const c = raw[i];
  if (inStr) { if (esc) esc = false; else if (c === "\\") esc = true; else if (c === '"') inStr = false; continue; }
  if (c === '"') inStr = true;
  else if (c === "{") { if (depth++ === 0) start = i; }
  else if (c === "}" && --depth === 0) { try { events.push(JSON.parse(raw.slice(start, i + 1))); } catch {} }
}
const lines = events.flatMap((e) => (e.logs || []).map((l) => JSON.stringify(l.message)));
writeFileSync(process.argv[3], lines.join("\n"));
const outcomes = {};
for (const e of events) outcomes[e.outcome] = (outcomes[e.outcome] || 0) + 1;
const cpu = events.map((e) => e.cpuTime).filter((x) => typeof x === "number").sort((a, b) => a - b);
// Exceptions the platform recorded (name and message only): why a request crashed.
for (const e of events) for (const x of e.exceptions || []) console.log(`  EXCEPTION ${e.event?.request?.url ?? ""}: ${x.name}: ${String(x.message).slice(0, 300)}`);
const byPath = {};
for (const e of events) {
  if (typeof e.cpuTime !== "number") continue;
  const path = (() => { try { return new URL(e.event?.request?.url).pathname; } catch { return "?"; } })();
  (byPath[path] ??= []).push(e.cpuTime);
}
for (const [path, a] of Object.entries(byPath)) console.log(`  CPU ms ${path}: n ${a.length}, p50 ${a.sort((x, y) => x - y)[a.length >> 1]}, max ${a.at(-1)}`);
console.log(`${events.length} events, ${lines.length} service log lines; outcomes ${JSON.stringify(outcomes)}` +
  (cpu.length ? `; CPU ms p50 ${cpu[cpu.length >> 1]}, max ${cpu.at(-1)} (Free plan limit 10)` : "; no CPU times in the tail events"));
