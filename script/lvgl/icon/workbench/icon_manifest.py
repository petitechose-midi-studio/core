#!/usr/bin/env python3
"""Load the active icon manifest from the approved documentation registry."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


HERE = Path(__file__).resolve().parent
REVIEW_DIR = (
    HERE.parents[6]
    / "petitechose-audio-docs"
    / "review"
    / "controller-ui-convergence-2026-08-17"
)
REGISTRY_PATH = REVIEW_DIR / "icon-placement-registry-2026-08-18.md"
SHEETS_DIR = REVIEW_DIR / "evidence" / "icon-system"
TEMPORARY_PNG_DIR = SHEETS_DIR / "temporary-png"
SHEETS = {
    "A": "proposal-actions-v1.png",
    "M": "proposal-musical-v1.png",
    "D": "proposal-drum-midi-v1.png",
    "S": "proposal-system-status-v1.png",
    "H": "proposal-hotspots-v1.png",
    "P": "proposal-project-views-v1.png",
    "R": "proposal-arbitrations-v1.png",
}
FAMILY_LABELS = {
    "A": "Actions",
    "M": "Musical",
    "D": "Drum and MIDI",
    "S": "System and status",
    "H": "Hotspots",
    "P": "Project and views",
    "R": "Arbitrations",
    "G": "Canonical geometry",
}
FAMILY_ORDER = tuple(FAMILY_LABELS)
EXCLUDED = {"action_apply"}
EXPECTED_ACTIVE_COUNT = 71
ROW_PATTERN = re.compile(r"temporary-png/([a-z0-9_]+)\.png")
REFERENCE_PATTERN = re.compile(r"\|\s*([AMDSHPRG]\d{2})\s*\|")


@dataclass(frozen=True)
class IconSpec:
    name: str
    reference: str

    @property
    def family(self) -> str:
        return self.reference[0]

    @property
    def cell(self) -> int:
        return int(self.reference[1:])

    @property
    def sheet_name(self) -> str | None:
        return SHEETS.get(self.family)


def load_specs(registry_path: Path = REGISTRY_PATH) -> tuple[IconSpec, ...]:
    if not registry_path.exists():
        raise RuntimeError(f"icon registry not found: {registry_path}")

    found: dict[str, str] = {}
    for line_number, line in enumerate(registry_path.read_text(encoding="utf-8").splitlines(), 1):
        names = ROW_PATTERN.findall(line)
        references = REFERENCE_PATTERN.findall(line)
        if len(names) != 1 or not references:
            continue
        name, reference = names[0], references[-1]
        previous = found.get(name)
        if previous is not None and previous != reference:
            raise RuntimeError(
                f"conflicting references for {name} at line {line_number}: {previous} vs {reference}"
            )
        found[name] = reference

    active = [IconSpec(name, reference) for name, reference in found.items() if name not in EXCLUDED]
    active.sort(key=lambda spec: (FAMILY_ORDER.index(spec.family), spec.cell, spec.name))

    if len(active) != EXPECTED_ACTIVE_COUNT:
        raise RuntimeError(f"expected {EXPECTED_ACTIVE_COUNT} active icons, found {len(active)}")
    references = [spec.reference for spec in active]
    if len(set(references)) != len(references):
        raise RuntimeError("the active icon registry contains duplicate source references")
    unknown = sorted({spec.family for spec in active} - set(FAMILY_ORDER))
    if unknown:
        raise RuntimeError(f"unknown icon reference families: {', '.join(unknown)}")
    return tuple(active)


def grouped_specs(specs: tuple[IconSpec, ...]) -> tuple[tuple[str, tuple[IconSpec, ...]], ...]:
    return tuple(
        (family, tuple(spec for spec in specs if spec.family == family))
        for family in FAMILY_ORDER
        if any(spec.family == family for spec in specs)
    )
