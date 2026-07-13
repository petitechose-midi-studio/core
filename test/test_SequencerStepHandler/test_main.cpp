#include <cassert>
#include <cstring>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include <config/Timing.hpp>

#include "../../src/handler/common/SharedTrackDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerStepHandler.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct SequencerStepHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 501;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers> navigationFocus;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    core::handler::SequencerStepHandler handler;
    core::handler::SequencerPatternQuickControlsHandler quickControlsHandler;

    SequencerStepHarness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , handler(
              core::handler::SequencerStepHandler::StateRefs{
                  state.sequencer,
                  state.sequencerTracks,
                  navigationFocus,
                  state.trackNavigation,
                  state.projectNavigation,
                  state.structureClipboard,
                  core::handler::SharedTrackDomainServices::fromCoreState(state),
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          )
        , quickControlsHandler(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          ) {
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

bool rootStepHasMicroSequence(const SequencerStepHarness& h, uint8_t step) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr) return false;
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    if (nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE);
}

const oc::note::sequencer::StepSequencerStepNode* rootStepNode(
    const SequencerStepHarness& h,
    uint8_t step
) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr) return nullptr;
    return graph->stepNode(core::state::sequencer::rootStepNodeId(step));
}

void createRootMicroSequence(SequencerStepHarness& h, uint8_t step) {
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    const auto result = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        nodeId,
        2
    );
    assert(result.ok);
}

bool nodeHasCycleStates(const SequencerStepHarness& h, uint16_t nodeId) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr || nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(oc::note::sequencer::STEP_NODE_CYCLE_SET);
}

void holdPatternQuickControls(SequencerStepHarness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.physicalHoldActive.get());
}

void test_step_toggle_undo_redo_workflow() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.focusedStep.set(3);
    createRootMicroSequence(h, 0);
    assert(core::state::sequencer::storeActiveTrack(
        h.state.sequencerTracks,
        h.state.sequencer
    ));

    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.tap(Config::MACRO_BUTTONS[0]);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(rootStepHasMicroSequence(h, 0));

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(rootStepHasMicroSequence(h, 0));
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(rootStepHasMicroSequence(h, 0));
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);

    std::cout << "[PASS] test_step_toggle_undo_redo_workflow\n";
}

void test_nav_selection_mode_deletes_selected_sequencer_page() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(16);
    h.state.sequencer.page.set(1);
    h.state.sequencer.focusedStep.set(8);
    h.state.sequencer.pattern.note[8] = 72;
    h.state.sequencer.pattern.velocity[8] = 90;
    h.state.sequencer.pattern.setEnabled(8, true);

    h.press(Config::ButtonID::NAV);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(h.state.sequencer.structureUi.pageSelection.scope.get() ==
           core::state::StructureSelectionScope::PAGE);
    assert(h.state.sequencer.structureUi.pageSelection.cursorIndex.get() == 1);

    h.release(Config::ButtonID::NAV);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 1U);

    h.state.sequencer.structureUi.pageSelection.selectedMask.set(0x0002);
    assert(h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0x0002);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(!h.state.sequencer.pattern.enabledMask.get().test(8));
    assert(h.state.sequencer.pattern.note[8] == core::state::sequencer::SequencerState::DEFAULT_NOTE);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() <= 7);
    assert(!h.state.sequencer.pattern.enabledMask.get().test(8));
    assert(!h.state.sequencer.structureUi.pageSelection.active.get());

    std::cout << "[PASS] test_nav_selection_mode_deletes_selected_sequencer_page\n";
}

void test_page_selection_cursor_can_move_across_inactive_slots() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.state.sequencer.structureUi.pageSelection.active.set(true);
    h.state.sequencer.structureUi.pageSelection.scope.set(core::state::StructureSelectionScope::PAGE);
    h.state.sequencer.structureUi.pageSelection.cursorIndex.set(0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.pageSelection.cursorIndex.get() == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.sequencer.structureUi.pageSelection.cursorIndex.get() == 0);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0x0001);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(
        h.state.sequencer.structureUi.pageSelection.cursorIndex.get() ==
        core::state::sequencer::SequencerState::PAGE_COUNT - 1
    );

    std::cout << "[PASS] test_page_selection_cursor_can_move_across_inactive_slots\n";
}

