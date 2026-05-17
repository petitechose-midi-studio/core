#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include <config/Timing.hpp>

#include "../../src/handler/common/SharedTrackDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/handler/sequencer/SequencerStepHandler.hpp"
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

    SequencerStepHarness()
        : state(storages.settings,
                storages.macroWorkspace,
                storages.macroLibrary,
                storages.sequencerWorkspace,
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
                  state.structureClipboard,
                  core::handler::SharedTrackDomainServices::fromCoreState(state),
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

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

void test_nav_selection_mode_deletes_selected_sequencer_page() {
    SequencerStepHarness h;
    h.state.sequencer.length.set(16);
    h.state.sequencer.page.set(1);
    h.state.sequencer.focusedStep.set(8);
    h.state.sequencer.note[8] = 72;
    h.state.sequencer.velocity[8] = 90;
    h.state.sequencer.setEnabled(8, true);

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

    assert(h.state.sequencer.length.get() == 8);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() <= 7);
    assert(!h.state.sequencer.enabledMask.get().test(8));
    assert(!h.state.sequencer.structureUi.pageSelection.active.get());

    std::cout << "[PASS] test_nav_selection_mode_deletes_selected_sequencer_page\n";
}

void test_page_selection_cursor_can_move_across_inactive_slots() {
    SequencerStepHarness h;
    h.state.sequencer.length.set(8);
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
    h.state.sequencer.length.set(24);
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
    assert(h.state.sequencer.length.get() == 48);
    assert(h.state.sequencer.page.get() == 5);
    assert(h.state.sequencer.focusedStep.get() == 40);

    for (uint8_t step = 24; step < 48; ++step) {
        assert(h.state.sequencer.note[step] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
        assert(h.state.sequencer.velocity[step] == core::state::sequencer::SequencerState::DEFAULT_VELOCITY);
        assert(h.state.sequencer.gate[step] == core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT);
        assert(h.state.sequencer.nudge[step] == 0);
        assert(h.state.sequencer.probability[step] == core::state::sequencer::SequencerState::DEFAULT_PROBABILITY);
        assert(!h.state.sequencer.isEnabled(step));
    }

    std::cout << "[PASS] test_sequencer_page_creation_extends_pattern_to_target_slot\n";
}

void test_nav_selection_mode_deletes_selected_sequencer_track() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencerTracks.track(1).midiChannel.set(5);
    h.state.sequencer.midiChannel.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.midiChannel.get() == 5);

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
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0);
    assert(h.state.sequencer.midiChannel.get() == 0);
    assert(!h.state.trackNavigation.selection.active.get());

    std::cout << "[PASS] test_nav_selection_mode_deletes_selected_sequencer_track\n";
}

void test_sequencer_page_copy_and_long_press_paste() {
    SequencerStepHarness h;
    h.state.sequencer.length.set(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.state.sequencer.note[0] = 72;
    h.state.sequencer.velocity[0] = 99;
    h.state.sequencer.gate[0] = 80;
    h.state.sequencer.nudge[0] = 3;
    h.state.sequencer.probability[0] = 87;
    h.state.sequencer.setEnabled(0, true);

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

    assert(h.state.sequencer.note[8] == 72);
    assert(h.state.sequencer.velocity[8] == 99);
    assert(h.state.sequencer.gate[8] == 80);
    assert(h.state.sequencer.nudge[8] == 3);
    assert(h.state.sequencer.probability[8] == 87);
    assert(h.state.sequencer.isEnabled(8));

    std::cout << "[PASS] test_sequencer_page_copy_and_long_press_paste\n";
}

void test_sequencer_selection_duplicate_copies_page_payload() {
    SequencerStepHarness h;
    h.state.sequencer.length.set(24);
    h.state.sequencer.page.set(2);
    h.state.sequencer.focusedStep.set(16);
    h.state.sequencer.note[8] = 75;
    h.state.sequencer.velocity[8] = 101;
    h.state.sequencer.setEnabled(8, true);

    h.state.sequencer.structureUi.pageSelection.active.set(true);
    h.state.sequencer.structureUi.pageSelection.scope.set(core::state::StructureSelectionScope::PAGE);
    h.state.sequencer.structureUi.pageSelection.cursorIndex.set(2);
    h.state.sequencer.structureUi.pageSelection.selectedMask.set(0x0002);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.length.get() == 24);
    assert(h.state.sequencer.page.get() == 2);
    assert(h.state.sequencer.focusedStep.get() == 16);
    assert(h.state.sequencer.note[16] == 75);
    assert(h.state.sequencer.velocity[16] == 101);
    assert(h.state.sequencer.isEnabled(16));
    assert(!h.state.sequencer.structureUi.pageSelection.active.get());

    std::cout << "[PASS] test_sequencer_selection_duplicate_copies_page_payload\n";
}

