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
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;
bool g_prepared_begin_seen = false;
core::state::sequencer::SequencerCoalescedPatternPayloadPlan g_prepared_payload_plan =
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly;

uint32_t mockTimeMs() { return g_now_ms; }

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

using StepProperty = core::state::sequencer::StepProperty;

core::state::sequencer::SequencerPreparedPatternEditBeginOutcome rejectPreparedPatternEdit(
    void*, core::state::sequencer::SequencerPreparedPatternEditOwner, uint8_t,
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    core::state::sequencer::SequencerHistoryDescriptor, bool) {
    g_prepared_begin_seen = true;
    g_prepared_payload_plan = payloadPlan;
    return core::state::sequencer::SequencerPreparedPatternEditBeginOutcome::ResourceUnavailable;
}

core::state::sequencer::SequencerPatternHistoryCommitOutcome noPendingPatternEdit(void*) {
    return core::state::sequencer::SequencerPatternHistoryCommitOutcome::NoPending;
}

core::handler::SequencerHistoryDomainServices preparedHistoryServices(core::state::CoreState& state,
                                                                      bool rejectPreparedEdits) {
    if (!rejectPreparedEdits) {
        return core::handler::SequencerHistoryDomainServices::fromCoreState(state);
    }
    static constexpr core::handler::SequencerHistoryDomainServices::Operations operations{
        .commitCoalescedPatternEdit = noPendingPatternEdit,
        .beginPreparedPatternEdit = rejectPreparedPatternEdit,
    };
    return core::handler::SequencerHistoryDomainServices::fromStaticOperations<operations>(nullptr);
}

struct SequencerInlineHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 701;
    static constexpr oc::type::ScopeID PITCH_SETTINGS_SCOPE = 702;
    static constexpr oc::type::ScopeID PITCH_SELECTOR_SCOPE = 703;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::state::Signal<core::state::StructureNavigationFocus,
                      core::state::kStructureNavigationFocusMaxSubscribers>
        navigationFocus;

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

    explicit SequencerInlineHarness(bool rejectPreparedEdits = false)
        : state(storages.settings), navigationFocus(core::state::StructureNavigationFocus::PAGE),
          inputBinding(eventBus, mockTimeMs), buttons(inputBinding, buttonHw),
          encoders(inputBinding, encoderHw), overlayManager(state.overlays, buttons),
          propertySelectorHandler(
              core::handler::SequencerPropertySelectorHandler::StateRefs{
                   state.overlays,
                   state.sequencer,
                   state.sequencerTracks,
                   state.trackNavigation,
                  navigationFocus,
                  preparedHistoryServices(state, rejectPreparedEdits),
              },
              encoders, buttons, SEQUENCER_SCOPE, mockTimeMs),
          patternPitchSettingsHandler(
              core::handler::PatternPitchSettingsHandler::StateRefs{
                  state.patternPitchSettings,
                  state.sequencer,
                  preparedHistoryServices(state, rejectPreparedEdits),
              },
              core::handler::PatternPitchSettingsDomainServices{
                  core::handler::PatternPitchSettingsDomainServices::StateRefs{
                      state.sequencer,
                      state.sequencerTracks,
                  }},
              overlayManager, encoders, buttons, SEQUENCER_SCOPE, PITCH_SETTINGS_SCOPE,
              PITCH_SELECTOR_SCOPE),
          patternQuickControlsHandler(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders, buttons, SEQUENCER_SCOPE),
          macroPropertyHandler(
              core::handler::SequencerMacroPropertyHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.sequencerTracks,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders, buttons, SEQUENCER_SCOPE, mockTimeMs) {
        overlayManager.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
        overlayManager.registerCleanup(core::ui::OverlayType::PATTERN_PITCH_SETTINGS,
                                       PITCH_SETTINGS_SCOPE);
        overlayManager.registerCleanup(core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR,
                                       PITCH_SELECTOR_SCOPE);
        g_now_ms = 0;
        g_prepared_begin_seen = false;
        g_prepared_payload_plan =
            core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly;
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

    std::cout
        << "[PASS] test_property_selector_left_top_closes_without_reverting_selected_property\n";
}