void test_sequencer_page_creation_extends_pattern_to_target_slot() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(24);
    h.state.sequencer.page.set(2);
    h.state.sequencer.focusedStep.set(16);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 3);
    assert(h.state.sequencer.page.get() == 3);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 4);
    assert(h.state.sequencer.page.get() == 4);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 5);
    assert(h.state.sequencer.page.get() == 5);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(!h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.pattern.length.get() == 48);
    assert(h.state.sequencer.page.get() == 5);
    assert(h.state.sequencer.focusedStep.get() == 40);

    for (uint8_t step = 24; step < 48; ++step) {
        assert(h.state.sequencer.pattern.note[step] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
        assert(h.state.sequencer.pattern.velocity[step] == core::state::sequencer::SequencerState::DEFAULT_VELOCITY);
        assert(h.state.sequencer.pattern.gate[step] == core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT);
        assert(h.state.sequencer.pattern.nudge[step] == 0);
        assert(h.state.sequencer.pattern.probability[step] == core::state::sequencer::SequencerState::DEFAULT_PROBABILITY);
        assert(!h.state.sequencer.pattern.isEnabled(step));
    }

    std::cout << "[PASS] test_sequencer_page_creation_extends_pattern_to_target_slot\n";
}

void test_nav_selection_mode_mutes_then_deletes_selected_sequencer_track() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencerTracks.track(1).midiChannel.set(5);
    h.state.sequencer.pattern.midiChannel.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.midiChannel.get() == 5);

    h.press(Config::ButtonID::NAV);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.trackNavigation.selection.active.get());
    assert(h.state.trackNavigation.selection.scope.get() ==
           core::state::StructureSelectionScope::TRACK);
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 1);

    h.release(Config::ButtonID::NAV);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 1U);

    h.state.trackNavigation.selection.selectedMask.set(0x0002);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0x0002);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.currentMutedMask() == 0x0002);
    assert(h.state.trackNavigation.selection.active.get());

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.currentMutedMask() == 0);
    assert(h.state.trackNavigation.selection.active.get());

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(h.state.sequencerTracks.currentMutedMask() == 0);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0);
    assert(h.state.sequencer.pattern.midiChannel.get() == 0);
    assert(!h.state.trackNavigation.selection.active.get());

    std::cout << "[PASS] test_nav_selection_mode_mutes_then_deletes_selected_sequencer_track\n";
}

void test_track_focus_bottom_left_mutes_without_clearing_payload() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 1);
    h.state.sequencer.pattern.note[0] = 82;
    h.state.sequencer.pattern.velocity[0] = 108;
    h.state.sequencer.pattern.setEnabled(0, true);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.currentMutedMask() == 0x0002);
    assert(h.state.sequencer.pattern.note[0] == 82);
    assert(h.state.sequencer.pattern.velocity[0] == 108);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.currentMutedMask() == 0);
    assert(h.state.sequencer.pattern.note[0] == 82);
    assert(h.state.sequencer.pattern.isEnabled(0));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencerTracks.currentMutedMask() == 0x0002);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencerTracks.currentMutedMask() == 0);

    std::cout << "[PASS] test_track_focus_bottom_left_mutes_without_clearing_payload\n";
}

