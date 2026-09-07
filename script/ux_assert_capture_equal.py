"""Assert exact equality of two named BMP captures from an SDL UX workflow."""
import argparse
from pathlib import Path


def capture(directory: Path, label: str) -> bytes:
    matches = list(directory.glob(f"*_{label}_screen.bmp"))
    if len(matches) != 1:
        raise ValueError(f"Expected one capture for {label!r}, found {len(matches)}")
    return matches[0].read_bytes()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("left")
    parser.add_argument("right")
    args = parser.parse_args()
    try:
        if capture(args.directory, args.left) != capture(args.directory, args.right):
            parser.exit(1, f"Captures differ: {args.left} / {args.right}\n")
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Identical captures: {args.left} / {args.right}")
