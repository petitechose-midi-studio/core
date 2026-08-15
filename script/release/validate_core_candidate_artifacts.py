#!/usr/bin/env python3
"""Validate the exact Core firmware bundle promoted by the candidate workflow."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping, Sequence


EXPECTED_FILENAMES = frozenset(
    {
        "midi-studio-default-firmware.hex",
        "midi-studio-default-firmware.elf",
        "midi-studio-default-firmware.map",
        "midi-studio-default-post-link.json",
        "midi-studio-default-product-profile.json",
    }
)
EXPECTED_PROFILE_ID = "midi-studio-core-teensy41"
EXPECTED_PROFILE_VERSION = "release-2026.08.1"
EXPECTED_MEMORY_VECTOR = "release-current"


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be a JSON object")
    return value


def validate_artifacts(artifacts_dir: Path) -> None:
    if not artifacts_dir.is_dir():
        raise ValueError(f"candidate artifact directory is missing: {artifacts_dir}")
    actual = {path.name for path in artifacts_dir.iterdir() if path.is_file()}
    if actual != EXPECTED_FILENAMES:
        raise ValueError(
            "qualified firmware artifact mismatch: "
            f"expected={sorted(EXPECTED_FILENAMES)} actual={sorted(actual)}"
        )
    for filename in EXPECTED_FILENAMES:
        path = artifacts_dir / filename
        if path.stat().st_size == 0:
            raise ValueError(f"qualified firmware artifact is empty: {filename}")

    report_path = artifacts_dir / "midi-studio-default-post-link.json"
    profile_path = artifacts_dir / "midi-studio-default-product-profile.json"
    report = _object(json.loads(report_path.read_text(encoding="utf-8")), "report")
    profile = _object(
        json.loads(profile_path.read_text(encoding="utf-8")), "product profile"
    )
    profile_sha = hashlib.sha256(profile_path.read_bytes()).hexdigest()

    if report.get("schemaVersion") != 1 or report.get("gateInterfaceVersion") != 1:
        raise ValueError("unexpected post-link report schema/interface version")
    if report.get("passed") is not True or report.get("violations") != []:
        raise ValueError("qualified firmware post-link report is not green")
    if not isinstance(report.get("advisories"), list):
        raise ValueError("qualified firmware post-link advisories are missing")
    if report.get("profileSha256") != profile_sha:
        raise ValueError("post-link report/profile digest mismatch")

    identity = _object(profile.get("profile"), "product profile identity")
    expected_identity = (
        EXPECTED_PROFILE_ID,
        EXPECTED_PROFILE_VERSION,
        EXPECTED_MEMORY_VECTOR,
    )
    actual_identity = (
        identity.get("id"),
        identity.get("version"),
        identity.get("activeMemoryVector"),
    )
    if actual_identity != expected_identity:
        raise ValueError(
            f"unexpected qualified firmware product profile: {actual_identity!r}"
        )

    policy = _object(report.get("policy"), "post-link report policy")
    report_identity = (
        policy.get("profile_id"),
        policy.get("profile_version"),
        policy.get("vector_name"),
    )
    if report_identity != expected_identity:
        raise ValueError(
            f"post-link report policy/profile mismatch: {report_identity!r}"
        )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        validate_artifacts(args.artifacts_dir)
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print(f"ERROR: {error}")
        return 1
    print("qualified firmware evidence: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