void test_property_selector_stays_open_when_history_barrier_fails() {
    namespace seq = core::state::sequencer;
    SequencerInlineHarness h;
    openPropertySelector(h);

    constexpr auto owner = seq::SequencerPreparedPatternEditOwner::PatternPitch;
    const auto descriptor = seq::SequencerHistoryDescriptor{
        .kind = seq::SequencerHistoryActionKind::PatternSettings,
        .trackIndex = 0U,
    };
    assert(h.state.beginOrContinueSequencerPreparedPatternEdit(
               owner, 0U, seq::SequencerCoalescedPatternPayloadPlan::FlatOnly, descriptor) ==
           seq::SequencerPreparedPatternEditBeginOutcome::Started);

    const uint32_t feedbackRevisionBefore = h.state.sequencer.historyFeedback.revision.get();
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.sequencer.historyFeedback.revision.get() == feedbackRevisionBefore + 1U);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "No change") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "History unavailable") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "") == 0);

    assert(h.state.sealSequencerPreparedPatternEdit(owner, 0U, false, descriptor) ==
           seq::SequencerPreparedPatternEditSealOutcome::Cleared);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    std::cout << "[PASS] Property Selector remains owned across failed history barrier\n";
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

    std::cout
        << "[PASS] test_property_selector_does_not_open_when_pattern_quick_controls_are_active\n";
}

void test_track_focus_remains_distinct_from_pattern_outside_structure() {
    SequencerInlineHarness h;

    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    std::cout << "[PASS] test_track_focus_remains_distinct_from_pattern_outside_structure\n";
}

void test_state_is_a_direct_step_property() {
    SequencerInlineHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.pattern.setContentLength(8);
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
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Step 01 State") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "On -> Off") == 0);
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

void test_property_selector_rejected_prepare_blocks_edit_and_feedback() {
    SequencerInlineHarness h(true);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    openPropertySelector(h);

    g_now_ms = 100;
    const uint32_t feedbackDeadline = h.state.sequencer.patternVariationFeedback.hideAtMs;
    const uint32_t historyFeedbackRevision = h.state.sequencer.historyFeedback.revision.get();
    h.turn(Config::EncoderID::OPT, 1.0f);

    assert(g_prepared_begin_seen);
    assert(g_prepared_payload_plan ==
           core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly);
    assert(h.state.sequencer.pattern.variationRanges.velocity == 0);
    assert(h.state.sequencer.patternVariationFeedback.hideAtMs == feedbackDeadline);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.historyFeedback.revision.get() == historyFeedbackRevision + 1U);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "No change") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Memory unavailable") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "") == 0);

    std::cout << "[PASS] Property Selector resource rejection is visible and atomic\n";
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
    h.state.sequencer.pattern.setContentLength(8);
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
    assert(core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 127);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph != nullptr) {
        node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
        assert(node == nullptr ||
               core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 0);
    }

    std::cout << "[PASS] test_property_selector_left_top_commits_live_local_random_edit\n";
}

void test_property_selector_local_exact_return_closes_without_history() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    assert(core::state::sequencer::ensureGraphRoot(h.state.sequencer.pattern));

    h.press(Config::ButtonID::LEFT_BOTTOM);
    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_3, 1.0f);
    h.turn(Config::EncoderID::MACRO_3, 0.0f);

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_property_selector_local_exact_return_closes_without_history\n";
}

void test_step_property_selector_left_bottom_is_secondary_random_layer() {
    SequencerInlineHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.pattern.setContentLength(8);
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
    assert(core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 127);

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
        assert(node == nullptr ||
               core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 0);
    }

    std::cout << "[PASS] test_step_property_selector_left_bottom_is_secondary_random_layer\n";
}

void test_property_selector_global_and_local_random_have_separate_undo() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.setContentLength(8);
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
    assert(h.state.sequencer.pattern.variationRanges.velocity == 127);
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph != nullptr) {
        node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
        assert(node == nullptr ||
               core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 0);
    }

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.variationRanges.velocity == 0);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.variationRanges.velocity == 127);
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph != nullptr) {
        node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
        assert(node == nullptr ||
               core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 0);
    }
    assert(h.state.redoSequencerHistory());
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 127);

    std::cout << "[PASS] test_property_selector_global_and_local_random_have_separate_undo\n";
}

void test_property_selector_local_then_global_undo_follows_chronology() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[2] = 64;

    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_3, 1.0f);
    h.turn(Config::EncoderID::OPT, 1.0f);
    h.tap(Config::ButtonID::LEFT_TOP);

    assert(h.state.sequencerHistory.undoCount() == 2);
    assert(h.state.sequencer.pattern.variationRanges.velocity == 127);
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
        assert(node == nullptr ||
               core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 0);
    }

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.variationRanges.velocity == 0);
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::VELOCITY) == 127);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.variationRanges.velocity == 127);

    std::cout << "[PASS] test_property_selector_local_then_global_undo_follows_chronology\n";
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

