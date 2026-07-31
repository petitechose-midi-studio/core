#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/Timing.hpp>
#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "handler/sequencer/SequencerCcLaneDomainServices.hpp"
#include "handler/sequencer/SequencerCcLaneHandler.hpp"
#include "handler/sequencer/SequencerCcLaneWorkflow.hpp"
#include "sequencer/MidiCcGlobalFrameCoordinator.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"
#include "state/CoreState.hpp"
#include "state/project/ProjectSnapshot.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "validation/ux/SequencerCcLaneSemanticGesture.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/NotificationTestUtils.hpp"
#include "../support/ProjectTrackRuntimeSnapshotTestFixture.hpp"

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
    };
    core::handler::SequencerCcLaneDomainServices services{
        {state.sequencer, state.sequencerTracks, state.projectTracks, nullptr}
    };
    core::handler::SequencerCcLaneWorkflow workflow{
        {state.sequencer,
         state.sequencerTracks,
         state.projectNavigation,
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
    oc::core::input::InputBinding inputBinding{
        eventBus,
        mockTimeMs,
        Config::Input::CONFIG,
    };
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
        SEQUENCER_SCOPE,
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

    void turnMacro(uint8_t index, float normalized) {
        const auto id = static_cast<oc::type::EncoderID>(Config::MACRO_ENCODERS[index]);
        encoderHw.setPosition(id, normalized);
        eventBus.emit(oc::core::event::EncoderChangedEvent(id, normalized));
    }

    void turnNav(float delta) {
        const auto id = static_cast<oc::type::EncoderID>(Config::EncoderID::NAV);
        eventBus.emit(oc::core::event::EncoderChangedEvent(id, delta));
    }

    void press(Config::ButtonID id) {
        const auto button = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(button, true);
        eventBus.emit(oc::core::event::ButtonPressEvent(button, true));
    }

    void release(Config::ButtonID id) {
        const auto button = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(button, false);
        eventBus.emit(oc::core::event::ButtonReleaseEvent(button));
    }

    void advance(uint32_t elapsedMs) {
        g_now_ms += elapsedMs;
        inputBinding.processTick();
        handler.update(g_now_ms);
    }
};

void createDefaultLane(Harness& h, uint32_t nowMs = 10U) {
    h.workflow.openLaneSelector();
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_SELECTOR);
    for (uint8_t scan = 0; scan < seq::SequencerCcLaneBank::MAX_LANES + 1U &&
                           !h.workflow.selectorFocusesAdd(); ++scan) {
        h.workflow.moveSelector(1.0f);
    }
    assert(h.workflow.selectorFocusesAdd());
    assert(h.workflow.activateSelector(nowMs));
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID);
}

void test_slot_defaults_reuse_and_local_override_are_exact() {
    Harness h;
    h.state.projectNavigation.ccLaneDefaultControllers = {20U, 40U, 60U, 80U};

    for (uint8_t lane = 0; lane < seq::SequencerCcLaneBank::MAX_LANES; ++lane) {
        createDefaultLane(h, static_cast<uint32_t>(10U + lane));
        const auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
        assert(bank != nullptr && bank->lanes[lane].occupied);
        assert(bank->lanes[lane].destination.controller ==
               h.state.projectNavigation.ccLaneDefaultControllers[lane]);
        assert(!bank->lanes[lane].activeMask.any());
        assert(h.state.sequencerHistory.undoCount() == lane + 1U);
    }

    assert(h.workflow.openLane(0U));
    assert(h.workflow.openSettings());
    h.workflow.editDraft(1.0f);
    assert(h.state.sequencer.ccLaneUi.draft.destination.controller == 21U);
    assert(h.workflow.executeTap(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 30U));
    assert(h.state.projectNavigation.ccLaneDefaultControllers[0] == 20U);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)
               ->lanes[0].destination.controller == 21U);

    assert(h.workflow.openSettings());
    assert(h.workflow.beginGuard(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT, 100U));
    assert(h.workflow.releaseGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT,
        100U + seq::SequencerCcLaneUiState::ACTION_GUARD_MS
    ));
    assert(!seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].occupied);

    createDefaultLane(h, 1000U);
    const auto* reused = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(reused->lanes[0].occupied);
    assert(reused->lanes[0].destination.controller == 20U);
    assert(!reused->lanes[0].activeMask.any());

    test_support::drainNotifications();
    std::cout << "[PASS] four slot defaults, local override and first-slot reuse are exact\n";
}