void test_sequencer_page_copy_and_long_press_paste() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.state.sequencer.pattern.note[0] = 72;
    h.state.sequencer.pattern.velocity[0] = 99;
    h.state.sequencer.pattern.gate[0] = 80;
    h.state.sequencer.pattern.nudge[0] = 3;
    h.state.sequencer.pattern.probability[0] = 87;
    h.state.sequencer.pattern.setEnabled(0, true);
    createRootMicroSequence(h, 0);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerPage());
    assert(h.state.structureClipboard.sequencerPage.sourcePage == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.page.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.note[8] == 72);
    assert(h.state.sequencer.pattern.velocity[8] == 99);
    assert(h.state.sequencer.pattern.gate[8] == 80);
    assert(h.state.sequencer.pattern.nudge[8] == 3);
    assert(h.state.sequencer.pattern.probability[8] == 87);
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(rootStepHasMicroSequence(h, 8));

    std::cout << "[PASS] test_sequencer_page_copy_and_long_press_paste\n";
}

void test_child_content_clear_copy_and_paste_are_undoable() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    h.state.sequencer.focusedStep.set(0);

    const auto childNode0 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    const auto cycle = core::state::sequencer::createCycleStateSet(
        h.state.sequencer.pattern,
        childNode0,
        2
    );
    assert(cycle.ok);
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        h.state.sequencer.pattern.graph->cycleSets[cycle.id].firstStateNode,
        5
    ));
    h.state.sequencer.contentView.bump();

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerStepContent());

    const uint8_t undoBeforeClear = h.state.sequencerHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    const auto* graphAfterClear = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterClear != nullptr);
    assert(!graphAfterClear->stepNodes[childNode0].has(
        oc::note::sequencer::STEP_NODE_CYCLE_SET
    ));
    assert(h.state.sequencerHistory.undoCount() == undoBeforeClear + 1U);

    h.press(Config::MACRO_BUTTONS[1]);
    h.release(Config::MACRO_BUTTONS[1]);
    assert(h.state.sequencer.focusedStep.get() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        1
    );
    const auto* graphAfterPaste = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterPaste != nullptr);
    assert(graphAfterPaste->stepNodes[childNode1].has(
        oc::note::sequencer::STEP_NODE_CYCLE_SET
    ));
    const auto* pastedCycle =
        graphAfterPaste->cycleSet(graphAfterPaste->stepNodes[childNode1].cycleSetId);
    assert(pastedCycle != nullptr);
    assert(graphAfterPaste->stepNodes[pastedCycle->firstStateNode].noteOffset == 5);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_CENTER);
    const auto* graphAfterUndo = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterUndo != nullptr);
    assert(!graphAfterUndo->stepNodes[childNode1].has(
        oc::note::sequencer::STEP_NODE_CYCLE_SET
    ));

    std::cout << "[PASS] test_child_content_clear_copy_and_paste_are_undoable\n";
}

void test_undo_removed_active_child_context_returns_to_root() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.focusedStep.set(0);

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(h.state.sequencer, before));

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(h.state.sequencer, after));
    assert(h.state.recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
            .stepIndex = 0,
        }
    ));

    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    assert(h.state.undoSequencerHistory());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(h.state.sequencer.contentView.depth.get() == 0);
    assert(h.state.sequencer.contentView.ownerNodeId.get() ==
           oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID);

    std::cout << "[PASS] test_undo_removed_active_child_context_returns_to_root\n";
}

void test_sequencer_selection_copy_paste_copies_page_payload() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(24);
    h.state.sequencer.page.set(1);
    h.state.sequencer.focusedStep.set(8);
    h.state.sequencer.pattern.note[8] = 75;
    h.state.sequencer.pattern.velocity[8] = 101;
    h.state.sequencer.pattern.setEnabled(8, true);

    h.state.sequencer.structureUi.pageSelection.active.set(true);
    h.state.sequencer.structureUi.pageSelection.scope.set(core::state::StructureSelectionScope::PAGE);
    h.state.sequencer.structureUi.pageSelection.cursorIndex.set(1);
    h.state.sequencer.structureUi.pageSelection.selectedMask.set(0x0002);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.hasSequencerPageSelection());
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(!h.state.sequencer.pattern.isEnabled(16));

    h.state.sequencer.structureUi.pageSelection.cursorIndex.set(2);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.length.get() == 24);
    assert(h.state.sequencer.page.get() == 2);
    assert(h.state.sequencer.focusedStep.get() == 16);
    assert(h.state.sequencer.pattern.note[16] == 75);
    assert(h.state.sequencer.pattern.velocity[16] == 101);
    assert(h.state.sequencer.pattern.isEnabled(16));
    assert(!h.state.sequencer.structureUi.pageSelection.active.get());

    std::cout << "[PASS] test_sequencer_selection_copy_paste_copies_page_payload\n";
}

