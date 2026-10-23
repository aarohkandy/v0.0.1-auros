#!/usr/bin/env python3
"""gen-compat.py -- hw/compat.json from the image, plus hw/seeds.json.

  python3 -I tools/hw/gen-compat.py ROOTFS [-o hw/compat.json] [--check]

ROOTFS is a tree with the image's packages installed: the forge rootfs
(work/forge/desktop/rootfs on CI), or "/" inside a container that has
installed packages_hardware from profiles/desktop.profile. What it reads:

  lib/modules/KVER/modules.{dep,alias}   which PCI/USB ids have a driver
  modinfo -F firmware                    what each driver wants to load
  usr/lib/firmware                       what the image actually has
  var/lib/dpkg/status                    which optional software is there
  usr/share/cups/drv/hpcups.drv          \\ the printers CUPS can drive
  usr/share/gutenprint/*/xml/printers    /  without being told (models)

Nothing here is a guess about a chip: an id is "supported" only if the
image's own kernel has a module that claims it, and "works" only if the
firmware that module asks for is in the image too. The hand-written
exceptions are hw/seeds.json. Output is deterministic (sorted, no dates),
so a diff of compat.json is a diff of what the image can drive.

--check: regenerate in memory and say what changed against the committed
file; exit 0 either way unless HW_STRICT=1 (a kernel update in the
archive is not a reason to stop a release; a human reads the summary).
"""
import argparse, fnmatch, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# module path prefix (below kernel/) -> what kind of hardware it drives
CATS = [
    ("drivers/net/wireless/", "wifi"),
    ("drivers/staging/rtl", "wifi"), ("drivers/staging/r8", "wifi"),
    ("drivers/net/ethernet/", "ethernet"), ("drivers/net/usb/", "ethernet"),
    ("drivers/bluetooth/", "bluetooth"),
    # all of sound/: the 7.0 kernel moved snd-hda-intel from sound/pci/ to
    # sound/hda/controllers/, and a prefix list would have silently dropped
    # every Intel sound controller
    ("sound/", "audio"),
    ("drivers/media/usb/uvc/", "camera"), ("drivers/media/pci/intel/", "camera"),
    ("drivers/media/usb/gspca/", "camera"), ("drivers/staging/media/ipu", "camera"),
    ("drivers/gpu/drm/", "gpu"),
    ("drivers/bcma/", "bus"), ("drivers/ssb/", "bus"),
]
COMP = (".ko.zst", ".ko.xz", ".ko.gz", ".ko")
FWCOMP = (".zst", ".xz", ".gz")

PCI = re.compile(r"^alias pci:v([0-9A-F]{8}|\*)d([0-9A-F]{8}|\*)sv([0-9A-F]{8}|\*)sd([0-9A-F]{8}|\*)"
                 r"bc([0-9A-F]{2}|\*)sc([0-9A-F]{2}|\*)i([0-9A-F]{2}|\*)\*?\s+(\S+)$")
# A USB module can match on the DEVICE's class (dc/dsc/dp: Bluetooth radios
# are E0/01/01 here) or on an INTERFACE's (ic/isc/ip: webcams are 0E/01).
USB = re.compile(r"^alias usb:v([0-9A-F]{4}|\*)p([0-9A-F]{4}|\*)d[0-9A-F*]*"
                 r"dc([0-9A-F]{2}|\*)dsc([0-9A-F]{2}|\*)dp([0-9A-F]{2}|\*)"
                 r"ic([0-9A-F]{2}|\*)isc([0-9A-F]{2}|\*)ip([0-9A-F]{2}|\*)in[0-9A-F*]*\s+(\S+)$")


def die(msg):
    sys.exit("gen-compat: " + msg)


def modname(path):
    b = os.path.basename(path)
    for c in COMP:
        if b.endswith(c):
            return b[: -len(c)].replace("-", "_")
    return b


def category(path):
    p = path[len("kernel/"):] if path.startswith("kernel/") else path
    for pre, cat in CATS:
        if p.startswith(pre):
            return cat
    return None


def ids_pci(m):
    v, d, sv, sd, bc, sc, i = m
    h = lambda x: "*" if x == "*" else x[-4:].lower()
    s = h(v) + ":" + h(d)
    if sv != "*" or sd != "*":
        s += "/" + h(sv) + ":" + h(sd)
    cls = ""
    if bc != "*":
        cls = bc.lower() + (sc.lower() if sc != "*" else "**") + (i.lower() if i != "*" else "**")
    return s, cls


