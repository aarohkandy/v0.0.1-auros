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

// Merge variants that differ only in `type` (their limits are dropped for decoding anyway).
const noDesc = (n) => JSON.parse(JSON.stringify(n, (k, v) => (k === "description" ? undefined : v)));
function mergeByShape(variants) {
  const groups = new Map();
  for (const v of variants) {
    const { type, ...rest } = v.properties;
    const key = JSON.stringify([noDesc(rest), [...v.required].sort()]);
    const types = type.enum ?? [type.const];
    if (groups.has(key)) groups.get(key).properties.type.enum.push(...types);
    else groups.set(key, { ...clone(v), properties: { ...clone(v.properties), type: { type: "string", enum: [...types] } } });
  }
  return [...groups.values()];
}
// One action object: type plus every field, optional.
function generic(variants) {
  const props = {};
  for (const v of variants) for (const [k, sch] of Object.entries(v.properties)) if (k !== "type") props[k] ??= noDesc(sch);
  return [{ type: "object", additionalProperties: false, required: ["type"], properties: { type: { type: "string", enum: [...new Set(variants.flatMap((v) => v.properties.type.enum ?? [v.properties.type.const]))] }, ...props } }];
}
const count = (s) => `${s.$defs.when.anyOf.length} when, ${s.$defs.action.anyOf.length} action variants`;

const VARIANTS = [
  variant("merged by shape", (s) => { s.$defs.action.anyOf = mergeByShape(s.$defs.action.anyOf); s.$defs.when.anyOf = mergeByShape(s.$defs.when.anyOf); }),
  variant("merged by shape, emoji as string", (s) => { s.$defs.emoji = { type: "string" }; s.$defs.action.anyOf = mergeByShape(s.$defs.action.anyOf); s.$defs.when.anyOf = mergeByShape(s.$defs.when.anyOf); }),
  variant("merged actions only", (s) => { s.$defs.action.anyOf = mergeByShape(s.$defs.action.anyOf); }),
  variant("generic action, merged when", (s) => { s.$defs.action.anyOf = generic(s.$defs.action.anyOf); s.$defs.when.anyOf = mergeByShape(s.$defs.when.anyOf); }),
  variant("generic action, full when", (s) => { s.$defs.action.anyOf = generic(s.$defs.action.anyOf); }),
  variant("first 12 actions", (s) => { s.$defs.action.anyOf = s.$defs.action.anyOf.slice(0, 12); }),
  variant("first 14 actions", (s) => { s.$defs.action.anyOf = s.$defs.action.anyOf.slice(0, 14); }),
  variant("first 10 actions, merged when", (s) => { s.$defs.action.anyOf = s.$defs.action.anyOf.slice(0, 10); s.$defs.when.anyOf = mergeByShape(s.$defs.when.anyOf); }),
  variant("first 13 actions, when first 6", (s) => { s.$defs.action.anyOf = s.$defs.action.anyOf.slice(0, 13); s.$defs.when.anyOf = s.$defs.when.anyOf.slice(0, 6); }),
];

for (const [name, schema] of VARIANTS) {
  const t = Date.now();
  try {
    const m = await client.messages.create({
      model: MODEL, max_tokens: 200, thinking: { type: "disabled" },
      messages: [{ role: "user", content: "Answer kind cannot, missing other, wish null if allowed, else the smallest valid wish." }],
      output_config: { format: { type: "json_schema", schema } },
    });
    console.log(`ACCEPTED ${name} (${count(schema)}): ${Date.now() - t} ms, stop ${m.stop_reason}, in ${m.usage.input_tokens} out ${m.usage.output_tokens}`);
  } catch (e) {
    console.log(`REFUSED  ${name} (${count(schema)}): ${Date.now() - t} ms, ${e.status} ${String(e.error?.error?.message ?? e.message).slice(0, 160)}`);
  }
}
