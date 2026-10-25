#!/usr/bin/env python3
"""e2e.py -- AurOS installed from real Windows, end to end, in a VM.

    e2e.py windows OUT VMDIR --iso WIN.iso --answer ANSWER.iso
        Windows Setup, unattended, to the desktop; then shut down. VMDIR
        then holds the installed machine (disk, firmware variables, TPM).
    e2e.py flow OUT GOLDEN WORK
        Start that machine (a copy-on-write layer over it in WORK), and do
        what a person does: open the website in Edge, download the
        installer, open it, answer Windows' warnings, go through the
        wizard, restart, watch AurOS install itself, say it works, then
        get a program from the store.
    e2e.py aur OUT IMAGE WORK
        AurOS alone: boot the release's raw image (no Windows) and do the
        AurOS half: the first screen, "Yes, it all works" if asked, the
        store, a program installed and opened.
    e2e.py inspect OUT WORK
        After the flow: read the disk with the machine off (did the
        program really land on AurOS's disk?).

Everything a person would press is pressed through QEMU's keyboard and
USB tablet, and every press is preceded by a screenshot (shots/NNNN-click-*
marks where it went). The settings that change between builds are the
environment variables below; tools/e2e/README.md has the rest.
"""
import argparse
import datetime
import hashlib
import math
import numpy as np
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vision                                                    # noqa: E402
from vm import (Failure, Hub, Machine, Recorder, OVMF_VARS,      # noqa: E402
                run)

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
E = os.environ.get

# ── what to install, and from where ──────────────────────────────────
# The default: the test build published on branch image-desktop of the
# repository this runs for (tools/repo.sh: AUROS_REPO, else
# GITHUB_REPOSITORY, else the origin remote). e2e.yml always sets it.
INSTALLER_URL = E("INSTALLER_URL") or (
    "https://github.com/"
    + subprocess.run(["sh", os.path.join(REPO, "tools", "repo.sh")], check=True,
                     stdout=subprocess.PIPE, text=True).stdout.strip()
    + "/raw/image-desktop/AurOS-Installer-test.exe")
INSTALLER_SHA256 = E("INSTALLER_SHA256", "")
# A file on the runner instead (an artifact from an earlier job): served
# from this host and the website's download links are pointed at it.
INSTALLER_PATH = E("INSTALLER_PATH", "")
# The file's name is whatever the link ends in (AurOS-Installer-test.exe,
# AurOS-Setup.exe...); Edge saves it under that name.
INSTALLER_NAME = os.path.basename(INSTALLER_PATH or INSTALLER_URL.split("?")[0])
INSTALLER_STEM = re.sub(r"\.exe$", "", INSTALLER_NAME, flags=re.I)
# The website a person starts from. Served from the runner; "none" gives
# a one-link page instead.
WEBSITE_DIR = E("WEBSITE_DIR") or os.path.join(REPO, "website")
WEBSITE_PAGE = E("WEBSITE_PAGE", "index.html")
# The download link's name, as UI Automation reads it from Edge: the
# website's "Get AurOS" button (an older page said "Download the ...").
DOWNLOAD_LINK = E("DOWNLOAD_LINK", r"(?i)^\s*get aurOS\b|download.*(installer|aurOS)")

# ── the store, after AurOS works (M3) ────────────────────────────────
CONTRAST_MIN = 4.5                                    # docs/EASY.md: text on a surface
STORE_NAME = E("STORE_NAME", "Get more programs")    # the app list's name for it
STORE_QUERY = E("STORE_QUERY", "VLC")                 # typed into its search
STORE_RESULT = E("STORE_RESULT", "VLC")               # the result to open
STORE_INSTALL = E("STORE_INSTALL", "Install")         # the button that installs
STORE_OPEN = E("STORE_OPEN", "Open|Launch|Press here to open")  # appears when installed
STORE_APP_CONTROLS = E("STORE_APP_CONTROLS", "Media|Playback|Audio|Video|Subtitle|Tools|View|Help")
# the accents the store's main button can be: AurOS's gold (legacy theme), its
# green (#0B5D4F light, #5DBB9E dark), the old teal
STORE_ACCENTS = ((200, 155, 74), (0x0B, 0x5D, 0x4F), (0x5D, 0xBB, 0x9E), (0x7D, 0xD3, 0xC0))
APP_FILES = E("APP_FILES", "usr/bin/vlc var/lib/flatpak/app/org.videolan.VLC")

# ── which PC (tools/e2e/README.md, "Variants") ──────────────────────
VARIANT = E("E2E_VARIANT", "base")
BITLOCKER_PUTBACK = E("E2E_BITLOCKER_PUTBACK", "0") == "1"
BITLOCKER_PAUSE_HEADING = "Pause Windows drive protection?"
BITLOCKER_PAUSE_BODY = (
    "Starting AurOS requires Windows drive protection to be paused. While it is paused, "
    "anyone with this computer could read the Windows drive. Start Windows again to let "
    "Windows turn protection back on."
)
BITLOCKER_PAUSE_ACCESSIBLE = (
    f"{BITLOCKER_PAUSE_HEADING} {BITLOCKER_PAUSE_BODY} Not yet is selected. "
    "Press Tab, then Enter, to pause protection and restart."
)
BITLOCKER_PAUSE_PRIMARY = "Pause protection and restart"
BITLOCKER_PAUSE_SECONDARY = "Not yet"
DISK_GB = int(E("E2E_DISK_GB", "128"))
DISK_THROTTLE = E("E2E_DISK_THROTTLE", "")     # e.g. bps=104857600,iops=120

# What the installer must say, and do, on each variant's PC (its stops
# and fixes: src/aurbridge/preflight.c, fix_win.c; selftest.py checks
# each page pattern still matches both titles of its stop). Without this a
# variant whose PC came out wrong (too much room left, encryption not
# on) would pass by testing the plain PC again.
#   page:     a regular expression some wizard page must show
#   fix:      "Fix these for me" was pressed
#   restarts: Windows restarted for the fix, before the install
VARIANT_EXPECT = {
    "smalldisk": {"page": r"enough free space|free space on drive", "fix": True, "restarts": 0},
    # D13: BitLocker PCs are in v0.1. Until the installer can pause BitLocker
    # (A2's fast path, AB_CHOICE_BITLOCKER_PAUSE) it stops at a plain page
    # with only Close, and this variant is red for that reason. Once it can:
    # the install must go on, and check_bitlocker() holds the proofs.
    # An installer that can pause BitLocker (the fast path) shows no stop at all: the
    # page is then optional, and the proofs are BitLocker fully encrypted when it
    # asks to restart, and (still to write) Windows coming back with it on.
    "bitlocker": {"page": r"is encrypted|encryption off|has locked", "fix": False, "restarts": 0, "optional": True,
                  "why_red": "D13 needs BitLocker paused for one restart, never decrypted: A2's fast path"},
    "pending":   {"page": r"waiting (to|for a) restart|finish an update", "fix": True, "restarts": 1},
    # not a stop: the Ready page says the drive is the slow kind (the installer
    # asks the drive, seek_penalty), and the time it gives is the hard disk's
    "hdd":       {"page": r"older, slower kind of drive", "fix": False, "restarts": 0},
}

HUB = "http://10.0.2.2:8000"
DEADLINE = time.time() + 60 * float(E("E2E_MINUTES", "300"))
# The installer's primary button by colour, when its names cannot be read:
# the old teal, then the 2026 restyle's green (docs/DESIGN-DIRECTION.md,
# light and dark).
PRIMARY_RGB = ((0x7D, 0xD3, 0xC0), (0x0B, 0x5D, 0x4F), (0x5D, 0xBB, 0x9E))
# Edge's words for a download it holds back until the person says keep
WARN_RX = (r"(isn.t commonly downloaded|can.t be downloaded securely|might be dangerous|"
           r"download blocked|was blocked|unsafe|couldn.t verify)")


def left():
    return DEADLINE - time.time()


def bitlocker_verdict(variant, vol, prot):
    """The PC must be the one the variant claims: BitLocker on (fully
    encrypted, protection on) for `bitlocker`, none at all (fully
    decrypted) for every other. Windows 11 turns device encryption on by
    itself on a TPM + Secure Boot PC, which once made the plain PC an
    encrypted one without anybody noticing (run 39923720924). Returns the
    reason it is not, or None."""
    if variant == "bitlocker":
        if vol != "FullyEncrypted" or prot != "On":
            return f"the bitlocker PC is {vol}, protection {prot}: BitLocker is not fully on"
    elif vol != "FullyDecrypted":
        return (f"the {variant} PC has BitLocker on its drive ({vol}, protection {prot}): "
                "Windows turned device encryption on by itself; the plain PC must have none")
    return None


def bitlocker_pause_lane_error(variant, putback, installer_name, installer_sha256):
    """Fail closed unless pause-driving is the exact pinned fastpath lane."""
    if not putback:
        return None
    if variant != "bitlocker":
        return "E2E_BITLOCKER_PUTBACK is valid only for the bitlocker variant"
    if installer_name != "AurOS-Setup-fastpath.exe":
        return "BitLocker put-back requires AurOS-Setup-fastpath.exe"
    if not re.fullmatch(r"[0-9a-f]{64}", installer_sha256 or ""):
        return "BitLocker put-back requires the fastpath installer's lowercase SHA-256"
    return None


def bitlocker_pause_controls(named):
    """Return the exact two actions of the in-window BitLocker warning.

    UI Automation names come from the installer's own MSAA tree.  No OCR
    approximation is allowed for the action that temporarily weakens drive
    protection.
    """
    panes = [e for e in named if e.get("type") == "Pane" and
             e.get("name") == BITLOCKER_PAUSE_ACCESSIBLE and e.get("rect") and
             e["rect"][2] > 0 and e["rect"][3] > 0]
    if len(panes) != 1:
        raise Failure("the exact BitLocker pause warning was not exposed once to accessibility")
    pane = panes[0]
    actions = [e for e in named if e.get("type") == "Button" and e.get("enabled", True) and
               e.get("rect") and e["rect"][2] > 0 and e["rect"][3] > 0 and inside(e, pane)]
    if len(actions) != 2 or {e.get("name") for e in actions} != {
            BITLOCKER_PAUSE_PRIMARY, BITLOCKER_PAUSE_SECONDARY}:
        raise Failure("the BitLocker pause warning did not expose exactly its two expected actions")

    def exact_button(name):
        got = [e for e in actions if e.get("name") == name]
        if len(got) != 1:
            raise Failure(f"the BitLocker pause warning did not expose exactly one enabled {name!r} button")
        return got[0]

    return exact_button(BITLOCKER_PAUSE_PRIMARY), exact_button(BITLOCKER_PAUSE_SECONDARY)


def bitlocker_restart_control_error(putback, installing, named_primary, raw_label, label):
    """The weakening warning may be entered only from exact Restart now."""
    if not putback or not installing:
        return None
    exact = (len(named_primary) == 1 and named_primary[0].get("name") == "Restart now" and
             raw_label == "Restart now" and label == "Restart now")
    return None if exact else (
        "the fastpath BitLocker lane requires the exact named Restart now control "
        "before showing its warning")


def parse_bitlocker_evidence(text):
    """Parse the deliberately small, secret-free BitLocker record emitted by
    the guest. Recovery passwords and raw protector output never cross the
    helper boundary or enter the evidence archive."""
    prefix = "AUROS_BITLOCKER_STATE "
    rows = [line[len(prefix):] for line in text.splitlines() if line.startswith(prefix)]
    if len(rows) != 1:
        raise Failure("Windows returned no single sanitized BitLocker state")
    try:
        state = json.loads(rows[0])
    except (TypeError, ValueError) as e:
        raise Failure(f"Windows returned malformed sanitized BitLocker state: {e}")
    if set(state) != {"volume_status", "protection_status", "encryption_percentage", "protectors"}:
        raise Failure("Windows returned an unexpected BitLocker state schema")
    if (isinstance(state["encryption_percentage"], bool) or
            not isinstance(state["encryption_percentage"], (int, float)) or
            not math.isfinite(float(state["encryption_percentage"])) or
            not 0 <= float(state["encryption_percentage"]) <= 100):
        raise Failure("Windows returned a non-numeric BitLocker percentage")
    protectors = state["protectors"]
    if not isinstance(protectors, list) or not protectors:
        raise Failure("Windows returned no BitLocker protector identities")
    clean = []
    for protector in protectors:
        if set(protector) != {"type", "id"}:
            raise Failure("Windows returned an unexpected protector schema")
        ptype, pid = protector["type"], protector["id"]
        if not isinstance(ptype, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9]{0,31}", ptype):
            raise Failure("Windows returned an invalid protector type")
        if not isinstance(pid, str) or not re.fullmatch(
                r"\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
                r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}", pid):
            raise Failure("Windows returned an invalid protector identifier")
        clean.append({"type": ptype, "id": pid.lower()})
    clean.sort(key=lambda p: (p["type"], p["id"]))
    state["protectors"] = clean
    state["encryption_percentage"] = float(state["encryption_percentage"])
    return state


def bitlocker_return_verdict(state, baseline_protectors=None):
    """A return to Windows is green only with encryption and protection on."""
    if state["volume_status"] != "FullyEncrypted":
        return f"volume is {state['volume_status']}, not FullyEncrypted"
    if state["protection_status"] != "On":
        return f"protection is {state['protection_status']}, not On"
    if state["encryption_percentage"] < 99.0:
        return f"encryption is only {state['encryption_percentage']}%"
    if baseline_protectors is not None and state["protectors"] != baseline_protectors:
        return "BitLocker protector types or identifiers changed from the pre-install baseline"
    return None


def putback_completion_errors(base, fixture, state, requested_at=None):
    """Return every fail-closed mismatch in Windows' final put-back report."""
    required = {"phase", "status_at", "c_bytes", "c_offset", "partition_sha256",
                "firmware_sha256", "hiberboot", "canary_sha256", "restored_sha256",
                "handover_volumes", "putback_tasks"}
    if not isinstance(state, dict) or set(state) != required:
        return ["completion report schema is missing or unexpected"]
    checks = (
        (state["phase"] == "finished", f"put-back phase is {state['phase']!r}, not 'finished'"),
        (state["c_bytes"] == base["c_bytes"], "Windows C: was not restored to its original size"),
        (state["c_offset"] == base["c_offset"], "Windows C: moved from its original partition offset"),
        (state["partition_sha256"] == base["partition_sha256"],
         "the original partition map was not restored exactly"),
        (state["firmware_sha256"] == base["firmware_sha256"],
         "the original firmware boot entries were not restored exactly"),
        (state["hiberboot"] == base["hiberboot"], "Fast Startup was not restored"),
        (state["canary_sha256"] == base["canary_sha256"], "the original Windows canary changed"),
        (state["restored_sha256"] == fixture["sha256"], "the AurOS file was not restored byte-for-byte"),
        (state["handover_volumes"] == 0, "the AUROSHANDOV volume remains"),
        (state["putback_tasks"] == 0, "a put-back scheduled task remains"),
    )
    errors = [message for ok, message in checks if not ok]
    if requested_at is not None:
        try:
            observed = datetime.datetime.fromisoformat(
                state["status_at"].replace("Z", "+00:00")).timestamp()
        except (AttributeError, TypeError, ValueError):
            observed = 0
        if observed < requested_at - 120:
            errors.append("put-back status is stale or has no valid completion timestamp")
    return errors


def label_is(label, *words):
    """OCR read a button's few letters ('| Instal |', 'Install.'): is it one
    of `words`, allowing a letter or two of noise? (Installed and Installing
    are other words and are not accepted.)"""
    import difflib
    got = re.sub(r"[^a-z]", "", (label or "").lower())
    return any(got and abs(len(got) - len(w)) <= 1 and
               difflib.SequenceMatcher(None, got, w.lower()).ratio() >= 0.8 for w in words)


# What the installer's primary button can say (src/aurbridge/wizard.c primary_label,
# and its stops): OCR reads these as "R¢start now", "J Install AugOs", "Checkpagain",
# and a regex on the raw reading missed "Restart now" (run 40038600863, so the
# BitLocker check before the restart never ran).
WIZARD_LABELS = ("Next", "Back", "Install AurOS", "Restart now", "Restart later", "Fix these for me",
                 "Check again", "Close AurOS Setup", "Try again", "Show a helper how",
                 "Copy for a helper", "Not now", "Checking", "Fixing", "Close")
WIZARD_ALIASES = {"gt": "Next", "gt next": "Next"}      # the arrow glyph on Next reads as 'gt'


def canon_label(raw):
    """The known button label a noisy OCR reading is closest to (or the reading)."""
    import difflib
    got = re.sub(r"[^a-z ]", "", (raw or "").lower()).strip()
    if got in WIZARD_ALIASES:
        return WIZARD_ALIASES[got]
    flat = got.replace(" ", "")
    best, score = None, 0.0
    for w in WIZARD_LABELS:
        r = difflib.SequenceMatcher(None, flat, w.lower().replace(" ", "")).ratio()
        if r > score:
            best, score = w, r
    return best if best and score >= 0.62 else raw


