#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/macro/MacroPerformanceHandler.hpp"
#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/handler/macro/MacroStructureDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::CoreStorages;
using test_support::drainNotifications;
using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct MacroPerformanceHarness {
    static constexpr oc::type::ScopeID MACRO_VIEW_SCOPE = 401;

    CoreStorages storage;
    core::state::CoreState state;
    core::handler::MacroPerformanceDomainServices performanceServices;
    core::handler::MacroStructureDomainServices structureServices;
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
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::MacroPerformanceHandler handler;

    MacroPerformanceHarness()
        : state(storage.settings,
                storage.macroLibrary,
                storage.sequencerPatternLibrary,
                storage.sequencerSetLibrary)
        , performanceServices(core::handler::MacroPerformanceDomainServices::fromCoreState(state))
        , structureServices(core::handler::MacroStructureDomainServices::fromCoreState(state))
        , navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(
              core::handler::MacroPerformanceHandler::StateRefs{
                  state.macroUi,
                  state.pages,
                  state.trackNavigation,
              state.sharedTrackActive,
              navigationFocus,
              clipboard,
              },
              performanceServices,
              structureServices,
              overlays,
              encoders,
              buttons,
              MACRO_VIEW_SCOPE,
              mockTimeMs
          ) {
        g_now_ms = 0;
        overlays.setActiveViewProvider([]() { return MACRO_VIEW_SCOPE; });
    }

    ~MacroPerformanceHarness() {
        drainNotifications();
    }

    void tick(uint32_t nowMs) {
        g_now_ms = nowMs;
        inputBinding.processTick();
        handler.update(nowMs);
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

void configureMacroAutomation(core::state::CoreState& state,
                              uint8_t track,
                              uint8_t page,
                              uint8_t macro,
                              float value) {
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = track,
            .page = page,
            .macro = macro,
        }
    );
    assert(slot != nullptr);
    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 1.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, value));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, value));
    assert(core::state::macro::macroAutomationAssignAutomation(
        state.pages.automation,
        *slot,
        lane
    ));
}

void test_nav_turn_previews_macro_page_until_nav_commit() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0003;
    h.state.pages.syncActiveTrackCache();
    std::strncpy(h.state.pages.activeTrackData().pages[1].name,
                 "Page 2",
                 core::state::macro::PAGE_NAME_SIZE - 1);
    h.state.pages.activeTrackData().pages[1].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';

    h.turn(Config::EncoderID::NAV, 1.0f);

    assert(h.state.macroUi.previewPageIndex.get() == 1);
    assert(h.state.pages.currentActivePage() == 0);
    assert(!h.state.macroUi.previewAddPageSlot.get());

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(h.state.pages.currentActivePage() == 1);
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Page 2") == 0);
    assert(!h.state.macroUi.clutchActive.get());

    drainNotifications();

    std::cout << "[PASS] test_nav_turn_previews_macro_page_until_nav_commit\n";
}

void test_nav_focus_track_turn_switches_context_to_highlighted_macro_track() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0003, h.state.currentSharedActiveTrack());
    h.state.pages.tracks[1].activePage = 3;
    h.state.pages.tracks[1].channel = 9;
    h.state.pages.tracks[1].pages[3].cc[0] = 91;
    std::strncpy(h.state.pages.tracks[1].pages[3].name,
                 "Track 2 Page 4",
                 core::state::macro::PAGE_NAME_SIZE - 1);
    h.state.pages.tracks[1].pages[3].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';

    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.turn(Config::EncoderID::NAV, 1.0f);

    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);
    assert(h.state.pages.currentActiveTrack() == 1);
    assert(h.state.pages.currentActivePage() == 3);
    assert(h.performanceServices.activeConfig(0).channel == 9);
    assert(h.performanceServices.activeConfig(0).cc == 91);
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Track 2 Page 4") == 0);

    drainNotifications();

    std::cout << "[PASS] test_nav_focus_track_turn_switches_context_to_highlighted_macro_track\n";
}

