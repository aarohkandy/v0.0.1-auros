// AurOS's own addresses. Each name the installed computers and the website use
// is the owner's (docs/ops/DOMAIN.md); today each one passes on to GitHub, so
// moving the files elsewhere later (WP-60, an R2 mirror) changes only this file.
//
//   get.<d>       /                  the installer (counted, capped per wave: gate.js)
//                 /AurOS-Setup.exe   the same
//                 /SHA256SUMS /pieces.txt ...   the latest release's files
//                 /download/<tag>/<file>        the image pieces of a release (never
//                                    capped: an install that has begun must finish)
//   updates.<d>   /apt/<file>        the update channel (releases/download/apt/)
//   <d>, www.<d>  the website (GitHub Pages, passed through); www goes to <d>
//   status.<d>    the website's status page
//   /_ops/...     the gate and the monitor (bearer tokens; state is public)
//
// Pure: everything outside comes in as arguments, so node --test runs it.

const FILES = new Set(["AurOS-Setup.exe", "SHA256SUMS", "pieces.txt", "image.sha256", "image.bytes"]);
const SAFE = /^[A-Za-z0-9._+-]{1,128}$/;
const APT_TAG = /^apt[A-Za-z0-9._-]{0,32}$/;

const plain = (status, body, extra = {}) =>
  new Response(body, { status, headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store", ...extra } });
const json = (status, obj, extra = {}) =>
  new Response(JSON.stringify(obj), { status, headers: { "content-type": "application/json", "cache-control": "no-store", ...extra } });
const to = (url, maxAge = 0) =>
  new Response(null, { status: 302, headers: { location: url, "cache-control": maxAge ? `public, max-age=${maxAge}` : "no-store", "x-content-type-options": "nosniff" } });

// What she reads when the door is shut. Plain words, no names of parts.
const WAIT = {
  closed: ["AurOS is not open to everyone yet.", "We are letting people in a few at a time, so we can look after each one. Please come back soon."],
  full: ["AurOS is full for today.", "We let in only a few people each day, so we can look after each one. Please come back tomorrow."],
  paused: ["AurOS is paused for a little while.", "We are looking at something. Your computer is not affected. Please try again later."],
};
const esc = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

export function waitPage(why, now, site) {
  const [h, p] = WAIT[why];
  const next = new Date(now); next.setUTCHours(24, 0, 0, 0);
  const retry = why === "full" ? Math.max(60, Math.ceil((next - now) / 1000)) : 3600;
  const html = `<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>${esc(h)}</title>
<style>body{background:#F6F2E9;color:#17181A;font:21px/1.5 system-ui,sans-serif;margin:0;padding:48px 24px}main{max-width:36rem;margin:auto}h1{font-size:34px;line-height:1.2}a{color:#0B5D4F}</style></head>
<body><main><h1>${esc(h)}</h1><p>${esc(p)}</p><p><a href="${esc(site)}">Back to the AurOS page</a></p></main></body></html>`;
  return new Response(html, { status: 503, headers: { "content-type": "text/html; charset=utf-8", "retry-after": String(retry), "cache-control": "no-store", "x-auros-gate": why } });
}

async function same(a, b) { // constant time, whatever the lengths
  const h = async (s) => new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s)));
  const [x, y] = [await h(a), await h(b)];
  let d = 0; for (let i = 0; i < x.length; i++) d |= x[i] ^ y[i];
  return d === 0;
}

async function role(request, env) { // "owner", "halt" or null
  const t = (request.headers.get("authorization") || "").replace(/^Bearer /, "");
  if (!t) return null;
  if (env.OPS_TOKEN && await same(t, env.OPS_TOKEN)) return "owner";
  if (env.HALT_TOKEN && await same(t, env.HALT_TOKEN)) return "halt";
  return null;
}

