import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { copyFile, mkdtemp } from "node:fs/promises";
import http from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { runBuildPreflight } from "./build-preflight.mjs";


async function server(handler) {
  const instance = http.createServer(handler);
  await new Promise((resolve) => instance.listen(0, "127.0.0.1", resolve));
  return {
    origin: `http://127.0.0.1:${instance.address().port}`,
    close: () => new Promise((resolve) => instance.close(resolve)),
  };
}

test("enabled preflight uses one read-only OPTIONS request and validates exact CORS", async () => {
  const requests = [];
  const live = await server((request, response) => {
    requests.push([request.method, request.url]);
    response.writeHead(204, {
      "access-control-allow-origin": "https://site.example",
      "access-control-allow-methods": "GET, POST, OPTIONS",
      "access-control-allow-headers": "authorization, content-type, x-auros-challenge, x-auros-proof",
    });
    response.end();
  });
  try {
    await runBuildPreflight(live.origin, "on", "https://site.example");
    assert.deepEqual(requests, [["OPTIONS", "/v1/builds/challenge"]]);
  } finally { await live.close(); }
});

test("enabled preflight rejects wrong CORS without trying a mutating request", async () => {
  const methods = [];
  const live = await server((request, response) => {
    methods.push(request.method);
    response.writeHead(204, { "access-control-allow-origin": "https://other.example" });
    response.end();
  });
  try {
    await assert.rejects(runBuildPreflight(live.origin, "on", "https://site.example"), /exact CORS readiness/);
    assert.deepEqual(methods, ["OPTIONS"]);
  } finally { await live.close(); }
});

test("disabled preflight proves both POST routes fail before admission", async () => {
  const paths = [];
  const live = await server((request, response) => {
    paths.push([request.method, request.url]);
    response.writeHead(503, { "content-type": "application/json", "access-control-allow-origin": "https://site.example" });
    response.end(JSON.stringify({ error: "builds_disabled", download_ready: false }));
  });
  try {
    await runBuildPreflight(live.origin, "off", "https://site.example");
    assert.deepEqual(paths, [["POST", "/v1/builds/challenge"], ["POST", "/v1/builds"]]);
  } finally { await live.close(); }
});

test("preflight refuses redirects and oversized bodies", async () => {
  const redirect = await server((_request, response) => { response.writeHead(302, { location: "https://elsewhere.example/" }); response.end(); });
  try { await assert.rejects(runBuildPreflight(redirect.origin, "on", "https://site.example")); }
  finally { await redirect.close(); }

  const oversized = await server((_request, response) => {
    response.writeHead(503, { "content-type": "application/json" });
    response.end("x".repeat(4097));
  });
  try { await assert.rejects(runBuildPreflight(oversized.origin, "off"), /too large/); }
  finally { await oversized.close(); }
});

test("standalone preflight CLI runs without project dependencies", async () => {
  const live = await server((_request, response) => {
    response.writeHead(204, {
      "access-control-allow-origin": "https://site.example",
      "access-control-allow-methods": "GET, POST, OPTIONS",
      "access-control-allow-headers": "authorization, content-type, x-auros-challenge, x-auros-proof",
    });
    response.end();
  });
  const isolated = await mkdtemp(join(tmpdir(), "auros-build-preflight-"));
  const script = join(isolated, "build-preflight.mjs");
  await copyFile(new URL("./build-preflight.mjs", import.meta.url), script);
  try {
    const result = await new Promise((resolve) => {
      const child = spawn(process.execPath, [script, live.origin, "on", "https://site.example"], {
        cwd: isolated, env: { PATH: process.env.PATH || "" }, stdio: ["ignore", "pipe", "pipe"],
      });
      let output = "";
      child.stdout.on("data", (chunk) => { output += chunk; });
      child.stderr.on("data", (chunk) => { output += chunk; });
      child.on("close", (status) => resolve({ status, output }));
    });
    assert.equal(result.status, 0, result.output);
    assert.match(result.output, /all custom-build on preflight checks passed/);
  } finally { await live.close(); }
});
