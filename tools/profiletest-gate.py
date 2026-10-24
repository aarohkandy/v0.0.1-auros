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
        or value in {"tools/profile_image.py", "tools/test_profile_image.py"}
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


def _selftest() -> None:
    allowed = [
        "tools/profiletest.sh",
        "tools/profiletest-gate.py",
        "tools/profile_image.py",
        "tools/test_profile_image.py",
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
    sub.add_parser("selftest")
    args = parser.parse_args()

    if args.command == "selftest":
        _selftest()
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