void test_direct_default_create_is_silent_atomic_and_event_edit_coalesces() {
    Harness h;
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);
    assert(h.state.sequencerHistory.undoCount() == 0);
    createDefaultLane(h);
    assert(h.state.sequencerHistory.undoCount() == 1);
    const auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].occupied);
    assert(bank->lanes[0].destination.controller == 1U);
    assert(bank->lanes[0].destination.minimum == 0U);
    assert(bank->lanes[0].destination.maximum == 127U);
    assert(bank->lanes[0].destination.routePolicy ==
           seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK);
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
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.updateSequencerPatternHistoryCoalescing(500));
    assert(h.state.sequencerHistory.undoCount() == 2);

    assert(h.state.undoSequencerHistory());
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].occupied);
    assert(!bank->lanes[0].activeMask.test(0));
    assert(h.state.redoSequencerHistory());
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern)->lanes[0].values[0] == 65);
    test_support::drainNotifications();
    std::cout << "[PASS] direct default create is silent/atomic; event edit coalesces\n";
}

void test_pending_event_is_committed_before_cross_track_switch_and_replays_on_t1() {
    Harness h;
    createDefaultLane(h);
    assert(h.workflow.editFocusedEvent(1.0f, 100U));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);

    // The Track boundary must close T1 while it is still the active editor.
    assert(h.state.setSharedTrackState(0x0003U, 1U));
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 2U);
    const auto* t1 = seq::sequencerCcLaneView(h.state.sequencerTracks.track(0U));
    assert(t1 != nullptr && t1->lanes[0].activeMask.test(0U));
    assert(t1->lanes[0].values[0] == 64U);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);

    // Global history targets T1 without replacing or contaminating active T2.
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    t1 = seq::sequencerCcLaneView(h.state.sequencerTracks.track(0U));
    assert(t1 != nullptr && !t1->lanes[0].activeMask.test(0U));
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);
    assert(h.state.redoProjectHistory());
    t1 = seq::sequencerCcLaneView(h.state.sequencerTracks.track(0U));
    assert(t1 != nullptr && t1->lanes[0].activeMask.test(0U));
    assert(t1->lanes[0].values[0] == 64U);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);

    assert(h.state.setSharedTrackState(0x0003U, 0U));
    const auto* active = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(active != nullptr && active->lanes[0].activeMask.test(0U));
    assert(active->lanes[0].values[0] == 64U);

    test_support::drainNotifications();
    std::cout
        << "[PASS] pending CC edit closes on T1 before Track switch; Undo/Redo target T1\n";
}

void test_global_undo_commits_and_consumes_the_pending_cc_event_once() {
    Harness h;
    createDefaultLane(h);
    assert(h.workflow.editFocusedEvent(1.0f, 100U));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);

    assert(h.state.undoProjectHistory());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.redoCount() == 1U);
    const auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && !bank->lanes[0].activeMask.test(0U));

    assert(h.state.redoProjectHistory());
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].activeMask.test(0U));
    assert(bank->lanes[0].values[0] == 64U);

    test_support::drainNotifications();
    std::cout << "[PASS] global Undo commits and consumes one pending CC event\n";
}

