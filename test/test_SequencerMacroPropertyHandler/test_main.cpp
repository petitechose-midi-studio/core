#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;
using StepProperty = core::state::sequencer::StepProperty;

struct SequencerMacroPropertyHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 1101;

    test_support::CoreStorages storages;
    core::state::CoreState state;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::SequencerMacroPropertyHandler handler;

    SequencerMacroPropertyHarness()
        : state(storages.settings,
                storages.macroWorkspace,
                storages.macroLibrary,
                storages.sequencerWorkspace,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::SequencerMacroPropertyHandler::StateRefs{
                      state.overlays,
                      state.sequencer,
                      state.trackNavigation,
                  },
                  encoders,
                  SEQUENCER_SCOPE,
                  mockTimeMs) {
        g_now_ms = 0;
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

void test_macro_encoder_edits_step_in_current_page_and_shows_feedback() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.length.set(16);
    h.state.sequencer.page.set(1);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    g_now_ms = 1234;

    h.turn(Config::EncoderID::MACRO_3, 1.0f);

    const uint8_t step = 10;
    assert(h.state.sequencer.velocity[step] == 127);
    assert(h.state.sequencer.stepInlineFeedback.visible.get());
    assert(h.state.sequencer.stepInlineFeedback.touchedMask.get().test(step));
    assert(h.state.sequencer.stepInlineFeedback.property.get() == StepProperty::VELOCITY);

    std::cout << "[PASS] test_macro_encoder_edits_step_in_current_page_and_shows_feedback\n";
}

void test_opt_encoder_edits_focused_step() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.length.set(8);
    h.state.sequencer.focusedStep.set(4);
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.gate[4] == 0);
    assert(h.state.sequencer.stepInlineFeedback.visible.get());
    assert(h.state.sequencer.stepInlineFeedback.touchedMask.get().test(4));

    std::cout << "[PASS] test_opt_encoder_edits_focused_step\n";
}

void test_macro_property_edits_are_blocked_by_modal_states() {
    {
        SequencerMacroPropertyHarness h;
        h.state.overlays.show(core::ui::OverlayType::SEQ_STEP_EDIT);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.state.trackNavigation.selection.active.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.state.sequencer.structureUi.pageSelection.active.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.state.sequencer.patternQuickControls.selecting.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    std::cout << "[PASS] test_macro_property_edits_are_blocked_by_modal_states\n";
}

}  // namespace

int main() {
    test_macro_encoder_edits_step_in_current_page_and_shows_feedback();
    test_opt_encoder_edits_focused_step();
    test_macro_property_edits_are_blocked_by_modal_states();

    std::cout << "\nAll SequencerMacroPropertyHandler tests passed.\n";
    return 0;
}
