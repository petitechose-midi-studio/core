#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

#include <config/InputIDs.hpp>
#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "handler/sequencer/SequencerCcLaneDomainServices.hpp"
#include "handler/sequencer/SequencerCcLaneHandler.hpp"
#include "handler/sequencer/SequencerCcLaneWorkflow.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "validation/ux/SequencerCcLaneSemanticGesture.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

namespace seq = core::state::sequencer;
namespace contextual = core::state::contextual;
namespace input_utils = core::handler::sequencer::input_utils;

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

struct Harness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 1801;
    static constexpr oc::type::ScopeID CC_LANE_SCOPE = 1802;

    test_support::CoreStorages storages;
    core::state::CoreState state{
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary,
    };
    core::handler::SequencerCcLaneDomainServices services{
        {state.sequencer, state.sequencerTracks, nullptr}
    };
    core::handler::SequencerCcLaneWorkflow workflow{
        {state.sequencer,
         state.sequencerTracks,
         core::handler::SequencerHistoryDomainServices::fromCoreState(state),
         state.statusBar},
        services,
    };
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers> navigationFocus{
            core::state::StructureNavigationFocus::PAGE
        };
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding{eventBus, mockTimeMs};
    test_support::TestButtonHardware buttonHw;
    test_support::TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons{inputBinding, buttonHw};
    oc::api::EncoderAPI encoders{inputBinding, encoderHw};
    oc::context::OverlayManager<core::ui::OverlayType> overlays{
        state.overlays,
        buttons,
    };
    core::handler::SequencerPropertySelectorHandler propertySelector{
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
        mockTimeMs,
        &workflow,
        &overlays,
    };
    core::handler::SequencerCcLaneHandler handler{
        state.sequencer,
        workflow,
        propertySelector,
        overlays,
        encoders,
        buttons,
        CC_LANE_SCOPE,
        mockTimeMs,
    };

    Harness() {
        g_now_ms = 0;
        overlays.registerCleanup(core::ui::OverlayType::SEQ_CC_LANE, CC_LANE_SCOPE);
        overlays.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
    }

    void turnOpt(float normalized) {
        const auto id = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
        encoderHw.setPosition(id, normalized);
        eventBus.emit(oc::core::event::EncoderChangedEvent(id, normalized));
    }
};

void openAdd(Harness& h) {
    h.workflow.openLaneSelector();
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_SELECTOR);
    assert(h.workflow.selectorFocusesAdd());
    assert(h.workflow.activateSelector());
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::ADD_LANE_DRAFT);
}

void test_draft_is_silent_create_and_event_edit_coalesces() {
    Harness h;
    openAdd(h);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(contextual::canExecute(
        h.state.sequencer.ccLaneUi
            .action(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT).tap
    ));

    assert(h.workflow.executeTap(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 10));
    assert(h.state.sequencerHistory.undoCount() == 1);
    const auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].occupied);
    assert(!bank->lanes[0].activeMask.any());
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID);
    assert(!h.state.sequencer.ccLaneUi.hasAuthoredValue);

    // Turning `--` authors Initial exactly; continuing the same gesture edits
    // the value while retaining one pending history entry.
    assert(h.workflow.editFocusedEvent(1.0f, 100));
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank->lanes[0].values[0] == 64);
    assert(h.workflow.editFocusedEvent(1.0f, 120));
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].values[0] == 65);
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.workflow.update(500);
    assert(h.state.sequencerHistory.undoCount() == 2);

    assert(h.state.undoSequencerHistory());
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].occupied);
    assert(!bank->lanes[0].activeMask.test(0));
    assert(h.state.redoSequencerHistory());
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].values[0] == 65);
    test_support::drainNotifications();
    std::cout << "[PASS] silent draft/create and coalesced `--` -> Initial event edit\n";
}

