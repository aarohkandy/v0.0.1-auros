#!/usr/bin/env python3
"""Reduce two QMP frames to bounded second-editor marker evidence."""
import argparse
from pathlib import Path

MARKER_SHA256 = "665cfe6eae08731ed61132f9c8ee8bea8b87da01eba84cc2215180b235a46fa1"
MAX_DIMENSION = 8192
MAX_HEADER = 4096
MAX_FRAME_BYTES = MAX_DIMENSION * MAX_DIMENSION * 3 + MAX_HEADER


class FrameError(ValueError):
    def __init__(self, code):
        super().__init__(code)
        self.code = code


def read_ppm(path: Path):
    try:
        size = path.stat().st_size
    except OSError as exc:
        raise FrameError("unreadable") from exc
    if size <= 0 or size > MAX_FRAME_BYTES:
        raise FrameError("invalid-size")
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise FrameError("unreadable") from exc
    parts = []
    offset = 0
    while len(parts) < 4:
        while offset < len(data) and data[offset:offset + 1].isspace():
            offset += 1
        if offset >= len(data) or offset > MAX_HEADER:
            raise FrameError("malformed-header")
        if data[offset:offset + 1] == b"#":
            while offset < len(data) and data[offset:offset + 1] != b"\n":
                offset += 1
            continue
        end = offset
        while end < len(data) and not data[end:end + 1].isspace():
            end += 1
        if end == len(data) or end > MAX_HEADER:
            raise FrameError("malformed-header")
        parts.append(data[offset:end])
        offset = end
    if parts[0] != b"P6" or parts[3] != b"255":
        raise FrameError("unsupported-header")
    try:
        width, height = int(parts[1]), int(parts[2])
    except ValueError as exc:
        raise FrameError("malformed-dimensions") from exc
    if not 0 < width <= MAX_DIMENSION or not 0 < height <= MAX_DIMENSION:
        raise FrameError("invalid-dimensions")
    if data[offset:offset + 2] == b"\r\n":
        offset += 2
    elif data[offset:offset + 1].isspace():
        offset += 1
    else:
        raise FrameError("missing-payload-separator")
    payload = data[offset:]
    if len(payload) != width * height * 3:
        raise FrameError("invalid-payload-length")
    return width, height, payload


def analyze(before: Path, after: Path):
    width, height, first = read_ppm(before)
    width2, height2, second = read_ppm(after)
    if (width, height) != (width2, height2):
        raise FrameError("dimension-mismatch")
    changed = []
    total = 0
    rows = {}
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            offset = (y * width + x) * 3
            total += 1
            delta = sum(abs(first[offset + i] - second[offset + i]) for i in range(3))
            if delta > 24:
                changed.append((x, y, delta))
                rows.setdefault(y, []).append(x)
    dense_rows = sorted(y for y, values in rows.items() if len(values) >= 5)
    row_records = []
    for y in dense_rows:
        points = [(x, row_y, delta) for x, row_y, delta in changed if row_y == y]
        row_records.append((min(x for x, _, _ in points), y,
                            max(x for x, _, _ in points), points))
    steps = []
    for record in row_records:
        width = record[2] - record[0]
        if (steps and record[1] <= steps[-1]["last_y"] + 4
                and abs(record[0] - steps[-1]["left"]) <= 24
                and abs(width - steps[-1]["width"]) <= 8):
            steps[-1]["last_y"] = record[1]
            steps[-1]["points"].extend(record[3])
        else:
            steps.append({"left": record[0], "top": record[1], "last_y": record[1],
                          "right": record[2], "width": width, "points": list(record[3])})
    records = []
    for step in steps:
        records.append((step["left"], step["top"], step["right"],
                        step["last_y"], step["points"]))
    paths = []
    for index, record in enumerate(records):
        best = [index]
        width = record[2] - record[0]
        for previous in range(index):
            old = records[previous]
            old_width = old[2] - old[0]
            gap = record[1] - old[1]
            growth = width - old_width
            if (8 <= gap <= 48 and abs(record[0] - old[0]) <= 24
                    and 4 <= growth <= 80 and len(paths[previous]) + 1 > len(best)):
                best = paths[previous] + [index]
        paths.append(best)
    pattern = max(paths, key=len) if paths else []
    dense = [point for index in pattern for point in records[index][4]]
    if dense:
        bounds = (min(x for x, _, _ in dense), min(y for _, y, _ in dense),
                  max(x for x, _, _ in dense), max(y for _, y, _ in dense))
    else:
        bounds = (-1, -1, -1, -1)
    passed = (len(changed) * 100 >= total * 3 and len(pattern) >= 20
              and bounds[2] - bounds[0] >= 200 and bounds[3] - bounds[1] >= 200)
    center = ((bounds[0] + bounds[2]) / 2, (bounds[1] + bounds[3]) / 2)
    click = min(dense, key=lambda point:
                (point[0] - center[0]) ** 2 + (point[1] - center[1]) ** 2) \
        if dense else (-1, -1, 0)
    return {
        "status": "pass" if passed else "fail",
        "marker_template_sha256": MARKER_SHA256,
        "changed_samples": len(changed),
        "total_samples": total,
        "threshold_percent": 3,
        "marker_pattern_steps": len(pattern),
        "marker_bbox_left": bounds[0],
        "marker_bbox_top": bounds[1],
        "marker_bbox_right": bounds[2],
        "marker_bbox_bottom": bounds[3],
        "click_x": click[0],
        "click_y": click[1],
        "click_pixel_delta": click[2],
    }


def generic_click(before: Path, after: Path):
    width, height, first = read_ppm(before)
    width2, height2, second = read_ppm(after)
    if (width, height) != (width2, height2):
        raise FrameError("dimension-mismatch")
    changed = []
    total = 0
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            offset = (y * width + x) * 3
            total += 1
            if sum(abs(first[offset + i] - second[offset + i]) for i in range(3)) > 24:
                changed.append((x, y))
    if len(changed) * 100 < total * 3:
        raise FrameError("insufficient-change")
    return ((min(x for x, _ in changed) + max(x for x, _ in changed)) // 2,
            (min(y for _, y in changed) + max(y for _, y in changed)) // 2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("evidence", type=Path, nargs="?")
    parser.add_argument("--generic", action="store_true")
    args = parser.parse_args()
    if args.generic:
        if args.evidence is not None:
            parser.error("generic mode does not write marker evidence")
        try:
            print(*generic_click(args.before, args.after))
        except FrameError:
            raise SystemExit(1)
        return
    if args.evidence is None:
        parser.error("marker mode requires an evidence path")
    try:
        result = analyze(args.before, args.after)
    except FrameError as exc:
        args.evidence.write_text(f"status=invalid-frame\nframe_error={exc.code}\n",
                                 encoding="ascii")
        raise SystemExit(1)
    args.evidence.write_text("".join(f"{key}={value}\n" for key, value in result.items()),
                             encoding="ascii")
    if result["status"] != "pass":
        raise SystemExit(1)
    print(result["click_x"], result["click_y"])


if __name__ == "__main__":
    main()
