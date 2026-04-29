#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerPropertySelectorHandler.hpp"
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

using PatternItem = core::state::sequencer::PatternQuickControlItem;
using StepProperty = core::state::sequencer::StepProperty;

struct SequencerInlineHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 701;

    test_support::CoreStorages storages;
    core::state::CoreState state;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    core::handler::SequencerPropertySelectorHandler propertySelectorHandler;
    core::handler::SequencerPatternQuickControlsHandler patternQuickControlsHandler;

    SequencerInlineHarness()
        : state(storages.settings,
                storages.macroWorkspace,
                storages.macroLibrary,
                storages.sequencerWorkspace,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , propertySelectorHandler(
              core::handler::SequencerPropertySelectorHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          )
        , patternQuickControlsHandler(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          ) {
        g_now_ms = 0;
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

void openPropertySelector(SequencerInlineHarness& h) {
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());
}

void openPatternQuickControls(SequencerInlineHarness& h) {
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
}

void test_property_selector_cancel_restores_snapshot() {
    SequencerInlineHarness h;
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);

    openPropertySelector(h);
    assert(h.state.sequencer.stepPropertyInlineSelector.selectedIndex.get() == 2);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.activeStepProperty.get() == StepProperty::NUDGE);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencer.activeStepProperty.get() == StepProperty::GATE);

    std::cout << "[PASS] test_property_selector_cancel_restores_snapshot\n";
}

void test_property_selector_apply_keeps_selected_property() {
    SequencerInlineHarness h;
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);

    openPropertySelector(h);
    for (int i = 0; i < 4; ++i) {
        h.turn(Config::EncoderID::NAV, 1.0f);
    }
    assert(h.state.sequencer.activeStepProperty.get() == StepProperty::PROBABILITY);

    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencer.activeStepProperty.get() == StepProperty::PROBABILITY);

    std::cout << "[PASS] test_property_selector_apply_keeps_selected_property\n";
}

void test_property_selector_does_not_open_when_pattern_quick_controls_are_active() {
    SequencerInlineHarness h;
    h.state.sequencer.patternQuickControls.selecting.set(true);

    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    std::cout << "[PASS] test_property_selector_does_not_open_when_pattern_quick_controls_are_active\n";
}

void test_pattern_quick_controls_length_apply_clamps_focus() {
    SequencerInlineHarness h;
    h.state.sequencer.length.set(16);
    h.state.sequencer.focusedStep.set(10);
    h.state.sequencer.page.set(1);

    openPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() == PatternItem::LENGTH);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.length.get() == 1);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencer.page.get() == 0);

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.length.get() == 1);

    std::cout << "[PASS] test_pattern_quick_controls_length_apply_clamps_focus\n";
}

void test_pattern_quick_controls_cancel_restores_offset_snapshot() {
    SequencerInlineHarness h;
    h.state.sequencer.length.set(4);
    for (uint8_t i = 0; i < 4; ++i) {
        h.state.sequencer.note[i] = static_cast<uint8_t>(60 + i);
        h.state.sequencer.setEnabled(i, true);
    }

    openPatternQuickControls(h);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() == PatternItem::OFFSET);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 3);
    assert(h.state.sequencer.note[0] != 60);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 0);
    for (uint8_t i = 0; i < 4; ++i) {
        assert(h.state.sequencer.note[i] == static_cast<uint8_t>(60 + i));
        assert(h.state.sequencer.isEnabled(i));
    }

    std::cout << "[PASS] test_pattern_quick_controls_cancel_restores_offset_snapshot\n";
}

void test_pattern_quick_controls_respect_blocking_states() {
    SequencerInlineHarness h;

    h.state.overlays.show(core::ui::OverlayType::DATA_MANAGER);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.state.overlays.hideAll();
    h.state.sequencer.stepPropertyInlineSelector.selecting.set(true);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.state.sequencer.stepPropertyInlineSelector.reset();
    h.state.trackNavigation.selection.active.set(true);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    std::cout << "[PASS] test_pattern_quick_controls_respect_blocking_states\n";
}

}  // namespace

int main() {
    test_property_selector_cancel_restores_snapshot();
    test_property_selector_apply_keeps_selected_property();
    test_property_selector_does_not_open_when_pattern_quick_controls_are_active();
    test_pattern_quick_controls_length_apply_clamps_focus();
    test_pattern_quick_controls_cancel_restores_offset_snapshot();
    test_pattern_quick_controls_respect_blocking_states();

    std::cout << "\nAll SequencerInlineHandlers tests passed.\n";
    return 0;
}
