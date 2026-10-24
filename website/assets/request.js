import { analyzeConfigureRequest, planFromSelection } from "./configure-generated.js";

var form = document.getElementById("chooser");
var buildController = null;

var LABELS = {
  base: {
    desktop: "My everyday computer",
    revive: "An older, slower computer",
    multilingual: "A multilingual computer",
    office: "Computers for an organisation",
    "school-kiosk": "A locked school computer"
  },
  look: {
    "": "Keep its usual arrangement",
    default: "Keep its usual arrangement",
    locked: "Locked to one browser",
    taskbar: "Like Windows",
    dock: "Like a Mac",
    rail: "Big and simple",
    tiles: "One thing at a time"
  },
  theme: {
    "": "Keep its usual colours",
    default: "Keep its usual colours",
    sandstone: "Light",
    nocturne: "Light writing on a dark background",
    ember: "Ember",
    moss: "Moss",
    slate: "Slate"
  }
};

var selected = function (name) {
  var input = form.querySelector("input[name='" + name + "']:checked");
  return input ? input.value : "";
};

function recipe() {
  var out = { schema: "org.auros.recipe", version: 1, base: selected("base") };
  var look = selected("look");
  var theme = selected("theme");
  var preferences = {};
  if (look && out.base !== "school-kiosk") preferences.look = look;
  if (theme) preferences.theme = theme;
  if (Object.keys(preferences).length) out.preferences = preferences;
  return out;
}

function update() {
  var base = selected("base");
  var kiosk = base === "school-kiosk";
  var lookInputs = form.querySelectorAll("input[name='look']");
  var keep = form.querySelector("input[name='look'][value='']");
  if (kiosk && selected("look")) keep.checked = true;
  Array.prototype.forEach.call(lookInputs, function (input) {
    input.disabled = kiosk && input.value !== "";
  });
  document.getElementById("kiosk-look-note").hidden = !kiosk;
  document.getElementById("look-help").hidden = kiosk;
  document.getElementById("review-base").textContent = LABELS.base[base];
  document.getElementById("review-look").textContent = kiosk ? LABELS.look.locked : LABELS.look[selected("look")];
  document.getElementById("review-theme").textContent = LABELS.theme[selected("theme")];
  document.getElementById("recipe-preview").textContent = JSON.stringify(recipe(), null, 2);
  if (buildController) buildController.choicesChanged();
}

form.addEventListener("change", update);
form.addEventListener("reset", function () { window.setTimeout(update, 0); });
form.addEventListener("submit", function (event) {
  event.preventDefault();
  var contents = JSON.stringify(recipe(), null, 2) + "\n";
  var blob = new Blob([contents], { type: "application/json" });
  var href = URL.createObjectURL(blob);
  var link = document.createElement("a");
  link.href = href;
  link.download = "auros-recipe.json";
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  window.setTimeout(function () { URL.revokeObjectURL(href); }, 0);
  document.getElementById("recipe-status").textContent = "Saved as auros-recipe.json. It has not been sent anywhere.";
});

var plain = document.getElementById("plain-request");
var understand = document.getElementById("understand-request");
var proposal = document.getElementById("request-proposal");
var accept = document.getElementById("accept-request");
var message = document.getElementById("request-words-status");
var count = document.getElementById("request-count");
var pending = null;
var revision = 0;

var REASONS = {
  hardware_sensor: "That needs hardware-sensor support and new tested code.",
  custom_behavior: "That asks AurOS to do something outside the safe recipe choices.",
  programs: "Adding or naming programs is not supported by this recipe preview.",
  language: "Choosing a language is not supported by recipe version 1.",
  managed_setup: "Managed setup still needs an organisation or school administrator.",
  unsupported_preference: "That asks for choices which cannot safely be used together.",
  other: "Part of that request is outside the safe recipe choices.",
  unclear: "I could not match that request to the choices on this page."
};

function clearProposal() {
  pending = null;
  proposal.hidden = true;
}