def read_modules(root, kver):
    base = os.path.join(root, "lib", "modules", kver)
    dep = os.path.join(base, "modules.dep")
    if not os.path.exists(dep):
        die("no %s" % dep)
    mods, deps, paths = {}, {}, {}
    for line in open(dep, encoding="utf-8"):
        path, _, rest = line.partition(":")
        deps[modname(path)] = [modname(x) for x in rest.split()]
        paths[modname(path)] = path
        cat = category(path)
        if cat:
            mods[modname(path)] = {"cat": cat, "path": path}
    for n in mods:
        mods[n]["deps"] = deps.get(n, [])
    return base, mods, deps, paths


def read_builtin(base, mods):
    """Drivers compiled into the kernel have no .ko and no modules.alias
    line; their ids are in modules.builtin.modinfo as 'name.alias=pci:...'."""
    bi = os.path.join(base, "modules.builtin")
    if not os.path.exists(bi):
        return
    for line in open(bi, encoding="utf-8"):
        path = line.strip()
        cat = category(path)
        if cat:
            mods.setdefault(modname(path), {"cat": cat, "path": path, "deps": [], "builtin": True})
    mi = os.path.join(base, "modules.builtin.modinfo")
    if os.path.exists(mi):
        for item in open(mi, "rb").read().split(b"\0"):
            t = item.decode("utf-8", "replace")
            if ".alias=" not in t:
                continue
            name, _, al = t.partition(".alias=")
            name = name.replace("-", "_")
            if name in mods and mods[name].get("builtin"):
                feed_alias("alias " + al + " " + name, mods)


SUSPECT = ("wireless", "wifi", "bluetooth", "sound", "snd", "audio", "camera", "video", "media", "drm", "gpu", "ipu", "net/")


def uncategorized(base, mods, paths):
    """Drivers with PCI or USB ids that no category claims, by folder. A
    kernel that moves a driver (7.0 moved snd-hda-intel) would otherwise
    make its devices look unsupported without a word."""
    seen, out = set(), {}
    for line in open(os.path.join(base, "modules.alias"), encoding="utf-8", errors="replace"):
        m = PCI.match(line) or USB.match(line)
        if not m:
            continue
        mod = m.groups()[-1].replace("-", "_")
        if mod in mods or mod in seen or mod not in paths:
            continue
        seen.add(mod)
        d = os.path.dirname(paths[mod]).replace("kernel/", "", 1)
        out[d] = out.get(d, 0) + 1
    # old framebuffer drivers (nvidiafb, atyfb...) are not what drives a screen now
    return {d: n for d, n in sorted(out.items())
            if any(k in d for k in SUSPECT) and not d.startswith("drivers/video/fbdev")}


def feed_alias(line, mods):
    m = PCI.match(line)
    if m:
        mod = m.group(8).replace("-", "_")
        if mod in mods:
            s, cls = ids_pci(m.groups()[:7])
            if not (s == "*:*" and not cls):
                mods[mod].setdefault("pci", set()).add(s if not cls else s + "@" + cls)
        return
    m = USB.match(line)
    if m:
        v, p, dc, dsc, dp, ic, isc, ip, mod = m.groups()
        mod = mod.replace("-", "_")
        if mod in mods:
            low = lambda x: "*" if x == "*" else x.lower()
            s = low(v) + ":" + low(p)
            c, sc, pr = (ic, isc, ip) if ic != "*" else (dc, dsc, dp)
            if c != "*":
                s += "@" + c.lower() + (sc.lower() if sc != "*" else "**") + (pr.lower() if pr != "*" else "**")
            if s != "*:*":
                mods[mod].setdefault("usb", set()).add(s)


def read_aliases(base, mods):
    for line in open(os.path.join(base, "modules.alias"), encoding="utf-8", errors="replace"):
        feed_alias(line, mods)


def firmware_files(root):
    have = set()
    for sub in ("usr/lib/firmware", "lib/firmware"):
        top = os.path.join(root, sub)
        if not os.path.isdir(top):
            continue
        for dp, _, fs in os.walk(top):
            for f in fs:
                rel = os.path.relpath(os.path.join(dp, f), top)
                for c in FWCOMP:
                    if rel.endswith(c):
                        rel = rel[: -len(c)]
                have.add(rel)
        break
    return have


