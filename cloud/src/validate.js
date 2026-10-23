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

// What Claude's answer is constrained to. The wish itself is NOT in the
// grammar: the API compiles at most ~20 alternatives, and the vocabulary has
// 26 shapes (probed 2026-10-23: test/schema-probe.mjs, docs/CLOUD.md). So the
// answer is a small closed envelope and the wish travels inside it as JSON
// text, which checkWish() then holds to the whole shared schema. A wish that
// does not parse or validate becomes "cannot", never a pass-through.
export const WISH_OUTPUT = {
  type: "object",
  additionalProperties: false,
  required: ["kind", "missing", "wish_json"],
  properties: {
    kind: { type: "string", enum: ["wish", "cannot"] },
    missing: { type: "string", enum: ["none", ...MISSING], description: "kind wish: none" },
    wish_json: { type: "string", description: "The wish as JSON text. kind cannot: the nearest wish you can do, or an empty string." },
  },
};

export const ASK_OUTPUT = {
  type: "object",
  additionalProperties: false,
  required: ["help_page_id", "wish_json"],
  properties: {
    help_page_id: { type: "string", enum: [...HELP_IDS, "none"] },
    wish_json: { type: "string", description: "A one-off (when now) wish as JSON text that does what she asked, or an empty string." },
  },
};

export function parseWish(text) {
  if (typeof text !== "string" || text.length === 0 || text.length > 4000) return null;
  try {
    const w = JSON.parse(text);
    return w !== null && typeof w === "object" && !Array.isArray(w) ? w : null;
  } catch { return null; }
}