void test_macro_track_cursor_can_cross_gaps_and_reach_any_track() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0005, 0);
    h.state.pages.tracks[2].activePage = 2;
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.pages.currentActiveTrack() == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 2);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.pages.currentActiveTrack() == 2);
    assert(h.state.pages.currentActivePage() == 2);

    std::cout << "[PASS] test_macro_track_cursor_can_cross_gaps_and_reach_any_track\n";
}

void test_macro_page_add_slot_is_terminal_and_does_not_wrap_on_reverse() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0001;
    h.state.pages.syncActiveTrackCache();

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.previewAddPageSlot.get());
    assert(h.state.pages.currentActivePage() == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.previewAddPageSlot.get());
    assert(h.state.pages.currentActivePage() == 0);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(!h.state.macroUi.previewAddPageSlot.get());
    assert(h.state.pages.currentActivePage() == 0);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(!h.state.macroUi.previewAddPageSlot.get());
    assert(h.state.pages.currentActivePage() == 0);

    std::cout << "[PASS] test_macro_page_add_slot_is_terminal_and_does_not_wrap_on_reverse\n";
}

void test_macro_slot_focus_creates_add_slot_only_on_nav_confirm() {
    MacroPerformanceHarness h;

    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(h.state.pages.isMacroSlotActive(0));
    assert(!h.state.pages.isMacroSlotActive(1));

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);
    assert(h.state.macroUi.focusedMacroSlot.get() == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.focusedMacroSlot.get() == 1);
    assert(h.state.pages.isMacroAddSlot(1));
    assert(!h.state.pages.isMacroSlotActive(1));

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);
    assert(h.state.macroUi.focusedMacroSlot.get() == 1);
    assert(h.state.pages.isMacroSlotActive(1));
    assert(h.state.pages.activeConfigs[1].cc == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);

    drainNotifications();

    std::cout << "[PASS] test_macro_slot_focus_creates_add_slot_only_on_nav_confirm\n";
}

void test_macro_page_selection_delete_guard_cancels_early_and_applies_at_threshold() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0003;
    h.state.pages.syncActiveTrackCache();
    h.structureServices.switchToPage(1);
    drainNotifications();

    h.press(Config::ButtonID::NAV);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
    assert(h.state.pages.currentActivePage() == 1);
    assert(h.state.pages.currentEnabledPageMask() == 0x0003);
    assert(!h.state.macroUi.pageSelection.active.get());

    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.macroUi.pageSelection.active.get());
    assert(h.state.macroUi.pageSelection.scope.get() ==
           core::state::StructureSelectionScope::PAGE);
    assert(h.state.macroUi.pageSelection.cursorIndex.get() == 1);

    h.release(Config::ButtonID::NAV);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 1U);

    h.state.macroUi.pageSelection.selectedMask.set(0x0002);
    assert(h.state.macroUi.pageSelection.selectedMask.get() == 0x0002);

    const uint32_t mutationBeforeCancel = h.state.project.metadata.modifiedCounter;
    const uint8_t historyBeforeCancel = h.state.sequencerHistory.undoCount();
    constexpr uint32_t earlyStart = 2000;
    h.tick(earlyStart);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::PRESSED);
    assert(h.state.macroUi.selectionDeleteFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::PRESSED);

    h.tick(earlyStart + Config::Timing::LATCH_THRESHOLD_MS - 1U);
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::PRESSED);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.pages.currentEnabledPageMask() == 0x0003);
    assert(h.state.pages.currentActivePage() == 1);
    assert(h.state.macroUi.pageSelection.active.get());
    assert(h.state.macroUi.pageSelection.selectedMask.get() == 0x0002);
    assert(h.state.project.metadata.modifiedCounter == mutationBeforeCancel);
    assert(h.state.sequencerHistory.undoCount() == historyBeforeCancel);
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::CANCELLED);
    assert(h.state.macroUi.selectionDeleteFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);

    h.tick(
        earlyStart + Config::Timing::LATCH_THRESHOLD_MS - 1U +
        Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
    );
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::IDLE);
    assert(!h.state.macroUi.selectionDeleteFeedback.get().active);

    constexpr uint32_t applyStart = 4000;
    h.tick(applyStart);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.tick(applyStart + Config::Timing::LATCH_THRESHOLD_MS);
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::ARMED);
    assert(h.state.macroUi.selectionDeleteGuard.get().progressPermille ==
           Config::Timing::LATCH_THRESHOLD_MS);
    assert(h.state.macroUi.selectionDeleteFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::ARMED);
    h.tick(applyStart + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
    assert(h.state.pages.currentEnabledPageMask() == 0x0003);
    assert(h.state.macroUi.pageSelection.active.get());

    // Exercise the scheduler boundary where the shared guard reaches its
    // terminal threshold immediately before the input long-press callback.
    auto boundaryGuard = h.state.macroUi.selectionDeleteGuard.get();
    assert(core::state::contextual::updateGuardedAction(
        boundaryGuard,
        applyStart + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
    ));
    assert(boundaryGuard.phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);
    h.state.macroUi.selectionDeleteGuard.set(boundaryGuard);

    h.tick(applyStart + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.pages.currentEnabledPageMask() == 0x0001);
    assert(h.state.pages.currentActivePage() == 0);
    assert(!h.state.macroUi.pageSelection.active.get());
    assert(h.state.project.metadata.modifiedCounter == mutationBeforeCancel + 1U);
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::IDLE);
    assert(h.state.macroUi.selectionDeleteFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);

    const uint32_t mutationAfterApply = h.state.project.metadata.modifiedCounter;
    h.tick(applyStart + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 1U);
    assert(h.state.project.metadata.modifiedCounter == mutationAfterApply);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    h.tick(applyStart + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 2U);
    assert(h.state.project.metadata.modifiedCounter == mutationAfterApply);
    assert(h.state.pages.currentEnabledPageMask() == 0x0001);

    drainNotifications();

    std::cout
        << "[PASS] test_macro_page_selection_delete_guard_cancels_early_and_applies_at_threshold\n";
}