void test_snapshot_and_reset_boundaries_see_or_purge_the_central_cc_transaction() {
    Harness h;
    createDefaultLane(h);
    assert(h.workflow.editFocusedEvent(1.0f, 100U));
    assert(h.state.hasPendingProjectTransaction());

    core::state::project::ProjectSnapshot snapshot;
    assert(core::state::project::captureProjectSnapshot(h.state, snapshot));
    const auto* captured = snapshot.sequencer.editorCcLanes.get();
    assert(captured != nullptr && captured->lanes[0].activeMask.test(0U));
    assert(captured->lanes[0].values[0] == 64U);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

    // A transient UI reset retains authored data but closes the history unit.
    h.state.resetStandaloneTransientUi();
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 2U);
    const auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].activeMask.test(0U));

    // A Project reset is destructive and must leave no stale transaction or
    // retained entry that could later address the replaced graph.
    assert(h.workflow.openLane(0U));
    assert(h.workflow.editFocusedEvent(1.0f, 200U));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    h.state.resetMusicalProject();
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(!h.state.projectHistory.canUndo());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);

    test_support::drainNotifications();
    std::cout << "[PASS] snapshots see pending CC data; reset boundaries close or purge it\n";
}

void test_immediate_create_and_settings_follow_pending_event_without_double_publish() {
    {
        Harness h;
        createDefaultLane(h);
        assert(h.workflow.editFocusedEvent(1.0f, 100U));
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

        h.workflow.openLaneSelector();
        while (!h.workflow.selectorFocusesAdd()) h.workflow.moveSelector(1.0f);
        assert(h.workflow.activateSelector(120U));
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencerHistory.undoCount() == 3U);

        assert(h.state.undoProjectHistory());
        auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
        assert(bank != nullptr && bank->lanes[0].occupied && !bank->lanes[1].occupied);
        assert(bank->lanes[0].activeMask.test(0U));
        assert(h.state.undoProjectHistory());
        bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
        assert(bank != nullptr && !bank->lanes[0].activeMask.test(0U));
    }

    {
        Harness h;
        createDefaultLane(h);
        assert(h.workflow.editFocusedEvent(1.0f, 100U));
        assert(h.workflow.openSettings());
        const uint8_t controller = h.state.sequencer.ccLaneUi.draft.destination.controller;
        h.workflow.editDraft(1.0f);
        assert(h.workflow.executeTap(
            seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT,
            120U
        ));
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencerHistory.undoCount() == 3U);

        assert(h.state.undoProjectHistory());
        auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
        assert(bank != nullptr);
        assert(bank->lanes[0].destination.controller == controller);
        assert(bank->lanes[0].activeMask.test(0U));
        assert(h.state.undoProjectHistory());
        bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
        assert(bank != nullptr && !bank->lanes[0].activeMask.test(0U));
    }

    test_support::drainNotifications();
    std::cout
        << "[PASS] immediate Create/Settings follow one pending CC entry in exact order\n";
}

void test_history_preflight_failure_does_not_publish_the_staged_cc_event() {
    Harness h;
    createDefaultLane(h);
    core::handler::SequencerCcLaneWorkflow unavailableHistory{
        {h.state.sequencer,
         h.state.sequencerTracks,
         h.state.projectNavigation,
         core::handler::SequencerHistoryDomainServices{},
         h.state.statusBar},
        h.services,
    };
    assert(unavailableHistory.openLane(0U));

    const auto* before = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(before != nullptr && !before->lanes[0].activeMask.test(0U));
    assert(!unavailableHistory.editFocusedEvent(1.0f, 100U));
    const auto* after = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(after != nullptr && !after->lanes[0].activeMask.test(0U));
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencer.ccLaneUi.operationFeedback.get().reason ==
           contextual::ContextActionReason::HISTORY_UNAVAILABLE);

    test_support::drainNotifications();
    std::cout << "[PASS] unavailable history rejects CC staging before publication\n";
}

