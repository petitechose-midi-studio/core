#!/usr/bin/env python3

from dataclasses import dataclass
import re


_RAM1_RE = re.compile(
    r"RAM1:\s+variables:(\d+),\s+code:(\d+),\s+padding:(\d+)"
    r"\s+free for local variables:(\d+)"
)
_RAM2_RE = re.compile(r"RAM2:.*free for malloc/new:(\d+)")
_EXTRAM_RE = re.compile(r"EXTRAM: variables:(\d+)")


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
    ram1 = _RAM1_RE.search(output)
    ram2 = _RAM2_RE.search(output)
    extram = _EXTRAM_RE.search(output)
    if ram1 is None or ram2 is None or extram is None:
        raise ValueError("teensy_size output does not contain RAM1, RAM2, and EXTRAM usage")
    return TeensyMemoryUsage(
        ram1_variables=int(ram1.group(1)),
        ram1_code=int(ram1.group(2)),
        ram1_padding=int(ram1.group(3)),
        ram1_free=int(ram1.group(4)),
        ram2_free=int(ram2.group(1)),
        extram_variables=int(extram.group(1)),
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
