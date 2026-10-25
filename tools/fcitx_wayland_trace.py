#!/usr/bin/env python3
"""Reduce a WAYLAND_DEBUG stream to parented-popup protocol syntax.

Object IDs in a merged stream do not prove a single process or connection.
Output contains object IDs and counts, never trace text.
"""
import argparse
import re
from pathlib import Path
from typing import Dict, List, Union

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


def _bounded_lines(raw: bytes):
    """Yield both offsets so a boundary-crossing line stays fail closed."""
    position = 0
    for raw_line in raw.splitlines(keepends=True):
        start = position
        position += len(raw_line)
        yield start, position, raw_line.decode("utf-8", "replace")


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

class ReducedEvidenceError(ValueError):
    """A fail-closed verdict carrying only bounded, reduced evidence."""

    def __init__(self, message: str, evidence: Dict[str, Union[str, int]]):
        super().__init__(message)
        self.evidence = evidence


def _password_phase(line_end: int, after: int, input_after: int,
                    screenshot_after: int, submit_after: int) -> str:
    if line_end <= input_after:
        return "focus_to_input"
    if line_end <= screenshot_after:
        return "input_to_screenshot"
    if line_end <= submit_after:
        return "screenshot_to_submit"
    return "after_submit"


def analyze_no_popup(raw: bytes, after: int, input_after: int = None,
                     screenshot_after: int = None,
                     submit_after: int = None) -> Dict[str, Union[str, int]]:
    """Reject popup activity after sensitive focus and return redacted evidence.

    The candidate-prefixed compatibility counters conservatively include every
    Firefox xdg_popup event. Neither its Fcitx attribution nor its connection
    identity is proven by the merged client trace.
    """
    input_after = after if input_after is None else input_after
    screenshot_after = input_after if screenshot_after is None else screenshot_after
    submit_after = screenshot_after if submit_after is None else submit_after
    if not 0 <= after <= input_after <= screenshot_after <= submit_after <= len(raw):
        raise ValueError("password trace boundaries are not ordered within the trace")

    phases = ("focus_to_input", "input_to_screenshot",
              "screenshot_to_submit", "after_submit")
    kinds = ("popup", "map", "configure", "grab")
    counts = {kind: 0 for kind in kinds}
    phase_counts = {(phase, kind): 0 for phase in phases for kind in kinds}
    matched = {"configure": 0, "grab": 0}
    unmatched = {"configure": 0, "grab": 0}
    xdg_to_wl = {}
    roots = set()
    objects = {}
    active_child = {}
    popup_to_object = {}
    wl_to_object = {}
    historical_candidate_wls = set()
    pending = {}
    historical_pending = {}
    mapped = {}
    relevant = set()
    events: List[str] = []
    omitted_events = 0
    layer_shell_objects = 0
    mapped_at_focus = set()
    checked_boundary = False

    def record_event(kind: str, phase: str, popup_id: int = 0,
                     object_id: int = 0, matched_candidate: bool = False) -> None:
        nonlocal omitted_events
        counts[kind] += 1
        phase_counts[(phase, kind)] += 1
        if kind in matched:
            bucket = matched if matched_candidate else unmatched
            bucket[kind] += 1
        if object_id:
            relevant.add(object_id)
        child = objects[object_id]["child"] if object_id else 0
        if len(events) < 32:
            events.append(
                f"type:{kind},phase:{phase},popup:{popup_id},"
                f"child:{child},candidate:{'yes' if matched_candidate else 'no'}")
        else:
            omitted_events += 1

    for position, line_end, line in _bounded_lines(raw):
        sensitive = line_end > after
        if not checked_boundary and sensitive:
            mapped_at_focus = {child for child, state in mapped.items() if state}
            relevant.update(mapped_at_focus)
            checked_boundary = True
        phase = _password_phase(line_end, after, input_after,
                                screenshot_after, submit_after)
        surface = GET_SURFACE.search(line)
        if surface:
            xdg_to_wl[int(surface.group(1))] = int(surface.group(2))
        top = GET_TOPLEVEL.search(line)
        if top:
            roots.add(int(top.group(1)))
        popup = GET_POPUP.search(line)
        if popup:
            child, popup_id, parent = map(int, popup.groups())
            wl = xdg_to_wl.get(child, 0)
            object_id = len(objects) + 1
            parent_role = ("toplevel" if parent in roots else
                           "popup" if parent in active_child else "unknown")
            objects[object_id] = {
                "child": child, "popup": popup_id, "parent": parent, "wl": wl,
                "parent_role": parent_role,
                "candidate": bool(wl and parent_role == "toplevel"),
                "created_phase": phase if sensitive else "before_focus",
                "map_count": 0, "unmap_count": 0,
                "configure_count": 0, "grab_count": 0,
                "destroyed": "no",
            }
            active_child[child] = object_id
            popup_to_object[popup_id] = object_id
            if wl:
                wl_to_object[wl] = object_id
                mapped.setdefault(object_id, False)
            if sensitive:
                record_event("popup", phase, popup_id, object_id,
                             objects[object_id]["candidate"])
        attach = ATTACH.search(line)
        if attach:
            wl = int(attach.group(1))
            object_id = wl_to_object.get(wl)
            if object_id is not None:
                pending[object_id] = attach.group(2).strip() != "nil"
            elif wl in historical_candidate_wls:
                historical_pending[wl] = attach.group(2).strip() != "nil"
        commit = COMMIT.search(line)
        if commit:
            wl = int(commit.group(1))
            object_id = wl_to_object.get(wl)
            if object_id is not None and object_id in pending:
                maps = pending.pop(object_id)
                was_mapped = mapped.get(object_id, False)
                mapped[object_id] = maps
                if sensitive and maps:
                    objects[object_id]["map_count"] += 1
                    record_event("map", phase, objects[object_id]["popup"], object_id,
                                 objects[object_id]["candidate"])
                elif sensitive and was_mapped and not maps:
                    objects[object_id]["unmap_count"] += 1
                    relevant.add(object_id)
            elif wl in historical_pending:
                maps = historical_pending.pop(wl)
                if sensitive and maps:
                    record_event("map", phase)
        configure = CONFIGURE.search(line)
        if configure and sensitive:
            popup_id = int(configure.group(1))
            object_id = popup_to_object.get(popup_id, 0)
            if object_id:
                objects[object_id]["configure_count"] += 1
            record_event("configure", phase, popup_id, object_id,
                         bool(object_id and objects[object_id]["candidate"]))
        grab = GRAB.search(line)
        if grab and sensitive:
            popup_id = int(grab.group(1))
            object_id = popup_to_object.get(popup_id, 0)
            if object_id:
                objects[object_id]["grab_count"] += 1
            record_event("grab", phase, popup_id, object_id,
                         bool(object_id and objects[object_id]["candidate"]))
        if sensitive and "layer_shell" in line:
            layer_shell_objects += 1
        xdg_destroy = XDG_DESTROY.search(line)
        if xdg_destroy:
            child = int(xdg_destroy.group(1))
            roots.discard(child)
            object_id = active_child.pop(child, None)
            if object_id is not None:
                objects[object_id]["destroyed"] = "yes"
                if sensitive:
                    relevant.add(object_id)
                popup_to_object.pop(objects[object_id]["popup"], None)
                mapped.pop(object_id, None)
                wl = objects[object_id]["wl"]
                if wl:
                    historical_candidate_wls.add(wl)
                    if wl_to_object.get(wl) == object_id:
                        wl_to_object.pop(wl)
        wl_destroy = WL_DESTROY.search(line)
        if wl_destroy:
            wl = int(wl_destroy.group(1))
            object_id = wl_to_object.pop(wl, None)
            if object_id is not None:
                objects[object_id]["destroyed"] = "yes"
                if sensitive:
                    relevant.add(object_id)
                popup_to_object.pop(objects[object_id]["popup"], None)
                active_child.pop(objects[object_id]["child"], None)
                mapped.pop(object_id, None)
                historical_candidate_wls.add(wl)

    if not checked_boundary:
        mapped_at_focus = {child for child, state in mapped.items() if state}
        relevant.update(mapped_at_focus)
    relevant_objects = sorted(relevant)
    evidence: Dict[str, Union[str, int]] = {
        "candidate_popup_creations": counts["popup"],
        "candidate_popup_maps": counts["map"],
        "candidate_popup_configures": counts["configure"],
        "candidate_popup_grabs": counts["grab"],
        "candidate_popup_configures_matched": matched["configure"],
        "candidate_popup_configures_unmatched": unmatched["configure"],
        "candidate_popup_grabs_matched": matched["grab"],
        "candidate_popup_grabs_unmatched": unmatched["grab"],
        "candidate_objects_at_focus": len(mapped_at_focus),
        "candidate_objects_relevant": len(relevant_objects),
        "candidate_objects_recorded": min(len(relevant_objects), 16),
        "candidate_objects_omitted": max(0, len(relevant_objects) - 16),
        "candidate_objects_mapped_at_end": sum(bool(mapped.get(object_id, False))
                                                   for object_id in relevant_objects),
        "candidate_objects_destroyed_after_focus": sum(
            objects[object_id]["destroyed"] == "yes" for object_id in relevant_objects),
        "reduced_events_recorded": len(events),
        "reduced_events_omitted": omitted_events,
        "layer_shell_objects": layer_shell_objects,
        "observed_candidate_popup": "yes" if any(counts.values()) or mapped_at_focus else "no",
        "connection_identity": "unproven",
        "popup_attribution": "unproven",
        "focus_boundary_byte": after,
        "input_boundary_byte": input_after,
        "screenshot_boundary_byte": screenshot_after,
        "submit_boundary_byte": submit_after,
    }
    for phase in phases:
        for kind in kinds:
            evidence[f"{phase}_{kind}_events"] = phase_counts[(phase, kind)]
    for index, object_id in enumerate(relevant_objects[:16], 1):
        state = objects[object_id]
        evidence[f"candidate_object_{index}"] = (
            f"child:{state['child']},popup:{state['popup']},wl:{state['wl']},"
            f"parent:{state['parent']},parent_role:{state['parent_role']},"
            f"created_phase:{state['created_phase']},"
            f"mapped_at_focus:{'yes' if object_id in mapped_at_focus else 'no'},"
            f"map_count:{state['map_count']},unmap_count:{state['unmap_count']},"
            f"configure_count:{state['configure_count']},grab_count:{state['grab_count']},"
            f"destroyed:{state['destroyed']}")
    for index, event in enumerate(events, 1):
        evidence[f"reduced_event_{index}"] = event

    if mapped_at_focus:
        raise ReducedEvidenceError(
            "a candidate popup was already mapped at sensitive focus", evidence)
    if layer_shell_objects:
        raise ReducedEvidenceError(
            "a layer-shell object appeared after sensitive focus", evidence)
    if any(counts.values()):
        raise ReducedEvidenceError("popup activity appeared after sensitive focus", evidence)
    return evidence