void test_macro_track_selection_delete_applies_only_after_guard_threshold() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0003, h.state.currentSharedActiveTrack());
    h.state.pages.tracks[1].enabledPageMask = 0x0001;
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    std::strncpy(h.state.pages.tracks[1].pages[0].name,
                 "Track 2 Page 1",
                 core::state::macro::PAGE_NAME_SIZE - 1);
    h.state.pages.tracks[1].pages[0].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);
    assert(h.state.pages.currentActiveTrack() == 1);

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

    constexpr uint32_t applyStart = 3000;
    h.tick(applyStart);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.tick(applyStart + Config::Timing::LATCH_THRESHOLD_MS);
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::ARMED);
    h.tick(applyStart + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
    assert(h.state.pages.currentTrackEnabledMask() == 0x0003);
    assert(h.state.trackNavigation.selection.active.get());

    h.tick(applyStart + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.pages.currentTrackEnabledMask() == 0x0001);
    assert(h.state.pages.currentActiveTrack() == 0);
    assert(!h.state.trackNavigation.selection.active.get());
    assert(h.state.macroUi.selectionDeleteFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);

    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.pages.currentTrackEnabledMask() == 0x0001);
    assert(h.state.pages.currentActiveTrack() == 0);
    assert(!h.state.trackNavigation.selection.active.get());

    drainNotifications();

    std::cout
        << "[PASS] test_macro_track_selection_delete_applies_only_after_guard_threshold\n";
}

