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
console.log(`${events.length} events, ${lines.length} service log lines`);
