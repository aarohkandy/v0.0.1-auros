// What the deployed Worker's own log lines (test/tail-logs.mjs output) must
// show.  node test/log-assert.mjs service-logs.txt off|on
//   off: every wish/ask request was answered 401 (a stranger) or 503 "off", and
//        none of them has a token count or a cost: Claude was not called, so
//        Anthropic bills nothing.
//   on:  no request line carries anything but the allowed fields.
import { readFileSync } from "node:fs";

const mode = process.argv[3];
const lines = readFileSync(process.argv[2], "utf8").split("\n").filter(Boolean).flatMap((l) => {
  try { return JSON.parse(l).map((m) => JSON.parse(m)); } catch { return []; }
});
const correlation = process.env.SMOKE_CORRELATION_TOKEN;
const source = process.env.DEPLOYMENT_SOURCE_SHA;
const nonce = process.env.DEPLOYMENT_NONCE;
const bound = correlation !== undefined || source !== undefined || nonce !== undefined;
if (bound && (!/^[a-f0-9]{64}$/.test(correlation || "") || !/^[a-f0-9]{40}$/.test(source || "") || !/^[a-f0-9]{64}$/.test(nonce || ""))) {
  console.error("FAIL: missing or malformed deployment/log correlation"); process.exit(1);
}
const reqs = lines.filter((l) => l.ev === "req" && (!bound ||
  l.smoke_correlation === correlation && l.deployment_source === source && l.deployment_nonce === nonce));
const claudeRoutes = reqs.filter((l) => l.route === "/v1/wish" || l.route === "/v1/ask");
const withTokens = claudeRoutes.filter((l) => "in" in l || "out" in l || "usd" in l || "cr" in l || "cw" in l);
let bad = 0;
if (mode === "off") {
  const resting = claudeRoutes.filter((l) => l.status === 503 && l.reason === "off");
  const strangers = claudeRoutes.filter((l) => l.status === 401);
  console.log(`resting check: ${reqs.length} request lines, ${claudeRoutes.length} wish/ask: ${resting.length} answered 503 reason "off", ${strangers.length} refused 401 (no valid token), ${withTokens.length} with token counts or cost`);
  if (resting.length < 2) { console.log("FAIL: expected at least a wish and an ask answered by the kill switch"); bad++; }
  if (resting.length + strangers.length !== claudeRoutes.length) { console.log("FAIL: a wish or ask was answered some other way"); bad++; }
  if (withTokens.length) { console.log("FAIL: a request line shows Claude was called"); bad++; }
} else if (mode === "on") {
  // The smoke always sends these two unauthenticated requests before registration.
  // Require both exact routes/statuses, not an arbitrary nonempty tail file.
  for (const route of ["/v1/wish", "/v1/ask"]) {
    if (!claudeRoutes.some((l) => l.route === route && l.status === 401)) {
      console.log(`FAIL: missing smoke refusal evidence for ${route}`); bad++;
    }
  }
  console.log(`logs: ${reqs.length} request lines, ${withTokens.length} with token counts`);
}
else { console.log("FAIL: invalid log mode"); bad++; }
process.exit(bad ? 1 : 0);
