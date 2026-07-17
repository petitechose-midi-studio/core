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
    if (benchmarkCase == validation::ProjectModulationBenchmarkCase::ADSR) {
        assert(workspace->domain.modulation.triggerBindingCount ==
               mod::PROJECT_MODULATION_TRIGGER_CAPACITY);
        assert(workspace->triggers.count ==
               mod::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY);
    } else {
        assert(workspace->triggers.count == 0U);
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
    assert(result.checksum != 0U);
    std::cout << "[PASS] "
              << validation::projectModulationBenchmarkCaseLabel(benchmarkCase)
              << " exact maximum graph\n";
}

}  // namespace

int main() {
    static_assert(
        sizeof(validation::ProjectModulationBenchmarkWorkspace) == 185568U
    );
    proveCase(validation::ProjectModulationBenchmarkCase::LFO);
    proveCase(validation::ProjectModulationBenchmarkCase::RECORDED_SHAPE);
    proveCase(validation::ProjectModulationBenchmarkCase::ADSR);
    std::cout << "Project modulation benchmark tests passed\n";
    return 0;
}