void test_sequencer_selection_copy_keeps_page_selection_active() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);

    h.state.sequencer.structureUi.pageSelection.active.set(true);
    h.state.sequencer.structureUi.pageSelection.scope.set(core::state::StructureSelectionScope::PAGE);
    h.state.sequencer.structureUi.pageSelection.cursorIndex.set(0);
    h.state.sequencer.structureUi.pageSelection.selectedMask.set(0x0001);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.hasSequencerPageSelection());
    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0x0001);

    std::cout << "[PASS] test_sequencer_selection_copy_keeps_page_selection_active\n";
}

void test_sequencer_selection_copy_paste_copies_track_payload() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0007, 1);
    h.state.sequencerTracks.track(2).midiChannel.set(10);
    h.state.sequencerTracks.setTrackMuted(2, true);
    h.state.sequencer.pattern.note[0] = 82;
    h.state.sequencer.pattern.velocity[0] = 108;
    h.state.sequencer.pattern.setEnabled(0, true);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.state.trackNavigation.selection.active.set(true);
    h.state.trackNavigation.selection.scope.set(core::state::StructureSelectionScope::TRACK);
    h.state.trackNavigation.selection.cursorIndex.set(1);
    h.state.trackNavigation.selection.selectedMask.set(0x0002);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.hasSequencerTrackSelection());
    assert(h.state.trackNavigation.selection.active.get());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0007);

    h.state.trackNavigation.selection.cursorIndex.set(2);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0007);
    assert(h.state.sequencerTracks.activeTrackIndex() == 2);
    assert(h.state.sequencer.pattern.midiChannel.get() == 10);
    assert(h.state.sequencerTracks.track(2).midiChannel.get() == 10);
    assert(h.state.sequencerTracks.isTrackMuted(2));
    assert(h.state.sequencer.pattern.note[0] == 82);
    assert(h.state.sequencer.pattern.velocity[0] == 108);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(!h.state.trackNavigation.selection.active.get());
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
    assert(h.state.structureClipboard.hasSequencerTrackSelection());

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.track(2).midiChannel.get() == 10);
    assert(h.state.sequencerTracks.isTrackMuted(2));
    assert(h.state.structureClipboard.hasSequencerTrackSelection());

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.midiChannel.get() == 10);
    assert(h.state.sequencerTracks.isTrackMuted(2));
    assert(h.state.structureClipboard.hasSequencerTrackSelection());

    std::cout << "[PASS] test_sequencer_selection_copy_paste_copies_track_payload\n";
}

void test_sequencer_track_copy_and_long_press_paste_to_add_slot() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0001, 0);
    h.state.sequencerTracks.track(1).midiChannel.set(8);
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[0] = 79;
    h.state.sequencer.pattern.velocity[0] = 96;
    h.state.sequencer.pattern.gate[0] = 72;
    h.state.sequencer.pattern.setEnabled(0, true);
    createRootMicroSequence(h, 0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.midiChannel.get() == 8);
    assert(h.state.sequencerTracks.track(1).midiChannel.get() == 8);
    assert(h.state.sequencer.pattern.note[0] == 79);
    assert(h.state.sequencer.pattern.velocity[0] == 96);
    assert(h.state.sequencer.pattern.gate[0] == 72);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(rootStepHasMicroSequence(h, 0));
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(h.state.sequencerTracks.track(1).midiChannel.get() == 8);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.midiChannel.get() == 8);
    assert(rootStepHasMicroSequence(h, 0));
    assert(h.state.structureClipboard.hasSequencerTrack());

    std::cout << "[PASS] test_sequencer_track_copy_and_long_press_paste_to_add_slot\n";
}

