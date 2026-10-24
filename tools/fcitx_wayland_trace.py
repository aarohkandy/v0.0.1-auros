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


def _lines(raw: bytes):
    position = 0
    for raw_line in raw.splitlines(keepends=True):
        start = position
        position += len(raw_line)
        yield start, raw_line.decode("utf-8", "replace")


def analyze_lifecycle(raw: bytes, initial_after: int, away_after: int,
                      b_input_after: int, return_after: int) -> Dict[str, Union[str, int]]:
    """Prove all candidates revoke before B input and remap in a new A epoch."""
    if not 0 <= initial_after < away_after < b_input_after <= return_after <= len(raw):
        raise ValueError("lifecycle trace boundaries are not strictly ordered")
    initial = analyze(raw[:away_after], initial_after)
    initial_wl = int(initial["popup_wl_surface"])
    initial_child = int(initial["popup_xdg_surface"])
    xdg_to_wl = {}
    roots = set()
    candidates = {}
    initial_unmapped = False
    checked_b_input = False
    return_map_epochs = set()
    for position, line in _lines(raw):
        if not checked_b_input and position >= b_input_after:
            if not initial_unmapped:
                raise ValueError("initial popup did not unmap or destroy before B input")
            if any(state[3] for state in candidates.values()):
                raise ValueError("a candidate popup remained mapped at B input")
            checked_b_input = True
        if position >= away_after and "layer_shell" in line:
            raise ValueError("a layer-shell object appeared during the lifecycle")
        match = GET_SURFACE.search(line)
        if match:
            xdg_to_wl[int(match.group(1))] = int(match.group(2))
        match = GET_TOPLEVEL.search(line)
        if match:
            roots.add(int(match.group(1)))
        match = GET_POPUP.search(line)
        if match:
            child, popup, parent = map(int, match.groups())
            wl = xdg_to_wl.get(child)
            if wl is not None and parent in roots:
                # popup, parent, wl, mapped, pending, geometry
                candidates[child] = [popup, parent, wl, False, None, None]
        match = GRAB.search(line) if position >= away_after else None
        if match and any(state[0] == int(match.group(1)) for state in candidates.values()):
            raise ValueError("a popup requested an input grab during the lifecycle")
        match = ATTACH.search(line)
        if match:
            wl = int(match.group(1))
            for state in candidates.values():
                if state[2] == wl:
                    state[4] = match.group(2).strip() != "nil"
        match = COMMIT.search(line)
        if match:
            wl = int(match.group(1))
            for child, state in candidates.items():
                if state[2] != wl or state[4] is None:
                    continue
                was_mapped, maps = state[3], state[4]
                state[3], state[4] = maps, None
                if child == initial_child and was_mapped and not maps and position < b_input_after:
                    initial_unmapped = True
                if away_after <= position < return_after and maps:
                    raise ValueError("candidate popup mapped during the away epoch")
                if position >= return_after and maps and not was_mapped:
                    return_map_epochs.add(child)
        match = CONFIGURE.search(line)
        if match:
            popup, x, y, width, height = map(int, match.groups())
            for state in candidates.values():
                if state[0] == popup and width > 0 and height > 0:
                    state[5] = (x, y, width, height)
        match = XDG_DESTROY.search(line)
        if match:
            destroyed = int(match.group(1))
            roots.discard(destroyed)
            if destroyed == initial_child and position < b_input_after:
                initial_unmapped = True
            candidates.pop(destroyed, None)
        match = WL_DESTROY.search(line)
        if match:
            destroyed = int(match.group(1))
            for child, state in list(candidates.items()):
                if state[2] == destroyed:
                    if child == initial_child and position < b_input_after:
                        initial_unmapped = True
                    candidates.pop(child)
    if not checked_b_input:
        raise ValueError("trace ended before the B-input boundary")
    mapped = [(child, state) for child, state in candidates.items()
              if state[3] and state[5] is not None]
    if len(mapped) != 1 or mapped[0][0] not in return_map_epochs:
        raise ValueError("return did not produce exactly one fresh mapped candidate epoch")
    _, returned = mapped[0]
    returned_wl = int(returned[2])
    return {
        "initial_popup_wl_surface": initial_wl,
        "return_popup_wl_surface": returned_wl,
        "initial_unmapped_before_b_input": "yes",
        "mapped_candidate_while_away": "no",
        "return_map_epoch": "fresh",
        "return_popup_reused": "yes" if returned_wl == initial_wl else "no",
        "popup_input_grab": "no",
        "layer_shell_objects": 0,
    }

