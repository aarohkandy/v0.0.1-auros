#!/usr/bin/env python3
"""Turn profiles/<name>.profile into the inert C data baked into AurBridge.

One AurOS-Setup.exe installs exactly one profile, chosen when it is BUILT.
This reads that profile and writes src/aurbridge's `aurbridge-profile.h`:

  AUROS_PROFILE_ID           the image this installer installs (its journal
                             profile, its file name auros-<id>.img)
  AUROS_PROFILE_LOOK         the shell the look page starts on ("" = the
                             wizard's own default, which is what `desktop`
                             has always shipped)
  AUROS_PROFILE_THEME        the theme the image keeps whichever look is picked
  AUROS_PROFILE_LOOK_OFFERED 1 if the person may pick a look, 0 if the image
                             decides it (kiosk, settings or theme locked, or
                             a shell the four look cards cannot express)

A new profile needs no edit anywhere else: the name is held to a strict
pattern and must have a profiles/<name>.profile whose profile_id says the same.
Anything doubtful is an error, never a fall-back to desktop.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# The same pattern src/aurbridge/profile_image.c enforces at run time; the
# image name auros-<id>.img and the journal field are built from it.
NAME = re.compile(r"[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?")
# The four looks the wizard has a card for (wizard.c STYLES[]).
CARD_LOOKS = ("taskbar", "dock", "rail", "tiles")
KNOWN = re.compile(r'^[ \t]*([a-z_]+)[ \t]*=[ \t]*"?([^"\n#]*)"?[ \t]*(?:#.*)?$')


class ProfileError(ValueError):
    pass


def check_name(name: str) -> str:
    if not NAME.fullmatch(name):
        raise ProfileError(
            f"profile name {name!r} is not lower-case letters, digits and "
            "dashes (1-32 characters, starting and ending with a letter or digit)"
        )
    return name


def _file_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ProfileError(f"cannot read {path}: {exc}") from exc
    for line in text.splitlines():
        m = KNOWN.match(line)
        if not m:
            continue
        value = m.group(2).strip()
        if "$" in value or "`" in value:
            if m.group(1) in ("inherit", "profile_id", "theme", "shell_archetype",
                               "kiosk_mode", "allow_settings_change", "allow_theme_change"):
                raise ProfileError(f"{path.name}: {m.group(1)} must be plain text, not a shell expression")
            continue
        values[m.group(1)] = value  # the last line for a key wins, as in the shell
    return values


def resolve(name: str, profiles: Path) -> dict[str, str]:
    """The profile's settings after `inherit`, parents first, as forge reads them."""
    chain: list[str] = []
    cur = check_name(name)
    while cur:
        if cur in chain or len(chain) > 8:
            raise ProfileError(f"profile inherit cycle or chain too deep at {cur!r}")
        path = profiles / f"{cur}.profile"
        if not path.is_file():
            raise ProfileError(f"no such profile: {cur!r} ({path})")
        chain.append(cur)
        cur = _file_values(path).get("inherit", "")
        if cur:
            check_name(cur)
    merged: dict[str, str] = {}
    for item in reversed(chain):
        merged.update(_file_values(profiles / f"{item}.profile"))
    if merged.get("profile_id") != name:
        raise ProfileError(
            f"profiles/{name}.profile says profile_id={merged.get('profile_id')!r}; "
            "the file name and profile_id must be the same"
        )
    return merged


def compile_profile(name: str, profiles: Path = ROOT / "profiles",
                    themes: Path = ROOT / "themes",
                    shells: Path = ROOT / "shells") -> str:
    p = resolve(name, profiles)
    look_shell = p.get("shell_archetype", "")
    theme = p.get("theme", "")
    if not re.fullmatch(r"[a-z0-9-]{1,32}", theme) or not (themes / f"{theme}.theme").is_file():
        raise ProfileError(f"profile {name!r} names theme {theme!r}, which themes/ does not have")
    if not re.fullmatch(r"[a-z0-9-]{1,32}", look_shell) or not (shells / f"{look_shell}.shell").is_file():
        raise ProfileError(f"profile {name!r} names shell {look_shell!r}, which shells/ does not have")

    # The look page asks the person to choose how it LOOKS. Offer that only
    # where the image would honour it: not on a kiosk, not where the profile
    # forbids settings or theme changes, and only where the profile's own
    # shell is one the four cards can show. Otherwise the image decides and
    # the installer asks nothing and writes nothing about it.
    offered = (
        p.get("kiosk_mode", "no") != "yes"
        and p.get("allow_settings_change", "yes") != "no"
        and p.get("allow_theme_change", "yes") != "no"
        and look_shell in CARD_LOOKS
    )
    # `desktop` keeps the wizard's built-in start (Like Windows, Light): that
    # is what the public installer has always done and it must not move. Any
    # other profile starts on its own look and theme.
    start_look = look_shell if (offered and name != "desktop") else ""
    start_theme = theme if (offered and name != "desktop") else ""
    values = {
        "AUROS_PROFILE_ID": f'"{name}"',
        "AUROS_PROFILE_LOOK": f'"{start_look}"',
        "AUROS_PROFILE_THEME": f'"{start_theme}"',
        "AUROS_PROFILE_LOOK_OFFERED": "1" if offered else "0",
    }
    lines = [
        "/* generated by tools/customize/installer_profile.py; do not edit */",
        "#ifndef AUROS_INSTALLER_PROFILE_GENERATED_H",
        "#define AUROS_INSTALLER_PROFILE_GENERATED_H",
    ]
    lines += [f"#define {k} {v}" for k, v in values.items()]
    lines += ["#endif", ""]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("profile", help="profile name, e.g. desktop, office, school-kiosk")
    ap.add_argument("--output", required=True, type=Path, help="generated C header")
    ap.add_argument("--profiles", type=Path, default=ROOT / "profiles",
                    help="directory of .profile files (tests use a scratch one)")
    args = ap.parse_args(argv)
    try:
        args.output.unlink()  # never leave yesterday's profile for the compiler to find
    except FileNotFoundError:
        pass
    try:
        text = compile_profile(args.profile, args.profiles)
    except ProfileError as exc:
        print(f"installer profile: {exc}", file=sys.stderr)
        return 2
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=args.output.name + ".", dir=args.output.parent)
    with os.fdopen(fd, "w", encoding="ascii", newline="\n") as stream:
        stream.write(text)
    os.replace(tmp, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