def read_firmware(root, kver, mods, have, pk):
    """What each driver wants, counting the modules it pulls in: a PCI
    front end such as rtw89_8852be declares nothing itself, its chip
    module (rtw89_8852b) names the firmware."""
    # Only the drivers' own families count as "pulled in": cfg80211 and
    # mac80211 name regulatory.db, and counting that would make every Wi-Fi
    # driver look as if it had its firmware.
    names = sorted(mods)
    wants = {n: [] for n in names}
    for i in range(0, len(names), 80):
        chunk = names[i:i + 80]
        r = subprocess.run(["modinfo", "-b", root, "-k", kver] + chunk, capture_output=True, text=True)
        # a block runs from "filename:" to the next one, and its "name:"
        # line comes AFTER its firmware lines
        fws = []
        for line in r.stdout.splitlines():
            if line.startswith("filename:"):
                fws = []
            elif line.startswith("firmware:"):
                fws.append(line.split(":", 1)[1].strip())
            elif line.startswith("name:"):
                nm = line.split(":", 1)[1].strip().replace("-", "_")
                if nm in wants:
                    wants[nm].extend(fws)
    for n in sorted(mods):
        decl = set(wants[n])
        for d in mods[n]["deps"]:
            if d in mods:
                decl.update(wants.get(d, []))
        decl = sorted(decl)
        pres = 0
        for pat in decl:
            if any(ch in pat for ch in "*?["):
                if any(fnmatch.fnmatchcase(f, pat) for f in have):
                    pres += 1
            elif pat in have:
                pres += 1
        mods[n]["fw_declared"] = len(decl)
        mods[n]["fw_present"] = pres
        mods[n]["fw"] = "none" if not decl else ("ok" if pres else "missing")
        # Only some drivers cannot start without their firmware: a Wi-Fi
        # chip, the Intel/AMD sound DSP, an AMD graphics chip. A wired
        # network card, an Intel graphics chip or an Nvidia one starts
        # without (slower, or with fewer features), so "missing" there is
        # a note and never a verdict.
        mods[n]["fw_required"] = (mods[n]["cat"] == "wifi" or n.startswith("snd_sof") or n == "amdgpu")
        # The SOF sound DSP loads sof-*.ri by a name it builds at run time,
        # so the kernel declares nothing: the package is the evidence.
        if n.startswith("snd_sof") and not decl:
            mods[n]["fw"] = "ok" if "firmware-sof-signed" in pk else "missing"


def dpkg(root):
    st = os.path.join(root, "var/lib/dpkg/status")
    pk = {}
    if os.path.exists(st):
        name = ver = None
        ok = False
        for line in open(st, encoding="utf-8", errors="replace"):
            if line.startswith("Package:"):
                name, ver, ok = line.split(":", 1)[1].strip(), None, False
            elif line.startswith("Status:"):
                ok = line.rstrip().endswith(" installed")
            elif line.startswith("Version:"):
                ver = line.split(":", 1)[1].strip()
            elif not line.strip() and name:
                if ok:
                    pk[name] = ver
                name = None
        if name and ok:
            pk[name] = ver
    return pk


def norm_tokens(s):
    return " ".join(re.sub(r"[^a-z0-9]+", " ", s.lower()).split())


def printer_models(root):
    out = set()
    drv = os.path.join(root, "usr/share/cups/drv/hpcups.drv")
    if os.path.exists(drv):
        for line in open(drv, encoding="utf-8", errors="replace"):
            m = re.match(r'\s*ModelName\s+"([^"]+)"', line)
            if m and not line.lstrip().startswith("//"):
                out.add(norm_tokens(m.group(1)))
    gp = os.path.join(root, "usr/share/gutenprint")
    for dp, _, fs in os.walk(gp):
        if not dp.endswith("xml/printers"):
            continue
        for f in fs:
            if f.endswith(".xml"):
                for m in re.finditer(r'<printer\b[^>]*?\bname="([^"]+)"',
                                     open(os.path.join(dp, f), encoding="utf-8", errors="replace").read()):
                    out.add(norm_tokens(m.group(1)))
    return sorted(x for x in out if len(x) >= 5)


