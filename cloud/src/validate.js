// What crosses the boundary: what the computer sends, and what Claude sends
// back. The engine on her computer re-checks every wish with the same schema
// and more rules (docs/AI.md 7.1): these checks save cost and keep answers
// useful; they are not the safety layer.
import { Validator } from "@cfworker/json-schema";
import wishSchema from "../../docs/wish-v1.schema.json" with { type: "json" };
import help from "./help.gen.js";

export { wishSchema };
export const MAX_TEXT = 300; // characters of her wish or question (AI.md 7.10)

// Why a wish can't be made. The device writes the sentence for each value.
export const MISSING = [
  "unclear", // gibberish, empty, or she must say it another way
  "question", // a question, not a wish: the device offers Ask AurOS
  "compass", "tilt", "light", // this computer has no such sensor
  "install", // installing programs (AI.md 7.2)
  "files", // deleting, moving, renaming, sending files
  "messages", // e-mail, messages, calls
  "money", // buying, paying, banking
  "private", // passwords, personal information
  "security", // network, firewall, updates, protections, accounts, remote access
  "other", // anything else outside the vocabulary
];
export const HELP_IDS = help.map((p) => p.id);

const v = (schema) => new Validator(schema, "2020-12", false);
const wishValidator = v(wishSchema);
const LANG = { type: "string", maxLength: 35, pattern: "^[A-Za-z]{2,3}([_-][A-Za-z0-9]{2,8}){0,3}$" };
const NAMES = { type: "array", maxItems: 40, items: { type: "string", pattern: "^[a-z_]{1,32}$" } };

// Unknown fields are ignored (a newer computer may send more); known ones are strict.
const requests = {
  register: v({
    type: "object",
    required: ["install_id", "consent_version", "pow"],
    properties: {
      install_id: { type: "string", pattern: "^[A-Za-z0-9_-]{22,64}$" },
      version: { type: "string", maxLength: 32, pattern: "^[A-Za-z0-9._+-]*$" },
      consent_version: { type: "string", pattern: "^[A-Za-z0-9._-]{1,16}$" },
      pow: {
        type: "object",
        required: ["day", "nonce"],
        properties: {
          day: { type: "string", pattern: "^\\d{4}-\\d{2}-\\d{2}$" },
          nonce: { type: "string", pattern: "^[A-Za-z0-9]{1,32}$" },
        },
      },
    },
  }),
  wish: v({
    type: "object",
    required: ["text", "schema"],
    properties: {
      text: { type: "string", minLength: 1, maxLength: MAX_TEXT },
      lang: LANG,
      schema: {
        type: "object",
        required: ["v", "triggers", "actions"],
        properties: { v: { const: 1 }, triggers: NAMES, actions: NAMES },
      },
      sensors: {
        type: "object",
        properties: { compass: { type: "boolean" }, tilt: { type: "boolean" }, light: { type: "boolean" } },
      },
    },
  }),
  ask: v({
    type: "object",
    required: ["question"],
    properties: {
      question: { type: "string", minLength: 1, maxLength: MAX_TEXT },
      lang: LANG,
      context: { type: "object", properties: { look: { enum: ["taskbar", "dock", "rail", "tiles"] } } },
    },
  }),
};

export function checkRequest(kind, body) {
  return body !== null && typeof body === "object" && !Array.isArray(body) && requests[kind].validate(body).valid;
}

// The cheap part of AI.md 7.3 (the device also checks every word is hers).
const BAD_TEXT = [
  /[​-‏‪-‮⁠-⁩﻿]/u, // zero-width and bidi controls
  /\p{N}(?:[\s().\-‐-―]*\p{N}){4,}/u, // 5 or more digits: phone and account numbers
  /https?:|www\.|:\/\/|@/i, // web and e-mail addresses
  /\b[\p{L}\p{N}-]+\.(com|net|org|info|biz|io|co|us|uk|ru|cn|xyz|top|app|dev|site|online|support|help|live|shop|store|link|click|tech|pro|me|tk)\b/iu,
  /[$€£¥₹]\s*\p{N}|\p{N}\s*(?:[$€£¥₹]|dollars?\b|euros?\b|pounds?\b)/iu, // money
  /\b(virus|infected|hacked|police|bank|microsoft|apple|auros|support|password)/iu,
];
export const badText = (s) => BAD_TEXT.some((re) => re.test(s.normalize("NFC")));

// null when the wish may go back to her computer, else a short reason (logged, never shown).
// `allowed` = the request's {triggers, actions, sensors}; absent for Ask, which
// may only offer a one-off ("now") wish.
export function checkWish(wish, allowed) {
  if (!wishValidator.validate(wish).valid) return "schema";
  const w = wish.when;
  if (!allowed) {
    if (w.type !== "now") return "ask_not_now";
  } else {
    if (!allowed.triggers.includes(w.type)) return "unsupported";
    if (w.type === "sensor" && !(allowed.sensors || {})[w.sensor]) return "sensor";
    if (wish.do.some((a) => !allowed.actions.includes(a.type))) return "unsupported";
  }
  if (wish.do.some((a) => typeof a.text === "string" && badText(a.text))) return "text";
  return null;
}

// Constrained decoding takes a subset of JSON Schema (no numeric or length
// limits, no pattern): those keywords move into the description, which Claude
// reads, and checkWish enforces them. Unlike the SDK's own transform this
// keeps enum and const, so the vocabulary itself is enforced while Claude writes.
const DROP = ["minimum", "maximum", "minLength", "maxLength", "maxItems", "pattern", "uniqueItems"];
function forApi(node) {
  if (Array.isArray(node)) return node.map(forApi);
  if (node === null || typeof node !== "object") return node;
  const out = {};
  const moved = [];
  for (const [k, val] of Object.entries(node)) {
    if (k === "$schema" || k === "title") continue;
    if (DROP.includes(k) || (k === "minItems" && val > 1)) moved.push(`${k}: ${JSON.stringify(val)}`);
    else if (k === "properties" || k === "$defs") out[k] = Object.fromEntries(Object.entries(val).map(([n, s]) => [n, forApi(s)]));
    else out[k] = forApi(val);
  }
  if (moved.length) out.description = `${out.description ? out.description + " " : ""}{${moved.join(", ")}}`;
  return out;
}

const { $defs, ...wishBody } = wishSchema;
const outputSchema = (properties) => forApi({
  type: "object",
  additionalProperties: false,
  required: Object.keys(properties),
  properties,
  $defs: { ...$defs, wish: wishBody },
});
const WISH_OR_NULL = { anyOf: [{ $ref: "#/$defs/wish" }, { type: "null" }] };

export const WISH_OUTPUT = outputSchema({
  kind: { type: "string", enum: ["wish", "cannot"] },
  missing: { type: "string", enum: ["none", ...MISSING], description: "kind wish: none" },
  wish: { ...WISH_OR_NULL, description: "kind wish: the wish. kind cannot: the nearest wish you can do, or null" },
});

export const ASK_OUTPUT = outputSchema({
  help_page_id: { type: "string", enum: [...HELP_IDS, "none"] },
  wish: { ...WISH_OR_NULL, description: "a one-off (when now) wish that does what she asked, or null" },
});
