#include <cassert>
#include <cstring>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/sequencer/PatternPitchSettingsDomainServices.hpp"
#include "../../src/handler/sequencer/PatternPitchSettingsHandler.hpp"
#include "../../src/handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
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

using StepProperty = core::state::sequencer::StepProperty;

struct SequencerInlineHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 701;
    static constexpr oc::type::ScopeID PITCH_SETTINGS_SCOPE = 702;
    static constexpr oc::type::ScopeID PITCH_SELECTOR_SCOPE = 703;

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
    oc::context::OverlayManager<core::ui::OverlayType> overlayManager;
    core::handler::SequencerPropertySelectorHandler propertySelectorHandler;
    core::handler::PatternPitchSettingsHandler patternPitchSettingsHandler;
    core::handler::SequencerPatternQuickControlsHandler patternQuickControlsHandler;
    core::handler::SequencerMacroPropertyHandler macroPropertyHandler;

    SequencerInlineHarness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlayManager(state.overlays, buttons)
        , propertySelectorHandler(
              core::handler::SequencerPropertySelectorHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE,
              mockTimeMs
          )
        , patternPitchSettingsHandler(
              core::handler::PatternPitchSettingsHandler::StateRefs{
                  state.patternPitchSettings,
                  state.sequencer,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              core::handler::PatternPitchSettingsDomainServices{
                  core::handler::PatternPitchSettingsDomainServices::StateRefs{
                      state.sequencer,
                      state.sequencerTracks,
                  }
              },
              overlayManager,
              encoders,
              buttons,
              SEQUENCER_SCOPE,
              PITCH_SETTINGS_SCOPE,
              PITCH_SELECTOR_SCOPE
          )
        , patternQuickControlsHandler(
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
          )
        , macroPropertyHandler(
              core::handler::SequencerMacroPropertyHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.sequencerTracks,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE,
              mockTimeMs
          ) {
        overlayManager.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
        overlayManager.registerCleanup(
            core::ui::OverlayType::PATTERN_PITCH_SETTINGS,
            PITCH_SETTINGS_SCOPE
        );
        overlayManager.registerCleanup(
            core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR,
            PITCH_SELECTOR_SCOPE
        );
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

void openPropertySelector(SequencerInlineHarness& h) {
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());
}

void openPatternQuickControls(SequencerInlineHarness& h) {
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
}

void openPatternPitchSettings(SequencerInlineHarness& h) {
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::LEFT_BOTTOM);

    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.patternPitchSettings.visible.get());
    assert(h.state.patternPitchSettings.flowPhase.get() ==
           core::state::PatternPitchSettingsFlowPhase::OVERLAY);
}

void holdPatternQuickControls(SequencerInlineHarness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.physicalHoldActive.get());
}

void test_property_selector_left_top_closes_without_reverting_selected_property() {
    SequencerInlineHarness h;
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);

    openPropertySelector(h);
    assert(h.state.sequencer.stepPropertyInlineSelector.selectedIndex.get() == 4);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.activeStepProperty.get() == StepProperty::NUDGE);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencer.activeStepProperty.get() == StepProperty::NUDGE);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_property_selector_left_top_closes_without_reverting_selected_property\n";
}

void test_property_selector_apply_keeps_selected_property() {
    SequencerInlineHarness h;
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);

    openPropertySelector(h);
    h.turn(Config::EncoderID::NAV, -1.0f);
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

void test_track_focus_is_projected_as_pattern_outside_structure() {
    SequencerInlineHarness h;

    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    std::cout << "[PASS] test_track_focus_is_projected_as_pattern_outside_structure\n";
}

void test_state_is_a_direct_step_property() {
    SequencerInlineHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.focusedStep.set(0);

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencer.stepPropertyInlineSelector.selectedIndex.get() == 2);
    h.turn(Config::EncoderID::NAV, -1.0f);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.sequencer.stepStatePropertyActive.get());
    assert(h.state.sequencer.stepPropertyInlineSelector.selectedIndex.get() == 0);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.isEnabled(0));
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(std::strcmp(
        h.state.sequencer.historyFeedback.line2.data(),
        "Step 01 State"
    ) == 0);
    assert(std::strcmp(
        h.state.sequencer.historyFeedback.line3.data(),
        "On -> Off"
    ) == 0);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(0));

    h.turn(Config::MACRO_ENCODERS[1], 1.0f);
    assert(h.state.sequencer.pattern.isEnabled(1));
    h.turn(Config::MACRO_ENCODERS[1], 0.0f);
    assert(!h.state.sequencer.pattern.isEnabled(1));

    std::cout << "[PASS] test_state_is_a_direct_step_property\n";
}

