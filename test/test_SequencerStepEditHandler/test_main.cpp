#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include <config/Timing.hpp>

#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerStepEditHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct SequencerStepEditHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 901;
    static constexpr oc::type::ScopeID OVERLAY_SCOPE = 902;

    test_support::CoreStorages storages;
    core::state::CoreState state;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::SequencerStepEditHandler handler;
    core::handler::SequencerPatternQuickControlsHandler quickControlsHandler;

    SequencerStepEditHarness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::SequencerStepEditHandler::StateRefs{
                      state.overlays,
                      state.sequencer,
                      state.trackNavigation,
                      core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                  },
                  overlays,
                  encoders,
                  buttons,
                  SEQUENCER_SCOPE,
                  OVERLAY_SCOPE)
        , quickControlsHandler(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          ) {
        overlays.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
        overlays.registerCleanup(core::ui::OverlayType::SEQ_STEP_EDIT, OVERLAY_SCOPE);
        g_now_ms = 0;
    }

    void tick(uint32_t nowMs) {
        g_now_ms = nowMs;
        inputBinding.processTick();
    }

    void press(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, true);
        eventBus.emit(oc::core::event::ButtonPressEvent(buttonId, true));
    }

    void release(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, false);
        eventBus.emit(oc::core::event::ButtonReleaseEvent(buttonId));
    }

    void tap(Config::ButtonID id) {
        press(id);
        release(id);
    }

    void advance(uint32_t ms) {
        g_now_ms += ms;
        inputBinding.processTick();
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

void longPressMacro(SequencerStepEditHarness& h, uint8_t indexInPage) {
    h.tick(0);
    h.press(Config::MACRO_BUTTONS[indexInPage]);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
    assert(!h.state.sequencer.stepEdit.visible.get());
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
}

void openStepEdit(SequencerStepEditHarness& h, uint8_t indexInPage) {
    longPressMacro(h, indexInPage);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
}

void holdPatternQuickControls(SequencerStepEditHarness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.physicalHoldActive.get());
}

void test_long_press_opens_step_edit_and_ignores_open_release() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(16);
    h.state.sequencer.page.set(1);

    openStepEdit(h, 2);
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 10);
    assert(h.state.sequencer.focusedStep.get() == 10);
    assert(h.state.sequencer.stepEdit.snapshotValid);

    h.release(Config::MACRO_BUTTONS[2]);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);

    h.tap(Config::MACRO_BUTTONS[2]);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_long_press_opens_step_edit_and_ignores_open_release\n";
}

void test_nav_and_opt_edit_then_nav_apply() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.velocity[3] = 64;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == 1);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.velocity[3] == 127);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.pattern.velocity[3] == 127);
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_nav_and_opt_edit_then_nav_apply\n";
}

void test_context_rows_are_focusable_and_root_action_rows_do_not_edit_properties() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[3] = 60;
    h.state.sequencer.pattern.velocity[3] = 64;
    h.state.sequencer.pattern.gate[3] = 70;
    h.state.sequencer.pattern.nudge[3] = 0;
    h.state.sequencer.pattern.probability[3] = 80;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);

    for (uint8_t i = 0; i < 5; ++i) {
        h.turn(Config::EncoderID::NAV, 1.0f);
    }
    assert(h.state.sequencer.stepEdit.focusedRow.get() == 5);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[3] == 60);
    assert(h.state.sequencer.pattern.velocity[3] == 64);
    assert(h.state.sequencer.pattern.gate[3] == 70);
    assert(h.state.sequencer.pattern.nudge[3] == 0);
    assert(h.state.sequencer.pattern.probability[3] == 80);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == 6);
    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.pattern.note[3] == 60);
    assert(h.state.sequencer.pattern.velocity[3] == 64);
    assert(h.state.sequencer.pattern.gate[3] == 70);
    assert(h.state.sequencer.pattern.nudge[3] == 0);
    assert(h.state.sequencer.pattern.probability[3] == 80);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == 0);

    std::cout << "[PASS] test_context_rows_are_focusable_and_root_action_rows_do_not_edit_properties\n";
}

