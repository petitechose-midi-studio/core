#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../../../open-control/framework/src/oc/core/event/EventBus.cpp"
#include "../../src/handler/sequencer/SequencerStepHandler.hpp"
#include "../../src/handler/sequencer/SequencerStepHandler.cpp"
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

    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState tracks;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers> navigationFocus;
    core::state::StructureClipboardState clipboard;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    core::handler::SequencerStepHandler handler;

    SequencerStepHarness()
        : navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , handler(
              core::handler::SequencerStepHandler::StateRefs{
                  sequencer,
                  tracks,
                  navigationFocus,
                  clipboard,
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
    h.sequencer.length.set(16);
    h.sequencer.page.set(1);
    h.sequencer.focusedStep.set(8);
    h.sequencer.note[8] = 72;
    h.sequencer.velocity[8] = 90;
    h.sequencer.setEnabled(8, true);

    h.press(Config::ButtonID::NAV);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.sequencer.structureUi.selection.active.get());
    assert(h.sequencer.structureUi.selection.scope.get() ==
           core::state::StructureSelectionScope::PAGE);
    assert(h.sequencer.structureUi.selection.cursorIndex.get() == 1);

    h.release(Config::ButtonID::NAV);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 1U);

    h.sequencer.structureUi.selection.selectedMask.set(0x0002);
    assert(h.sequencer.structureUi.selection.selectedMask.get() == 0x0002);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.sequencer.length.get() == 8);
    assert(h.sequencer.page.get() == 0);
    assert(h.sequencer.focusedStep.get() <= 7);
    assert(!h.sequencer.enabledMask.get().test(8));
    assert(!h.sequencer.structureUi.selection.active.get());

    std::cout << "[PASS] test_nav_selection_mode_deletes_selected_sequencer_page\n";
}

void test_sequencer_page_add_slot_is_terminal_and_does_not_wrap_on_reverse() {
    SequencerStepHarness h;
    h.sequencer.length.set(8);
    h.sequencer.page.set(0);
    h.sequencer.focusedStep.set(0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.sequencer.structureUi.previewAddSlot.get());
    assert(h.sequencer.page.get() == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.sequencer.structureUi.previewAddSlot.get());
    assert(h.sequencer.page.get() == 0);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(!h.sequencer.structureUi.previewAddSlot.get());
    assert(h.sequencer.page.get() == 0);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(!h.sequencer.structureUi.previewAddSlot.get());
    assert(h.sequencer.page.get() == 0);

    std::cout << "[PASS] test_sequencer_page_add_slot_is_terminal_and_does_not_wrap_on_reverse\n";
}

void test_nav_selection_mode_deletes_selected_sequencer_track() {
    SequencerStepHarness h;
    h.tracks.reset();
    h.tracks.setTrackEnabled(1, true);
    h.tracks.track(1).midiChannel.set(5);
    h.sequencer.midiChannel.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.tracks.activeTrack.get() == 1);
    assert(h.sequencer.midiChannel.get() == 5);

    h.press(Config::ButtonID::NAV);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.sequencer.structureUi.selection.active.get());
    assert(h.sequencer.structureUi.selection.scope.get() ==
           core::state::StructureSelectionScope::TRACK);
    assert(h.sequencer.structureUi.selection.cursorIndex.get() == 1);

    h.release(Config::ButtonID::NAV);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 1U);

    h.sequencer.structureUi.selection.selectedMask.set(0x0002);
    assert(h.sequencer.structureUi.selection.selectedMask.get() == 0x0002);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.tracks.enabledMask.get() == 0x0001);
    assert(h.tracks.activeTrack.get() == 0);
    assert(h.sequencer.midiChannel.get() == 0);
    assert(!h.sequencer.structureUi.selection.active.get());

    std::cout << "[PASS] test_nav_selection_mode_deletes_selected_sequencer_track\n";
}

void test_sequencer_page_copy_and_long_press_paste() {
    SequencerStepHarness h;
    h.sequencer.length.set(16);
    h.sequencer.page.set(0);
    h.sequencer.focusedStep.set(0);
    h.sequencer.note[0] = 72;
    h.sequencer.velocity[0] = 99;
    h.sequencer.gate[0] = 80;
    h.sequencer.nudge[0] = 3;
    h.sequencer.probability[0] = 87;
    h.sequencer.setEnabled(0, true);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.clipboard.hasSequencerPage());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.sequencer.page.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.sequencer.note[8] == 72);
    assert(h.sequencer.velocity[8] == 99);
    assert(h.sequencer.gate[8] == 80);
    assert(h.sequencer.nudge[8] == 3);
    assert(h.sequencer.probability[8] == 87);
    assert(h.sequencer.isEnabled(8));

    std::cout << "[PASS] test_sequencer_page_copy_and_long_press_paste\n";
}

}  // namespace

int main() {
    test_sequencer_page_add_slot_is_terminal_and_does_not_wrap_on_reverse();
    test_nav_selection_mode_deletes_selected_sequencer_page();
    test_nav_selection_mode_deletes_selected_sequencer_track();
    test_sequencer_page_copy_and_long_press_paste();

    std::cout << "\nAll SequencerStepHandler tests passed.\n";
    return 0;
}
