// Natural-language customization is allowed to select only the checked-in
// recipe-v1 vocabulary.  This module never emits profile text, commands,
// package names, URLs, or an artifact claim.
import recipeSchema from "../../src/customize/recipe-v1.schema.json" with { type: "json" };

export const CONFIGURE_MAX_TEXT = 300;
export const CONFIGURE_PLAN_SCHEMA = "org.auros.configuration-plan";
export const CONFIGURE_CAPABILITY = "recipe-v1";

export const BASE_IDS = Object.freeze([...recipeSchema.properties.base.enum]);
export const LOOK_IDS = Object.freeze([...recipeSchema.properties.preferences.properties.look.enum]);
export const THEME_IDS = Object.freeze([...recipeSchema.properties.preferences.properties.theme.enum]);

// Defaults and delivery requirements are the runtime copy of the catalogue in
// src/customize/recipe.py.  configure.test.js compares every field with that
// compiler, so either side changing alone makes the cloud suite red.
export const BASES = Object.freeze({
  desktop: Object.freeze({ look: "rail", theme: "sandstone", requires: ["trusted-builder", "release-gates"] }),
  multilingual: Object.freeze({ look: "rail", theme: "sandstone", requires: ["trusted-builder", "release-gates", "input-method-integration"] }),
  office: Object.freeze({ look: "rail", theme: "slate", requires: ["trusted-builder", "release-gates", "managed-update-channel"] }),
  revive: Object.freeze({ look: "taskbar", theme: "ember", requires: ["trusted-builder", "profile-release-gates"] }),
  "school-kiosk": Object.freeze({ look: "locked", theme: "sandstone", requires: ["trusted-builder", "release-gates", "managed-update-channel"] }),
});

export const UNSUPPORTED_REASONS = Object.freeze([
  "unclear",
  "custom_behavior",
  "hardware_sensor",
  "programs",
  "language",
  "managed_setup",
  "unsupported_preference",
  "other",
]);

// These cheap checks make common unsupported requests immediate and
// deterministic. The closed vocabulary below is the trust boundary.
export function obviousUnsupported(text) {
  const s = String(text).normalize("NFC");
  if (/\b(compass|accelerometer|gyroscope|light[ -]?sensor|tilt(?:ed|ing)?|points?\s+north|faces?\s+north|heading\s+north)\b/iu.test(s)) return "hardware_sensor";
  if (/\b(whenever|every\s+time|when\s+I|if\s+.+\s+then)\b/iu.test(s)) return "custom_behavior";
  if (/https?:\/\/|www\.|\b(?:shell|command|script|source\s+code|terminal|sudo|powershell|bash|registry)\b/iu.test(s)) return "custom_behavior";
  if (/\b(?:install|add|include|bundle|preload)(?:\s+me)?\s+(?:an?\s+)?(?:app|application|program|package|software|[A-Z][\p{L}\p{N}._+-]*)\b/iu.test(s)) return "programs";
  return null;
}

const CONCEPTS = [
  ["base", "desktop", [/\b(?:everyday|ordinary|personal|standard)(?:\s+computer)?\b/giu, /\bdesktop\b/giu]],
  ["base", "multilingual", [/\bmultilingual\b/giu, /\bmultiple\s+languages?\b/giu]],
  ["base", "office", [/\boffice\s+(?:base|computers?)\b/giu, /\b(?:organisation|organization|business|company|workplace)(?:\s+computers?)?\b/giu]],
  ["base", "revive", [/\brevive\b/giu, /\b(?:old|older|slow|slower)(?:\s+computer|\s+laptop)?\b/giu, /\b2\s*gb\b/giu, /\bspinning\s+disk\b/giu]],
  ["base", "school-kiosk", [/\bschool(?:\s+computer)?\b/giu, /\bkiosk\b/giu, /\bbrowser[ -]only\b/giu, /\blocked(?:\s+computer)?\b/giu]],
  ["look", "taskbar", [/\bwindows[ -]like\b/giu, /\blike\s+windows\b/giu, /\btaskbar\b/giu, /\bbottom\s+bar\b/giu]],
  ["look", "dock", [/\bmac[ -]like\b/giu, /\blike\s+a?\s*mac\b/giu, /\bdock\b/giu]],
  ["look", "rail", [/\bbig\s+and\s+simple\b/giu, /\brail\b/giu, /\bside\s+by\s+side\b/giu]],
  ["look", "tiles", [/\bone\s+thing\s+at\s+a\s+time\b/giu, /\btiles?\b/giu, /\bfull[ -]?screen\b/giu]],
  ["theme", "nocturne", [/\blight\s+(?:writing|text)\s+on\s+(?:a\s+)?dark\s+background\b/giu, /\bnocturne\b/giu, /\bdark\s+theme\b/giu]],
  ["theme", "sandstone", [/\bsandstone\b/giu, /\blight\s+theme\b/giu]],
  ["theme", "ember", [/\bember\b/giu]],
  ["theme", "moss", [/\bmoss\b/giu]],
  ["theme", "slate", [/\bslate\b/giu]],
];
const NEUTRAL = new Set("a an the i id want would please make give build create need needs my me for this that it its is should be have has use using with and plus in on of to as kind type computer pc laptop machine os operating system version base starting point arrangement layout laid out colours colors color colour theme look usual keep default familiar warm bar choose chosen something".split(" "));