void test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencerTracks.track(1).midiChannel.set(11);
    h.state.sequencerTracks.setTrackMuted(1, true);
    h.state.sequencer.pattern.note[0] = 76;
    h.state.sequencer.pattern.velocity[0] = 104;
    h.state.sequencer.pattern.setEnabled(0, true);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.midiChannel.get() == 11);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.midiChannel.get() == 11);
    assert(h.state.sequencerTracks.track(1).midiChannel.get() == 11);
    assert(h.state.sequencerTracks.isTrackMuted(1));
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.sequencer.pattern.velocity[0] == 104);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.midiChannel.get() == 11);
    assert(h.state.sequencerTracks.isTrackMuted(1));
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.midiChannel.get() == 11);
    assert(h.state.sequencerTracks.isTrackMuted(1));
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.structureClipboard.hasSequencerTrack());

    std::cout
        << "[PASS] test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute\n";
}

void test_deleted_track_slot_can_be_recreated_at_any_gap() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005, 0);
    h.state.sequencerTracks.track(1).midiChannel.set(8);
    h.state.sequencerTracks.track(2).midiChannel.set(2);
    h.state.sequencerTracks.track(2).note[0] = 83;
    h.state.sequencerTracks.track(2).setEnabled(0, true);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.sequencerTracks.activeTrackIndex() == 2);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.sequencerTracks.isTrackEnabled(1));
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.midiChannel.get() == 8);
    assert(h.state.sequencerTracks.track(1).midiChannel.get() == 8);
    assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerTracks.track(2).note[0] == 83);
    assert(h.state.sequencerTracks.track(2).isEnabled(0));

    std::cout << "[PASS] test_deleted_track_slot_can_be_recreated_at_any_gap\n";
}

void test_created_page_is_undoable_and_redoable() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.page.get() == 1);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::PatternOnly
    ) == 1);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::FullBank
    ) == 0);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.page.get() == 0);
    assert(!h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "UNDO T01") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Page Structure") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "2 pages -> 1 page") == 0);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.page.get() == 1);
    assert(!h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "REDO T01") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "1 page -> 2 pages") == 0);

    std::cout << "[PASS] test_created_page_is_undoable_and_redoable\n";
}

void test_created_track_is_undoable_and_redoable() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0001, 0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.midiChannel.get() == 1);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::FullBank
    ) == 0);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 0);
    assert(h.state.sequencer.pattern.midiChannel.get() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "UNDO T02") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Track Structure") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "2 tracks -> 1 track") == 0);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);
    assert(h.state.sequencer.pattern.midiChannel.get() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "REDO T02") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "1 track -> 2 tracks") == 0);

    std::cout << "[PASS] test_created_track_is_undoable_and_redoable\n";
}

