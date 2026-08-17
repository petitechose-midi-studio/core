#!/usr/bin/env python3
"""Convert MS Manager/Core UXR input records to a replayable .ux script."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable


def _payload(line: str) -> dict | None:
    line = line.strip()
    if not line:
        return None
    marker = line.find("UXR ")
    if marker >= 0:
        return json.loads(line[marker + 4 :])
    if line.startswith("{"):
        value = json.loads(line)
        return value if isinstance(value, dict) else None
    return None


def _milli(value: object) -> str:
    milli = int(value)
    sign = "-" if milli < 0 else ""
    whole, fraction = divmod(abs(milli), 1000)
    return f"{sign}{whole}.{fraction:03d}".rstrip("0").rstrip(".")


def ux_lines(lines: Iterable[str]) -> list[str]:
    actions: list[str] = []
    first_ms: int | None = None
    for line_number, line in enumerate(lines, 1):
        row = _payload(line)
        if not row or row.get("kind") != "input":
            continue

        try:
            now_ms = int(row["ms"])
            first_ms = now_ms if first_ms is None else first_ms
            due_ms = now_ms - first_ms
            if due_ms < 0:
                raise ValueError("timestamps are not monotonic")

            if "button" in row:
                state = {"press": "down", "release": "up"}[str(row["gesture"])]
                actions.append(f"{due_ms} button {row['button']} {state}")
                continue

            value_key = (
                "delta_milli" if row.get("value_kind") == "delta" else "value_milli"
            )
            actions.append(
                f"{due_ms} encoder_value {row['encoder']} {_milli(row[value_key])}"
            )
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"line {line_number}: invalid input record: {error}") from error

    if not actions:
        raise ValueError("recording contains no replayable input records")
    return actions


def convert(source: Path, destination: Path) -> int:
    actions = ux_lines(source.read_text(encoding="utf-8").splitlines())
    destination.write_text("\n".join(actions) + "\n", encoding="utf-8")
    return len(actions)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path, help="UXR or MS Manager NDJSON session")
    parser.add_argument("output", type=Path, help="destination .ux script")
    args = parser.parse_args()
    count = convert(args.recording, args.output)
    print(f"wrote {count} replayable inputs to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
