import assert from "node:assert/strict";
import { chmodSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { generateKeyPairSync } from "node:crypto";
import test from "node:test";

const workflow = readFileSync(new URL("../../.github/workflows/cloud.yml", import.meta.url), "utf8");

function stepScript(name) {
  const lines = workflow.split("\n");
  const start = lines.indexOf(`      - name: ${name}`);
  assert.notEqual(start, -1, `workflow step ${name} exists`);
  const run = lines.indexOf("        run: |", start);
  assert.ok(run > start, `workflow step ${name} has a literal run block`);
  const body = [];
  for (let i = run + 1; i < lines.length && (lines[i].startsWith("          ") || lines[i] === ""); i++) {
    body.push(lines[i].startsWith("          ") ? lines[i].slice(10) : "");
  }
  return body.join("\n");
}

const configScript = stepScript("Custom delivery configuration");
const refreshScript = stepScript("Refresh custom delivery secrets before enabled deploy");
const deployScript = stepScript("Deploy");
const workerSecretsScript = stepScript("Worker secrets (stdin only, never echoed)");
const buildPreflightScript = stepScript("Custom delivery live preflight");
const secretNames = ["BUILD_NETWORK_SECRET", "BUILD_CHALLENGE_SECRET", "GITHUB_APP_PRIVATE_KEY"];
const variableNames = ["GITHUB_APP_ID", "GITHUB_APP_INSTALLATION_ID"];
const allNames = [...secretNames, ...variableNames];
const generatedPrivateKey = generateKeyPairSync("rsa", { modulusLength: 2048 }).privateKey;
const testPrivateKey = generatedPrivateKey.export({ format: "pem", type: "pkcs8" }).toString();
const testPkcs1Key = generatedPrivateKey.export({ format: "pem", type: "pkcs1" }).toString();
const sentinel = {
  BUILD_NETWORK_SECRET: "network-do-not-print-0123456789abcdef",
  BUILD_CHALLENGE_SECRET: "challenge-do-not-print-0123456789abcdef",
  GITHUB_APP_PRIVATE_KEY: testPrivateKey,
  GITHUB_APP_ID: "123456",
  GITHUB_APP_INSTALLATION_ID: "789012",
};

function runConfig({ enabled, absent = [], overrides = {}, workerSecrets = secretNames, listFails = false, malformed = false, deployCredentials = true, config = "" }) {
  const dir = mkdtempSync(join(tmpdir(), "auros-cloud-wiring-"));
  writeFileSync(join(dir, "wrangler.toml"), config || `BUILD_REQUESTS_ENABLED = "${enabled ? 1 : 0}"\nBUILD_ALLOWED_ORIGIN = "https://example.test"\nBUILD_GITHUB_REPOSITORY = "aarohkandy/v0.0.1-auros"\n`);
  const bin = join(dir, "bin");
  mkdirSync(bin);
  const fakeNpx = join(bin, "npx");
  writeFileSync(fakeNpx, `#!/bin/sh\nif [ "${"$"}FAKE_LIST_FAIL" = 1 ]; then exit 42; fi\nprintf '%s' "${"$"}FAKE_SECRET_LIST"\n`);
  chmodSync(fakeNpx, 0o700);
  const fakeTimeout = join(bin, "timeout");
  writeFileSync(fakeTimeout, "#!/bin/sh\nshift\nexec \"$@\"\n");
  chmodSync(fakeTimeout, 0o700);
  const values = { ...sentinel, ...overrides };
  for (const name of absent) values[name] = "";
  const result = spawnSync("bash", ["--noprofile", "--norc", "-e", "-o", "pipefail", "-c", configScript], {
    cwd: dir,
    encoding: "utf8",
    env: {
      ...process.env,
      PATH: `${bin}:${process.env.PATH}`,
      WRANGLER: "wrangler@test",
      BUILD_PUBLISHED_ORIGIN: "https://example.test",
      HAVE_DEPLOY_CREDENTIALS: deployCredentials ? "true" : "false",
      GITHUB_OUTPUT: join(dir, "github-output"),
      FAKE_LIST_FAIL: listFails ? "1" : "0",
      FAKE_SECRET_LIST: malformed ? "not-json" : JSON.stringify(workerSecrets.map((name) => ({ name, type: "secret_text" }))),
      ...values,
    },
  });
  const output = result.stdout + result.stderr;
  for (const value of Object.values(values).filter(Boolean)) assert.equal(output.includes(value), false, "configuration output does not reveal a setting value");
  return { ...result, output };
}

function commandHarness(enabled) {
  const dir = mkdtempSync(join(tmpdir(), "auros-cloud-order-"));
  writeFileSync(join(dir, "wrangler.toml"), `BUILD_REQUESTS_ENABLED = "${enabled ? 1 : 0}"\nBUILD_ALLOWED_ORIGIN = "https://example.test"\nBUILD_GITHUB_REPOSITORY = "aarohkandy/v0.0.1-auros"\n`);
  const bin = join(dir, "bin");
  mkdirSync(bin);
  const fakeNpx = join(bin, "npx");
  writeFileSync(fakeNpx, `#!/bin/sh
last=""
for arg in "$@"; do last="$arg"; done
case " $* " in
  *" secret list "*) printf '%s' "$FAKE_SECRET_LIST" ;;
  *" secret put "*) cat >/dev/null; printf 'secret:%s\\n' "$last" >> "$COMMAND_RECORD" ;;
  *" deploy "*) printf 'deploy:%s\\n' "$*" >> "$COMMAND_RECORD"; printf '%s\\n' 'https://auros-wishes.test.workers.dev' ;;
  *) exit 97 ;;
esac
`);
  chmodSync(fakeNpx, 0o700);
  const fakeTimeout = join(bin, "timeout");
  writeFileSync(fakeTimeout, "#!/bin/sh\nshift\nexec \"$@\"\n");
  chmodSync(fakeTimeout, 0o700);
  const record = join(dir, "commands");
  const baseEnv = {
    ...process.env,
    ...sentinel,
    PATH: `${bin}:${process.env.PATH}`,
    WRANGLER: "wrangler@test",
      BUILD_PUBLISHED_ORIGIN: "https://example.test",
    HAVE_DEPLOY_CREDENTIALS: "true",
    FAKE_SECRET_LIST: JSON.stringify(["TOKEN_SECRET", ...secretNames].map((name) => ({ name, type: "secret_text" }))),
    COMMAND_RECORD: record,
    GITHUB_OUTPUT: join(dir, "github-output"),
    GITHUB_ENV: join(dir, "github-env"),
    DEPLOYMENT_SOURCE_SHA: "a".repeat(40),
    DEPLOYMENT_NONCE: "b".repeat(64),
    SMOKE_CORRELATION_TOKEN: "c".repeat(64),
    GITHUB_STEP_SUMMARY: join(dir, "summary"),
    ANTHROPIC_WORKSPACE_ID: "workspace-test",
    ANTHROPIC_API_KEY: "anthropic-do-not-print",
    ALERT_URL: "",
    CUSTOM_DELIVERY_ENABLED: enabled ? "true" : "false",
  };
  const fakeGh = join(bin, "gh");
  writeFileSync(fakeGh, '#!/bin/sh\nprintf "%s\\n" "$GITHUB_SHA"\n');
  chmodSync(fakeGh, 0o700);
  baseEnv.GITHUB_SHA = "a".repeat(40);
  baseEnv.GITHUB_REPOSITORY = "aarohkandy/v0.0.1-auros";
  const run = (script) => {
    script = script.replaceAll("../tools/site/require_current_main.sh", new URL("../../tools/site/require_current_main.sh", import.meta.url).pathname);
    const result = spawnSync("bash", ["--noprofile", "--norc", "-e", "-o", "pipefail", "-c", script], { cwd: dir, encoding: "utf8", env: baseEnv });
    assert.equal(result.status, 0, result.stdout + result.stderr);
    const output = result.stdout + result.stderr;
    for (const secret of [sentinel.BUILD_NETWORK_SECRET, sentinel.BUILD_CHALLENGE_SECRET, sentinel.GITHUB_APP_PRIVATE_KEY, baseEnv.ANTHROPIC_API_KEY]) {
      assert.equal(output.includes(secret), false, "step output does not reveal a secret value");
    }
  };
  return { dir, record, run };
}

test("disabled custom delivery permits absent configuration without querying Worker secrets", () => {
  const result = runConfig({ enabled: false, absent: allNames, listFails: true });
  assert.equal(result.status, 0, result.output);
  assert.match(result.stdout, /Custom delivery remains disabled/);
});

for (const missing of allNames) {
  test(`enabled custom delivery refuses absent ${missing}`, () => {
    const result = runConfig({ enabled: true, absent: [missing] });
    assert.notEqual(result.status, 0);
    assert.match(result.output, new RegExp(`settings are absent:.*${missing}`));
  });
}

test("enabled custom delivery refuses absent deployment credentials", () => {
  const result = runConfig({ enabled: true, deployCredentials: false });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /deploy credentials are absent/);
});

