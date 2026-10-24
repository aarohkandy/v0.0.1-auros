#!/usr/bin/env node
// The website's front door, checked the way she meets it.
//
//   node tools/site/check.mjs                      check website/
//   node tools/site/check.mjs --dir <folder>       check another copy
//   node tools/site/check.mjs --shots <folder>     also save screenshots
//
// Stamps a copy with the repository it is published from (tools/site/
// stamp.sh, as pages.yml does), serves it under /<repository name>/ (as
// GitHub Pages does, so a link that starts with "/" breaks here too),
// drives Chromium with Playwright, and fails if:
//   - "Get AurOS" is not wholly in the first screen at 1366x768 and
//     390x844 (Windows, and with script off), or is shorter than 56px;
//   - its href is not the release address;
//   - any visible text is under 18px, or any text (buttons included) is
//     under 4.5:1 against its background, from the computed styles;
//   - any internal link, image, script, stylesheet or #anchor is missing;
//   - a Mac, iPhone, Android, Chromebook or Linux visit does not show
//     "Open this page on the Windows computer...", or a Windows-on-ARM or
//     32-bit Windows visit does not show "AurOS can't go on this
//     computer", each with an email link and a copy button;
//   - a page asks any server but this one for anything;
//   - the page scrolls sideways at 320px or at 200% zoom;
//   - advanced.html (the page the installer names when it cannot go on)
//     is missing, unlinked from the footer, or fails the same text floors;
//   - published unstamped on another repository's GitHub Pages, the
//     button does not find that repository's release (assets/door.js);
//   - a promise on any page is not what the code does: sizes and times
//     (data-fact, from preflight.h and wizard.c), behaviour (data-claim:
//     BitLocker, the start-up hold, the charger, the release gate),
//     screen words (data-ui, in the installer's or desktop's source), or
//     an old claim that is false for this build comes back (STALE);
//   - a GitHub address is not this repository's, or a "Get AurOS" link
//     is not the download itself (the e2e run presses the first one);
//   - any real page misses the text floors; the four looks don't switch
//     with a press and the arrow keys, or don't all show without script;
//   - the first screen costs more than 300 KB.
//
// Playwright: uses the one Node can find (a local install, the global
// one, or the folder named by PLAYWRIGHT_DIR).

import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import os from "node:os";
import crypto from "node:crypto";
import { execSync, execFileSync } from "node:child_process";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, "..", "..");
// The repository the site is published from (AUROS_REPO, else
// GITHUB_REPOSITORY, else the origin remote): never written here.
const repoSh = (what) => execFileSync("sh", [path.join(REPO, "tools", "repo.sh"), what]).toString().trim();
const AUROS_REPO = repoSh("repo");
// The installer: GitHub's release address, or get.<domain> once AUROS_DOMAIN is set (tools/repo.sh).
const RELEASE_URL = repoSh("get");
const PAGES_HOST = repoSh("host");                      // owner.github.io/name/
const BASE = PAGES_HOST.slice(PAGES_HOST.indexOf("/"));  // /name/, or / for owner.github.io

const argv = process.argv.slice(2);
const opt = (name) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : undefined; };
const SRC = path.resolve(opt("--dir") || path.join(REPO, "website"));
// What is served and read: the folder as the deploy publishes it.
const SITE = fs.mkdtempSync(path.join(os.tmpdir(), "auros-site-"));
process.stdout.write(execFileSync("sh", [path.join(REPO, "tools", "site", "stamp.sh"), SRC, SITE]));
process.on("exit", () => fs.rmSync(SITE, { recursive: true, force: true }));
const SHOTS = opt("--shots") ? path.resolve(opt("--shots")) : null;

function loadPlaywright() {
  const bases = [];
  if (process.env.PLAYWRIGHT_DIR) bases.push(path.join(process.env.PLAYWRIGHT_DIR, "node_modules"), process.env.PLAYWRIGHT_DIR);
  bases.push(process.cwd(), HERE);
  try { bases.push(execSync("npm root -g", { stdio: ["ignore", "pipe", "ignore"] }).toString().trim()); } catch {}
  for (const b of bases) {
    try { return createRequire(path.join(b, "noop.js"))("playwright"); } catch {}
    try { return createRequire(path.join(b, "node_modules", "noop.js"))("playwright"); } catch {}
  }
  console.error("check.mjs: Playwright not found. Install it (npm i playwright) or set PLAYWRIGHT_DIR.");
  process.exit(2);
}

// ── a static server, like GitHub Pages, under a subpath ───────────────
const TYPES = { ".html": "text/html; charset=utf-8", ".css": "text/css", ".js": "text/javascript", ".mjs": "text/javascript",
  ".json": "application/json", ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".svg": "image/svg+xml",
  ".webp": "image/webp", ".woff2": "font/woff2", ".woff": "font/woff", ".md": "text/markdown; charset=utf-8", ".txt": "text/plain; charset=utf-8", ".ico": "image/x-icon" };

function serve() {
  const server = http.createServer((req, res) => {
    const u = new URL(req.url, "http://x");
    let p = decodeURIComponent(u.pathname);
    if (!p.startsWith(BASE)) { res.writeHead(404); return res.end("not under " + BASE); }
    p = p.slice(BASE.length);
    if (p === "" || p.endsWith("/")) p += "index.html";
    const file = path.resolve(SITE, p);
    if (!file.startsWith(SITE + path.sep)) { res.writeHead(403); return res.end(); }
    fs.stat(file, (err, st) => {
      if (err || !st.isFile()) { res.writeHead(404); return res.end("404"); }
      res.writeHead(200, { "content-type": TYPES[path.extname(file).toLowerCase()] || "application/octet-stream" });
      fs.createReadStream(file).pipe(res);
    });
  });
  return new Promise((ok) => server.listen(0, "127.0.0.1", () => ok(server)));
}

// ── results ───────────────────────────────────────────────────────────
const failures = [];
let passes = 0;
const fail = (what) => { failures.push(what); console.log("FAIL " + what); };
const pass = (what) => { passes++; console.log("ok   " + what); };
const check = (cond, what, why) => (cond ? pass(what) : fail(what + (why ? " -- " + why : "")));

// ── who visits ────────────────────────────────────────────────────────
const UA = {
  winEdge: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36 Edg/130.0.0.0",
  winChrome: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
  winFirefox: "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:131.0) Gecko/20100101 Firefox/131.0",
  winFirefoxWow64: "Mozilla/5.0 (Windows NT 10.0; WOW64; rv:115.0) Gecko/20100101 Firefox/115.0",
  win32Firefox: "Mozilla/5.0 (Windows NT 10.0; rv:115.0) Gecko/20100101 Firefox/115.0",
  macSafari: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Safari/605.1.15",
  macChrome: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
  chromeOS: "Mozilla/5.0 (X11; CrOS x86_64 14541.0.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
  linux: "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
};
// navigator.userAgentData as each real browser has it (null: none, as in
// Safari and Firefox). A UA override alone would leave Chromium's Linux one.
const UAD = {
  winX64: { platform: "Windows", architecture: "x86", bitness: "64", mobile: false },
  winArm: { platform: "Windows", architecture: "arm", bitness: "64", mobile: false },
  mac: { platform: "macOS", architecture: "arm", bitness: "64", mobile: false },
  chromeOS: { platform: "Chrome OS", architecture: "x86", bitness: "64", mobile: false },
  android: { platform: "Android", architecture: "", bitness: "", mobile: true },
  linux: { platform: "Linux", architecture: "x86", bitness: "64", mobile: false },
};
function uadScript(mock) {
  if (mock === null) {
    Object.defineProperty(Navigator.prototype, "userAgentData", { configurable: true, get: () => undefined });
    return;
  }
  const data = { brands: [], mobile: mock.mobile, platform: mock.platform,
    getHighEntropyValues: async () => ({ platform: mock.platform, architecture: mock.architecture, bitness: mock.bitness, mobile: mock.mobile }),
    toJSON() { return { brands: [], mobile: mock.mobile, platform: mock.platform }; } };
  Object.defineProperty(Navigator.prototype, "userAgentData", { configurable: true, get: () => data });
}

