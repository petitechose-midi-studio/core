"""Assert exact equality of two named BMP captures from an SDL UX workflow."""
import argparse
import struct
from pathlib import Path


def capture(directory: Path, label: str, region: list[int] | None = None) -> bytes:
    matches = list(directory.glob(f"*_{label}_screen.bmp"))
    if len(matches) != 1:
        raise ValueError(f"Expected one capture for {label!r}, found {len(matches)}")
    data = matches[0].read_bytes()
    if region is None:
        return data
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("Expected an SDL BMP capture")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or height <= 0 or bits not in (24, 32) or compression not in (0, 3):
        raise ValueError("Regions require an uncompressed, bottom-up 24/32-bit BMP")
    x, y, region_width, region_height = region
    if x < 0 or y < 0 or region_width <= 0 or region_height <= 0 or x + region_width > width or y + region_height > height:
        raise ValueError("Comparison region is outside the capture")
    stride = ((width * bits + 31) // 32) * 4
    if offset < 54 or len(data) < offset + stride * height:
        raise ValueError("Truncated BMP capture")
    # Retain the header so differing image dimensions/formats cannot compare equal.
    pixel_bytes = bits // 8
    rows = []
    for row in range(y, y + region_height):
        start = offset + (height - 1 - row) * stride + x * pixel_bytes
        rows.append(data[start:start + region_width * pixel_bytes])
    return data[:offset] + b"".join(rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("--region", nargs=4, type=int, metavar=("X", "Y", "WIDTH", "HEIGHT"),
                        help="Compare a rectangle, measured from the top-left pixel")
    args = parser.parse_args()
    try:
        if capture(args.directory, args.left, args.region) != capture(args.directory, args.right, args.region):
            parser.exit(1, f"Captures differ: {args.left} / {args.right}\n")
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Identical captures: {args.left} / {args.right}")