void test_create_edit_and_commit_micro_sequence_context() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[3] = 60;
    h.state.sequencer.pattern.velocity[3] = 64;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);

    for (uint8_t i = 0; i < 5; ++i) {
        h.turn(Config::EncoderID::NAV, 1.0f);
    }
    assert(h.state.sequencer.stepEdit.focusedRow.get() == 5);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencer.contentView.parentStep.get() == 3);
    assert(h.state.sequencer.contentView.length.get() == 2);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(core::state::sequencer::stepHasMicroSequence(h.state.sequencer.pattern, 3));

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    assert(root != nullptr);
    const auto* sequence = graph->sequence(root->childSequenceId);
    assert(sequence != nullptr);
    const auto* firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset == 0);

    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer,
        0,
        core::state::sequencer::StepProperty::NOTE,
        62.0f / 127.0f,
        h.state.sequencer.pattern.pitchEditMode,
        {}
    ));

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    sequence = graph->sequence(root->childSequenceId);
    firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset == 2);

    holdPatternQuickControls(h);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::LENGTH
    );
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::LENGTH
    );
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.contentView.length.get() == 16);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.state.sequencer.focusedStep.set(15);
    h.state.sequencer.page.set(1);
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer,
        15,
        core::state::sequencer::StepProperty::VELOCITY,
        127.0f / 127.0f,
        h.state.sequencer.pattern.pitchEditMode,
        {}
    ));

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    sequence = graph->sequence(root->childSequenceId);
    const auto* lastMicroStep = graph->stepNode(static_cast<uint16_t>(sequence->firstStepNode + 15U));
    assert(lastMicroStep != nullptr);
    assert(lastMicroStep->velocityOffset == 63);

    assert(core::state::sequencer::leaveContentView(h.state.sequencer));
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 2);

    std::cout << "[PASS] test_create_edit_and_commit_micro_sequence_context\n";
}

void test_step_edit_session_undo_redo_workflow() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.focusedStep.set(6);
    h.state.sequencer.pattern.note[2] = 61;
    h.state.sequencer.pattern.gate[2] = 55;

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[2] == 127);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == 2);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(
        h.state.sequencer.pattern.gate[2] ==
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT
    );

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencer.pattern.note[2] == 127);
    assert(
        h.state.sequencer.pattern.gate[2] ==
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT
    );

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.pattern.note[2] == 61);
    assert(h.state.sequencer.pattern.gate[2] == 55);
    assert(h.state.sequencer.focusedStep.get() == 6);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.pattern.note[2] == 127);
    assert(
        h.state.sequencer.pattern.gate[2] ==
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT
    );
    assert(h.state.sequencer.focusedStep.get() == 2);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);

    std::cout << "[PASS] test_step_edit_session_undo_redo_workflow\n";
}

void test_cancel_restores_snapshot() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[4] = 62;
    h.state.sequencer.pattern.velocity[4] = 80;
    h.state.sequencer.pattern.gate[4] = 70;
    h.state.sequencer.pattern.nudge[4] = -5;
    h.state.sequencer.pattern.probability[4] = 90;

    openStepEdit(h, 4);
    h.release(Config::MACRO_BUTTONS[4]);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[4] == 127);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.pattern.note[4] == 62);
    assert(h.state.sequencer.pattern.velocity[4] == 80);
    assert(h.state.sequencer.pattern.gate[4] == 70);
    assert(h.state.sequencer.pattern.nudge[4] == -5);
    assert(h.state.sequencer.pattern.probability[4] == 90);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_cancel_restores_snapshot\n";
}

void test_step_edit_does_not_open_when_blocked() {
    {
        SequencerStepEditHarness h;
        h.state.overlays.show(core::ui::OverlayType::DATA_MANAGER);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.trackNavigation.selection.active.set(true);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.structureUi.pageSelection.active.set(true);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.patternQuickControls.selecting.set(true);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.stepPropertyInlineSelector.selecting.set(true);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    std::cout << "[PASS] test_step_edit_does_not_open_when_blocked\n";
}

}  // namespace

int main() {
    test_long_press_opens_step_edit_and_ignores_open_release();
    test_nav_and_opt_edit_then_nav_apply();
    test_context_rows_are_focusable_and_root_action_rows_do_not_edit_properties();
    test_create_edit_and_commit_micro_sequence_context();
    test_step_edit_session_undo_redo_workflow();
    test_cancel_restores_snapshot();
    test_step_edit_does_not_open_when_blocked();

    std::cout << "\nAll SequencerStepEditHandler tests passed.\n";
    return 0;
}
