#!/usr/bin/env python3

from teensy_diagnostics_placement import (
    diagnostics_placement_violations,
    normal_build_diagnostics_violations,
)


def main() -> int:
    valid = """
1611218610 46 T core::diagnostics::PerformanceReporter::update(unsigned long)
1611218112 92 T core::diagnostics::performanceReporter()
1610701172 1288 T core::diagnostics::logMemoryFootprint(char const*)
1610855692 1040 T core::state::diagnostics::configureDebugLabels(core::state::CoreState&)
1611042776 496 T oc::state::NotificationQueue::reportOverflow_(std::pair<void*, unsigned int>, char const*) const
539198464 6688 b core::diagnostics::(anonymous namespace)::reporterStorage
539205152 128 b core::diagnostics::(anonymous namespace)::memoryHighWaterStorage
1879048192 12408 b core::diagnostics::(anonymous namespace)::psramSpanTable
"""
    assert diagnostics_placement_violations(valid) == ()
    assert normal_build_diagnostics_violations("") == ()
    normal_violations = normal_build_diagnostics_violations(valid)
    assert any(
        "PerformanceReporter" in violation
        for violation in normal_violations
    )
    assert any(
        "memoryHighWaterStorage" in violation
        for violation in normal_violations
    )
    assert any(
        "psramSpanTable" in violation
        for violation in normal_violations
    )

    qualification_trace = """
1879066320 524288 b core::diagnostics::storage_qualification::(anonymous namespace)::traceStorage
1610720000 1068 t core::diagnostics::storage_qualification::(anonymous namespace)::updateCold(bool, bool)
"""
    qualification_violations = normal_build_diagnostics_violations(
        qualification_trace
    )
    assert any("traceStorage" in item for item in qualification_violations)
    assert any("updateCold" in item for item in qualification_violations)

    invalid = valid.replace(
        "1611218610 46 T core::diagnostics::PerformanceReporter::update",
        "267696 46 T core::diagnostics::PerformanceReporter::update",
    ).replace(
        "539198464 6688 b core::diagnostics::(anonymous namespace)::reporterStorage",
        "536946316 6688 b core::diagnostics::(anonymous namespace)::reporterStorage",
    ).replace(
        "539205152 128 b core::diagnostics::(anonymous namespace)::memoryHighWaterStorage",
        "536953004 128 b core::diagnostics::(anonymous namespace)::memoryHighWaterStorage",
    ).replace(
        "1879048192 12408 b core::diagnostics::(anonymous namespace)::psramSpanTable",
        "539205280 12407 b core::diagnostics::(anonymous namespace)::psramSpanTable",
    )
    violations = diagnostics_placement_violations(invalid)
    assert "diagnostics reporter methods must execute from Flash" in violations
    assert "diagnostics reporter samples and counters must stay in RAM2" in violations
    assert "diagnostics PSRAM span table must stay in EXTRAM" in violations
    assert "diagnostics PSRAM span table must be exactly 12408 bytes" in violations

    print("Teensy diagnostics placement parser: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
