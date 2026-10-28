#!/usr/bin/env node
// remoteaccess.js -- is a remote-control program stopped before it goes on?
//
// Checks rootfs/etc/polkit-1/rules.d/40-auros-remote-access.rules (run
// here with a stand-in for polkit) and rootfs/usr/lib/auros/remote-access-gate
// (run for real, with a stand-in for dbus-send). --prove breaks each
// protection in a copy and insists that a check goes red.
//
//   node tools/scamtest/remoteaccess.js [--prove]
//
// tools/scamtest/polkit.sh runs the same rule under a real polkitd.
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

const ROOT = path.resolve(__dirname, "..", "..");
const RULE = path.join(ROOT, "rootfs/etc/polkit-1/rules.d/40-auros-remote-access.rules");
const GATE = path.join(ROOT, "rootfs/usr/lib/auros/remote-access-gate");
const GRANT = "49-auros.rules";       // build/forge writes this one; ours must sort first

const NO = "no";
const FP = "org.freedesktop.Flatpak.";
const GDEBI = "com.ubuntu.pkexec.gdebi.gtk";
const PK = "org.freedesktop.packagekit.package-install";
// The other PackageKit actions whose policy says they IMPLY package-install:
// polkit asks them too when an install is asked, and build/forge grants
// package-remove (tools/scamtest/polkit.sh shows the real polkitd doing it).
const PKU = "org.freedesktop.packagekit.package-install-untrusted";
const PKD = "org.freedesktop.packagekit.package-downgrade";
const PKR = "org.freedesktop.packagekit.package-remove";
const label = (v) => (/^(app|runtime)\//.test(v) ? v.split("/")[1] : v.split("/").pop()).slice(0, 34);
const deb = (f) => "/usr/bin/pkexec gdebi-gtk --non-interactive /home/her/Downloads/" + f;

// What scammers actually ask for, as the store and their websites name it.
const REMOTE = [
  [FP + "app-install", "ref", "app/com.anydesk.Anydesk/x86_64/stable"],
  [FP + "app-install", "ref", "app/com.rustdesk.RustDesk/x86_64/stable"],
  [FP + "app-install", "ref", "app/com.teamviewer.TeamViewer/x86_64/stable"],
  [FP + "app-install", "ref", "app/com.hoptodesk.HopToDesk/x86_64/stable"],
  [FP + "app-install", "ref", "app/com.splashtop.Business/x86_64/stable"],
  [FP + "app-install", "ref", "app/com.nomachine.NoMachine/x86_64/stable"],
  [GDEBI, "command_line", deb("anydesk_6.3.2-1_amd64.deb")],
  [GDEBI, "command_line", deb("teamviewer_15.58.4_amd64.deb")],
  [GDEBI, "command_line", deb("rustdesk-1.3.1-x86_64.deb")],
  [GDEBI, "command_line", deb("chrome-remote-desktop_current_amd64.deb")],
  [GDEBI, "command_line", deb("ConnectWiseControl.ClientSetup.deb")],
  [GDEBI, "command_line", deb("dwagent.deb")],
  [GDEBI, "command_line", deb("LogMeIn-Resolve.deb")],
  [GDEBI, "command_line", deb("Zoho_Assist_Agent.deb")],
  [PK, "package_ids", "anydesk;6.3.2;amd64;anydesk-stable"],
  [PK, "package_ids", "teamviewer;15.58.4;amd64;local"],
  [PK, "package_ids", "vlc;3.0.20;amd64;noble&teamviewer-host;15.58.4;amd64;local"],
  [PKU, "package_ids", "teamviewer;15.58.4;amd64;local"],
  [PKD, "package_ids", "anydesk;6.3.2;amd64;anydesk-stable"],
  // What polkit asks as part of an install: a removal with ids that are not installed.
  [PKR, "package_ids", "teamviewer;15.58.4;amd64;local"],
  [PKR, "package_ids", "vlc;3.0.20;amd64;installed&anydesk;6.3.2;amd64;noble"],
];
// Everyday programs, and programs she uses to reach ANOTHER computer.
const PLAIN = [
  [FP + "app-install", "ref", "app/org.videolan.VLC/x86_64/stable"],
  [FP + "app-install", "ref", "app/org.mozilla.firefox/x86_64/stable"],
  [FP + "app-install", "ref", "app/org.libreoffice.LibreOffice/x86_64/stable"],
  [FP + "app-install", "ref", "app/us.zoom.Zoom/x86_64/stable"],
  [FP + "app-install", "ref", "app/com.skype.Client/x86_64/stable"],
  [FP + "app-install", "ref", "app/org.remmina.Remmina/x86_64/stable"],
  [FP + "app-install", "ref", "app/org.gnome.Connections/x86_64/stable"],
  [FP + "app-install", "ref", "app/org.kde.krdc/x86_64/stable"],
  [FP + "app-install", "ref", "app/com.github.tchx84.Flatseal/x86_64/stable"],
  [GDEBI, "command_line", deb("google-chrome-stable_current_amd64.deb")],
  [PK, "package_ids", "vlc;3.0.20;amd64;noble"],
  [PKU, "package_ids", "vlc;3.0.20;amd64;local"],
  [PKR, "package_ids", "vlc;3.0.20;amd64;installed"],
  [PK, "eula_id", "teamviewer"],
];
// She must always be able to take one off, or keep it up to date.
const NEVER = [
  [FP + "app-uninstall", "ref", "app/com.anydesk.Anydesk/x86_64/stable"],
  [FP + "app-update", "ref", "app/com.anydesk.Anydesk/x86_64/stable"],
  [FP + "runtime-install", "ref", "runtime/com.anydesk.Anydesk.Locale/x86_64/stable"],
  [GDEBI, "command_line", "/usr/bin/pkexec gdebi-gtk --non-interactive /home/her/Downloads/anydesk_6.3.2-1_amd64.deb --remove"],
  [PKR, "package_ids", "anydesk;6.3.2;amd64;installed"],
  [PKR, "package_ids", "teamviewer;15.58.4;amd64;installed:noble-updates"],
  [PKR, "package_ids", "teamviewer;15.58.4;amd64;installed&teamviewer-host;15.58.4;amd64;installed"],
  ["org.freedesktop.packagekit.package-reinstall", "package_ids", "anydesk;6.3.2;amd64;installed"],
  ["org.freedesktop.packagekit.system-update", "package_ids", "libc6;2.39-0ubuntu8;amd64;noble-updates"],
];

// ── the rule, under a stand-in polkit ───────────────────────────────
function load(text) {
  const rules = [];
  const spawned = [];
  const pk = {
    Result: { YES: "yes", NO: NO, AUTH_SELF: "auth_self", AUTH_ADMIN: "auth_admin" },
    consent: false,
    addRule(f) { rules.push(f); },
    log() {},
    spawn(argv) {
      spawned.push(argv);
      if (!pk.consent) throw new Error("exited with status 1");
      return "";
    },
  };
  new Function("polkit", text)(pk);
  return {
    pk, spawned,
    ask(id, key, val, consent) {
      pk.consent = !!consent;
      spawned.length = 0;
      const action = { id, lookup: (k) => (k === key ? val : undefined) };
      const subject = { user: "her" };
      for (const r of rules) {
        const v = r(action, subject);
        if (v !== undefined) return v;
      }
      return undefined;
    },
  };
}

function ruleChecks(text) {
  const out = [];
  const ok = (name, good) => out.push([name, !!good]);
  let p;
  try { p = load(text); } catch (e) { ok("the rule file loads", false); return out; }
  ok("the rule file loads", true);

  for (const [id, k, v] of REMOTE) {
    const r = p.ask(id, k, v, false);
    const argv = p.spawned[0] || [];
    ok("refused until she answers: " + label(v), r === NO &&
       argv[0] === "/usr/lib/auros/remote-access-gate" && /^[a-z0-9]+$/.test(argv[1] || "") &&
       argv[2] === "her");
    ok("allowed once she said yes: " + label(v), p.ask(id, k, v, true) === undefined);
  }
  for (const [id, k, v] of PLAIN) {
    const r = p.ask(id, k, v, false);
    ok("never in the way of " + label(v), r === undefined && p.spawned.length === 0);
  }
  for (const [id, k, v] of NEVER) {
    const r = p.ask(id, k, v, false);
    ok("never gates " + id.split(".").pop() + (/--remove/.test(v) ? " (gdebi --remove)" : ""),
       r === undefined && p.spawned.length === 0);
  }
  let grants = 0;
  for (const set of [REMOTE, PLAIN, NEVER])
    for (const [id, k, v] of set)
      for (const c of [false, true]) if (p.ask(id, k, v, c) === "yes") grants++;
  ok("it never grants anything", grants === 0);

  // polkit 124's JavaScript is duktape: ES5. Node would accept these.
  const code = text.replace(/\/\/.*$/gm, "").replace(/\/\*[\s\S]*?\*\//g, "");
  ok("ES5 only (no =>, let, const, class, template strings)",
     !/=>|\blet\s|\bconst\s|\bclass\s|`/.test(code));
  return out;
}

// ── the helper, for real ────────────────────────────────────────────
function gateChecks(script) {
  const out = [];
  const ok = (name, good) => out.push([name, !!good]);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "s2gate-"));
  const dir = path.join(tmp, "consent");
  fs.mkdirSync(dir);
  const sent = path.join(tmp, "sent");
  const fake = path.join(tmp, "dbus-send");
  fs.writeFileSync(fake, '#!/bin/sh\nprintf "%s\\n" "$*" >> "' + sent + '"\n', { mode: 0o755 });
  const gate = path.join(tmp, "gate");
  fs.writeFileSync(gate, script, { mode: 0o755 });
  const me = os.userInfo().username;

  const run = (name, user) => {
    try { fs.unlinkSync(sent); } catch (e) {}
    const r = spawnSync("/bin/sh", [gate, name, user],
      { env: { PATH: "/usr/bin:/bin", AUROS_REMOTE_DIR: dir, AUROS_DBUS_SEND: fake } });
    const s = fs.existsSync(sent) ? fs.readFileSync(sent, "utf8") : "";
    return { code: r.status, sent: s };
  };
  const consent = (name, agoMin) => {
    const f = path.join(dir, name);
    fs.writeFileSync(f, "");
    const t = Date.now() / 1000 - agoMin * 60;
    fs.utimesSync(f, t, t);
  };

  let r = run("anydesk", me);
  ok("gate: no answer yet -> refused", r.code === 1);
  ok("gate: ...and the desktop is told, with the program's word",
     /--system/.test(r.sent) && /--type=signal/.test(r.sent) &&
     /org\.auros\.Guard\.RemoteAccess/.test(r.sent) && /string:anydesk(\s|$)/.test(r.sent));
  consent("anydesk", 1);
  r = run("anydesk", me);
  ok("gate: she said yes a minute ago -> allowed", r.code === 0 && r.sent === "");
  r = run("anydesk", "nobody");
  ok("gate: a yes written by someone else does not count", r.code === 1);
  r = run("teamviewer", me);
  ok("gate: a yes to one program is not a yes to another", r.code === 1);
  consent("anydesk", 20);
  r = run("anydesk", me);
  ok("gate: a yes from 20 minutes ago has run out", r.code === 1);
  r = run("../anydesk", me);
  ok("gate: a strange name is refused and never sent anywhere", r.code === 1 && r.sent === "");
  r = run("anydesk", "-delete");
  ok("gate: a strange user name is refused", r.code === 1 && r.sent === "");
  fs.rmSync(tmp, { recursive: true, force: true });
  return out;
}

function ordering() {
  return [["sorts before " + GRANT + " (asked first)", path.basename(RULE) < GRANT],
          ["the gate is executable", (fs.statSync(GATE).mode & 0o111) !== 0]];
}

function report(rows) {
  let bad = 0;
  for (const [n, g] of rows) { console.log("    " + n.padEnd(64) + (g ? "ok" : "FAIL")); if (!g) bad++; }
  return bad;
}

const rule = fs.readFileSync(RULE, "utf8");
const gate = fs.readFileSync(GATE, "utf8");
console.log("  remote-control programs wait for her answer");
let bad = report(ruleChecks(rule)) + report(gateChecks(gate)) + report(ordering());

if (process.argv.includes("--prove")) {
  console.log("\n  each protection, broken on purpose, must turn a check red");
  const red = (rows) => rows.some(([, g]) => !g);
  const muts = [
    ["the rule is never registered", "r", (t) => t.replace("polkit.addRule(", "(function(){})(")],
    ["the rule says nothing instead of NO", "r", (t) => t.replace("return polkit.Result.NO;", "return undefined;")],
    ["AnyDesk is off the list", "r", (t) => t.replace("anydesk|", "")],
    ["names are matched as written", "r", (t) => t.replace('.replace(/[^a-z0-9]/g, "")', "")],
    ["package-install-untrusted, -downgrade and -remove are not asked about", "r",
     (t) => t.replace('action.id.indexOf(AUROS_PK) === 0', 'action.id === "org.freedesktop.packagekit.package-install"')],
    ["a removal of what is installed is refused", "r", (t) => t.replace("if (aurosAllInstalled(what)) { return undefined; }", "")],
    ["a mixed list counts as a removal", "r", (t) => t.replace("i < list.length", "i < 1")],
    ["an install counts as a removal", "r", (t) => t.replace('if (data !== "installed" && data.indexOf("installed:") !== 0) { return false; }', "")],
    ["'installed:<where>' is not a removal", "r", (t) => t.replace(' && data.indexOf("installed:") !== 0', "")],
    ["gdebi --remove is gated too", "r", (t) => t.replace("{ return undefined; }\n", "{ }\n")],
    ["every Flatpak action is gated", "r", (t) => t.replace('action.id === "org.freedesktop.Flatpak.app-install"', 'action.id.indexOf("Flatpak") >= 0')],
    ["a failed helper means yes", "r", (t) => t.replace("return polkit.Result.NO;", "return polkit.Result.YES;")],
    ["the gate ignores who wrote the yes", "g", (t) => t.replace(' -user "$user"', "")],
    ["the gate's yes never runs out", "g", (t) => t.replace(" \\\n               -mmin -15", "")],
    ["the gate never tells the desktop", "g", (t) => t.replace(/^"\$DBUS_SEND".*\n.*\n/m, "")],
    ["the gate trusts any name", "g", (t) => t.replace("case \"$name\" in ''|*[!a-z0-9]*) exit 1 ;; esac", "")],
  ];
  for (const [what, which, f] of muts) {
    const t = f(which === "r" ? rule : gate);
    const same = t === (which === "r" ? rule : gate);
    const rows = which === "r" ? ruleChecks(t) : gateChecks(t);
    const good = !same && red(rows);
    console.log("    " + ("broken: " + what).padEnd(64) + (good ? "red, ok" : same ? "MUTATION DID NOT APPLY" : "STILL GREEN"));
    if (!good) bad++;
  }
}
console.log(bad ? "  remoteaccess: FAIL (" + bad + ")" : "  remoteaccess: ok");
process.exit(bad ? 1 : 0);