void test_pattern_pitch_rejected_prepare_blocks_projection_and_feedback() {
    SequencerInlineHarness h(true);
    const auto ownerNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::beginStepContentDraft(
        h.state.sequencer, core::state::sequencer::SequencerStepContentDraftKind::MICRO_SEQUENCE, 0,
        ownerNode));
    const auto* draftBefore = h.state.sequencer.stepContentDraft.pattern();
    assert(draftBefore != nullptr);
    assert(draftBefore->scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT);
    openPatternPitchSettings(h);

    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    const uint32_t feedbackRevision = h.state.sequencer.historyFeedback.revision.get();
    h.tap(Config::ButtonID::NAV);

    assert(g_prepared_begin_seen);
    assert(g_prepared_payload_plan ==
           core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly);
    assert(h.state.sequencer.pattern.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT);
    const auto* draftAfter = h.state.sequencer.stepContentDraft.pattern();
    assert(draftAfter != nullptr);
    assert(draftAfter->scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT);
    assert(h.state.patternPitchSettings.flowPhase.get() ==
           core::state::PatternPitchSettingsFlowPhase::VALUE_SELECTOR);
    assert(h.state.sequencer.historyFeedback.revision.get() == feedbackRevision + 1U);
    assert(h.state.sequencer.historyFeedback.visible.get());
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "No change") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Memory unavailable") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "") == 0);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] Pattern Pitch resource rejection is visible and atomic\n";
}

void test_pattern_pitch_payload_plan_tracks_enabled_graph() {
    {
        SequencerInlineHarness h(true);
        assert(core::state::sequencer::ensureGraphRoot(h.state.sequencer.pattern));
        openPatternPitchSettings(h);
        h.tap(Config::ButtonID::NAV);
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.tap(Config::ButtonID::NAV);
        assert(g_prepared_begin_seen);
        assert(g_prepared_payload_plan ==
               core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload);
    }

    {
        SequencerInlineHarness h(true);
        assert(core::state::sequencer::ensureGraphRoot(h.state.sequencer.pattern));
        h.state.sequencer.pattern.graph->enabled = false;
        openPatternPitchSettings(h);
        h.tap(Config::ButtonID::NAV);
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.tap(Config::ButtonID::NAV);
        assert(g_prepared_begin_seen);
        assert(g_prepared_payload_plan ==
               core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly);
    }

    std::cout << "[PASS] test_pattern_pitch_payload_plan_tracks_enabled_graph\n";
}

void test_pattern_quick_controls_short_tap_opens_one_edit_layer() {
    SequencerInlineHarness h;

    h.tap(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencer.patternQuickControls.selecting.get());

    std::cout << "[PASS] test_pattern_quick_controls_short_tap_opens_one_edit_layer\n";
}

void test_pattern_quick_controls_hold_keeps_one_edit_layer() {
    SequencerInlineHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000);

    assert(h.state.sequencer.patternQuickControls.selecting.get());

    std::cout << "[PASS] test_pattern_quick_controls_hold_keeps_one_edit_layer\n";
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
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.patternQuickControls.selecting.get());

    std::cout << "[PASS] test_pattern_quick_controls_are_pattern_focus_only\n";
}

void test_pattern_quick_controls_open_defaults_to_length_and_cycles_order() {
    SequencerInlineHarness h;

    openPatternQuickControls(h);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::LENGTH);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::DIVISION);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::OFFSET);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::SWING);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::NUDGE);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::LENGTH);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::NUDGE);

    std::cout << "[PASS] test_pattern_quick_controls_open_defaults_to_length_and_cycles_order\n";
}

void test_pattern_quick_controls_left_top_is_cancel_not_local_history() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(core::state::sequencer::authoringPattern(h.state.sequencer)
               .length.get() != 8);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(!h.state.sequencer.stepPropertyInlineSelector.selecting.get());

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 0U);

    std::cout << "[PASS] test_pattern_quick_controls_left_top_is_cancel_not_local_history\n";
}

void test_pattern_quick_controls_length_undo_redo_workflow() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    holdPatternQuickControls(h);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::LENGTH);
    h.turn(Config::EncoderID::OPT, 1.0f);
    const uint8_t appliedLength =
        core::state::sequencer::authoringPattern(h.state.sequencer).length.get();
    assert(appliedLength != 8);
    assert(h.state.sequencer.pattern.length.get() == 8);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.pattern.length.get() == appliedLength);
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.length.get() == 8);

    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.length.get() == appliedLength);

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);

    std::cout << "[PASS] test_pattern_quick_controls_length_undo_redo_workflow\n";
}

void test_pattern_quick_controls_offset_undo_redo_workflow() {
    SequencerInlineHarness h;
    h.state.sequencer.pattern.setContentLength(8);
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
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::OFFSET);
    h.turn(Config::EncoderID::OPT, 1.0f);
    const auto& preview =
        core::state::sequencer::authoringPattern(h.state.sequencer);
    assert(preview.isEnabled(0));
    assert(preview.isEnabled(6));
    assert(preview.isEnabled(7));
    assert(preview.note[0] == 62);
    assert(preview.note[6] == 67);
    assert(preview.note[7] == 60);
    assert(preview.velocity[0] == 91);
    assert(preview.velocity[6] == 103);
    assert(preview.velocity[7] == 80);
    assert(h.state.sequencer.pattern.isEnabled(1));
    assert(!h.state.sequencer.pattern.isEnabled(6));
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencer.pattern.isEnabled(6));
    assert(!h.state.sequencer.pattern.isEnabled(1));

    assert(h.state.undoProjectHistory());
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
    assert(h.state.sequencerHistory.redoCount() == 1);

    assert(h.state.redoProjectHistory());
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
    std::cout << "[PASS] test_pattern_quick_controls_offset_undo_redo_workflow\n";
}