// ── measuring inside the page ─────────────────────────────────────────
// Every visible text, with its computed size and its contrast against the
// colour actually behind it (backgrounds composed up the tree).
function measureText() {
  const parse = (c) => {
    const m = c.match(/rgba?\(([^)]+)\)/);
    if (!m) return null;
    const v = m[1].split(/[ ,/]+/).filter(Boolean).map(Number);
    return { r: v[0], g: v[1], b: v[2], a: v.length > 3 ? v[3] : 1 };
  };
  const lin = (x) => { x /= 255; return x <= 0.04045 ? x / 12.92 : Math.pow((x + 0.055) / 1.055, 2.4); };
  const L = (c) => 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
  const over = (top, under) => ({ r: top.r * top.a + under.r * (1 - top.a), g: top.g * top.a + under.g * (1 - top.a), b: top.b * top.a + under.b * (1 - top.a), a: 1 });
  const ratio = (a, b) => { const x = L(a), y = L(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
  function background(el) {
    const layers = [];
    for (let e = el; e; e = e.parentElement) {
      const cs = getComputedStyle(e);
      if (cs.backgroundImage && cs.backgroundImage !== "none") return null; // a picture behind: can't tell
      const c = parse(cs.backgroundColor);
      if (c && c.a > 0) { layers.push(c); if (c.a >= 1) break; }
    }
    let bg = { r: 255, g: 255, b: 255, a: 1 };
    for (let i = layers.length - 1; i >= 0; i--) bg = over(layers[i], bg);
    return bg;
  }
  const out = [];
  const seen = new Set();
  const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  while (w.nextNode()) {
    const t = w.currentNode;
    if (!t.textContent.trim()) continue;
    const el = t.parentElement;
    if (!el || seen.has(el) || el.closest("script,style,noscript")) continue;
    seen.add(el);
    const r = document.createRange(); r.selectNodeContents(t);
    const box = r.getBoundingClientRect();
    if (box.width <= 1 || box.height <= 1) continue;          // not drawn (or screen-reader only)
    let hidden = false, op = 1;
    for (let e = el; e; e = e.parentElement) {
      const cs = getComputedStyle(e);
      if (cs.visibility === "hidden" || cs.display === "none") { hidden = true; break; }
      op *= parseFloat(cs.opacity);
    }
    if (hidden || op === 0) continue;
    if (el.closest(".skip") && box.bottom < 0) continue;      // the skip link, until it has focus (checked separately)
    const cs = getComputedStyle(el);
    const fg = parse(cs.color), bg = background(el);
    const cr = bg && fg ? ratio(over(fg, bg), bg) : null;
    out.push({ text: t.textContent.trim().slice(0, 60), tag: el.tagName.toLowerCase() + (el.id ? "#" + el.id : "") + (el.className && typeof el.className === "string" ? "." + el.className.trim().split(/\s+/).join(".") : ""),
      size: parseFloat(cs.fontSize), contrast: cr });
  }
  return out;
}

function rectOf(sel) {
  const el = document.querySelector(sel);
  if (!el) return null;
  const r = el.getBoundingClientRect();
  const cs = getComputedStyle(el);
  return { top: r.top, left: r.left, bottom: r.bottom, right: r.right, width: r.width, height: r.height,
    shown: r.width > 0 && r.height > 0 && cs.visibility !== "hidden", vw: innerWidth, vh: innerHeight,
    href: el.getAttribute && el.getAttribute("href"), text: (el.innerText || "").trim() };
}
const inFold = (r) => r && r.shown && r.top >= 0 && r.left >= 0 && r.bottom <= r.vh && r.right <= r.vw;

// ── the run ───────────────────────────────────────────────────────────
const server = await serve();
const ORIGIN = `http://127.0.0.1:${server.address().port}`;
const HOME = ORIGIN + BASE;
const { chromium, devices } = loadPlaywright();
const browser = await chromium.launch();
console.log(`checking ${path.relative(REPO, SRC) || SRC} at ${HOME}`);

const external = new Map();   // url -> page it was asked for on
const broken = new Map();     // url -> status
async function visit({ name, url = HOME, ua, uad, viewport = { width: 1366, height: 768 }, js = true, device, init }) {
  const ctxOpts = device ? { ...devices[device], deviceScaleFactor: 1 } : { viewport, userAgent: ua };
  if (!js) ctxOpts.javaScriptEnabled = false;
  const ctx = await browser.newContext(ctxOpts);
  if (uad !== undefined && js) await ctx.addInitScript(uadScript, uad);
  if (init && js) await ctx.addInitScript(init.fn, init.arg);
  const page = await ctx.newPage();
  page.on("request", (rq) => {
    const u = rq.url();
    if (!u.startsWith(ORIGIN) && !/^(data|blob|about):/.test(u)) external.set(u, `${name} ${page.url()}`);
  });
  page.on("response", (rs) => { if (rs.url().startsWith(ORIGIN) && rs.status() >= 400) broken.set(rs.url(), rs.status()); });
  await page.goto(url, { waitUntil: "load" });
  await page.waitForTimeout(250); // let door.js finish its one async question
  return { ctx, page };
}

function textFindings(name, items) {
  const small = items.filter((i) => i.size < 18);
  const low = items.filter((i) => i.contrast !== null && i.contrast < 4.5);
  check(small.length === 0, `${name}: all text 18px or larger (${items.length} pieces)`,
    small.slice(0, 6).map((i) => `${i.size}px "${i.text}" <${i.tag}>`).join("; "));
  check(low.length === 0, `${name}: all text 4.5:1 or better`,
    low.slice(0, 6).map((i) => `${i.contrast.toFixed(2)}:1 "${i.text}" <${i.tag}>`).join("; "));
  const min = items.reduce((m, i) => (i.contrast !== null && i.contrast < m.c ? { c: i.contrast, t: i.text } : m), { c: 99, t: "" });
  if (items.length) console.log(`     lowest contrast ${min.c.toFixed(2)}:1 ("${min.t}")`);
}

async function shot(page, file, full = false) {
  if (!SHOTS) return;
  fs.mkdirSync(SHOTS, { recursive: true });
  if (full) { // let lazy pictures load, as they do for someone scrolling
    await page.evaluate(async () => { document.documentElement.style.scrollBehavior = "auto"; for (let y = 0; y < document.body.scrollHeight; y += 500) { scrollTo(0, y); await new Promise((r) => setTimeout(r, 60)); } scrollTo(0, 0); });
    await page.waitForTimeout(500);
  }
  await page.screenshot({ path: path.join(SHOTS, file), fullPage: full, ...(file.endsWith(".jpg") ? { type: "jpeg", quality: 80 } : {}) });
  console.log(`     saved ${path.relative(REPO, path.join(SHOTS, file))}`);
}

// 1. Windows, at the two sizes, with and without script
for (const v of [
  { name: "Windows Edge 1366x768", ua: UA.winEdge, uad: UAD.winX64, viewport: { width: 1366, height: 768 } },
  { name: "Windows Chrome 390x844", ua: UA.winChrome, uad: UAD.winX64, viewport: { width: 390, height: 844 } },
  { name: "Windows Firefox 1366x768 (can't tell the chip)", ua: UA.winFirefox, uad: null, viewport: { width: 1366, height: 768 } },
  { name: "32-bit Firefox on 64-bit Windows", ua: UA.winFirefoxWow64, uad: null, viewport: { width: 1366, height: 768 } },
  { name: "no script 1366x768", ua: UA.linux, js: false, viewport: { width: 1366, height: 768 } },
  { name: "no script 390x844", ua: UA.linux, js: false, viewport: { width: 390, height: 844 } },
]) {
  const { ctx, page } = await visit(v);
  const r = await page.evaluate(rectOf, "#get");
  check(inFold(r), `${v.name}: "Get AurOS" wholly on the first screen`, r ? `at ${Math.round(r.top)}-${Math.round(r.bottom)} of ${r.vh}, ${Math.round(r.left)}-${Math.round(r.right)} of ${r.vw}${r.shown ? "" : ", hidden"}` : "no #get");
  check(r && r.href === RELEASE_URL, `${v.name}: it links to the release`, r && r.href);
  check(r && /get aurOS/i.test(r.text), `${v.name}: it says "Get AurOS"`, r && JSON.stringify(r.text));
  check(r && r.height >= 56, `${v.name}: it is at least 56px tall`, r && `${Math.round(r.height)}px`);
  const el = await page.evaluate(rectOf, "#elsewhere");
  check(!el || !el.shown, `${v.name}: no "open it on another computer" message`);
  if (v.js !== false) {
    const again = await page.evaluate(() => [...document.querySelectorAll("[data-download]")].map((a) => a.href));
    check(again.every((h) => h === RELEASE_URL), `${v.name}: every other download link is the release too`, again.join(" "));
  }
  if (v.name.startsWith("Windows Edge") || v.name === "no script 390x844") textFindings(v.name, await page.evaluate(measureText));
  for (const [i, f] of (v.shots || []).entries()) await shot(page, f, i === 1);
  await ctx.close();
}

// 2. Not a Windows PC AurOS can go on: the kind message instead
for (const v of [
  { name: "Windows on ARM (Edge)", ua: UA.winEdge, uad: UAD.winArm, want: /can.t go on this computer/i, shots: ["index-arm.png"] },
  { name: "32-bit Windows (Firefox, no chip hints)", ua: UA.win32Firefox, uad: null, want: /can.t go on this computer/i },
  { name: "32-bit Windows (Edge's hints)", ua: UA.winEdge, uad: { platform: "Windows", architecture: "x86", bitness: "32", mobile: false }, want: /can.t go on this computer/i },
  { name: "Mac (Safari)", ua: UA.macSafari, uad: null, want: /open this page on (the|your) Windows computer/i, shots: ["index-mac.png"] },
  { name: "Mac (Chrome)", ua: UA.macChrome, uad: UAD.mac, want: /open this page on (the|your) Windows computer/i },
  { name: "iPhone", device: "iPhone 13", uad: null, want: /open this page on (the|your) Windows computer/i, shots: ["index-phone-fold.png"], init: { fn: () => Object.defineProperty(navigator, "share", { configurable: true, value: () => Promise.resolve() }) } },
  { name: "Android phone", device: "Pixel 7", uad: UAD.android, want: /open this page on (the|your) Windows computer/i },
  { name: "Chromebook", ua: UA.chromeOS, uad: UAD.chromeOS, want: /open this page on (the|your) Windows computer/i },
  { name: "Linux", ua: UA.linux, uad: UAD.linux, want: /open this page on (the|your) Windows computer/i },
]) {
  const { ctx, page } = await visit(v);
  const m = await page.evaluate(rectOf, "#elsewhere");
  const title = await page.evaluate(() => [...document.querySelectorAll("#elsewhere h2")].filter((h) => h.getClientRects().length).map((h) => h.innerText).join(" | "));
  check(m && m.shown && v.want.test(title), `${v.name}: says "${title || "nothing"}"`);
  const head = await page.evaluate(rectOf, "#elsewhere h2:not([hidden])");
  const titleTop = await page.evaluate(() => { const h = [...document.querySelectorAll("#elsewhere h2")].find((x) => x.getClientRects().length); return h ? h.getBoundingClientRect().top : null; });
  check(titleTop !== null && titleTop >= 0 && titleTop < (m ? m.vh : 0) - 60, `${v.name}: the message starts on the first screen`, `at ${titleTop}`);
  const mail = await page.evaluate(() => { const a = document.getElementById("mail-link"); return a && a.getClientRects().length ? a.href : ""; });
  const pageUrl = await page.evaluate(() => location.href.split("#")[0]);
  check(mail.startsWith("mailto:") && decodeURIComponent(mail).includes(pageUrl), `${v.name}: "Email me the link" carries this page's address`, mail);
  const copy = await page.evaluate(() => { const b = document.getElementById("copy-link"); return !!(b && b.tagName === "BUTTON" && b.getClientRects().length); });
  check(copy, `${v.name}: a "Copy the link" button`);
  const g = await page.evaluate(rectOf, "#get");
  check(!g || !g.shown, `${v.name}: the big download button is not offered`);
  if (v.name === "iPhone" || v.name.startsWith("Windows on ARM")) textFindings(v.name, await page.evaluate(measureText));
  for (const f of v.shots || []) await shot(page, f);
  await ctx.close();
}

// 3. Zoom and small screens: nothing scrolls sideways
for (const v of [
  { name: "200% zoom of 1366x768 (683x384)", viewport: { width: 683, height: 384 } },
  { name: "320px wide", viewport: { width: 320, height: 640 } },
]) {
  const { ctx, page } = await visit({ ...v, ua: UA.winEdge, uad: UAD.winX64 });
  const o = await page.evaluate(() => ({ sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth }));
  check(o.sw <= o.cw + 1, `${v.name}: no sideways scrolling`, `${o.sw}px of content in ${o.cw}px`);
  const r = await page.evaluate(rectOf, "#get");
  check(r && r.shown && r.right <= r.vw && r.left >= 0, `${v.name}: "Get AurOS" fits across the screen`);
  await ctx.close();
}

// 3b. The page the installer names when it cannot go on
//     (src/aurbridge/fix_win.c ADVANCED): it exists, the front door's
//     footer links it, and its text passes the same size and contrast
//     floors -- the person reading it may be the one who could not read
//     the installer either.
{
  const adv = path.join(SITE, "advanced.html");
  check(fs.existsSync(adv), "advanced.html: the page the installer names exists");
  const front = fs.readFileSync(path.join(SITE, "index.html"), "utf8");
  check(/<footer[\s\S]*href="advanced\.html"[\s\S]*<\/footer>/.test(front), "advanced.html: linked from the front door's footer");
  if (fs.existsSync(adv)) {
    for (const v of [
      { name: "advanced.html 1366x768", viewport: { width: 1366, height: 768 } },
      { name: "advanced.html 390x844", viewport: { width: 390, height: 844 } },
    ]) {
      const { ctx, page } = await visit({ ...v, url: HOME + "advanced.html", ua: UA.winEdge, uad: UAD.winX64 });
      textFindings(v.name, await page.evaluate(measureText));
      // The places the installer and the front door send a helper to.
      const want = ["bios", "rst", "smart-app-control", "secure-boot", "room"];
      const ids = await page.evaluate((w) => w.filter((id) => document.getElementById(id)), want);
      check(ids.length === want.length, `${v.name}: has the BIOS, Intel storage, Smart App Control, Secure Boot and room sections`, ids.join(","));
      await ctx.close();
    }
    const { ctx, page } = await visit({ name: "advanced.html 320px", url: HOME + "advanced.html", ua: UA.winEdge, uad: UAD.winX64, viewport: { width: 320, height: 640 } });
    const o = await page.evaluate(() => ({ sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth }));
    check(o.sw <= o.cw + 1, "advanced.html 320px wide: no sideways scrolling", `${o.sw}px of content in ${o.cw}px`);
    await ctx.close();
    // Every address on it is this repository's or this site's: it is the
    // page a helper follows from the installer, so a link elsewhere is one
    // more thing that can move, or be imitated.
    const mine = [`https://github.com/${AUROS_REPO}`, repoSh("site").replace(/\/$/, "")];
    const away = [...fs.readFileSync(adv, "utf8").matchAll(/(?:href|src)="((?:https?:)?\/\/[^"]*)"/g)]
      .map((m) => m[1]).filter((u) => !mine.some((b) => u === b || u.startsWith(b + "/")));
    check(away.length === 0, "advanced.html: every address is this repository's or this site's", away.join(" "));
  }
}

// 4. Keyboard: the first Tab reaches the skip link, which is then readable
{
  const { ctx, page } = await visit({ name: "keyboard", ua: UA.winEdge, uad: UAD.winX64 });
  await page.keyboard.press("Tab");
  const f = await page.evaluate(() => { const a = document.activeElement; const r = a.getBoundingClientRect(); const cs = getComputedStyle(a);
    return { text: a.innerText, top: r.top, size: parseFloat(cs.fontSize), outline: cs.outlineStyle !== "none" || a.classList.contains("skip") }; });
  check(f.top >= 0 && f.size >= 18 && /download/i.test(f.text), `keyboard: Tab first shows "${f.text}"`);
  await page.keyboard.press("Tab"); await page.keyboard.press("Tab"); await page.keyboard.press("Tab");
  const ring = await page.evaluate(() => { const cs = getComputedStyle(document.activeElement); return `${cs.outlineStyle} ${cs.outlineWidth}`; });
  check(/solid (3|4|5|6)px/.test(ring), `keyboard: focus is drawn with a thick ring`, ring);
  await ctx.close();
}

// 5. Every page of the site: links, pictures, scripts, anchors; and no
//    request to any other server
{
  // every page in the folder (old addresses included), and whatever they link to
  const seed = ["index.html", ...fs.readdirSync(SITE).filter((f) => f.endsWith(".html") && f !== "index.html").sort()];
  const pages = new Set(seed);
  const queue = [...seed];
  const targets = new Map(); // "page.html#frag" or "file" -> where it is linked from
  const ids = new Map();
  const isStub = (html) => /http-equiv="refresh"/i.test(html);
  while (queue.length) {
    const p = queue.shift();
    const file = path.join(SITE, p);
    if (!fs.existsSync(file)) continue;
    const html = fs.readFileSync(file, "utf8");
    ids.set(p, new Set([...html.matchAll(/\b(?:id|name)\s*=\s*"([^"]+)"/g)].map((m) => m[1])));
    const refs = [...html.matchAll(/\b(?:href|src)\s*=\s*"([^"]*)"/g)].map((m) => m[1])
      .concat([...html.matchAll(/\bcontent\s*=\s*"[^"]*?url=([^"]+)"/gi)].map((m) => m[1].trim()));
    for (const raw of refs) {
      if (!raw || /^(mailto:|tel:|javascript:|data:|https?:|\/\/)/i.test(raw) || /^[\d.]+;?$/.test(raw) || raw.startsWith("width=")) continue;
      if (/[\s<>{}$]/.test(raw)) continue; // not a link (meta content, template text)
      const u = new URL(raw, ORIGIN + BASE + p);
      if (!u.pathname.startsWith(BASE)) { targets.set(raw + "  (leaves the site's folder)", p); continue; }
      const rel = u.pathname.slice(BASE.length) || "index.html";
      const key = (rel.endsWith("/") ? rel + "index.html" : rel) + (u.hash || "");
      if (!targets.has(key)) targets.set(key, p);
      const target = key.split("#")[0];
      if (target.endsWith(".html") && !pages.has(target)) { pages.add(target); queue.push(target); }
    }
  }
  const missing = [];
  for (const [key, from] of targets) {
    if (key.includes("(leaves")) { missing.push(`${key} on ${from}`); continue; }
    const [file, frag] = key.split("#");
    if (!fs.existsSync(path.join(SITE, file))) { missing.push(`${key} on ${from}`); continue; }
    if (frag && file.endsWith(".html") && !(ids.get(file) || new Set()).has(decodeURIComponent(frag))) missing.push(`${key} (no such place) on ${from}`);
  }
  check(missing.length === 0, `links: every internal link, picture, script and anchor exists (${targets.size} checked on ${pages.size} pages)`, missing.slice(0, 10).join("; "));

  const realPages = [...pages].filter((p) => !isStub(fs.readFileSync(path.join(SITE, p), "utf8")));
  const badPrivacyFooters = realPages.filter((p) => {
    const html = fs.readFileSync(path.join(SITE, p), "utf8");
    const footer = html.match(/<!-- legal-footer:[\s\S]*?<!-- \/legal-footer -->/);
    return !footer || /collects nothing about you/i.test(footer[0]) || !/href="privacy\.html"[^>]*>Privacy page<\/a>/i.test(footer[0]);
  });
  check(badPrivacyFooters.length === 0, "privacy: every legal footer links the request-handling disclosure without a collects-nothing claim", badPrivacyFooters.join(", "));

  for (const p of [...pages].sort()) {
    const html = fs.readFileSync(path.join(SITE, p), "utf8");
    if (isStub(html)) continue;
    const { ctx, page } = await visit({ name: p, url: HOME + p, ua: UA.winEdge, uad: UAD.winX64 });
    await page.waitForTimeout(400);
    await ctx.close();
  }
  check(broken.size === 0, `requests: nothing the pages load is missing`, [...broken].map(([u, s]) => `${s} ${u.replace(ORIGIN, "")}`).slice(0, 10).join("; "));
  check(external.size === 0, `requests: no page asks another server for anything`, [...external].map(([u, w]) => `${u} (${w})`).slice(0, 10).join("; "));
}

// 5a. The custom-build preview is a small, closed contract. It may save a
//     recipe, but it must never imply that the recipe was built, uploaded or
//     applied by today's installer.
{
  execFileSync("node", [path.join(REPO, "tools", "site", "generate-configure-browser.mjs"), "--check"]);
  pass("request: the browser interpreter is generated from cloud/src/configure.js and is current");
  const { ctx, page } = await visit({ name: "request recipe", url: HOME + "request.html", ua: UA.winEdge, uad: UAD.winX64 });
  const start = await page.evaluate(() => ({
    recipe: JSON.parse(document.getElementById("recipe-preview").textContent),
    text: document.body.innerText,
    bases: [...document.querySelectorAll("input[name=base]")].map((e) => e.value),
    looks: [...document.querySelectorAll("input[name=look]")].map((e) => e.value),
    themes: [...document.querySelectorAll("input[name=theme]")].map((e) => e.value),
    other: [...document.querySelectorAll("input:not([type=radio]), textarea:not(#plain-request), select")].map((e) => e.outerHTML),
    plain: { max: document.getElementById("plain-request").maxLength, labels: document.getElementById("plain-request").labels.length },
  }));
  check(JSON.stringify(start.recipe) === JSON.stringify({ schema: "org.auros.recipe", version: 1, base: "desktop" }), "request: the default recipe is the exact v1 desktop contract", JSON.stringify(start.recipe));
  check(JSON.stringify(start.bases) === JSON.stringify(["desktop", "revive", "multilingual", "office", "school-kiosk"]), "request: all five real base profiles are offered once", JSON.stringify(start.bases));
  check(JSON.stringify(start.looks) === JSON.stringify(["", "taskbar", "dock", "rail", "tiles"]), "request: look is optional and limited to the four public arrangements", JSON.stringify(start.looks));
  check(JSON.stringify(start.themes) === JSON.stringify(["", "sandstone", "nocturne", "ember", "moss", "slate"]), "request: theme is optional and limited to the five shipped themes", JSON.stringify(start.themes));
  check(start.other.length === 0 && start.plain.max === 300 && start.plain.labels === 1, "request: the one plain-language box is bounded, labelled, and no other open-ended input exists", JSON.stringify({ other: start.other, plain: start.plain }));
  check(/does not make an AurOS download yet/i.test(start.text) && /AurOS-Setup\.exe does not read it/i.test(start.text) && /does not send it anywhere/i.test(start.text), "request: it says a valid recipe is not a build, today's installer does not read it, and nothing is sent");
  check(await page.locator("#build-request").isHidden(), "request build: disabled default leaves the honest local recipe preview unchanged");

  // Once the authoritative compiler is present, CI ties the hand-written,
  // buildless page to its catalogue. The page may add plain-language context,
  // but its identifiers and contract labels cannot silently diverge.
  const recipeCompiler = path.join(REPO, "src", "customize", "recipe.py");
  if (fs.existsSync(recipeCompiler)) {
    const catalogue = JSON.parse(execFileSync("python3", ["-m", "src.customize.recipe", "catalogue"], { cwd: REPO }).toString());
    const ids = (items) => items.map((item) => item.id).sort();
    check(catalogue.schema === "org.auros.recipe-catalogue" && catalogue.version === 1, "request: the website reads the authoritative v1 recipe catalogue", JSON.stringify({ schema: catalogue.schema, version: catalogue.version }));
    check(JSON.stringify([...start.bases].sort()) === JSON.stringify(ids(catalogue.bases)), "request: base IDs match the compiler catalogue", JSON.stringify(ids(catalogue.bases)));
    check(JSON.stringify(start.looks.filter(Boolean).sort()) === JSON.stringify(ids(catalogue.looks)), "request: look IDs match the compiler catalogue", JSON.stringify(ids(catalogue.looks)));
    check(JSON.stringify(start.themes.filter(Boolean).sort()) === JSON.stringify(ids(catalogue.themes)), "request: theme IDs match the compiler catalogue", JSON.stringify(ids(catalogue.themes)));
    const labels = catalogue.bases.concat(catalogue.looks, catalogue.themes).map((item) => item.label);
    check(labels.every((label) => start.text.includes(label)), "request: every compiler catalogue label appears on the page", labels.filter((label) => !start.text.includes(label)).join(", "));
    for (const base of catalogue.bases) {
      const plan = JSON.parse(execFileSync("python3", ["-m", "src.customize.recipe", "compile", "-"], {
        cwd: REPO,
        input: JSON.stringify({ schema: "org.auros.recipe", version: 1, base: base.id }),
      }).toString());
      check(plan.resolved_preferences.look === base.default_look && plan.resolved_preferences.theme === base.default_theme,
        `request: keeping ${base.id}'s usual choices resolves to its catalogue defaults`, JSON.stringify(plan.resolved_preferences));
    }
  }

  const formSelection = () => page.evaluate(() => ({
    base: document.querySelector("input[name=base]:checked").value,
    look: document.querySelector("input[name=look]:checked").value,
    theme: document.querySelector("input[name=theme]:checked").value,
    recipe: JSON.parse(document.getElementById("recipe-preview").textContent),
  }));
  const initialSelection = await formSelection();
  await page.click("#understand-request");
  const empty = await page.evaluate(() => ({ text: document.getElementById("request-words-status").innerText, focus: document.activeElement.id }));
  check(/write what you want first/i.test(empty.text) && /nothing was changed/i.test(empty.text) && empty.focus === "plain-request" && JSON.stringify(await formSelection()) === JSON.stringify(initialSelection),
    "request words: empty input explains what to do, focuses the labelled box, and changes nothing", JSON.stringify(empty));

  await page.fill("#plain-request", "An older, slower computer with a familiar Windows-like bar and warm ember colours.");
  await page.click("#understand-request");
  const proposed = await page.evaluate(() => ({
    shown: document.getElementById("request-proposal").getClientRects().length > 0,
    base: document.getElementById("proposal-base").innerText,
    look: document.getElementById("proposal-look").innerText,
    theme: document.getElementById("proposal-theme").innerText,
    status: document.getElementById("request-words-status").innerText,
  }));
  check(proposed.shown && proposed.base === "An older, slower computer" && proposed.look === "Like Windows" && proposed.theme === "Ember" && /form has not changed/i.test(proposed.status) && JSON.stringify(await formSelection()) === JSON.stringify(initialSelection),
    "request words: a supported request shows all resolved choices without changing the form", JSON.stringify(proposed));
  await page.click("#accept-request");
  const accepted = await formSelection();
  check(JSON.stringify(accepted) === JSON.stringify({ base: "revive", look: "taskbar", theme: "ember", recipe: { schema: "org.auros.recipe", version: 1, base: "revive", preferences: { look: "taskbar", theme: "ember" } } }),
    "request words: explicit acceptance applies exactly the reviewed recipe", JSON.stringify(accepted));

  for (const [words, reason] of [
    ["Make the screen brighter whenever this computer points north", /hardware-sensor support/iu],
    ["Use the Revive base in moss and also make the pointer enormous", /part of that request is outside/iu],
    ["Give me Office", /adding or naming programs/iu],
  ]) {
    await page.fill("#plain-request", words);
    await page.click("#understand-request");
    const refused = await page.evaluate(() => ({ text: document.getElementById("request-words-status").innerText,
      proposal: document.getElementById("request-proposal").getClientRects().length > 0 }));
    check(reason.test(refused.text) && /nothing was changed/i.test(refused.text) && !refused.proposal && JSON.stringify(await formSelection()) === JSON.stringify(accepted),
      `request words: unsupported request is wholly refused without partial changes (${JSON.stringify(words)})`, JSON.stringify(refused));
  }

  await page.fill("#plain-request", "Use the office computer base");
  await page.click("#understand-request");
  const office = await page.evaluate(() => ({ base: document.getElementById("proposal-base").innerText,
    look: document.getElementById("proposal-look").innerText, theme: document.getElementById("proposal-theme").innerText }));
  check(JSON.stringify(office) === JSON.stringify({ base: "Computers for an organisation", look: "Big and simple", theme: "Slate" }) && JSON.stringify(await formSelection()) === JSON.stringify(accepted),
    "request words: explicit office-computer wording resolves to office defaults and still waits for acceptance", JSON.stringify(office));
  await page.evaluate(() => {
    const button = document.getElementById("accept-request");
    const input = document.getElementById("plain-request");
    input.value = "An everyday computer in moss";
    input.dispatchEvent(new Event("input", { bubbles: true }));
    button.click();
  });
  const stale = await page.evaluate(() => ({ text: document.getElementById("request-words-status").innerText,
    proposal: document.getElementById("request-proposal").getClientRects().length > 0 }));
  check(/words changed/i.test(stale.text) && /nothing was changed/i.test(stale.text) && !stale.proposal && JSON.stringify(await formSelection()) === JSON.stringify(accepted),
    "request words: editing after review makes acceptance stale and applies nothing", JSON.stringify(stale));

  await page.check("input[name=base][value=office]");
  await page.check("input[name=look][value=dock]");
  await page.check("input[name=theme][value=moss]");
  const changed = await page.evaluate(() => JSON.parse(document.getElementById("recipe-preview").textContent));
  check(JSON.stringify(changed) === JSON.stringify({ schema: "org.auros.recipe", version: 1, base: "office", preferences: { look: "dock", theme: "moss" } }), "request: supported choices produce only allowlisted recipe fields", JSON.stringify(changed));

  await page.check("input[name=base][value=school-kiosk]");
  const kiosk = await page.evaluate(() => ({
    recipe: JSON.parse(document.getElementById("recipe-preview").textContent),
    look: document.querySelector("input[name=look]:checked").value,
    disabled: [...document.querySelectorAll("input[name=look]")].filter((e) => e.value).every((e) => e.disabled),
    warning: document.getElementById("kiosk-look-note").getClientRects().length > 0,
  }));
  check(kiosk.look === "" && kiosk.disabled && kiosk.warning && !(kiosk.recipe.preferences || {}).look, "request: school-kiosk rejects a public look override and explains why", JSON.stringify(kiosk));

  const download = page.waitForEvent("download");
  await page.click("#save-recipe");
  const saved = await download;
  check(saved.suggestedFilename() === "auros-recipe.json", "request: Save my recipe downloads auros-recipe.json", saved.suggestedFilename());
  const status = await page.evaluate(() => document.getElementById("recipe-status").innerText);
  check(/has not been sent anywhere/i.test(status), "request: after saving, the page repeats that nothing was sent", JSON.stringify(status));
  await ctx.close();

  const narrow = await visit({ name: "request 320px", url: HOME + "request.html", ua: UA.winEdge, uad: UAD.winX64, viewport: { width: 320, height: 640 } });
  const fit = await narrow.page.evaluate(() => ({ sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth,
    cards: [...document.querySelectorAll(".choice-card")].every((e) => e.getBoundingClientRect().height >= 44) }));
  check(fit.sw <= fit.cw + 1 && fit.cards, "request: at 320px there is no sideways scrolling and every choice target is at least 44px", `${fit.sw}px of content in ${fit.cw}px`);
  await narrow.ctx.close();

  // The build adapter is inert by default. These contexts replace only its
  // checked-in static configuration and API, never the public default.
  const API = "https://builds.example";
  const ID = "123e4567-e89b-42d3-a456-426614174000";
  const TOKEN = "abcdefghijklmnopqrstuvwxYZ012345";
  const RID = "sha256:" + "a".repeat(64);
  const desktop = { schema: "org.auros.recipe", version: 1, base: "desktop" };
  async function buildVisit(handler, receipt) {
    const ctx = await browser.newContext({ viewport: { width: 1366, height: 768 }, userAgent: UA.winEdge });
    await ctx.addInitScript(uadScript, UAD.winX64);
    if (receipt) await ctx.addInitScript((saved) => localStorage.setItem("auros.build-receipt.v1", JSON.stringify(saved)), receipt);
    await ctx.route("**/build-service.json", (route) => route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify({ enabled: true, api_origin: API }) }));
    await ctx.route(API + "/**", handler);
    const page = await ctx.newPage();
    await page.goto(HOME + "request.html", { waitUntil: "load" });
    await page.waitForSelector("#build-request:not([hidden])");
    return { ctx, page };
  }
  const challenge = { challenge: "0123456789abcdef0123456789abcdef", expires_at: "2026-10-24T12:00:00Z", bits: 0, proof_prefix: "auros-build-v1:" };
  const acceptedJob = { id: ID, token: TOKEN, recipe_id: RID, state: "queued", status_url: "/v1/builds/" + ID, download_ready: false };
  const pending = { id: ID, recipe_id: RID, state: "running", failure: null, download_ready: false, download: null, expires_at: "2026-10-25T12:00:00Z" };

  for (const [error, status, words] of [
    ["existing_delivery_busy", 503, "being checked for another request"],
    ["existing_delivery_unavailable", 503, "could not check the saved installer"],
    ["existing_delivery_conflict", 409, "saved build details do not agree"]
  ]) {
    let submissions = 0;
    const { ctx, page } = await buildVisit((route) => {
      if (route.request().url().endsWith("/challenge")) return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(challenge) });
      submissions++;
      return route.fulfill({ status, contentType: "application/json", body: JSON.stringify({ error, download_ready: false }) });
    });
    await page.click("#request-build");
    await page.waitForFunction((text) => document.getElementById("build-status").innerText.includes(text), words);
    check(submissions === 1 && await page.locator("#build-download").isHidden(),
      "request build: " + error + " explains the refusal without a download or repeated submission");
    await ctx.close();
  }

  {
    const { ctx, page } = await buildVisit((route) => route.fulfill({ status: 503, contentType: "application/json", body: JSON.stringify({ error: "base_unavailable", download_ready: false }) }));
    await page.click("#request-build");
    await page.waitForFunction(() => /not available/i.test(document.getElementById("build-status").innerText));
    check(/not available for a tested build/i.test(await page.locator("#build-status").innerText()), "request build: service refusals are shown as refusals, not progress");
    await ctx.close();
  }
  {
    let calls = 0;
    const { ctx, page } = await buildVisit((route) => { calls++; return route.abort(); });
    await page.check("input[name=base][value=revive]");
    await page.click("#request-build");
    const text = await page.locator("#build-status").innerText();
    check(/only My everyday computer/i.test(text) && /not partly submitted/i.test(text) && calls === 0,
      "request build: unsupported bases are wholly refused before any network request", JSON.stringify({ text, calls }));
    await ctx.close();
  }
  {
    const { ctx, page } = await buildVisit(async (route) => {
      if (route.request().url().endsWith("/challenge")) return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(challenge) });
      return route.fulfill({ status: 429, contentType: "application/json", body: JSON.stringify({ error: "network_quota", download_ready: false }) });
    });
    await page.click("#request-build");
    await page.waitForFunction(() => /temporary request limit/i.test(document.getElementById("build-status").innerText));
    check(/temporary request limit/i.test(await page.locator("#build-status").innerText()), "request build: rate limits are explained without fake progress");
    await ctx.close();
  }
  {
    const { ctx, page } = await buildVisit((route) => route.abort("connectionfailed"));
    await page.click("#request-build");
    await page.waitForFunction(() => /fetch|network|could not|failed/i.test(document.getElementById("build-status").innerText));
    check(!(await page.locator("#build-status").innerText()).includes(TOKEN), "request build: network failure is explicit and never exposes a receipt token");
    await ctx.close();
  }
  {
    let buildPosts = 0;
    let releaseChallenge;
    const challengeGate = new Promise((resolve) => { releaseChallenge = resolve; });
    const { ctx, page } = await buildVisit(async (route) => {
      if (route.request().url().endsWith("/challenge")) {
        await challengeGate;
        return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(challenge) });
      }
      buildPosts++;
      return route.fulfill({ status: 201, contentType: "application/json", body: JSON.stringify(acceptedJob) });
    });
    const challengeRequest = page.waitForRequest((request) => request.url().endsWith("/challenge"));
    await page.click("#request-build");
    await challengeRequest;
    await page.check("input[name=theme][value=moss]");
    releaseChallenge();
    await page.waitForFunction(() => /choices changed/i.test(document.getElementById("build-status").innerText));
    check(buildPosts === 0, "request build: changed choices invalidate prepared proof before submission", String(buildPosts));
    await ctx.close();
  }
  {
    let posts = 0, auth = "";
    const receipt = { api_origin: API, id: ID, token: TOKEN, recipe_id: RID, recipe: desktop };
    const { ctx, page } = await buildVisit((route) => {
      if (route.request().method() === "POST") posts++;
      auth = route.request().headers().authorization || "";
      return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(pending) });
    }, receipt);
    await page.waitForFunction(() => /signing must pass/i.test(document.getElementById("build-status").innerText));
    const body = await page.locator("body").innerText();
    check(posts === 0 && auth === "Bearer " + TOKEN && !body.includes(TOKEN) && !page.url().includes(TOKEN) && await page.locator("#build-result").isHidden(),
      "request build: refresh resumes an unsigned pending job with header-only receipt and no resubmission", JSON.stringify({ posts, auth: auth ? "present" : "missing" }));
    await ctx.close();
  }
  {
    const receipt = { api_origin: API, id: ID, token: TOKEN, recipe_id: RID, recipe: desktop };
    const wrong = { ...pending, recipe_id: "sha256:" + "b".repeat(64) };
    const { ctx, page } = await buildVisit((route) => route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(wrong) }), receipt);
    await page.waitForFunction(() => /different choices/i.test(document.getElementById("build-status").innerText));
    check(await page.locator("#build-result").isHidden(), "request build: a stale or foreign recipe result is blocked");
    await ctx.close();
  }
  {
    const receipt = { api_origin: API, id: ID, token: TOKEN, recipe_id: RID, recipe: desktop };
    const failed = { ...pending, state: "failed", failure: "signing_failed" };
    const { ctx, page } = await buildVisit((route) => route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(failed) }), receipt);
    await page.waitForFunction(() => /build failed/i.test(document.getElementById("build-status").innerText));
    check(/signing_failed/.test(await page.locator("#build-status").innerText()) && await page.locator("#build-result").isHidden(), "request build: real build failures remain failures with no download");
    await ctx.close();
  }
  {
    const receipt = { api_origin: API, id: ID, token: TOKEN, recipe_id: RID, recipe: desktop };
    const name = "AurOS-Custom-" + "a".repeat(64) + ".exe";
    const ready = { ...pending, state: "download-ready", download_ready: true, download: {
      name, sha256: "b".repeat(64), bytes: 123456,
      url: "https://github.com/aarohkandy/v0.0.1-auros/releases/download/custom-test/" + name,
      manifest_url: "https://github.com/aarohkandy/v0.0.1-auros/releases/download/custom-test/AurOS-Custom-" + "a".repeat(64) + ".json",
    } };
    const { ctx, page } = await buildVisit((route) => route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(ready) }), receipt);
    await page.waitForSelector("#build-result:not([hidden])");
    const got = await page.evaluate(() => ({ href: document.getElementById("build-download").href, hash: document.getElementById("build-sha256").innerText, choices: document.getElementById("build-choices").innerText }));
    check(got.href === ready.download.url && got.hash === ready.download.sha256 && /My everyday computer/.test(got.choices),
      "request build: ready download is the exact allowlisted GitHub EXE and hash tied to submitted choices", JSON.stringify(got));
    textFindings("request build ready", await page.evaluate(measureText));
    await ctx.close();
  }
}

