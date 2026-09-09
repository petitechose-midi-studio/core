#include <cassert>
#include <cstring>
#include <memory>
#include "state/macro/MacroHistoryInternals.hpp"
#include "validation/benchmark/BenchStorage.hpp"
#include "validation/fixtures/MacroModulationFixture.hpp"

int main() {
    using namespace core::state::modulation;
    using namespace core::state::macro;
    using namespace core::state::macro::history_detail;
    core::validation::benchmark::BenchSettingsStorage settings;
    auto state = std::make_unique<core::state::CoreState>(settings);
    core::validation::fixtures::prepareMacroMultiModulationScenario(*state);
    constexpr MacroAutomationSlotAddress address{0, 0, 0};
    const auto destination = projectControlDestination(address);
    const auto sharedDestination = projectControlDestination({0, 0, 1});
    const std::array<ProjectPackedCurvePoint, 3> points{{{0, 0}, {64, 16000}, {128, 32000}}};
    ProjectCurveSpec spec{};
    spec.sourceDurationTicks = spec.durationTicks = 128;
    spec.valueDomain = ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    auto& domain = state->pages.control.authored();
    assert(setProjectAutomationCurve(domain.automation, domain.curves, destination,
        spec, points.data(), points.size(), true).changed());
    assert(duplicateProjectAutomationCurve(domain.automation, domain.curves, destination, sharedDestination).changed());
    domain.curves.points.back() = {12345, -12345};
    MacroSlotDeletionState before, after;
    assert(captureMacroSlotDeletionState(state->pages, address, before));
    assert(captureMacroSlotDeletionState(state->pages, address, after));
    after.automation.automation = {};
    after.automation.points.reset();
    after.automation.pointCount = 0;
    after.modulation.globalBindingCount -= after.modulation.assignmentCount;
    after.modulation.assignmentCount = 0;
    after.modulation.destinationScaleQ15 = PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    after.macroActive = false;

    const auto unchanged = [&](const auto& invoke) {
        const auto original = std::make_unique<ProjectControlDomainState>(state->pages.control.authored());
        const auto revision = state->pages.control.authoredRevision;
        const auto active = state->pages.pageData(0, 0).isMacroActive(0);
        assert(!invoke());
        assert(std::memcmp(original.get(), &state->pages.control.authored(), sizeof(*original)) == 0);
        assert(state->pages.control.authoredRevision == revision);
        assert(state->pages.pageData(0, 0).isMacroActive(0) == active);
    };
    for (unsigned ordinal : {1U, 2U}) {
        unchanged([&] {
            core::app::testing::ScopedExtmemAllocationFailure fail(ordinal);
            return applyMacroSlotDeletionState(state->pages, address, after);
        });
    }
    // The curve deletion would succeed, but the graph must fail before any live write.
    after.modulation.globalBindingCount = 513;
    unchanged([&] { return applyMacroSlotDeletionState(state->pages, address, after); });
    after.modulation.globalBindingCount = before.modulation.globalBindingCount - before.modulation.assignmentCount;
    const auto originalCount = domain.modulation.outputBindingCount;
    domain.modulation.outputBindingCount = 513;
    unchanged([&] { return applyMacroSlotDeletionState(state->pages, address, after); });
    domain.modulation.outputBindingCount = originalCount;

    for (unsigned cycle = 0; cycle < 24; ++cycle) {
        for (const auto* target : {&after, &before}) {
            assert(applyMacroSlotDeletionState(state->pages, address, *target));
            assert(liveMacroSlotDeletionStateMatches(state->pages, address, *target));
            const auto& live = state->pages.control.authored();
            assert(validProjectModulationDomain(live.modulation, live.curves, &live.automation));
            const auto* entry = findProjectAutomationCurve(live.automation, sharedDestination);
            assert(entry);
            const auto* curve = findProjectCurve(live.curves, entry->curveId);
            assert(curve && curve->pointCount == points.size());
            assert(std::memcmp(live.curves.points.data() + curve->pointOffset, points.data(), sizeof(points)) == 0);
            assert(live.curves.points.back().tick == 12345 && live.curves.points.back().value == -12345);
        }
    }
    state->pages.control.authoredRevision = UINT32_MAX;
    assert(applyMacroSlotDeletionState(state->pages, address, after));
    assert(state->pages.control.authoredRevision == 1);
}
