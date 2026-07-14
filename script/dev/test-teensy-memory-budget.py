#!/usr/bin/env python3

from teensy_memory_budget import (
    TeensyMemoryBudget,
    TeensyMemoryUsage,
    budget_violations,
    parse_teensy_size,
)


def main() -> int:
    output = """
teensy_size:    RAM1: variables:64384, code:300008, padding:27672   free for local variables:132224
teensy_size:    RAM2: variables:247968  free for malloc/new:276320
teensy_size:  EXTRAM: variables:4282368
"""
    usage = parse_teensy_size(output)
    assert usage == TeensyMemoryUsage(
        ram1_variables=64384,
        ram1_code=300008,
        ram1_padding=27672,
        ram1_free=132224,
        ram2_free=276320,
        extram_variables=4282368,
    )

    budget = TeensyMemoryBudget(
        ram1_min_free=114688,
        ram1_code_max=327680,
        ram2_min_free=196608,
        extram_capacity=8388608,
        extram_min_free=2097152,
    )
    assert budget_violations(usage, budget) == ()

    constrained = TeensyMemoryBudget(
        ram1_min_free=140000,
        ram1_code_max=294912,
        ram2_min_free=300000,
        extram_capacity=5000000,
        extram_min_free=1000000,
    )
    violations = budget_violations(usage, constrained)
    assert len(violations) == 4

    try:
        parse_teensy_size("RAM1 only")
    except ValueError:
        pass
    else:
        raise AssertionError("incomplete teensy_size output must be rejected")

    print("Teensy memory budget parser: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
