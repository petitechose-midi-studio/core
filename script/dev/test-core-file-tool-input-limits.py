#!/usr/bin/env python3
"""Exercise command-specific host-tool input limits at the process boundary."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path
from typing import Sequence


PROJECT_FILE_MAX_SIZE = 512 * 1024
# Wire-format maximum derived from SequencerGraphAssetCodec.hpp. Keeping the
# value here makes a format-limit change require an explicit behavior update.
STEP_GRAPH_PRESET_MAX_SIZE = 14_864
INPUT_ERROR_EXIT = 66


def create_file(path: Path, size: int) -> None:
    with path.open("wb") as stream:
        if size > 0:
            stream.seek(size - 1)
            stream.write(b"\0")


def run_tool(tool: Path, command: str, input_path: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(tool), command, str(input_path)],
        capture_output=True,
        check=False,
        encoding="utf-8",
        errors="replace",
        timeout=10,
    )


def assert_reaches_codec(
    result: subprocess.CompletedProcess[str],
    expected_report: str,
) -> None:
    assert result.returncode == 1, result
    assert expected_report in result.stdout, result
    assert "exceeds command limit" not in result.stderr, result


def assert_rejected_before_codec(
    result: subprocess.CompletedProcess[str],
    expected_limit: int,
) -> None:
    assert result.returncode == INPUT_ERROR_EXIT, result
    assert result.stdout == "", result
    assert f"exceeds command limit of {expected_limit} bytes" in result.stderr, result


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    tool = args.tool.resolve()
    if not tool.is_file():
        raise FileNotFoundError(f"host tool not found: {tool}")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)

        # Exercise the UTF-8 command-line/path conversion on every host build.
        project_at_limit = root / "projet-é-at-limit.mspj"
        create_file(project_at_limit, PROJECT_FILE_MAX_SIZE)
        assert_reaches_codec(
            run_tool(tool, "inspect", project_at_limit),
            "inspect: failed",
        )

        project_over_limit = root / "project-over-limit.mspj"
        create_file(project_over_limit, PROJECT_FILE_MAX_SIZE + 1)
        assert_rejected_before_codec(
            run_tool(tool, "inspect", project_over_limit),
            PROJECT_FILE_MAX_SIZE,
        )

        preset_at_limit = root / "préréglage-at-limit.mssp"
        create_file(preset_at_limit, STEP_GRAPH_PRESET_MAX_SIZE)
        assert_reaches_codec(
            run_tool(tool, "inspect-step-graph-preset", preset_at_limit),
            "inspect-step-graph-preset: invalid_format",
        )

        preset_over_limit = root / "preset-over-limit.mssp"
        create_file(preset_over_limit, STEP_GRAPH_PRESET_MAX_SIZE + 1)
        assert_rejected_before_codec(
            run_tool(tool, "inspect-step-graph-preset", preset_over_limit),
            STEP_GRAPH_PRESET_MAX_SIZE,
        )

    print("Core file tool input limits: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
