#!/usr/bin/env python3
"""Create or validate a bounded, redacted Fcitx profile snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import sys
from pathlib import Path


SCHEMA = "auros-fcitx-profile-snapshot-v1"
MAX_BYTES = 8192
MAX_LINES = 256
MAX_LINE_BYTES = 512
SOURCES = {"runtime", "template"}
PHASES = {"start", "end"}
HEX = frozenset("0123456789abcdef")
KNOWN_SECTIONS = {
    "Groups/0",
    "Groups/0/Items/0",
    "Groups/0/Items/1",
    "GroupOrder",
}
KNOWN_KEYS = {"Name", "Default Layout", "DefaultIM", "Layout", "0"}
KNOWN_VALUES = {"", "Default", "fr", "pinyin", "keyboard-fr"}


class SnapshotError(ValueError):
    pass


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _read_regular(path: Path) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise SnapshotError(f"cannot open profile safely: {exc.strerror}") from exc
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise SnapshotError("profile is not a regular file")
        if info.st_size > MAX_BYTES:
            raise SnapshotError(f"profile exceeds {MAX_BYTES} bytes")
        data = os.read(fd, MAX_BYTES + 1)
        if len(data) > MAX_BYTES:
            raise SnapshotError(f"profile exceeds {MAX_BYTES} bytes")
        return data
    finally:
        os.close(fd)


def _token(kind: str, value: str, known: set[str]) -> dict[str, object]:
    encoded = value.encode("utf-8")
    result: dict[str, object] = {
        f"{kind}_sha256": digest(encoded),
        f"{kind}_bytes": len(encoded),
    }
    if value in known:
        result[kind] = value
    return result


def snapshot(path: Path, source: str, phase: str) -> dict[str, object]:
    if source not in SOURCES or phase not in PHASES:
        raise SnapshotError("invalid source or phase")
    raw = _read_regular(path)
    if b"\0" in raw:
        raise SnapshotError("profile contains a NUL byte")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SnapshotError("profile is not UTF-8") from exc
    raw_lines = text.splitlines(keepends=True)
    if len(raw_lines) > MAX_LINES:
        raise SnapshotError(f"profile exceeds {MAX_LINES} lines")

    records: list[dict[str, object]] = []
    section_identity = ""
    seen_sections: set[str] = set()
    seen_keys: set[tuple[str, str]] = set()
    for number, raw_line in enumerate(raw_lines, 1):
        line_bytes = raw_line.encode("utf-8")
        if len(line_bytes) > MAX_LINE_BYTES:
            raise SnapshotError(f"line {number} exceeds {MAX_LINE_BYTES} bytes")
        logical = raw_line.rstrip("\r\n")
        record: dict[str, object] = {
            "line": number,
            "raw_sha256": digest(line_bytes),
            "raw_bytes": len(line_bytes),
        }
        if logical == "":
            record["kind"] = "blank"
        elif logical.startswith(("#", ";")):
            record["kind"] = "comment"
        elif logical.startswith("[") and logical.endswith("]"):
            section = logical[1:-1]
            if not section or section in seen_sections:
                raise SnapshotError(f"duplicate or empty section at line {number}")
            seen_sections.add(section)
            section_identity = digest(section.encode("utf-8"))
            record["kind"] = "section"
            record.update(_token("section", section, KNOWN_SECTIONS))
        elif "=" in logical and section_identity:
            key, value = logical.split("=", 1)
            key_identity = digest(key.encode("utf-8"))
            identity = (section_identity, key_identity)
            if not key or identity in seen_keys:
                raise SnapshotError(f"duplicate or empty key at line {number}")
            seen_keys.add(identity)
            record["kind"] = "entry"
            record.update(_token("key", key, KNOWN_KEYS))
            record.update(_token("value", value, KNOWN_VALUES))
        else:
            raise SnapshotError(f"unrecognized profile syntax at line {number}")
        records.append(record)

    return {
        "schema": SCHEMA,
        "status": "ok",
        "source": source,
        "phase": phase,
        "raw_sha256": digest(raw),
        "raw_bytes": len(raw),
        "line_count": len(raw_lines),
        "records": records,
    }


def validate(value: object, source: str, phase: str) -> None:
    if not isinstance(value, dict):
        raise SnapshotError("snapshot root is not an object")
    expected = {"schema", "status", "source", "phase", "raw_sha256", "raw_bytes", "line_count", "records"}
    if set(value) != expected:
        raise SnapshotError("snapshot fields are not exact")
    if value["schema"] != SCHEMA or value["status"] != "ok":
        raise SnapshotError("snapshot schema or status is invalid")
    if value["source"] != source or value["phase"] != phase:
        raise SnapshotError("snapshot source or phase is invalid")
    if (not isinstance(value["raw_sha256"], str) or len(value["raw_sha256"]) != 64
            or not set(value["raw_sha256"]) <= HEX):
        raise SnapshotError("snapshot hash is invalid")
    if not isinstance(value["raw_bytes"], int) or not 0 <= value["raw_bytes"] <= MAX_BYTES:
        raise SnapshotError("snapshot byte count is invalid")
    records = value["records"]
    if (not isinstance(records, list) or not records or not isinstance(value["line_count"], int)
            or value["line_count"] != len(records)):
        raise SnapshotError("snapshot line count is invalid")
    if len(records) > MAX_LINES:
        raise SnapshotError("snapshot has too many records")
    section_identity = ""
    seen_sections: set[str] = set()
    seen_keys: set[tuple[str, str]] = set()
    byte_total = 0
    for index, record in enumerate(records, 1):
        if not isinstance(record, dict) or record.get("line") != index:
            raise SnapshotError("snapshot record order is invalid")
        kind = record.get("kind")
        common = {"line", "kind", "raw_sha256", "raw_bytes"}
        optional: set[str]
        if kind in {"blank", "comment"}:
            optional = set()
        elif kind == "section":
            optional = {"section_sha256", "section_bytes"} | ({"section"} if "section" in record else set())
            if record.get("section") not in KNOWN_SECTIONS and "section" in record:
                raise SnapshotError("snapshot exposes an unknown section")
            section_identity = record.get("section_sha256", "")
            if section_identity in seen_sections:
                raise SnapshotError("snapshot contains a duplicate section")
            seen_sections.add(section_identity)
        elif kind == "entry":
            if not section_identity:
                raise SnapshotError("snapshot entry has no section")
            optional = {"key_sha256", "key_bytes", "value_sha256", "value_bytes"}
            if "key" in record:
                optional.add("key")
                if record["key"] not in KNOWN_KEYS:
                    raise SnapshotError("snapshot exposes an unknown key")
            if "value" in record:
                optional.add("value")
                if record["value"] not in KNOWN_VALUES:
                    raise SnapshotError("snapshot exposes an unknown value")
            key_identity = record.get("key_sha256", "")
            identity = (section_identity, key_identity)
            if identity in seen_keys:
                raise SnapshotError("snapshot contains a duplicate key")
            seen_keys.add(identity)
        else:
            raise SnapshotError("snapshot record kind is invalid")
        if set(record) != common | optional:
            raise SnapshotError("snapshot record fields are not exact")
        for key in (name for name in record if name.endswith("sha256")):
            if (not isinstance(record[key], str) or len(record[key]) != 64
                    or not set(record[key]) <= HEX):
                raise SnapshotError("snapshot record hash is invalid")
        for key in (name for name in record if name.endswith("bytes")):
            if not isinstance(record[key], int) or not 0 <= record[key] <= MAX_LINE_BYTES:
                raise SnapshotError("snapshot record size is invalid")
        for token in ("section", "key", "value"):
            if token in record:
                encoded = record[token].encode("utf-8")
                if (record.get(f"{token}_sha256") != digest(encoded)
                        or record.get(f"{token}_bytes") != len(encoded)):
                    raise SnapshotError("snapshot clear token does not match its hash")
        byte_total += record["raw_bytes"]
    if byte_total != value["raw_bytes"]:
        raise SnapshotError("snapshot record sizes do not match the file size")


def semantic_records(value: dict[str, object]) -> list[dict[str, object]]:
    """Return the exact ordered non-comment/nonblank byte evidence."""
    return [
        {key: item[key] for key in item if key != "line"}
        for item in value["records"]  # type: ignore[union-attr]
        if item["kind"] not in {"comment", "blank"}
    ]


def profile_identity(environ: bytes, actual_uid: int, expected_uid: int,
                     home: str) -> dict[str, str]:
    if actual_uid != expected_uid:
        raise SnapshotError("shipped Fcitx UID mismatch")
    entries = environ.split(b"\0")
    homes = [entry[5:] for entry in entries if entry.startswith(b"HOME=")]
    xdgs = [entry[16:] for entry in entries if entry.startswith(b"XDG_CONFIG_HOME=")]
    expected_home = os.fsencode(home)
    if homes != [expected_home]:
        raise SnapshotError("shipped Fcitx HOME mismatch")
    if not xdgs:
        xdg = "absent"
    elif xdgs == [expected_home + b"/.config"]:
        xdg = home + "/.config"
    else:
        raise SnapshotError("shipped Fcitx XDG_CONFIG_HOME mismatch")
    return {"uid": str(expected_uid), "home": home, "xdg_config_home": xdg}


def compare_boundaries(values: dict[str, dict[str, object]],
                       config_hashes: tuple[str, str]) -> dict[str, object]:
    expected_names = {
        f"{source}{round_number}{phase}"
        for source in ("runtime", "template")
        for round_number in (1, 2)
        for phase in ("start", "end")
    }
    if set(values) != expected_names:
        raise SnapshotError("comparison snapshot names are not exact")
    for source in ("runtime", "template"):
        for round_number in (1, 2):
            for phase in ("start", "end"):
                validate(values[f"{source}{round_number}{phase}"], source, phase)

    templates = [values[f"template{round_number}{phase}"]
                 for round_number in (1, 2) for phase in ("start", "end")]
    template_identity = {(item["raw_sha256"], item["raw_bytes"]) for item in templates}
    if len(template_identity) != 1:
        raise SnapshotError("template bytes differ across observed boundaries")

    runtimes = [values[f"runtime{round_number}{phase}"]
                for round_number in (1, 2) for phase in ("start", "end")]
    for round_number in (1, 2):
        start = values[f"runtime{round_number}start"]
        end = values[f"runtime{round_number}end"]
        if ((start["raw_sha256"], start["raw_bytes"], start["records"])
                != (end["raw_sha256"], end["raw_bytes"], end["records"])):
            raise SnapshotError(f"round {round_number} runtime bytes changed")
        if start["raw_sha256"] != config_hashes[round_number - 1]:
            raise SnapshotError(f"round {round_number} runtime hash is not bound to process evidence")

    projections = [semantic_records(item) for item in templates + runtimes]
    if any(item != projections[0] for item in projections[1:]):
        raise SnapshotError("non-comment profile bytes differ across boundaries")
    r1 = values["runtime1start"]
    r2 = values["runtime2start"]
    return {
        "status": "pass",
        "template_raw_sha256": templates[0]["raw_sha256"],
        "round1_raw_sha256": r1["raw_sha256"],
        "round2_raw_sha256": r2["raw_sha256"],
        "cross_boot_raw_equal": "yes" if r1["raw_sha256"] == r2["raw_sha256"] else "no",
        "cross_boot_comment_blank_only": "yes",
    }


def _load_snapshot(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) > 65536:
        raise SnapshotError("reduced snapshot exceeds 65536 bytes")
    value = json.loads(data.decode("utf-8"))
    if not isinstance(value, dict):
        raise SnapshotError("snapshot root is not an object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("create")
    create.add_argument("--source", choices=sorted(SOURCES), required=True)
    create.add_argument("--phase", choices=sorted(PHASES), required=True)
    create.add_argument("--input", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    check = sub.add_parser("validate")
    check.add_argument("--source", choices=sorted(SOURCES), required=True)
    check.add_argument("--phase", choices=sorted(PHASES), required=True)
    check.add_argument("snapshot", type=Path)
    compare = sub.add_parser("compare")
    compare.add_argument("--round1-config-hash", required=True)
    compare.add_argument("--round2-config-hash", required=True)
    for source in ("runtime", "template"):
        for round_number in (1, 2):
            for phase in ("start", "end"):
                compare.add_argument(
                    f"--{source}-round{round_number}-{phase}", type=Path, required=True)
    identity = sub.add_parser("identity")
    identity.add_argument("--pid", type=int, required=True)
    identity.add_argument("--expected-uid", type=int, required=True)
    identity.add_argument("--home", required=True)
    args = parser.parse_args()
    try:
        if args.command == "create":
            value = snapshot(args.input, args.source, args.phase)
            encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
            if len(encoded.encode("utf-8")) > 65536:
                raise SnapshotError("reduced snapshot exceeds 65536 bytes")
            temporary = args.output.with_name(args.output.name + ".tmp")
            temporary.write_text(encoded, encoding="utf-8")
            os.replace(temporary, args.output)
        elif args.command == "validate":
            validate(_load_snapshot(args.snapshot), args.source, args.phase)
        elif args.command == "compare":
            values = {}
            for source in ("runtime", "template"):
                for round_number in (1, 2):
                    for phase in ("start", "end"):
                        key = f"{source}{round_number}{phase}"
                        values[key] = _load_snapshot(
                            getattr(args, f"{source}_round{round_number}_{phase}"))
            result = compare_boundaries(
                values, (args.round1_config_hash, args.round2_config_hash))
            for key, value in result.items():
                print(f"{key}={value}")
        else:
            proc = Path("/proc") / str(args.pid)
            result = profile_identity(
                (proc / "environ").read_bytes(), proc.stat().st_uid,
                args.expected_uid, args.home)
            for key, value in result.items():
                print(f"{key}={value}")
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, SnapshotError) as exc:
        print(f"fcitx profile snapshot: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
