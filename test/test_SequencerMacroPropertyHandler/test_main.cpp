#include <cassert>
#include <cstring>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "../../src/handler/sequencer/SequencerInputUtils.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
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
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
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

    void advance(uint32_t nowMs) {
        g_now_ms = nowMs;
        state.updateSequencerPatternHistoryCoalescing(nowMs);
    }
};

void test_macro_encoder_edits_step_in_current_page_and_shows_feedback() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.length.set(16);
    h.state.sequencer.page.set(1);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    g_now_ms = 1234;

    h.turn(Config::EncoderID::MACRO_3, 1.0f);

    const uint8_t step = 10;
    assert(h.state.sequencer.pattern.velocity[step] == 127);
    assert(h.state.sequencer.stepInlineFeedback.visible.get());
    assert(h.state.sequencer.stepInlineFeedback.touchedMask.get().test(step));
    assert(h.state.sequencer.stepInlineFeedback.property.get() == StepProperty::VELOCITY);

    std::cout << "[PASS] test_macro_encoder_edits_step_in_current_page_and_shows_feedback\n";
}

void test_opt_encoder_has_no_default_step_edit_binding() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.focusedStep.set(4);
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.pattern.gate[4] == core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT);
    assert(!h.state.sequencer.stepInlineFeedback.visible.get());
    assert(!h.state.sequencer.stepInlineFeedback.touchedMask.get().test(4));

    std::cout << "[PASS] test_opt_encoder_has_no_default_step_edit_binding\n";
}

void test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);

    const uint8_t step = 2;
    h.state.sequencer.cycleVariationTelemetry.validMask.setBit(step, true);
    h.state.sequencer.cycleVariationTelemetry.triggeredMask.setBit(step, true);
    h.state.sequencer.cycleVariationTelemetry.resolvedNote[step] = 60;
    h.state.sequencer.variationTelemetryRevision.set(10);

    h.turn(Config::EncoderID::MACRO_3, 1.0f);

    assert(h.state.sequencer.pattern.note[step] == 127);
    assert(!h.state.sequencer.cycleVariationTelemetry.validMask.test(step));
    assert(!h.state.sequencer.cycleVariationTelemetry.triggeredMask.test(step));
    assert(h.state.sequencer.variationTelemetryRevision.get() == 11);

    std::cout << "[PASS] test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step\n";
}

void test_constrained_scale_pitch_edit_writes_scale_degree_note() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.length.set(8);
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

    assert(h.state.sequencer.pattern.note[0] == 71);

    std::cout << "[PASS] test_constrained_scale_pitch_edit_writes_scale_degree_note\n";
}

void test_macro_property_edits_are_blocked_by_modal_states() {
    {
        SequencerMacroPropertyHarness h;
        h.state.overlays.show(core::ui::OverlayType::SEQ_STEP_EDIT);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.state.trackNavigation.selection.active.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.state.sequencer.structureUi.pageSelection.active.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.state.sequencer.patternQuickControls.selecting.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    std::cout << "[PASS] test_macro_property_edits_are_blocked_by_modal_states\n";
}

void test_macro_property_edits_coalesce_until_idle() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[0] = 0;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 0.25f);
    g_now_ms = 200;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencer.pattern.velocity[0] == 127);

    h.advance(699);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.advance(700);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.velocity[0] == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    std::cout << "[PASS] test_macro_property_edits_coalesce_until_idle\n";
}

void test_macro_property_step_change_commits_previous_coalesced_edit() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);
    h.state.sequencer.pattern.gate[0] = 50;
    h.state.sequencer.pattern.gate[1] = 60;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    g_now_ms = 200;
    h.turn(Config::EncoderID::MACRO_2, 0.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    h.advance(700);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 2);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.gate[0] == core::state::sequencer::SequencerState::MAX_GATE_PERCENT);
    assert(h.state.sequencer.pattern.gate[1] == 60);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.gate[0] == 50);

    std::cout << "[PASS] test_macro_property_step_change_commits_previous_coalesced_edit\n";
}

void test_macro_property_track_change_commits_pending_coalesced_edit() {
    SequencerMacroPropertyHarness h;
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[0] = 10;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    assert(h.state.setSharedTrackState(0x0003, 1));
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencerTracks.track(0).velocity[0] == 127);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencerTracks.track(0).velocity[0] == 10);
    assert(h.state.sequencer.pattern.velocity[0] != 10);

    std::cout << "[PASS] test_macro_property_track_change_commits_pending_coalesced_edit\n";
}

void test_macro_property_pending_edit_undoes_with_single_command() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    h.state.sequencer.pattern.note[0] = 60;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencer.pattern.note[0] == 127);

    assert(h.state.undoSequencerHistory());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[0] == 60);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(h.state.sequencer.historyFeedback.visible.get());
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "UNDO T01") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Step 01 Note") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "G9 -> C4") == 0);

    std::cout << "[PASS] test_macro_property_pending_edit_undoes_with_single_command\n";
}

void test_macro_property_new_pending_edit_invalidates_redo_on_commit() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[0] = 10;
    h.state.sequencer.pattern.velocity[1] = 20;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    h.advance(700);
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(h.state.sequencer.pattern.velocity[0] == 10);

    g_now_ms = 800;
    h.turn(Config::EncoderID::MACRO_2, 1.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

    assert(!h.state.redoSequencerHistory());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);
    assert(h.state.sequencer.pattern.velocity[0] == 10);
    assert(h.state.sequencer.pattern.velocity[1] == 127);

    std::cout << "[PASS] test_macro_property_new_pending_edit_invalidates_redo_on_commit\n";
}

}  // namespace

int main() {
    test_macro_encoder_edits_step_in_current_page_and_shows_feedback();
    test_opt_encoder_has_no_default_step_edit_binding();
    test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step();
    test_constrained_scale_pitch_edit_writes_scale_degree_note();
    test_macro_property_edits_are_blocked_by_modal_states();
    test_macro_property_edits_coalesce_until_idle();
    test_macro_property_step_change_commits_previous_coalesced_edit();
    test_macro_property_track_change_commits_pending_coalesced_edit();
    test_macro_property_pending_edit_undoes_with_single_command();
    test_macro_property_new_pending_edit_invalidates_redo_on_commit();

    std::cout << "\nAll SequencerMacroPropertyHandler tests passed.\n";
    return 0;
}
