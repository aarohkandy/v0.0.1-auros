import { realpathSync } from "node:fs";
import { fileURLToPath } from "node:url";

const exactServiceOrigin = (raw) => {
  let url;
  try { url = new URL(raw); } catch { throw new Error("build API must be one exact HTTPS origin"); }
  const local = url.protocol === "http:" && url.hostname === "127.0.0.1";
  if ((!local && url.protocol !== "https:") || url.origin !== raw || url.username || url.password || url.pathname !== "/" || url.search || url.hash) {
    throw new Error("build API must be one exact HTTPS origin");
  }
  return url.origin;
};

const exactPublishedOrigin = (raw) => {
  let url;
  try { url = new URL(raw); } catch { throw new Error("build site must be one exact HTTPS origin"); }
  if (url.protocol !== "https:" || url.origin !== raw || url.username || url.password || url.pathname !== "/" || url.search || url.hash) {
    throw new Error("build site must be one exact HTTPS origin");
  }
  return url.origin;
};

async function bounded(response, limit = 4096) {
  const declared = Number(response.headers.get("content-length"));
  if (Number.isFinite(declared) && declared > limit) throw new Error("build preflight response is too large");
  if (!response.body) return "";
  const reader = response.body.getReader();
  const chunks = [];
  let length = 0;
  for (;;) {
    const { value, done } = await reader.read();
    if (done) break;
    length += value.byteLength;
    if (length > limit) { await reader.cancel(); throw new Error("build preflight response is too large"); }
    chunks.push(value);
  }
  const out = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) { out.set(chunk, offset); offset += chunk.byteLength; }
  return new TextDecoder("utf-8", { fatal: true }).decode(out);
}

async function preflightFetch(url, init) {
  const response = await fetch(url, { ...init, redirect: "error", signal: AbortSignal.timeout(10_000) });
  const text = await bounded(response);
  return { response, text };
}

export async function runBuildPreflight(apiRaw, mode, siteRaw = "") {
  const api = exactServiceOrigin(apiRaw);
  if (!['on', 'off'].includes(mode)) throw new Error("build preflight mode must be on or off");
  const site = siteRaw ? exactPublishedOrigin(siteRaw) : "https://disabled.invalid";
  if (mode === "on") {
    const { response, text } = await preflightFetch(api + "/v1/builds/challenge", {
      method: "OPTIONS",
      headers: { origin: site, "access-control-request-method": "POST", "access-control-request-headers": "content-type,x-auros-challenge,x-auros-proof" },
    });
    const methods = (response.headers.get("access-control-allow-methods") || "").split(/\s*,\s*/);
    const headers = (response.headers.get("access-control-allow-headers") || "").toLowerCase().split(/\s*,\s*/);
    if (response.status !== 204 || text !== "" || response.headers.get("access-control-allow-origin") !== site || !methods.includes("POST") ||
        !["content-type", "x-auros-challenge", "x-auros-proof"].every((name) => headers.includes(name))) {
      throw new Error(`enabled build preflight was not an exact CORS readiness response (${response.status})`);
    }
    return;
  }

  for (const path of ["/v1/builds/challenge", "/v1/builds"]) {
    const { response, text } = await preflightFetch(api + path, {
      method: "POST", headers: { origin: site, "content-type": "application/json" },
      body: '{"schema":"org.auros.recipe","version":1,"base":"desktop"}',
    });
    let body;
    try { body = JSON.parse(text); } catch { throw new Error(`disabled ${path} returned unreadable JSON`); }
    if (response.status !== 503 || body?.error !== "builds_disabled" || body?.download_ready !== false) {
      throw new Error(`disabled ${path} did not fail closed (${response.status})`);
    }
    const allowed = response.headers.get("access-control-allow-origin");
    if (siteRaw && allowed !== site) throw new Error(`disabled ${path} did not return the exact configured CORS origin`);
    if (!siteRaw && allowed !== null) throw new Error(`disabled ${path} unexpectedly allowed an origin`);
  }
}

if (process.argv[1] && realpathSync(process.argv[1]) === realpathSync(fileURLToPath(import.meta.url))) {
  try {
    await runBuildPreflight(process.argv[2] || "", process.argv[3] || "", process.argv[4] || "");
    console.log(`all custom-build ${process.argv[3]} preflight checks passed`);
  } catch (error) {
    console.error("FAIL custom-build preflight — " + (error instanceof Error ? error.message : String(error)));
    process.exitCode = 1;
  }
}