test("enabled custom delivery validates credential formats and source routing", () => {
  for (const [name, value, message] of [
    ["BUILD_NETWORK_SECRET", "short", /at least 32 characters/],
    ["BUILD_CHALLENGE_SECRET", "short", /at least 32 characters/],
    ["GITHUB_APP_ID", "0", /positive decimal integers/],
    ["GITHUB_APP_INSTALLATION_ID", "12x", /positive decimal integers/],
    ["GITHUB_APP_PRIVATE_KEY", "not a pem", /not a valid RSA/],
  ]) {
    const result = runConfig({ enabled: true, overrides: { [name]: value } });
    assert.notEqual(result.status, 0);
    assert.match(result.output, message);
  }
  const badOrigin = runConfig({ enabled: true, config: 'BUILD_REQUESTS_ENABLED = "1"\nBUILD_ALLOWED_ORIGIN = "https://example.test/path"\nBUILD_GITHUB_REPOSITORY = "aarohkandy/v0.0.1-auros"\n' });
  assert.notEqual(badOrigin.status, 0);
  assert.match(badOrigin.output, /one exact HTTPS origin/);
  const badRepository = runConfig({ enabled: true, config: 'BUILD_REQUESTS_ENABLED = "1"\nBUILD_ALLOWED_ORIGIN = "https://example.test"\nBUILD_GITHUB_REPOSITORY = "someone/else"\n' });
  assert.notEqual(badRepository.status, 0);
  assert.match(badRepository.output, /not the fixed reviewed repository/);
  for (const origin of ["https://example.test:99999", "https://-", "https://example.test/"]) {
    const invalid = runConfig({ enabled: true, config: `BUILD_REQUESTS_ENABLED = "1"\nBUILD_ALLOWED_ORIGIN = "${origin}"\nBUILD_GITHUB_REPOSITORY = "aarohkandy/v0.0.1-auros"\n` });
    assert.notEqual(invalid.status, 0);
    assert.match(invalid.output, /one exact HTTPS origin/);
  }
});