void test_property_selector_is_unavailable_during_step_selection() {
    SequencerInlineHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.structureUi.stepSelection.active.set(true);

    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    std::cout << "[PASS] test_property_selector_is_unavailable_during_step_selection\n";
}

void test_property_selector_edits_active_property_variation_range() {
    SequencerInlineHarness h;
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);

    openPropertySelector(h);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.variationRanges.pitchSemitones == 36);

    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencer.pattern.variationRanges.pitchSemitones == 36);
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.variationRanges.pitchSemitones == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.variationRanges.pitchSemitones == 36);

    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    openPropertySelector(h);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.variationRanges.velocity == 127);

    std::cout << "[PASS] test_property_selector_edits_active_property_variation_range\n";
}

void test_property_selector_left_top_commits_live_variation_edit() {
    SequencerInlineHarness h;
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);
    h.state.sequencer.setVariationRangeForProperty(StepProperty::GATE, 12);

    openPropertySelector(h);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.variationRanges.gatePercent == 100);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencer.pattern.variationRanges.gatePercent == 100);
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.variationRanges.gatePercent == 12);

    std::cout << "[PASS] test_property_selector_left_top_commits_live_variation_edit\n";
}

void test_property_selector_left_top_commits_live_local_random_edit() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[2] = 64;

    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_3, 1.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(
        core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) ==
        127
    );

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph != nullptr) {
        node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
        assert(
            node == nullptr ||
            core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 0
        );
    }

    std::cout << "[PASS] test_property_selector_left_top_commits_live_local_random_edit\n";
}

void test_step_property_selector_left_bottom_is_secondary_random_layer() {
    SequencerInlineHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[2] = 64;

    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    h.release(Config::ButtonID::LEFT_BOTTOM);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    h.advance(100);
    h.press(Config::ButtonID::LEFT_BOTTOM);

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_3, 1.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive.get());

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(
        core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) ==
        127
    );

    h.release(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.undoSequencerHistory());
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph != nullptr) {
        node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
        assert(
            node == nullptr ||
            core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 0
        );
    }

    std::cout << "[PASS] test_step_property_selector_left_bottom_is_secondary_random_layer\n";
}

void test_property_selector_global_and_local_random_have_separate_undo() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[2] = 64;

    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.variationRanges.velocity == 127);

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_3, 1.0f);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 2);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 127);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.variationRanges.velocity == 0);
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 127);

    assert(h.state.undoSequencerHistory());
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph != nullptr) {
        node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
        assert(
            node == nullptr ||
            core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 0
        );
    }

    std::cout << "[PASS] test_property_selector_global_and_local_random_have_separate_undo\n";
}

void test_property_selector_does_not_edit_probability_variation() {
    SequencerInlineHarness h;
    h.state.sequencer.activeStepProperty.set(StepProperty::PROBABILITY);

    openPropertySelector(h);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.variationRanges.pitchSemitones == 0);
    assert(h.state.sequencer.pattern.variationRanges.velocity == 0);
    assert(h.state.sequencer.pattern.variationRanges.gatePercent == 0);
    assert(h.state.sequencer.pattern.variationRanges.nudge == 0);

    std::cout << "[PASS] test_property_selector_does_not_edit_probability_variation\n";
}

void test_pattern_quick_controls_do_not_edit_variation_range() {
    SequencerInlineHarness h;
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);

    openPatternQuickControls(h);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.variationRanges.velocity == 0);

    std::cout << "[PASS] test_pattern_quick_controls_do_not_edit_variation_range\n";
}

void test_pattern_pitch_settings_are_undoable() {
    SequencerInlineHarness h;

    openPatternPitchSettings(h);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.patternPitchSettings.flowPhase.get() ==
           core::state::PatternPitchSettingsFlowPhase::VALUE_SELECTOR);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);

    assert(h.state.sequencer.pattern.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);
    assert(h.state.sequencerHistory.undoCount() == 1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.patternPitchSettings.visible.get());

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT);
    assert(h.state.sequencerHistory.redoCount() == 1);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);

    std::cout << "[PASS] test_pattern_pitch_settings_are_undoable\n";
}