void test_clear_settings_cancel_and_guarded_remove_are_exact_history() {
    Harness h;
    openAdd(h);
    assert(h.workflow.executeTap(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 10));
    assert(h.workflow.editFocusedEvent(1.0f, 20));
    h.workflow.update(500);
    assert(h.state.sequencerHistory.undoCount() == 2);

    assert(h.workflow.executeTap(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 600));
    assert(h.state.sequencerHistory.undoCount() == 3);
    assert(!seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].activeMask.test(0));
    assert(h.state.undoSequencerHistory());
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].activeMask.test(0));
    assert(h.state.redoSequencerHistory());
    assert(!seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].activeMask.test(0));

    assert(h.workflow.openSettings());
    const auto controller = h.state.sequencer.ccLaneUi.draft.destination.controller;
    h.workflow.editDraft(1.0f);
    assert(h.state.sequencer.ccLaneUi.draft.destination.controller == controller + 1U);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)
               ->lanes[0].destination.controller == controller);
    h.workflow.closeOneLevel(700);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)
               ->lanes[0].destination.controller == controller);
    assert(h.state.sequencerHistory.undoCount() == 3);

    assert(h.workflow.openSettings());
    const auto& removeSpec = h.state.sequencer.ccLaneUi.action(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT
    );
    assert(removeSpec.hold.action == contextual::ContextActionId::REMOVE);
    assert(removeSpec.hold.visual.tone == contextual::ContextTone::RED);

    // A short press is explicitly cancelled; it cannot leave PRESSED feedback
    // behind or remove the lane.
    assert(h.workflow.beginGuard(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 800));
    assert(!h.workflow.releaseGuard(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 850));
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].occupied);
    assert(h.state.sequencer.ccLaneUi.operationFeedback.get().status ==
           contextual::OperationFeedbackStatus::CANCELLED);
    assert(h.state.sequencer.ccLaneUi.actionGuard.get().phase ==
           contextual::GuardedActionPhase::IDLE);

    assert(h.workflow.beginGuard(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 1000));
    h.workflow.update(1400);
    assert(h.state.sequencer.ccLaneUi.actionGuard.get().phase ==
           contextual::GuardedActionPhase::ARMED);
    assert(h.state.sequencer.ccLaneUi.operationFeedback.get().status ==
           contextual::OperationFeedbackStatus::ARMED);
    h.workflow.update(1700);
    assert(h.workflow.releaseGuard(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 1700));
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);
    assert(h.state.sequencerHistory.undoCount() == 4);
    assert(h.state.undoSequencerHistory());
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].occupied);
    assert(h.state.redoSequencerHistory());
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);
    test_support::drainNotifications();
    std::cout << "[PASS] clear/cancel/remove semantics and exact history\n";
}

void test_semantic_gesture_classifier_never_claims_early_hold_mutation() {
    contextual::ContextActionSpec remove{};
    remove.hold = {
        .action = contextual::ContextActionId::REMOVE,
        .impact = contextual::ContextActionImpact::DESTRUCTIVE,
        .availability = contextual::ContextActionAvailability::AVAILABLE,
        .reason = contextual::ContextActionReason::NONE,
    };
    remove.guard = {
        .kind = contextual::ContextGuardKind::HOLD,
        .durationMs = seq::SequencerCcLaneUiState::ACTION_GUARD_MS,
    };
    contextual::OperationFeedbackState feedback{};
    contextual::GuardedActionState guard{};

    auto semantic = core::validation::ux::classifySequencerCcLaneGesture(
        remove,
        guard,
        feedback,
        core::validation::ux::SequencerCcLaneGesturePhase::PRESS
    );
    assert(std::strcmp(semantic.effect, "press_remove_cc_lane") == 0);
    assert(std::strcmp(semantic.outcome, "pressed") == 0);

    guard.phase = contextual::GuardedActionPhase::PRESSED;
    semantic = core::validation::ux::classifySequencerCcLaneGesture(
        remove,
        guard,
        feedback,
        core::validation::ux::SequencerCcLaneGesturePhase::RELEASE
    );
    assert(std::strcmp(semantic.effect, "cancel_remove_cc_lane") == 0);
    assert(std::strcmp(semantic.outcome, "cancelled") == 0);

    guard.phase = contextual::GuardedActionPhase::ARMED;
    semantic = core::validation::ux::classifySequencerCcLaneGesture(
        remove,
        guard,
        feedback,
        core::validation::ux::SequencerCcLaneGesturePhase::RELEASE
    );
    assert(std::strcmp(semantic.effect, "arm_remove_cc_lane") == 0);
    assert(std::strcmp(semantic.outcome, "armed") == 0);

    // Capture projections reuse the originating press while the guarded state
    // advances independently on update ticks.
    semantic = core::validation::ux::classifySequencerCcLaneGesture(
        remove,
        guard,
        feedback,
        core::validation::ux::SequencerCcLaneGesturePhase::PRESS
    );
    assert(std::strcmp(semantic.effect, "arm_remove_cc_lane") == 0);
    assert(std::strcmp(semantic.outcome, "armed") == 0);

    guard.phase = contextual::GuardedActionPhase::COMMITTED;
    semantic = core::validation::ux::classifySequencerCcLaneGesture(
        remove,
        guard,
        feedback,
        core::validation::ux::SequencerCcLaneGesturePhase::RELEASE
    );
    assert(std::strcmp(semantic.effect, "remove_cc_lane") == 0);
    assert(std::strcmp(semantic.outcome, "applied") == 0);

    std::cout << "[PASS] CC-lane semantic gestures distinguish press/arm/cancel/apply\n";
}