test("custom delivery rejects an ambiguous or invalid source switch", () => {
  for (const config of [
    'BUILD_REQUESTS_ENABLED = "yes"\n',
    'BUILD_REQUESTS_ENABLED = "0"\nBUILD_REQUESTS_ENABLED = "1"\n',
    'BUILD_REQUESTS_ENABLED = "1"\nBUILD_REQUESTS_ENABLED = "1"\n',
  ]) {
    const result = runConfig({ enabled: true, config });
    assert.notEqual(result.status, 0);
    assert.match(result.output, /must appear exactly once/);
  }
});

for (const missing of secretNames) {
  test(`enabled custom delivery refuses unstaged Worker secret ${missing}`, () => {
    const result = runConfig({ enabled: true, workerSecrets: secretNames.filter((name) => name !== missing) });
    assert.notEqual(result.status, 0);
    assert.match(result.output, new RegExp(`Worker secret ${missing} is not staged`));
  });
}

test("enabled custom delivery refuses an unreadable or malformed Worker secret list", () => {
  const unreadable = runConfig({ enabled: true, listFails: true });
  assert.notEqual(unreadable.status, 0);
  assert.match(unreadable.output, /secret names could not be read/);
  const malformed = runConfig({ enabled: true, malformed: true });
  assert.notEqual(malformed.status, 0);
  assert.match(malformed.output, /Worker secret BUILD_NETWORK_SECRET is not staged/);
});

test("enabled custom delivery accepts complete settings after disabled staging", () => {
  const result = runConfig({ enabled: true });
  assert.equal(result.status, 0, result.output);
  const escapedPkcs1 = runConfig({ enabled: true, overrides: { GITHUB_APP_PRIVATE_KEY: testPkcs1Key.replaceAll("\n", "\\n") } });
  assert.equal(escapedPkcs1.status, 0, escapedPkcs1.output);
});

test("enabled transition refreshes custom secrets before the deploy command", () => {
  const harness = commandHarness(true);
  harness.run(configScript);
  harness.run(refreshScript);
  harness.run(deployScript);
  const commands = readFileSync(harness.record, "utf8").trim().split("\n");
  assert.deepEqual(commands.slice(0, 3), secretNames.map((name) => `secret:${name}`));
  assert.match(commands[3], /^deploy:/);
  assert.match(commands[3], /--var ANTHROPIC_WORKSPACE_ID:workspace-test/);
  assert.match(commands[3], /--var GITHUB_APP_ID:123456/);
  assert.match(commands[3], /--var GITHUB_APP_INSTALLATION_ID:789012/);
});

