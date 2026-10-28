/* AurOS front door (index.html). Everything here is optional: without
 * script the page shows the Windows path, the button and every browser's
 * steps, which is the right default. Nothing here sends anything.
 *
 * Every download link carries the release address in the page itself
 * (so it works without script, and the e2e run can point them all at
 * its own file). On GitHub Pages this works the address out again from
 * the page's own, below, and gives it to every download link.
 */
(function () {
  "use strict";

  var root = document.documentElement;
  var $ = function (id) { return document.getElementById(id); };
  var get = $("get");
  var DOWNLOAD_URL = get.getAttribute("href");

  /* On GitHub Pages the page's own address says where the releases are:
   * https://<owner>.github.io/<name>/ is github.com/<owner>/<name>, and
   * the repository <owner>.github.io is the site at the top. Anywhere else
   * (a custom domain, a test server) the href above stands: the deploy
   * writes the repository it publishes from into it (tools/site/stamp.sh). */
  var gh = /^([a-z0-9-]+)\.github\.io$/i.exec(location.hostname);
  /* Only while the button still points at GitHub's release address: on the owner's own
   * domain it points at get.<domain> (tools/site/stamp.sh), and that stands even when the
   * page is read through its github.io copy. */
  if (gh && location.protocol === "https:" && /^https:\/\/github\.com\//.test(DOWNLOAD_URL)) {
    var first = location.pathname.split("/")[1] || "";
    var name = (first && !/\.html?$/i.test(first)) ? first : gh[1] + ".github.io";
    if (/^[A-Za-z0-9._-]+$/.test(name)) {
      DOWNLOAD_URL = "https://github.com/" + gh[1] + "/" + name + "/releases/latest/download/AurOS-Setup.exe";
      get.setAttribute("href", DOWNLOAD_URL);
    }
  }

  Array.prototype.forEach.call(document.querySelectorAll("[data-download]"), function (a) { a.href = DOWNLOAD_URL; });

  var ua = navigator.userAgent || "";

  /* ── which browser, so step 1 shows only hers ──────────────────── */
  var browser = /Edg(e|A|iOS)?\//.test(ua) ? "edge"
              : /Firefox\/|FxiOS\//.test(ua) ? "firefox"
              : (/Chrome\/|CriOS\//.test(ua) && !/OPR\/|Opera|Vivaldi|YaBrowser|SamsungBrowser/.test(ua)) ? "chrome"
              : "";
  if (browser) {
    root.classList.add("browser-known", "b-" + browser);
    $("all-browsers-row").hidden = false;
    $("all-browsers").addEventListener("click", function () {
      root.classList.remove("browser-known");
      $("all-browsers-row").hidden = true;
    });
  }

  /* ── which computer ───────────────────────────────────────────────
   * Sure answers only. Anything unsure keeps the button. */
  function osKind() {
    if (/iPhone|iPad|iPod|Android/i.test(ua)) return "phone";
    if (/Macintosh|Mac OS X/.test(ua)) return (navigator.maxTouchPoints > 1) ? "phone" : "mac"; /* iPads say Macintosh */
    if (/CrOS/.test(ua)) return "chromeos";
    if (/Windows/.test(ua)) return "windows";
    if (/Linux|X11/.test(ua)) return "linux";
    return "";
  }

  function pageAddress() {
    var u = location.href.split("#")[0].split("?")[0];
    return u;
  }

  function showElsewhere(kind) {
    root.classList.add("is-elsewhere", "k-" + kind);
    $("getbox").hidden = true;
    $("elsewhere").hidden = false;
    var url = pageAddress();
    $("mail-link").href = "mailto:?subject=" + encodeURIComponent("AurOS, for my Windows computer") +
      "&body=" + encodeURIComponent("Open this on the Windows computer you want to change:\n\n" + url + "\n");
  }

  /* What she is told after pressing: big, and it says what to do next
   * (.copied is a 24px box, and a status for a screen reader). */
  function said(msg) { $("copied").textContent = msg; }

  /* Copy the link. Where the browser won't, the address itself is shown in
   * the same box, to press and hold. */
  function copyLink() {
    var url = pageAddress();
    var ok = function () { said("Copied. Now paste it into a message or an email to yourself."); };
    var fallback = function () {
      var t = document.createElement("textarea");
      t.value = url; t.setAttribute("readonly", ""); t.style.position = "fixed"; t.style.opacity = "0";
      document.body.appendChild(t); t.select();
      var done = false;
      try { done = document.execCommand("copy"); } catch (e) { done = false; }
      document.body.removeChild(t);
      if (done) ok();
      else said("Your browser wouldn't copy it. Press and hold this address, then choose Copy: " + url.replace(/^https?:\/\//, ""));
    };
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(url).then(ok, fallback);
    else fallback();
  }
  $("copy-link").addEventListener("click", copyLink);

  /* "Send this page to myself": the phone's own share sheet (Messages,
   * Mail, Notes, AirDrop...), where it has one. She picks where it goes;
   * pressing Cancel there is not an error. Without a share sheet the
   * button stays hidden and Copy the link is the way. */
  var share = $("share-link");
  if (share && navigator.share) {
    share.hidden = false;
    share.addEventListener("click", function () {
      navigator.share({ title: "AurOS", text: "Open this on the Windows computer you want to change:", url: pageAddress() })
        .then(function () { said("Done. Now open it on your Windows computer."); },
              function (e) { if (!e || e.name !== "AbortError") copyLink(); });
    });
  }

  /* ── the four looks: one big picture, four buttons ───────────────
   * Without script all four pictures stand one under the other. */
  var tabs = document.querySelector(".looks-tabs");
  if (tabs) {
    var btns = [].slice.call(tabs.querySelectorAll("[role=tab]"));
    var pick = function (b, focus) {
      btns.forEach(function (x) {
        var on = x === b;
        x.setAttribute("aria-selected", on ? "true" : "false");
        x.tabIndex = on ? 0 : -1;
        $(x.getAttribute("aria-controls")).classList.toggle("on", on);
      });
      if (focus) b.focus();
    };
    btns.forEach(function (b, i) {
      b.addEventListener("click", function () { pick(b); });
      b.addEventListener("keydown", function (e) {
        var d = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
        if (d) { e.preventDefault(); pick(btns[(i + d + btns.length) % btns.length], true); }
      });
    });
    tabs.hidden = false;
    tabs.parentNode.parentNode.classList.add("looks-tabs-on");
  }

  /* After the press on a Windows computer: the browser's warning appears at the
   * top right while this page still shows the button, and step 1 (with the
   * picture of that warning) is two screens down. Say where she is, and bring
   * step 1 up. Nothing here stops the download. */
  var here = $("here");
  var reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  [].forEach.call(document.querySelectorAll("#get, [data-download]"), function (a) {
    a.addEventListener("click", function () {
      if (!here || root.classList.contains("is-elsewhere")) return;
      setTimeout(function () {
        here.textContent = "You are here: your browser is asking if you trust the file. Follow the numbered circles.";
        var step = $("step-download");
        if (step) step.scrollIntoView({ behavior: reduced ? "auto" : "smooth", block: "start" });
      }, 600);
    });
  });

  var kind = osKind();
  if (kind && kind !== "windows") {
    showElsewhere(kind);
  } else if (kind === "windows") {
    if (/Windows NT (5|6)\./.test(ua)) $("old-win").hidden = false;
    var uad = navigator.userAgentData;
    var hints = uad && typeof uad.getHighEntropyValues === "function";
    /* Without Chrome's hints (Firefox), the old UA string still tells
     * 64-bit Windows apart: a 64-bit browser says Win64/x64, a 32-bit
     * one on 64-bit Windows says WOW64, and on 32-bit Windows it says
     * neither. Chrome and Edge always say "Win64; x64", so for them the
     * hints below decide. */
    if (!hints) {
      if (/ARM64|aarch64/i.test(ua)) showElsewhere("arm");
      else if (!/Win64|WOW64|x64|x86_64|amd64/i.test(ua)) showElsewhere("win32");
    }
    /* Chrome and Edge say what the processor is. "arm" means the
     * computer is ARM even when this browser runs as x64 under
     * emulation. */
    if (hints) {
      uad.getHighEntropyValues(["architecture", "bitness"]).then(function (v) {
        if (v && v.architecture === "arm") showElsewhere("arm");
        else if (v && v.bitness === "32") showElsewhere("win32");
      }, function () { /* unsure: keep the button */ });
    }
  }
})();