function say(text, ok) {
  message.textContent = text;
  message.classList.toggle("ok", Boolean(ok));
}

plain.addEventListener("input", function () {
  revision += 1;
  count.textContent = plain.value.length + " of 300 characters";
  if (pending) say("The words changed. Press Understand my request again before using any choices.", false);
  clearProposal();
});

understand.addEventListener("click", function () {
  clearProposal();
  var text = plain.value.trim();
  if (!text) {
    say("Write what you want first. Nothing was changed.", false);
    plain.focus();
    return;
  }
  var analysis = analyzeConfigureRequest(text);
  if (analysis.reason) {
    say((REASONS[analysis.reason] || REASONS.unclear) + " Nothing was changed; use the choices below or ask for new code and testing.", false);
    return;
  }
  var plan;
  try {
    plan = planFromSelection(analysis.expected);
  } catch (error) {
    say(REASONS.unsupported_preference + " Nothing was changed; use the choices below.", false);
    return;
  }
  pending = { revision: revision, selection: analysis.expected };
  document.getElementById("proposal-base").textContent = LABELS.base[analysis.expected.base];
  document.getElementById("proposal-look").textContent = LABELS.look[plan.resolved_preferences.look];
  document.getElementById("proposal-theme").textContent = LABELS.theme[plan.resolved_preferences.theme];
  proposal.hidden = false;
  say("I found three choices. Check them below. The form has not changed.", true);
  proposal.scrollIntoView({ block: "nearest" });
});

accept.addEventListener("click", function () {
  if (!pending || pending.revision !== revision) {
    clearProposal();
    say("The words changed. Press Understand my request again. Nothing was changed.", false);
    return;
  }
  var selection = pending.selection;
  form.querySelector("input[name='base'][value='" + selection.base + "']").checked = true;
  form.querySelector("input[name='look'][value='" + (selection.look === "default" ? "" : selection.look) + "']").checked = true;
  form.querySelector("input[name='theme'][value='" + (selection.theme === "default" ? "" : selection.theme) + "']").checked = true;
  update();
  clearProposal();
  say("Those choices are now in the form below. Review them once more before saving the recipe.", true);
  document.getElementById("base-title").scrollIntoView({ block: "start" });
});

update();

var RECEIPT_KEY = "auros.build-receipt.v1";
var POLL_MS = 15000;
var MAX_POLLS = 40;
var buildBox = document.getElementById("build-request");
var buildButton = document.getElementById("request-build");
var checkButton = document.getElementById("check-build");
var buildStatus = document.getElementById("build-status");
var buildResult = document.getElementById("build-result");
var pollTimer = 0;
var polls = 0;
var activeReceipt = null;
var submitting = false;

function exactApiOrigin(value) {
  try {
    var url = new URL(value);
    return url.protocol === "https:" && !url.username && !url.password && url.pathname === "/" && !url.search && !url.hash ? url.origin : "";
  } catch (error) { return ""; }
}

function recipeSummary(value) {
  var prefs = value.preferences || {};
  return LABELS.base[value.base] + "; " + LABELS.look[prefs.look || ""] + "; " + LABELS.theme[prefs.theme || ""];
}

