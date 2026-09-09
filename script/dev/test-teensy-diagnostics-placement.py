#!/usr/bin/env python3

from teensy_diagnostics_placement import (
    diagnostics_placement_violations,
    hardware_benchmark_placement_violations,
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

    bench = """
1611218610 46 T core::validation::benchmark::HardwareBenchmarkEndpoint::begin()
251000 512 T core::validation::benchmark::HardwareBenchmarkEndpoint::advance(unsigned long)
250000 128 T core::validation::benchmark::beginMetrics(unsigned long)
1611218112 92 T core::diagnostics::beginMemoryFootprintTracking()
1611218210 192 T core::diagnostics::dynamicMemorySnapshot()
260000 92 T oc::ui::lvgl::benchmark::beginFrame()
261000 192 T oc::ui::lvgl::benchmark::endFrame(unsigned long, unsigned long)
539198464 4224 b core::validation::benchmark::(anonymous namespace)::metrics
539205152 128 b core::diagnostics::(anonymous namespace)::memoryHighWaterStorage
1879048192 12408 b core::diagnostics::(anonymous namespace)::psramSpanTable
"""
    assert hardware_benchmark_placement_violations(bench) == ()
    scoped_sd = bench + """
1610613100 12 T core::validation::benchmark::BenchFileSystem::init()
1610613200 12 T oc::hal::teensy::SDFileSystemBackend::init()
"""
    assert hardware_benchmark_placement_violations(scoped_sd, filesystem=True) == ()
    assert hardware_benchmark_placement_violations(scoped_sd)
    assert hardware_benchmark_placement_violations(bench, filesystem=True)
    assert hardware_benchmark_placement_violations(
        scoped_sd + "1610613300 12 T oc::hal::teensy::SDCardBackend::init()\n", filesystem=True)
    assert hardware_benchmark_placement_violations("")
    assert normal_build_diagnostics_violations(bench)
    # Missing or misplaced measurements cannot silently turn into a passing
    # benchmark; crossing the region end is as invalid as a wrong start.
    for line in bench.strip().splitlines():
        assert hardware_benchmark_placement_violations(bench.replace(line, ""))
    bad_metrics = bench.replace("539198464 4224", "539492344 4224")
    assert any("fit wholly in RAM2" in item
               for item in hardware_benchmark_placement_violations(bad_metrics))
    bad_spans = bench.replace("1879048192 12408", "1887436792 12408")
    assert any("fit wholly in EXTRAM" in item
               for item in hardware_benchmark_placement_violations(bad_spans))
    assert any("exactly 12408" in item for item in hardware_benchmark_placement_violations(
        bench.replace("1879048192 12408", "1879048192 12407")))
    for marker in (
        "oc::hal::teensy::SDCardBackend::init()",
        "oc::hal::teensy::SDFileSystemBackend::init()",
        "FatFormatter::makeFat32()",
        "StorageRecoveryRuntimeManager::update()",
        "storageRecovery",
        "core::persistence::ProjectSessionRestoreService::restore()",
        "core::persistence::ProjectSessionAutosaveService::update()",
        "core::diagnostics::PerformanceReporter::update()",
        "core::diagnostics::performanceReporter()",
        "reporterStorage",
        "void oc::log::detail::log<int>(char const*, char const*, char const*, int&&)",
        "oc::log::detail::formatImpl(char const*)",
        "oc::log::ScopeTimer::~ScopeTimer()",
    ):
        assert any("forbidden symbol" in item for item in hardware_benchmark_placement_violations(
            bench + f"1610613000 12 T {marker}\n"))

    print("Teensy diagnostics and RAM-only benchmark placement parsers: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