async function ops(request, url, env, gate, now) {
  const p = url.pathname;
  if (p === "/_ops/state" && request.method === "GET") {
    return json(200, await gate.state(now), { "access-control-allow-origin": "*" });
  }
  if (p === "/_ops/history" && request.method === "GET") { // the day-by-day counts (no one's identity), for fleet.csv
    return json(200, await gate.history(), { "access-control-allow-origin": "*" });
  }
  if (request.method !== "POST") return plain(405, "method");
  const who = await role(request, env);
  if (!who) return plain(401, "token");
  const body = async () => { try { const t = await request.text(); return t.length > 8192 ? null : JSON.parse(t || "{}"); } catch { return null; } };
  if (p === "/_ops/pause") { // the owner and the second responder: pausing only
    const b = await body(); if (!b) return plain(400, "body");
    return json(200, await gate.pause(`${who === "halt" ? "second responder" : "owner"}: ${b.reason || "no reason given"}`, now));
  }
  if (who !== "owner") return plain(403, "owner only"); // resume, wave, status: never the second responder
  if (p === "/_ops/resume") return json(200, await gate.resume(now));
  if (p === "/_ops/wave") {
    const b = await body();
    try { return json(200, await gate.setWave(b && b.wave, now)); } catch (e) { return plain(400, String(e.message)); }
  }
  if (p === "/_ops/status") {
    const b = await body(); if (!b || !Array.isArray(b.results)) return plain(400, "body");
    await gate.putStatus({ checked_at: String(b.checked_at || ""), ok: !!b.ok, results: b.results.slice(0, 20).map((r) => ({ check: String(r.check).slice(0, 20), state: String(r.state).slice(0, 10) })) });
    return json(200, { ok: true });
  }
  return plain(404, "not found");
}

async function pages(request, url, env, fetchImpl, path, gate, now) {
  if (path === "/status.json") {
    const s = await gate.getStatus();
    if (s && s.checked_at && now - Date.parse(s.checked_at) < 24 * 3600e3) return json(200, s);
  }
  // Cloudflare keeps the answer for 5 minutes, so GitHub Pages serves each file a few times an hour,
  // not once per visitor (docs/ops/COSTS.md: Pages has a soft 100 GB a month).
  const r = await fetchImpl(env.PAGES_ORIGIN.replace(/\/$/, "") + path,
    { method: request.method === "HEAD" ? "HEAD" : "GET", redirect: "manual", cf: { cacheEverything: true, cacheTtl: 300 } });
  const h = new Headers();
  for (const k of ["content-type", "last-modified", "etag"]) if (r.headers.get(k)) h.set(k, r.headers.get(k));
  h.set("cache-control", "public, max-age=300");
  h.set("x-content-type-options", "nosniff");
  h.set("referrer-policy", "no-referrer");
  return new Response(request.method === "HEAD" ? null : r.body, { status: r.status, headers: h });
}

export async function handle(request, env, gate, fetchImpl = fetch, now = Date.now()) {
  const url = new URL(request.url);
  const host = url.hostname.toLowerCase();
  const D = String(env.DOMAIN || "").toLowerCase();
  const GH = `https://github.com/${env.GH_REPO}/releases`;
  const site = `https://${D}/`;
  if (!D) return plain(500, "not configured");
  if (url.pathname.startsWith("/_ops/")) return ops(request, url, env, gate, now);
  if (!["GET", "HEAD"].includes(request.method)) return plain(405, "method");

  if (host === `www.${D}`) return new Response(null, { status: 301, headers: { location: `https://${D}${url.pathname}${url.search}` } });

  if (host === `get.${D}`) {
    const parts = url.pathname.split("/").filter(Boolean);
    const first = parts[0] || "AurOS-Setup.exe";
    if (parts.length <= 1 && FILES.has(first)) {
      if (first !== "AurOS-Setup.exe") return to(`${GH}/latest/download/${first}`);
      // Only a person fetching the whole file is counted. The monitor's one-byte
      // Range request, a resumed download and a HEAD are not.
      const counted = request.method === "GET" && !request.headers.get("range");
      if (counted) {
        const a = await gate.admit(now);
        if (!a.ok) return waitPage(a.why, now, site);
      } else {
        const s = await gate.state(now);
        if (!s.open) return waitPage(s.paused ? "paused" : s.wave === 0 ? "closed" : "full", now, site);
      }
      return to(`${GH}/latest/download/AurOS-Setup.exe`);
    }
    if (parts[0] === "download" && parts.length === 3 && SAFE.test(parts[1]) && SAFE.test(parts[2])) {
      return to(`${GH}/download/${parts[1]}/${parts[2]}`, 300); // never capped: an install that began must finish
    }
    return plain(404, "not found");
  }

  if (host === `updates.${D}`) {
    const parts = url.pathname.split("/").filter(Boolean);
    if (parts.length === 2 && APT_TAG.test(parts[0]) && SAFE.test(parts[1])) return to(`${GH}/download/${parts[0]}/${parts[1]}`, 300);
    return plain(404, "not found");
  }

  if (host === D || host === `status.${D}`) {
    let path = url.pathname;
    if (host === `status.${D}` && path === "/") path = "/status.html";
    if (!/^\/[A-Za-z0-9._~\/-]*$/.test(path) || path.includes("..")) return plain(400, "path");
    return pages(request, url, env, fetchImpl, path, gate, now);
  }
  return plain(404, "not found");
}