function sameRecipe(a, b) { return JSON.stringify(a) === JSON.stringify(b); }
function setBuildStatus(text, ok) {
  buildStatus.textContent = text;
  buildStatus.classList.toggle("ok", Boolean(ok));
}
function stopPolling() { window.clearTimeout(pollTimer); pollTimer = 0; }
function saveReceipt(value) {
  activeReceipt = value;
  try { localStorage.setItem(RECEIPT_KEY, JSON.stringify(value)); return true; }
  catch (error) { return false; }
}
function validStoredRecipe(value) {
  if (!value || value.schema !== "org.auros.recipe" || value.version !== 1 || value.base !== "desktop") return false;
  var keys = Object.keys(value).sort().join(",");
  if (keys !== "base,schema,version" && keys !== "base,preferences,schema,version") return false;
  if (!value.preferences) return true;
  var preferenceKeys = Object.keys(value.preferences).sort();
  return preferenceKeys.length > 0 && preferenceKeys.every(function (key) {
    return key === "look" ? ["taskbar", "dock", "rail", "tiles"].includes(value.preferences[key]) :
      key === "theme" && ["ember", "moss", "nocturne", "sandstone", "slate"].includes(value.preferences[key]);
  });
}
function validReceipt(value, apiOrigin) {
  return value && value.api_origin === apiOrigin && /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(value.id || "") &&
    /^[A-Za-z0-9_-]{32}$/.test(value.token || "") && /^sha256:[0-9a-f]{64}$/.test(value.recipe_id || "") && validStoredRecipe(value.recipe);
}
function loadReceipt(apiOrigin) {
  try {
    var value = JSON.parse(localStorage.getItem(RECEIPT_KEY));
    return validReceipt(value, apiOrigin) ? value : null;
  } catch (error) { return null; }
}
function apiError(code, status) {
  var known = {
    desktop_only: "Only My everyday computer can be built by this service.",
    base_unavailable: "This starting point is not available for a tested build yet.",
    builds_disabled: "The build service is unavailable right now.",
    network_quota: "This network has reached the temporary request limit. Try again later.",
    global_quota: "The build service has reached its temporary request limit. Try again later.",
    challenge: "The one-use request expired or was refused. Press the button once to try a new request.",
    challenge_used: "That one-use request expired. Press the button once to try a new request.",
    receipt: "The saved build receipt is invalid or expired.",
    not_found: "The saved build receipt is wrong or has expired.",
    origin: "This copy of the page is not allowed to use the build service.",
    dispatch_rejected: "The build service could not start this build. Nothing was resubmitted."
  };
  return known[code] || (status === 401 ? "The saved build receipt is invalid or expired." : "The build service refused this request (" + status + ").");
}
async function jsonResponse(response) {
  var body;
  try { body = await response.json(); } catch (error) { throw new Error("The build service returned an unreadable response."); }
  if (!response.ok) throw new Error(apiError(body && body.error, response.status));
  return body;
}
async function solveProof(prefix, challenge, bits, cancelled) {
  if (prefix !== "auros-build-v1:" || typeof challenge !== "string" || challenge.length < 16 || !Number.isInteger(bits) || bits < 0 || bits > 28) throw new Error("The build service returned an invalid one-use challenge.");
  var encoder = new TextEncoder();
  var deadline = Date.now() + 30000;
  for (var start = 0; start < 5000000; start += 128) {
    if (cancelled() || document.hidden) throw new Error("The one-use request stopped because the choices changed or this page was hidden. Press the button to try again.");
    if (Date.now() > deadline) throw new Error("The one-use request took too long. Press the button to try again.");
    var hashes = await Promise.all(Array.from({ length: 128 }, function (_, offset) {
      return crypto.subtle.digest("SHA-256", encoder.encode(prefix + challenge + ":" + (start + offset)));
    }));
    for (var offset = 0; offset < hashes.length; offset += 1) {
      var digest = new Uint8Array(hashes[offset]);
      var left = bits;
      for (var i = 0; left > 0; i += 1) {
        var take = Math.min(8, left);
        if ((digest[i] >>> (8 - take)) !== 0) break;
        left -= take;
      }
      if (left === 0) return String(start + offset);
    }
    await new Promise(function (resolve) { window.setTimeout(resolve, 0); });
  }
  throw new Error("The one-use request reached its safe work limit. Press the button to try again.");
}
function validJob(value) {
  return value && /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(value.id || "") &&
    /^sha256:[0-9a-f]{64}$/.test(value.recipe_id || "") && ["dispatch-uncertain", "queued", "running", "failed", "download-ready"].includes(value.state) && typeof value.download_ready === "boolean";
}
function safeDownload(value) {
  if (!value || typeof value !== "object" || !/^AurOS-Custom-[0-9a-f]{64}\.exe$/.test(value.name || "") || !/^[0-9a-f]{64}$/.test(value.sha256 || "") || !Number.isSafeInteger(value.bytes) || value.bytes <= 0) return null;
  try {
    var url = new URL(value.url);
    var manifest = new URL(value.manifest_url);
    var urlParts = url.pathname.split("/");
    var manifestParts = manifest.pathname.split("/");
    var good = !url.pathname.includes("%") && !manifest.pathname.includes("%") && url.protocol === "https:" && url.hostname === "github.com" && !url.username && !url.password && !url.search && !url.hash &&
      /^\/aarohkandy\/v0\.0\.1-auros\/releases\/download\/[^/]+\/[^/]+\.exe$/.test(url.pathname) && decodeURIComponent(url.pathname.split("/").pop()) === value.name &&
      manifest.protocol === "https:" && manifest.hostname === "github.com" && !manifest.username && !manifest.password && !manifest.search && !manifest.hash &&
      /^\/aarohkandy\/v0\.0\.1-auros\/releases\/download\/[^/]+\/[^/]+\.json$/.test(manifest.pathname) && urlParts[5] === manifestParts[5] &&
      manifestParts[6] === value.name.replace(/\.exe$/, ".json");
    return good ? { name: value.name, url: url.href, sha256: value.sha256, bytes: value.bytes } : null;
  } catch (error) { return null; }
}
function schedulePoll() {
  stopPolling();
  if (!document.hidden && activeReceipt && polls < MAX_POLLS) pollTimer = window.setTimeout(checkStatus, POLL_MS);
  if (polls >= MAX_POLLS) { checkButton.hidden = false; setBuildStatus("Automatic checking paused. Press Check build status when you are ready.", false); }
}
async function checkStatus() {
  stopPolling();
  if (!activeReceipt || document.hidden) return;
  polls += 1;
  try {
    var response = await fetch(activeReceipt.api_origin + "/v1/builds/" + activeReceipt.id, { headers: { Authorization: "Bearer " + activeReceipt.token }, cache: "no-store" });
    var job = await jsonResponse(response);
    if (!validJob(job) || job.id !== activeReceipt.id || job.recipe_id !== activeReceipt.recipe_id) throw new Error("The build service returned a status for different choices. The download was blocked.");
    if (job.state === "failed") {
      checkButton.hidden = false;
      setBuildStatus("The build failed: " + (typeof job.failure === "string" && job.failure ? job.failure : "the service did not give a reason") + ". It was not resubmitted.", false);
      return;
    }
    if (job.state === "download-ready" || job.download_ready) {
      var file = job.state === "download-ready" && job.download_ready ? safeDownload(job.download) : null;
      if (!file) throw new Error("The service did not provide a valid signed AurOS EXE. The download was blocked.");
      document.getElementById("build-choices").textContent = recipeSummary(activeReceipt.recipe);
      document.getElementById("build-file").textContent = file.name + " (" + file.bytes.toLocaleString() + " bytes)";
      document.getElementById("build-sha256").textContent = file.sha256;
      var link = document.getElementById("build-download");
      link.href = file.url;
      buildResult.hidden = false;
      checkButton.hidden = true;
      setBuildStatus("Your tested, signed build is ready. The file and SHA-256 below belong to the choices you submitted.", true);
      return;
    }
    buildResult.hidden = true;
    checkButton.hidden = true;
    setBuildStatus(job.state === "running" ? "AurOS is building. Signing must pass before a download can appear." : "Your request is waiting safely. No download is ready yet.", true);
    schedulePoll();
  } catch (error) {
    checkButton.hidden = false;
    setBuildStatus(error instanceof Error ? error.message + " Nothing was resubmitted." : "The status could not be checked. Nothing was resubmitted.", false);
  }
}
async function requestBuild(apiOrigin) {
  if (submitting) return;
  if (activeReceipt) { setBuildStatus("This browser already has a build receipt. Check that build instead of submitting it again.", false); return; }
  var submitted = recipe();
  if (submitted.base !== "desktop") { setBuildStatus("Only My everyday computer can be built right now. Your choices were not partly submitted.", false); return; }
  submitting = true;
  buildButton.disabled = true;
  setBuildStatus("Preparing a one-use request in this browser…", true);
  try {
    var challenge = await jsonResponse(await fetch(apiOrigin + "/v1/builds/challenge", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(submitted) }));
    var proof = await solveProof(challenge.proof_prefix, challenge.challenge, challenge.bits, function () { return !sameRecipe(submitted, recipe()); });
    if (!sameRecipe(submitted, recipe())) throw new Error("Your choices changed while the request was being prepared. Nothing was submitted.");
    var response = await fetch(apiOrigin + "/v1/builds", { method: "POST", headers: { "Content-Type": "application/json", "X-AurOS-Challenge": challenge.challenge, "X-AurOS-Proof": proof }, body: JSON.stringify(submitted) });
    var job = await jsonResponse(response);
    var receipt = { api_origin: apiOrigin, id: job.id, token: job.token, recipe_id: job.recipe_id, recipe: submitted };
    if (!validReceipt(receipt, apiOrigin) || !validJob({ id: job.id, recipe_id: job.recipe_id, state: job.state, download_ready: job.download_ready })) throw new Error("The build service did not return a valid receipt.");
    var persisted = saveReceipt(receipt);
    polls = 0;
    setBuildStatus(persisted ? "Your build request was accepted. This browser saved its private receipt and will check the real status." : "Your build request was accepted, but this browser could not save its receipt. Keep this page open while it checks the status.", persisted);
    await checkStatus();
  } catch (error) {
    setBuildStatus(error instanceof Error ? error.message : "The build request could not be sent.", false);
  } finally { submitting = false; buildButton.disabled = Boolean(activeReceipt); }
}

