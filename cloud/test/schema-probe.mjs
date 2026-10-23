// Which output schemas does the API's grammar compiler accept for MODEL?
// A refused schema is a free 400; an accepted one costs a few hundred tokens.
//   ANTHROPIC_API_KEY=... node test/schema-probe.mjs
import Anthropic from "@anthropic-ai/sdk";
import { MODEL } from "../src/app.js";
import { WISH_OUTPUT } from "../src/validate.js";

const client = new Anthropic({ maxRetries: 1, defaultHeaders: process.env.ANTHROPIC_WORKSPACE_ID ? { "anthropic-workspace-id": process.env.ANTHROPIC_WORKSPACE_ID } : undefined });
const clone = (x) => JSON.parse(JSON.stringify(x));
const variant = (name, edit) => { const s = clone(WISH_OUTPUT); edit(s); return [name, s]; };
const uniqTypes = (variants) => { const seen = new Set(); return variants.filter((v) => { const t = v.properties.type.const ?? v.properties.type.enum?.join(); return seen.has(t) ? false : seen.add(t); }); };

const VARIANTS = [
  variant("full", () => {}),
  variant("emoji as string", (s) => { s.$defs.emoji = { type: "string" }; }),
  variant("wish not nullable", (s) => { s.properties.wish = { $ref: "#/$defs/wish" }; }),
  variant("actions: one variant per type (16)", (s) => { s.$defs.action.anyOf = uniqTypes(s.$defs.action.anyOf); }),
  variant("actions: first 10", (s) => { s.$defs.action.anyOf = s.$defs.action.anyOf.slice(0, 10); }),
  variant("when: first 5", (s) => { s.$defs.when.anyOf = s.$defs.when.anyOf.slice(0, 5); }),
  variant("wish without when", (s) => { delete s.$defs.wish.properties.when; s.$defs.wish.required = s.$defs.wish.required.filter((r) => r !== "when"); }),
  variant("wish without do", (s) => { delete s.$defs.wish.properties.do; s.$defs.wish.required = s.$defs.wish.required.filter((r) => r !== "do"); }),
  variant("emoji string + not nullable + one per type", (s) => { s.$defs.emoji = { type: "string" }; s.properties.wish = { $ref: "#/$defs/wish" }; s.$defs.action.anyOf = uniqTypes(s.$defs.action.anyOf); }),
  variant("no descriptions", (s) => { const strip = (n) => { if (n && typeof n === "object") { delete n.description; Object.values(n).forEach(strip); } }; strip(s); }),
];

for (const [name, schema] of VARIANTS) {
  const t = Date.now();
  try {
    const m = await client.messages.create({
      model: MODEL, max_tokens: 200, thinking: { type: "disabled" },
      messages: [{ role: "user", content: "Answer kind cannot, missing other, wish null if allowed, else the smallest valid wish." }],
      output_config: { format: { type: "json_schema", schema } },
    });
    console.log(`ACCEPTED ${name}: ${Date.now() - t} ms, stop ${m.stop_reason}, in ${m.usage.input_tokens} out ${m.usage.output_tokens}`);
  } catch (e) {
    console.log(`REFUSED  ${name}: ${Date.now() - t} ms, ${e.status} ${String(e.error?.error?.message ?? e.message).slice(0, 160)}`);
  }
}