void test_pattern_quick_controls_short_tap_does_not_arm_history_layer() {
    SequencerInlineHarness h;

    h.tap(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(!h.state.sequencer.patternQuickControls.physicalHoldActive.get());

    std::cout << "[PASS] test_pattern_quick_controls_short_tap_does_not_arm_history_layer\n";
}

void test_pattern_quick_controls_hold_arms_history_layer() {
    SequencerInlineHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000);

    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.physicalHoldActive.get());

    std::cout << "[PASS] test_pattern_quick_controls_hold_arms_history_layer\n";
}

void test_pattern_quick_controls_are_pattern_focus_only() {
    SequencerInlineHarness h;
    const auto initialLength = h.state.sequencer.pattern.length.get();

    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.length.get() == initialLength);
    assert(!h.state.sequencer.patternQuickControls.feedbackVisible.get());
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.state.sequencer.structureUi.workspace.active.set(true);
    h.state.sequencer.structureUi.workspace.level.set(
        core::state::sequencer::SequencerStructureWorkspaceLevel::TRACKS
    );
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    h.state.sequencer.structureUi.workspace.active.set(false);

    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.patternQuickControls.selecting.get());

    std::cout << "[PASS] test_pattern_quick_controls_are_pattern_focus_only\n";
}

void test_pattern_quick_controls_open_defaults_to_length_and_cycles_order() {
    SequencerInlineHarness h;

    openPatternQuickControls(h);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::LENGTH
    );

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::DIVISION
    );

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::OFFSET
    );

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::SWING
    );

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::NUDGE
    );

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::LENGTH
    );

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::NUDGE
    );

    std::cout << "[PASS] test_pattern_quick_controls_open_defaults_to_length_and_cycles_order\n";
}

void test_pattern_quick_controls_history_noops_do_not_cancel_or_open_property_selector() {
    SequencerInlineHarness h;

    holdPatternQuickControls(h);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.physicalHoldActive.get());

    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.physicalHoldActive.get());
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(!h.state.sequencer.patternQuickControls.physicalHoldActive.get());

    std::cout << "[PASS] test_pattern_quick_controls_history_noops_do_not_cancel_or_open_property_selector\n";
}

void test_pattern_quick_controls_length_undo_redo_workflow() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.length.set(8);

    holdPatternQuickControls(h);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::LENGTH
    );
    h.turn(Config::EncoderID::OPT, 1.0f);
    const uint8_t appliedLength = h.state.sequencer.pattern.length.get();
    assert(appliedLength != 8);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.pattern.length.get() == 8);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.pattern.length.get() == appliedLength);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);

    std::cout << "[PASS] test_pattern_quick_controls_length_undo_redo_workflow\n";
}

void test_pattern_quick_controls_offset_undo_redo_workflow() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[0] = 60;
    h.state.sequencer.pattern.note[1] = 62;
    h.state.sequencer.pattern.note[7] = 67;
    h.state.sequencer.pattern.velocity[0] = 80;
    h.state.sequencer.pattern.velocity[1] = 91;
    h.state.sequencer.pattern.velocity[7] = 103;
    h.state.sequencer.pattern.setEnabled(0, true);
    h.state.sequencer.pattern.setEnabled(1, true);
    h.state.sequencer.pattern.setEnabled(7, true);

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::OFFSET
    );
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.pattern.isEnabled(6));
    assert(h.state.sequencer.pattern.isEnabled(7));
    assert(h.state.sequencer.pattern.note[0] == 62);
    assert(h.state.sequencer.pattern.note[6] == 67);
    assert(h.state.sequencer.pattern.note[7] == 60);
    assert(h.state.sequencer.pattern.velocity[0] == 91);
    assert(h.state.sequencer.pattern.velocity[6] == 103);
    assert(h.state.sequencer.pattern.velocity[7] == 80);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.pattern.isEnabled(1));
    assert(h.state.sequencer.pattern.isEnabled(7));
    assert(!h.state.sequencer.pattern.isEnabled(6));
    assert(h.state.sequencer.pattern.note[0] == 60);
    assert(h.state.sequencer.pattern.note[1] == 62);
    assert(h.state.sequencer.pattern.note[7] == 67);
    assert(h.state.sequencer.pattern.velocity[0] == 80);
    assert(h.state.sequencer.pattern.velocity[1] == 91);
    assert(h.state.sequencer.pattern.velocity[7] == 103);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.redoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.pattern.isEnabled(6));
    assert(h.state.sequencer.pattern.isEnabled(7));
    assert(!h.state.sequencer.pattern.isEnabled(1));
    assert(h.state.sequencer.pattern.note[0] == 62);
    assert(h.state.sequencer.pattern.note[6] == 67);
    assert(h.state.sequencer.pattern.note[7] == 60);
    assert(h.state.sequencer.pattern.velocity[0] == 91);
    assert(h.state.sequencer.pattern.velocity[6] == 103);
    assert(h.state.sequencer.pattern.velocity[7] == 80);
    h.release(Config::ButtonID::LEFT_CENTER);

    std::cout << "[PASS] test_pattern_quick_controls_offset_undo_redo_workflow\n";
}