void test_sequencer_selection_duplicate_self_map_stays_in_selection() {
    SequencerStepHarness h;
    h.state.sequencer.length.set(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);

    h.state.sequencer.structureUi.pageSelection.active.set(true);
    h.state.sequencer.structureUi.pageSelection.scope.set(core::state::StructureSelectionScope::PAGE);
    h.state.sequencer.structureUi.pageSelection.cursorIndex.set(0);
    h.state.sequencer.structureUi.pageSelection.selectedMask.set(0x0001);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.length.get() == 16);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0x0001);

    std::cout << "[PASS] test_sequencer_selection_duplicate_self_map_stays_in_selection\n";
}

void test_sequencer_selection_duplicate_copies_track_payload() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 1);
    h.state.sequencer.note[0] = 82;
    h.state.sequencer.velocity[0] = 108;
    h.state.sequencer.setEnabled(0, true);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.state.trackNavigation.selection.active.set(true);
    h.state.trackNavigation.selection.scope.set(core::state::StructureSelectionScope::TRACK);
    h.state.trackNavigation.selection.cursorIndex.set(1);
    h.state.trackNavigation.selection.selectedMask.set(0x0002);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0007);
    assert(h.state.sequencerTracks.activeTrackIndex() == 2);
    assert(h.state.sequencer.note[0] == 82);
    assert(h.state.sequencer.velocity[0] == 108);
    assert(h.state.sequencer.isEnabled(0));
    assert(!h.state.trackNavigation.selection.active.get());

    std::cout << "[PASS] test_sequencer_selection_duplicate_copies_track_payload\n";
}

void test_sequencer_track_copy_and_long_press_paste_to_add_slot() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0001, 0);
    h.state.sequencer.length.set(8);
    h.state.sequencer.note[0] = 79;
    h.state.sequencer.velocity[0] = 96;
    h.state.sequencer.gate[0] = 72;
    h.state.sequencer.setEnabled(0, true);
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
    assert(h.state.sequencer.note[0] == 79);
    assert(h.state.sequencer.velocity[0] == 96);
    assert(h.state.sequencer.gate[0] == 72);
    assert(h.state.sequencer.isEnabled(0));

    std::cout << "[PASS] test_sequencer_track_copy_and_long_press_paste_to_add_slot\n";
}

void test_deleted_track_slot_can_be_recreated_at_any_gap() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005, 0);
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
    assert(h.state.sequencer.midiChannel.get() == 1);
    assert(h.state.sequencer.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!h.state.sequencer.isEnabled(0));
    assert(h.state.sequencerTracks.track(2).note[0] == 83);
    assert(h.state.sequencerTracks.track(2).isEnabled(0));

    std::cout << "[PASS] test_deleted_track_slot_can_be_recreated_at_any_gap\n";
}

}  // namespace

int main() {
    test_sequencer_page_creation_extends_pattern_to_target_slot();
    test_nav_selection_mode_deletes_selected_sequencer_page();
    test_page_selection_cursor_can_move_across_inactive_slots();
    test_nav_selection_mode_deletes_selected_sequencer_track();
    test_sequencer_page_copy_and_long_press_paste();
    test_sequencer_selection_duplicate_copies_page_payload();
    test_sequencer_selection_duplicate_self_map_stays_in_selection();
    test_sequencer_selection_duplicate_copies_track_payload();
    test_sequencer_track_copy_and_long_press_paste_to_add_slot();
    test_deleted_track_slot_can_be_recreated_at_any_gap();

    std::cout << "\nAll SequencerStepHandler tests passed.\n";
    return 0;
}
