#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>

#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/CoreState.hpp"
#include "support/CoreStorages.hpp"
#include "support/NotificationTestUtils.hpp"
#include "support/ProjectControlTestUtils.hpp"

using namespace core::state::macro;
using namespace core::state::modulation;

// Compare the public take commit with the original full-domain assignment,
// including unused arena bytes after a grow-then-shrink multi-lane replacement.
void checkPublication(bool existingDenseCurve) {
    test_support::CoreStorages storage;
    auto state = std::make_unique<core::state::CoreState>(storage.settings);
    auto& control = state->pages.control;
    auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(*state);
    state->statusBar.tempo.set(120.0f);
    state->pages.activePageData().setMacroActive(1U, true);
    if (existingDenseCurve) {
        MacroAutomationLane lane;
        lane.active = true;
        lane.durationBeats = 2.0f;
        for (uint16_t i = 0; i < 128; ++i) {
            assert(macroAutomationAppendPoint(lane, i / 64.0f, (i & 1U) ? 0.8f : 0.2f));
        }
        assert(test_support::project_control::assignAutomation(control, {0, 0, 1}, lane));
    }
    // Unrelated recorded modulation shares the arena and must survive publication.
    test_support::project_control::ModulationShape shape;
    assert(test_support::project_control::appendModulationPoint(shape, 0.0f, -0.5f));
    assert(test_support::project_control::appendModulationPoint(shape, 1.0f, 0.5f));
    assert(test_support::project_control::assignModulation(control, {0, 0, 0}, shape, 0.4f));
    for (size_t i = control.authored.curves.pointCount; i < control.authored.curves.points.size(); ++i) {
        control.authored.curves.points[i] = {static_cast<uint16_t>(i), 123};
    }
    const auto before = std::make_unique<ProjectControlDomainState>(control.authored);
    const auto undoBefore = state->macroHistory.undoCount();
    (void)services.setAutomationTakeTiming(MacroAutomationTakeTiming::HOLD);
    assert(services.armAutomationTake());
    for (uint32_t i = 0; i < 64; ++i) {
        assert(services.recordAutomationTakeValue(0, 1000 + i * 10, (i & 1U) ? 0.9f : 0.1f));
        assert(services.recordAutomationTakeValue(1, 1000 + i * 10, 0.4f));
    }
    auto take = state->macroUi.automationTake;
    assert(take.finish(2U * MACRO_AUTOMATION_TICKS_PER_BEAT));
    auto expected = std::make_unique<ProjectControlDomainState>(*state->macroUi.automationTakeDomain);
    uint16_t highWater = expected->curves.pointCount;
    for (uint8_t macro = 0; macro < 2; ++macro) {
        std::array<ProjectPackedCurvePoint, MACRO_AUTOMATION_RECORDING_MAX_POINTS> points{};
        uint16_t count = 0;
        assert(take.buildPackedCurve(macro, points.data(), points.size(), count));
        ProjectControlCurvePayload payload{
            .spec = {
                .sourceDurationTicks = take.durationTicks,
                .durationTicks = take.durationTicks,
                .windowOffsetTicks = take.playbackWindowOffsetTicks(),
                .interpolation = ProjectCurveInterpolation::LINEAR,
                .valueDomain = ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
                .origin = ProjectCurveOrigin::NATIVE,
            },
            .pointOffset = 0,
            .pointCount = count,
            .enabled = count > 0,
        };
        assert(replaceProjectControlAutomationInDomain(*expected, {0, 0, macro}, payload, points.data(), count));
        highWater = std::max(highWater, expected->curves.pointCount);
    }
    if (existingDenseCurve) {
        assert(highWater > std::max(before->curves.pointCount, expected->curves.pointCount));
    }
    assert(services.releaseAutomationTake(2000));
    assert(std::memcmp(&control.authored, expected.get(), sizeof(*expected)) == 0);
    assert(state->macroHistory.undoCount() == undoBefore + 1);
    assert(state->macroHistory.undo(state->pages));
    assert(std::memcmp(&control.authored.modulation, &before->modulation, sizeof(before->modulation)) == 0);
    assert(state->macroHistory.redo(state->pages));
    assert(validProjectModulationDomain(control.authored.modulation, control.authored.curves, &control.authored.automation));
    test_support::drainNotifications();
}

int main() {
    checkPublication(false);
    checkPublication(true);
    std::cout << "Automation publication: full-copy equivalence, peak prefix, modulation and undo/redo OK\n";
}