void test_step_selection_copy_paste_extends_sparse_root_steps() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[1] = 65;
    h.state.sequencer.pattern.velocity[1] = 91;
    h.state.sequencer.pattern.gate[1] = 130;
    h.state.sequencer.pattern.nudge[1] = -2;
    h.state.sequencer.pattern.probability[1] = 76;
    h.state.sequencer.pattern.setEnabled(1, true);

    h.state.sequencer.pattern.note[3] = 70;
    h.state.sequencer.pattern.velocity[3] = 112;
    h.state.sequencer.pattern.gate[3] = 180;
    h.state.sequencer.pattern.nudge[3] = 4;
    h.state.sequencer.pattern.probability[3] = 64;
    h.state.sequencer.pattern.setEnabled(3, true);
    createRootMicroSequence(h, 3);
    oc::note::sequencer::StepSequencerChordSpec selectedChord{};
    selectedChord.voiceCount = 5;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(3),
        selectedChord
    ));

    h.press(Config::ButtonID::NAV);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);
    h.release(Config::ButtonID::NAV);

    h.tap(Config::MACRO_BUTTONS[1]);
    h.tap(Config::MACRO_BUTTONS[3]);
    assert(h.state.sequencer.structureUi.stepSelection.selected(1));
    assert(h.state.sequencer.structureUi.stepSelection.selected(3));

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(h.state.structureClipboard.sequencerSteps.rootContext);
    assert(h.state.structureClipboard.sequencerSteps.count == 2);
    assert(h.state.structureClipboard.sequencerSteps.span == 3);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.stepSelection.cursorStep.get() == 6);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    assert(
        h.state.sequencer.structureUi.pageHold.action.get() ==
        core::state::StructureHoldAction::PASTE
    );
    h.advance(0);
    assert(h.state.sequencer.structureUi.stepSelection.pastePreviewActive.get());
    assert(
        h.state.sequencer.structureUi.stepSelection.pastePreview.get() ==
        core::state::sequencer::SequencerStepPastePreview::GHOST
    );
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(!h.state.sequencer.structureUi.stepSelection.active.get());
    assert(
        h.state.sequencer.structureUi.pageHold.action.get() ==
        core::state::StructureHoldAction::NONE
    );
    assert(h.state.sequencer.pattern.length.get() == 9);
    assert(h.state.sequencer.focusedStep.get() == 6);
    assert(h.state.sequencer.pattern.note[6] == 65);
    assert(h.state.sequencer.pattern.velocity[6] == 91);
    assert(h.state.sequencer.pattern.gate[6] == 130);
    assert(h.state.sequencer.pattern.nudge[6] == -2);
    assert(h.state.sequencer.pattern.probability[6] == 76);
    assert(h.state.sequencer.pattern.isEnabled(6));
    assert(h.state.sequencer.pattern.note[8] == 70);
    assert(h.state.sequencer.pattern.velocity[8] == 112);
    assert(h.state.sequencer.pattern.gate[8] == 180);
    assert(h.state.sequencer.pattern.nudge[8] == 4);
    assert(h.state.sequencer.pattern.probability[8] == 64);
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(rootStepHasMicroSequence(h, 8));
    const auto* pastedChordNode = rootStepNode(h, 8);
    assert(pastedChordNode != nullptr);
    assert(pastedChordNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(pastedChordNode->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(pastedChordNode->chordSpec.voiceCount == 5);

    std::cout << "[PASS] test_step_selection_copy_paste_extends_sparse_root_steps\n";
}

void test_step_selection_macro_long_press_consumes_release_without_toggling() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(2);
    h.state.sequencer.structureUi.stepSelection.setSelected(2, true);
    h.state.sequencer.structureUi.stepSelection.setSelected(4, true);

    h.press(Config::MACRO_BUTTONS[2]);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::MACRO_BUTTONS[2]);

    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.structureUi.stepSelection.selected(2));
    assert(h.state.sequencer.structureUi.stepSelection.selected(4));

    h.tap(Config::MACRO_BUTTONS[2]);
    assert(!h.state.sequencer.structureUi.stepSelection.selected(2));
    assert(h.state.sequencer.structureUi.stepSelection.selected(4));

    std::cout << "[PASS] test_step_selection_macro_long_press_consumes_release_without_toggling\n";
}

void test_macro_press_on_future_page_does_not_wrap_to_existing_step() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.page.set(1);
    h.state.sequencer.focusedStep.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    h.tap(Config::MACRO_BUTTONS[0]);

    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_macro_press_on_future_page_does_not_wrap_to_existing_step\n";
}