void test_guard_release_promotes_elapsed_hold_without_periodic_update() {
    const auto prepareRemove = [](Harness& h) {
        openAdd(h);
        assert(h.workflow.executeTap(
            seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 10
        ));
        assert(h.workflow.openSettings());
    };

    Harness shortHold;
    prepareRemove(shortHold);
    assert(shortHold.workflow.beginGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 100
    ));
    assert(!shortHold.workflow.releaseGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 150
    ));
    assert(seq::sequencerCcLaneView(shortHold.state.sequencer.pattern) != nullptr);
    assert(shortHold.state.sequencer.ccLaneUi.operationFeedback.get().status ==
           contextual::OperationFeedbackStatus::CANCELLED);

    Harness exactDeadline;
    prepareRemove(exactDeadline);
    assert(exactDeadline.workflow.beginGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 100
    ));
    assert(exactDeadline.workflow.releaseGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT,
        100 + seq::SequencerCcLaneUiState::ACTION_GUARD_MS
    ));
    assert(seq::sequencerCcLaneView(exactDeadline.state.sequencer.pattern) == nullptr);
    assert(exactDeadline.state.sequencer.ccLaneUi.operationFeedback.get().status ==
           contextual::OperationFeedbackStatus::APPLIED);

    Harness elapsedWithoutTick;
    prepareRemove(elapsedWithoutTick);
    assert(elapsedWithoutTick.workflow.beginGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 100
    ));
    assert(elapsedWithoutTick.workflow.releaseGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT,
        101 + seq::SequencerCcLaneUiState::ACTION_GUARD_MS
    ));
    assert(seq::sequencerCcLaneView(elapsedWithoutTick.state.sequencer.pattern) == nullptr);

    test_support::drainNotifications();
    std::cout
        << "[PASS] guarded release handles short/exact/elapsed holds without update tick\n";
}

void test_lane_duplicate_blocks_but_macro_conflict_uses_amber_hold() {
    Harness h;
    openAdd(h);
    assert(h.workflow.executeTap(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 10));
    h.workflow.openLaneSelector();
    h.workflow.moveSelector(1.0f);
    assert(h.workflow.selectorFocusesAdd());
    assert(h.workflow.activateSelector());
    const auto duplicateSpec = h.state.sequencer.ccLaneUi.action(
        seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT
    );
    assert(duplicateSpec.tap.availability == contextual::ContextActionAvailability::DISABLED);
    assert(duplicateSpec.tap.reason == contextual::ContextActionReason::CONFLICT);
    assert(!h.workflow.executeTap(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 20));
    assert(seq::sequencerCcLaneCount(*seq::sequencerCcLaneView(h.state.sequencer.pattern)) == 1);

    Harness macroHarness;
    auto& page = macroHarness.state.pages.activePageData();
    page.cc[0] = 74;
    macroHarness.state.pages.activeTrackData().channel =
        macroHarness.state.sequencer.pattern.midiChannel.get();
    core::handler::SequencerCcLaneDomainServices macroServices{
        {macroHarness.state.sequencer,
         macroHarness.state.sequencerTracks,
         &macroHarness.state.pages}
    };
    core::handler::SequencerCcLaneWorkflow macroWorkflow{
        {macroHarness.state.sequencer,
         macroHarness.state.sequencerTracks,
         core::handler::SequencerHistoryDomainServices::fromCoreState(macroHarness.state),
         macroHarness.state.statusBar},
        macroServices,
    };
    macroWorkflow.openLaneSelector();
    assert(macroWorkflow.activateSelector());
    const auto macroSpec = macroHarness.state.sequencer.ccLaneUi.action(
        seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT
    );
    assert(macroSpec.tap.availability == contextual::ContextActionAvailability::DISABLED);
    assert(macroSpec.hold.availability == contextual::ContextActionAvailability::WARNING);
    assert(macroSpec.hold.visual.tone == contextual::ContextTone::AMBER);
    assert(macroWorkflow.beginGuard(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 100));
    macroWorkflow.update(800);
    assert(macroWorkflow.releaseGuard(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 800));
    const auto* accepted = seq::sequencerCcLaneView(macroHarness.state.sequencer.pattern);
    assert(accepted != nullptr && accepted->lanes[0].acceptedMacroConflict);
    assert(macroHarness.state.sequencerHistory.undoCount() == 1);
    test_support::drainNotifications();
    std::cout << "[PASS] lane duplicate blocked; Macro conflict accepted by amber hold\n";
}