void test_pattern_quick_controls_division_undo_redo_workflow() {
    SequencerInlineHarness h;
    const uint8_t initialDivision = h.state.sequencer.pattern.stepsPerBeat.get();

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::DIVISION);
    h.turn(Config::EncoderID::OPT, 1.0f);
    const uint8_t appliedDivision =
        core::state::sequencer::authoringPattern(h.state.sequencer)
            .stepsPerBeat.get();
    assert(appliedDivision != initialDivision);
    assert(h.state.sequencer.pattern.stepsPerBeat.get() == initialDivision);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencer.pattern.stepsPerBeat.get() == appliedDivision);

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.stepsPerBeat.get() == initialDivision);

    assert(h.state.sequencerHistory.redoCount() == 1);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.stepsPerBeat.get() == appliedDivision);

    std::cout << "[PASS] test_pattern_quick_controls_division_undo_redo_workflow\n";
}

void test_pattern_quick_controls_swing_and_nudge_workflow() {
    SequencerInlineHarness h;

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::SWING);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.swingOffsetPercent.get() == 0);
    assert(core::state::sequencer::authoringPattern(h.state.sequencer)
               .swingOffsetPercent.get() == 75);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.pattern.swingOffsetPercent.get() == 75);
    assert(h.state.sequencerHistory.undoCount() == 1);

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::NUDGE);
    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.pattern.patternNudgePercent.get() == 0);
    assert(core::state::sequencer::authoringPattern(h.state.sequencer)
               .patternNudgePercent.get() == -50);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.pattern.patternNudgePercent.get() == -50);
    assert(h.state.sequencerHistory.undoCount() == 2);

    std::cout << "[PASS] test_pattern_quick_controls_swing_and_nudge_workflow\n";
}

void test_pattern_quick_controls_opt_edits_focused_pattern_prop_without_hold() {
    SequencerInlineHarness h;

    openPatternQuickControls(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::SWING);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::SWING);

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
    h.state.sequencer.pattern.setContentLength(8);

    holdPatternQuickControls(h);
    h.turn(Config::EncoderID::OPT, 1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoProjectHistory());

    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    std::cout << "[PASS] test_pattern_quick_controls_undo_release_does_not_record_inverse_action\n";
}

void test_pattern_quick_controls_respect_blocking_states() {
    SequencerInlineHarness h;

    h.state.overlays.show(core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.state.overlays.hideAll();
    h.state.sequencer.stepPropertyInlineSelector.selecting.set(true);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.state.sequencer.stepPropertyInlineSelector.reset();
    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    std::cout << "[PASS] test_pattern_quick_controls_respect_blocking_states\n";
}

}  // namespace

int main() {
    test_property_selector_left_top_closes_without_reverting_selected_property();
    test_property_selector_stays_open_when_history_barrier_fails();
    test_property_selector_apply_keeps_selected_property();
    test_property_selector_does_not_open_when_pattern_quick_controls_are_active();
    test_track_focus_remains_distinct_from_pattern_outside_structure();
    test_state_is_a_direct_step_property();
    test_property_selector_is_unavailable_during_step_selection();
    test_property_selector_edits_active_property_variation_range();
    test_property_selector_rejected_prepare_blocks_edit_and_feedback();
    test_property_selector_left_top_commits_live_variation_edit();
    test_property_selector_left_top_commits_live_local_random_edit();
    test_property_selector_local_exact_return_closes_without_history();
    test_step_property_selector_left_bottom_is_secondary_random_layer();
    test_property_selector_global_and_local_random_have_separate_undo();
    test_property_selector_local_then_global_undo_follows_chronology();
    test_property_selector_does_not_edit_probability_variation();
    test_pattern_quick_controls_do_not_edit_variation_range();
    test_pattern_pitch_settings_are_undoable();
    test_pattern_pitch_rejected_prepare_blocks_projection_and_feedback();
    test_pattern_pitch_payload_plan_tracks_enabled_graph();
    test_pattern_quick_controls_short_tap_opens_one_edit_layer();
    test_pattern_quick_controls_hold_keeps_one_edit_layer();
    test_pattern_quick_controls_are_pattern_focus_only();
    test_pattern_quick_controls_open_defaults_to_length_and_cycles_order();
    test_pattern_quick_controls_left_top_is_cancel_not_local_history();
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