void test_macro_selection_delete_release_cannot_fall_through_after_context_cancel() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0003;
    h.state.pages.syncActiveTrackCache();
    h.state.macroUi.pageSelection.active.set(true);
    h.state.macroUi.pageSelection.scope.set(
        core::state::StructureSelectionScope::PAGE
    );
    h.state.macroUi.pageSelection.cursorIndex.set(1);
    h.state.macroUi.pageSelection.selectedMask.set(0x0002);

    const uint32_t mutationBefore = h.state.project.metadata.modifiedCounter;
    h.tick(2000);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::PRESSED);

    // Cancel selection through another control while delete is still held.
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.state.macroUi.pageSelection.active.get());
    assert(h.state.pages.currentEnabledPageMask() == 0x0003);

    // The release belongs to the old guarded gesture and must not be
    // reinterpreted as CLEAR_STRUCTURE in the newly active context.
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.pages.currentEnabledPageMask() == 0x0003);
    assert(h.state.project.metadata.modifiedCounter == mutationBefore);

    drainNotifications();

    std::cout
        << "[PASS] test_macro_selection_delete_release_cannot_fall_through_after_context_cancel\n";
}

void test_macro_selection_delete_release_cancels_committed_guard_without_mutation() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0003;
    h.state.pages.syncActiveTrackCache();
    h.state.macroUi.pageSelection.active.set(true);
    h.state.macroUi.pageSelection.scope.set(
        core::state::StructureSelectionScope::PAGE
    );
    h.state.macroUi.pageSelection.cursorIndex.set(1);
    h.state.macroUi.pageSelection.selectedMask.set(0x0002);

    constexpr uint32_t start = 6000;
    h.tick(start);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.tick(start + Config::Timing::LATCH_THRESHOLD_MS);

    auto boundaryGuard = h.state.macroUi.selectionDeleteGuard.get();
    assert(core::state::contextual::updateGuardedAction(
        boundaryGuard,
        start + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
    ));
    assert(boundaryGuard.phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);
    h.state.macroUi.selectionDeleteGuard.set(boundaryGuard);

    const uint32_t mutationBefore = h.state.project.metadata.modifiedCounter;
    g_now_ms = start + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.pages.currentEnabledPageMask() == 0x0003);
    assert(h.state.macroUi.pageSelection.active.get());
    assert(h.state.project.metadata.modifiedCounter == mutationBefore);
    assert(h.state.macroUi.selectionDeleteGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::CANCELLED);
    assert(h.state.macroUi.selectionDeleteFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);

    drainNotifications();

    std::cout
        << "[PASS] test_macro_selection_delete_release_cancels_committed_guard_without_mutation\n";
}

void test_macro_page_copy_and_long_press_paste() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0003;
    h.state.pages.syncActiveTrackCache();
    std::strncpy(
        h.state.pages.activeTrackData().pages[0].name,
        "Copied Page",
        core::state::macro::PAGE_NAME_SIZE - 1
    );
    h.state.pages.activeTrackData().pages[0].cc[0] = 55;
    h.state.pages.activeTrackData().pages[1].cc[0] = 12;
    h.structureServices.switchToPage(0);
    configureMacroAutomation(h.state, h.state.pages.currentActiveTrack(), 0, 0, 0.75f);
    drainNotifications();

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.clipboard.hasMacroPage());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.previewPageIndex.get() == 1);
    assert(h.state.pages.currentActivePage() == 0);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.pages.currentActivePage() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(std::strcmp(h.state.pages.activePageData().name, "Copied Page") == 0);
    assert(h.state.pages.activePageData().cc[0] == 55);
    const auto* pastedAutomation = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = 1,
            .macro = 0,
        }
    );
    assert(pastedAutomation != nullptr);
    assert(pastedAutomation->automation.active);
    core::state::macro::MacroCurvePoint pastedPoint{};
    assert(core::state::macro::macroAutomationReadPoint(
        pastedAutomation->automation,
        h.state.pages.automation.pointPool,
        0,
        false,
        pastedPoint
    ));
    assert(std::fabs(pastedPoint.value - 0.75f) < 0.0001f);

    std::cout << "[PASS] test_macro_page_copy_and_long_press_paste\n";
}