def close_verdict(variant, page):
    """Why the installer ended on Close. On the stop this variant's PC was
    made for, no way on is the right red (D13: BitLocker PCs must get
    through; until the installer can pause BitLocker it stops there,
    plainly): the message says so, and what is missing."""
    want = VARIANT_EXPECT.get(variant)
    if want and re.search(want["page"], page, re.I):
        return (f"variant {variant}: STOPPED AT ITS STOP, no way on: the installer offers only Close, "
                f"not a fix ({want.get('why_red', 'its fix is missing')}). The page says: {page[:700]}")
    return "the installer stopped without offering a restart: " + page[:900]


def inside(e, box, slack=4):
    """UI element e lies within element box (both [x, y, w, h])."""
    x, y, w, h = e.get("rect") or (0, 0, 0, 0)
    bx, by, bw, bh = box["rect"]
    return (w > 0 and x >= bx - slack and y >= by - slack
            and x + w <= bx + bw + slack and y + h <= by + bh + slack)


# ════════════════════════════════════════════════════════════════════
#  M1: Windows
# ════════════════════════════════════════════════════════════════════

def pass_cd_prompt(m, timeout=180):
    """Windows' DVD says "Press any key to boot from CD or DVD" for about
    five seconds, then gives up and the firmware moves on. Press a key
    exactly then and never after: Setup starts seconds later with its
    Cancel button focused, and a stray Enter there asks whether to quit."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        img = m.screendump()
        lit = (img.max(axis=2) > 60).mean()
        if lit > 0.3:           # a full screen of colour: past the prompt
            return False
        if lit > 0.0005 and vision.find_text(vision.ocr(img), "Press any key"):
            m.save(img, "cd-prompt")
            m.key("ret", label="boot from the DVD")
            time.sleep(1.5)
            if not vision.find_text(vision.ocr(m.screendump()), "Press any key"):
                return True
            continue
        time.sleep(0.3)
    return False


def cmd_windows(a):
    rec = Recorder(a.out)
    hub = Hub(rec)
    os.makedirs(a.vm, exist_ok=True)
    disk, vars_fd, tpm = (os.path.join(a.vm, n) for n in ("win.qcow2", "vars.fd", "tpm"))
    run(["qemu-img", "create", "-f", "qcow2", disk, f"{DISK_GB}G"], rec)
    shutil.copyfile(OVMF_VARS, vars_fd)
    m = Machine(rec, a.vm, disk, vars_fd, tpm, cdroms=[a.iso, a.answer])
    ok = False
    try:
        with rec.phase("windows: firmware to Setup"):
            m.start()
            m.auto_shots(2)
            if not pass_cd_prompt(m):
                raise Failure("the DVD's 'Press any key' prompt never showed")
            m.shot("setup-loading")
        m.auto_shots(30)
        with rec.phase("windows: Setup, to the first logon"):
            facts = hub.wait_event("firstlogon", timeout=min(left(), 120 * 60), alive=m.alive)
            if facts is None:
                raise Failure("Windows did not reach its first logon")
        with open(os.path.join(a.out, "windows-facts.json"), "w") as f:
            f.write(facts)
        golden = E("E2E_GOLDEN", "base")
        bl = re.search(r"VolumeStatus\s*:\s*(\w+)\s+ProtectionStatus\s*:\s*(\w+)",
                       str(json.loads(facts).get("bitlocker")))
        if golden != "bitlocker":     # (that one is made encrypted after these facts; the flow checks it)
            bad = bitlocker_verdict("base", bl.group(1), bl.group(2)) if bl else \
                "BitLocker's state is not in the first-logon facts"
            if bad:
                raise Failure(bad)
        for k, v in json.loads(facts).items():
            rec.log(f"windows {k}: {str(v).strip()}")
        with rec.phase("windows: settle on the desktop"):
            hub.wait_event("helper-up", 600, alive=m.alive)
            time.sleep(90)
            m.shot("windows-desktop")
            w = hub.ask("windows")
            rec.log("windows on screen: " + json.dumps(
                [(x["name"], x["proc"]) for x in (w or {}).get("windows", [])]))
        with rec.phase("windows: shut down (power button)"):
            m.stop("powerdown", 900)
        ok = True
    except Failure as e:
        rec.log(f"FAILED: {e}")
        if m.alive():
            m.shot("failure")
    finally:
        m.stop("quit", 5)
        rec.write_timings()
    return 0 if ok else 1


# ════════════════════════════════════════════════════════════════════
#  M2: from the website to "does this work?"
# ════════════════════════════════════════════════════════════════════

class Flow:
    def __init__(self, a):
        self.a = a
        self.rec = Recorder(a.out)
        self.www = self.prepare_website()
        self.hub = Hub(self.rec, self.www)
        os.makedirs(a.work, exist_ok=True)
        self.disk = os.path.join(a.work, "win.qcow2")
        self.vars = os.path.join(a.work, "vars.fd")
        self.tpm = os.path.join(a.work, "tpm")
        if not os.path.exists(self.disk):
            run(["qemu-img", "create", "-f", "qcow2", "-F", "qcow2", "-b",
                 os.path.join(a.golden, "win.qcow2"), self.disk], self.rec)
            shutil.copyfile(os.path.join(a.golden, "vars.fd"), self.vars)
            shutil.copytree(os.path.join(a.golden, "tpm"), self.tpm)
        self.m = Machine(self.rec, a.work, self.disk, self.vars, self.tpm,
                         throttle=DISK_THROTTLE)
        self.rec.log(f"variant {VARIANT}: disk {DISK_GB} GB, throttle '{DISK_THROTTLE or 'none'}'")
        self.findings = []          # things a person would be hurt by that do not stop the journey
        self.wizard_pages = []      # the text of each page a button was pressed on
        self.fix_presses = 0
        self.fix_restarts = 0
        self._named_diag = False
        self.putback_base = None
        self.putback_fixture = None
        self.putback_requested_at = None

    def check_variant(self):
        """After the wizard: the variant's PC got the stop it was made
        for, and the installer's own fix cleared it."""
        want = VARIANT_EXPECT.get(VARIANT)
        if not want:
            return
        seen = [t for t in self.wizard_pages if re.search(want["page"], t, re.I)]
        self.log(f"variant {VARIANT}: pages matching /{want['page']}/: {len(seen)}, "
                 f"'Fix these for me' pressed {self.fix_presses}, restarts for a fix {self.fix_restarts}")
        if not seen and want.get("optional"):
            self.log(f"variant {VARIANT}: the installer went on with no stop page: it deals with this PC itself")
            return
        if not seen:
            raise Failure(f"variant {VARIANT}: no wizard page said /{want['page']}/, so this PC was "
                          "not the one the variant claims (or the installer did not notice); pages: " +
                          " || ".join(t[:200] for t in self.wizard_pages))
        if want["fix"] and not self.fix_presses:
            raise Failure(f"variant {VARIANT}: the installer went on without its fix being pressed")
        if self.fix_restarts < want["restarts"]:
            raise Failure(f"variant {VARIANT}: the fix should have restarted Windows, and it did not")

    def bitlocker_state(self, when):
        """What Windows itself says about BitLocker on C: (read with the
        elevated helper). Returns (volume status, protection, percent)."""
        try:
            state = self.bitlocker_evidence()
        except Failure as e:
            self.log(f"bitlocker {when}: unreadable ({e})")
            # (the helper is elevated, and can read it; if it cannot, only the
            # bitlocker variant depends on knowing, the staging screen refuses
            # an encrypted drive anyway)
            if VARIANT == "bitlocker":
                raise Failure(f"variant bitlocker: cannot read BitLocker's state {when}: {e}")
            return "unknown", "unknown", 0.0
        self.log(f"bitlocker {when}: {state['volume_status']}|{state['protection_status']}|"
                 f"{state['encryption_percentage']}; protectors=" +
                 ",".join(p["type"] for p in state["protectors"]))
        return state["volume_status"], state["protection_status"], state["encryption_percentage"]

    def bitlocker_evidence(self):
        """Return only the non-secret BitLocker fields needed by this test.
        Never ask Windows for, print, or retain recovery password material."""
        out = self.guest(
            "$v=Get-BitLockerVolume -MountPoint C:; "
            "$p=@($v.KeyProtector | ForEach-Object { "
            "@{type=[string]$_.KeyProtectorType;id=[string]$_.KeyProtectorId} }); "
            "$s=@{volume_status=[string]$v.VolumeStatus;"
            "protection_status=[string]$v.ProtectionStatus;"
            "encryption_percentage=[double]$v.EncryptionPercentage;protectors=$p}; "
            "'AUROS_BITLOCKER_STATE '+($s|ConvertTo-Json -Compress -Depth 4)")
        return parse_bitlocker_evidence(out)

    def turn_on_bitlocker(self):
        """BitLocker as a PC with a TPM has it: on C:, a TPM protector and a
        recovery password, protection on, used space encrypted, waited until
        done. Here, not at Windows' first logon: BitLocker refuses to start
        while a bootable CD is attached ("detected bootable media", run
        40024967637), and the install ISO is attached until Setup is over."""
        with self.rec.phase("windows: BitLocker on, as the PC has it"):
            out = self.guest(
                "$v = Get-BitLockerVolume -MountPoint C:; "
                "if ($v.VolumeStatus -eq 'FullyDecrypted') { "
                "  Enable-BitLocker -MountPoint C: -EncryptionMethod XtsAes128 -UsedSpaceOnly -TpmProtector "
                "    -SkipHardwareTest -ErrorAction Stop | Out-Null }; "
                "if (-not ((Get-BitLockerVolume -MountPoint C:).KeyProtector | "
                "    Where-Object KeyProtectorType -eq 'RecoveryPassword')) { "
                "  Add-BitLockerKeyProtector -MountPoint C: -RecoveryPasswordProtector | Out-Null }; "
                "'started'")
            self.log("BitLocker: " + out.strip()[:600])
            deadline = time.time() + 25 * 60
            while time.time() < deadline:
                vol, prot, pct = self.bitlocker_state("while it encrypts")
                if vol == "FullyEncrypted":
                    break
                time.sleep(20)
            if vol != "FullyEncrypted":
                raise Failure(f"BitLocker did not finish encrypting C: in 25 minutes ({vol}, {pct}%): " + out.strip()[:300])
            if prot != "On":
                self.guest("Resume-BitLocker -MountPoint C: | Out-Null")

    def check_plain_before_restart(self):
        """The plain PC must still have no BitLocker when the installer is done
        (not only when the run began): Windows 11 can start device encryption
        by itself at any time, and the 'base' journey must test the plain path."""
        vol, prot, pct = self.bitlocker_state("before the restart")
        bad = bitlocker_verdict(VARIANT, vol, prot) if vol != "unknown" else None
        if bad:
            raise Failure("at the installer's Restart now: " + bad)

    def check_bitlocker_before_restart(self):
        """D11/D13: the installer PAUSES BitLocker, it never decrypts. At the
        page before the restart the drive must still be fully encrypted
        (suspended, protection off, or still on), never decrypting."""
        vol, prot, pct = self.bitlocker_state("before the restart")
        if vol != "FullyEncrypted" or pct < 99:
            raise Failure(f"variant bitlocker: the drive is {vol} ({pct}%) when the installer asks to "
                          "restart: it decrypted instead of pausing BitLocker (D11)")

    # -- the website a person starts from --
    def prepare_website(self):
        www = os.path.join(self.a.out, "www")
        shutil.rmtree(www, ignore_errors=True)
        if WEBSITE_DIR != "none" and os.path.isdir(WEBSITE_DIR):
            shutil.copytree(WEBSITE_DIR, www)
        else:
            os.makedirs(www)
            with open(os.path.join(www, WEBSITE_PAGE), "w") as f:
                f.write(f'<!doctype html><title>AurOS</title><h1>AurOS</h1>'
                        f'<p><a href="{INSTALLER_URL}">Download the AurOS installer</a></p>')
        target = INSTALLER_URL
        if INSTALLER_PATH:
            shutil.copyfile(INSTALLER_PATH, os.path.join(www, INSTALLER_NAME))
            target = f"{HUB}/{INSTALLER_NAME}"
        # Whatever the pages link to as the installer (any AurOS .exe),
        # link to this one.
        pat = re.compile(r'href="[^"]*/AurOS[^"/]*\.exe"')
        n = 0
        for dp, _, fs in os.walk(www):
            for fn in fs:
                if fn.endswith(".html"):
                    p = os.path.join(dp, fn)
                    s = open(p, encoding="utf-8").read()
                    s2, k = pat.subn(f'href="{target}"', s)
                    if k:
                        open(p, "w", encoding="utf-8").write(s2)
                        n += k
        self_log = os.path.join(self.a.out, "website.txt")
        with open(self_log, "w") as f:
            f.write(f"website from {WEBSITE_DIR}; {n} installer link(s) -> {target}\n")
        return www

    # -- small helpers --
    def log(self, msg):
        self.rec.log(msg)

    def elements(self, match, types, max=400, quiet=False):
        r = self.hub.ask("elements", match=match, types=types, max=max, timeout=120)
        if not r or not r.get("ok"):
            self.log(f"helper: no elements ({(r or {}).get('error', 'no answer')})")
            return []
        els = r.get("elements") or []
        if isinstance(els, dict):
            els = [els]
        if not quiet:
            with open(os.path.join(self.a.out, "ui-elements.log"), "a") as f:
                f.write(f"--- {time.strftime('%H:%M:%S')} {match} {types} ({r.get('ms')} ms)\n")
                for e in els:
                    f.write(f"{e.get('type')}\t{e.get('name')!r}\t{e.get('rect')}\t"
                            f"toggle={e.get('toggle')}\t{'page' if self.is_page(e) else 'ui'}\t"
                            f"{e.get('window')!r}\n")
        return els

    def windows(self):
        r = self.hub.ask("windows", timeout=60)
        ws = (r or {}).get("windows") or []
        return [ws] if isinstance(ws, dict) else ws

    def guest(self, script, timeout=120):
        r = self.hub.ask("run", script=script, timeout=timeout)
        out = (r or {}).get("output") or (r or {}).get("error") or "(no answer)"
        with open(os.path.join(self.a.out, "guest-diagnostics.log"), "a") as f:
            f.write(f"--- {time.strftime('%H:%M:%S')} {script}\n{out}\n")
        return out

    def click_el(self, e, why):
        x, y, w, h = e["rect"]
        self.m.click(x + w // 2, y + h // 2, label=why)

    @staticmethod
    def is_page(e):
        """Part of a website: inside a web document that fills most of a
        window. Edge's downloads flyout and menus are small documents."""
        return (e.get("doc") or 0) >= 250000

    @staticmethod
    def pick(els, name, types=None, page=None):
        """The first enabled element whose name matches; page=True only
        a web page's own, page=False only the browser's or Windows'."""
        rx = re.compile(name, re.I)
        for e in els:
            if page is not None and Flow.is_page(e) != page:
                continue
            if (types is None or e.get("type") in types) and rx.search(e.get("name") or ""):
                if e.get("enabled", True):
                    return e
        return None

    def ocr_find(self, phrase, img=None, region=None, last=False):
        img = self.m.screendump() if img is None else img
        words = vision.ocr(img, region)
        return vision.find_text(words, phrase, last=last), words

    def wait_text(self, phrases, timeout, every=10, fail=()):
        """OCR the screen until one of `phrases` shows; returns (phrase,
        box, words). A phrase in `fail` showing is a Failure."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not self.m.alive():
                raise Failure("the machine switched off")
            img = self.m.screendump()
            words = vision.ocr(img)
            for p in fail:
                if vision.find_text(words, p):
                    self.m.save(img, "failure-" + p)
                    raise Failure(f"the screen says '{p}': {vision.text_of(words)[:600]}")
            for p in phrases:
                b = vision.find_text(words, p)
                if b:
                    return p, b, words
            time.sleep(every)
        return None, None, None

    def wait_store_result(self, query, phrase, timeout, every=10):
        """Wait for a named catalogue row, excluding the query field."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not self.m.alive():
                raise Failure("the machine switched off")
            img = self.m.screendump()
            words = vision.ocr(img)
            found = self.store_result(words, img.shape[0], query, phrase)
            if found:
                return found, words
            time.sleep(every)
        return None, None

    # -- 1. Windows, as the person finds it --
    def boot_windows(self):
        with self.rec.phase("windows: power on to the desktop"):
            t = time.time()
            self.m.start()
            self.m.auto_shots(10)
            if self.hub.wait_event("helper-up", 25 * 60, since=t, alive=self.m.alive) is None:
                raise Failure("Windows did not reach the desktop (the helper never started)")
            if VARIANT == "pending":
                # the feature that leaves a restart pending is enabled at logon
                r = self.hub.wait_event("pending-ready", 15 * 60, since=t, alive=self.m.alive)
                self.log(f"pending restart prepared: {r}")
            time.sleep(45)
            self.m.shot("windows-desktop")
            # What Windows says about itself, read as the signed-in user
            # (no elevation): the state the installer is about to meet.
            facts = self.guest(
                "$o = Get-CimInstance Win32_OperatingSystem; \"$($o.Caption) $($o.Version)\"; "
                "'DisplayVersion: ' + (Get-ItemProperty 'HKLM:\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion').DisplayVersion; "
                "'SecureBoot (UEFISecureBootEnabled): ' + (Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State').UEFISecureBootEnabled; "
                "'Fast Startup (HiberbootEnabled): ' + (Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power').HiberbootEnabled; "
                "'BitLocker on C: (shell, 1=on 2=off 3=encrypting 6=on/suspended): ' + "
                "(New-Object -ComObject Shell.Application).NameSpace('C:').Self.ExtendedProperty('System.Volume.BitLockerProtection'); "
                "'SmartScreen: ' + (Get-ItemProperty 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer' -ErrorAction SilentlyContinue).SmartScreenEnabled; "
                "'Smart App Control (0 off, 1 on, 2 evaluation): ' + (Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\CI\\Policy' -ErrorAction SilentlyContinue).VerifiedAndReputablePolicyState; "
                "Get-Volume -DriveLetter C | Format-List FileSystem,Size,SizeRemaining; "
                "Get-CimInstance Win32_DiskPartition | Format-Table Index,Type,Size,StartingOffset -AutoSize; "
                "Get-CimInstance Win32_VideoController | Format-List Name,CurrentHorizontalResolution,CurrentVerticalResolution")
            self.log("windows, as the installer will find it:\n" + facts.strip())
            if VARIANT == "bitlocker":
                self.turn_on_bitlocker()
            vol, prot, pct = self.bitlocker_state("at the start")
            bad = bitlocker_verdict(VARIANT, vol, prot) if vol != "unknown" else None
            if bad:
                raise Failure(bad)
            with open(os.path.join(self.a.out, "windows-facts.txt"), "w") as fh:
                fh.write(facts)
            if BITLOCKER_PUTBACK:
                self.prepare_putback_windows()
        self.m.auto_shots(30)

    def prepare_putback_windows(self):
        """Place a harmless canary in the disposable Windows guest and retain
        the exact pre-install state that put-back must restore."""
        if VARIANT != "bitlocker":
            raise Failure("BitLocker put-back was requested outside the bitlocker variant")
        token = "auros-e2e-windows-" + os.urandom(16).hex()
        out = self.guest(
            "$ErrorActionPreference='Stop'; "
            "function H([string]$x){$a=[Security.Cryptography.SHA256]::Create();try{"
            "([BitConverter]::ToString($a.ComputeHash([Text.Encoding]::UTF8.GetBytes($x)))-replace'-','').ToLowerInvariant()"
            "}finally{$a.Dispose()}}; "
            "$p=Join-Path $env:USERPROFILE 'Documents\\auros-e2e-before.txt'; "
            f"[IO.File]::WriteAllText($p,'{token}',[Text.Encoding]::ASCII); "
            "$cpart=Get-Partition -DriveLetter C; "
            "$pm=((Get-Partition|Sort-Object DiskNumber,Offset|ForEach-Object{"
            "'{0}|{1}|{2}|{3}|{4}'-f $_.DiskNumber,$_.PartitionNumber,$_.Offset,$_.Size,$_.GptType})-join';'); "
            "$fwRaw=& bcdedit.exe /enum firmware 2>&1; if($LASTEXITCODE -ne 0){throw 'bcdedit failed'}; "
            "$fw=(($fwRaw|Out-String).Trim()); "
            "$h=(Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power').HiberbootEnabled; "
            "$s=@{c_bytes=[uint64]$cpart.Size;c_offset=[uint64]$cpart.Offset;partition_sha256=(H $pm);"
            "firmware_sha256=(H $fw);hiberboot=[int]$h;"
            "canary_sha256=(Get-FileHash $p -Algorithm SHA256).Hash.ToLowerInvariant()}; "
            "'AUROS_PUTBACK_BASE '+($s|ConvertTo-Json -Compress)")
        rows = [x[len("AUROS_PUTBACK_BASE "):] for x in out.splitlines()
                if x.startswith("AUROS_PUTBACK_BASE ")]
        try:
            base = json.loads(rows[0]) if len(rows) == 1 else None
        except ValueError:
            base = None
        if (not isinstance(base, dict) or set(base) != {"c_bytes", "c_offset", "partition_sha256",
                                                        "firmware_sha256", "hiberboot", "canary_sha256"}
                or not isinstance(base["c_bytes"], int) or base["c_bytes"] <= 0
                or not isinstance(base["c_offset"], int) or base["c_offset"] <= 0
                or base["hiberboot"] not in (0, 1)
                or any(not re.fullmatch(r"[0-9a-f]{64}", str(base[k]))
                       for k in ("partition_sha256", "firmware_sha256", "canary_sha256"))):
            raise Failure("Windows did not return the exact pre-install put-back state")
        protectors = self.bitlocker_evidence()["protectors"]
        if not {"Tpm", "RecoveryPassword"}.issubset({p["type"] for p in protectors}):
            raise Failure("the baseline BitLocker volume lacks its TPM or recovery protector")
        base["protectors"] = protectors
        self.putback_base = base
        with open(os.path.join(self.a.out, "putback-before.json"), "w") as fh:
            json.dump(base, fh, indent=2, sort_keys=True)
            fh.write("\n")

    # -- 2. Edge: the website, its download link, Edge's own warnings --
    def open_website(self):
        url = f"{HUB}/{WEBSITE_PAGE}"
        with self.rec.phase("edge: open the website"):
            self.m.key("meta_l", "r", label="Run dialog")
            time.sleep(3)
            self.m.type(f"msedge {url}", label="open Edge on the website")
            self.m.key("ret")
            deadline = time.time() + 180
            while time.time() < deadline:
                ws = [w for w in self.windows() if w.get("proc") == "msedge" and w.get("name")]
                if any("AurOS" in (w.get("name") or "") for w in ws):
                    break
                time.sleep(5)
            else:
                raise Failure("Edge did not show the website: windows " +
                              json.dumps([(w.get('name'), w.get('proc')) for w in self.windows()]))
            time.sleep(8)
            self.m.key("meta_l", "up", label="maximise Edge")
            time.sleep(3)
            self.m.shot("website")

    def click_download(self):
        with self.rec.phase("edge: press the website's download button"):
            # The button as a person finds it: on screen, or further down
            # (End, then the wheel). Any boxes that gate it get ticked.
            for attempt in range(8):
                els = [e for e in self.elements("msedge", ["CheckBox", "Hyperlink", "Button"])
                       if self.is_page(e)]
                boxes = [e for e in els if e.get("type") == "CheckBox" and e.get("toggle") == "Off"]
                if boxes:
                    self.click_el(boxes[0], "website tick: " + (boxes[0].get("name") or "")[:40])
                    time.sleep(1.5)
                    continue
                link = self.pick(els, DOWNLOAD_LINK, ("Hyperlink", "Button"))
                if link:
                    self.click_el(link, "website: " + (link.get("name") or "download")[:40])
                    time.sleep(5)
                    return
                if attempt == 0:
                    self.m.key("end", label="look further down the page")
                else:
                    self.m.scroll(640, 400, 5)
                time.sleep(3)
            raise Failure("no download link on the page (ui-elements.log has what Edge showed)")

    def keep_download(self):
        """Edge's download warnings, answered the way a person who wants
        the file does; returns once the file is downloaded."""
        with self.rec.phase("edge: download, and keep it"):
            deadline = time.time() + 8 * 60
            pressed = []
            seen = None
            while time.time() < deadline:
                els = [e for e in self.elements("msedge", ["Button", "MenuItem", "Hyperlink",
                                                           "ListItem", "Text", "SplitButton"])
                       if not self.is_page(e)]  # never the page's own words
                names = " | ".join((e.get("name") or "")[:120] for e in els)
                if names != seen:
                    self.log("edge shows: " + names[:900])
                    seen = names
                # Edge 2026: "Keep" opens "Make sure you trust ...", whose
                # Keep anyway sits in the menu of its Delete button
                # ("Open menu", the arrow); older Edge had "Show more".
                for want, types in (("^Keep anyway$", ("Button", "MenuItem")),
                                    ("^Show more", ("Button", "SplitButton")),
                                    ("^Open menu$", ("Button", "SplitButton", "MenuItem")),
                                    ("^Keep$", ("Button", "MenuItem"))):
                    if want == "^Open menu$" and not re.search(r"Make sure you trust", names):
                        continue
                    e = self.pick(els, want, types)
                    if e and pressed.count(want) < 4:
                        self.click_el(e, "edge " + want.strip("^$"))
                        pressed.append(want)
                        time.sleep(3)
                        break
                else:
                    # the newest finished .exe in Downloads (Edge may add " (1)")
                    done = self.guest("$f = Get-ChildItem \"$env:USERPROFILE\\Downloads\" -Filter *.exe | "
                                      "Sort-Object LastWriteTime -Descending | Select-Object -First 1; "
                                      "if ($f) { (Get-FileHash $f.FullName).Hash; $f.Name } else { 'missing' }; "
                                      "Get-ChildItem \"$env:USERPROFILE\\Downloads\" | Format-Table Name,Length")
                    h = done.split()[0].lower() if done.strip() else ""
                    if re.fullmatch(r"[0-9a-f]{64}", h):
                        self.log(f"downloaded {done.split()[1] if len(done.split()) > 1 else '?'}: SHA-256 {h}")
                        if INSTALLER_SHA256 and h != INSTALLER_SHA256.lower():
                            raise Failure(f"the downloaded installer is {h}, expected {INSTALLER_SHA256}")
                        self.m.shot("downloaded")
                        return
                    warn = re.search(WARN_RX, names, re.I)
                    # The flyout's header has its own "More options" (open the
                    # downloads page, clear history...): no Keep in it. Close it.
                    if self.pick(els, r"^(Open downloads page|Clear all download history)$", ("MenuItem",)):
                        self.m.key("esc", label="close the flyout's own menu")
                        time.sleep(2)
                        continue
                    item = None
                    for t in ("ListItem", "Text") if warn else ():
                        item = item or next((e for e in els if e.get("type") == t and
                                             re.search(WARN_RX, e.get("name") or "", re.I)), None)
                    # The warned item's own buttons, which Edge shows while the
                    # pointer is on it: "More actions" holds Keep.
                    more = item and self.pick([e for e in els if e is not item and inside(e, item)],
                                              r"^(More actions|More options|See more)",
                                              ("Button", "SplitButton", "MenuItem"))
                    if more and pressed.count("more") < 4:
                        self.click_el(more, "edge download item: more actions")
                        pressed.append("more")
                        time.sleep(3)
                        continue
                    if item and pressed.count("hover") < 4:
                        x, y, w, h = item["rect"]
                        self.m.move(x + w // 2, y + h // 2)
                        pressed.append("hover")
                        time.sleep(2)
                        continue
                    if item and pressed.count("right") < 2:
                        # Edge's context menu on a download item is the same menu
                        x, y, w, h = item["rect"]
                        self.m.click(x + w // 2, y + h // 2, button="right",
                                     label="edge download item: context menu")
                        pressed.append("right")
                        time.sleep(3)
                        continue
                    if not warn and pressed.count("flyout") < 3:
                        # the flyout went away: the toolbar's Downloads button
                        tb = self.pick(els, r"^Downloads", ("Button", "SplitButton"))
                        if tb:
                            self.click_el(tb, "edge Downloads button")
                            pressed.append("flyout")
                            time.sleep(3)
                            continue
                    time.sleep(5)
            raise Failure("the download never finished (ui-elements.log has Edge's buttons)")

    # -- 3. open it: SmartScreen, then UAC --
    def open_installer(self):
        with self.rec.phase("windows: open the installer (SmartScreen, UAC)"):
            els = self.elements("msedge", ["Button", "Hyperlink", "ListItem", "MenuItem"])
            op = (self.pick(els, r"^Open file$", page=False) or
                  self.pick(els, re.escape(INSTALLER_STEM), page=False))
            if op:
                self.click_el(op, "edge open file")
            else:
                # The flyout closed: open Downloads (Ctrl+J) and use it there.
                self.m.key("ctrl", "j", label="Edge downloads")
                time.sleep(4)
                els = self.elements("msedge", ["Button", "Hyperlink", "ListItem"])
                # edge://downloads is Edge's own page, so a page is fine here
                op = (self.pick(els, r"^Open file$") or
                      self.pick(els, re.escape(INSTALLER_STEM), ("Hyperlink", "Button")))
                if not op:
                    raise Failure("Edge offers no way to open the download")
                self.click_el(op, "edge open file")
            deadline = time.time() + 6 * 60
            uac_tries = 0
            while time.time() < deadline:
                time.sleep(4)
                ws = self.windows()
                if self.wizard_rect(ws):
                    self.log("the installer's window is open")
                    return
                ss = [w for w in ws if re.search(r"smartscreen", (w.get("proc") or "") + (w.get("name") or ""), re.I)
                      or "Windows protected your PC" in (w.get("name") or "")]
                if ss:
                    els = self.elements("smartscreen|Windows protected|SmartScreen",
                                        ["Button", "Hyperlink", "Text"])
                    e = self.pick(els, r"^Run anyway$") or self.pick(els, r"^More info$")
                    if e:
                        self.click_el(e, "smartscreen " + e["name"])
                        continue
                img = self.m.screendump()
                words = vision.ocr(img)
                # "Do you want to allow this app to make changes to your
                # device?", or, for an unsigned file (run 39906470700, shot
                # 0018): "...this app from an unknown publisher to make changes..."
                if (vision.find_text(words, "to make changes to your") or
                        vision.find_text(words, "User Account Control")):
                    # The secure desktop: UI Automation cannot see it, and
                    # neither can anything else on the normal desktop. The
                    # keyboard reaches it: Alt+Y is "Yes".
                    uac_tries += 1
                    self.m.save(img, "uac")
                    if uac_tries <= 2:
                        self.m.key("alt", "y", label="UAC yes (Alt+Y)")
                    else:
                        yes = vision.find_text(words, "Yes", last=True)
                        if yes:
                            self.m.click(*vision.centre(yes), label="UAC Yes")
                        else:
                            self.m.key("left")
                            self.m.key("ret", label="UAC yes (Left, Enter)")
                    continue
                if vision.find_text(words, "Windows protected your PC"):
                    b = vision.find_text(words, "Run anyway") or vision.find_text(words, "More info")
                    if b:
                        self.m.click(*vision.centre(b), label="smartscreen (by OCR)")
                        continue
            raise Failure("the installer's window never opened")

    # -- 4. the wizard --
    def wizard_win(self, ws=None):
        """The installer's window: its process is the downloaded file, or
        its title names AurOS outside Edge and Explorer."""
        for w in (self.windows() if ws is None else ws):
            proc, name = w.get("proc") or "", w.get("name") or ""
            if not w.get("rect") or w["rect"][2] < 200:
                continue
            if proc.lower().startswith(INSTALLER_STEM.lower()[:10]) or (
                    re.search(r"AurOS", name) and proc.lower() not in
                    ("msedge", "explorer", "searchhost", "shellexperiencehost")):
                return w
        return None

    def wizard_rect(self, ws=None):
        w = self.wizard_win(ws)
        return w["rect"] if w else None

    def wizard_named(self, win):
        """The installer's own controls, by name: its screen-reader object
        (MSAA), read through UI Automation by the elevated helper. Its
        primary button is the one whose key is Enter (B keeps the names
        stable, docs/handoff/streams/B.md "Changed for e2e"). [] for an
        installer without one, or a helper that cannot see inside it."""
        if not win or not win.get("proc"):
            return []
        match = "^" + re.escape(win["proc"]) + "$"
        got = [e for e in self.elements(match, ["Button", "RadioButton", "CheckBox", "Pane"],
                                        max=80, quiet=True)
               if e.get("proc") == win["proc"]]
        if not got and not self._named_diag:
            # Once, so that the evidence says why names could not be read
            # (not elevated, an installer without the screen-reader object...)
            self._named_diag = True
            self.log("installer window as UI Automation sees it: " + json.dumps(win))
            self.elements(match, ["Button", "RadioButton", "CheckBox", "Pane", "Window", "Custom",
                                  "Text", "Document", "Group", "List", "ListItem", "Image"], max=60)
        return got

    def wait_bitlocker_pause_confirmation(self, label):
        """Wait for, record and return the exact safe pause question."""
        deadline = time.time() + 30
        last = "the installer exposed no readable controls"
        while time.time() < deadline:
            if not self.m.alive():
                raise Failure("Windows restarted before the BitLocker pause was explicitly confirmed")
            img = self.m.screendump()
            win = self.wizard_win()
            named = self.wizard_named(win)
            try:
                primary, secondary = bitlocker_pause_controls(named)
            except Failure as e:
                last = str(e)
                time.sleep(1)
                continue
            # The wizard deliberately ignores action presses during the first
            # 700 ms of a new page. Re-read the exact controls after settling;
            # never click a rectangle retained from a page that changed.
            time.sleep(1.2)
            img = self.m.screendump()
            win = self.wizard_win()
            named = self.wizard_named(win)
            try:
                primary, secondary = bitlocker_pause_controls(named)
            except Failure as e:
                last = str(e)
                continue
            self.m.save(img, label)
            self.log("BitLocker pause confirmation: " + BITLOCKER_PAUSE_ACCESSIBLE)
            return primary, secondary
        raise Failure("the exact BitLocker pause confirmation did not appear: " + last)

    def prove_pause_not_armed(self, seen_events):
        """Enter must choose the initially focused Not yet and leave Windows safe."""
        self.m.key("ret", label="BitLocker warning: initially focused Not yet")
        deadline = time.time() + 30
        while time.time() < deadline:
            resets = [e for e in self.m.qmp.events[seen_events:] if e[1] == "RESET"]
            if resets or not self.m.alive():
                raise Failure("the first BitLocker warning action restarted instead of choosing Not yet")
            img = self.m.screendump()
            win = self.wizard_win()
            named = self.wizard_named(win)
            modal = [e for e in named if e.get("type") == "Pane" and
                     e.get("name") == BITLOCKER_PAUSE_ACCESSIBLE]
            restart = [e for e in named if e.get("type") == "Button" and
                       e.get("name") == "Restart now" and e.get("enabled", True) and
                       (e.get("key") or "").lower() == "enter"]
            if not modal and len(restart) == 1:
                vol, prot, pct = self.bitlocker_state("after the initially focused Not yet")
                if vol != "FullyEncrypted" or prot != "On" or pct < 99:
                    raise Failure("the first BitLocker warning action changed protection instead of "
                                  f"choosing Not yet ({vol}, protection {prot}, {pct}%)")
                self.m.save(img, "bitlocker-pause-not-yet")
                return
            time.sleep(1)
        raise Failure("Enter did not choose the BitLocker warning's initially focused Not yet")

    def drive_bitlocker_pause_confirmation(self, already_checked, seen_events):
        """Exercise safe default once, then explicitly choose the weakening action."""
        primary, _ = self.wait_bitlocker_pause_confirmation(
            "bitlocker-pause-warning-" + ("confirm" if already_checked else "not-yet"))
        if not already_checked:
            self.prove_pause_not_armed(seen_events)
            return False
        self.m.click(*vision.centre(primary["rect"]),
                     label="wizard " + BITLOCKER_PAUSE_PRIMARY)
        self.restart_clicked = time.time()
        return True

    def drive_wizard(self):
        """Press the primary button whenever it is enabled (found by name:
        the installer's button whose key is Enter; by colour when names
        cannot be read); when it is not, do what the page asks (tick its
        boxes, type the word it names) or wait. Each press starts a new timed phase named after
        it. Returns right after pressing "Restart now"."""
        clicks = {}
        type_tries = 0
        idle_since = time.time()
        last_label = None
        installing = False          # pressed the button that starts the install
        pause_warning_checked = False
        seen_events = len(self.m.qmp.events)
        last_text, text_since = "", time.time()
        last_sig, cand = None, [None, 0.0]     # the button about to be pressed, since when
        run_start = time.time()                # when the label pressed now was first pressed
        rect = self.wizard_rect()
        stage = [self.rec.phase("wizard: first page")]
        stage[0].__enter__()

        def next_stage(name):
            stage[0].__exit__(None, None, None)
            stage[0] = self.rec.phase(name)
            stage[0].__enter__()

        try:
            while left() > 0:
                if not self.m.alive():
                    raise Failure("the machine switched off during the wizard")
                resets = [e for e in self.m.qmp.events[seen_events:] if e[1] == "RESET"]
                seen_events = len(self.m.qmp.events)
                if resets and installing:
                    # It restarted by itself (the countdown after "Restart
                    # now"), which is what a person who walked away sees.
                    self.log("the machine restarted by itself after the install was prepared")
                    self.restart_clicked = resets[0][0]
                    stage[0].__exit__(None, None, None)
                    return
                if resets:
                    self.log("Windows restarted during the wizard (a fix that needs one?)")
                    self.fix_restarts += 1
                    rect = None
                if rect is None:
                    # Gone: closed, crashed, or Windows is restarting for
                    # a fix and the installer will reopen after sign-in.
                    self.m.shot("wizard-window-gone")
                    gone = time.time()
                    while rect is None and time.time() - gone < 15 * 60:
                        time.sleep(10)
                        rect = self.wizard_rect()
                    if rect is None:
                        w = self.windows()
                        raise Failure("the installer's window went away and did not come back; "
                                      "windows: " + json.dumps([(x.get('name'), x.get('proc')) for x in w]))
                    self.log("the installer's window is back")
                    continue
                img = self.m.screendump()
                win = self.wizard_win()
                rect = win["rect"] if win else rect
                named = self.wizard_named(win)
                sig = " | ".join(f"{e.get('type')}:{e.get('name')}{'' if e.get('enabled', True) else '(off)'}"
                                 f"{'[' + e['key'] + ']' if e.get('key') else ''}" for e in named)
                if sig != last_sig:
                    self.log("installer controls: " + (sig or "(none readable: colour is the fallback)"))
                    last_sig = sig
                enter = [e for e in named if e.get("type") == "Button" and (e.get("key") or "").lower() == "enter"]
                on = [e for e in enter if e.get("enabled", True)]
                if enter:
                    btns = [tuple(on[0]["rect"])] if on else []
                    label = on[0]["name"] if on else None
                else:
                    btns = [b for rgb in PRIMARY_RGB for b in vision.find_buttons(img, rect, rgb)]
                    label = None
                if btns:
                    b = max(btns, key=lambda b: (b[1] + b[3], b[0] + b[2]))
                    label = label or vision.text_of(vision.ocr(img, b, psm=7)).strip() or "?"
                    raw_label, label = label, canon_label(label)
                    restart_error = bitlocker_restart_control_error(
                        BITLOCKER_PUTBACK, installing, on, raw_label, label)
                    if restart_error:
                        self.m.save(img, "bitlocker-pause-unsafe-restart-control")
                        raise Failure(restart_error)
                    # OCR reads the same button a little differently each time
                    # ("Check again", "Checkiagain"): count presses by its first letters
                    # (run 39964877711 pressed 60 of them and never called it stuck)
                    key = re.sub(r"[^a-z]", "", label.lower())[:4] or "?"
                    # The installer ignores a press in the first 700 ms of a
                    # page (a double click must not skip one): press a button
                    # only once it has stood still for over a second.
                    if tuple(b) != cand[0]:      # (by place: OCR'd labels flicker)
                        cand[0], cand[1] = tuple(b), time.time()
                        time.sleep(1.2)
                        continue
                    if time.time() - cand[1] < 1.0:
                        time.sleep(0.5)
                        continue
                    if re.search(r"close", label, re.I) and not re.search(r"restart", label, re.I):
                        self.m.save(img, "wizard-ended-with-close")
                        raise Failure(close_verdict(VARIANT, vision.text_of(vision.ocr(img, rect))))
                    n = clicks.get(key, 0)
                    if not self.wizard_pages or n == 0 or key != last_label:
                        page = " ".join(e.get("name") or "" for e in named if e.get("type") == "Pane")
                        page = (page + " || " if page else "") + vision.text_of(vision.ocr(img, rect))
                        self.wizard_pages.append(page)
                        self.log(f"wizard page before '{label}' (read as {raw_label!r}): {page[:400]}")
                    if re.search(r"fix these", label, re.I):
                        self.fix_presses += 1
                    if key != last_label:
                        run_start = time.time()
                    # (B: the primary button is always there and pressing it early is
                    # harmless, so a page that is still working gets pressed again and
                    # again: stuck is many presses over minutes, not six in a row)
                    if key == last_label and n >= 6 and time.time() - run_start > 300:
                        self.m.save(img, "wizard-stuck")
                        raise Failure(f"pressed '{label}' {n} times over {int(time.time() - run_start)} s "
                                      "and the page did not move on: " +
                                      vision.text_of(vision.ocr(img, rect))[:900])
                    if re.search(r"restart", label, re.I):
                        if VARIANT == "bitlocker":
                            self.check_bitlocker_before_restart()  # while Windows is still up
                        else:
                            self.check_plain_before_restart()
                    self.m.click(*vision.centre(b), label="wizard " + label)
                    cand[0] = None
                    if label == "Restart now" and BITLOCKER_PUTBACK:
                        if not self.drive_bitlocker_pause_confirmation(
                                pause_warning_checked, seen_events):
                            pause_warning_checked = True
                            idle_since = time.time()
                            time.sleep(1)
                            continue
                        stage[0].__exit__(None, None, None)
                        return
                    if re.search(r"install|erase", label, re.I):
                        installing = True
                    if re.search(r"restart", label, re.I):
                        self.restart_clicked = time.time()
                        stage[0].__exit__(None, None, None)
                        return
                    clicks[key] = n + 1
                    last_label = key
                    idle_since = time.time()
                    next_stage(f"wizard: after '{label}'")
                    time.sleep(3)
                    continue
                # its own tick boxes, by name (a box she must tick to go on)
                tick = next((e for e in named if e.get("type") == "CheckBox" and e.get("toggle") == "Off"
                             and e.get("enabled", True) and clicks.get("#tick " + (e.get("name") or ""), 0) < 2), None)
                if enter and not on and tick:
                    clicks["#tick " + (tick.get("name") or "")] = clicks.get("#tick " + (tick.get("name") or ""), 0) + 1
                    self.click_el(tick, "wizard tick: " + (tick.get("name") or "")[:40])
                    idle_since = time.time()
                    time.sleep(1.5)
                    continue
                boxes = [] if enter else vision.find_unchecked_boxes(img, rect)
                if boxes:
                    self.m.click(*vision.centre(boxes[0]), label="wizard tick box")
                    time.sleep(1.5)
                    if len(vision.find_unchecked_boxes(self.m.screendump(), rect)) >= len(boxes):
                        self.log("the box did not tick")
                    idle_since = time.time()
                    continue
                words = vision.ocr(img, rect)
                text = vision.text_of(words)
                mt = re.search(r"\bType ([A-Z]{3,})\b", text)
                if mt and type_tries < 3 and time.time() - idle_since > (0 if type_tries == 0 else 8):
                    word = mt.group(1)
                    type_tries += 1
                    self.m.key("end", label=f"scroll to where {word} is typed")
                    time.sleep(1.5)
                    img = self.m.screendump()
                    # the input shows the word as its placeholder, in capitals,
                    # below the sentence asking for it (the button says "agree")
                    ph = vision.find_lone_word(vision.ocr(img, rect), word)
                    if ph:
                        self.m.click(*vision.centre(ph), label=f"wizard: the box for {word}")
                    for _ in range(len(word) + 4):
                        self.m.key("backspace")
                    self.m.type(word, label="typed confirmation")
                    time.sleep(1.5)
                    self.m.shot("wizard-typed")
                    idle_since = time.time()
                    continue
                # Busy: words the old wizard shows while it works, or any
                # number on the page moving (percent, MB, a countdown).
                if text != last_text:
                    if re.sub(r"\d", "", text) == re.sub(r"\d", "", last_text):
                        text_since = time.time()     # only numbers moved
                    last_text = text
                busy = (re.search(r"Setting up AurOS|Checking this PC|We only read", text)
                        or (enter and not on)       # "Checking…", "Fixing…": it is working
                        or time.time() - text_since < 45)
                if not busy and time.time() - idle_since > 60:
                    # Nothing to press, tick or type that can be seen: the
                    # page may be below the fold, or want Enter (the wizard's
                    # Enter is its primary action).
                    nudge = clicks.get("#nudge", 0)
                    clicks["#nudge"] = nudge + 1
                    if nudge % 2 == 0:
                        self.m.key("ret", label="nothing to click: Enter")
                    else:
                        self.m.key("end", label="nothing to click: scroll to the end")
                    idle_since = time.time()
                if time.time() - idle_since > 300 and int(time.time() - idle_since) % 300 < 15:
                    self.log("wizard waiting: " + text[-300:])
                time.sleep(15 if busy else 5)
            raise Failure("ran out of time in the wizard")
        except BaseException:
            stage[0].__exit__(*sys.exc_info())
            raise

    # -- 5. the restart, the staging screen, AurOS --
    def watch_install(self):
        with self.rec.phase("restart: Windows shuts down, firmware starts the installer"):
            self.m.auto_shots(5)
            p, _, words = self.wait_text(["Installing AurOS", "Getting ready", "AurOS is installed",
                                          "Yes, it all works"], 20 * 60, every=5,
                                         fail=["AurOS was not installed"])
            if not p:
                raise Failure("no installer screen within 20 minutes of Restart now")
            self.log(f"first AurOS screen: {p}")
            back = self.hub.event("helper-up", since=self.restart_clicked)
            if back is not None:
                raise Failure("Windows came back instead of the installer")
        self.m.auto_shots(10)
        with self.rec.phase("staging: make room, copy AurOS, check, start it"):
            p, _, words = self.wait_text(["Yes, it all works", "Does it work", "does this work"],
                                         min(left(), 150 * 60), every=15,
                                         fail=["AurOS was not installed"])
            if not p:
                raise Failure("AurOS did not ask whether it works")
            if self.hub.event("helper-up", since=self.restart_clicked) is not None:
                raise Failure("Windows started again during the install")
        self.m.auto_shots(30)

    def answer_yes(self):
        with self.rec.phase("aurOS: say it works"):
            time.sleep(5)
            b, words = self.ocr_find("Yes, it all works")
            if not b:
                raise Failure("the Yes button went away")
            self.m.click(*vision.centre(b), label="AurOS: Yes, it all works")
            time.sleep(10)
            self.m.shot("after-yes")
            img = self.m.screendump()
            words = vision.ocr(img)
            self.log("after yes: " + vision.text_of(words)[:600])
            # The newer first start asks once more: "Start AurOS from now on?" with
            # a button of that name (the lowest match: its heading says it too).
            sure = vision.find_text(words, "Start AurOS from now on", last=True)
            if sure and vision.find_text(words, "Not yet"):
                self.m.click(*vision.centre(sure), label="AurOS: Start AurOS from now on")
                time.sleep(12)
                self.m.shot("after-start")
                self.log("after start: " + vision.text_of(vision.ocr(self.m.screendump()))[:600])

    # -- 6. M3: a program from the store --
    def store(self):
        with self.rec.phase("aurOS: open the store"):
            # Panels left over from the first start (bring my files, close)
            for dismiss in ("Not now", "Close"):
                b, words = self.ocr_find(dismiss)
                if not b and dismiss == "Not now":
                    # "Bring my files across" is the page's green button and "Not now" sits a
                    # button's height under it; OCR read it as 'ai' in run 40043978048
                    bring = vision.find_text(words, "Bring my files")
                    if bring:
                        x, y = vision.centre(bring)
                        b = (x - 40, y + 66 - 12, 80, 24)
                if b:
                    self.m.click(*vision.centre(b), label="dismiss " + dismiss)
                    time.sleep(3)
            # Where a person would look: the screen as it is, the Home
            # card (Home key in the rail layout), the AurOS button at the
            # top left, the Super key.
            b, words = self.ocr_find(STORE_NAME)
            tries = [("key", "home"), ("text", "AurOS"), ("key", "meta_l"), ("key", "esc")]
            while not b and tries:
                kind, what = tries.pop(0)
                if kind == "key":
                    self.m.key(what, label=f"look for {STORE_NAME}: {what}")
                else:
                    t = vision.find_text(words, what)
                    if not t:
                        continue
                    self.m.click(*vision.centre(t), label=f"look for {STORE_NAME}: {what}")
                time.sleep(3)
                b, words = self.ocr_find(STORE_NAME)
            if not b:
                self.m.shot("no-store")
                raise Failure(f"no '{STORE_NAME}' anywhere a person would look: " +
                              vision.text_of(words)[:600])
            self.m.click(*vision.centre(b), label="open " + STORE_NAME)
            # Opened = its window is there: a word of it read (OCR garbles the tabs, "#extere
            # / Ginstat...", their icons in front), or its frame seen in pixels.
            p, words = None, None
            deadline = time.time() + 300
            while time.time() < deadline and not p:
                img = self.m.screendump()
                if self.store_frame(img):
                    p = "its window frame"
                    break
                words = vision.ocr(img)
                for w in ("Search", "Explore", "Installed", "Updates", "Software", "Instal"):
                    if vision.find_text(words, w):
                        p = w
                        break
                if not p:
                    time.sleep(8)
            if not p:
                self.m.shot("store-did-not-open")
                raise Failure("the store NEVER OPENED (no window of it read on screen in 5 minutes): " +
                              vision.text_of(vision.ocr(self.m.screendump()))[:400])
            self.m.shot("store-open")
            # opened is not the same as showing something: its window drew its header
            # and an empty body for five minutes on a real install (run 40043978048)
            deadline = time.time() + 300
            while time.time() < deadline and not self.store_has_content(self.m.screendump()):
                time.sleep(10)
            if not self.store_has_content(self.m.screendump()):
                self.m.shot("store-empty")
                raise Failure("the store OPENED but its catalogue stayed EMPTY for 5 minutes (header bar and tabs "
                              "drawn, nothing under them): " + vision.text_of(vision.ocr(self.m.screendump()))[:300])
        with self.rec.phase("aurOS: find and install " + STORE_QUERY):
            found = None
            for attempt in range(3):
                b, _ = self.ocr_find("Search")
                if b:
                    self.m.click(*vision.centre(b), label="store search")
                else:
                    # The search is a magnifying glass at the left of the store's
                    # Explore tab (no word on it; run 40003466428). A person clicks
                    # it; Ctrl+F reached nothing, the keyboard was not in that window.
                    ex, _ = self.ocr_find("Explore")
                    mag = self.store_magnifier(self.m.screendump(), ex)
                    if mag and attempt != 1:
                        self.m.click(*mag, label="store search (the magnifying glass)")
                    else:
                        # a click in the window's header gives it the keyboard, then its own shortcut
                        if mag:
                            self.m.click(mag[0] + 300, mag[1], label="store: its header, for the keyboard")
                            time.sleep(1)
                        self.m.key("ctrl", "f", label="store search (Ctrl+F)")
                time.sleep(2)
                self.m.key("ctrl", "a")
                self.m.type(STORE_QUERY, label="search")
                self.m.key("ret")
                # the catalogue can take a minute or two after first going online
                found, _ = self.wait_store_result(STORE_QUERY, STORE_RESULT, 90, every=10)
                if found:
                    break
                self.m.shot("store-no-result-yet")
            if not found:
                raise Failure(f"no '{STORE_RESULT}' in the search results after three searches")
            self.m.click(*vision.centre(found), label="open result " + STORE_RESULT)
            # The page's one big button: "Install", then (when done) "Open". Found as
            # the solid block of the theme's accent in the page header and read by
            # OCR on its own; full-screen OCR missed "Install" on a gold button (run
            # 40005591638). On the light theme its letters are dark on dark green and
            # cannot be read at all (run 40038619603, 0025-store-no-install.png), so
            # an UNREADABLE block in the header counts as the button, and what it does
            # is watched in pixels: it goes away while installing and comes back as Open.
            deadline = time.time() + 300
            btn = None
            while time.time() < deadline and not btn:
                time.sleep(8)
                img = self.m.screendump()
                b = self.store_button(img)
                if b:
                    self.log(f"the store's button reads {b[1]!r} at {b[0]}")
                    if not b[1] or label_is(b[1], STORE_INSTALL):
                        btn = b[0]
                if not btn:
                    btn = vision.find_lone_word(vision.ocr(img), STORE_INSTALL)
            if not btn:
                self.m.shot("store-no-install")
                raise Failure(f"no '{STORE_INSTALL}' button")
            self.m.move(1150, 300)
            time.sleep(1)
            img0 = self.m.screendump()
            sig0 = self.header_sig(img0)                     # the page before the press
            ratio = vision.label_contrast(img0, btn)
            self.log(f"the store's {STORE_INSTALL} button: label contrast {ratio:.1f}:1 (docs/EASY.md wants 4.5)")
            if ratio < CONTRAST_MIN:
                self.findings.append(f"the store's '{STORE_INSTALL}' button label has contrast {ratio:.1f}:1, "
                                     f"under {CONTRAST_MIN}:1 (shot {os.path.basename(self.m.shot('store-install-contrast'))})")
            self.m.click(*vision.centre(btn), label="install")
            # Hands off the button: the pointer drawn over its letters makes them
            # unreadable (run 40011085445 read nothing for nine minutes)
            self.m.move(1150, 300)
            done = None
            deadline = time.time() + 45 * 60
            clicked, presses, seen = time.time(), 1, None
            left_install = False        # the button went away: it is installing
            open_words = [o for o in STORE_OPEN.split("|") if " " not in o]
            while time.time() < deadline and not done:
                time.sleep(15)
                img = self.m.screendump()
                words = vision.ocr(img)
                for bad in ("Authentication", "Failed to install", "Unable to install"):
                    if vision.find_text(words, bad):
                        self.m.save(img, "store-" + bad)
                        raise Failure(f"the store says '{bad}': {vision.text_of(words)[:500]}")
                b = self.store_button(img)
                changed = self.header_sig(img) != sig0
                state = (bool(b), b[1] if b else None, changed, left_install)
                if state != seen:
                    seen = state
                    self.log(f"the store's button now {'reads %r' % b[1] if b else 'is gone'}; page changed: {changed}")
                if not b:
                    left_install = True
                elif label_is(b[1], *open_words) or (left_install and not label_is(b[1], STORE_INSTALL)):
                    done = (b[1] or "Open", b[0])
                    break
                # A press that changed nothing (run 40008357634: five minutes later
                # the page still said Install): a person presses again, and says so
                # if it never takes.
                if b and not changed and not left_install and time.time() - clicked > 75:
                    if presses >= 4:
                        self.m.shot("store-install-did-nothing")
                        raise Failure(f"pressed '{STORE_INSTALL}' {presses} times over {int(time.time() - clicked)} s "
                                      "and the store showed no sign of installing: " + vision.text_of(words)[:400])
                    # Each try is a different hand, and the log says which one took:
                    # 2: a hand arriving (small moves) and a slow press; 3: a double press
                    how = {1: dict(jiggle=True, hold=0.4), 2: dict(double=True)}.get(presses, {})
                    self.m.click(*vision.centre(b[0]), label=f"install (again, try {presses + 1}: {how or 'plain'})", **how)
                    self.m.move(1150, 300)
                    clicked, presses = time.time(), presses + 1
                    continue
                for o in STORE_OPEN.split("|"):
                    box = (vision.find_lone_word(words, o) if " " not in o
                           else vision.find_text(words, o))
                    if box:
                        done = (o, box)
                        break
            if not done:
                raise Failure("the install did not finish in 45 minutes")
            self.m.shot("store-installed")
        with self.rec.phase("aurOS: open " + STORE_QUERY):
            self.m.click(*vision.centre(done[1]), label="open the installed program: " + done[0])
            self.m.move(1150, 300)
            deadline = time.time() + 120
            privacy_continued = False
            while time.time() < deadline:
                time.sleep(5)
                img = self.m.screendump()
                words = vision.ocr(img)
                error = self.program_launch_error(words)
                if error:
                    self.m.save(img, "program-launch-failed")
                    raise Failure(f"the store could not open {STORE_QUERY}: {error}: " +
                                  vision.text_of(words)[:500])
                identity = self.program_window_identity(words, img.shape[0], STORE_RESULT)
                privacy_step = self.program_privacy_step(
                    words, img.shape[0], STORE_RESULT)
                if privacy_step is not None:
                    identity, proceed = privacy_step
                    if not identity or not proceed or privacy_continued:
                        self.m.save(img, "program-launch-failed")
                        raise Failure(f"{STORE_QUERY}'s first-launch privacy dialog was not a "
                                      "single attributed Continue step: " +
                                      vision.text_of(words)[:500])
                    if proceed == "focused default":
                        self.m.key("ret", label=f"continue {STORE_RESULT}'s first launch")
                    else:
                        self.m.click(*vision.centre(proceed),
                                     label=f"continue {STORE_RESULT}'s first launch")
                    privacy_continued = True
                    continue
                if identity:
                    where, box = identity
                    # A window task is independent compositor evidence, but the
                    # program may still be behind Software. Bring that exact task
                    # forward before judging its client controls; this prevents
                    # Software's embedded VLC promotional screenshot supplying them.
                    if where == "window task":
                        self.m.click(*vision.centre(box), label=f"show the mapped {STORE_RESULT} window")
                        time.sleep(3)
                        img = self.m.screendump()
                        words = vision.ocr(img)
                        error = self.program_launch_error(words)
                        if error:
                            self.m.save(img, "program-launch-failed")
                            raise Failure(f"{STORE_QUERY} did not reach a usable window: {error}: " +
                                          vision.text_of(words)[:500])
                        identity = self.program_window_identity(words, img.shape[0], STORE_RESULT)
                        privacy_step = self.program_privacy_step(
                            words, img.shape[0], STORE_RESULT)
                        if privacy_step is not None:
                            identity, proceed = privacy_step
                            if not identity or not proceed or privacy_continued:
                                self.m.save(img, "program-launch-failed")
                                raise Failure(f"{STORE_QUERY}'s first-launch privacy dialog was not a "
                                              "single attributed Continue step: " +
                                              vision.text_of(words)[:500])
                            if proceed == "focused default":
                                self.m.key("ret", label=f"continue {STORE_RESULT}'s first launch")
                            else:
                                self.m.click(*vision.centre(proceed),
                                             label=f"continue {STORE_RESULT}'s first launch")
                            privacy_continued = True
                            continue
                    proven = self.program_window_proven(
                        words, img.shape[0], STORE_RESULT, STORE_APP_CONTROLS.split("|"))
                    if proven:
                        identity, controls = proven
                        self.m.save(img, "program-open")
                        self.log(f"mapped program window identity: {identity}; "
                                 f"visible controls: {', '.join(controls)}; on screen: " +
                                 vision.text_of(words)[:500])
                        break
            else:
                img = self.m.shot("program-did-not-open")
                raise Failure(f"{STORE_QUERY} never opened a mapped program window in 2 minutes; "
                              "the store's listing and preview are not a running app: " +
                              vision.text_of(vision.ocr(img))[:500])

    @staticmethod
    def program_launch_error(words):
        """A visible store error after Open is a failure, never app evidence."""
        for message in ("Sorry, something went wrong", "This did not start"):
            if vision.find_text(words, message):
                return message
        return None

    @classmethod
    def program_privacy_step(cls, words, screen_h, app_name):
        """Return mapped identity and this dialog's Continue action.

        VLC's real first-launch dialog can put its focused Continue button far
        to the right and below the title, where whole-screen OCR misses it.
        The dialog body names VLC itself, so that text below the privacy title
        is mapped-app attribution which Software's listing cannot supply.
        """
        privacy = vision.find_text(words, "Privacy and Network Access Policy")
        if not privacy:
            return None
        px, py, pw, ph = privacy
        identity = cls.program_window_identity(words, screen_h, app_name)
        mapped_identity = cls.program_privacy_window_identity(words, screen_h, app_name)
        if mapped_identity:
            identity = mapped_identity
        dialog_identity = None
        named = vision.find_text(words, app_name + " media player")
        if named:
            _, ny, _, _ = named
            ncx, _ = vision.centre(named)
            if (py + ph < ny <= py + ph + 250 and
                    px - 100 <= ncx <= px + pw + 400):
                dialog_identity = "privacy dialog", named
                if mapped_identity:
                    identity = dialog_identity
        buttons = []
        for word in words:
            if re.sub(r"[^a-z]", "", word.get("text", "").lower()) != "continue":
                continue
            box = (word.get("x", 0), word.get("y", 0),
                   word.get("w", 0), word.get("h", 0))
            cx, cy = vision.centre(box)
            if px <= cx <= px + pw and py + ph < cy <= py + ph + 300:
                buttons.append(box)
        proceed = buttons[0] if identity and len(buttons) == 1 else None
        if dialog_identity and mapped_identity and not buttons:
            proceed = "focused default"
        return identity, proceed

    @classmethod
    def program_privacy_window_identity(cls, words, screen_h, app_name):
        """Prove the app window beside its dialog despite a one-letter OCR miss."""
        controls = STORE_APP_CONTROLS.split("|")
        proven = cls.program_window_proven(words, screen_h, app_name, controls)
        if proven:
            return proven[0]
        top = max(120, screen_h // 5)
        expected = re.sub(r"[^a-z0-9]", "", app_name.lower())
        for word in words:
            token = re.sub(r"[^a-z0-9]", "", word.get("text", "").lower())
            if (word.get("y", 0) + word.get("h", 0) > top or
                    len(token) != len(expected) or
                    sum(a != b for a, b in zip(token, expected)) > 1):
                continue
            same_line = [other for other in words
                         if other.get("line") == word.get("line") and
                         word.get("x", 0) < other.get("x", 0) <= word.get("x", 0) + 160]
            media = next((other for other in same_line
                          if re.sub(r"[^a-z]", "", other.get("text", "").lower()) == "media"), None)
            if not media:
                continue
            title = (word.get("x", 0), word.get("y", 0),
                     media.get("x", 0) + media.get("w", 0) - word.get("x", 0),
                     max(word.get("h", 0), media.get("h", 0)))
            if len(cls.program_window_controls(words, controls, title)) >= 2:
                return "title", title
        return None

    @staticmethod
    def program_window_identity(words, screen_h, app_name):
        """Return visible identity belonging to a mapped top-level app window.

        The shell paints mapped window identity in the top-level title band and,
        for layouts with a window task band, at the bottom.  Store listing text
        and its promotional screenshots live in the middle and are deliberately
        excluded: run 40131473307 showed a VLC screenshot there after launch had
        failed, which is not a running VLC window or process.
        """
        top = max(120, screen_h // 5)
        bottom = screen_h - max(120, screen_h // 5)
        shell_identity = [word for word in words
                          if word.get("y", 0) + word.get("h", 0) <= top or
                          word.get("y", 0) >= bottom]
        box = vision.find_text(shell_identity, app_name)
        if not box:
            return None
        where = "title" if box[1] < top else "window task"
        return where, box

    @staticmethod
    def program_window_controls(words, app_controls, title_box=None):
        """Client controls visible immediately below the foreground title."""
        if title_box:
            title_bottom = title_box[1] + title_box[3]
            words = [word for word in words
                     if title_bottom <= word.get("y", 0) <= title_bottom + 180]
        return [control for control in app_controls if vision.find_text(words, control)]

    @classmethod
    def program_window_proven(cls, words, screen_h, app_name, app_controls):
        """Require the app to own the foreground title and expose real controls."""
        identity = cls.program_window_identity(words, screen_h, app_name)
        if not identity or identity[0] != "title":
            return None
        controls = cls.program_window_controls(words, app_controls, identity[1])
        if len(controls) < 2:
            return None
        return identity, controls

    @staticmethod
    def store_magnifier(img, explore_box=None):
        """The search button of the store's header: 47 px right of the window's
        left frame line, on the tabs' row. (Measured on the screenshots of runs
        40040689945 / 40047454422: frame at x=243, the button's outline at
        263..316, its centre 290. An offset from the Explore tab drifted when the
        tabs gained icons, 196 then 272, and OCR missed the tab altogether in
        run 40051742550: so the frame is found by pixels, on the row of the tabs
        when OCR gave it, else y=103 (the header's middle at 100 and at 107).)"""
        cy = vision.centre(explore_box)[1] if explore_box else 103
        lum = img[300].astype(int).sum(axis=1) // 3       # a row through the window's body
        frame = next((x for x in range(200, 400) if lum[x] < 110), None)
        return (frame + 47, cy) if frame else None

    @staticmethod
    def store_result(words, screen_h, query, phrase):
        """The one exact result-title line below the typed query, or None.

        Run 40138682256 exposed ``VLC`` in both places. A full-screen lookup
        clicked the query at y=162 instead of the result at y=254. Relative
        placement distinguishes them without assuming a window size, while an
        exact, unique line refuses prefixes, descriptions and ambiguous rows.
        """
        lines = {}
        for word in words:
            raw_line = word.get("line")
            key = tuple(raw_line) if isinstance(raw_line, (list, tuple)) else (raw_line,)
            lines.setdefault(key, []).append(word)

        def normalized(line):
            text = " ".join(word.get("text", "") for word in
                            sorted(line, key=lambda word: word.get("x", 0)))
            return re.sub(r"[^a-z0-9]", "", text.lower())

        def box(line):
            x0 = min(word.get("x", 0) for word in line)
            y0 = min(word.get("y", 0) for word in line)
            x1 = max(word.get("x", 0) + word.get("w", 0) for word in line)
            y1 = max(word.get("y", 0) + word.get("h", 0) for word in line)
            return x0, y0, x1 - x0, y1 - y0

        want_query = re.sub(r"[^a-z0-9]", "", query.lower())
        want_result = re.sub(r"[^a-z0-9]", "", phrase.lower())
        query_boxes = [box(line) for line in lines.values()
                       if normalized(line) == want_query and
                       box(line)[1] + box(line)[3] < screen_h // 4]
        if len(query_boxes) != 1:
            return None
        query_box = query_boxes[0]
        query_bottom = query_box[1] + query_box[3]
        candidates = [box(line) for line in lines.values()
                      if normalized(line) == want_result and
                      box(line)[1] >= screen_h // 4 and box(line)[1] > query_bottom]
        return candidates[0] if len(candidates) == 1 else None

    @staticmethod
    def store_frame(img):
        """The x of the store window's left frame line (a dark vertical line down
        most of the rows 150..500 between x=200 and 400), or None: the shell's
        own panel border at x=192 is not in that range."""
        lum = img[150:500, 200:400].astype(int).sum(axis=2) // 3
        dark = (lum < 110).mean(axis=0)
        cols = np.nonzero(dark > 0.9)[0]
        return int(cols[0]) + 200 if len(cols) else None

    @staticmethod
    def store_has_content(img):
        """The store's window shows something under its header: in a block just
        below the header (x from the window's frame, y 150..420: inside the
        smallest window it has opened as), more than a few percent of the
        pixels differ from the commonest colour there. An empty body is one
        flat colour; a page of tiles is not."""
        x0 = (AurFlow.store_frame(img) or 243) + 10
        reg = img[150:420:2, x0:x0 + 540:2].reshape(-1, 3)
        vals, counts = np.unique(reg, axis=0, return_counts=True)
        return 1.0 - counts.max() / len(reg) > 0.03

    def store_button(self, img):
        """The store's main button on an app's page: (box, what it says), or
        None. It is a solid block of the theme's accent in the page's header
        (AurOS's gold, its green, the old teal), read by OCR on its own: a
        button's few letters are read far better alone than among a screen."""
        for rgb in STORE_ACCENTS:
            for b in vision.find_buttons(img, (240, 100, 800, 330), rgb)[:1]:
                return b, vision.text_of(vision.ocr(img, b, psm=7)).strip()     # '' if unreadable
        return None

    @staticmethod
    def header_sig(img):
        """A fingerprint of the store page's header: what changes when the button
        is pressed and something starts (the pointer is kept away from it)."""
        import hashlib
        return hashlib.md5(img[100:330:3, 240:1040:3].tobytes()).hexdigest()

    # -- D13, after AurOS: Windows comes back, with BitLocker on, no recovery key asked --
    def pick_grub_entry(self, name, tries=12):
        """In AurOS's start menu (text GRUB): move down until the line that
        says exactly `name` is the highlighted one (its row is light: the
        selected row is drawn inverted), then Enter. Fails with what is shown."""
        for i in range(tries):
            img = self.m.screendump()
            words = vision.ocr(img)
            lines = {}
            for w in words:
                lines.setdefault(w["line"], []).append(w)
            for ws in lines.values():
                text = " ".join(w["text"] for w in ws)
                if re.sub(r"[^a-z]", "", text.lower()) == re.sub(r"[^a-z]", "", name.lower()):
                    y0 = max(0, min(w["y"] for w in ws) - 2)
                    y1 = min(img.shape[0], max(w["y"] + w["h"] for w in ws) + 2)
                    lum = float(img[y0:y1, :].mean())
                    self.log(f"start menu: '{text}' row brightness {lum:.0f}")
                    if lum > 110:                      # the selected row is light
                        self.m.key("ret", label=f"start menu: {name}")
                        return
            self.m.key("down", label="start menu: down" if i == 0 else None)
            time.sleep(1.2)
        self.m.shot("start-menu-no-entry")
        raise Failure(f"AurOS's start menu never highlighted '{name}': " +
                      vision.text_of(vision.ocr(self.m.screendump()))[:400])

    def wait_windows_return(self, since, ordinal, reason, timeout=12 * 60):
        """Wait for Windows without ever exposing a recovery password, then
        record a sanitized protection state for this particular return."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.hub.event("helper-up", since=since) is not None:
                break
            words = vision.ocr(self.m.screendump())
            text = vision.text_of(words)
            if re.search(r"recovery key|BitLocker recovery|enter the recovery", text, re.I):
                self.m.shot(f"bitlocker-recovery-screen-return-{ordinal}")
                raise Failure(f"Windows return {ordinal} asked for the BitLocker recovery key")
            time.sleep(10)
        else:
            self.m.shot(f"windows-return-{ordinal}-missing")
            raise Failure(f"Windows return {ordinal} did not reach its helper in {timeout // 60} minutes")
        time.sleep(45)
        self.m.shot(f"windows-return-{ordinal}")
        deadline = time.time() + 4 * 60
        while True:
            state = self.bitlocker_evidence()
            baseline = self.putback_base["protectors"] if self.putback_base else None
            bad = bitlocker_return_verdict(state, baseline)
            evidence = {"schema": 1, "return": ordinal, "reason": reason,
                        "observed_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                        "protection_ok": bad is None, "failure": bad, **state}
            self.write_bitlocker_return(evidence)
            if bad is None:
                break
            if time.time() > deadline:
                raise Failure(f"Windows return {ordinal} has unsafe BitLocker state: {bad}")
            time.sleep(20)

        self.log(f"Windows return {ordinal}: BitLocker fully encrypted and protection on")
        return evidence

    def write_bitlocker_return(self, evidence):
        """Keep the last sanitized observation even when the return is RED."""
        ordinal, reason = evidence["return"], evidence["reason"]
        state = evidence
        with open(os.path.join(self.a.out, f"bitlocker-return-{ordinal}.json"), "w") as fh:
            json.dump(evidence, fh, indent=2, sort_keys=True)
            fh.write("\n")
        with open(os.path.join(self.a.out, f"bitlocker-return-{ordinal}.txt"), "w") as fh:
            fh.write(f"Windows return {ordinal}: {reason}\n")
            fh.write(f"volume={state['volume_status']} protection={state['protection_status']} "
                     f"encrypted={state['encryption_percentage']}%\n")
            fh.write(f"protection_ok={str(evidence['protection_ok']).lower()}\n")
            if evidence["failure"]:
                fh.write(f"failure={evidence['failure']}\n")
            for protector in state["protectors"]:
                fh.write(f"protector type={protector['type']} id={protector['id']}\n")

    def back_to_windows(self):
        """The machine is switched off and on again like a person would, AurOS's
        start menu picks Windows, and Windows must come up WITHOUT the BitLocker
        recovery screen and with BitLocker protecting C: again (D13). Written
        from the menu's source (rootfs/usr/lib/auros/grub.cfg.in), NOT yet seen
        working: the first run that gets here shows how far it goes."""
        with self.rec.phase("windows: back from AurOS, through its own start menu"):
            t = time.time()
            self.m.start()
            self.m.auto_shots(10)
            # The menu counts down 3 seconds and OCR takes longer than that, so first a
            # key every 0.4 s, blind, for as long as the firmware and shim need (any key
            # stops the countdown for good); only then look (run 40047642180 looked too
            # early, the countdown ran out and AurOS's desktop came up).
            end_blind = time.time() + 40
            while time.time() < end_blind:
                self.m.key("shift")
                time.sleep(0.4)
            seen_menu = False
            deadline = time.time() + 120
            while time.time() < deadline and not seen_menu:
                self.m.key("shift")
                words = vision.ocr(self.m.screendump())
                seen_menu = bool(vision.find_text(words, "verbose") or vision.find_text(words, "GRUB"))
                if not seen_menu:
                    self.log("start menu: not yet: " + vision.text_of(words)[:160])
            if not seen_menu:
                self.m.shot("start-menu-never-showed")
                raise Failure("AurOS's start menu never showed after the restart: " +
                              vision.text_of(vision.ocr(self.m.screendump()))[:300])
            self.m.shot("start-menu")
            self.pick_grub_entry("Windows")
            return self.wait_windows_return(
                t, 1, "first Windows return selected from AurOS after installation")

    def seed_putback_fixture(self):
        """Put one known file in AurOS while the disposable VM is off.

        This touches only this run's qcow2 overlay. It refuses a live VM,
        symlinks, a disk outside WORK, a non-overlay image, and ambiguous
        AurOS roots. The golden image and host disks are never candidates.
        """
        if self.m.alive():
            raise Failure("refusing to seed the put-back fixture while the VM is running")
        work = os.path.realpath(self.a.work)
        disk = os.path.realpath(self.disk)
        if os.path.islink(self.disk) or os.path.dirname(disk) != work or os.path.basename(disk) != "win.qcow2":
            raise Failure("refusing to seed a disk that is not WORK/win.qcow2")
        info = json.loads(run(["qemu-img", "info", "--output=json", disk], self.rec).stdout)
        backing = info.get("full-backing-filename") or info.get("backing-filename")
        if info.get("format") != "qcow2" or not backing or os.path.realpath(backing) == disk:
            raise Failure("refusing to seed a disk that is not a qcow2 overlay")

        data = ("AurOS put-back E2E fixture " + os.urandom(24).hex() + "\n").encode("ascii")
        fixture = {"name": "auros-e2e-putback.txt", "sha256": hashlib.sha256(data).hexdigest(),
                   "bytes": len(data)}
        local = os.path.join(work, "putback-fixture")
        with open(local, "wb") as fh:
            fh.write(data)
        dev, mnt, chosen = "/dev/nbd7", os.path.join(work, "putback-mnt"), None
        os.makedirs(mnt, exist_ok=True)
        run("sudo modprobe nbd max_part=16", self.rec, check=False)
        if run(["sudo", "qemu-nbd", "--connect", dev, disk], self.rec, check=False).returncode != 0:
            raise Failure("could not attach the disposable overlay for the put-back fixture")
        try:
            time.sleep(3)
            run(["sudo", "partprobe", dev], self.rec, check=False)
            base = os.path.basename(dev)
            candidates = []
            for part in sorted(p for p in os.listdir("/dev") if p.startswith(base + "p")):
                path = "/dev/" + part
                fs = run(["sudo", "blkid", "-o", "value", "-s", "TYPE", path], self.rec,
                         check=False).stdout.strip()
                if fs != "ext4":
                    continue
                if run(["sudo", "mount", "-o", "ro,noload", path, mnt], self.rec,
                       check=False).returncode != 0:
                    continue
                try:
                    if os.path.isfile(os.path.join(mnt, "usr/lib/auros/answer.sh")) and \
                            os.path.isdir(os.path.join(mnt, "home/her/Documents")):
                        present = [rel for rel in APP_FILES.split()
                                   if os.path.lexists(os.path.join(mnt, rel))]
                        candidates.append((path, present))
                finally:
                    run(["sudo", "umount", mnt], self.rec)
            if len(candidates) != 1:
                raise Failure(f"expected exactly one installed AurOS root, found {len(candidates)}")
            chosen, installed_apps = candidates[0]
            if not installed_apps:
                raise Failure("the installed program is absent before put-back")
            with open(os.path.join(self.a.out, "putback-app-on-disk.txt"), "w") as fh:
                fh.write("Program evidence present before removal:\n" + "\n".join(installed_apps) + "\n")
            if run(["sudo", "mount", "-o", "rw", chosen, mnt], self.rec,
                   check=False).returncode != 0:
                raise Failure("could not mount the installed AurOS root for the fixture")
            try:
                docs = os.path.join(mnt, "home/her/Documents")
                dest = os.path.join(docs, fixture["name"])
                if os.path.islink(docs) or os.path.lexists(dest):
                    raise Failure("refusing an unexpected Documents link or existing fixture path")
                run(["sudo", "cp", "--", local, dest], self.rec)
                run(["sudo", "chown", "--reference=" + docs, "--", dest], self.rec)
                run(["sudo", "chmod", "0644", "--", dest], self.rec)
                run(["sudo", "sync"], self.rec)
            finally:
                run(["sudo", "umount", mnt], self.rec)
        finally:
            mounts = run(["findmnt", "-rn", "-o", "SOURCE"], self.rec, check=False).stdout.splitlines()
            still_mounted = [source for source in mounts if source == dev or source.startswith(dev + "p")]
            if still_mounted:
                run(["sudo", "umount", mnt], self.rec, check=False)
                mounts = run(["findmnt", "-rn", "-o", "SOURCE"], self.rec,
                             check=False).stdout.splitlines()
                still_mounted = [source for source in mounts
                                 if source == dev or source.startswith(dev + "p")]
            unsafe = os.path.join(self.a.out, "UNSAFE-DISK-ATTACHED.txt")
            if still_mounted:
                with open(unsafe, "w") as fh:
                    fh.write("nbd partitions remain mounted; do not open this overlay again\n")
                raise Failure("refusing to detach or boot: nbd partitions remain mounted: " +
                              ", ".join(still_mounted))
            if run(["sudo", "qemu-nbd", "--disconnect", dev], self.rec,
                   check=False).returncode != 0:
                with open(unsafe, "w") as fh:
                    fh.write("qemu-nbd disconnect failed; do not open this overlay again\n")
                raise Failure("refusing to boot: qemu-nbd did not detach the qcow2 overlay")
            for _ in range(20):
                if not os.path.exists("/sys/block/" + os.path.basename(dev) + "/pid"):
                    break
                time.sleep(0.25)
            else:
                with open(unsafe, "w") as fh:
                    fh.write("qemu-nbd still reports the overlay attached; do not open it again\n")
                raise Failure("refusing to boot: the qcow2 overlay remains attached to qemu-nbd")
            try:
                os.unlink(local)
            except FileNotFoundError:
                pass
        self.putback_fixture = fixture
        with open(os.path.join(self.a.out, "putback-fixture.json"), "w") as fh:
            json.dump(fixture, fh, indent=2, sort_keys=True)
            fh.write("\n")

    def request_putback(self):
        """Boot AurOS normally and use the same two deliberate presses a
        person uses in Settings to request put-back."""
        with self.rec.phase("aurOS: request Put Windows back with files"):
            t = time.time()
            self.m.start()
            self.m.auto_shots(10)
            deadline = time.time() + 10 * 60
            settings = None
            while time.time() < deadline:
                img = self.m.screendump()
                words = vision.ocr(img)
                settings = vision.find_text(words, "Settings", last=True)
                if settings:
                    break
                self.m.key("home", label="AurOS: show home")
                time.sleep(8)
            if not settings:
                self.m.shot("putback-no-settings")
                raise Failure("AurOS did not show Settings before put-back")
            self.m.click(*vision.centre(settings), label="AurOS: Settings")
            time.sleep(3)
            putback, words = self.ocr_find("Put Windows back")
            if not putback:
                raise Failure("Settings did not offer Put Windows back: " + vision.text_of(words)[:400])
            self.m.click(*vision.centre(putback), label="Settings: Put Windows back")
            time.sleep(3)
            remove, words = self.ocr_find("Remove AurOS and put Windows back")
            if not remove:
                raise Failure("Put Windows back did not offer removal: " + vision.text_of(words)[:400])
            self.m.click(*vision.centre(remove), label="Put Windows back: first deliberate press")
            time.sleep(2.0)
            armed, words = self.ocr_find("Press again to restart and remove AurOS")
            if not armed:
                raise Failure("Put Windows back did not arm its second press: " + vision.text_of(words)[:400])
            self.putback_requested_at = time.time()
            self.m.click(*vision.centre(armed), label="Put Windows back: second deliberate press")
            return t

    def putback_final_state(self):
        """Read bounded final state; hashes and counts only, never file data."""
        if not self.putback_base or not self.putback_fixture:
            raise Failure("put-back verification has no recorded initial state or fixture")
        restored = "C:\\Users\\aurtest\\From AurOS\\Documents\\" + self.putback_fixture["name"]
        out = self.guest(
            "$ErrorActionPreference='Stop'; "
            "function H([string]$x){$a=[Security.Cryptography.SHA256]::Create();try{"
            "([BitConverter]::ToString($a.ComputeHash([Text.Encoding]::UTF8.GetBytes($x)))-replace'-','').ToLowerInvariant()"
            "}finally{$a.Dispose()}}; "
            "$st=$null; try {$st=Get-Content 'C:\\ProgramData\\AurOS\\user\\status.json' -Raw|ConvertFrom-Json}catch{}; "
            "$cpart=Get-Partition -DriveLetter C; "
            "$pm=((Get-Partition|Sort-Object DiskNumber,Offset|ForEach-Object{"
            "'{0}|{1}|{2}|{3}|{4}'-f $_.DiskNumber,$_.PartitionNumber,$_.Offset,$_.Size,$_.GptType})-join';'); "
            "$fwRaw=& bcdedit.exe /enum firmware 2>&1; if($LASTEXITCODE -ne 0){throw 'bcdedit failed'}; "
            "$fw=(($fwRaw|Out-String).Trim()); "
            "$h=(Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power').HiberbootEnabled; "
            "$can='C:\\Users\\aurtest\\Documents\\auros-e2e-before.txt'; "
            f"$rest='{restored}'; "
            "$tasks=@(Get-ScheduledTask -ErrorAction SilentlyContinue|Where-Object {$_.TaskName -like 'AurOS put-back*'}); "
            "$vols=@(Get-Volume -ErrorAction SilentlyContinue|Where-Object {$_.FileSystemLabel -eq 'AUROSHANDOV'}); "
            "$s=@{phase=$(if($st){[string]$st.phase}else{''});status_at=$(if($st){[string]$st.at}else{''});"
            "c_bytes=[uint64]$cpart.Size;c_offset=[uint64]$cpart.Offset;partition_sha256=(H $pm);firmware_sha256=(H $fw);hiberboot=[int]$h;"
            "canary_sha256=$(if(Test-Path -LiteralPath $can){(Get-FileHash -LiteralPath $can -Algorithm SHA256).Hash.ToLowerInvariant()}else{''});"
            "restored_sha256=$(if(Test-Path -LiteralPath $rest){(Get-FileHash -LiteralPath $rest -Algorithm SHA256).Hash.ToLowerInvariant()}else{''});"
            "handover_volumes=[int]$vols.Count;putback_tasks=[int]$tasks.Count}; "
            "'AUROS_PUTBACK_FINAL '+($s|ConvertTo-Json -Compress)")
        rows = [x[len("AUROS_PUTBACK_FINAL "):] for x in out.splitlines()
                if x.startswith("AUROS_PUTBACK_FINAL ")]
        try:
            state = json.loads(rows[0]) if len(rows) == 1 else None
        except ValueError:
            state = None
        if isinstance(state, dict):
            with open(os.path.join(self.a.out, "putback-final-observation.json"), "w") as fh:
                json.dump(state, fh, indent=2, sort_keys=True)
                fh.write("\n")
        errors = putback_completion_errors(
            self.putback_base, self.putback_fixture, state, self.putback_requested_at)
        if errors:
            raise Failure("put-back did not finish safely: " + "; ".join(errors))
        with open(os.path.join(self.a.out, "putback-finished.json"), "w") as fh:
            json.dump(state, fh, indent=2, sort_keys=True)
            fh.write("\n")
        return state

    def complete_putback(self):
        """Run the destructive leg only in this run's disposable overlay."""
        self.shutdown()
        self.seed_putback_fixture()
        since = self.request_putback()
        # The restore and Windows tasks are asynchronous. A helper-up event is
        # only the beginning; final state below is polled to a bounded deadline.
        second = self.wait_windows_return(since, 2, "after Put Windows back removed AurOS", 20 * 60)
        deadline = time.time() + 30 * 60
        last = None
        while time.time() < deadline:
            try:
                self.putback_final_state()
                break
            except Failure as e:
                last = e
                time.sleep(20)
        else:
            raise Failure(f"put-back did not reach verified completion in 30 minutes: {last}")
        first_path = os.path.join(self.a.out, "bitlocker-return-1.json")
        if not os.path.isfile(first_path):
            raise Failure("the first Windows return has no protection evidence")
        with open(first_path) as fh:
            first = json.load(fh)
        with open(os.path.join(self.a.out, "bitlocker-returns.json"), "w") as fh:
            json.dump([first, second], fh, indent=2, sort_keys=True)
            fh.write("\n")

    def finish_findings(self):
        """The journey got through; what a person would be hurt by on the way
        (a label she cannot read) still fails the run, with its words."""
        if not self.findings:
            return 0
        msg = "the journey went through, but: " + "; ".join(self.findings)
        self.log("FAILED: " + msg)
        with open(os.path.join(self.a.out, "FAILED.txt"), "w") as fh:
            fh.write(msg + "\n")
        return 1

    def shutdown(self):
        with self.rec.phase("aurOS: shut down"):
            self.m.stop("powerdown", 300)


class AurFlow(Flow):
    """AurOS alone: boot the release's image (no Windows, no installer) and do
    the AurOS half of the journey with the same hands: answer "does it work?"
    if it is asked, open "Get more programs", install a program, open it.
    Minutes a try, for fixing the store's steps without a Windows each time."""

    def __init__(self, a):
        self.a = a
        self.rec = Recorder(a.out)
        self.hub = Hub(self.rec)
        os.makedirs(a.work, exist_ok=True)
        # (named win.qcow2 so that `inspect` reads it like any other run's disk)
        self.disk = os.path.join(a.work, "win.qcow2")
        self.vars = os.path.join(a.work, "vars.fd")
        self.tpm = os.path.join(a.work, "tpm")
        os.makedirs(self.tpm, exist_ok=True)
        # larger than the image: its first start grows the root file system
        run(["qemu-img", "create", "-f", "qcow2", "-F", "raw", "-b", os.path.abspath(a.image),
             self.disk, f"{a.grow_gb}G"], self.rec)
        shutil.copyfile(OVMF_VARS, self.vars)
        self.m = Machine(self.rec, a.work, self.disk, self.vars, self.tpm)
        self.wizard_pages, self.fix_presses, self.fix_restarts = [], 0, 0
        self.findings = []
        self._named_diag = False

    def first_screen(self):
        with self.rec.phase("aurOS: start, to its first screen"):
            self.m.start()
            self.m.auto_shots(10)
            p, _, words = self.wait_text(["Yes, it all works", STORE_NAME, "What would you like to do"],
                                         min(left(), 20 * 60), every=10)
            if not p:
                raise Failure("AurOS showed none of its first screens within 20 minutes")
            self.log(f"first AurOS screen: {p}")
            self.m.shot("aur-first-screen")
            return p


def cmd_aur(a):
    f = AurFlow(a)
    rc = 1
    try:
        first = f.first_screen()
        if first == "Yes, it all works":
            f.answer_yes()
        f.store()
        rc = f.finish_findings()
    except Failure as e:
        f.log(f"FAILED: {e}")
        with open(os.path.join(a.out, "FAILED.txt"), "w") as fh:
            fh.write(str(e) + "\n")
        if f.m.alive():
            f.m.shot("failure")
    finally:
        try:
            if f.m.alive():
                f.shutdown()
        except Failure as e:
            f.log(f"shutdown: {e}")
        f.m.stop("quit", 5)
        f.rec.write_timings()
    return rc


def cmd_flow(a):
    lane_error = bitlocker_pause_lane_error(
        VARIANT, BITLOCKER_PUTBACK, INSTALLER_NAME, INSTALLER_SHA256)
    if lane_error:
        raise Failure(lane_error)
    f = Flow(a)
    rc = 1
    try:
        f.boot_windows()
        f.open_website()
        f.click_download()
        f.keep_download()
        f.open_installer()
        f.drive_wizard()
        f.check_variant()
        f.watch_install()
        f.answer_yes()
        store_fail = None
        if E("E2E_STORE", "1") == "1":
            try:
                f.store()
            except Failure as e:
                store_fail = e          # the way back to Windows is checked all the same
        if VARIANT == "bitlocker":
            f.shutdown()
            try:
                f.back_to_windows()
                if BITLOCKER_PUTBACK:
                    f.complete_putback()
            except Failure as e:
                if store_fail is None:
                    raise
                f.log(f"FAILED (also): {e}")
        if store_fail is not None:
            raise store_fail
        rc = f.finish_findings()
    except Failure as e:
        f.log(f"FAILED: {e}")
        with open(os.path.join(a.out, "FAILED.txt"), "w") as fh:
            fh.write(str(e) + "\n")
        if f.m.alive():
            f.m.shot("failure")
            if f.hub.helper_alive():
                f.guest("Get-ChildItem C:\\AurOS -ErrorAction SilentlyContinue | Format-Table Name,Length; "
                        "Get-Process | Sort-Object CPU -Descending | Select-Object -First 15 Name,Id,CPU | Format-Table")
    finally:
        try:
            if f.m.alive():
                f.shutdown()
        except Failure as e:
            f.log(f"shutdown: {e}")
        f.m.stop("quit", 5)
        f.rec.write_timings()
    return rc


# ════════════════════════════════════════════════════════════════════
#  After the run: what is on the disk
# ════════════════════════════════════════════════════════════════════

# Units that fail on a bare boot of the image, in the normal part of a session (before
# the power button): none today (the aur variant's journal, run 40052634313).
BASELINE_FAILED_UNITS = frozenset()


FIRST_SESSION_FACTS = (
    ("AppArmor enabled with a loaded profile", "apparmor", "enabled"),
    ("default route present", "route", "default"),
    ("DNS resolves a public name", "dns", "ok"),
    ("/run mode is 0755", "run_mode", "0755"),
    ("staging saved.bin released", "saved_bin", "absent"),
    ("installed root filesystem is writable", "root_mode", "rw"),
    ("installed root filesystem is ext4", "root_fs", "ext4"),
    ("no staging mount remains under /run/aurstage", "aurstage_mount", "absent"),
    ("no aurscreen process remains", "aurscreen", "absent"),
)

FIRST_SESSION_PROBE = re.compile(
    r"apparmor=(?P<apparmor>enabled|disabled) "
    r"profiles=(?P<profiles>[0-9]{1,6}) "
    r"route=(?P<route>default|missing) "
    r"dns=(?P<dns>ok|failed) "
    r"run_mode=(?P<run_mode>[0-7]{4}|unknown) "
    r"saved_bin=(?P<saved_bin>absent|present) "
    r"root_mode=(?P<root_mode>rw|ro|unknown) "
    r"root_fs=(?P<root_fs>ext4|other|unknown) "
    r"aurstage_mount=(?P<aurstage_mount>absent|present|unknown) "
    r"aurscreen=(?P<aurscreen>absent|present|unknown)"
)

PERSONALIZED_THEME_LABELS = {
    "ember": "Ember",
    "moss": "Moss",
    "nocturne": "Light writing on a dark background",
    "sandstone": "Light",
    "slate": "Slate",
}


def first_session_probe_verdicts(lines):
    """Parse one root-observed state record from the installed session.

    The Linux guest deliberately has no test command channel: no guest agent,
    no serial login, and its live disk must not be mounted by the host.  A
    bounded asynchronous service in the image therefore records these facts in
    the same persistent journal `inspect` already reads.  Its exact message is:

      auros-first-session-probe: v1 apparmor=enabled profiles=N route=default
        dns=ok run_mode=0755 saved_bin=absent root_mode=rw root_fs=ext4
        aurstage_mount=absent aurscreen=absent

    N must be positive. Missing, duplicate, or malformed evidence is red; the
    harness must never infer ephemeral /run state after the machine is off.
    """
    records = [l for l in lines if re.search(r"auros-first-session-probe(?:\[[0-9]+\])?: v1\b", l)]
    detail = "the first-session probe record is missing"
    values = {}
    valid = False
    if len(records) == 1:
        tail = records[0].split(": v1", 1)[-1].strip()
        match = FIRST_SESSION_PROBE.fullmatch(tail)
        if match:
            values = match.groupdict()
            detail = tail
            valid = True
        else:
            shown = tail[:240] + ("..." if len(tail) > 240 else "")
            detail = "malformed probe record: " + shown
    elif len(records) > 1:
        detail = f"{len(records)} probe records found; expected exactly one"

    out = []
    profiles = values.get("profiles", "")
    profile_ok = valid and int(profiles) > 0
    for name, key, want in FIRST_SESSION_FACTS:
        ok = valid and values.get(key) == want
        if key == "apparmor":
            ok = ok and profile_ok
        out.append((name, ok, detail if not ok else
                    (f"enabled, {profiles} profile(s) loaded" if key == "apparmor" else f"{key}={want}")))
    return out


def first_session_lines(text):
    """The first boot in a persistent journal, before shutdown begins."""
    lines = text.splitlines()
    starts = [i for i, line in enumerate(lines) if "kernel: Command line:" in line]
    seg = lines[starts[0]:(starts[1] if len(starts) > 1 else len(lines))] if starts else lines
    end = next((i for i, line in enumerate(seg)
                if re.search(r"Power key pressed|System is powering down", line)), len(seg))
    return seg[:end]


def personalized_verdicts(expected, receipt, theme, shell_target,
                          firstboot_stamp, firstboot_log, journal):
    """Verdicts proving a personalized installer recipe reached the live OS.

    This consumes only evidence read from a powered-off installed disk.  It
    does not infer success from the requested recipe or from build metadata.
    """
    recipe_id, look, want_theme = expected

    def one_value(key):
        values = [line[len(key) + 1:] for line in (receipt or "").splitlines()
                  if line.startswith(key + "=")]
        if len(values) != 1:
            return None, ("missing" if not values else
                          f"recorded {len(values)} times; expected exactly once")
        return values[0], values[0]

    out = []
    for name, key, want in (("saved recipe id", "recipe_id", recipe_id),
                            ("saved look", "shell", look),
                            ("saved theme", "theme", want_theme)):
        got, detail = one_value(key)
        out.append((name, got == want,
                    f"{key}={got}" if got == want else
                    f"expected {key}={want}; installed receipt: {detail}"))

    out.append(("applied theme", theme == want_theme + "\n",
                f"/etc/auros/theme={want_theme}" if theme == want_theme + "\n" else
                f"expected {want_theme!r}; got {(theme if theme is not None else 'missing')!r}"))
    want_shell = f"/usr/share/auros/shells/{look}.shell"
    out.append(("applied look", shell_target == want_shell,
                f"active.shell -> {want_shell}" if shell_target == want_shell else
                f"expected {want_shell}; got {shell_target or 'missing'}"))
    out.append(("first boot completion stamp", firstboot_stamp,
                "/var/lib/auros/firstboot.done exists" if firstboot_stamp else "stamp is missing"))
    completed = "=== first boot complete ===" in (firstboot_log or "").splitlines()
    out.append(("first boot completed", completed,
                "completion line is in /var/log/auros-firstboot.log" if completed else
                "completion line is missing from /var/log/auros-firstboot.log"))
    applied_line = f"choices: personalized recipe applied: {recipe_id}"
    logged = applied_line in (firstboot_log or "").splitlines()
    out.append(("personalized recipe applied at first boot", logged,
                applied_line if logged else "personalized success line is missing from the first-boot log"))

    first = first_session_lines(journal or "")
    runtime_line = (f"personalized recipe applied: {recipe_id}; "
                    f"theme={want_theme}; shell={look}")
    journal_recipe = any(line.endswith(runtime_line) for line in first)
    out.append(("first-session journal has applied recipe and theme", journal_recipe,
                runtime_line if journal_recipe else
                "exact personalized runtime record is missing from the first session"))
    journal_look = any(re.search(rf"aurshell: archetype '{re.escape(look)}' \(", line)
                       for line in first)
    out.append(("live shell uses chosen look", journal_look,
                f"aurshell: archetype '{look}'" if journal_look else
                f"no live archetype '{look}' in the first session"))
    theme_label = PERSONALIZED_THEME_LABELS.get(want_theme)
    live_theme = (theme_label is not None and
                  any(line.endswith(f"aurshell: theme '{theme_label}'") for line in first))
    out.append(("live shell loaded chosen theme", live_theme,
                f"aurshell: theme '{theme_label}'" if live_theme else
                (f"no theme label mapping for {want_theme}" if theme_label is None else
                 f"no live theme '{theme_label}' in the first session")))
    return out


def generic_default_verdicts(expected, receipt, theme, shell_target,
                             firstboot_stamp, firstboot_log, journal):
    """Verdicts proving the generic installer's shipped defaults reached the live OS."""
    look, want_theme = expected

    def one_value(key):
        values = [line[len(key) + 1:] for line in (receipt or "").splitlines()
                  if line.startswith(key + "=")]
        if len(values) != 1:
            return None, ("missing" if not values else
                          f"recorded {len(values)} times; expected exactly once")
        return values[0], values[0]

    out = []
    recipe_values = [line[len("recipe_id="):] for line in (receipt or "").splitlines()
                     if line.startswith("recipe_id=")]
    generic_receipt = receipt is not None and not recipe_values
    out.append(("no personalized recipe id", generic_receipt,
                "installed receipt has no recipe_id" if generic_receipt else
                ("installed receipt is missing" if receipt is None else
                 f"installed receipt has {len(recipe_values)} recipe_id value(s)")))
    for name, key, want in (("saved default look", "shell", look),
                            ("saved default theme", "theme", want_theme)):
        got, detail = one_value(key)
        out.append((name, got == want,
                    f"{key}={got}" if got == want else
                    f"expected {key}={want}; installed receipt: {detail}"))

    out.append(("applied default theme", theme == want_theme + "\n",
                f"/etc/auros/theme={want_theme}" if theme == want_theme + "\n" else
                f"expected {want_theme!r}; got {(theme if theme is not None else 'missing')!r}"))
    want_shell = f"/usr/share/auros/shells/{look}.shell"
    out.append(("applied default look", shell_target == want_shell,
                f"active.shell -> {want_shell}" if shell_target == want_shell else
                f"expected {want_shell}; got {shell_target or 'missing'}"))
    out.append(("first boot completion stamp", firstboot_stamp,
                "/var/lib/auros/firstboot.done exists" if firstboot_stamp else "stamp is missing"))
    completed = "=== first boot complete ===" in (firstboot_log or "").splitlines()
    out.append(("first boot completed", completed,
                "completion line is in /var/log/auros-firstboot.log" if completed else
                "completion line is missing from /var/log/auros-firstboot.log"))
    rendered_line = f"firstboot: theme applied: {want_theme}"
    rendered = (firstboot_log or "").splitlines().count(rendered_line) == 1
    out.append(("default theme render completed", rendered,
                rendered_line if rendered else
                f"expected exactly one {rendered_line!r} success marker"))

    first = first_session_lines(journal or "")
    journal_look = any(re.search(rf"aurshell: archetype '{re.escape(look)}' \(", line)
                       for line in first)
    out.append(("live shell uses default look", journal_look,
                f"aurshell: archetype '{look}'" if journal_look else
                f"no live archetype '{look}' in the first session"))
    theme_label = PERSONALIZED_THEME_LABELS.get(want_theme)
    live_theme = (theme_label is not None and
                  any(line.endswith(f"aurshell: theme '{theme_label}'") for line in first))
    out.append(("live shell loaded default theme", live_theme,
                f"aurshell: theme '{theme_label}'" if live_theme else
                (f"no theme label mapping for {want_theme}" if theme_label is None else
                 f"no live theme '{theme_label}' in the first session")))
    return out


def journal_verdicts(text):
    """Gating verdicts from AurOS's own journal of its FIRST session (the first
    boot in it: the one the installer handed over to, on a Windows variant; the
    only one on the bare image), up to the power button. [(name, ok, detail)].
    Written after run 40050260949: on a real install NetworkManager 'assumed' the NIC
    the installer left configured, never ran DHCP, and Flathub's catalogue, the
    clock and so the store never came: the journal said all of it, nothing read it."""
    win = first_session_lines(text)

    def ts(l):
        import datetime
        try:
            return datetime.datetime.fromisoformat(l.split(" ", 1)[0]).timestamp()
        except ValueError:
            return None

    def first(rx):
        return next((l for l in win if re.search(rx, l)), None)

    out = []
    if not win or not any("systemd" in l for l in win):
        return [("journal readable", False, "AurOS's journal has no first session in it (not persistent, or unreadable)")]
    out.extend(first_session_probe_verdicts(win))
    desk = first(r"Started aurshell\.service")
    net = first(r"NetworkManager state is now CONNECTED_GLOBAL") or first(r"dhcp4 \(\S+\): state changed new lease")
    if not net:
        st = [l.split("manager: ", 1)[-1] for l in win if "NetworkManager state is now" in l]
        out.append(("network within 60 s of the desktop", False,
                    "no DHCP lease and never CONNECTED_GLOBAL in the whole session; NetworkManager said: " +
                    (st[-1] if st else "nothing")))
    else:
        td, tn = (ts(desk) if desk else None), ts(net)
        late = td is not None and tn is not None and tn - td > 60
        out.append(("network within 60 s of the desktop", not late,
                    (f"online {int(tn - td)} s after the desktop" if td and tn else "online") + ": " + net.split(" ", 2)[-1][:120]))
    cat = first(r"software-ready: Flathub's catalogue fetched")
    out.append(("Flathub's catalogue fetched", bool(cat),
                cat.split("software-ready: ")[-1] if cat else "never; software-ready said: " +
                ((first(r"software-ready: Flathub's catalogue: not yet") or "nothing").split("software-ready: ")[-1])))
    chr_ = first(r"chronyd\[\d+\]: Selected source")
    out.append(("clock synced (chrony)", bool(chr_), chr_.split("chronyd")[-1][:100] if chr_ else "chrony never selected a time source"))
    failed = sorted({m.group(1) for l in win for m in [re.search(r"([\w@:.\\-]+\.(?:service|socket|mount|timer|path))(?:: Failed with result|: Failed to start| entered failed state)", l)] if m}
                    - BASELINE_FAILED_UNITS)
    out.append(("no unit failed that does not fail on the bare boot", not failed,
                "none" if not failed else "failed: " + ", ".join(failed)))
    return out


def aur_root_report(rec, mnt, out_dir, expected_personalization=None,
                    expected_defaults=None):
    """What AurOS's own disk says, read with the machine off (every variant, pass
    or fail): its logs, the software catalogue (what the store has to show),
    stamps of the store's first-start job, and the journal of every boot. The
    whole journal goes to aur-journal.txt (packagekit's one-line-per-package
    assertion spam left out); the summary here is for a reader in a hurry."""
    lines = []
    sh = lambda cmd: run(["sudo", "sh", "-c", f"cd {mnt} && {cmd}"], rec, check=False).stdout
    lines.append("--- AurOS's logs\n" + sh(
        "ls -la var/log 2>&1 | head -40; "
        "for f in var/log/auros-answer.log var/log/auros-firstboot.log; do echo \"== $f\"; head -c 3000 \"$f\" 2>&1; echo; done"))
    lines.append("--- the software catalogue (what the store shows), with times\n" + sh(
        "find var/lib/flatpak/appstream var/lib/flatpak/repo -maxdepth 3 -printf '%TY-%Tm-%Td %TH:%TM:%TS %10s %p\\n' 2>&1 | head -60; "
        "echo; du -sh var/lib/flatpak var/lib/flatpak/appstream 2>&1; "
        "echo; ls -la var/lib/flatpak/app 2>&1 | head; ls -la usr/share/swcatalog usr/share/app-info var/lib/swcatalog 2>&1 | head -20"))
    lines.append("--- AurOS's own state (software-ready and friends)\n" + sh(
        "ls -laR var/lib/auros etc/auros 2>&1 | head -60; "
        "for f in var/lib/auros/software-ready*; do echo \"== $f\"; head -c 2000 \"$f\" 2>&1; echo; done; "
        "ls -la etc/systemd/system/*.wants 2>&1 | grep -i 'auros\\|software' | head"))
    jd = f"{mnt}/var/log/journal"
    full = run(["sudo", "sh", "-c",
                f"journalctl -D {jd} --no-pager -o short-iso --list-boots 2>&1 | head -20; echo '-----'; "
                f"journalctl -D {jd} --no-pager -o short-iso 2>&1 | grep -v set_update_severity | head -c 6000000"],
               rec, check=False).stdout
    try:
        with open(os.path.join(out_dir, "aur-journal.txt"), "w") as fh:
            fh.write(full)
    except OSError:
        pass
    key = re.compile(r"gnome-software|flatpak|packagekit|auros-software|software-ready|NetworkManager|chrony|timesyncd|"
                     r"aurwl|aurshell|wayland|xdg-desktop-portal|dbus-run-session", re.I)
    pick = [l for l in full.splitlines() if key.search(l)]
    warn = [l for l in full.splitlines() if re.search(r"\b(warning|error|critical|failed|denied|fatal)\b", l, re.I)]
    journal = full.split("-----", 1)[-1]
    v = journal_verdicts(journal)
    if expected_personalization or expected_defaults:
        def root_file(rel):
            result = run(["sudo", "head", "-c", "65536", os.path.join(mnt, rel)], rec, check=False)
            return result.stdout if result.returncode == 0 else None

        link = run(["sudo", "readlink", os.path.join(mnt, "etc/auros/shell/active.shell")],
                   rec, check=False)
        stamp = run(["sudo", "test", "-f", os.path.join(mnt, "var/lib/auros/firstboot.done")],
                    rec, check=False).returncode == 0
        evidence = (root_file("etc/auros/installer-choices.conf"),
                    root_file("etc/auros/theme"),
                    link.stdout.strip() if link.returncode == 0 else None,
                    stamp,
                    root_file("var/log/auros-firstboot.log"),
                    journal)
        if expected_personalization:
            pv = personalized_verdicts(expected_personalization, *evidence)
            heading = "The requested recipe on the installed, powered-off system:\n"
            verdict_file = "personalized-verdicts.txt"
        else:
            pv = generic_default_verdicts(expected_defaults, *evidence)
            heading = "The shipped defaults on the installed, powered-off system:\n"
            verdict_file = "default-choice-verdicts.txt"
        v.extend(pv)
        ptext = "\n".join(f"{'ok  ' if ok else 'RED '} {name}: {detail}"
                           for name, ok, detail in pv)
        try:
            with open(os.path.join(out_dir, verdict_file), "w") as fh:
                fh.write(heading + ptext + "\n")
        except OSError:
            pass
    vtext = "\n".join(f"{'ok  ' if ok else 'RED '} {name}: {detail}" for name, ok, detail in v)
    try:
        with open(os.path.join(out_dir, "journal-verdicts.txt"), "w") as fh:
            fh.write("The first AurOS session after the install, from its own journal:\n" + vtext + "\n")
    except OSError:
        pass
    rec.journal_red = [f"{name}: {detail}" for name, ok, detail in v if not ok]
    lines.append("--- verdicts from the first session's journal\n" + vtext)
    lines.append(f"--- the journal ({len(full.splitlines())} lines, all boots; aur-journal.txt has all)\n" +
                 full.split("-----")[0].strip())
    lines.append("--- about the store and its neighbours (last 250)\n" + "\n".join(l[:260] for l in pick[-250:]))
    lines.append("--- every warning and error of the journal (last 250)\n" + "\n".join(l[:260] for l in warn[-250:]))
    return "\n".join(lines)


def diagnose_partition(rec, p, fs, mnt):
    """After a flow that failed with the machine off: what the staging
    installer saw, said again by the same tools, read-only. The staging
    screen gives only a verdict word (run 39931612628: 'cannot-measure');
    its reason is ntfsresize's own words, and the staging's journal and
    report are small files on the ESP and in C:\\AurOS."""
    lines = [f"--- {p} ({fs or 'no filesystem'})"]
    dev = "/dev/" + p
    if fs == "ntfs":
        for argv in (["ntfsresize", "--info", "--no-action", dev], ["ntfsfix", "--no-action", dev]):
            r = run(["sudo"] + argv, rec, check=False)
            lines.append("$ " + " ".join(argv) + f"   (exit {r.returncode})\n" + (r.stdout or "") + (r.stderr or ""))
        # NTFS read-only; an unclean or hibernated volume may refuse: say so
        r = run(["sudo", "mount", "-t", "ntfs3", "-o", "ro", dev, mnt], rec, check=False)
        if r.returncode != 0:
            lines.append("mount (ntfs3, ro) failed: " + (r.stderr or "").strip())
            r = run(["sudo", "ntfs-3g", "-o", "ro,norecover", dev, mnt], rec, check=False)
            if r.returncode != 0:
                lines.append("ntfs-3g (ro, norecover) failed: " + (r.stderr or "").strip())
        if r.returncode == 0:
            try:
                lines.append(run(["sudo", "sh", "-c", f"ls -la {mnt} | head -40; echo; ls -laR {mnt}/AurOS 2>&1 | head -60; "
                                  f"echo; for f in {mnt}/AurOS/*.txt {mnt}/AurOS/*.log {mnt}/AurOS/*.json; do "
                                  "[ -f \"$f\" ] && { echo \"== $f\"; head -c 8000 \"$f\"; echo; }; done 2>&1"],
                                 rec, check=False).stdout)
            finally:
                run(["sudo", "umount", mnt], rec, check=False)
    elif fs == "vfat":
        r = run(["sudo", "mount", "-o", "ro", dev, mnt], rec, check=False)
        if r.returncode == 0:
            try:
                lines.append(run(["sudo", "sh", "-c", f"cd {mnt} && find . -maxdepth 4 | head -80; echo; "
                                  "for f in $(find EFI/AurOS aurbridge* -type f -size -64k 2>/dev/null | head -30); do "
                                  "case $f in *.efi|*.EFI|*.bin|*.img|*.gz) continue;; esac; "
                                  "echo \"== $f\"; head -c 8000 \"$f\"; echo; done"], rec, check=False).stdout)
            finally:
                run(["sudo", "umount", mnt], rec, check=False)
    return "\n".join(lines)


def cmd_inspect(a):
    """Read AurOS's root file system with the machine off, read-only.
    Proves the program is on the disk, not just on the screen."""
    rec = Recorder(a.out)
    disk = os.path.join(a.work, "win.qcow2")
    out, found = [], {}
    attached = []
    with rec.phase("inspect: the disk, machine off"):
        dev = "/dev/nbd0"
        run("sudo modprobe nbd max_part=16", rec, check=False)
        if run(["sudo", "qemu-nbd", "--read-only", "--connect", dev, disk], rec,
               check=False).returncode == 0:
            attached.append(("nbd", dev))
            time.sleep(3)
            run(["sudo", "partprobe", dev], rec, check=False)
        else:
            # no nbd in this kernel: a sparse raw copy on a loop device
            raw = os.path.join(a.work, "disk.raw")
            run(["qemu-img", "convert", "-O", "raw", disk, raw], rec)
            dev = run(["sudo", "losetup", "--find", "--show", "--read-only", "-P", raw], rec).stdout.strip()
            attached.append(("loop", dev))
        try:
            out.append(run(["sudo", "sgdisk", "-p", dev], rec, check=False).stdout)
            out.append(run(["lsblk", "-o", "NAME,SIZE,FSTYPE,LABEL,PARTLABEL", dev], rec, check=False).stdout)
            mnt = os.path.join(a.work, "mnt")
            os.makedirs(mnt, exist_ok=True)
            base = os.path.basename(dev)
            parts = sorted(p for p in os.listdir("/dev") if p.startswith(base + "p"))
            for p in parts:
                fs = run(["sudo", "blkid", "-o", "value", "-s", "TYPE", "/dev/" + p], rec,
                         check=False).stdout.strip()
                if a.diag:
                    out.append(diagnose_partition(rec, p, fs, mnt))
                if fs not in ("ext4", "btrfs", "xfs"):
                    continue
                if run(["sudo", "mount", "-o", "ro,noload" if fs == "ext4" else "ro", "/dev/" + p, mnt],
                       rec, check=False).returncode != 0:
                    continue
                try:
                    out.append(f"{p} ({fs}): " + run(["sudo", "ls", mnt], rec, check=False).stdout.replace("\n", " "))
                    for rel in APP_FILES.split():
                        r = run(["sudo", "ls", "-ld", os.path.join(mnt, rel)], rec, check=False)
                        if r.returncode == 0:
                            found[rel] = f"{p}: {r.stdout.strip()}"
                    out.append(run(["sudo", "sh", "-c",
                                    f"tail -n 30 {mnt}/var/log/apt/history.log 2>/dev/null; "
                                    f"ls -l {mnt}/var/lib/flatpak/app 2>/dev/null; "
                                    f"cat {mnt}/etc/os-release 2>/dev/null | head -3"],
                                   rec, check=False).stdout)
                    if fs == "ext4" and os.path.exists(os.path.join(mnt, "usr/lib/auros")):
                        personalized = ((a.expect_recipe_id, a.expect_look, a.expect_theme)
                                        if a.expect_recipe_id else None)
                        defaults = ((a.expect_look, a.expect_theme)
                                    if a.expect_look and not a.expect_recipe_id else None)
                        out.append(aur_root_report(rec, mnt, a.out, personalized, defaults))
                finally:
                    run(["sudo", "umount", mnt], rec, check=False)
            out.append("found: " + json.dumps(found, indent=1))
            with open(os.path.join(a.out, "disk-inspection.txt"), "w") as fh:
                fh.write("\n".join(out))
            red = getattr(rec, "journal_red", [])
            if red:
                rec.log("JOURNAL VERDICTS RED: " + " | ".join(red))
            if a.diag:         # after a failed flow: what the disk says, not a verdict
                return 0
            if red:
                raise Failure("AurOS's first session after the install: " + " | ".join(red))
            if not found:
                raise Failure(f"none of {APP_FILES} is on any Linux partition")
        finally:
            for kind, d in attached:
                if kind == "nbd":
                    run(["sudo", "qemu-nbd", "--disconnect", d], rec, check=False)
                else:
                    run(["sudo", "losetup", "-d", d], rec, check=False)
            rec.write_timings()
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    w = sub.add_parser("windows")
    w.add_argument("out"); w.add_argument("vm")
    w.add_argument("--iso", required=True); w.add_argument("--answer", required=True)
    fl = sub.add_parser("flow")
    fl.add_argument("out"); fl.add_argument("golden"); fl.add_argument("work")
    au = sub.add_parser("aur")
    au.add_argument("out"); au.add_argument("image"); au.add_argument("work")
    au.add_argument("--grow-gb", type=int, default=16)
    i = sub.add_parser("inspect")
    i.add_argument("out"); i.add_argument("work")
    i.add_argument("--diag", action="store_true", help="after a failed flow: say what the disk holds, no verdict")
    i.add_argument("--expect-recipe-id", help="require this personalized sha256 recipe on the installed system")
    i.add_argument("--expect-look", help="require this shell archetype on the installed system")
    i.add_argument("--expect-theme", help="require this theme on the installed system")
    a = p.parse_args()
    if a.cmd == "inspect":
        choices = (a.expect_look, a.expect_theme)
        if any(choices) and not all(choices):
            p.error("--expect-look and --expect-theme must be given together")
        if a.expect_recipe_id and not all(choices):
            p.error("--expect-recipe-id requires --expect-look and --expect-theme")
        if a.diag and (a.expect_recipe_id or any(choices)):
            p.error("installed-choice expectations cannot be combined with --diag, which does not return a verdict")
        if a.expect_recipe_id and not re.fullmatch(r"sha256:[0-9a-f]{64}", a.expect_recipe_id):
            p.error("--expect-recipe-id must be sha256: followed by 64 lowercase hexadecimal digits")
        for flag, value in (("--expect-look", a.expect_look), ("--expect-theme", a.expect_theme)):
            if value and not re.fullmatch(r"[a-z0-9-]{1,32}", value):
                p.error(f"{flag} must be a lowercase id (letters, digits and hyphens; 1-32 characters)")
    os.makedirs(a.out, exist_ok=True)
    return {"windows": cmd_windows, "flow": cmd_flow, "aur": cmd_aur, "inspect": cmd_inspect}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