void test_nav_grammar_toggles_events_and_reveals_advanced_settings() {
    Harness h;
    createDefaultLane(h);
    assert(h.workflow.openSettings());
    auto& ui = h.state.sequencer.ccLaneUi;
    assert(!ui.advancedSettings);

    h.workflow.moveDraftField(1.0f);
    assert(ui.focusedField == seq::SequencerCcLaneDraftField::ROUTE_POLICY);
    h.workflow.moveDraftField(1.0f);
    assert(ui.focusedField == seq::SequencerCcLaneDraftField::ADVANCED);
    assert(h.workflow.activateDraftField());
    assert(ui.advancedSettings);
    h.workflow.moveDraftField(-1.0f);
    assert(ui.focusedField == seq::SequencerCcLaneDraftField::INITIAL);
    const auto initial = ui.draft.initialValue;
    h.workflow.editDraft(1.0f);
    assert(ui.draft.initialValue == initial + 1U);

    assert(h.workflow.executeTap(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT, 10));
    assert(h.workflow.toggleFocusedEvent(20));
    const auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].activeMask.test(0));
    assert(bank->lanes[0].values[0] == initial + 1U);
    assert(h.workflow.toggleFocusedEvent(30));
    assert(!seq::sequencerCcLaneView(h.state.sequencer.pattern)
                ->lanes[0].activeMask.test(0));

    test_support::drainNotifications();
    std::cout << "[PASS] NAV grammar toggles events and progressive advanced settings\n";
}