void test_macro_selection_duplicate_copies_page_into_first_free_slot() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0003;
    h.state.pages.syncActiveTrackCache();
    h.state.pages.activeTrackData().pages[1].cc[0] = 77;
    std::strncpy(
        h.state.pages.activeTrackData().pages[1].name,
        "Dupe Page",
        core::state::macro::PAGE_NAME_SIZE - 1
    );

    h.state.macroUi.pageSelection.active.set(true);
    h.state.macroUi.pageSelection.scope.set(core::state::StructureSelectionScope::PAGE);
    h.state.macroUi.pageSelection.cursorIndex.set(1);
    h.state.macroUi.pageSelection.selectedMask.set(0x0002);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.pages.currentEnabledPageMask() == 0x0007);
    assert(h.state.pages.currentActivePage() == 2);
    assert(h.state.pages.activePageData().cc[0] == 77);
    assert(std::strcmp(h.state.pages.activePageData().name, "Dupe Page") == 0);
    assert(!h.state.macroUi.pageSelection.active.get());

    drainNotifications();

    std::cout << "[PASS] test_macro_selection_duplicate_copies_page_into_first_free_slot\n";
}

void test_macro_selection_duplicate_copies_track_into_first_free_slot() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0003, 0);
    h.state.pages.tracks[1].channel = 12;
    h.state.pages.tracks[1].activePage = 0;
    h.state.pages.tracks[1].pages[0].cc[0] = 88;
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.state.trackNavigation.selection.active.set(true);
    h.state.trackNavigation.selection.scope.set(core::state::StructureSelectionScope::TRACK);
    h.state.trackNavigation.selection.cursorIndex.set(1);
    h.state.trackNavigation.selection.selectedMask.set(0x0002);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.pages.currentTrackEnabledMask() == 0x0007);
    assert(h.state.pages.currentActiveTrack() == 2);
    assert(h.state.pages.activeTrackData().channel == 12);
    assert(h.state.pages.activeTrackData().pages[0].cc[0] == 88);
    assert(!h.state.trackNavigation.selection.active.get());

    drainNotifications();

    std::cout << "[PASS] test_macro_selection_duplicate_copies_track_into_first_free_slot\n";
}

void test_macro_track_copy_and_long_press_paste_to_add_slot() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0001, 0);
    h.state.pages.tracks[0].channel = 10;
    h.state.pages.tracks[0].pages[0].cc[0] = 64;
    configureMacroAutomation(h.state, 0, 0, 0, 0.33f);
    std::strncpy(
        h.state.pages.tracks[0].pages[0].name,
        "Copied Track",
        core::state::macro::PAGE_NAME_SIZE - 1
    );
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.clipboard.hasMacroTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.pages.currentTrackEnabledMask() == 0x0003);
    assert(h.state.pages.currentActiveTrack() == 1);
    assert(h.state.pages.activeTrackData().channel == 10);
    assert(h.state.pages.activeTrackData().pages[0].cc[0] == 64);
    assert(std::strcmp(h.state.pages.activeTrackData().pages[0].name, "Copied Track") == 0);
    const auto* pastedAutomation = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = 1,
            .page = 0,
            .macro = 0,
        }
    );
    assert(pastedAutomation != nullptr);
    assert(pastedAutomation->automation.active);
    core::state::macro::MacroCurvePoint pastedPoint{};
    assert(core::state::macro::macroAutomationReadPoint(
        pastedAutomation->automation,
        h.state.pages.automation.pointPool,
        0,
        false,
        pastedPoint
    ));
    assert(std::fabs(pastedPoint.value - 0.33f) < 0.0001f);

    drainNotifications();

    std::cout << "[PASS] test_macro_track_copy_and_long_press_paste_to_add_slot\n";
}

