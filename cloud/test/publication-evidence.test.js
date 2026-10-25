import assert from "node:assert/strict";
import test from "node:test";
import { publicationEvidence } from "./publication-evidence.mjs";

test("enabled publishing requires both real smoke and log assertions", () => {
  assert.doesNotThrow(() => publicationEvidence(true, 0, true));
  for (const [exit, logs] of [[3, true], [0, false], [1, true], [124, false]]) {
    assert.throws(() => publicationEvidence(true, exit, logs), /skipped evidence is insufficient/);
  }
});
test("disabled incident path retains its existing smoke handling", () => {
  assert.doesNotThrow(() => publicationEvidence(false, 3, false));
});
test("malformed evidence cannot become a pass", () => {
  for (const args of [["true", 0, true], [true, NaN, true], [true, -1, true], [true, 0, "yes"]]) {
    assert.throws(() => publicationEvidence(...args), /Invalid/);
  }
});