test("disabled deployment stages supplied custom secrets only after deploy", () => {
  const harness = commandHarness(false);
  harness.run(configScript);
  harness.run(deployScript);
  harness.run(workerSecretsScript);
  const commands = readFileSync(harness.record, "utf8").trim().split("\n");
  const relevant = commands.filter((line) => line.startsWith("deploy:") || secretNames.some((name) => line === `secret:${name}`));
  assert.match(relevant[0], /^deploy:/);
  assert.deepEqual(relevant.slice(1), secretNames.map((name) => `secret:${name}`));
});

test("workflow wires every setting through its bounded channel", () => {
  for (const name of secretNames) {
    assert.ok(workflow.includes(name + ": ${{ secrets." + name + " }}"));
    assert.match(workflow, new RegExp(`printf '%s' "\\$${name}" \\| timeout 120 npx --yes "\\$WRANGLER" secret put ${name}`));
  }
  for (const name of variableNames) {
    assert.ok(workflow.includes(name + ": ${{ vars." + name + " }}"));
    assert.match(workflow, new RegExp(`--var "${name}:\\$${name}"`));
  }
  assert.match(workflow, /set \+x/);
  const config = readFileSync(new URL("../wrangler.toml", import.meta.url), "utf8");
  assert.match(config, /^BUILD_REQUESTS_ENABLED\s*=\s*"0"$/m);
});

test("deployment runs custom-build preflight independently in the configured mode", () => {
  assert.match(buildPreflightScript, /node test\/build-preflight\.mjs "\$AUROS_WISHES_URL" "\$mode" "\$BUILD_ALLOWED_ORIGIN"/);
  assert.match(buildPreflightScript, /\[ "\$CUSTOM_DELIVERY_ENABLED" != true \] \|\| mode=on/);
  assert.ok(workflow.indexOf("- name: Custom delivery live preflight") < workflow.indexOf("- name: Live end to end, with its logs captured"));
});

test("enabled deployment refuses origin differing from versioned publishing authority", () => {
  const result = runConfig({ enabled: true, overrides: { BUILD_PUBLISHED_ORIGIN: "https://different.test" } });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /differs from versioned publishing settings/);
  assert.match(deployScript, /--var "BUILD_ALLOWED_ORIGIN:\$BUILD_PUBLISHED_ORIGIN"/);
});

test("enabled publication cannot accept skipped smoke or missing log assertions", () => {
  const live = stepScript("Live end to end, with its logs captured");
  assert.match(live, /URL="\$AUROS_WISHES_URL"/);
  assert.ok(live.indexOf('node test/publication-evidence.mjs') < live.indexOf('if [ "$smoke" = 3 ]'));
  assert.ok(live.indexOf('log_proven=1') > live.indexOf('node test/log-assert.mjs'));
});

test("every Worker mutation step checks current main before any wrangler call", () => {
  for (const script of [refreshScript, deployScript, workerSecretsScript]) {
    assert.ok(script.indexOf('bash ../tools/site/require_current_main.sh') >= 0);
    assert.ok(script.indexOf('bash ../tools/site/require_current_main.sh') < script.indexOf('npx'));
  }
  assert.match(workflow, /group: cloud-\$\{\{ github.ref \}\}\n  cancel-in-progress: false/);
});

test("monitor and publisher share versioned public addresses", () => {
  const monitor = readFileSync(new URL('../../.github/workflows/monitor.yml', import.meta.url), 'utf8');
  assert.match(monitor, /tools\/site\/publish_settings.py --github-env/);
  assert.match(monitor, /s\/\^AUROS_WISHES_URL=\/WISHES_URL=\/p/);
  assert.doesNotMatch(monitor, /vars\.AUROS_(WISHES_URL|DOMAIN)/);
});


test("cloud evidence binds deployed identity before smoke and proof upload", () => {
  assert.ok(workflow.indexOf('name: Public endpoint identifies the deployed Worker') < workflow.indexOf('name: Live end to end, with its logs captured'));
  assert.ok(workflow.indexOf('name: Bind reusable cloud publication proof') > workflow.indexOf('node test/publication-evidence.mjs'));
  assert.match(deployScript, /DEPLOYMENT_SOURCE_SHA:\$DEPLOYMENT_SOURCE_SHA/);
  assert.match(deployScript, /DEPLOYMENT_NONCE:\$DEPLOYMENT_NONCE/);
  assert.match(workerSecretsScript, /secret put SMOKE_CORRELATION_TOKEN/);
  assert.doesNotMatch(deployScript, /--var.*SMOKE_CORRELATION_TOKEN/);
  assert.match(workflow, /name: cloud-publication-proof-\$\{\{ github.run_attempt \}\}/);
  assert.match(workflow, /src\/customize\/recipe-v1.schema.json/);
});
