#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::validation::project {

enum class ProjectModulationBenchmarkCase : uint8_t {
    LFO = 0,
    RECORDED_SHAPE,
    ADSR,
    DAHDSR_SHARED_TRACK,
};

inline constexpr uint32_t PROJECT_MODULATION_BENCHMARK_WARMUP_FRAMES = 64U;
inline constexpr uint32_t PROJECT_MODULATION_BENCHMARK_MEASURED_FRAMES = 2048U;
inline constexpr uint32_t PROJECT_MODULATION_BENCHMARK_AVERAGE_LIMIT_US = 500U;
inline constexpr uint32_t PROJECT_MODULATION_BENCHMARK_MAXIMUM_LIMIT_US = 1000U;
inline constexpr uint32_t
    PROJECT_MODULATION_BENCHMARK_DAHDSR_DISTRIBUTED_TRIGGER_TESTS_PER_FRAME =
        static_cast<uint32_t>(
            core::state::modulation::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY
        ) *
        (core::state::modulation::PROJECT_MODULATOR_CAPACITY /
         core::state::modulation::PROJECT_MODULATION_TRACK_COUNT);
inline constexpr uint32_t
    PROJECT_MODULATION_BENCHMARK_DAHDSR_SHARED_TRACK_TRIGGER_TESTS_PER_FRAME =
        static_cast<uint32_t>(
            core::state::modulation::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY
        ) * core::state::modulation::PROJECT_MODULATOR_CAPACITY;

static_assert(
    PROJECT_MODULATION_BENCHMARK_DAHDSR_DISTRIBUTED_TRIGGER_TESTS_PER_FRAME ==
    2048U
);
static_assert(
    PROJECT_MODULATION_BENCHMARK_DAHDSR_SHARED_TRACK_TRIGGER_TESTS_PER_FRAME ==
    32768U
);

/**
 * Benchmark-only storage. The hardware entry point places this complete object
 * in EXTMEM; it is never part of an ordinary product image.
 */
struct ProjectModulationBenchmarkWorkspace {
    core::state::modulation::ProjectControlDomainState domain{};
    core::state::modulation::ProjectModulationRuntimePlan plan{};
    core::state::modulation::ProjectControlRuntimeState runtime{};
    std::array<
        float,
        core::state::modulation::PROJECT_MODULATOR_CAPACITY
    > sourceValues{};
    std::array<
        core::state::modulation::ProjectLogicalMacroBaseInput,
        core::state::modulation::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY
    > bases{};
    core::state::modulation::ProjectModulationTriggerFrame triggers{};
};

struct ProjectModulationBenchmarkResult {
    ProjectModulationBenchmarkCase benchmarkCase =
        ProjectModulationBenchmarkCase::LFO;
    uint32_t iterations = 0;
    uint32_t averageUs = 0;
    uint32_t maximumUs = 0;
    uint32_t checksum = 0;
    uint32_t triggerTestsPerFrame = 0;
    uint16_t sourceCount = 0;
    uint16_t bindingCount = 0;
    uint16_t destinationCount = 0;
    uint16_t triggerEventCount = 0;
    uint16_t triggerRouteCount = 0;
    bool prepared = false;
    bool evaluated = false;

    [[nodiscard]] bool workloadMatchesCase() const {
        const bool dahdsr =
            benchmarkCase == ProjectModulationBenchmarkCase::ADSR ||
            benchmarkCase ==
                ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK;
        const uint16_t expectedEventCount = dahdsr
            ? core::state::modulation::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY
            : 0U;
        const uint16_t expectedRouteCount = dahdsr
            ? core::state::modulation::PROJECT_MODULATOR_CAPACITY
            : 0U;
        uint32_t expectedTests = 0U;
        if (benchmarkCase == ProjectModulationBenchmarkCase::ADSR) {
            expectedTests =
                PROJECT_MODULATION_BENCHMARK_DAHDSR_DISTRIBUTED_TRIGGER_TESTS_PER_FRAME;
        } else if (
            benchmarkCase ==
            ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK
        ) {
            expectedTests =
                PROJECT_MODULATION_BENCHMARK_DAHDSR_SHARED_TRACK_TRIGGER_TESTS_PER_FRAME;
        }
        return triggerEventCount == expectedEventCount &&
            triggerRouteCount == expectedRouteCount &&
            triggerTestsPerFrame == expectedTests;
    }

    [[nodiscard]] bool withinBudget() const {
        return prepared && evaluated && workloadMatchesCase() &&
            averageUs < PROJECT_MODULATION_BENCHMARK_AVERAGE_LIMIT_US &&
            maximumUs < PROJECT_MODULATION_BENCHMARK_MAXIMUM_LIMIT_US;
    }
};

[[nodiscard]] const char* projectModulationBenchmarkCaseLabel(
    ProjectModulationBenchmarkCase benchmarkCase
);

/** Builds and compiles one exact 128-source/512-binding/128-destination graph. */
[[nodiscard]] bool prepareProjectModulationBenchmark(
    ProjectModulationBenchmarkWorkspace& workspace,
    ProjectModulationBenchmarkCase benchmarkCase
);

/** Measures only the allocation-free product evaluator after bounded warmup. */
[[nodiscard]] ProjectModulationBenchmarkResult runProjectModulationBenchmark(
    ProjectModulationBenchmarkWorkspace& workspace,
    ProjectModulationBenchmarkCase benchmarkCase,
    uint32_t warmupFrames = PROJECT_MODULATION_BENCHMARK_WARMUP_FRAMES,
    uint32_t measuredFrames = PROJECT_MODULATION_BENCHMARK_MEASURED_FRAMES
);

// DAHDSR's accepted-note masks and smoothing state remain in this EXTMEM-only
// benchmark slab; the delta is deliberately visible here.
static_assert(sizeof(ProjectModulationBenchmarkWorkspace) == 187384U);
static_assert(std::is_trivially_copyable_v<ProjectModulationBenchmarkWorkspace>);

}  // namespace core::validation::project