void test_macro_track_copy_preserves_multiple_pages_and_automations() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0001, 0);
    auto& sourceTrack = h.state.pages.tracks[0];
    sourceTrack.channel = 6;
    sourceTrack.activePage = 2;
    sourceTrack.enabledPageMask = 0x0005;
    sourceTrack.pages[0].cc[0] = 21;
    sourceTrack.pages[2].cc[0] = 84;
    sourceTrack.pages[2].cc[1] = 85;
    sourceTrack.pages[2].setMacroActive(1, true);
    std::strncpy(
        sourceTrack.pages[0].name,
        "Source P1",
        core::state::macro::PAGE_NAME_SIZE - 1
    );
    std::strncpy(
        sourceTrack.pages[2].name,
        "Source P3",
        core::state::macro::PAGE_NAME_SIZE - 1
    );
    sourceTrack.pages[0].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';
    sourceTrack.pages[2].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';
    h.state.pages.syncSharedTrackState(0x0001, 0);
    configureMacroAutomation(h.state, 0, 0, 0, 0.25f);
    configureMacroAutomation(h.state, 0, 2, 1, 0.80f);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.clipboard.hasMacroTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.pages.currentTrackEnabledMask() == 0x0003);
    assert(h.state.pages.currentActiveTrack() == 1);
    assert(h.state.pages.activeTrackData().channel == 6);
    assert(h.state.pages.activeTrackData().activePage == 2);
    assert(h.state.pages.activeTrackData().enabledPageMask == 0x0005);
    assert(h.state.pages.activeTrackData().pages[0].cc[0] == 21);
    assert(h.state.pages.activeTrackData().pages[2].cc[0] == 84);
    assert(h.state.pages.activeTrackData().pages[2].cc[1] == 85);
    assert(h.state.pages.activeTrackData().pages[2].isMacroActive(1));
    assert(std::strcmp(h.state.pages.activeTrackData().pages[0].name, "Source P1") == 0);
    assert(std::strcmp(h.state.pages.activeTrackData().pages[2].name, "Source P3") == 0);

    const auto* pastedPage0Automation = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{.track = 1, .page = 0, .macro = 0}
    );
    const auto* pastedPage2Automation = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{.track = 1, .page = 2, .macro = 1}
    );
    assert(pastedPage0Automation != nullptr);
    assert(pastedPage0Automation->automation.active);
    assert(pastedPage2Automation != nullptr);
    assert(pastedPage2Automation->automation.active);

    core::state::macro::MacroCurvePoint page2Point{};
    assert(core::state::macro::macroAutomationReadPoint(
        pastedPage2Automation->automation,
        h.state.pages.automation.pointPool,
        0,
        false,
        page2Point
    ));
    assert(std::fabs(page2Point.value - 0.80f) < 0.0001f);

    drainNotifications();

    std::cout << "[PASS] test_macro_track_copy_preserves_multiple_pages_and_automations\n";
}

void test_macro_slot_focus_uses_typed_clipboard_and_guarded_remove() {
    MacroPerformanceHarness h;

    configureMacroAutomation(h.state, 0, 0, 0, 0.42f);
    auto& page = h.state.pages.activePageData();
    page.setMacroActive(0, true);
    page.cc[0] = 74;
    page.values[0] = 0.37f;
    h.state.pages.updateActiveConfigs();
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.macroUi.focusedMacroSlot.set(0);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.clipboard.hasMacroSlot());
    assert(!h.clipboard.hasMacroAutomation());
    assert(h.clipboard.macroAutomationSet->sourceMacroActive);
    assert(h.clipboard.macroAutomationSet->sourceSlotPresent);
    assert(h.clipboard.macroAutomationSet->sourceCc == 74);
    assert(std::fabs(h.clipboard.macroAutomationSet->sourceStaticValue - 0.37f) <
           0.0001f);

    // A short release cancels the destructive hold. It must neither clear the
    // Automation source nor leave the visual hold state armed.
    h.press(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.macroUi.pageHold.action.get() ==
           core::state::StructureHoldAction::REMOVE);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    const auto* unchanged = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(unchanged != nullptr);
    assert(unchanged->automation.active);
    assert(h.state.macroUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(!h.state.pages.activePageData().isMacroActive(0));
    assert(core::state::macro::macroAutomationFindSlot(
               h.state.pages.automation,
               core::state::macro::MacroAutomationSlotAddress{
                   .track = 0,
                   .page = 0,
                   .macro = 0,
               }
           ) == nullptr);
    assert(h.state.macroUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.tick(2 * Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    const auto* restored = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(restored != nullptr);
    assert(restored->automation.active);
    core::state::macro::MacroCurvePoint restoredPoint{};
    assert(core::state::macro::macroAutomationReadPoint(
        restored->automation,
        h.state.pages.automation.pointPool,
        0,
        false,
        restoredPoint
    ));
    assert(std::fabs(restoredPoint.value - 0.42f) < 0.0001f);
    assert(h.state.pages.activePageData().isMacroActive(0));
    assert(h.state.pages.activePageData().cc[0] == 74);
    assert(std::fabs(h.state.pages.activePageData().values[0] - 0.37f) < 0.0001f);

    drainNotifications();

    std::cout << "[PASS] test_macro_slot_focus_uses_typed_clipboard_and_guarded_remove\n";
}

void test_left_bottom_toggles_macro_slot_property_selector() {
    MacroPerformanceHarness h;

    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::VALUE);
    assert(!h.state.macroUi.clutchActive.get());

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.tick(1);
    assert(h.state.macroUi.clutchActive.get());
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::CC);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::AUTOMATION);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::CC);

    h.release(Config::ButtonID::LEFT_BOTTOM);
    h.tick(2);
    assert(h.state.macroUi.clutchActive.get());
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::CC);

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.tick(3);
    h.release(Config::ButtonID::LEFT_BOTTOM);
    h.tick(4);
    assert(!h.state.macroUi.clutchActive.get());
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::VALUE);

    drainNotifications();

    std::cout << "[PASS] test_left_bottom_toggles_macro_slot_property_selector\n";
}

