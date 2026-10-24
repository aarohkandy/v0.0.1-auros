#!/usr/bin/env python3
"""Reduce a WAYLAND_DEBUG stream to parented-popup protocol syntax.

Object IDs in a merged stream do not prove a single process or connection.
Output contains object IDs and counts, never trace text.
"""
import argparse
import re
from pathlib import Path
from typing import Dict, Union

# libwayland's closure logger renders every object as interface@id. Keep these
# patterns aligned with that real wire-debug syntax; accepting a made-up
# separator would let fixtures pass while every guest trace remains invisible.
GET_SURFACE = re.compile(r"xdg_wm_base@\d+\.get_xdg_surface\(new id xdg_surface@(\d+), wl_surface@(\d+)\)")
GET_TOPLEVEL = re.compile(r"xdg_surface@(\d+)\.get_toplevel\(new id xdg_toplevel@\d+\)")
GET_POPUP = re.compile(r"xdg_surface@(\d+)\.get_popup\(new id xdg_popup@(\d+), xdg_surface@(\d+), xdg_positioner@\d+\)")
ATTACH = re.compile(r"wl_surface@(\d+)\.attach\(([^,]+),")
COMMIT = re.compile(r"wl_surface@(\d+)\.commit\(\)")
CONFIGURE = re.compile(r"xdg_popup@(\d+)\.configure\((-?\d+), (-?\d+), (\d+), (\d+)\)")
GRAB = re.compile(r"xdg_popup@(\d+)\.grab\(")
XDG_DESTROY = re.compile(r"xdg_surface@(\d+)\.destroy\(\)")
WL_DESTROY = re.compile(r"wl_surface@(\d+)\.destroy\(\)")


def analyze(raw: bytes, after: int) -> Dict[str, Union[str, int]]:
    if after < 0 or after > len(raw):
        raise ValueError("trace boundary is outside the trace")
    xdg_to_wl = {}  # type: Dict[int, int]
    roots = set()
    # child xdg surface -> [popup, parent, wl surface, pending buffer, mapped, geometry]
    candidates = {}
    counts = {"surface": 0, "toplevel": 0, "popup": 0,
              "attach": 0, "commit": 0, "configure": 0}
    position = 0
    for raw_line in raw.splitlines(keepends=True):
        line_start = position
        position += len(raw_line)
        line = raw_line.decode("utf-8", "replace")
        candidate_phase = line_start >= after
        if candidate_phase and "layer_shell" in line:
            raise ValueError("a layer-shell object appeared during the candidate phase")
        match = GET_SURFACE.search(line)
        if match:
            counts["surface"] += 1
            xdg_to_wl[int(match.group(1))] = int(match.group(2))
        match = GET_TOPLEVEL.search(line)
        if match:
            counts["toplevel"] += 1
            roots.add(int(match.group(1)))
        match = GET_POPUP.search(line) if candidate_phase else None
        if match:
            counts["popup"] += 1
            child, popup, parent = map(int, match.groups())
            wl_surface = xdg_to_wl.get(child)
            if parent in roots and wl_surface is not None:
                candidates[child] = [popup, parent, wl_surface, None, False, None]
        match = ATTACH.search(line) if candidate_phase else None
        if match:
            counts["attach"] += 1
            wl_surface = int(match.group(1))
            for state in candidates.values():
                if state[2] == wl_surface:
                    state[3] = match.group(2).strip() != "nil"
        match = COMMIT.search(line) if candidate_phase else None
        if match:
            counts["commit"] += 1
            wl_surface = int(match.group(1))
            for state in candidates.values():
                if state[2] == wl_surface and state[3] is not None:
                    state[4] = state[3]
                    state[3] = None
        match = CONFIGURE.search(line) if candidate_phase else None
        if match:
            counts["configure"] += 1
            popup, x, y, width, height = map(int, match.groups())
            for state in candidates.values():
                if state[0] == popup and width > 0 and height > 0:
                    state[5] = (x, y, width, height)
        match = GRAB.search(line) if candidate_phase else None
        if match and any(state[0] == int(match.group(1)) for state in candidates.values()):
            raise ValueError("the candidate popup requested an input grab")
        match = XDG_DESTROY.search(line)
        if match:
            destroyed = int(match.group(1))
            roots.discard(destroyed)
            xdg_to_wl.pop(destroyed, None)
            candidates.pop(destroyed, None)
        match = WL_DESTROY.search(line)
        if match:
            destroyed = int(match.group(1))
            for child, state in list(candidates.items()):
                if state[2] == destroyed:
                    candidates.pop(child)
    popups = [(child, state[0], state[1]) for child, state in candidates.items()
              if state[4] and state[5] is not None]
    if len(popups) != 1:
        summary = " ".join(f"{key}={counts[key]}" for key in
                           ("surface", "toplevel", "popup", "attach", "commit", "configure"))
        raise ValueError(
            f"expected exactly one mapped popup parented to a toplevel, got {len(popups)} ({summary})")
    child, popup, parent = popups[0]
    geometry = candidates[child][5]
    return {
        "parent_xdg_surface": parent,
        "popup_xdg_surface": child,
        "popup_xdg_popup": popup,
        "popup_wl_surface": xdg_to_wl[child],
        "popup_x": geometry[0],
        "popup_y": geometry[1],
        "popup_width": geometry[2],
        "popup_height": geometry[3],
        "parented_mapped_popup_count": 1,
        "parented_popup_syntax_observed": "yes",
        "connection_identity": "unproven",
        "layer_shell_objects": 0,
        "popup_input_grab": "no",
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", type=Path)
    ap.add_argument("--after-byte", required=True, type=int)
    args = ap.parse_args()
    raw = args.trace.read_bytes()
    result = analyze(raw, args.after_byte)
    for key, value in result.items():
        print(f"{key}={value}")


if __name__ == "__main__":
    main()
