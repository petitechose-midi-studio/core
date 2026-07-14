#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

#include <oc/api/EncoderAPI.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/macro/MacroMidiHandler.hpp"
#include "../../src/midi/MidiUtils.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t mockTimeMs() {
    return 1000;
}

void configureAutomation(core::state::CoreState& state) {
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);

    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 1.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 1.0f));
    assert(core::state::macro::macroAutomationAssignAutomation(
        state.pages.automation,
        *slot,
        lane
    ));
}

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    test_support::TestEncoderHardware encoderHardware;
    oc::api::EncoderAPI encoders;
    core::handler::MacroMidiHandler handler;

    Harness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , encoders(inputBinding, encoderHardware)
        , handler(
              core::handler::MacroMidiHandler::StateRefs{state.activeView},
              core::handler::MacroPerformanceDomainServices::fromCoreState(state),
              encoders
          ) {
        state.pages.activePageData().cc[0] = 74;
        state.pages.activePageData().cc[1] = 75;
        state.pages.updateActiveConfigs();
        configureAutomation(state);
        state.activeView.set(core::ui::ViewType::MACRO);
    }
};

void test_mapped_cc_takes_manual_ownership_and_authors_absolute_base() {
    Harness harness;

    harness.handler.onCC(0, 74, 100);

    const float expected = core::midi::fromCC(100);
    assert(std::fabs(harness.state.macros[0].value.get() - expected) < 0.0001f);
    assert(std::fabs(harness.state.pages.activePageData().values[0] - expected) < 0.0001f);
    assert(std::fabs(
        harness.encoders.getPosition(Config::MACRO_ENCODERS[0]) - expected
    ) < 0.0001f);
    assert((harness.state.macroUi.automationManualOverrideMask.get() & 0x0001U) != 0);
    assert(harness.state.hasPendingProjectMutationCoalescing());

    std::cout
        << "[PASS] test_mapped_cc_takes_manual_ownership_and_authors_absolute_base\n";
}

void test_inactive_macro_slot_does_not_accept_mapped_cc() {
    Harness harness;
    const float before = harness.state.pages.activePageData().values[1];

    harness.handler.onCC(0, 75, 100);

    assert(harness.state.pages.activePageData().values[1] == before);
    assert(!harness.state.hasPendingProjectMutationCoalescing());

    std::cout << "[PASS] test_inactive_macro_slot_does_not_accept_mapped_cc\n";
}

}  // namespace

int main() {
    test_mapped_cc_takes_manual_ownership_and_authors_absolute_base();
    test_inactive_macro_slot_does_not_accept_mapped_cc();

    std::cout << "\nAll MacroMidiHandler tests passed.\n";
    return 0;
}