void test_clear_settings_cancel_and_guarded_remove_are_exact_history() {
    Harness h;
    createDefaultLane(h);
    assert(h.workflow.editFocusedEvent(1.0f, 20));
    assert(h.state.updateSequencerPatternHistoryCoalescing(500));
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

void test_global_history_closes_a_cc_lane_session_when_its_lane_disappears() {
    Harness createUndoRedo;
    createDefaultLane(createUndoRedo);
    assert(createUndoRedo.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::LANE_GRID);

    assert(createUndoRedo.state.undoProjectHistory());
    assert(seq::sequencerCcLaneView(createUndoRedo.state.sequencer.pattern) == nullptr);
    assert(createUndoRedo.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::CLOSED);
    assert(!createUndoRedo.state.sequencer.ccLaneUi.visible());
    assert(!createUndoRedo.state.sequencer.ccLaneUi.overlayVisible.get());

    assert(createUndoRedo.state.redoProjectHistory());
    const auto* restored =
        seq::sequencerCcLaneView(createUndoRedo.state.sequencer.pattern);
    assert(restored != nullptr && restored->lanes[0].occupied);
    // Redo restores authored data only. The user explicitly reopens the lane.
    assert(createUndoRedo.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::CLOSED);
    assert(createUndoRedo.workflow.openLane(0U));

    Harness removeUndoReopenRedo;
    createDefaultLane(removeUndoReopenRedo);
    assert(removeUndoReopenRedo.workflow.openSettings());
    assert(removeUndoReopenRedo.workflow.beginGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT,
        100U
    ));
    assert(removeUndoReopenRedo.workflow.releaseGuard(
        seq::SequencerCcLaneActionSlot::BOTTOM_LEFT,
        100U + seq::SequencerCcLaneUiState::ACTION_GUARD_MS
    ));
    assert(seq::sequencerCcLaneView(removeUndoReopenRedo.state.sequencer.pattern) ==
           nullptr);

    assert(removeUndoReopenRedo.state.undoProjectHistory());
    restored = seq::sequencerCcLaneView(removeUndoReopenRedo.state.sequencer.pattern);
    assert(restored != nullptr && restored->lanes[0].occupied);
    assert(removeUndoReopenRedo.workflow.openLane(0U));
    assert(removeUndoReopenRedo.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::LANE_GRID);

    assert(removeUndoReopenRedo.state.redoProjectHistory());
    assert(seq::sequencerCcLaneView(removeUndoReopenRedo.state.sequencer.pattern) ==
           nullptr);
    assert(removeUndoReopenRedo.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::CLOSED);
    assert(!removeUndoReopenRedo.state.sequencer.ccLaneUi.visible());
    assert(!removeUndoReopenRedo.state.sequencer.ccLaneUi.overlayVisible.get());

    test_support::drainNotifications();
    std::cout
        << "[PASS] global history closes CC-lane sessions whose lane disappears\n";
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
        createDefaultLane(h);
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

void test_slot_defaults_conflicts_and_macro_arbitration_are_direct() {
    Harness h;
    createDefaultLane(h);
    h.state.projectNavigation.ccLaneDefaultControllers[1] = 1U;
    h.workflow.openLaneSelector();
    h.workflow.moveSelector(1.0f);
    assert(h.workflow.selectorFocusesAdd());
    assert(!h.workflow.activateSelector(20));
    assert(seq::sequencerCcLaneCount(*seq::sequencerCcLaneView(h.state.sequencer.pattern)) == 1);
    assert(h.state.sequencerHistory.undoCount() == 1);

    Harness macroHarness;
    auto& page = macroHarness.state.pages.activePageData();
    page.cc[0] = 1;
    assert(core::state::project::setProjectTrackMidiChannel(
        macroHarness.state.projectTracks, 0U, 9U
    ).changed());
    // Both the inherited Lane and active Macro route resolve through
    // ProjectTrackState.
    core::handler::SequencerCcLaneDomainServices macroServices{
        {macroHarness.state.sequencer,
         macroHarness.state.sequencerTracks,
         macroHarness.state.projectTracks,
         &macroHarness.state.pages}
    };
    core::handler::SequencerCcLaneWorkflow macroWorkflow{
        {macroHarness.state.sequencer,
         macroHarness.state.sequencerTracks,
         macroHarness.state.projectNavigation,
         core::handler::SequencerHistoryDomainServices::fromCoreState(macroHarness.state),
         macroHarness.state.statusBar},
        macroServices,
    };
    macroWorkflow.openLaneSelector();
    assert(macroWorkflow.activateSelector(100));
    const auto* accepted = seq::sequencerCcLaneView(macroHarness.state.sequencer.pattern);
    assert(accepted != nullptr && accepted->lanes[0].acceptedMacroConflict);
    assert(accepted->lanes[0].destination.pinnedChannel == 9U);
    assert(macroServices.trackRoute(0U).channel == 9U);
    assert(macroHarness.state.sequencerHistory.undoCount() == 1);
    test_support::drainNotifications();
    std::cout << "[PASS] duplicate default blocks atomically; Macro arbitration stays direct\n";
}

void test_live_projection_requires_the_lane_in_committed_runtime_telemetry() {
    Harness h;
    createDefaultLane(h);
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID);
    assert(!h.state.sequencer.ccLaneUi.liveProjection);

    // Merely starting transport is not evidence that this silent lane has
    // emitted anything. The UI must remain in its waiting state.
    h.state.statusBar.playing.set(true);
    h.workflow.update(20);
    assert(!h.state.sequencer.ccLaneUi.liveProjection);
    assert(!h.state.sequencer.ccLaneUi.hasResolvedValue);

    core::sequencer::RealtimeMidiQueue queue;
    core::sequencer::MidiCcGlobalFrameCoordinator coordinator{queue};
    core::handler::SequencerCcLaneWorkflow liveWorkflow{
        {h.state.sequencer,
         h.state.sequencerTracks,
         h.state.projectNavigation,
         core::handler::SequencerHistoryDomainServices::fromCoreState(h.state),
         h.state.statusBar,
         &coordinator},
        h.services,
    };
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    frame.candidateCount = 1;
    frame.candidates[0] = {
        .destination = {
            .identity = {
                .port = 0,
                .channel = core::state::project::projectTrackMidiChannel(
                    h.state.projectTracks, 0U
                ),
                .controller = 1,
            },
            .routeValidity = core::state::shared::MidiCcRouteValidity::VALID,
        },
        .author = {
            .candidateClass =
                core::state::shared::MidiCcCandidateClass::SEQUENCER_CC_LANE,
            .stableAddress = 0,
        },
        .localValue = 91,
    };
    assert(coordinator.publishSequencerLanes(frame));
    const auto runtimeTracks =
        test_support::makeAllAudibleProjectTrackRuntimeSnapshot();
    assert(coordinator.resolveLive(1000, runtimeTracks).ok());
    liveWorkflow.refreshProjection();
    assert(h.state.sequencer.ccLaneUi.liveProjection);
    assert(h.state.sequencer.ccLaneUi.hasResolvedValue);
    assert(h.state.sequencer.ccLaneUi.resolvedValue == 91);

    h.state.statusBar.playing.set(false);
    liveWorkflow.update(30);
    assert(!h.state.sequencer.ccLaneUi.liveProjection);
    test_support::drainNotifications();
    std::cout << "[PASS] Live projection requires committed lane runtime telemetry\n";
}