// 6. Published as it is in the tree (no stamp) on another repository's
//    GitHub Pages: the button works the release out from the page's own
//    address (assets/door.js), for a project site and for owner.github.io.
for (const [at, want] of [
  ["https://someone-else.github.io/their-copy/", "https://github.com/someone-else/their-copy/releases/latest/download/AurOS-Setup.exe"],
  ["https://someone-else.github.io/", "https://github.com/someone-else/someone-else.github.io/releases/latest/download/AurOS-Setup.exe"],
]) {
  const ctx = await browser.newContext({ viewport: { width: 1366, height: 768 }, userAgent: UA.winEdge });
  await ctx.addInitScript(uadScript, UAD.winX64);
  await ctx.route(() => true, (route) => {
    const u = route.request().url();
    if (!u.startsWith(at)) return route.abort();
    let p = decodeURIComponent(new URL(u).pathname.slice(new URL(at).pathname.length));
    if (p === "" || p.endsWith("/")) p += "index.html";
    const file = path.resolve(SRC, p);
    if (!file.startsWith(SRC + path.sep) || !fs.existsSync(file)) return route.fulfill({ status: 404, body: "404" });
    return route.fulfill({ path: file, contentType: TYPES[path.extname(file).toLowerCase()] || "application/octet-stream" });
  });
  const page = await ctx.newPage();
  await page.goto(at, { waitUntil: "load" });
  await page.waitForTimeout(250);
  const links = await page.evaluate(() => [document.getElementById("get"), ...document.querySelectorAll("[data-download]")].map((a) => a && a.href));
  check(links.length > 0 && links.every((h) => h === want), `unstamped at ${at}: every download link is that repository's release`, links.join(" "));
  await ctx.close();
}