def analyze_no_popup(raw: bytes, after: int) -> Dict[str, Union[str, int]]:
    """Reject a pre-existing or newly active popup after a sensitive click."""
    if after < 0 or after > len(raw):
        raise ValueError("trace boundary is outside the trace")
    counts = {"popup": 0, "map": 0, "configure": 0, "grab": 0}
    xdg_to_wl = {}
    candidate_wls = set()
    pending = {}
    mapped = {}
    checked_boundary = False
    for position, line in _lines(raw):
        surface = GET_SURFACE.search(line)
        if surface:
            xdg_to_wl[int(surface.group(1))] = int(surface.group(2))
        popup = GET_POPUP.search(line)
        if popup:
            child = int(popup.group(1))
            if child in xdg_to_wl:
                wl = xdg_to_wl[child]
                candidate_wls.add(wl)
                mapped.setdefault(wl, False)
        attach = ATTACH.search(line)
        if attach and int(attach.group(1)) in candidate_wls:
            pending[int(attach.group(1))] = attach.group(2).strip() != "nil"
        commit = COMMIT.search(line)
        if commit and int(commit.group(1)) in pending:
            wl = int(commit.group(1))
            maps = pending.pop(wl)
            mapped[wl] = maps
            if position >= after and maps:
                counts["map"] += 1
        if not checked_boundary and position >= after:
            if any(mapped.values()):
                raise ValueError("a candidate popup was already mapped at sensitive focus")
            checked_boundary = True
        if position < after:
            continue
        if "layer_shell" in line:
            raise ValueError("a layer-shell object appeared after sensitive focus")
        if popup:
            counts["popup"] += 1
        if CONFIGURE.search(line):
            counts["configure"] += 1
        if GRAB.search(line):
            counts["grab"] += 1
    if not checked_boundary and any(mapped.values()):
        raise ValueError("a candidate popup was already mapped at sensitive focus")
    if any(counts.values()):
        raise ValueError("popup activity appeared after sensitive focus")
    return {
        "candidate_popup_creations": 0,
        "candidate_popup_maps": 0,
        "candidate_popup_configures": 0,
        "candidate_popup_grabs": 0,
        "layer_shell_objects": 0,
        "observed_candidate_popup": "no",
    }

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", type=Path)
    ap.add_argument("--after-byte", required=True, type=int)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--no-popup", action="store_true")
    mode.add_argument("--lifecycle", action="store_true")
    ap.add_argument("--away-byte", type=int)
    ap.add_argument("--b-input-byte", type=int)
    ap.add_argument("--return-byte", type=int)
    args = ap.parse_args()
    raw = args.trace.read_bytes()
    if args.lifecycle:
        if args.away_byte is None or args.b_input_byte is None or args.return_byte is None:
            ap.error("--lifecycle requires --away-byte, --b-input-byte and --return-byte")
        result = analyze_lifecycle(raw, args.after_byte, args.away_byte,
                                   args.b_input_byte, args.return_byte)
    elif args.no_popup:
        result = analyze_no_popup(raw, args.after_byte)
    else:
        result = analyze(raw, args.after_byte)
    for key, value in result.items():
        print(f"{key}={value}")


if __name__ == "__main__":
    main()