void test_handler_owns_a_centered_directional_opt_contract() {
    Harness h;
    createDefaultLane(h);
    assert(h.workflow.openSettings());

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

void test_eight_macro_controls_edit_visible_steps_and_long_hold_selects_shape() {
    Harness h;
    createDefaultLane(h);
    h.overlays.show(core::ui::OverlayType::SEQ_CC_LANE);
    h.handler.update(20);

    h.turnMacro(1, 1.0f);
    const auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr);
    assert(bank->lanes[0].activeMask.test(1));
    assert(bank->lanes[0].values[1] == 127);

    h.press(Config::ButtonID::MACRO_2);
    h.handler.update(g_now_ms);
    h.release(Config::ButtonID::MACRO_2);
    h.handler.update(g_now_ms);
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(!bank->lanes[0].activeMask.test(1));

    h.turnMacro(1, 0.75f);
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank->lanes[0].activeMask.test(1));
    assert(bank->lanes[0].values[1] == 95);

    h.press(Config::ButtonID::MACRO_2);
    h.handler.update(g_now_ms);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 1U);
    assert(h.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::TRANSITION_PICKER);
    h.release(Config::ButtonID::MACRO_2);
    h.handler.update(g_now_ms);
    assert(h.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::TRANSITION_PICKER);

    h.press(Config::ButtonID::NAV);
    h.turnNav(1.0f);
    assert(h.state.sequencer.ccLaneUi.selectedTransition ==
           seq::SequencerCcLaneTransition::LINEAR);
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID);
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 1) ==
           seq::SequencerCcLaneTransition::LINEAR);

    // The frequent path is direct: hold the matching Macro button, turn the
    // same encoder, inspect the temporary picker, and commit on release.
    h.press(Config::ButtonID::MACRO_2);
    h.handler.update(g_now_ms);
    h.turnMacro(1, 1.0f);
    assert(h.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::TRANSITION_PICKER);
    assert(h.state.sequencer.ccLaneUi.selectedTransition ==
           seq::SequencerCcLaneTransition::EASE_IN_OUT);
    h.release(Config::ButtonID::MACRO_2);
    h.handler.update(g_now_ms);
    assert(h.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::LANE_GRID);
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 1) ==
           seq::SequencerCcLaneTransition::EASE_IN_OUT);

    test_support::drainNotifications();
    std::cout << "[PASS] eight Macro controls map 1:1 to values, toggles and shapes\n";
}

