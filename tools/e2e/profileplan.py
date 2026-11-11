#!/usr/bin/env python3
"""profileplan.py -- what the Windows journey expects of ONE profile, and which
of its steps cannot apply to that profile (and why).

    python3 tools/e2e/profileplan.py PROFILE [--markdown]

e2e.py (the journey) and e2e.yml (the gate that prints the plan before hours
of runner) both ask here. The profile is read by tools/customize/installer_profile.py,
the same code build/make-os uses to decide what the installer asks, so the
journey never holds a second opinion about what a profile means.

With no profile, none of this is used: the journey is the desktop release's,
unchanged. Standard library only (no numpy: the gate has none).

A step is skipped only by a rule below, from the profile's own policy fields,
and the reason is printed beside it. Nothing is skipped silently.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "customize"))
import installer_profile as ip  # noqa: E402

# What the installer's look page starts on when the profile does not say
# (desktop: wizard.c's "Like Windows", Light). The journey has always expected
# exactly this of the public installer (e2e.yml: --expect-look taskbar
# --expect-theme sandstone).
WIZARD_LOOK, WIZARD_THEME = "taskbar", "sandstone"


class Plan:
    """Everything the journey needs to know about one profile."""

    def __init__(self, name, values, header):
        self.name = name
        self.values = values            # the profile after `inherit`, as forge reads it
        defs = dict(re.findall(r"#define AUROS_PROFILE_(\w+) (.+)", header))
        self.look_offered = defs.get("LOOK_OFFERED") == "1"
        start_look = defs.get("LOOK", '""').strip('"')
        start_theme = defs.get("THEME", '""').strip('"')
        if self.look_offered:
            # The harness presses the primary button on every page, so the look
            # page is left on what it started on.
            self.look = start_look or WIZARD_LOOK
            self.theme = start_theme or WIZARD_THEME
        else:
            # No look page: the image keeps its own.
            self.look = values.get("shell_archetype", "")
            self.theme = values.get("theme", "")
        v = values
        self.kiosk = v.get("kiosk_mode", "no").lower() == "yes"
        self.no_install = v.get("allow_user_install", "yes").lower() in ("no", "false", "0", "off", "n")
        self.store = v.get("software_store", "none") not in ("", "none")
        self.locale = v.get("locale", "en_US.UTF-8")
        self.keyboard = v.get("keyboard_layout", "us")
        self.apps = v.get("packages_apps", "").split()
        self.default_user = v.get("default_user", "")

    # -- the rules ------------------------------------------------------
    def store_reason(self):
        """None when the store step (install VLC, open it) applies; else why not."""
        if self.kiosk:
            return "kiosk_mode=yes: the desktop offers only allowed_apps, there is no store to open"
        if not self.store:
            return "the profile has no software_store: no store is installed"
        if self.no_install:
            return ("allow_user_install=no: the store has no Flathub and may not install; "
                    "VLC cannot be installed from it")
        if not self.locale.lower().startswith("en"):
            return ("locale=%s: the store's words are not English, and the journey finds its "
                    "buttons by their English names" % self.locale)
        if self.keyboard not in ("", "us"):
            return ("keyboard_layout=%s: the journey types on a US keyboard" % self.keyboard)
        return None

    def flathub_reason(self):
        """None when first start must fetch Flathub's catalogue; else why not."""
        if not self.store:
            return "the profile has no software_store: no Flathub"
        if self.no_install:
            return "allow_user_install=no: forge leaves Flathub out"
        return None

    def steps(self):
        """[(name, runs, why)] in journey order. `why` is the reason when skipped,
        the thing proved when it runs."""
        sr, fr = self.store_reason(), self.flathub_reason()
        return [
            ("download", True, "Edge opens the website, the installer AurOS-Setup-%s.exe is downloaded and its SHA-256 compared" % self.name),
            ("windows warnings", True, "SmartScreen and the permission box are answered"),
            ("wizard", True, "every installer page is passed with its main button" +
             ("; the look page starts on %s / %s" % (self.look, self.theme) if self.look_offered
              else "; this profile gets no look page (the image decides its look)")),
            ("restart and install", True, "Windows restarts into the installer, which downloads the image, copies AurOS and starts it"),
            ("it all works", True, "AurOS asks 'Yes, it all works' and 'Start AurOS from now on' and takes the answers"),
            ("store", sr is None, "VLC is found in 'Get more programs', installed and opened" if sr is None else sr),
            ("profile on disk", True, "powered off: policy.conf names %s%s" % (self.name,
             "; every program of the profile is installed" if self.apps else "; the profile adds no programs")),
            ("look on disk", True, "powered off: the desktop is %s and the theme is %s, as %s" % (
             self.look, self.theme, "the installer's look page left it" if self.look_offered else "the image's own")),
            ("first-session journal", True, "network, clock, no failed unit, AppArmor, DNS and the rest of the first-session facts"),
            ("flathub catalogue", fr is None, "Flathub's catalogue was fetched at first start" if fr is None else fr),
        ]

    def markdown(self):
        out = ["### Windows journey for profile `%s`" % self.name, "",
               "| Step | Runs | What / why not |", "|---|---|---|"]
        for name, runs, why in self.steps():
            out.append("| %s | %s | %s |" % (name, "yes" if runs else "**SKIPPED**", why))
        return "\n".join(out) + "\n"

    def text(self):
        return "\n".join("%-24s %-8s %s" % (n, "runs" if r else "SKIPPED", w) for n, r, w in self.steps())


def load(name, profiles=None):
    """The plan for `name`; ip.ProfileError (a plain sentence) when it is not a profile
    an installer can be made for."""
    root = ip.ROOT
    pdir = profiles or root / "profiles"
    header = ip.compile_profile(name, pdir)
    return Plan(name, ip.resolve(name, pdir), header)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print("usage: profileplan.py PROFILE [--markdown]", file=sys.stderr)
        return 2
    try:
        plan = load(args[0])
    except ip.ProfileError as exc:
        print("profileplan: %s" % exc, file=sys.stderr)
        return 2
    print(plan.markdown() if "--markdown" in argv else plan.text())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
