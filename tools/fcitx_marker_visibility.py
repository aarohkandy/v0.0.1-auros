#!/usr/bin/env python3
"""Reduce two QMP frames to bounded second-editor marker evidence."""
import argparse
from pathlib import Path

MARKER_SHA256 = "665cfe6eae08731ed61132f9c8ee8bea8b87da01eba84cc2215180b235a46fa1"


def read_ppm(path: Path):
    data = path.read_bytes()
    parts = []
    offset = 0
    while len(parts) < 4:
        while data[offset:offset + 1].isspace():
            offset += 1
        if data[offset:offset + 1] == b"#":
            while data[offset:offset + 1] not in (b"\n", b""):
                offset += 1
            continue
        end = offset
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[offset:end])
        offset = end
    offset += 1
    return int(parts[1]), int(parts[2]), data[offset:]


def analyze(before: Path, after: Path):
    width, height, first = read_ppm(before)
    width2, height2, second = read_ppm(after)
    if (width, height) != (width2, height2):
        raise ValueError("frame dimensions changed")
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
    bands = []
    for y in dense_rows:
        if not bands or y > bands[-1][-1] + 4:
            bands.append([y])
        else:
            bands[-1].append(y)
    records = []
    for band in bands:
        points = [(x, y, delta) for x, y, delta in changed if y in set(band)]
        records.append((min(x for x, _, _ in points), min(band),
                        max(x for x, _, _ in points), max(band), points))
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("evidence", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.before, args.after)
    except ValueError:
        args.evidence.write_text("status=dimension-mismatch\n", encoding="ascii")
        raise SystemExit(1)
    args.evidence.write_text("".join(f"{key}={value}\n" for key, value in result.items()),
                             encoding="ascii")
    if result["status"] != "pass":
        raise SystemExit(1)
    print(result["click_x"], result["click_y"])


if __name__ == "__main__":
    main()
