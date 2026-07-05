#!/usr/bin/env python3
"""Validate macro automation recording UX captures.

The workflow must prove the full user-visible path:
- a clean macro view has no automation-colored value arc,
- committing a recording colors only the recorded macro value arc,
- holding another macro without movement does not create an automation arc.
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path


CAPTURE_NAMES = {
    "ready": "50_macro_ready_screen.bmp",
    "recording": "650_macro_record_active_screen.bmp",
    "committed": "1250_macro_record_committed_screen.bmp",
    "no_motion": "2550_macro_record_no_motion_discarded_screen.bmp",
}

KNOB_CENTERS = (
    (57, 94),
    (124, 94),
    (191, 94),
    (257, 94),
    (57, 166),
    (124, 166),
    (191, 166),
    (257, 166),
)

ACTIVE_ARC_MIN_PIXELS = 80
INACTIVE_ARC_MAX_PIXELS = 25


def read_bmp(path: Path) -> list[list[tuple[int, int, int]]]:
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{path} is not a BMP file")

    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height_raw = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32):
        raise ValueError(f"{path} uses unsupported BMP depth {bpp}")

    top_down = height_raw < 0
    height = abs(height_raw)
    stride = ((width * bpp + 31) // 32) * 4
    pixels: list[list[tuple[int, int, int]]] = []
    bytes_per_pixel = bpp // 8

    for y in range(height):
        source_y = y if top_down else height - 1 - y
        row = offset + source_y * stride
        line: list[tuple[int, int, int]] = []
        for x in range(width):
            base = row + x * bytes_per_pixel
            b, g, r = data[base:base + 3]
            line.append((r, g, b))
        pixels.append(line)
    return pixels


def count_automation_arc_pixels(image: list[list[tuple[int, int, int]]],
                                center: tuple[int, int]) -> int:
    cx, cy = center
    count = 0
    height = len(image)
    width = len(image[0]) if height else 0

    for y in range(max(0, cy - 40), min(height, cy + 40)):
        for x in range(max(0, cx - 40), min(width, cx + 40)):
            distance = math.hypot(x - cx, y - cy)
            if not 25 <= distance <= 34:
                continue

            r, g, b = image[y][x]
            if r >= 170 and 70 <= g <= 150 and 70 <= b <= 150 and r - g >= 40 and r - b >= 40:
                count += 1
    return count


def fail(message: str) -> int:
    print(f"macro automation visual check failed: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: automation_record_visual_check.py <capture-directory>")

    capture_dir = Path(argv[1])
    captures = {}
    for key, name in CAPTURE_NAMES.items():
        path = capture_dir / name
        if not path.exists():
            return fail(f"missing capture {path}")
        captures[key] = [count_automation_arc_pixels(read_bmp(path), center)
                         for center in KNOB_CENTERS]

    if any(value > INACTIVE_ARC_MAX_PIXELS for value in captures["ready"]):
        return fail(f"ready capture is not clean: {captures['ready']}")

    if captures["recording"][0] < ACTIVE_ARC_MIN_PIXELS:
        return fail(f"recording capture does not show M1 automation: {captures['recording']}")
    if any(value > INACTIVE_ARC_MAX_PIXELS for value in captures["recording"][1:]):
        return fail(f"recording capture leaks automation to other macros: {captures['recording']}")

    if captures["committed"][0] < ACTIVE_ARC_MIN_PIXELS:
        return fail(f"committed capture does not show M1 automation: {captures['committed']}")
    if any(value > INACTIVE_ARC_MAX_PIXELS for value in captures["committed"][1:]):
        return fail(f"committed capture leaks automation to other macros: {captures['committed']}")

    if captures["no_motion"][0] < ACTIVE_ARC_MIN_PIXELS:
        return fail(f"no-motion capture lost committed M1 automation: {captures['no_motion']}")
    if captures["no_motion"][1] > INACTIVE_ARC_MAX_PIXELS:
        return fail(f"hold without movement created M2 automation: {captures['no_motion']}")
    if any(value > INACTIVE_ARC_MAX_PIXELS for value in captures["no_motion"][2:]):
        return fail(f"no-motion capture leaks automation to other macros: {captures['no_motion']}")

    print("macro automation visual check OK")
    for key, values in captures.items():
        print(f"{key}: {values}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