void test_step_focus_bottom_left_resets_focused_step_only() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(3);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[3] = 74;
    h.state.sequencer.pattern.velocity[3] = 105;
    h.state.sequencer.pattern.setEnabled(3, true);
    createRootMicroSequence(h, 3);
    oc::note::sequencer::StepSequencerChordSpec chord{};
    chord.voiceCount = 6;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(3),
        chord
    ));
    h.state.sequencer.pattern.note[8] = 81;
    h.state.sequencer.pattern.setEnabled(8, true);

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(h.state.sequencer.page.get() == 0);
    assert(!h.state.sequencer.pattern.isEnabled(3));
    assert(h.state.sequencer.pattern.note[3] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(rootStepHasMicroSequence(h, 3));
    const auto* shallowResetNode = rootStepNode(h, 3);
    assert(shallowResetNode != nullptr);
    assert(!shallowResetNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(h.state.sequencer.pattern.note[8] == 81);
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(h.state.sequencer.page.get() == 0);
    assert(!rootStepHasMicroSequence(h, 3));

    std::cout << "[PASS] test_step_focus_bottom_left_resets_focused_step_only\n";
}

void test_step_focus_copy_paste_copies_complete_step_without_selection() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(1);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[1] = 76;
    h.state.sequencer.pattern.velocity[1] = 112;
    h.state.sequencer.pattern.gate[1] = 180;
    h.state.sequencer.pattern.nudge[1] = 3;
    h.state.sequencer.pattern.setEnabled(1, true);
    createRootMicroSequence(h, 1);
    oc::note::sequencer::StepSequencerChordSpec chord{};
    chord.voiceCount = 7;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(1),
        chord
    ));

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(h.state.structureClipboard.sequencerSteps.rootContext);
    assert(h.state.structureClipboard.sequencerSteps.count == 1);
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.focusedStep.get() == 2);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(!h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.focusedStep.get() == 2);
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 76);
    assert(h.state.sequencer.pattern.velocity[2] == 112);
    assert(h.state.sequencer.pattern.gate[2] == 180);
    assert(h.state.sequencer.pattern.nudge[2] == 3);
    assert(rootStepHasMicroSequence(h, 2));
    const auto* pastedNode = rootStepNode(h, 2);
    assert(pastedNode != nullptr);
    assert(pastedNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(pastedNode->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(pastedNode->chordSpec.voiceCount == 7);

    std::cout << "[PASS] test_step_focus_copy_paste_copies_complete_step_without_selection\n";
}

void test_step_selection_clear_is_undoable_and_keeps_selection_active() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.pattern.note[2] = 74;
    h.state.sequencer.pattern.velocity[2] = 105;
    h.state.sequencer.pattern.setEnabled(2, true);
    createRootMicroSequence(h, 2);

    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(2);
    h.state.sequencer.structureUi.stepSelection.setSelected(2, true);
    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(rootStepHasMicroSequence(h, 2));
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 74);
    assert(h.state.sequencer.pattern.velocity[2] == 105);
    assert(rootStepHasMicroSequence(h, 2));

    const uint8_t undoBeforeDeepReset = h.state.sequencerHistory.undoCount();

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!rootStepHasMicroSequence(h, 2));
    assert(h.state.sequencerHistory.undoCount() == undoBeforeDeepReset + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 74);
    assert(h.state.sequencer.pattern.velocity[2] == 105);
    assert(rootStepHasMicroSequence(h, 2));

    std::cout << "[PASS] test_step_selection_clear_is_undoable_and_keeps_selection_active\n";
}

void test_step_selection_wrap_paste_overwrites_inside_pattern() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.projectNavigation.stepPasteMode =
        core::state::project::ProjectStepPasteMode::WRAP;

    h.state.sequencer.pattern.note[1] = 61;
    h.state.sequencer.pattern.note[3] = 63;
    h.state.sequencer.pattern.setEnabled(1, true);
    h.state.sequencer.pattern.setEnabled(3, true);
    h.state.sequencer.pattern.note[7] = 79;
    h.state.sequencer.pattern.setEnabled(7, true);

    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(1);
    h.state.sequencer.structureUi.stepSelection.setSelected(1, true);
    h.state.sequencer.structureUi.stepSelection.setSelected(3, true);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());

    h.state.sequencer.structureUi.stepSelection.cursorStep.set(7);
    h.state.sequencer.focusedStep.set(7);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    assert(h.state.sequencer.structureUi.stepSelection.pastePreviewActive.get());
    assert(
        h.state.sequencer.structureUi.stepSelection.pastePreview.get() ==
        core::state::sequencer::SequencerStepPastePreview::OVERWRITE
    );
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.pattern.note[7] == 61);
    assert(h.state.sequencer.pattern.isEnabled(7));
    assert(h.state.sequencer.pattern.note[1] == 63);
    assert(h.state.sequencer.pattern.isEnabled(1));

    std::cout << "[PASS] test_step_selection_wrap_paste_overwrites_inside_pattern\n";
}

