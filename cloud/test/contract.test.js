// The shared schema (docs/wish-v1.schema.json) and the schemas we send to the
// API for constrained decoding.
import { test } from "node:test";
import assert from "node:assert/strict";
import { Validator } from "@cfworker/json-schema";
import schema from "../../docs/wish-v1.schema.json" with { type: "json" };
import { ASK_OUTPUT, HELP_IDS, MISSING, WISH_OUTPUT, checkWish, badText, parseWish } from "../src/validate.js";
import { WISH_SYSTEM } from "../src/prompts.js";
import { ALL, NORTH, POTATO } from "./helpers.js";

const valid = (w) => new Validator(schema, "2020-12", false).validate(w).valid;
const W = (when, ...d) => ({ v: 1, when, do: d, enabled: true });

test("the shared schema accepts the vocabulary of AI.md 2 + 7", () => {
  for (const w of [
    POTATO, NORTH,
    W({ type: "now" }, { type: "set_text_size", size: "biggest" }, { type: "pointer_size", size: "bigger" }),
    W({ type: "time", at: "21:30", days: ["sat", "sun"] }, { type: "remind", text: "call my daughter" }),
    W({ type: "every", minutes: 60 }, { type: "show_message", text: "drink some water" }),
    W({ type: "app_opened", app_name: "Firefox" }, { type: "play_sound", sound: "birds" }),
    W({ type: "now" }, { type: "open_website", site: "BBC News" }),
    W({ type: "time", at: "16:00", days: ["sun"] }, { type: "open_app", app_name: "Video calls" }),
    W({ type: "sensor", sensor: "light", below_lux: 20 }, { type: "night_light", on: true }),
    W({ type: "sensor", sensor: "tilt", orientation: "upside_down" }, { type: "fill_screen", text: "hello", seconds: 2 }),
    W({ type: "now" }, { type: "do_not_disturb", on: true, until: "18:00" }, { type: "speak_time" }),
    W({ type: "now" }, { type: "set_wallpaper", color: "#336699" }),
    W({ type: "now" }, { type: "set_wallpaper", theme_name: "Ocean" }),
    W({ type: "battery_below", percent: 15 }, { type: "brightness", set_percent: 30 }),
    W({ type: "idle", minutes: 30 }, { type: "volume", change_percent: -10 }),
  ]) assert.ok(valid(w), JSON.stringify(w));
});

test("the shared schema refuses what v1 removed or bounds", () => {
  for (const w of [
    W({ type: "now" }, { type: "install_program", flathub_id: "org.videolan.VLC" }),
    W({ type: "now" }, { type: "open_website", url: "https://example.com" }),
    W({ type: "now" }, { type: "open_app", app: "firefox.desktop" }),
    { ...POTATO, title: "Potatoes!" },
    { ...POTATO, summary: "x" },
    W({ type: "typed_word", word: "pot" }, { type: "fill_screen", emoji: "🥔", seconds: 3 }),
    W({ type: "typed_word", word: "pot ato" }, { type: "fill_screen", emoji: "🥔", seconds: 3 }),
    W({ type: "now" }, { type: "fill_screen", emoji: "💣", seconds: 3 }),
    W({ type: "now" }, { type: "fill_screen", emoji: "🥔", seconds: 11 }),
    W({ type: "now" }, { type: "fill_screen", emoji: "🥔", text: "both", seconds: 3 }),
    W({ type: "now" }, { type: "brightness", set_percent: 29 }),
    W({ type: "now" }, { type: "volume", set_percent: 19 }),
    W({ type: "now" }, { type: "volume", set_percent: 81 }),
    W({ type: "now" }, { type: "show_message", text: "x".repeat(141) }),
    W({ type: "time", at: "25:00" }, { type: "remind", text: "x" }),
    W({ type: "every", minutes: 4 }, { type: "play_sound", sound: "bell" }),
    W({ type: "now" }),
    W({ type: "now" }, ...Array(5).fill({ type: "speak_time" })),
    W({ type: "now" }, { type: "run_command", command: "rm -rf ~" }),
    W({ type: "now" }, { type: "speak_time", extra: 1 }),
    { ...POTATO, v: 2 },
  ]) assert.ok(!valid(w), JSON.stringify(w));
});

