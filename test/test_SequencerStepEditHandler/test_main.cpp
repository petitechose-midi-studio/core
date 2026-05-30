#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include <config/Timing.hpp>

#include "../../src/handler/sequencer/SequencerStepEditHandler.hpp"
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

    SequencerStepEditHarness()
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
        , handler(core::handler::SequencerStepEditHandler::StateRefs{
                      state.overlays,
                      state.sequencer,
                      state.trackNavigation,
                  },
                  overlays,
                  encoders,
                  buttons,
                  SEQUENCER_SCOPE,
                  OVERLAY_SCOPE) {
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

    std::cout << "[PASS] test_nav_and_opt_edit_then_nav_apply\n";
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
    test_cancel_restores_snapshot();
    test_step_edit_does_not_open_when_blocked();

    std::cout << "\nAll SequencerStepEditHandler tests passed.\n";
    return 0;
}
