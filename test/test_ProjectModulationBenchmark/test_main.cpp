#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <iostream>
#include <memory>

#include "validation/project/ProjectModulationBenchmark.hpp"

namespace {

namespace validation = core::validation::project;
namespace mod = core::state::modulation;

constexpr bool isDahdsrCase(
    validation::ProjectModulationBenchmarkCase benchmarkCase
) {
    return benchmarkCase == validation::ProjectModulationBenchmarkCase::ADSR ||
        benchmarkCase ==
            validation::ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK;
}

void proveCase(validation::ProjectModulationBenchmarkCase benchmarkCase) {
    auto workspace =
        std::make_unique<validation::ProjectModulationBenchmarkWorkspace>();
    assert(validation::prepareProjectModulationBenchmark(
        *workspace,
        benchmarkCase
    ));
    assert(workspace->plan.sourceCount == mod::PROJECT_MODULATOR_CAPACITY);
    assert(workspace->plan.bindingCount == mod::PROJECT_MODULATION_BINDING_CAPACITY);
    assert(workspace->plan.destinationCount ==
           mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY);
    if (benchmarkCase ==
        validation::ProjectModulationBenchmarkCase::RECORDED_SHAPE) {
        assert(workspace->domain.curves.recordCount ==
               mod::PROJECT_MODULATOR_CAPACITY);
        assert(workspace->domain.curves.pointCount ==
               mod::PROJECT_CURVE_POINT_CAPACITY);
    } else {
        assert(workspace->domain.curves.recordCount == 0U);
        assert(workspace->domain.curves.pointCount == 0U);
    }
    if (isDahdsrCase(benchmarkCase)) {
        assert(workspace->domain.modulation.triggerBindingCount ==
               mod::PROJECT_MODULATION_TRIGGER_CAPACITY);
        assert(workspace->triggers.count ==
               mod::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY);
        assert(workspace->plan.triggerRouteCount ==
               mod::PROJECT_MODULATOR_CAPACITY);
    } else {
        assert(workspace->triggers.count == 0U);
        assert(workspace->plan.triggerRouteCount == 0U);
    }
    if (benchmarkCase == validation::ProjectModulationBenchmarkCase::LFO) {
        bool hasFastSync = false;
        bool hasSlowSync = false;
        bool hasFastFree = false;
        bool hasSlowFree = false;
        for (uint16_t index = 0U; index < workspace->plan.sourceCount; ++index) {
            const auto& source = workspace->plan.sources[index];
            if (source.traits.lfo.timing == mod::ModulatorTimingMode::SYNC) {
                hasFastSync |= source.parameters.lfo.periodTicks == 12U;
                hasSlowSync |= source.parameters.lfo.periodTicks == 24576U;
            } else {
                hasFastFree |= source.parameters.lfo.freePeriodMs == 8U;
                hasSlowFree |= source.parameters.lfo.freePeriodMs == 32000U;
            }
        }
        assert(hasFastSync && hasSlowSync && hasFastFree && hasSlowFree);
    }
    if (benchmarkCase == validation::ProjectModulationBenchmarkCase::ADSR) {
        for (uint16_t track = 0U;
             track < mod::PROJECT_MODULATION_TRACK_COUNT;
             ++track) {
            const uint16_t start = workspace->plan.triggerBucketOffset[track];
            const uint16_t end = workspace->plan.triggerBucketOffset[track + 1U];
            assert(end - start ==
                   mod::PROJECT_MODULATOR_CAPACITY /
                       mod::PROJECT_MODULATION_TRACK_COUNT);
        }
    } else if (
        benchmarkCase ==
        validation::ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK
    ) {
        const uint16_t start = workspace->plan.triggerBucketOffset[0U];
        const uint16_t end = workspace->plan.triggerBucketOffset[1U];
        assert(start == 0U);
        assert(end - start == mod::PROJECT_MODULATOR_CAPACITY);
        for (uint16_t source = 0U;
             source < workspace->plan.sourceCount;
             ++source) {
            assert(workspace->plan.sources[source].trigger.track == 0U);
        }
        for (uint16_t event = 0U;
             event < workspace->triggers.count;
             ++event) {
            assert(workspace->triggers.events[event].trigger.track == 0U);
        }
    }

    const auto result = validation::runProjectModulationBenchmark(
        *workspace,
        benchmarkCase,
        2U,
        8U
    );
    assert(result.prepared);
    assert(result.evaluated);
    assert(result.iterations == 8U);
    assert(result.sourceCount == mod::PROJECT_MODULATOR_CAPACITY);
    assert(result.bindingCount == mod::PROJECT_MODULATION_BINDING_CAPACITY);
    assert(result.destinationCount ==
           mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY);
    assert(result.triggerEventCount == workspace->triggers.count);
    assert(result.triggerRouteCount == workspace->plan.triggerRouteCount);
    assert(result.workloadMatchesCase());
    if (benchmarkCase == validation::ProjectModulationBenchmarkCase::ADSR) {
        assert(
            result.triggerTestsPerFrame ==
            validation::PROJECT_MODULATION_BENCHMARK_DAHDSR_DISTRIBUTED_TRIGGER_TESTS_PER_FRAME
        );
    } else if (
        benchmarkCase ==
        validation::ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK
    ) {
        assert(
            result.triggerTestsPerFrame ==
            validation::PROJECT_MODULATION_BENCHMARK_DAHDSR_SHARED_TRACK_TRIGGER_TESTS_PER_FRAME
        );
    } else {
        assert(result.triggerTestsPerFrame == 0U);
    }
    assert(result.checksum != 0U);
    std::cout << "[PASS] "
              << validation::projectModulationBenchmarkCaseLabel(benchmarkCase)
              << " exact maximum graph\n";
}

void proveBudgetContractIncludesWorkload() {
    validation::ProjectModulationBenchmarkResult result{};
    result.benchmarkCase =
        validation::ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK;
    result.triggerEventCount =
        mod::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY;
    result.triggerRouteCount = mod::PROJECT_MODULATOR_CAPACITY;
    result.triggerTestsPerFrame =
        validation::PROJECT_MODULATION_BENCHMARK_DAHDSR_SHARED_TRACK_TRIGGER_TESTS_PER_FRAME;
    result.prepared = true;
    result.evaluated = true;
    result.averageUs =
        validation::PROJECT_MODULATION_BENCHMARK_AVERAGE_LIMIT_US - 1U;
    result.maximumUs =
        validation::PROJECT_MODULATION_BENCHMARK_MAXIMUM_LIMIT_US - 1U;
    assert(result.withinBudget());

    result.averageUs =
        validation::PROJECT_MODULATION_BENCHMARK_AVERAGE_LIMIT_US;
    assert(!result.withinBudget());
    result.averageUs =
        validation::PROJECT_MODULATION_BENCHMARK_AVERAGE_LIMIT_US - 1U;
    result.maximumUs =
        validation::PROJECT_MODULATION_BENCHMARK_MAXIMUM_LIMIT_US;
    assert(!result.withinBudget());
    result.maximumUs =
        validation::PROJECT_MODULATION_BENCHMARK_MAXIMUM_LIMIT_US - 1U;
    --result.triggerTestsPerFrame;
    assert(!result.withinBudget());
}

}  // namespace

int main() {
    static_assert(
        sizeof(validation::ProjectModulationBenchmarkWorkspace) == 187384U
    );
    proveCase(validation::ProjectModulationBenchmarkCase::LFO);
    proveCase(validation::ProjectModulationBenchmarkCase::RECORDED_SHAPE);
    proveCase(validation::ProjectModulationBenchmarkCase::ADSR);
    proveCase(
        validation::ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK
    );
    proveBudgetContractIncludesWorkload();
    std::cout << "Project modulation benchmark tests passed\n";
    return 0;
}
