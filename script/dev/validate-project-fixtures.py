#!/usr/bin/env python3
"""Validate project migration fixtures with the Core-built host file tool."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--tool",
        type=Path,
        default=None,
        help="Path to ms-core-file-tool. Defaults to build/core-native/ms-core-file-tool(.exe).",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Core repository root.",
    )
    return parser.parse_args()


def default_tool(repo_root: Path) -> Path:
    suffix = ".exe" if sys.platform.startswith("win") else ""
    return repo_root / "build" / "core-native" / f"ms-core-file-tool{suffix}"


def run_tool(tool: Path, *args: str) -> tuple[int, dict]:
    completed = subprocess.run(
        [str(tool), *args, "--json"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if not completed.stdout.strip():
        raise AssertionError(f"{tool} produced no JSON output: {completed.stderr}")
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(f"invalid JSON from {tool}: {completed.stdout}") from exc
    return completed.returncode, payload


def assert_report(
    label: str,
    exit_code: int,
    report: dict,
    *,
    status: str,
    load_status: str,
    overwrite_safe: bool,
    expected_exit: int,
) -> None:
    checks = {
        "status": status,
        "loadStatus": load_status,
        "overwriteSafe": overwrite_safe,
    }
    for key, expected in checks.items():
        actual = report.get(key)
        if actual != expected:
            raise AssertionError(f"{label}: {key} expected {expected!r}, got {actual!r}")
    if exit_code != expected_exit:
        raise AssertionError(f"{label}: exit expected {expected_exit}, got {exit_code}")


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    tool = (args.tool or default_tool(repo_root)).resolve()
    if not tool.is_file():
        raise AssertionError(f"ms-core-file-tool not found: {tool}")

    fixtures = repo_root / "test" / "fixtures" / "projects"
    stale = fixtures / "v1_0" / "stale-sequencer.mspj"
    current = fixtures / "v1_1" / "current-from-stale-sequencer.mspj"

    exit_code, report = run_tool(tool, "inspect", str(stale))
    assert_report(
        "v1_0/stale-sequencer.mspj",
        exit_code,
        report,
        status="partial",
        load_status="partial",
        overwrite_safe=False,
        expected_exit=2,
    )

    exit_code, report = run_tool(tool, "inspect", str(current))
    assert_report(
        "v1_1/current-from-stale-sequencer.mspj",
        exit_code,
        report,
        status="current",
        load_status="ok",
        overwrite_safe=True,
        expected_exit=0,
    )

    print("project fixture host-tool validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
