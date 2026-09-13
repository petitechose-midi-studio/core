"""Assert exact equality of two named BMP captures from an SDL UX workflow."""
import argparse
import struct
from pathlib import Path


def capture(directory: Path, label: str, bottom_rows: int | None = None) -> bytes:
    matches = list(directory.glob(f"*_{label}_screen.bmp"))
    if len(matches) != 1:
        raise ValueError(f"Expected one capture for {label!r}, found {len(matches)}")
    data = matches[0].read_bytes()
    if bottom_rows is None:
        return data
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("Expected an SDL BMP capture")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or not 0 < bottom_rows <= height or bits not in (24, 32) or compression not in (0, 3):
        raise ValueError("Bottom rows require an uncompressed, bottom-up 24/32-bit BMP")
    stride = ((width * bits + 31) // 32) * 4
    if offset < 54 or len(data) < offset + stride * height:
        raise ValueError("Truncated BMP capture")
    # Retain the header so differing image dimensions/formats cannot compare equal.
    return data[:offset + stride * bottom_rows]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("--bottom-rows", type=int, help="Compare only the footer rows")
    args = parser.parse_args()
    try:
        if capture(args.directory, args.left, args.bottom_rows) != capture(args.directory, args.right, args.bottom_rows):
            parser.exit(1, f"Captures differ: {args.left} / {args.right}\n")
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Identical captures: {args.left} / {args.right}")