void test_child_content_nav_enters_step_selection_and_pastes_child_steps() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.state.sequencer.focusedStep.set(0);

    const auto childNode0 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        childNode0,
        4
    ));
    const auto cycle = core::state::sequencer::createCycleStateSet(
        h.state.sequencer.pattern,
        childNode0,
        2
    );
    assert(cycle.ok);

    h.press(Config::ButtonID::NAV);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);
    h.release(Config::ButtonID::NAV);

    h.tap(Config::MACRO_BUTTONS[0]);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(!h.state.structureClipboard.sequencerSteps.rootContext);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.stepSelection.cursorStep.get() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        1
    );
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode1].noteOffset == 4);
    assert(graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_CYCLE_SET));

    std::cout << "[PASS] test_child_content_nav_enters_step_selection_and_pastes_child_steps\n";
}

void test_child_step_focus_bottom_actions_use_local_step_payload() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.focusedStep.set(0);

    auto childNode0 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        childNode0,
        4
    ));
    assert(core::state::sequencer::createCycleStateSet(
        h.state.sequencer.pattern,
        childNode0,
        2
    ).ok);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode0].noteOffset == 0);
    assert(!graph->stepNodes[childNode0].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(nodeHasCycleStates(h, childNode0));

    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        childNode0,
        5
    ));
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(!h.state.structureClipboard.sequencerSteps.rootContext);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.focusedStep.get() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        1
    );
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode1].noteOffset == 5);
    assert(graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(nodeHasCycleStates(h, childNode1));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(!graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(!nodeHasCycleStates(h, childNode1));

    std::cout << "[PASS] test_child_step_focus_bottom_actions_use_local_step_payload\n";
}

}  // namespace

int main() {
    test_step_toggle_undo_redo_workflow();
    test_sequencer_page_creation_extends_pattern_to_target_slot();
    test_nav_selection_mode_deletes_selected_sequencer_page();
    test_page_selection_cursor_can_move_across_inactive_slots();
    test_nav_selection_mode_mutes_then_deletes_selected_sequencer_track();
    test_track_focus_bottom_left_mutes_without_clearing_payload();
    test_sequencer_page_copy_and_long_press_paste();
    test_child_content_clear_copy_and_paste_are_undoable();
    test_undo_removed_active_child_context_returns_to_root();
    test_sequencer_selection_copy_paste_copies_page_payload();
    test_sequencer_selection_copy_keeps_page_selection_active();
    test_sequencer_selection_copy_paste_copies_track_payload();
    test_sequencer_track_copy_and_long_press_paste_to_add_slot();
    test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute();
    test_deleted_track_slot_can_be_recreated_at_any_gap();
    test_created_page_is_undoable_and_redoable();
    test_created_track_is_undoable_and_redoable();
    test_macro_press_on_future_page_does_not_wrap_to_existing_step();
    test_step_focus_bottom_left_resets_focused_step_only();
    test_step_focus_copy_paste_copies_complete_step_without_selection();
    test_step_selection_copy_paste_extends_sparse_root_steps();
    test_step_selection_macro_long_press_consumes_release_without_toggling();
    test_step_selection_clear_is_undoable_and_keeps_selection_active();
    test_step_selection_wrap_paste_overwrites_inside_pattern();
    test_child_content_nav_enters_step_selection_and_pastes_child_steps();
    test_child_step_focus_bottom_actions_use_local_step_payload();

    std::cout << "\nAll SequencerStepHandler tests passed.\n";
    return 0;
}
