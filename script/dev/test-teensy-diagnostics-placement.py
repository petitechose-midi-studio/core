#!/usr/bin/env python3

from teensy_diagnostics_placement import diagnostics_placement_violations


def main() -> int:
    valid = """
1611218610 46 T core::diagnostics::PerformanceReporter::update(unsigned long)
1611218112 92 T core::diagnostics::performanceReporter()
1610701172 1288 T core::diagnostics::logMemoryFootprint(char const*)
1610855692 1040 T core::state::diagnostics::configureDebugLabels(core::state::CoreState&)
1611042776 496 T oc::state::NotificationQueue::reportOverflow_(std::pair<void*, unsigned int>, char const*) const
539198464 6688 b core::diagnostics::(anonymous namespace)::reporterStorage
"""
    assert diagnostics_placement_violations(valid) == ()

    invalid = valid.replace(
        "1611218610 46 T core::diagnostics::PerformanceReporter::update",
        "267696 46 T core::diagnostics::PerformanceReporter::update",
    ).replace(
        "539198464 6688 b core::diagnostics::(anonymous namespace)::reporterStorage",
        "536946316 6688 b core::diagnostics::(anonymous namespace)::reporterStorage",
    )
    violations = diagnostics_placement_violations(invalid)
    assert "diagnostics reporter methods must execute from Flash" in violations
    assert "diagnostics reporter samples and counters must stay in RAM2" in violations

    print("Teensy diagnostics placement parser: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