async function enableBuildService() {
  try {
    var response = await fetch("build-service.json", { cache: "no-store" });
    var config = response.ok ? await response.json() : null;
    if (!config || config.enabled !== true) return;
    var apiOrigin = exactApiOrigin(config.api_origin);
    if (!apiOrigin) return;
    buildBox.hidden = false;
    document.getElementById("request-boundary").innerHTML = "The file records your choices for a builder or helper. Today&rsquo;s <span class=\"file\">AurOS-Setup.exe</span> does not read it. Saving the recipe stays local; the optional tested-build request below sends only the reviewed recipe fields.";
    document.querySelector(".foot-legal + p").textContent = "No cookies and no ads. The optional build service receives only the reviewed recipe fields, keeps a job and receipt hash, and keeps one-way network hashes to prevent abuse. GitHub, which hosts this site, keeps its own records of the internet addresses that visit. Write to us: CONTACT_EMAIL.";
    buildController = { choicesChanged: function () {
      buildResult.hidden = true;
      if (activeReceipt && !sameRecipe(activeReceipt.recipe, recipe())) setBuildStatus("These choices differ from the saved build request. Its status remains tied to: " + recipeSummary(activeReceipt.recipe) + ".", false);
    } };
    buildButton.addEventListener("click", function () { requestBuild(apiOrigin); });
    checkButton.addEventListener("click", function () { polls = 0; checkStatus(); });
    document.addEventListener("visibilitychange", function () { if (document.hidden) stopPolling(); else if (activeReceipt) checkStatus(); });
    activeReceipt = loadReceipt(apiOrigin);
    if (activeReceipt) {
      buildButton.disabled = true;
      setBuildStatus("Recovered the saved receipt for: " + recipeSummary(activeReceipt.recipe) + ". Checking its status without submitting again.", true);
      await checkStatus();
    }
  } catch (error) {
    buildBox.hidden = true;
  }
}

enableBuildService();