void test_left_top_cancels_macro_slot_property_selector_without_committing_preview() {
    MacroPerformanceHarness h;

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.tick(1);
    assert(h.state.macroUi.clutchActive.get());
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::CC);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::AUTOMATION);

    h.release(Config::ButtonID::LEFT_TOP);
    h.tick(2);
    assert(!h.state.macroUi.clutchActive.get());
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::VALUE);

    drainNotifications();

    std::cout << "[PASS] test_left_top_cancels_macro_slot_property_selector_without_committing_preview\n";
}

void test_left_center_is_reserved_and_does_not_open_macro_set_controls() {
    MacroPerformanceHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.tick(1);
    h.release(Config::ButtonID::LEFT_CENTER);
    h.tick(2);
    assert(!h.state.macroUi.clutchActive.get());
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::VALUE);

    drainNotifications();

    std::cout << "[PASS] test_left_center_is_reserved_and_does_not_open_macro_set_controls\n";
}

}  // namespace

int main() {
    test_nav_turn_previews_macro_page_until_nav_commit();
    test_nav_focus_track_turn_switches_context_to_highlighted_macro_track();
    test_macro_track_cursor_can_cross_gaps_and_reach_any_track();
    test_macro_page_add_slot_is_terminal_and_does_not_wrap_on_reverse();
    test_macro_slot_focus_creates_add_slot_only_on_nav_confirm();
    test_macro_page_selection_delete_guard_cancels_early_and_applies_at_threshold();
    test_macro_track_selection_delete_applies_only_after_guard_threshold();
    test_macro_selection_delete_release_cannot_fall_through_after_context_cancel();
    test_macro_selection_delete_release_cancels_committed_guard_without_mutation();
    test_macro_page_copy_and_long_press_paste();
    test_macro_selection_duplicate_copies_page_into_first_free_slot();
    test_macro_selection_duplicate_copies_track_into_first_free_slot();
    test_macro_track_copy_and_long_press_paste_to_add_slot();
    test_macro_track_copy_preserves_multiple_pages_and_automations();
    test_macro_slot_focus_uses_typed_clipboard_and_guarded_remove();
    test_left_bottom_toggles_macro_slot_property_selector();
    test_left_top_cancels_macro_slot_property_selector_without_committing_preview();
    test_left_center_is_reserved_and_does_not_open_macro_set_controls();
    std::cout << "\nAll MacroPerformanceHandler tests passed.\n";
    return 0;
}
