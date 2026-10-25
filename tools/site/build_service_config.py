#!/usr/bin/env python3
"""Write the fail-closed custom-build configuration for a staged website."""

import argparse
import json
import pathlib
import re
import sys
import urllib.parse


def exact_https_origin(value: str, label: str) -> str:
    try:
        parsed = urllib.parse.urlsplit(value)
        port = parsed.port
    except ValueError as error:
        raise ValueError(f"{label} must be one exact HTTPS origin") from error
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path not in ("", "/")
        or parsed.query
        or parsed.fragment
        or port is not None and not 1 <= port <= 65535
    ):
        raise ValueError(f"{label} must be one exact HTTPS origin")
    host = parsed.hostname.lower()
    labels = host.split(".")
    if not all(re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?", label) for label in labels):
        raise ValueError(f"{label} must be one exact HTTPS origin")
    authority = host if port is None else f"{host}:{port}"
    return f"https://{authority}"


def site_origin(value: str) -> str:
    try:
        parsed = urllib.parse.urlsplit(value)
    except ValueError as error:
        raise ValueError("site URL must be HTTPS") from error
    if parsed.scheme != "https" or not parsed.netloc or parsed.username is not None or parsed.password is not None or parsed.query or parsed.fragment:
        raise ValueError("site URL must be HTTPS")
    return exact_https_origin(f"https://{parsed.netloc}", "site origin")


def read_variables(wrangler: pathlib.Path) -> dict:
    variables = {}
    section = ""
    for number, raw in enumerate(wrangler.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r"\[([A-Za-z0-9_.-]+)\]", line)
        if match:
            section = match.group(1)
            continue
        if section != "vars":
            continue
        match = re.fullmatch(r'([A-Z0-9_]+)\s*=\s*"([^"\r\n]*)"\s*(?:#.*)?', line)
        if not match:
            continue
        key, value = match.groups()
        if key in variables:
            raise ValueError(f"duplicate {key} in wrangler configuration")
        variables[key] = value
    if section == "" and not variables:
        raise ValueError("wrangler configuration has no [vars] table")
    return variables


def configuration(wrangler: pathlib.Path, site_url: str, api_origin: str) -> dict:
    variables = read_variables(wrangler)
    enabled = variables.get("BUILD_REQUESTS_ENABLED")
    if enabled not in ("0", "1"):
        raise ValueError("BUILD_REQUESTS_ENABLED must be exactly 0 or 1")
    if enabled == "0":
        return {"enabled": False, "api_origin": ""}

    allowed = exact_https_origin(str(variables.get("BUILD_ALLOWED_ORIGIN", "")), "BUILD_ALLOWED_ORIGIN")
    published = site_origin(site_url)
    if allowed != published:
        raise ValueError(f"BUILD_ALLOWED_ORIGIN {allowed} does not match the published site origin {published}")
    api = exact_https_origin(api_origin, "AUROS_WISHES_URL")
    if api == published:
        raise ValueError("AUROS_WISHES_URL must name the Worker, not the website")
    return {"enabled": True, "api_origin": api}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wrangler", required=True, type=pathlib.Path)
    parser.add_argument("--site-url", required=True)
    parser.add_argument("--api-origin", default="")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)
    try:
        value = configuration(args.wrangler, args.site_url, args.api_origin)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    except (OSError, UnicodeError, ValueError) as error:
        print(f"build-service config: {error}", file=sys.stderr)
        return 1
    print("build-service config: " + (f"enabled for {value['api_origin']}" if value["enabled"] else "disabled"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
