// Does the API's grammar compiler accept our output schemas, and how fast?
// A refused schema is a free 400. Findings 2026-10-23 (claude-haiku-5-5): the
// full wish schema (11 triggers x 20 actions as anyOf) is refused ("compiled
// grammar is too large") from about 22 alternatives; merging variants by shape,
// emoji as a plain string, dropping the null union and descriptions do not
// help, and one generic action object with 14 optional fields is "too complex".
// So the wish travels as JSON text inside a small envelope (validate.js).
//   ANTHROPIC_API_KEY=... node test/schema-probe.mjs
import Anthropic from "@anthropic-ai/sdk";
import { MODEL } from "../src/app.js";
import { ASK_OUTPUT, WISH_OUTPUT } from "../src/validate.js";

const client = new Anthropic({ maxRetries: 1, defaultHeaders: process.env.ANTHROPIC_WORKSPACE_ID ? { "anthropic-workspace-id": process.env.ANTHROPIC_WORKSPACE_ID } : undefined });
let failed = 0;
for (const [name, schema] of [["WISH_OUTPUT", WISH_OUTPUT], ["ASK_OUTPUT", ASK_OUTPUT]]) {
  const t = Date.now();
  try {
    const m = await client.messages.create({
      model: MODEL, max_tokens: 100, thinking: { type: "disabled" },
      messages: [{ role: "user", content: "Answer with kind cannot, missing other and an empty wish_json (or help_page_id none)." }],
      output_config: { format: { type: "json_schema", schema } },
    });
    console.log(`ACCEPTED ${name}: ${Date.now() - t} ms, stop ${m.stop_reason}`);
  } catch (e) {
    failed++;
    console.log(`REFUSED  ${name}: ${Date.now() - t} ms, ${e.status} ${String(e.error?.error?.message ?? e.message).slice(0, 200)}`);
  }
}
process.exit(failed ? 1 : 0);