void test_nav_tap_hold_and_hold_turn_have_distinct_cc_lane_grammar() {
    Harness h;
    createDefaultLane(h);

    // A genuine tap keeps the existing event toggle contract.
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    const auto* bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].activeMask.test(0));
    const std::size_t afterToggle = h.state.sequencerHistory.undoCount();

    // A hold without rotation is deliberately inert.
    h.press(Config::ButtonID::NAV);
    h.handler.update(g_now_ms);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 1U);
    h.release(Config::ButtonID::NAV);
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank->lanes[0].activeMask.test(0));
    assert(h.state.sequencerHistory.undoCount() == afterToggle);

    // Hold+turn opens only the compact current-choice card and commits on
    // release. It neither toggles the event nor moves the focused Step.
    h.press(Config::ButtonID::NAV);
    h.handler.update(g_now_ms);
    h.turnNav(1.0f);
    assert(h.state.sequencer.ccLaneUi.mode ==
           seq::SequencerCcLaneUiMode::TRANSITION_PICKER);
    assert(h.state.sequencer.ccLaneUi.compactTransitionPicker);
    assert(h.state.sequencer.ccLaneUi.selectedTransition ==
           seq::SequencerCcLaneTransition::LINEAR);
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID);
    assert(!h.state.sequencer.ccLaneUi.compactTransitionPicker);
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank->lanes[0].activeMask.test(0));
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 0) ==
           seq::SequencerCcLaneTransition::LINEAR);

    assert(h.state.undoProjectHistory());
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 0) ==
           seq::SequencerCcLaneTransition::HOLD);
    assert(h.state.redoProjectHistory());
    bank = seq::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 0) ==
           seq::SequencerCcLaneTransition::LINEAR);

    // Rotation without the button remains plain Step navigation.
    h.turnNav(1.0f);
    assert(h.state.sequencer.ccLaneUi.focusedStep == 1U);

    test_support::drainNotifications();
    std::cout << "[PASS] CC NAV tap/hold/hold-turn grammar is unambiguous\n";
}

void test_handler_registers_only_guard_capable_action_presses() {
    Harness h;

    // The property grammar has no CC-only long press or NAV shortcut: five
    // button bindings cover open/apply/cancel. CC owns seven overlay bindings
    // plus seven main-grid bindings; Macro keys remain polled only in context.
    constexpr std::size_t PROPERTY_SELECTOR_BINDINGS = 5U;
    constexpr std::size_t CC_LANE_BINDINGS = 14U;
    assert(h.inputBinding.buttonBindingCount() ==
           PROPERTY_SELECTOR_BINDINGS + CC_LANE_BINDINGS);

    test_support::drainNotifications();
    std::cout << "[PASS] CC-lane action bindings stay bounded\n";
}

}  // namespace

int main() {
    test_direct_default_create_is_silent_atomic_and_event_edit_coalesces();
    test_pending_event_is_committed_before_cross_track_switch_and_replays_on_t1();
    test_global_undo_commits_and_consumes_the_pending_cc_event_once();
    test_snapshot_and_reset_boundaries_see_or_purge_the_central_cc_transaction();
    test_immediate_create_and_settings_follow_pending_event_without_double_publish();
    test_history_preflight_failure_does_not_publish_the_staged_cc_event();
    test_slot_defaults_reuse_and_local_override_are_exact();
    test_nav_grammar_toggles_events_and_reveals_advanced_settings();
    test_clear_settings_cancel_and_guarded_remove_are_exact_history();
    test_global_history_closes_a_cc_lane_session_when_its_lane_disappears();
    test_slot_defaults_conflicts_and_macro_arbitration_are_direct();
    test_live_projection_requires_the_lane_in_committed_runtime_telemetry();
    test_handler_owns_a_centered_directional_opt_contract();
    test_eight_macro_controls_edit_visible_steps_and_long_hold_selects_shape();
    test_nav_tap_hold_and_hold_turn_have_distinct_cc_lane_grammar();
    test_handler_registers_only_guard_capable_action_presses();
    test_semantic_gesture_classifier_never_claims_early_hold_mutation();
    test_guard_release_promotes_elapsed_hold_without_periodic_update();
    std::cout << "All Sequencer CC lane workflow tests passed.\n";
    return 0;
}
