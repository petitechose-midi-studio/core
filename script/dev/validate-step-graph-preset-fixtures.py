#!/usr/bin/env python3
"""Validate step graph preset fixtures with the Core-built host file tool."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
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


def read_hex_fixture(path: Path) -> bytes:
    parts: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        payload = line.split("#", 1)[0].strip()
        if payload:
            parts.extend(payload.split())
    return bytes.fromhex("".join(parts))


def run_tool(tool: Path, command: str, fixture: Path) -> tuple[int, dict]:
    completed = subprocess.run(
        [str(tool), command, str(fixture), "--json"],
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


def assert_current_root_micro(label: str, exit_code: int, report: dict) -> None:
    checks = {
        "fileKind": "step_graph_preset",
        "status": "ok",
        "rootContext": True,
        "rootValues": True,
        "stepNodeCount": 3,
        "sequenceCount": 1,
        "cycleSetCount": 0,
    }
    for key, expected in checks.items():
        actual = report.get(key)
        if actual != expected:
            raise AssertionError(f"{label}: {key} expected {expected!r}, got {actual!r}")
    flags = report.get("flags") or {}
    if flags.get("rootValues") is not True or flags.get("graphPayload") is not True:
        raise AssertionError(f"{label}: expected rootValues and graphPayload flags, got {flags}")
    if exit_code != 0:
        raise AssertionError(f"{label}: exit expected 0, got {exit_code}")


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    tool = (args.tool or default_tool(repo_root)).resolve()
    if not tool.is_file():
        raise AssertionError(f"ms-core-file-tool not found: {tool}")

    fixture_hex = (
        repo_root
        / "test"
        / "fixtures"
        / "step_graph_presets"
        / "v1"
        / "current-root-micro.msgp.hex.txt"
    )
    fixture_bytes = read_hex_fixture(fixture_hex)

    with tempfile.TemporaryDirectory() as tmp:
        fixture = Path(tmp) / "current-root-micro.msgp"
        fixture.write_bytes(fixture_bytes)

        for command in ("inspect-step-graph-preset", "validate-step-graph-preset"):
            exit_code, report = run_tool(tool, command, fixture)
            assert_current_root_micro(command, exit_code, report)

    print("step graph preset fixture host-tool validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
