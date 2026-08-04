#!/usr/bin/env python3
"""Compatibility façade for the legacy Core memory-budget API.

Parsing is delegated to the shared v1 post-link provider. The old budget shape
remains only for the current Core PlatformIO adapter and expires at L-R11-02R.
"""

from dataclasses import dataclass
from pathlib import Path
import sys


_RELEASE_DIR = Path(__file__).resolve().parents[1] / "release"
if str(_RELEASE_DIR) not in sys.path:
    sys.path.insert(0, str(_RELEASE_DIR))

from teensy_post_link_gate_v1 import parse_teensy_size as _parse_v1  # noqa: E402


@dataclass(frozen=True)
class TeensyMemoryUsage:
    ram1_variables: int
    ram1_code: int
    ram1_padding: int
    ram1_free: int
    ram2_free: int
    extram_variables: int


@dataclass(frozen=True)
class TeensyMemoryBudget:
    ram1_min_free: int
    ram1_code_max: int
    ram2_min_free: int
    extram_capacity: int
    extram_min_free: int


def parse_teensy_size(output: str) -> TeensyMemoryUsage:
    usage = _parse_v1(output)
    return TeensyMemoryUsage(
        ram1_variables=usage.ram1_variables,
        ram1_code=usage.ram1_code,
        ram1_padding=usage.ram1_padding,
        ram1_free=usage.ram1_free,
        ram2_free=usage.ram2_free,
        extram_variables=usage.extram_variables,
    )


def budget_violations(
    usage: TeensyMemoryUsage,
    budget: TeensyMemoryBudget,
) -> tuple[str, ...]:
    violations: list[str] = []
    extram_free = budget.extram_capacity - usage.extram_variables
    if usage.ram1_free < budget.ram1_min_free:
        violations.append(
            f"RAM1 local headroom {usage.ram1_free}B is below {budget.ram1_min_free}B"
        )
    if usage.ram1_code > budget.ram1_code_max:
        violations.append(
            f"RAM1 ITCM code {usage.ram1_code}B exceeds "
            f"the {budget.ram1_code_max}B FlexRAM block limit"
        )
    if usage.ram2_free < budget.ram2_min_free:
        violations.append(
            f"RAM2 heap headroom {usage.ram2_free}B is below {budget.ram2_min_free}B"
        )
    if extram_free < budget.extram_min_free:
        violations.append(
            f"PSRAM static headroom {extram_free}B is below {budget.extram_min_free}B"
        )
    return tuple(violations)


def summary(usage: TeensyMemoryUsage, budget: TeensyMemoryBudget) -> str:
    extram_free = budget.extram_capacity - usage.extram_variables
    return (
        "Memory budget: "
        f"RAM1 free={usage.ram1_free}B/{budget.ram1_min_free}B min, "
        f"ITCM code={usage.ram1_code}B/{budget.ram1_code_max}B max, "
        f"RAM2 free={usage.ram2_free}B/{budget.ram2_min_free}B min, "
        f"PSRAM static free={extram_free}B/{budget.extram_min_free}B min"
    )


__all__ = (
    "TeensyMemoryUsage",
    "TeensyMemoryBudget",
    "parse_teensy_size",
    "budget_violations",
    "summary",
)
