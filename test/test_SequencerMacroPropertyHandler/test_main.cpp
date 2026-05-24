#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "../../src/handler/sequencer/SequencerInputUtils.hpp"
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
namespace input_utils = core::handler::sequencer::input_utils;

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
                      state.sequencerTracks,
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

void test_opt_encoder_has_no_default_step_edit_binding() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.length.set(8);
    h.state.sequencer.focusedStep.set(4);
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.gate[4] == core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT);
    assert(!h.state.sequencer.stepInlineFeedback.visible.get());
    assert(!h.state.sequencer.stepInlineFeedback.touchedMask.get().test(4));

    std::cout << "[PASS] test_opt_encoder_has_no_default_step_edit_binding\n";
}

void test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);

    const uint8_t step = 2;
    h.state.sequencer.cycleVariationTelemetry.validMask.setBit(step, true);
    h.state.sequencer.cycleVariationTelemetry.triggeredMask.setBit(step, true);
    h.state.sequencer.cycleVariationTelemetry.resolvedNote[step] = 60;
    h.state.sequencer.variationTelemetryRevision.set(10);

    h.turn(Config::EncoderID::MACRO_3, 1.0f);

    assert(h.state.sequencer.note[step] == 127);
    assert(!h.state.sequencer.cycleVariationTelemetry.validMask.test(step));
    assert(!h.state.sequencer.cycleVariationTelemetry.triggeredMask.test(step));
    assert(h.state.sequencer.variationTelemetryRevision.get() == 11);

    std::cout << "[PASS] test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step\n";
}

void test_constrained_scale_pitch_edit_writes_scale_degree_note() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    h.state.sequencer.setStepNoteAt(0, 60);

    oc::note::sequencer::StepSequencerScaleSettings settings{
        .root = 0,
        .type = oc::note::sequencer::StepSequencerScaleType::Major,
        .mode = oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    h.state.sequencerTracks.setProjectScaleSettings(settings);
    h.state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );
    h.state.sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::CHROMATIC);

    const float b4AsScaleDegree = input_utils::indexToNormalized(
        input_utils::scaleDegreeIndexForNote(71, settings),
        input_utils::countScaleNotes(settings)
    );

    h.turn(Config::EncoderID::MACRO_1, b4AsScaleDegree);

    assert(h.state.sequencer.note[0] == 71);

    std::cout << "[PASS] test_constrained_scale_pitch_edit_writes_scale_degree_note\n";
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
    test_opt_encoder_has_no_default_step_edit_binding();
    test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step();
    test_constrained_scale_pitch_edit_writes_scale_degree_note();
    test_macro_property_edits_are_blocked_by_modal_states();

    std::cout << "\nAll SequencerMacroPropertyHandler tests passed.\n";
    return 0;
}
