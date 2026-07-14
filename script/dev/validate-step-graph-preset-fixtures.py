#!/usr/bin/env python3
"""Validate step graph preset fixtures with the Core-built host file tool."""

from __future__ import annotations

import argparse
import json
import os
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
    parser.add_argument(
        "--manager-probe",
        type=Path,
        default=None,
        help=(
            "Optional ms-manager-core Step Preset contract probe. The probe is "
            "run against the same real fixtures and its decoded report must "
            "match the Core file-tool JSON exactly."
        ),
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


def run_tool(
    tool: Path,
    command: str,
    fixture: Path,
    *extra_args: str,
) -> tuple[int, dict]:
    completed = subprocess.run(
        [str(tool), command, str(fixture), *extra_args, "--json"],
        text=True,
        encoding="utf-8",
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


def assert_rename_output_alias_refused(
    tool: Path,
    source: Path,
    output: Path,
    *,
    label: str,
) -> None:
    before = source.read_bytes()
    completed = subprocess.run(
        [
            str(tool),
            "rename-step-graph-preset",
            str(source),
            "--name",
            "Refused alias",
            "--out",
            str(output),
            "--json",
        ],
        text=True,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 64:
        raise AssertionError(
            f"{label}: same-file output must be refused with usage exit 64, "
            f"got {completed.returncode}: {completed.stdout}{completed.stderr}"
        )
    if source.read_bytes() != before:
        raise AssertionError(f"{label}: refused rename modified its input")


def run_manager_probe(
    probe: Path,
    tool: Path,
    command: str,
    fixture: Path,
    *extra_args: str,
) -> dict:
    operation = {
        "inspect-step-graph-preset": "inspect",
        "validate-step-graph-preset": "validate",
        "rename-step-graph-preset": "rename",
    }.get(command)
    if operation is None:
        raise AssertionError(f"unsupported manager probe command: {command}")
    completed = subprocess.run(
        [str(probe), str(tool), operation, str(fixture), *extra_args],
        text=True,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        raise AssertionError(
            f"manager Step Preset probe failed ({completed.returncode}): "
            f"{completed.stderr}"
        )
    if not completed.stdout.strip():
        raise AssertionError("manager Step Preset probe produced no JSON output")
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(
            f"invalid JSON from manager Step Preset probe: {completed.stdout}"
        ) from exc


def make_v2_fixture(v1: bytes) -> bytes:
    """Lift the stable v1 graph bytes into a strict, metadata-bearing v2 file."""
    if len(v1) < 21 or v1[4] != 1 or v1[6] != 21:
        raise AssertionError("unexpected v1 step graph preset fixture")

    technical_id = b"fixture-root-micro"
    semantic_name = "Fixture mélodique".encode("utf-8")
    metadata = bytearray(4 + 55 + 32)
    metadata[0] = 1  # scale_relative; all legacy graph nodes have relative flags.
    metadata[1:4] = bytes((0, 0, 0))
    metadata[4 : 4 + len(technical_id)] = technical_id
    metadata[4 + 55 : 4 + 55 + len(semantic_name)] = semantic_name

    base = bytearray(v1[:21])
    base[4] = 2
    base[6] = 21 + len(metadata)
    return bytes(base + metadata + v1[21:])


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


def assert_exact_json_contract(report: dict) -> None:
    expected = {
        "operation",
        "fileKind",
        "status",
        "compatibility",
        "formatVersion",
        "technicalId",
        "semanticName",
        "metadataDefaulted",
        "mixedPitchPolicy",
        "scalePolicy",
        "defaultScalePolicy",
        "sourceScale",
        "rootContext",
        "rootValues",
        "stepNodeCount",
        "sequenceCount",
        "cycleSetCount",
        "bytesWritten",
        "flags",
    }
    if set(report) != expected:
        raise AssertionError(
            f"JSON contract mismatch: expected {sorted(expected)}, got {sorted(report)}"
        )
    if set(report["sourceScale"]) != {"root", "type", "mode"}:
        raise AssertionError(f"invalid sourceScale contract: {report['sourceScale']}")
    if set(report["flags"]) != {
        "rootValues",
        "graphPayload",
        "overwrite",
        "mixedPitchPolicy",
    }:
        raise AssertionError(f"invalid flags contract: {report['flags']}")


def assert_asset_compatibility(
    label: str,
    report: dict,
    *,
    compatibility: str,
    format_version: int,
    metadata_defaulted: bool,
    scale_policy: str,
    default_scale_policy: str,
    technical_id: str,
    semantic_name: str,
) -> None:
    expected = {
        "compatibility": compatibility,
        "formatVersion": format_version,
        "metadataDefaulted": metadata_defaulted,
        "mixedPitchPolicy": False,
        "scalePolicy": scale_policy,
        "defaultScalePolicy": default_scale_policy,
        "sourceScale": {"root": 0, "type": 0, "mode": 0},
        "technicalId": technical_id,
        "semanticName": semantic_name,
    }
    for key, value in expected.items():
        if report.get(key) != value:
            raise AssertionError(
                f"{label}: {key} expected {value!r}, got {report.get(key)!r}"
            )
    if report["flags"]["mixedPitchPolicy"] is not False:
        raise AssertionError(f"{label}: flags.mixedPitchPolicy expected false")


def assert_manager_report_matches(
    probe: Path | None,
    tool: Path,
    command: str,
    fixture: Path,
    core_report: dict,
) -> None:
    if probe is None:
        return
    manager_report = run_manager_probe(probe, tool, command, fixture)
    if manager_report != core_report:
        raise AssertionError(
            f"{command}: Manager report differs from Core file-tool facts\n"
            f"Core: {core_report}\nManager: {manager_report}"
        )


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    tool = (args.tool or default_tool(repo_root)).resolve()
    if not tool.is_file():
        raise AssertionError(f"ms-core-file-tool not found: {tool}")
    manager_probe = args.manager_probe.resolve() if args.manager_probe else None
    if manager_probe is not None and not manager_probe.is_file():
        raise AssertionError(f"ms-manager Step Preset probe not found: {manager_probe}")

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
            assert_exact_json_contract(report)
            assert_asset_compatibility(
                command,
                report,
                compatibility="warning_legacy_defaulted",
                format_version=1,
                metadata_defaulted=True,
                scale_policy="chromatic",
                default_scale_policy="chromatic",
                technical_id="",
                semantic_name="",
            )
            assert_manager_report_matches(
                manager_probe, tool, command, fixture, report
            )

        v2_fixture = Path(tmp) / "current-root-micro.mssp"
        v2_bytes = make_v2_fixture(fixture_bytes)
        v2_fixture.write_bytes(v2_bytes)

        assert_rename_output_alias_refused(
            tool,
            v2_fixture,
            v2_fixture,
            label="rename exact input/output",
        )
        if sys.platform.startswith("win"):
            assert_rename_output_alias_refused(
                tool,
                v2_fixture,
                Path(str(v2_fixture).swapcase()),
                label="rename case-insensitive input/output",
            )
        hardlink = Path(tmp) / "current-root-micro-hardlink.mssp"
        os.link(v2_fixture, hardlink)
        assert_rename_output_alias_refused(
            tool,
            v2_fixture,
            hardlink,
            label="rename hardlink output alias",
        )

        for command in ("inspect-step-graph-preset", "validate-step-graph-preset"):
            exit_code, report = run_tool(tool, command, v2_fixture)
            label = f"{command} v2"
            assert_current_root_micro(label, exit_code, report)
            assert_exact_json_contract(report)
            assert_asset_compatibility(
                label,
                report,
                compatibility="ready",
                format_version=2,
                metadata_defaulted=False,
                scale_policy="scale_relative",
                default_scale_policy="scale_relative",
                technical_id="fixture-root-micro",
                semantic_name="Fixture mélodique",
            )
            assert_manager_report_matches(
                manager_probe, tool, command, v2_fixture, report
            )

        renamed = Path(tmp) / "renamed.mssp"
        renamed_name = 'Élan "Nord" \\ Ω'
        exit_code, report = run_tool(
            tool,
            "rename-step-graph-preset",
            v2_fixture,
            "--name",
            renamed_name,
            "--out",
            str(renamed),
        )
        assert_current_root_micro("rename-step-graph-preset", exit_code, report)
        assert_exact_json_contract(report)
        assert_asset_compatibility(
            "rename-step-graph-preset",
            report,
            compatibility="ready",
            format_version=2,
            metadata_defaulted=False,
            scale_policy="scale_relative",
            default_scale_policy="scale_relative",
            technical_id="fixture-root-micro",
            semantic_name=renamed_name,
        )
        if report["operation"] != "rename-step-graph-preset":
            raise AssertionError(f"unexpected operation: {report['operation']!r}")
        if report["semanticName"] != renamed_name:
            raise AssertionError(f"rename JSON escaping failed: {report['semanticName']!r}")
        if report["bytesWritten"] != len(v2_bytes):
            raise AssertionError(
                f"bytesWritten expected {len(v2_bytes)}, got {report['bytesWritten']}"
            )

        renamed_bytes = renamed.read_bytes()
        semantic_start = 21 + 4 + 55
        semantic_end = semantic_start + 32
        if len(renamed_bytes) != len(v2_bytes):
            raise AssertionError("rename changed encoded file size")
        if (
            renamed_bytes[:semantic_start] != v2_bytes[:semantic_start]
            or renamed_bytes[semantic_end:] != v2_bytes[semantic_end:]
        ):
            raise AssertionError("rename changed bytes outside semanticName")

        if manager_probe is not None:
            manager_renamed = Path(tmp) / "manager-renamed.mssp"
            manager_report = run_manager_probe(
                manager_probe,
                tool,
                "rename-step-graph-preset",
                v2_fixture,
                renamed_name,
                str(manager_renamed),
            )
            if manager_report != report:
                raise AssertionError(
                    "Manager rename report differs from Core file-tool facts\n"
                    f"Core: {report}\nManager: {manager_report}"
                )
            if manager_renamed.read_bytes() != renamed_bytes:
                raise AssertionError(
                    "Manager StepPresetTool rename output differs from Core file-tool bytes"
                )

        refused = Path(tmp) / "legacy-renamed.mssp"
        exit_code, report = run_tool(
            tool,
            "rename-step-graph-preset",
            fixture,
            "--name",
            "Legacy rename",
            "--out",
            str(refused),
        )
        if exit_code == 0 or report["status"] != "unsupported_version":
            raise AssertionError(f"v1 rename should be refused, got {report}")
        if refused.exists():
            raise AssertionError("refused v1 rename unexpectedly created output")

    print("step graph preset fixture host-tool validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
