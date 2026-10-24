(function () {
  "use strict";

  var form = document.getElementById("chooser");
  if (!form) return;

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
      taskbar: "Like Windows",
      dock: "Like a Mac",
      rail: "Big and simple",
      tiles: "One thing at a time"
    },
    theme: {
      "": "Keep its usual colours",
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
    document.getElementById("review-look").textContent = kiosk ? "Locked to one browser" : LABELS.look[selected("look")];
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

  update();
})();