test("free text: the cheap part of AI.md 7.3", () => {
  for (const s of ["call 555 123 4567", "go to evil.com", "see https://x.y", "mail me@x.org", "pay $500", "your PC is infected", "Microsoft support", "your bank", "enter your password", "abc‮def", "zero​width", "50 euros"]) {
    assert.ok(badText(s), s);
  }
  for (const s of ["take my pills", "call my daughter", "drink some water", "Potato time!", "it is 10:30", "feed the cat"]) assert.ok(!badText(s), s);
});

test("checkWish: only what the computer said it has", () => {
  const allowed = { ...ALL, sensors: { compass: true } };
  assert.equal(checkWish(POTATO, allowed), null);
  assert.equal(checkWish(NORTH, { ...ALL, sensors: { compass: false } }), "sensor");
  assert.equal(checkWish(POTATO, { ...allowed, triggers: ["now"] }), "unsupported");
  assert.equal(checkWish(POTATO, { ...allowed, actions: ["speak"] }), "unsupported");
  assert.equal(checkWish(W({ type: "now" }, { type: "show_message", text: "Call 0800 123 456" }), allowed), "text");
  assert.equal(checkWish(POTATO, null), "ask_not_now");
});

// Limits of constrained decoding (structured outputs docs): only these keywords,
// additionalProperties false everywhere, <= 24 optional and <= 16 union parameters.
function walk(node, visit, path = "$") {
  if (Array.isArray(node)) return node.forEach((n, i) => walk(n, visit, `${path}[${i}]`));
  if (node && typeof node === "object") {
    visit(node, path);
    for (const [k, v] of Object.entries(node)) walk(v, visit, `${path}.${k}`);
  }
}
const SUPPORTED = new Set(["type", "properties", "required", "additionalProperties", "items", "enum", "description"]);

for (const [name, s] of [["WISH_OUTPUT", WISH_OUTPUT], ["ASK_OUTPUT", ASK_OUTPUT]]) {
  test(`${name} uses only what constrained decoding supports`, () => {
    let optional = 0;
    let unions = 0;
    const isSchema = (path) => !/\.(properties|\$defs)$/.test(path);
    walk(s, (n, path) => {
      if (!isSchema(path)) return;
      for (const k of Object.keys(n)) assert.ok(SUPPORTED.has(k), `${path}: ${k}`);
      if ("minItems" in n) assert.ok(n.minItems <= 1, path);
      if (n.type === "object") {
        assert.equal(n.additionalProperties, false, path);
        optional += Object.keys(n.properties).filter((p) => !(n.required || []).includes(p)).length;
      }
      if (n.anyOf || Array.isArray(n.type)) unions++;
    });
    assert.ok(optional <= 24, `optional ${optional}`);
    assert.ok(unions <= 16, `unions ${unions}`);
  });
}

test("output enums: every missing value, every help page", () => {
  assert.deepEqual(WISH_OUTPUT.properties.missing.enum, ["none", ...MISSING]);
  assert.deepEqual(ASK_OUTPUT.properties.help_page_id.enum, [...HELP_IDS, "none"]);
  assert.ok(HELP_IDS.includes("printing") && HELP_IDS.includes("index"));
});

test("the prompt carries the whole emoji allowlist and its examples are valid answers", () => {
  for (const e of schema.$defs.emoji.enum) assert.ok(WISH_SYSTEM.includes(e), e);
  assert.ok(WISH_SYSTEM.includes("🥔"));
  const answers = [...WISH_SYSTEM.matchAll(/^\{"kind":.*\}$/gm)].map((m) => JSON.parse(m[0]));
  assert.equal(answers.length, 4);
  for (const a of answers) {
    assert.ok(a.kind === "wish" ? a.missing === "none" : MISSING.includes(a.missing));
    const w = parseWish(a.wish_json);
    if (a.kind === "wish") assert.equal(checkWish(w, { ...ALL, sensors: { compass: true } }), null, a.wish_json);
    else assert.equal(a.wish_json, "");
  }
  assert.equal(parseWish(answers[0].wish_json).do[0].emoji, "🥔");
});