void test_pattern_quick_controls_division_undo_redo_workflow() {
    SequencerInlineHarness h;
    const uint8_t initialDivision = h.state.sequencer.pattern.stepsPerBeat.get();

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::DIVISION
    );
    h.turn(Config::EncoderID::OPT, 1.0f);
    const uint8_t appliedDivision = h.state.sequencer.pattern.stepsPerBeat.get();
    assert(appliedDivision != initialDivision);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.pattern.stepsPerBeat.get() == initialDivision);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.redoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.pattern.stepsPerBeat.get() == appliedDivision);
    h.release(Config::ButtonID::LEFT_CENTER);

    std::cout << "[PASS] test_pattern_quick_controls_division_undo_redo_workflow\n";
}

void test_pattern_quick_controls_swing_and_nudge_workflow() {
    SequencerInlineHarness h;

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.swingOffsetPercent.get() == 75);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencerHistory.undoCount() == 1);

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::NUDGE
    );
    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.pattern.patternNudgePercent.get() == -50);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencerHistory.undoCount() == 2);

    std::cout << "[PASS] test_pattern_quick_controls_swing_and_nudge_workflow\n";
}

void test_pattern_quick_controls_opt_edits_focused_pattern_prop_without_hold() {
    SequencerInlineHarness h;

    openPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::SWING
    );

    g_now_ms = 100;
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.swingOffsetPercent.get() == 75);
    assert(h.state.sequencer.patternQuickControls.feedbackVisible.get());
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    assert(h.state.commitSequencerPatternHistoryCoalescing());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.swingOffsetPercent.get() == 0);

    std::cout << "[PASS] test_pattern_quick_controls_opt_edits_focused_pattern_prop_without_hold\n";
}

void test_pattern_quick_controls_undo_release_does_not_record_inverse_action() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.length.set(8);

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::OPT, 1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencerHistory.undoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    std::cout << "[PASS] test_pattern_quick_controls_undo_release_does_not_record_inverse_action\n";
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

    h.state.trackNavigation.selection.active.set(false);
    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    std::cout << "[PASS] test_pattern_quick_controls_respect_blocking_states\n";
}

}  // namespace

int main() {
    test_property_selector_left_top_closes_without_reverting_selected_property();
    test_property_selector_apply_keeps_selected_property();
    test_property_selector_does_not_open_when_pattern_quick_controls_are_active();
    test_track_focus_is_projected_as_pattern_outside_structure();
    test_state_is_a_direct_step_property();
    test_property_selector_is_unavailable_during_step_selection();
    test_property_selector_edits_active_property_variation_range();
    test_property_selector_left_top_commits_live_variation_edit();
    test_property_selector_left_top_commits_live_local_random_edit();
    test_step_property_selector_left_bottom_is_secondary_random_layer();
    test_property_selector_global_and_local_random_have_separate_undo();
    test_property_selector_does_not_edit_probability_variation();
    test_pattern_quick_controls_do_not_edit_variation_range();
    test_pattern_pitch_settings_are_undoable();
    test_pattern_quick_controls_short_tap_does_not_arm_history_layer();
    test_pattern_quick_controls_hold_arms_history_layer();
    test_pattern_quick_controls_are_pattern_focus_only();
    test_pattern_quick_controls_open_defaults_to_length_and_cycles_order();
    test_pattern_quick_controls_history_noops_do_not_cancel_or_open_property_selector();
    test_pattern_quick_controls_length_undo_redo_workflow();
    test_pattern_quick_controls_offset_undo_redo_workflow();
    test_pattern_quick_controls_division_undo_redo_workflow();
    test_pattern_quick_controls_swing_and_nudge_workflow();
    test_pattern_quick_controls_opt_edits_focused_pattern_prop_without_hold();
    test_pattern_quick_controls_undo_release_does_not_record_inverse_action();
    test_pattern_quick_controls_respect_blocking_states();

    std::cout << "\nAll SequencerInlineHandlers tests passed.\n";
    return 0;
}