def build(root):
    libmods = os.path.join(root, "lib", "modules")
    kvers = sorted(os.listdir(libmods)) if os.path.isdir(libmods) else []
    if not kvers:
        die("no kernel under %s" % libmods)
    kver = kvers[-1]
    base, mods, deps, paths = read_modules(root, kver)
    read_aliases(base, mods)
    read_builtin(base, mods)
    for d, n in uncategorized(base, mods, paths).items():
        print("gen-compat: warning: %d driver(s) with PCI/USB ids in %s are in no category" % (n, d), file=sys.stderr)
    for n in mods:
        mods[n].setdefault("deps", [])
    have = firmware_files(root)
    pk = dpkg(root)
    read_firmware(root, kver, mods, have, pk)

    def has(*names):
        return any(n in pk for n in names)

    features = {
        "fprintd": has("fprintd", "libfprint-2-2"),
        "sane": has("sane-utils", "libsane1", "sane-airscan"),
        "ipp_usb": has("ipp-usb"),
        "foo2zjs": has("printer-driver-foo2zjs", "hplip"),
        "broadcom_wl": has("bcmwl-kernel-source", "broadcom-sta-dkms"),
        "nvidia": any(k.startswith("nvidia-driver") for k in pk),
        "libcamera": has("libcamera0.2", "libcamera0.3", "libcamera0.4", "pipewire-libcamera"),
        "cups_driverless": os.path.exists(os.path.join(root, "usr/lib/cups/driver/driverless")),
    }
    seeds = json.load(open(os.path.join(REPO, "hw", "seeds.json"), encoding="utf-8"))

    modules = {}
    for n in sorted(mods):
        m = mods[n]
        if not (m.get("pci") or m.get("usb")) and m["cat"] != "bus":
            continue
        e = {"cat": m["cat"], "fw": m["fw"], "fw_required": m["fw_required"],
             "fw_declared": m["fw_declared"], "fw_present": m["fw_present"]}
        if m.get("pci"):
            e["pci"] = " ".join(sorted(m["pci"]))
        if m.get("usb"):
            e["usb"] = " ".join(sorted(m["usb"]))
        modules[n] = e
    return {
        "schema": 1,
        "_about": "GENERATED by tools/hw/gen-compat.py from the image + hw/seeds.json. Do not edit; edit seeds.json.",
        "image": {
            "kernel": kver,
            "kernel_package": pk.get("linux-image-generic"),
            "firmware_files": len(have),
            "features": features,
            "packages": {k: pk[k] for k in sorted(pk) if re.match(
                r"(linux-firmware|firmware-sof|bluez$|cups$|cups-filters$|printer-driver-|sane-|ipp-usb|fprintd|"
                r"libfprint|libcamera|pipewire-libcamera|hplip|bcmwl|broadcom-sta|nvidia-)", k)},
        },
        "modules": modules,
        "printers": {"models": printer_models(root)},
        "words": seeds["words"],
        "rules": seeds["rules"],
    }


def dumps(d):
    # one line per module and per list: a diff reads as "this id changed"
    s = json.dumps(d, indent=1, ensure_ascii=False, sort_keys=False)
    return s + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("-o", "--out", default=os.path.join(REPO, "hw", "compat.json"))
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    new = dumps(build(a.root))
    if a.check:
        old = open(a.out, encoding="utf-8").read() if os.path.exists(a.out) else ""
        if old == new:
            print("hw/compat.json is what this image gives")
            return 0
        o, n = json.loads(old or "{}"), json.loads(new)
        print("hw/compat.json is STALE against this image:")
        print("  kernel: %s -> %s" % (o.get("image", {}).get("kernel"), n["image"]["kernel"]))
        om, nm = o.get("modules", {}), n["modules"]
        for k in sorted(set(om) | set(nm)):
            if om.get(k) != nm.get(k):
                print("  module %-24s %s" % (k, "added" if k not in om else "gone" if k not in nm else "changed"))
        if o.get("printers", {}).get("models") != n["printers"]["models"]:
            print("  printer models changed")
        if o.get("image", {}).get("features") != n["image"]["features"]:
            print("  features: %s -> %s" % (o.get("image", {}).get("features"), n["image"]["features"]))
        print("regenerate: python3 -I tools/hw/gen-compat.py ROOTFS, then python3 -I tools/hw/gen-header.py")
        return 1 if os.environ.get("HW_STRICT") == "1" else 0
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(new)
    n = json.loads(new)
    pci = sum(len(m.get("pci", "").split()) for m in n["modules"].values())
    usb = sum(len(m.get("usb", "").split()) for m in n["modules"].values())
    print("wrote %s: kernel %s, %d modules, %d pci + %d usb ids, %d printer models" % (
        a.out, n["image"]["kernel"], len(n["modules"]), pci, usb, len(n["printers"]["models"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