// Recipe v1 is deliberately a small language. Every substantive word must
// belong to a known recipe concept; otherwise the whole request is unsupported
// and no partial recipe is claimed.
export function analyzeConfigureRequest(text) {
  const immediate = obviousUnsupported(text);
  if (immediate) return { reason: immediate };
  let rest = String(text).normalize("NFKC").toLowerCase();
  if (/\b(?:english|french|spanish|arabic|chinese|mandarin|language)\b/iu.test(rest)) return { reason: "language" };
  const found = { base: new Set(), look: new Set(), theme: new Set() };
  for (const [kind, id, patterns] of CONCEPTS) {
    for (const re of patterns) {
      if (re.test(rest)) found[kind].add(id);
      re.lastIndex = 0;
      rest = rest.replace(re, " ");
    }
  }
  for (const kind of Object.keys(found)) if (found[kind].size > 1) return { reason: "unsupported_preference" };
  const words = rest.normalize("NFKD").replace(/[^\p{L}\p{N}]+/gu, " ").trim().split(/\s+/).filter(Boolean);
  if (words.some((word) => !NEUTRAL.has(word))) return { reason: "other" };
  return {
    reason: null,
    expected: {
      base: [...found.base][0] || "desktop",
      look: [...found.look][0] || "default",
      theme: [...found.theme][0] || "default",
    },
  };
}

export class ConfigureOutputError extends Error {
  constructor(message) { super(message); this.name = "ConfigureOutputError"; }
}

const exactKeys = (value, keys) => {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const got = Object.keys(value).sort();
  const want = [...keys].sort();
  return got.length === want.length && got.every((k, i) => k === want[i]);
};

export function unsupportedPlan(reason) {
  if (!UNSUPPORTED_REASONS.includes(reason)) throw new ConfigureOutputError("unsupported reason is invalid");
  return {
    schema: CONFIGURE_PLAN_SCHEMA,
    version: 1,
    capability: CONFIGURE_CAPABILITY,
    status: "unsupported",
    reason,
  };
}

export function planFromSelection(selection) {
  if (!exactKeys(selection, ["base", "look", "theme"]) || !BASE_IDS.includes(selection.base) ||
      !["default", ...LOOK_IDS].includes(selection.look) || !["default", ...THEME_IDS].includes(selection.theme)) {
    throw new ConfigureOutputError("selection is outside recipe v1");
  }
  if (selection.base === "school-kiosk" && selection.look !== "default") {
    throw new ConfigureOutputError("school-kiosk has a fixed look");
  }
  const preferences = {};
  if (selection.look !== "default") preferences.look = selection.look;
  if (selection.theme !== "default") preferences.theme = selection.theme;
  const recipe = { schema: "org.auros.recipe", version: 1, base: selection.base };
  if (Object.keys(preferences).length) recipe.preferences = preferences;
  const base = BASES[selection.base];
  return {
    schema: CONFIGURE_PLAN_SCHEMA,
    version: 1,
    capability: CONFIGURE_CAPABILITY,
    status: "validated-not-built",
    recipe,
    resolved_preferences: {
      look: preferences.look || base.look,
      theme: preferences.theme || base.theme,
    },
    delivery: {
      state: "validated-not-built",
      artifact_created: false,
      download_ready: false,
      attestation: null,
      requires: [...base.requires],
    },
  };
}