void test_projection_follows_transport_without_claiming_stopped_output_is_live() {
    Harness h;
    openAdd(h);
    assert(h.workflow.executeTap(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 10));
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID);
    assert(!h.state.sequencer.ccLaneUi.liveProjection);

    h.state.statusBar.playing.set(true);
    h.workflow.update(20);
    assert(h.state.sequencer.ccLaneUi.liveProjection);

    h.state.statusBar.playing.set(false);
    h.workflow.update(30);
    assert(!h.state.sequencer.ccLaneUi.liveProjection);
    test_support::drainNotifications();
    std::cout << "[PASS] stopped Preview and playing Live projections stay truthful\n";
}

void test_handler_owns_a_centered_directional_opt_contract() {
    Harness h;
    openAdd(h);

    constexpr auto OPT = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    h.encoderHw.setDiscreteSteps(OPT, 31);
    h.encoderHw.setPosition(OPT, 0.0f);
    h.overlays.show(core::ui::OverlayType::SEQ_CC_LANE);
    assert(h.state.sequencer.ccLaneUi.overlayVisible.get());
    h.handler.update(0);

    assert(h.encoderHw.getMode(OPT) == oc::interface::EncoderMode::NORMALIZED);
    assert(std::fabs(h.encoderHw.getBoundsMin(OPT) - 0.0f) < 0.0005f);
    assert(std::fabs(h.encoderHw.getBoundsMax(OPT) - 1.0f) < 0.0005f);
    assert(h.encoderHw.getDiscreteSteps(OPT) == 0);
    assert(h.encoderHw.getDiscreteTicksPerStep(OPT) ==
           input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    assert(std::fabs(h.encoderHw.getNormalizedTurns(OPT) -
                     input_utils::DEFAULT_NORMALIZED_TURNS) < 0.0005f);
    assert(std::fabs(h.encoderHw.getPosition(OPT) - 0.5f) < 0.0005f);

    const uint8_t controller = h.state.sequencer.ccLaneUi.draft.destination.controller;
    h.turnOpt(0.55f);
    assert(h.state.sequencer.ccLaneUi.draft.destination.controller ==
           static_cast<uint8_t>(controller + 1U));
    assert(std::fabs(h.encoderHw.getPosition(OPT) - 0.5f) < 0.0005f);

    h.turnOpt(0.45f);
    assert(h.state.sequencer.ccLaneUi.draft.destination.controller == controller);
    assert(std::fabs(h.encoderHw.getPosition(OPT) - 0.5f) < 0.0005f);

    h.turnOpt(std::numeric_limits<float>::quiet_NaN());
    assert(h.state.sequencer.ccLaneUi.draft.destination.controller == controller);
    assert(std::isfinite(h.encoderHw.getPosition(OPT)));
    assert(std::fabs(h.encoderHw.getPosition(OPT) - 0.5f) < 0.0005f);

    // Leaving and re-entering the overlay must reassert ownership after any
    // intervening property editor reconfiguration.
    h.overlays.hide();
    assert(!h.state.sequencer.ccLaneUi.overlayVisible.get());
    h.handler.update(10);
    h.encoderHw.setDiscreteSteps(OPT, 17);
    h.encoderHw.setPosition(OPT, 0.9f);
    h.overlays.show(core::ui::OverlayType::SEQ_CC_LANE);
    h.handler.update(20);
    assert(h.encoderHw.getDiscreteSteps(OPT) == 0);
    assert(std::fabs(h.encoderHw.getPosition(OPT) - 0.5f) < 0.0005f);

    test_support::drainNotifications();
    std::cout << "[PASS] CC-lane handler owns centered directional OPT safely\n";
}

void test_handler_registers_only_guard_capable_action_presses() {
    Harness h;

    // NAV release, three action releases, Back release, two guarded presses
    // (Remove/clear and conflict-confirm Apply), plus the property long press.
    // Settings is tap-only and must not consume a redundant press binding.
    constexpr std::size_t PROPERTY_SELECTOR_BINDINGS = 6U;
    assert(h.inputBinding.buttonBindingCount() == PROPERTY_SELECTOR_BINDINGS + 8U);

    test_support::drainNotifications();
    std::cout << "[PASS] CC-lane action bindings stay bounded\n";
}

}  // namespace

int main() {
    test_draft_is_silent_create_and_event_edit_coalesces();
    test_clear_settings_cancel_and_guarded_remove_are_exact_history();
    test_lane_duplicate_blocks_but_macro_conflict_uses_amber_hold();
    test_projection_follows_transport_without_claiming_stopped_output_is_live();
    test_handler_owns_a_centered_directional_opt_contract();
    test_handler_registers_only_guard_capable_action_presses();
    test_semantic_gesture_classifier_never_claims_early_hold_mutation();
    test_guard_release_promotes_elapsed_hold_without_periodic_update();
    std::cout << "All Sequencer CC lane workflow tests passed.\n";
    return 0;
}
