// What the deployed Worker's own log lines (test/tail-logs.mjs output) must
// show.  node test/log-assert.mjs service-logs.txt off|on
//   off: every wish/ask request was answered 503 "off" and none of them has a
//        token count or a cost: Claude was not called, so Anthropic bills nothing.
//   on:  no request line carries anything but the allowed fields.
import { readFileSync } from "node:fs";

const mode = process.argv[3];
const lines = readFileSync(process.argv[2], "utf8").split("\n").filter(Boolean).flatMap((l) => {
  try { return JSON.parse(l).map((m) => JSON.parse(m)); } catch { return []; }
});
const reqs = lines.filter((l) => l.ev === "req");
const claudeRoutes = reqs.filter((l) => l.route === "/v1/wish" || l.route === "/v1/ask");
const withTokens = claudeRoutes.filter((l) => "in" in l || "out" in l || "usd" in l || "cr" in l || "cw" in l);
let bad = 0;
if (mode === "off") {
  const answered = claudeRoutes.filter((l) => l.status === 503 && l.reason === "off");
  console.log(`resting check: ${reqs.length} request lines, ${claudeRoutes.length} wish/ask, ${answered.length} answered 503 reason "off", ${withTokens.length} with token counts or cost`);
  if (claudeRoutes.length < 2) { console.log("FAIL: expected at least a wish and an ask in the logs"); bad++; }
  if (answered.length !== claudeRoutes.length) { console.log("FAIL: a wish or ask was not answered by the kill switch"); bad++; }
  if (withTokens.length) { console.log("FAIL: a request line shows Claude was called"); bad++; }
} else {
  console.log(`logs: ${reqs.length} request lines, ${withTokens.length} with token counts`);
}
process.exit(bad ? 1 : 0);
