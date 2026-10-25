#!/usr/bin/env python3
"""Choose profile builds without letting diagnostic-only pushes hide source changes."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path, PurePosixPath


PROFILES = ("office", "revive", "school-kiosk", "multilingual")
DIAGNOSTIC_MARKER = "[profiles:diagnostic]"


def _valid_path(value: str) -> bool:
    path = PurePosixPath(value)
    return bool(value) and not path.is_absolute() and "\\" not in value and not ({".", ".."} & set(path.parts))


def _diagnostic_path(value: str) -> bool:
    return (
        value.startswith("docs/")
        or value == ".github/workflows/profiles.yml"
        or value in {
            "tools/profile_image.py",
            "tools/test_profile_image.py",
            "tools/x11_xres_probe.py",
            "tools/test_x11_xres_probe.py",
            "tools/profiletest-fcitx-guest.sh",
            "tools/fcitx_profile_snapshot.py",
            "tools/test_fcitx_profile_snapshot.py",
            "tools/fcitx_wayland_trace.py",
            "tools/test_fcitx_wayland_trace.py",
            "tools/fcitx_connection_probe.py",
            "tools/test_fcitx_connection_probe.py",
            "tools/fcitx_marker_visibility.py",
            "tools/test_fcitx_marker_visibility.py",
            "tools/fcitx_password_server.py",
            "tools/test_fcitx_password_server.py",
            "tools/fcitx_trace_writers.py",
            "tools/test_fcitx_trace_writers.py",
        }
        or re.fullmatch(r"tools/profiletest[^/]*", value) is not None
    )


def _github_files(pages: object) -> list[str]:
    if not isinstance(pages, list) or not pages:
        raise ValueError("GitHub returned no changed-file pages")
    result: list[str] = []
    for page in pages:
        if not isinstance(page, dict) or not isinstance(page.get("files"), list):
            raise ValueError("GitHub changed-file page has no files array")
        for entry in page["files"]:
            if not isinstance(entry, dict) or not isinstance(entry.get("filename"), str):
                raise ValueError("GitHub changed-file entry has no filename")
            names = [entry["filename"]]
            previous = entry.get("previous_filename")
            if entry.get("status") == "renamed" and not isinstance(previous, str):
                raise ValueError("GitHub renamed-file entry has no previous_filename")
            if previous is not None:
                if not isinstance(previous, str):
                    raise ValueError("GitHub changed-file previous_filename is not a string")
                names.append(previous)
            for name in names:
                if name not in result:
                    result.append(name)
    return result


def decide(subject: str, files: list[str]) -> dict[str, object]:
    if not files:
        raise ValueError("GitHub returned no changed files")
    invalid = [value for value in files if not _valid_path(value)]
    if invalid:
        raise ValueError(f"invalid changed path: {invalid[0]!r}")

    all_profiles = list(PROFILES)
    if DIAGNOSTIC_MARKER in subject:
        if all(_diagnostic_path(value) for value in files):
            return {"go": False, "list": [], "ibus_functional": False, "diagnostic_only": True}
        # A mixed or production change must build every base. Do not let a
        # commit-message marker suppress production evidence.
        return {
            "go": True,
            "list": all_profiles,
            "ibus_functional": "[ibus-functional]" in subject,
            "diagnostic_only": False,
        }

    wanted: list[str] = []
    match = re.search(r"\[profiles([^]]*)\]", subject)
    if match:
        for value in match.group(1).split():
            if value in PROFILES and value not in wanted:
                wanted.append(value)
        if not wanted:
            wanted = all_profiles.copy()
    elif "[profiles" in subject:
        # Preserve the old gate's fail-closed treatment of an incomplete
        # marker: a typo must cost builds, never suppress them.
        wanted = all_profiles.copy()

    ibus = "[ibus-functional]" in subject
    if ibus and "multilingual" not in wanted:
        wanted.append("multilingual")

    if not wanted:
        force_all = {
            "profiles/desktop.profile",
            ".github/workflows/profiles.yml",
            "tools/profile_image.py",
            "tools/test_profile_image.py",
        }
        if any(value in force_all or re.fullmatch(r"tools/profiletest[^/]*", value) for value in files):
            wanted = all_profiles.copy()
        else:
            for profile in PROFILES:
                if f"profiles/{profile}.profile" in files:
                    wanted.append(profile)

    return {
        "go": bool(wanted),
        "list": wanted,
        "ibus_functional": ibus,
        "diagnostic_only": False,
    }


def probe_mode(x11: str, fcitx: str, security: str,
               production_security: str) -> dict[str, str]:
    values = {"true": "1", "false": "0", "": "0"}
    if (x11 not in values or fcitx not in values or security not in values
            or production_security not in values):
        raise ValueError("diagnostic probe options must be booleans")
    selected_x11 = values[x11]
    selected_fcitx = values[fcitx]
    selected_security = values[security]
    selected_production = values[production_security]
    if selected_production == "1" and "1" in (
            selected_x11, selected_fcitx, selected_security):
        raise ValueError("fcitx_production_security_probe is mutually exclusive")
    if selected_x11 == "1" and selected_fcitx == "1":
        raise ValueError("x11_panel_probe and fcitx_client_probe are mutually exclusive")
    if selected_security == "1" and selected_fcitx != "1":
        raise ValueError("fcitx_security_probe requires fcitx_client_probe")
    if selected_production == "1":
        mode = "fcitx-production-security"
    elif selected_security == "1":
        mode = "fcitx-security"
    elif selected_fcitx == "1":
        mode = "fcitx-client"
    elif selected_x11 == "1":
        mode = "x11-panel"
    else:
        mode = "ibus-functional"
    ibus = "0" if selected_fcitx == "1" or selected_production == "1" else "1"
    return {"ibus": ibus, "x11": selected_x11, "fcitx": selected_fcitx,
            "security": "1" if selected_production == "1" else selected_security,
            "production": selected_production, "mode": mode}


def _selftest() -> None:
    allowed = [
        "tools/profiletest.sh",
        "tools/profiletest-gate.py",
        "tools/profile_image.py",
        "tools/test_profile_image.py",
        "tools/x11_xres_probe.py",
        "tools/test_x11_xres_probe.py",
        "tools/profiletest-fcitx-guest.sh",
        "tools/fcitx_profile_snapshot.py",
        "tools/test_fcitx_profile_snapshot.py",
        "tools/fcitx_wayland_trace.py",
        "tools/test_fcitx_wayland_trace.py",
        "tools/fcitx_connection_probe.py",
        "tools/test_fcitx_connection_probe.py",
        "tools/fcitx_marker_visibility.py",
        "tools/test_fcitx_marker_visibility.py",
        "tools/fcitx_password_server.py",
        "tools/test_fcitx_password_server.py",
        "tools/fcitx_trace_writers.py",
        "tools/test_fcitx_trace_writers.py",
        ".github/workflows/profiles.yml",
        "docs/handoff/streams/P.md",
    ]
    for value in allowed:
        result = decide(DIAGNOSTIC_MARKER, [value])
        assert result == {"go": False, "list": [], "ibus_functional": False, "diagnostic_only": True}, value
    assert decide(DIAGNOSTIC_MARKER, allowed)["go"] is False

    for value in (
        "profiles/multilingual.profile",
        "rootfs/etc/auros-release",
        "src/aurshell/main.c",
        "build/forge",
        "site/index.html",
        "tools/profiletest/hidden.sh",
    ):
        result = decide(DIAGNOSTIC_MARKER, [value])
        assert result["go"] is True and result["list"] == list(PROFILES), value

    assert decide(DIAGNOSTIC_MARKER, ["docs/note.md", "src/aurshell/main.c"])["list"] == list(PROFILES)
    assert decide(f"{DIAGNOSTIC_MARKER} [ibus-functional]", ["tools/profiletest.sh"])["go"] is False
    assert decide("docs", ["docs/note.md"])["go"] is False
    assert decide("test", ["tools/profiletest.sh"])["list"] == list(PROFILES)
    assert decide("fix", ["profiles/revive.profile"])["list"] == ["revive"]
    assert decide("[profiles revive revive office]", ["docs/note.md"])["list"] == ["revive", "office"]
    assert decide("unfinished [profiles", ["docs/note.md"])["list"] == list(PROFILES)
    functional = decide("[ibus-functional]", ["docs/note.md"])
    assert functional["list"] == ["multilingual"] and functional["ibus_functional"] is True
    assert probe_mode("false", "false", "false", "false") == {
        "ibus": "1", "x11": "0", "fcitx": "0", "security": "0",
        "production": "0", "mode": "ibus-functional",
    }
    assert probe_mode("true", "false", "false", "false") == {
        "ibus": "1", "x11": "1", "fcitx": "0", "security": "0",
        "production": "0", "mode": "x11-panel",
    }
    assert probe_mode("false", "true", "false", "false") == {
        "ibus": "0", "x11": "0", "fcitx": "1", "security": "0",
        "production": "0", "mode": "fcitx-client",
    }
    assert probe_mode("false", "true", "true", "false") == {
        "ibus": "0", "x11": "0", "fcitx": "1", "security": "1",
        "production": "0", "mode": "fcitx-security",
    }
    assert probe_mode("false", "false", "false", "true") == {
        "ibus": "0", "x11": "0", "fcitx": "0", "security": "1",
        "production": "1", "mode": "fcitx-production-security",
    }
    for options in (("true", "true", "false", "false"),
                    ("yes", "false", "false", "false"),
                    ("false", "1", "false", "false"),
                    ("false", "false", "true", "false"),
                    ("false", "true", "false", "true"),
                    ("false", "false", "true", "true"),
                    ("true", "false", "false", "true")):
        try:
            probe_mode(*options)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unsafe diagnostic probe options accepted: {options!r}")
    for files in ([], ["../src/main.c"], ["/src/main.c"], ["src\\main.c"]):
        try:
            decide("", files)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unsafe file list accepted: {files!r}")
    renamed = _github_files([
        {"files": [{
            "filename": "docs/old-profile.md",
            "previous_filename": "profiles/multilingual.profile",
            "status": "renamed",
        }]}
    ])
    assert decide(DIAGNOSTIC_MARKER, renamed)["list"] == list(PROFILES)
    try:
        _github_files([{"files": [{"filename": "docs/note.md", "status": "renamed"}]}])
    except ValueError:
        pass
    else:
        raise AssertionError("rename without previous_filename accepted")
    print("profiletest gate selftest: pass")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    choose = sub.add_parser("decide")
    choose.add_argument("--subject", required=True)
    choose.add_argument("--github-pages", type=Path, required=True)
    choose.add_argument("--github-output", type=Path, required=True)
    mode = sub.add_parser("probe-mode")
    mode.add_argument("--x11", required=True)
    mode.add_argument("--fcitx", required=True)
    mode.add_argument("--security", required=True)
    mode.add_argument("--production-security", required=True)
    mode.add_argument("--github-output", type=Path, required=True)
    sub.add_parser("selftest")
    args = parser.parse_args()

    if args.command == "selftest":
        _selftest()
        return

    if args.command == "probe-mode":
        try:
            result = probe_mode(args.x11, args.fcitx, args.security,
                                args.production_security)
        except ValueError as exc:
            parser.error(str(exc))
        lines = [f"{key}={value}" for key, value in result.items()]
        with args.github_output.open("a", encoding="utf-8") as output:
            output.write("\n".join(lines) + "\n")
        print("\n".join(lines))
        return

    pages = json.loads(args.github_pages.read_text(encoding="utf-8"))
    files = _github_files(pages)
    result = decide(args.subject, files)
    lines = [
        f"go={str(result['go']).lower()}",
        f"list={json.dumps(result['list'], separators=(',', ':'))}",
        f"ibus_functional={str(result['ibus_functional']).lower()}",
    ]
    with args.github_output.open("a", encoding="utf-8") as output:
        output.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    if result["diagnostic_only"]:
        print("diagnostic-only marker accepted: normal profile image builds are suppressed")


if __name__ == "__main__":
    main()
