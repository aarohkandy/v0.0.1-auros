import { analyzeConfigureRequest, planFromSelection } from "./configure-generated.js";

var form = document.getElementById("chooser");

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