def analyze_toplevel(raw: bytes, after: int) -> Dict[str, Union[str, int]]:
    """Require exactly one mapped ordinary toplevel in one client trace."""
    if after < 0 or after > len(raw):
        raise ValueError("trace boundary is outside the trace")
    xdg_to_wl = {}
    toplevels = {}
    pending = {}
    for position, line in _lines(raw):
        if position >= after and "layer_shell" in line:
            raise ValueError("a layer-shell object appeared during toplevel mapping")
        surface = GET_SURFACE.search(line)
        if surface:
            xdg_to_wl[int(surface.group(1))] = int(surface.group(2))
        top = GET_TOPLEVEL.search(line)
        if top:
            xdg = int(top.group(1))
            wl = xdg_to_wl.get(xdg)
            if wl is not None:
                toplevels[xdg] = [wl, False]
        attach = ATTACH.search(line) if position >= after else None
        if attach:
            pending[int(attach.group(1))] = attach.group(2).strip() != "nil"
        commit = COMMIT.search(line) if position >= after else None
        if commit:
            wl = int(commit.group(1))
            if wl in pending:
                for state in toplevels.values():
                    if state[0] == wl:
                        state[1] = pending[wl]
                pending.pop(wl)
        destroyed = XDG_DESTROY.search(line)
        if destroyed:
            toplevels.pop(int(destroyed.group(1)), None)
        destroyed = WL_DESTROY.search(line)
        if destroyed:
            wl = int(destroyed.group(1))
            for xdg, state in list(toplevels.items()):
                if state[0] == wl:
                    toplevels.pop(xdg)
    mapped = [(xdg, state[0]) for xdg, state in toplevels.items() if state[1]]
    if len(mapped) != 1:
        raise ValueError(f"expected exactly one mapped toplevel, got {len(mapped)}")
    return {
        "toplevel_xdg_surface": mapped[0][0],
        "toplevel_wl_surface": mapped[0][1],
        "mapped_toplevel_count": 1,
        "layer_shell_objects": 0,
    }

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", type=Path)
    ap.add_argument("--after-byte", required=True, type=int)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--no-popup", action="store_true")
    mode.add_argument("--lifecycle", action="store_true")
    mode.add_argument("--toplevel", action="store_true")
    ap.add_argument("--away-byte", type=int)
    ap.add_argument("--b-input-byte", type=int)
    ap.add_argument("--return-byte", type=int)
    ap.add_argument("--input-byte", type=int)
    ap.add_argument("--screenshot-byte", type=int)
    ap.add_argument("--submit-byte", type=int)
    args = ap.parse_args()
    raw = args.trace.read_bytes()
    if args.lifecycle:
        if args.away_byte is None or args.b_input_byte is None or args.return_byte is None:
            ap.error("--lifecycle requires --away-byte, --b-input-byte and --return-byte")
        result = analyze_lifecycle(raw, args.after_byte, args.away_byte,
                                   args.b_input_byte, args.return_byte)
    elif args.no_popup:
        try:
            result = analyze_no_popup(raw, args.after_byte, args.input_byte,
                                      args.screenshot_byte, args.submit_byte)
        except ReducedEvidenceError as exc:
            for key, value in exc.evidence.items():
                print(f"{key}={value}")
            raise SystemExit(str(exc))
    elif args.toplevel:
        result = analyze_toplevel(raw, args.after_byte)
    else:
        result = analyze(raw, args.after_byte)
    for key, value in result.items():
        print(f"{key}={value}")


if __name__ == "__main__":
    main()