// 7. Every promise the site makes, against the code that keeps it.
//    The pages mark what they promise:
//      data-fact="NAME"   its text must be what the source says (sizes, times)
//      data-claim="K:V"   V must be what the source does (BitLocker, the hold...)
//      data-ui="WHERE"    its text must be a word on that screen, as the source spells it
//    and some old claims are false for this build and must not come back.
{
  const read = (f) => { try { return fs.readFileSync(path.join(REPO, f), "utf8"); } catch { return ""; } };
  const plan = read("src/aurstage/plan.c"), wizard = read("src/aurbridge/wizard.c"), preflightH = read("src/aurbridge/preflight.h");
  const fixWin = read("src/aurbridge/fix_win.c"), phases = read("src/aurbridge/phases.h");
  const shellSrc = ["src/aurshell"].concat(fs.readdirSync(path.join(REPO, "src/aurshell/layouts")).map((f) => "src/aurshell/layouts/" + f))
    .flatMap((d) => d.endsWith(".c") ? [d] : fs.readdirSync(path.join(REPO, d)).filter((f) => f.endsWith(".c")).map((f) => d + "/" + f))
    .map(read).join("\n");
  const num = (src, re, what) => { const m = src.match(re); if (!m) fail(`promises: cannot read ${what} from the source (${re})`); return m ? Number(m[1]) : NaN; };

  // How long: wizard.c eta_range() with nothing measured yet, worded as range_words() words it.
  function eta(slow) {
    const d = (n) => num(wizard, new RegExp(`#define ${n}\\s+([\\d.]+)`), n);
    const [lo1, hi1] = (wizard.match(/double dl_lo = ([\d.]+), dl_hi = ([\d.]+);/) || []).slice(1).map(Number);
    const margin = num(wizard, /double hi_m = dl \* dl_hi \+ w \* ([\d.]+);/, "how much longer the top of the range is");
    const dl = d("DL_BYTES_FALLBACK") / d("ETA_LINK_BPS") / 60;
    const w = slow ? d("ETA_HDD_WORK_MIN") : d("ETA_SSD_WORK_MIN");
    const lo_m = dl * lo1 + w, hi_m = dl * hi1 + w * margin;
    let l = Math.max(5, Math.floor(lo_m / 5) * 5), h = Math.ceil(hi_m / 5) * 5;
    if (h > 30) h = Math.ceil(hi_m / 15) * 15;
    if (h <= l) h = l + 5;
    if (h >= 120) { const lh = Math.max(1, Math.floor(l / 60)); let hh = Math.ceil(h / 60); if (hh <= lh) hh = lh + 1; return `${lh} to ${hh} hours`; }
    return `${l} to ${h}`;
  }
  const FACTS = {
    "auros-need-gb": String(num(preflightH, /PF_AUROS_NEED_BYTES\s+\(\s*(\d+)ULL \* 1024 \* 1024 \* 1024\)/, "what AurOS needs")),
    "windows-floor-gb": String(num(preflightH, /PF_WINDOWS_KEEP_BYTES\s+\(\s*(\d+)ULL \* 1024 \* 1024 \* 1024\)/, "what Windows keeps at least")),
    // the shrink policy: src/aurstage/plan.c shrink_pick_target() (the installer no longer shows these figures)
    "auros-max-gb": String(num(plan, /headroom < (\d+) \* PLAN_GIB \? headroom : \1 \* PLAN_GIB/, "the most AurOS takes")),
    "windows-room-gb": String(num(plan, /if \(reserve < (\d+) \* PLAN_GIB\)/, "what Windows keeps when there is room")),
    "time-ssd": eta(false),
    "journey-ssd": (() => { try { const v = JSON.parse(read("tools/site/journey.json")).ssd_median_min; return v == null ? "none" : String(v); } catch { return "none"; } })(),
    "time-hdd": eta(true),
  };
  // What the code does, in the words the pages use for it.
  // THE SERVED BUILD. The "Get AurOS" button serves the default build of
  // AurOS-Setup.exe, and build/aurbridge says whether that has the fast path
  // (${AUROS_FASTPATH:-0}): src/aurbridge/phases.h defines the BitLocker pause and
  // the Wi-Fi carry-over only under #ifdef AUROS_FASTPATH. When the lead promotes the
  // fast path (flips that default), this check follows, and goes red until the page's
  // one switch, <html data-build="...">, is flipped to match.
  const DEFAULT_FAST = (read("build/aurbridge").match(/\$\{AUROS_FASTPATH:-(\d)\}/) || [])[1] === "1";
  const defineGuarded = (name) => {          // is `#define name` inside #ifdef AUROS_FASTPATH?
    const m = phases.match(new RegExp(`^\\s*#\\s*define\\s+${name}\\b`, "m"));
    if (!m) return null;                     // not defined at all
    const before = phases.slice(0, m.index).split("\n");
    let depth = 0, guarded = false;
    for (const l of before) { const t = l.trim(); if (/^#\s*ifdef\s+AUROS_FASTPATH\b/.test(t)) { depth++; guarded = true; } else if (/^#\s*if(n?def)?\b/.test(t)) depth++; else if (/^#\s*endif\b/.test(t)) { depth--; if (depth === 0) guarded = false; } }
    return guarded;
  };
  const has = (name, fast) => { const g = defineGuarded(name); return g === null ? false : (g ? fast : true); };
  // What the code does, in the words the pages use for it, for a build with the fast path on or off.
  const claimsFor = (fast) => ({
    bitlocker: has("AB_CHOICE_BITLOCKER_PAUSE", fast) ? "pauses"
             : /\{\s*"bitlocker-system",\s*FIX_AUTO/.test(fixWin) ? "decrypts" : "refuses",
    wifi: has("AB_CHOICE_BRING_WIFI", fast) ? "comes-across" : "stays-behind",
    hold: fs.existsSync(path.join(REPO, "rootfs/etc/systemd/system/auros-hold.service")) && /int af_hold\(/.test(read("src/aurfirst/state.c")) ? "rearms" : "windows-first",
    charger: /\{\s*"not-on-ac",\s*FIX_WAIT/.test(fixWin) ? "required" : "not-required",
    // Put Windows back: settings.c says on the row and on its page that what she saved in AurOS goes with it,
    // and the help page says the same (K's one truth): the FAQ's loss sentence is tied to both.
    putback: /add_choice\("putback",[\s\S]{0,160}?Anything you saved in AurOS goes with it\./.test(read("src/aurshell/settings.c"))
          && /Anything you saved in AurOS goes with it/.test(read("rootfs/usr/share/auros/help/windows.html")) ? "removes-saved" : "other",
    // How far AurOS has been tried: tools/site/journey.json counts runs on a real computer (F's e2e is a virtual one).
    tested: (() => { try { return JSON.parse(read("tools/site/journey.json")).real_pc_runs > 0 ? "real" : "virtual"; } catch { return "unknown"; } })(),
    dark: /Restart now: the screen goes black for a minute or two/.test(wizard) ? "minute-or-two" : "other",
    stop: /AurOS stops here\. Nothing was changed\./.test(wizard) && /Nothing on this computer has been changed\./.test(wizard) ? "nothing-changed" : "other",
    release: (() => { const y = read(".github/workflows/release.yml"); const p = (y.match(/\n  promote:\n[\s\S]*?(?=\n  [a-z][\w-]*:\n)/) || [""])[0];
      // promote waits for the e2e job and asks the jobs themselves (tools/releasegate.py e2e: the real-Windows
      // journey of THIS exe passed), and the site is deployed only after promote
      return /needs: \[[^\]]*\be2e\b/.test(p) && /releasegate\.py e2e/.test(p) && /needs\.publish\.outputs\.signed/.test(p) && /releasegate\.py signed "\$SIGNED"/.test(p) && /\n  pages:\n\s+needs: promote/.test(y) ? "tested" : "untested"; })(),
  });
  const CLAIMS = claimsFor(DEFAULT_FAST);
  const UI = {
    wizard: (t) => wizard.includes(`L"${t}"`),
    look: (t) => /static const style_info STYLES\[\][\s\S]*?\};/.exec(wizard)?.[0].includes(`L"${t}"`),
    "look-says": (t) => /static const style_info STYLES\[\][\s\S]*?\};/.exec(wizard)?.[0].includes(`L"${t}"`),
    desktop: (t) => shellSrc.includes(`"${t}"`),
    // A title Setup shows on a can't-go-on page (fix_win.c, preflight.c,
    // sbdb.c, wizard.c), quoted by advanced.html for the helper.
    setup: (t) => [wizard, fixWin, read("src/aurbridge/preflight.c"), read("src/aurbridge/sbdb.c")]
      .some((src) => src.includes(`"${t}"`)),
  };
  // Said once, false for this build. Each with where the truth is.
  const STALE = [
    [/switching (this computer|it) on,? (still )?starts Windows/i, "until she answers, AurOS re-arms itself on every start (auros-hold.service, src/aurfirst/state.c af_hold)"],
    [/can.t be switched back on/i, "Smart App Control can be switched back on since April 2026 (docs/research/every-machine-2026-10.md, section 5)"],
    [/no firefox/i, "the image has Firefox (build/forge)"],
    [/\bunplug|\b(\d+|any|every|no) power cuts?\b|power cuts? (are|is|can|won.t|cannot|can.t)\b/i, "no promise about surviving a power cut is proven for this build"],
    [/(bring|brings|bringing) (copies of )?(your|her) (documents|files|pictures)|files across/i, "bringing her files across does not work yet (docs/ATLAS.md, section 8, gap 1)"],
    // (There was a regex here for "updates itself": since the update channel was proven (S5,
    // aptrepo.yml) that is true while the computer is online, and the Terms say it in their words.
    // What the site must not do is promise an update that cannot be restarted-into or is instant:)
    [/(updates?|fixes) (instantly|immediately|the moment)/i, "updates arrive while the computer is online, not at once, and AurOS does not restart for them (docs/legal/TERMS.md section 8)"],
    [/about 10 minutes/i, "Setup says its own range (wizard.c eta_range)"],
    [/\bnew again\b|\b(like|feel(s|ing)?) new\b|\bas good as new\b|\bbrand.new\b/i, "the headline may not promise an outcome as an adjective (DESIGN-DIRECTION section 4, rule 4; docs/legal/REVIEWS.md C3): say what AurOS is"],
    [/that.s normal|that is normal|normal too/i, "reassure with facts, never adjectives (docs/DESIGN-DIRECTION.md section 4, rule 4)"],
    [/Show more(?! details)/, "Edge no longer has Show more: Keep anyway is behind the small arrow beside Delete (docs/handoff/streams/F.md, finding 1)"],
    [/Stuck\? Show a helper/, "she has no helper there: the button says Stuck? Ask someone to help (walkthrough A-06)"],
    [/shows both numbers/i, "the Ready page shows what Windows keeps free, not both numbers (docs/legal/LEGAL.md, D hand-off); say that Setup checks and tells her"],
    [/double-click/i, "she presses Open file (docs/DESIGN-DIRECTION.md section 5, K P1-4)"],
    [/switch(ing)? (that|it|Smart App Control) off/i, "she is never walked through switching off a Windows protection (DESIGN-DIRECTION section 6, rule 2); the helper's page may", /^(?!advanced\.html$)/],
  ];

  const real = fs.readdirSync(SITE).filter((f) => f.endsWith(".html") && !/http-equiv="refresh"/i.test(fs.readFileSync(path.join(SITE, f), "utf8"))).sort();
  let facts = 0, claims = 0, words = 0;
  for (const p of real) {
    const { ctx, page } = await visit({ name: "promises " + p, url: HOME + p, ua: UA.winEdge, uad: UAD.winX64 });
    const marks = await page.evaluate(() => [...document.querySelectorAll("[data-fact],[data-claim],[data-ui]")].map((e) => ({
      fact: e.dataset.fact, claim: e.dataset.claim, ui: e.dataset.ui, text: e.textContent.replace(/\s+/g, " ").trim(),
      buildFor: (e.closest("[data-build-for]") || {}).dataset ? e.closest("[data-build-for]").dataset.buildFor : null })));
    const builds = await page.evaluate(() => ({ attr: document.documentElement.dataset.build || null,
      variants: [...document.querySelectorAll("[data-build-for]")].map((e) => ({ for: e.dataset.buildFor, shown: e.getClientRects().length > 0 })) }));
    if (builds.variants.length) {
      check(builds.attr === (DEFAULT_FAST ? "fastpath" : "default"), `promises ${p}: <html data-build="${builds.attr}"> is the build the button serves (build/aurbridge's default has the fast path ${DEFAULT_FAST ? "ON" : "off"}: "${DEFAULT_FAST ? "fastpath" : "default"}")`, "flip data-build on <html>, or the default in build/aurbridge");
      check(builds.variants.every((v) => v.shown === (v.for === builds.attr)), `promises ${p}: only the wording for the served build is shown (${builds.variants.length} variants)`, JSON.stringify(builds.variants));
    }
    for (const m of marks) {
      if (m.fact) { facts++; check(FACTS[m.fact] !== undefined && m.text === FACTS[m.fact], `promises ${p}: "${m.text}" is what the code says for ${m.fact}`, `the code says "${FACTS[m.fact]}"`); }
      if (m.claim) { claims++; const [k, v] = m.claim.split(":"); const fast = m.buildFor ? m.buildFor === "fastpath" : DEFAULT_FAST; const want = claimsFor(fast)[k];
        check(want === v, `promises ${p}: the page says ${k} ${v}${m.buildFor ? ` (for the ${m.buildFor} build)` : ""}, the code ${want}`, `rewrite "${m.text.slice(0, 70)}..." for "${want}", or change the code`); }
      if (m.ui) { words++; check(UI[m.ui] && UI[m.ui](m.text), `promises ${p}: "${m.text}" is a word on the ${m.ui} screen`, `not found in its source`); }
    }
    const text = await page.evaluate(() => document.body.innerText);
    for (const [re, why, only] of STALE) { if (only && !only.test(p)) continue; const m = text.match(re); check(!m, `promises ${p}: never says ${re}`, m && `says "${m[0]}": ${why}`); }
    // Every address on GitHub is this repository's; every "Get AurOS" is the download.
    const links = await page.evaluate(() => [...document.querySelectorAll("a[href]")].map((a) => ({ href: a.href, text: a.textContent.trim() })));
    const gh = links.filter((l) => /^https:\/\/(github\.com|[^/]*\.github\.io)\//.test(l.href) && !l.href.startsWith(`https://github.com/${AUROS_REPO}`) && !l.href.startsWith(`https://${PAGES_HOST}`));
    check(gh.length === 0, `promises ${p}: every GitHub address is ${AUROS_REPO}'s`, gh.map((l) => l.href).join(" "));
    check(!/(\+?\d{1,3}[ .-])?\(?\d{3}\)?[ .-]\d{3}[ .-]\d{4}/.test(text) && !links.some((l) => /^tel:/i.test(l.href)), `promises ${p}: no phone number, no tel: link (AurOS has no phone line, and never phones)`);
    if (p === "guide.html") check(text.includes(PAGES_HOST.replace(/\/$/, "")), `promises ${p}: the printed sheet says where to go (${PAGES_HOST.replace(/\/$/, "")})`);
    if (p === "index.html") {
      // Numbered circles: in each picture group they count 1, 2, 3... with none missed or repeated.
      const groups = await page.evaluate(() => [...document.querySelectorAll(".step")].flatMap((li) => {
        const parts = li.querySelector(".browser") ? [...li.querySelectorAll(".browser")] : [li];
        return parts.map((g) => ({ at: (li.id || "step") + (g === li ? "" : " " + g.dataset.browser), n: [...g.querySelectorAll("[data-n]")].map((e) => Number(e.dataset.n)) }));
      }));
      for (const g of groups.filter((g) => g.n.length)) check(g.n.every((v, i) => v === i + 1), `promises ${p}: the numbered circles in ${g.at} count 1 to ${g.n.length}`, g.n.join(","));
      const av = await page.evaluate(() => { const e = document.getElementById("antivirus"); return e ? { text: e.innerText, shown: e.getClientRects().length > 0 } : null; });
      check(av && av.shown && /antivirus/i.test(av.text) && /nothing has been installed/i.test(av.text) && /Stuck\? Ask someone to help/.test(av.text), `promises ${p}: a calm step for an antivirus program that takes the file`, av ? av.text.slice(0, 80) : "no #antivirus");
      const edge = await page.evaluate(() => { const e = document.querySelector('[data-browser="edge"]'); return e ? e.innerText : ""; });
      check(/Keep anyway/.test(edge) && /Open file/.test(edge) && /three dots/.test(edge) && /Don.t press Delete/.test(edge), `promises ${p}: the Edge pictures follow real Edge (Keep, then Keep anyway behind the arrow, then Open file), with the words for when they look different`);
    }
    const gets = links.filter((l) => /^get aurOS\b/i.test(l.text));
    check(gets.every((l) => l.href === RELEASE_URL), `promises ${p}: every "Get AurOS" link is the download itself (${gets.length})`, gets.map((l) => l.href).join(" "));
    await ctx.close();
  }
  check(facts >= 3 && claims >= 8 && words >= 10, `promises: the pages mark what they promise (${facts} facts, ${claims} claims, ${words} words)`);
  for (const k of Object.keys(FACTS)) console.log(`     the code says ${k} = ${FACTS[k]}`);
  for (const k of Object.keys(CLAIMS)) console.log(`     the code does ${k}: ${CLAIMS[k]}` + (claimsFor(!DEFAULT_FAST)[k] !== CLAIMS[k] ? ` (with the fast path ${DEFAULT_FAST ? "off" : "ON"}: ${claimsFor(!DEFAULT_FAST)[k]})` : ""));
  console.log(`     the button serves the default build: fast path ${DEFAULT_FAST ? "ON" : "off"} (build/aurbridge)`);
  // The time she is promised may not be below what the real-Windows journey clock measured.
  {
    let J = null; try { J = JSON.parse(read("tools/site/journey.json")); } catch {}
    const shown = FACTS["time-ssd"].match(/(\d+) to (\d+)/);
    check(J !== null && shown !== null, "journey: tools/site/journey.json is there, and the page's time reads 'N to M'");
    check(J && Number.isFinite(J.ssd_median_min) && Array.isArray(J.runs) && J.runs.length > 0 && J.runs.every((r) => r.run && r.get_aurOS_to_restart_pressed), "journey: the floor is wired (ssd_median_min is a number, with the e2e runs it came from)", J && JSON.stringify(J.ssd_median_min));
    if (J && shown) {
      if (J.ssd_median_min == null) console.log(`     journey clock: no measured median yet (${J.source}); the time shown is Setup's own estimate`);
      else check(Number(shown[2]) >= Number(J.ssd_median_min), `journey: the time shown (up to ${shown[2]} minutes) is not below the measured median (${J.ssd_median_min})`, J.source);
    }
  }

  // Floors on every real page, not only the front door.
  for (const p of real.filter((f) => f !== "index.html" && f !== "advanced.html")) {
    for (const viewport of [{ width: 1366, height: 768 }, { width: 390, height: 844 }]) {
      const { ctx, page } = await visit({ name: p, url: HOME + p, ua: UA.winEdge, uad: UAD.winX64, viewport });
      textFindings(`${p} ${viewport.width}x${viewport.height}`, await page.evaluate(measureText));
      await ctx.close();
    }
  }

  // The four looks: four buttons, one picture each; without script, all four.
  {
    const { ctx, page } = await visit({ name: "looks", ua: UA.winEdge, uad: UAD.winX64 });
    const shown = () => page.evaluate(() => [...document.querySelectorAll(".look")].filter((f) => f.getClientRects().length).map((f) => f.id));
    check(JSON.stringify(await shown()) === '["look-taskbar"]', "looks: one picture at a time, Like Windows first", JSON.stringify(await shown()));
    const tabs = await page.evaluate(() => [...document.querySelectorAll(".looks-tabs [role=tab]")].filter((b) => b.getClientRects().length).map((b) => b.textContent));
    check(tabs.length === 4, "looks: four buttons to choose with", JSON.stringify(tabs));
    await page.click("#tab-dock", { timeout: 3000 }).catch(() => {});
    check(JSON.stringify(await shown()) === '["look-dock"]', "looks: pressing Like a Mac shows Like a Mac", JSON.stringify(await shown()));
    await page.focus("#tab-dock"); await page.keyboard.press("ArrowRight");
    check(JSON.stringify(await shown()) === '["look-rail"]' && await page.evaluate(() => document.activeElement.id) === "tab-rail", "looks: the arrow keys move between them");
    await ctx.close();
    const nojs = await visit({ name: "looks no script", ua: UA.linux, js: false });
    const all = await nojs.page.evaluate(() => [...document.querySelectorAll(".look")].filter((f) => f.getClientRects().length).length);
    check(all === 4, "looks: without script all four pictures show", `${all}`);
    await nojs.ctx.close();
  }

  // Sending the page to herself, from a phone or a Mac: the share sheet where
  // there is one, else Copy the link; either way a big sentence says what
  // happened and what to do next.
  for (const v of [
    { name: "iPhone", device: "iPhone 13", uad: null },
    { name: "Android phone", device: "Pixel 7", uad: UAD.android },
    { name: "Mac (Safari)", ua: UA.macSafari, uad: null },
  ]) {
    // with a share sheet
    let { ctx, page } = await visit({ ...v, name: v.name + " with a share sheet", init: { fn: () => { window.__shared = []; Object.defineProperty(navigator, "share", { configurable: true, value: (d) => { window.__shared.push(d); return window.__shareAnswer ? window.__shareAnswer() : Promise.resolve(); } }); } } });
    const pageUrl = await page.evaluate(() => location.href.split("#")[0]);
    const btn = await page.evaluate(rectOf, "#share-link");
    check(btn && btn.shown && btn.height >= 56 && /send this page to myself/i.test(btn.text), `${v.name}: "Send this page to myself" is there and at least 56px tall`, btn && `${Math.round(btn.height)}px ${JSON.stringify(btn.text)}`);
    if (v.device) check(btn && inFold(btn), `${v.name}: it is wholly on the first screen`, btn && `at ${Math.round(btn.top)}-${Math.round(btn.bottom)} of ${btn.vh}`);
    await page.click("#share-link");
    await page.waitForTimeout(150);
    const shared = await page.evaluate(() => window.__shared);
    check(shared.length === 1 && shared[0].url === pageUrl && /windows computer/i.test(shared[0].text || ""), `${v.name}: pressing it opens the share sheet with this page's address`, JSON.stringify(shared));
    const done = await page.evaluate(() => { const e = document.getElementById("copied"); const cs = getComputedStyle(e); return { text: e.innerText, size: parseFloat(cs.fontSize), shown: e.getClientRects().length > 0 }; });
    check(done.shown && done.size >= 22 && /now open it on your windows computer/i.test(done.text), `${v.name}: afterwards a big sentence says what to do next`, `${done.size}px ${JSON.stringify(done.text)}`);
    textFindings(`${v.name} after sharing`, await page.evaluate(measureText));
    await ctx.close();
    // Cancel is not an error; any other failure becomes Copy the link
    ({ ctx, page } = await visit({ ...v, name: v.name + " cancelling", init: { fn: () => { window.__shareAnswer = () => Promise.reject(Object.assign(new Error("x"), { name: "AbortError" })); Object.defineProperty(navigator, "share", { configurable: true, value: () => window.__shareAnswer() }); } } }));
    await page.click("#share-link"); await page.waitForTimeout(150);
    check((await page.evaluate(() => document.getElementById("copied").innerText)) === "", `${v.name}: Cancel in the share sheet says nothing and copies nothing`);
    await ctx.close();
    ({ ctx, page } = await visit({ ...v, name: v.name + " share failing", init: { fn: () => { Object.defineProperty(navigator, "share", { configurable: true, value: () => Promise.reject(Object.assign(new Error("x"), { name: "NotAllowedError" })) }); } } }));
    await page.click("#share-link"); await page.waitForTimeout(300);
    const fb = await page.evaluate(() => document.getElementById("copied").innerText);
    check(/^Copied\.|press and hold this address/i.test(fb), `${v.name}: if the share sheet fails, the link is copied (or shown) instead`, JSON.stringify(fb));
    await ctx.close();
    // without one
    ({ ctx, page } = await visit({ ...v, name: v.name + " without a share sheet", init: { fn: () => { Object.defineProperty(navigator, "share", { configurable: true, value: undefined }); } } }));
    const none = await page.evaluate(rectOf, "#share-link");
    check(!none || !none.shown, `${v.name}: no share button where the browser has no share sheet`);
    await page.click("#copy-link"); await page.waitForTimeout(300);
    const cp = await page.evaluate(() => { const e = document.getElementById("copied"); const r = e.getBoundingClientRect(); const cs = getComputedStyle(e); return { text: e.innerText, size: parseFloat(cs.fontSize), right: r.right, vw: innerWidth }; });
    check(/^Copied\. Now paste it|press and hold this address/i.test(cp.text) && cp.size >= 22 && cp.right <= cp.vw, `${v.name}: Copy the link answers in a big sentence that fits the screen`, `${cp.size}px ${JSON.stringify(cp.text)}`);
    textFindings(`${v.name} after copying`, await page.evaluate(measureText));
    if (v.name === "iPhone") { await page.evaluate(() => { document.documentElement.style.scrollBehavior = "auto"; document.getElementById("copied").scrollIntoView({ block: "center" }); }); await page.waitForTimeout(100); await shot(page, "index-phone-copied.png"); }
    await ctx.close();
  }

  // The usability walkthrough's remaining fixes (docs/research/people-walkthrough-2026-10.md):
  // A-02, A-05, A-06, A-09, B-03, B-04, B-06.
  {
    // A-02: she does not know which Windows she has, and decides here whether this is for her.
    for (const v of [{ name: "1366x768", viewport: { width: 1366, height: 768 } }, { name: "390x844", viewport: { width: 390, height: 844 } }]) {
      const { ctx, page } = await visit({ name: "A-02 " + v.name, ua: UA.winEdge, uad: UAD.winX64, viewport: v.viewport });
      const r = await page.evaluate(() => { const p = [...document.querySelectorAll("#getbox p")].find((e) => /not sure which windows/i.test(e.innerText)); if (!p) return null; const b = p.getBoundingClientRect(); return { text: p.innerText, bottom: b.bottom, vh: innerHeight, shown: b.width > 0 }; });
      check(r && r.shown && /press get aurOS/i.test(r.text) && /changes nothing if it can.t go on/i.test(r.text) && r.bottom <= r.vh, `A-02 ${v.name}: under the button, "Not sure which Windows you have?" says to press it, and that Setup changes nothing if it can't go on, on the first screen`, r ? `${JSON.stringify(r.text)} ends at ${Math.round(r.bottom)} of ${r.vh}` : "no such line under the button");
      await ctx.close();
    }
    // A-05: after the press she is shown where she is: step 1, "your browser is asking"; the download still goes ahead.
    for (const sel of ["#get", ".bar .pill"]) {
      const { ctx, page } = await visit({ name: "A-05 " + sel, ua: UA.winEdge, uad: UAD.winX64, viewport: { width: 1366, height: 768 } });
      await page.evaluate(() => { window.__prevented = null; window.addEventListener("click", (e) => { window.__prevented = e.defaultPrevented; e.preventDefault(); }); });
      const before = await page.evaluate(() => { const e = document.getElementById("here"); return e ? e.innerText.trim() : "missing"; });
      await page.click(sel);
      await page.waitForTimeout(900);
      for (let i = 0, last = -1; i < 40; i++) { const y = await page.evaluate(() => scrollY); if (y === last) break; last = y; await page.waitForTimeout(250); } // the smooth scroll has come to rest
      const after = await page.evaluate(() => { const h = document.getElementById("here"); const st = document.getElementById("step-download").getBoundingClientRect(); return { shown: !!h && h.getClientRects().length > 0, text: h ? h.innerText : "", live: h ? (h.getAttribute("role") || h.getAttribute("aria-live")) : null, top: st.top, y: scrollY, prevented: window.__prevented }; });
      check(before === "", `A-05 ${sel}: before the press nothing says "you are here"`, JSON.stringify(before));
      check(after.shown && /you are here/i.test(after.text) && /asking if you trust the file/i.test(after.text) && after.live, `A-05 ${sel}: after the press, "You are here: your browser is asking if you trust the file", announced`, JSON.stringify(after));
      check(after.y > 300 && after.top >= 0 && after.top < 300, `A-05 ${sel}: and step 1 is brought into view`, `step 1 at ${Math.round(after.top)}, scrolled ${Math.round(after.y)}`);
      check(after.prevented === false, `A-05 ${sel}: the page does not stop the download`, String(after.prevented));
      if (sel === "#get") await shot(page, "index-after-press.png");
      await ctx.close();
    }
    // A-06 and B-03 and B-06 and A-09, on the front page
    {
      const { ctx, page } = await visit({ name: "walkthrough index", ua: UA.winEdge, uad: UAD.winX64 });
      const t = await page.evaluate(() => ({
        helpers: [...document.querySelectorAll(".step .helper a")].map((a) => ({ text: a.innerText, h: a.getBoundingClientRect().height, href: a.getAttribute("href") })),
        three: (document.querySelector("#steps .head p:last-child") || {}).innerText || "",
        sac: (() => { const e = document.getElementById("smart-app-control"); return e ? { text: e.innerText, link: !!e.querySelector('a[href="advanced.html#smart-app-control"]') } : null; })(),
        files: (() => { const h = [...document.querySelectorAll(".qa h3")].find((x) => /lose my files/i.test(x.innerText)); return h ? h.parentElement.innerText : ""; })(),
      }));
      check(t.helpers.length === 3 && t.helpers.every((a) => a.text === "Stuck? Ask someone to help" && a.h >= 48 && a.href === "guide.html"), "A-06: each step has a 48px button that says Stuck? Ask someone to help, to the page that tells her what to do", JSON.stringify(t.helpers));
      check(/keep\b[^.]*run anyway[^.]*\byes\b/i.test(t.three) && /never[^.]*delete[^.]*don.t run[^.]*\bno\b/i.test(t.three) && /numbered circles/i.test(t.three), "B-03: before step 1, the three answers once (Keep, Run anyway, Yes) and the three never to press (Delete, Don't run, No)", JSON.stringify(t.three));
      check(t.sac && t.sac.link && /windows setting/i.test(t.sac.text) && /nothing was changed/i.test(t.sac.text) && !/can.t go on this computer/i.test(t.sac.text), "B-06: Smart App Control is a Windows setting a helper can change, not \"AurOS can't go on\"; nothing was changed; with the way to the helper's page", t.sac ? JSON.stringify(t.sac.text) : "no #smart-app-control");
      check(!/^No\./.test(t.files) && /made to keep/i.test(t.files) && /memory stick/i.test(t.files), "A-09: \"Will I lose my files?\" does not say an absolute No beside advice to copy photos: it says made to keep, and copy what you treasure first", JSON.stringify(t.files));
      await ctx.close();
    }
    // A-06 (what she does when stuck, first), B-04 (the keyboard for the small buttons): the page the button opens
    for (const v of [{ name: "1366x768", viewport: { width: 1366, height: 768 } }, { name: "390x844", viewport: { width: 390, height: 844 } }]) {
      const { ctx, page } = await visit({ name: "guide " + v.name, url: HOME + "guide.html", ua: UA.winEdge, uad: UAD.winX64, viewport: v.viewport });
      const g = await page.evaluate(() => { const h = document.querySelector(".page-head"); const pr = document.getElementById("print").getBoundingClientRect(); return { head: h.innerText, printBottom: pr.bottom, vh: innerHeight, all: document.body.innerText }; });
      check(/close/i.test(g.head) && /nothing has been changed/i.test(g.head) && /show this page to someone you trust/i.test(g.head) && g.printBottom <= g.vh, `A-06 guide ${v.name}: the first screen is what she does (close it, Not now changes nothing, show this to someone she trusts), then Print`, JSON.stringify(g.head.slice(0, 160)));
      if (v.name === "1366x768") check(/press tab until/i.test(g.all) && /enter/i.test(g.all) && /small button/i.test(g.all), "B-04: the helper's page gives the keyboard way for the small buttons (Tab until it is outlined, then Enter)");
      await ctx.close();
    }
  }

  // THE PICTURES, tied to what they show (docs/handoff/GATE-STATUS.md D-P0-2, D-P0-3). website/assets/shots/pictures.json
  // (tools/site/pictures.py) records each picture's source render and its hash, every line of text in it, and where its rings go.
  {
    const sha = (rel) => { try { return crypto.createHash("sha256").update(fs.readFileSync(path.join(REPO, rel))).digest("hex"); } catch { return null; } };
    const joined = (src) => src.replace(/"\s*\n\s*L?"/g, "").replace(/\\u([0-9a-fA-F]{4})/g, (_, h) => String.fromCharCode(parseInt(h, 16))).replace(/\\"/g, '"');
    let P = null; try { P = JSON.parse(read("website/assets/shots/pictures.json")); } catch {}
    check(P !== null && Object.keys(P.pictures || {}).length === 7, "pictures: website/assets/shots/pictures.json describes the 3 Setup and 4 look pictures", P ? Object.keys(P.pictures || {}).join(",") : "missing");
    const wizSrc = joined([wizard, fixWin, read("src/aurbridge/preflight.c")].join("\n"));
    const shellFiles = (() => { const out = []; const walk = (d) => { for (const f of fs.readdirSync(path.join(REPO, d), { withFileTypes: true })) { if (f.isDirectory()) walk(d + "/" + f.name); else if (/\.[ch]$/.test(f.name)) out.push(d + "/" + f.name); } }; walk("src/aurshell"); return out; })();
    const shellSrc = joined(shellFiles.map(read).join("\n") + "\n" + read("build/forge"));
    const eta = FACTS["time-ssd"];
    for (const [file, e] of Object.entries((P || {}).pictures || {})) {
      const isSetup = file.startsWith("setup-");
      check(sha(e.source) === e.source_sha256, `pictures: ${file} was made from the current ${e.source}`, "that render changed: run python3 tools/site/pictures.py and look at the result");
      const src = isSetup ? wizSrc : shellSrc;
      const gone = e.says.filter((line) => {
        const t = line.replace(/\d+ to \d+ (minutes|hours)/, "%ls");
        return !src.includes(t);
      });
      check(gone.length === 0, `pictures: every line ${file} shows is still a string in ${isSetup ? "the installer" : "the shell or build/forge's program names"} (${e.says.length} lines)`, gone.map((g) => JSON.stringify(g)).join(" | "));
      check(!e.says.some((l) => /\.\.\.|…/.test(l)), `pictures: ${file} shows no text cut short with an ellipsis`);
      const range = e.says.map((l) => l.match(/(\d+ to \d+) (minutes|hours)/)).find(Boolean);
      if (range) check(range[1] === eta, `pictures: the time in ${file} (${range[1]}) is what Setup says now (${eta})`);
    }
    // Ready: a row Setup added since the picture was made would make it promise less than the installer says.
    {
      const region = (wizard.match(/struct \{ int glyph; const wchar_t \*text; \} row\[16\];[\s\S]*?#undef ROW/) || [""])[0];
      const lits = [...joined(region).matchAll(/L"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]).filter((t) => t.length >= 25);
      const optional = [/^Windows’ drive lock is paused/, /^AurOS has already downloaded/, /^\s*This computer has an older, slower kind of drive/];
      const says = (((P || {}).pictures || {})["setup-ready.jpg"] || { says: [] }).says;
      const missing = lits.filter((t) => !optional.some((r) => r.test(t))).filter((t) => { const head = t.split("%ls")[0].trim(); return !says.some((l) => l.startsWith(head)); });
      check(lits.length >= 6 && missing.length === 0, `pictures: Setup's Ready page says nothing more than setup-ready.jpg shows (${lits.length} rows read from wizard.c)`, missing.map((m) => JSON.stringify(m.slice(0, 70))).join(" | "));
    }
    // The page: the rings, and the note about how the Setup pictures were made.
    {
      const { ctx, page } = await visit({ name: "pictures on the page", ua: UA.winEdge, uad: UAD.winX64 });
      const got = await page.evaluate(() => [...document.querySelectorAll(".shot")].map((f) => ({ img: (f.querySelector("img") || {}).getAttribute ? f.querySelector("img").getAttribute("src").split("/").pop() : "", w: +f.querySelector("img").getAttribute("width"), h: +f.querySelector("img").getAttribute("height"),
        rings: [...f.querySelectorAll(".ring")].map((r) => ({ n: r.dataset.n, style: r.getAttribute("style").replace(/\s+/g, "") })) })));
      const want = { "setup-check.jpg": ["1"], "setup-look.jpg": ["2", "3"], "setup-ready.jpg": ["4"] };
      for (const g of got) {
        const e = ((P || {}).pictures || {})[g.img];
        check(e && JSON.stringify(g.rings.map((r) => r.n)) === JSON.stringify(want[g.img]), `pictures: ${g.img} carries the numbered circles ${(want[g.img] || []).join(", ")} (1 to 4 across the three Setup pictures; 5 is the first question)`, JSON.stringify(g.rings.map((r) => r.n)));
        check(e && JSON.stringify(g.rings.map((r) => r.style)) === JSON.stringify(e.rings.map((r) => r.style.replace(/\s+/g, ""))), `pictures: the rings on ${g.img} sit where pictures.py found the buttons`, e ? JSON.stringify(g.rings.map((r) => r.style)) : "unknown picture");
      }
      check(got.length === 3, `pictures: the page shows the three Setup pictures (${got.length})`);
      const note = await page.evaluate(() => { const n = document.getElementById("setup-note"); return n ? { text: n.innerText, shown: n.getClientRects().length > 0 } : null; });
      if (P && P.setup_on_real_windows === false) check(note && note.shown && /test computer/i.test(note.text), "pictures: the Setup pictures are test renders, and the page says so", JSON.stringify(note));
      else check(!note, "pictures: the Setup pictures are real Windows screenshots now, so the 'test computer' note is gone", JSON.stringify(note));
      // The looks: shown big enough to read (560px or wider), the size they were made at, and each one the real thing.
      const widths = [];
      for (const id of ["taskbar", "dock", "rail", "tiles"]) {
        await page.click(`#tab-${id}`);
        widths.push(await page.evaluate(async (i) => { const im = document.querySelector(`#look-${i} img`); im.loading = "eager"; if (!im.complete || !im.naturalWidth) await new Promise((ok) => { im.addEventListener("load", ok, { once: true }); setTimeout(ok, 3000); }); const r = im.getBoundingClientRect(); return { id: i, w: Math.round(r.width), nat: [im.naturalWidth, im.naturalHeight], attr: [+im.getAttribute("width"), +im.getAttribute("height")], src: im.getAttribute("src").split("/").pop() }; }, id));
      }
      for (const w of widths) {
        const e = ((P || {}).pictures || {})[w.src];
        check(w.w >= 560, `pictures: the ${w.id} look is shown ${w.w}px wide (560 or more, at 1366px)`);
        check(e && e.size && w.nat[0] === e.size[0] && w.nat[1] === e.size[1] && w.attr[0] === e.size[0] && w.attr[1] === e.size[1], `pictures: ${w.src} is ${e && e.size ? e.size.join("x") : "?"} and the page says so`, JSON.stringify(w));
      }
      await ctx.close();
    }
  }

  // The helper pages (docs/handoff/GATE-STATUS.md K-P0-1): the warning comes first, and every address the installer sends her to exists.
  {
    const { ctx, page } = await visit({ name: "advanced.html order", url: HOME + "advanced.html", ua: UA.winEdge, uad: UAD.winX64 });
    const o = await page.evaluate(() => {
      const sec = document.getElementById("smart-app-control");
      const warn = sec && sec.querySelector(".stop"), step = sec && sec.querySelector("ol.doing li");
      if (!warn || !step) return null;
      return { first: !!(warn.compareDocumentPosition(step) & Node.DOCUMENT_POSITION_FOLLOWING), shown: warn.getClientRects().length > 0, text: warn.innerText,
               ids: [...document.querySelectorAll("[id]")].map((e) => e.id) };
    });
    check(o && o.shown && o.first && /before switching it off/i.test(o.text) && /protection/i.test(o.text), "K-P0-1: the Smart App Control warning box (what it takes away) comes before the first step, in advanced.html#smart-app-control", o ? JSON.stringify({ first: o.first, text: o.text.slice(0, 80) }) : "no .stop box or no steps in #smart-app-control");
    const wanted = new Set();
    for (const f of ["src/aurbridge/fix_win.c", "src/aurbridge/wizard.c", "src/aurbridge/sbdb.c", "src/aurbridge/preflight.c"]) for (const m of read(f).matchAll(/ADVANCED\s+L"#([\w-]+)"/g)) wanted.add(m[1]);
    check(wanted.size >= 3, `K-P0-1: the installer names ${wanted.size} anchors on advanced.html (${[...wanted].join(", ")})`);
    for (const id of wanted) check(o && o.ids.includes(id), `K-P0-1: advanced.html#${id}, which Setup links to, exists`);
    await ctx.close();
  }

  // The headline says what AurOS is (as the installer's first page does), not what it will do to her computer.
  {
    const { ctx, page } = await visit({ name: "headline", ua: UA.winEdge, uad: UAD.winX64 });
    const h = await page.evaluate(() => ({ h1: document.getElementById("hero-title").innerText, title: document.title }));
    check(/new way to use/i.test(h.h1) && !/new again/i.test(h.h1 + h.title), "headline: the h1 says what AurOS is ('a simple new way to use...'), and neither it nor the tab title promises 'new again'", JSON.stringify(h));
    await ctx.close();
  }

  // The letters: the zero is a plain oval in every weight the site loads
  // (the upstream zero is slashed; docs/research/people-walkthrough-2026-10.md A-03).
  {
    const ctxGuide = async () => {
      const { ctx: c2, page: p2 } = await visit({ name: "letters guide", url: HOME + "guide.html", ua: UA.winEdge, uad: UAD.winX64 });
      const r = await p2.evaluate(async () => {
        const out = [];
        for (const e of document.querySelectorAll(".addr")) {
          const cs = getComputedStyle(e);
          await document.fonts.load(`${cs.fontWeight} 100px ${cs.fontFamily}`, "0");
          const c = document.createElement("canvas"); c.width = 200; c.height = 160;
          const g = c.getContext("2d"); g.font = `${cs.fontWeight} 100px ${cs.fontFamily}`; g.fillText("0", 40, 120);
          const mid = 40 + g.measureText("0").width / 2;
          const px = g.getImageData(Math.round(mid) - 2, 120 - 35 - 2, 5, 5).data;
          let ink = 0; for (let i = 3; i < px.length; i += 4) ink += px[i];
          out.push({ family: cs.fontFamily, slashed: ink > 0 });
        }
        return out;
      });
      await c2.close();
      return r.length ? r : [{ family: "none", slashed: false }];
    };
    const { ctx, page } = await visit({ name: "letters", ua: UA.winEdge, uad: UAD.winX64 });
    const holes = await page.evaluate(async () => {
      const out = {};
      for (const w of [400, 700, 800]) {
        await document.fonts.load(`${w} 100px "Atkinson Hyperlegible Next"`);
        const c = document.createElement("canvas"); c.width = 200; c.height = 160;
        const g = c.getContext("2d"); g.font = `${w} 100px "Atkinson Hyperlegible Next"`; g.textBaseline = "alphabetic";
        const x = 40; g.fillText("0", x, 120);
        const mid = x + g.measureText("0").width / 2;
        const px = g.getImageData(Math.round(mid) - 2, 120 - 35 - 2, 5, 5).data;
        let ink = 0; for (let i = 3; i < px.length; i += 4) ink += px[i];
        out[w] = { ink, loaded: document.fonts.check(`${w} 100px "Atkinson Hyperlegible Next"`) };
      }
      return out;
    });
    for (const [w, h] of Object.entries(holes)) check(h.loaded && h.ink === 0, `letters: the zero has no slash in the ${w} weight`, JSON.stringify(h));
    // ...except in an address she types: there the old zero, so it is not read as the letter O.
    const g = await ctxGuide();
    check(g.every((x) => x.family.startsWith('"AurOS Zero"') && x.slashed), "letters: the address on the printed sheet keeps the slashed zero", JSON.stringify(g));
    await ctx.close();
  }

  // Small: what the first screen costs to load (pictures further down wait).
  {
    const { ctx, page } = await visit({ name: "weight", ua: UA.winEdge, uad: UAD.winX64 });
    const kb = await page.evaluate(() => Math.round(performance.getEntriesByType("navigation").concat(performance.getEntriesByType("resource"))
      .reduce((s, e) => s + (e.encodedBodySize || 0), 0) / 1024));
    check(kb > 0 && kb <= 300, `weight: the first screen loads ${kb} KB (300 at most)`);
    await ctx.close();
  }
}

// 8. --shots: every real page at the three sizes the site is made for,
//    the first screen and the whole page (docs/shots/website/).
if (SHOTS) {
  const real = fs.readdirSync(SITE).filter((f) => f.endsWith(".html") && !/http-equiv="refresh"/i.test(fs.readFileSync(path.join(SITE, f), "utf8"))).sort();
  for (const p of real) for (const [w, h] of [[1366, 768], [1920, 1080], [390, 844]]) {
    const { ctx, page } = await visit({ name: "shot " + p, url: HOME + p, ua: UA.winEdge, uad: UAD.winX64, viewport: { width: w, height: h } });
    const base = `${p.replace(/\.html$/, "")}-${w}x${h}`;
    await shot(page, base + "-fold.png");
    await shot(page, base + "-full.jpg", true);
    await ctx.close();
  }
}

await browser.close();
server.close();
console.log(`\n${passes} passed, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
