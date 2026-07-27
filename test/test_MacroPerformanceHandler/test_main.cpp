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

#include <config/App.hpp>

#include "../../src/handler/macro/MacroPerformanceHandler.hpp"
#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/handler/macro/MacroStructureDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/modulation/ProjectControlMacroOps.hpp"
#include "../../src/state/modulation/ProjectModulationDomainOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/NotificationTestUtils.hpp"
#include "../support/ProjectControlTestUtils.hpp"

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
        : state(storage.settings)
        , performanceServices(core::handler::MacroPerformanceDomainServices::fromCoreState(state))
        , structureServices(core::handler::MacroStructureDomainServices::fromCoreState(state))
        , navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs, Config::Input::CONFIG)
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
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = track,
        .page = page,
        .macro = macro,
    };
    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 1.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, value));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, value));
    assert(test_support::project_control::assignAutomation(
        state.pages.control,
        address,
        lane
    ));
}

core::state::modulation::ModulatorId configureLocalLfo(
    core::state::CoreState& state,
    uint8_t track,
    uint8_t page,
    uint8_t macro
) {
    return test_support::project_control::addLocalLfo(
        state.pages.control,
        {
        .track = track,
        .page = page,
        .macro = macro,
        },
        "Local Copy LFO"
    );
}

uint8_t modulationBindingCountAt(
    const core::state::CoreState& state,
    uint8_t track,
    uint8_t page,
    uint8_t macro
) {
    return test_support::project_control::outputBindingCountAt(
        state.pages.control,
        {
            .track = track,
            .page = page,
            .macro = macro,
        }
    );
}

void test_nav_turn_switches_enabled_macro_page_directly() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0003;
    h.state.pages.syncActiveTrackCache();
    std::strncpy(h.state.pages.activeTrackData().pages[1].name,
                 "Page 2",
                 core::state::macro::PAGE_NAME_SIZE - 1);
    h.state.pages.activeTrackData().pages[1].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';

    h.turn(Config::EncoderID::NAV, 1.0f);

    assert(h.state.macroUi.previewPageIndex.get() == 1);
    assert(h.state.pages.currentActivePage() == 1);
    assert(!h.state.macroUi.previewAddPageSlot.get());
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Page 2") == 0);
    assert(!h.state.macroUi.clutchActive.get());

    drainNotifications();

    std::cout << "[PASS] test_nav_turn_switches_enabled_macro_page_directly\n";
}

void test_nav_focus_track_turn_switches_context_to_highlighted_macro_track() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0003, h.state.currentSharedActiveTrack());
    h.state.pages.tracks[1].activePage = 3;
    h.state.projectTracks.authored.midiChannels[1] = 9;
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
    assert(h.performanceServices.activeTrackChannel() == 9);
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

void test_macro_page_hot_surface_exposes_terminal_add_and_creates() {
    MacroPerformanceHarness h;

    h.state.pages.activeTrackData().enabledPageMask = 0x0001;
    h.state.pages.syncActiveTrackCache();

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.previewAddPageSlot.get());
    assert(h.state.macroUi.previewPageIndex.get() == 1U);
    assert(h.state.pages.currentActivePage() == 0);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(!h.state.macroUi.previewAddPageSlot.get());
    assert(h.state.pages.currentEnabledPageMask() == 0x0003U);
    assert(h.state.pages.currentActivePage() == 1U);
    assert(h.state.undoProjectHistory());
    assert(h.state.pages.currentEnabledPageMask() == 0x0001U);
    assert(h.state.pages.currentActivePage() == 0U);
    assert(h.state.redoProjectHistory());
    assert(h.state.pages.currentEnabledPageMask() == 0x0003U);
    assert(h.state.pages.currentActivePage() == 1U);

    std::cout << "[PASS] Macro Page hot create has exact Undo/Redo\n";
}

void test_macro_track_hot_surface_crosses_sparse_free_slots_and_creates_exactly() {
    MacroPerformanceHarness h;

    assert(h.state.setSharedTrackState(0x0005U, 0U));
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.pages.currentActiveTrack() == 0U);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.pages.currentTrackEnabledMask() == 0x0007U);
    assert(h.state.pages.currentActiveTrack() == 1U);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.undoProjectHistory());
    assert(h.state.pages.currentTrackEnabledMask() == 0x0005U);
    assert(h.state.pages.currentActiveTrack() == 0U);
    assert(h.state.redoProjectHistory());
    assert(h.state.pages.currentTrackEnabledMask() == 0x0007U);
    assert(h.state.pages.currentActiveTrack() == 1U);

    std::cout << "[PASS] Macro Track sparse create has shared Undo/Redo\n";
}

void test_track_focus_outside_structure_targets_the_active_track() {
    MacroPerformanceHarness h;

    auto& track = h.state.pages.activeTrackData();
    h.state.projectTracks.authored.midiChannels[0] = 12;
    track.enabledPageMask = 0x0003;
    track.pages[0].setMacroActive(0, true);
    std::strncpy(
        track.pages[0].name,
        "Page Must Clear",
        core::state::macro::PAGE_NAME_SIZE - 1
    );
    track.pages[0].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';
    h.state.pages.syncActiveTrackCache();

    // TRACK is a legal hot-surface context selected by the shared selector.
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(!h.clipboard.hasMacroPage());
    assert(h.clipboard.hasMacroTrack());

    assert(h.state.projectTracks.authored.midiChannels[0] == 12);
    assert(h.state.pages.currentEnabledPageMask() == 0x0003);
    assert(std::strcmp(h.state.pages.activePageData().name, "Page Must Clear") == 0);

    drainNotifications();

    std::cout
        << "[PASS] Track focus outside Structure targets the active Track\n";
}

void test_macro_context_nav_tap_creates_focused_empty_slot() {
    MacroPerformanceHarness h;

    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(h.state.pages.isMacroSlotActive(0));
    assert(!h.state.pages.isMacroSlotActive(1));

    h.press(Config::ButtonID::NAV);
    assert(h.state.macroUi.contextSelector.visible);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.contextSelector.previewFocus ==
           core::state::StructureNavigationFocus::STEP);
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

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, -1.0f);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);

    drainNotifications();

    std::cout << "[PASS] Macro context NAV tap creates focused empty slot\n";
}

void test_macro_buttons_create_empty_slots_and_toggle_manual_resume() {
    MacroPerformanceHarness h;

    assert(!h.state.pages.isMacroSlotActive(1U));
    h.press(Config::ButtonID::MACRO_2);
    h.release(Config::ButtonID::MACRO_2);
    assert(h.state.pages.isMacroSlotActive(1U));
    assert(h.state.macroUi.focusedMacroSlot.get() == 1U);

    configureMacroAutomation(h.state, 0U, 0U, 0U, 0.42f);
    assert(!h.performanceServices.manualOverrideActiveFor(0U));
    h.press(Config::ButtonID::MACRO_1);
    h.release(Config::ButtonID::MACRO_1);
    assert(h.performanceServices.manualOverrideActiveFor(0U));
    h.press(Config::ButtonID::MACRO_1);
    h.release(Config::ButtonID::MACRO_1);
    assert(!h.performanceServices.manualOverrideActiveFor(0U));

    std::cout << "[PASS] Macro buttons create and toggle Manual/Resume\n";
}

void test_macro_context_selector_and_hot_navigation_are_direct() {
    MacroPerformanceHarness h;

    h.state.pages.tracks[0].enabledPageMask = 0x0003;
    h.state.pages.tracks[0].activePage = 1;
    h.state.pages.tracks[0].pages[1].setMacroActive(2, true);
    h.state.pages.tracks[1].enabledPageMask = 0x0003;
    h.state.pages.tracks[1].activePage = 0;
    assert(h.state.setSharedTrackState(0x0003, 0));
    h.structureServices.switchToPage(1);
    h.state.macroUi.syncPreviewPage(1);
    h.state.macroUi.focusedMacroSlot.set(2);

    // PAGE -> TRACK is one held preview and one release.
    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, -1.0f);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);

    // Ordinary NAV only visits enabled Tracks and applies immediately.
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.pages.currentActiveTrack() == 1);

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.pages.currentActivePage() == 1);

    // A hold without rotation is inspection-only and never opens Structure.
    constexpr uint32_t inspectAt = 3000;
    h.tick(inspectAt);
    h.press(Config::ButtonID::NAV);
    h.tick(inspectAt + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.macroUi.contextSelector.visible);
    h.release(Config::ButtonID::NAV);
    assert(!h.state.macroUi.contextSelector.visible);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);

    drainNotifications();

    std::cout
        << "[PASS] test_macro_context_selector_and_hot_navigation_are_direct\n";
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
    assert(h.state.pages.currentActivePage() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(std::strcmp(h.state.pages.activePageData().name, "Copied Page") == 0);
    assert(h.state.pages.activePageData().cc[0] == 55);
    const auto pastedAutomation = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), 1, 0}
    );
    assert(pastedAutomation.automation.enabled);
    const auto pastedPoint = test_support::project_control::readCurvePoint(
        h.state.pages.control,
        pastedAutomation.automation.id,
        0,
        false
    );
    assert(std::fabs(pastedPoint.value - 0.75f) < 0.0001f);

    std::cout << "[PASS] test_macro_page_copy_and_long_press_paste\n";
}

void test_macro_track_copy_and_long_press_paste_to_add_slot() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0001, 0);
    h.state.projectTracks.authored.midiChannels[0] = 10U;
    h.state.projectTracks.authored.midiChannels[1] = 14U;
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
    assert(h.state.projectTracks.authored.midiChannels[1] == 14U);
    assert(h.state.pages.activeTrackData().pages[0].cc[0] == 64);
    assert(std::strcmp(h.state.pages.activeTrackData().pages[0].name, "Copied Track") == 0);
    const auto pastedAutomation = test_support::project_control::readSlot(
        h.state.pages.control,
        {1, 0, 0}
    );
    assert(pastedAutomation.automation.enabled);
    const auto pastedPoint = test_support::project_control::readCurvePoint(
        h.state.pages.control,
        pastedAutomation.automation.id,
        0,
        false
    );
    assert(std::fabs(pastedPoint.value - 0.33f) < 0.0001f);

    drainNotifications();

    std::cout << "[PASS] test_macro_track_copy_and_long_press_paste_to_add_slot\n";
}

void test_early_paste_release_only_cancels_the_hold_for_page_and_track() {
    {
        MacroPerformanceHarness h;
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        const uint16_t enabledBefore = h.state.pages.currentEnabledPageMask();

        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.release(Config::ButtonID::BOTTOM_RIGHT);
        assert(h.clipboard.hasMacroPage());

        h.turn(Config::EncoderID::NAV, 1.0f);
        assert(!h.state.pages.isPageEnabled(
            h.state.macroUi.previewPageIndex.get()
        ));
        h.press(Config::ButtonID::BOTTOM_RIGHT);
        assert(h.state.macroUi.pageHold.action.get() ==
               core::state::StructureHoldAction::PASTE);
        h.release(Config::ButtonID::BOTTOM_RIGHT);

        assert(h.state.pages.currentEnabledPageMask() == enabledBefore);
        assert(!h.state.pages.isPageEnabled(
            h.state.macroUi.previewPageIndex.get()
        ));
        assert(h.clipboard.hasMacroPage());
        assert(h.state.macroUi.pageHold.action.get() ==
               core::state::StructureHoldAction::NONE);
    }

    drainNotifications();

    {
        MacroPerformanceHarness h;
        h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
        const uint16_t enabledBefore = h.state.pages.currentTrackEnabledMask();

        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.release(Config::ButtonID::BOTTOM_RIGHT);
        assert(h.clipboard.hasMacroTrack());

        h.turn(Config::EncoderID::NAV, 1.0f);
        assert(h.state.trackNavigation.previewAddSlot.get());
        h.press(Config::ButtonID::BOTTOM_RIGHT);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::PASTE);
        h.release(Config::ButtonID::BOTTOM_RIGHT);

        assert(h.state.pages.currentTrackEnabledMask() == enabledBefore);
        assert(h.state.trackNavigation.previewAddSlot.get());
        assert(h.clipboard.hasMacroTrack());
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::NONE);
    }

    drainNotifications();

    std::cout
        << "[PASS] early Paste release cancels Page and Track holds only\n";
}

void test_paste_only_press_cannot_turn_into_copy_after_navigation() {
    MacroPerformanceHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    auto& track = h.state.pages.activeTrackData();
    track.enabledPageMask = 0x0003;
    track.pages[0].cc[0] = 41;
    track.pages[1].cc[0] = 99;
    h.state.pages.syncActiveTrackCache();

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.clipboard.hasMacroPage());
    assert(h.clipboard.macroPage.cc[0] == 41);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 2.0f);
    assert(h.state.macroUi.previewPageIndex.get() == 2U);
    assert(!h.state.pages.isPageEnabled(2U));
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.macroUi.pageHold.action.get() ==
           core::state::StructureHoldAction::PASTE);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.macroUi.previewPageIndex.get() == 1U);
    assert(h.state.pages.isPageEnabled(1U));
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.macroUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(h.clipboard.hasMacroPage());
    assert(h.clipboard.macroPage.cc[0] == 41);

    drainNotifications();
    std::cout
        << "[PASS] Paste-only press cannot become Copy after navigation\n";
}

void test_guarded_page_and_track_actions_cancel_when_target_moves() {
    {
        MacroPerformanceHarness h;
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        auto& track = h.state.pages.activeTrackData();
        track.enabledPageMask = 0x0003;
        track.pages[0].cc[0] = 41U;
        track.pages[1].cc[0] = 99U;
        h.state.pages.syncActiveTrackCache();

        h.press(Config::ButtonID::BOTTOM_LEFT);
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.tick(0U);
        h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::BOTTOM_LEFT);

        assert(h.state.pages.currentEnabledPageMask() == 0x0003);
        assert(h.state.pages.pageData(0U, 0U).cc[0] == 41U);
        assert(h.state.pages.pageData(0U, 1U).cc[0] == 99U);
    }

    drainNotifications();

    {
        MacroPerformanceHarness h;
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        auto& track = h.state.pages.activeTrackData();
        track.enabledPageMask = 0x0003;
        track.pages[0].cc[0] = 41U;
        track.pages[1].cc[0] = 99U;
        h.state.pages.syncActiveTrackCache();

        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.release(Config::ButtonID::BOTTOM_RIGHT);
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.turn(Config::EncoderID::NAV, -1.0f);
        h.tick(0U);
        h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::BOTTOM_RIGHT);

        assert(h.state.pages.pageData(0U, 0U).cc[0] == 41U);
        assert(h.state.pages.pageData(0U, 1U).cc[0] == 99U);
    }

    drainNotifications();

    {
        MacroPerformanceHarness h;
        h.state.setSharedTrackState(0x0003, 0U);
        h.state.projectTracks.authored.midiChannels[0] = 10U;
        h.state.projectTracks.authored.midiChannels[1] = 11U;
        h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

        h.press(Config::ButtonID::BOTTOM_LEFT);
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.tick(0U);
        h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::BOTTOM_LEFT);

        assert(h.state.pages.currentTrackEnabledMask() == 0x0003);
        assert(h.state.projectTracks.authored.midiChannels[0] == 10U);
        assert(h.state.projectTracks.authored.midiChannels[1] == 11U);
    }

    drainNotifications();

    {
        MacroPerformanceHarness h;
        h.state.setSharedTrackState(0x0003, 0U);
        h.state.projectTracks.authored.midiChannels[0] = 10U;
        h.state.projectTracks.authored.midiChannels[1] = 11U;
        h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.release(Config::ButtonID::BOTTOM_RIGHT);
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.turn(Config::EncoderID::NAV, -1.0f);
        h.tick(0U);
        h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::BOTTOM_RIGHT);

        assert(h.state.pages.currentTrackEnabledMask() == 0x0003);
        assert(h.state.projectTracks.authored.midiChannels[0] == 10U);
        assert(h.state.projectTracks.authored.midiChannels[1] == 11U);
    }

    drainNotifications();
    std::cout
        << "[PASS] guarded Page/Track actions cancel when their target moves\n";
}

void test_page_navigation_applies_live_and_actions_follow_visible_page() {
    MacroPerformanceHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    auto& track = h.state.pages.activeTrackData();
    track.enabledPageMask = 0x0007;
    track.pages[0].cc[0] = 41U;
    track.pages[1].cc[0] = 99U;
    track.pages[2].cc[0] = 77U;
    h.state.pages.syncActiveTrackCache();
    assert(h.state.pages.currentActivePage() == 0U);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.previewPageIndex.get() == 1U);
    assert(h.state.pages.currentActivePage() == 1U);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.clipboard.hasMacroPage());
    assert(h.clipboard.macroPage.cc[0] == 99U);
    assert(h.state.pages.currentActivePage() == 1U);

    track.pages[1].cc[0] = 12U;
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0U);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.pages.currentActivePage() == 1U);
    assert(h.state.pages.activePageData().cc[0] == 99U);
    assert(h.state.pages.pageData(0U, 0U).cc[0] == 41U);

    h.state.pages.setActivePage(0U);
    h.state.macroUi.syncPreviewPage(2U);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.tick(0U);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(!h.state.pages.isPageEnabled(2U));
    assert(h.state.pages.isPageEnabled(0U));
    assert(h.state.pages.currentActivePage() == 0U);
    assert(h.state.pages.pageData(0U, 0U).cc[0] == 41U);

    h.state.macroUi.syncPreviewPage(1U);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.pages.pageData(0U, 1U).cc[0] ==
           core::state::macro::defaultMacroCc(1U, 0U));
    assert(h.state.pages.pageData(0U, 0U).cc[0] == 41U);

    drainNotifications();
    std::cout << "[PASS] Page navigation applies live and actions follow it\n";
}

void test_macro_track_copy_preserves_multiple_pages_and_automations() {
    MacroPerformanceHarness h;

    h.state.setSharedTrackState(0x0001, 0);
    h.state.projectTracks.authored.midiChannels[0] = 6U;
    h.state.projectTracks.authored.midiChannels[1] = 9U;
    auto& sourceTrack = h.state.pages.tracks[0];
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
    assert(h.state.projectTracks.authored.midiChannels[1] == 9U);
    assert(h.state.pages.activeTrackData().activePage == 2);
    assert(h.state.pages.activeTrackData().enabledPageMask == 0x0005);
    assert(h.state.pages.activeTrackData().pages[0].cc[0] == 21);
    assert(h.state.pages.activeTrackData().pages[2].cc[0] == 84);
    assert(h.state.pages.activeTrackData().pages[2].cc[1] == 85);
    assert(h.state.pages.activeTrackData().pages[2].isMacroActive(1));
    assert(std::strcmp(h.state.pages.activeTrackData().pages[0].name, "Source P1") == 0);
    assert(std::strcmp(h.state.pages.activeTrackData().pages[2].name, "Source P3") == 0);

    const auto pastedPage0Automation = test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{.track = 1, .page = 0, .macro = 0}
    );
    const auto pastedPage2Automation = test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{.track = 1, .page = 2, .macro = 1}
    );
    assert(pastedPage0Automation.automation.enabled);
    assert(pastedPage2Automation.automation.enabled);

    const auto page2Point = test_support::project_control::readCurvePoint(
        h.state.pages.control,
        pastedPage2Automation.automation.id,
        0,
        false
    );
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

    // A short release clears only Automation. The Macro destination remains.
    const uint8_t undoCountBeforeReset = h.state.macroHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.macroUi.pageHold.action.get() ==
           core::state::StructureHoldAction::REMOVE);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    const auto cleared = test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(!cleared.automation.stored());
    assert(h.state.pages.activePageData().isMacroActive(0));
    assert(h.state.macroHistory.undoCount() ==
           static_cast<uint8_t>(undoCountBeforeReset + 1U));
    assert(h.state.macroUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(h.state.macroHistory.undo(h.state.pages));
    assert(test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 0U, .macro = 0U}
    ).automation.stored());
    assert(h.state.macroHistory.redo(h.state.pages));
    assert(!test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 0U, .macro = 0U}
    ).automation.stored());

    configureMacroAutomation(h.state, 0, 0, 0, 0.42f);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(!h.state.pages.activePageData().isMacroActive(0));
    assert(!test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = 0,
            .page = 0,
            .macro = 0,
        }
    ).present());
    assert(h.state.macroUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.tick(2 * Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    const auto restored = test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(restored.automation.enabled);
    const auto restoredPoint = test_support::project_control::readCurvePoint(
        h.state.pages.control,
        restored.automation.id,
        0,
        false
    );
    assert(std::fabs(restoredPoint.value - 0.42f) < 0.0001f);
    assert(h.state.pages.activePageData().isMacroActive(0));
    assert(h.state.pages.activePageData().cc[0] == 74);
    assert(std::fabs(h.state.pages.activePageData().values[0] - 0.37f) < 0.0001f);

    drainNotifications();

    std::cout << "[PASS] test_macro_slot_focus_uses_typed_clipboard_and_guarded_remove\n";
}

void test_delete_intermediate_macro_page_compacts_complete_page_state() {
    MacroPerformanceHarness h;
    auto& track = h.state.pages.activeTrackData();
    track.enabledPageMask = 0x0007U;
    std::strncpy(track.pages[1].name,
                 "Deleted",
                 core::state::macro::PAGE_NAME_SIZE - 1U);
    std::strncpy(track.pages[2].name,
                 "Shifted Page",
                 core::state::macro::PAGE_NAME_SIZE - 1U);
    track.pages[2].name[core::state::macro::PAGE_NAME_SIZE - 1U] = '\0';
    track.pages[2].cc[3] = 93U;
    track.pages[2].values[3] = 0.81f;
    track.pages[2].setMacroActive(3U, true);
    h.state.pages.syncActiveTrackCache();
    h.state.pages.setActivePage(1U);

    configureMacroAutomation(h.state, 0U, 1U, 0U, 0.21f);
    configureMacroAutomation(h.state, 0U, 2U, 3U, 0.81f);
    const auto shiftedBefore = test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 2U, .macro = 3U}
    );
    assert(h.state.macroUi.manualOverrides.activate(
        {.track = 0U, .page = 1U, .macro = 0U},
        0.21f
    ) == core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(h.state.macroUi.manualOverrides.activate(
        {.track = 0U, .page = 2U, .macro = 3U},
        0.81f
    ) == core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    assert(h.structureServices.deletePage(1U));

    assert(track.enabledPageMask == 0x0003U);
    assert(h.state.pages.currentEnabledPageMask() == 0x0003U);
    assert(h.state.pages.currentActivePage() == 1U);
    assert(std::strcmp(track.pages[1].name, "Shifted Page") == 0);
    assert(track.pages[1].cc[3] == 93U);
    assert(std::fabs(track.pages[1].values[3] - 0.81f) < 0.0001f);
    assert(track.pages[1].isMacroActive(3U));

    const auto compacted = test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 1U, .macro = 3U}
    );
    assert(compacted.automation.stored());
    assert(compacted.automation.id == shiftedBefore.automation.id);
    assert(!test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 1U, .macro = 0U}
    ).present());
    assert(!test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 2U, .macro = 3U}
    ).present());
    float manual = 0.0f;
    assert(!h.state.macroUi.manualOverrides.activeFor(
        {.track = 0U, .page = 1U, .macro = 0U}
    ));
    assert(h.state.macroUi.manualOverrides.valueFor(
        {.track = 0U, .page = 1U, .macro = 3U},
        manual
    ));
    assert(std::fabs(manual - 0.81f) < 0.0001f);
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Shifted Page") == 0);

    assert(h.state.projectHistory.undoCount() == 1U);
    assert(h.state.undoProjectHistory());
    assert(track.enabledPageMask == 0x0007U);
    assert(h.state.pages.currentActivePage() == 1U);
    assert(std::strcmp(track.pages[1].name, "Deleted") == 0);
    assert(std::strcmp(track.pages[2].name, "Shifted Page") == 0);
    assert(test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 1U, .macro = 0U}
    ).automation.stored());
    assert(test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 2U, .macro = 3U}
    ).automation.stored());
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Deleted") == 0);
    assert(h.state.redoProjectHistory());
    assert(track.enabledPageMask == 0x0003U);
    assert(h.state.pages.currentActivePage() == 1U);
    assert(std::strcmp(track.pages[1].name, "Shifted Page") == 0);
    assert(compacted.automation.id == test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 1U, .macro = 3U}
    ).automation.id);
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Shifted Page") == 0);

    // Removing the last Page returns focus to the previous surviving Page.
    assert(h.structureServices.deletePage(1U));
    assert(track.enabledPageMask == 0x0001U);
    assert(h.state.pages.currentActivePage() == 0U);
    assert(!test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 1U, .macro = 3U}
    ).present());

    drainNotifications();
    std::cout << "[PASS] test_delete_intermediate_macro_page_compacts_complete_page_state\n";
}

void test_left_bottom_shows_temporary_edit_prompt() {
    MacroPerformanceHarness h;

    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::NONE);

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.tick(1);
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::EDIT);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::EDIT);

    h.release(Config::ButtonID::LEFT_BOTTOM);
    h.tick(2);
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::NONE);

    drainNotifications();

    std::cout << "[PASS] LEFT_BOTTOM shows temporary Edit prompt\n";
}

void test_left_bottom_macro_chord_cannot_fall_through_to_performance_action() {
    MacroPerformanceHarness h;

    configureMacroAutomation(h.state, 0U, 0U, 0U, 0.42f);
    assert(!h.performanceServices.manualOverrideActiveFor(0U));

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.press(Config::ButtonID::MACRO_1);
    h.release(Config::ButtonID::MACRO_1);

    assert(h.state.macroUi.focusedMacroSlot.get() == 0U);
    assert(!h.performanceServices.manualOverrideActiveFor(0U));
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::EDIT);
    h.release(Config::ButtonID::LEFT_BOTTOM);

    assert(!h.state.pages.isMacroSlotActive(1U));
    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.press(Config::ButtonID::MACRO_2);
    h.release(Config::ButtonID::MACRO_2);

    assert(h.state.macroUi.focusedMacroSlot.get() == 1U);
    assert(!h.state.pages.isMacroSlotActive(1U));
    h.release(Config::ButtonID::LEFT_BOTTOM);

    drainNotifications();
    std::cout << "[PASS] LEFT_BOTTOM + Macro owns release without performance fallthrough\n";
}

void test_left_top_cancels_edit_prompt() {
    MacroPerformanceHarness h;

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.tick(1);
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::EDIT);

    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    h.tick(2);
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::NONE);

    drainNotifications();

    std::cout << "[PASS] LEFT_TOP cancels Edit prompt\n";
}

void test_left_center_arms_take_and_nav_selects_timing() {
    MacroPerformanceHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.tick(1);
    assert(h.state.macroUi.automationTake.phase ==
           core::state::macro::MacroAutomationTakePhase::ARMED);
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::AUTOMATION_TAKE);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.automationTakeTiming.get() ==
           core::state::macro::MacroAutomationTakeTiming::NOTE_1_16);
    h.release(Config::ButtonID::LEFT_CENTER);
    h.tick(2);
    assert(h.state.macroUi.automationTake.phase ==
           core::state::macro::MacroAutomationTakePhase::IDLE);
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::NONE);

    drainNotifications();

    std::cout << "[PASS] LEFT_CENTER arms take and NAV selects timing\n";
}

void test_macro_slot_selection_pastes_sparse_footprint_atomically() {
    MacroPerformanceHarness h;
    auto& page = h.state.pages.pageData(0U, 0U);
    page.setMacroActive(0U, true);
    page.setMacroActive(3U, true);
    page.cc[0] = 21U;
    page.values[0] = 0.21f;
    page.cc[3] = 74U;
    page.values[3] = 0.74f;
    h.state.pages.updateActiveConfigs();
    configureMacroAutomation(h.state, 0U, 0U, 0U, 0.42f);
    h.navigationFocus.set(
        core::state::StructureNavigationFocus::STEP
    );

    h.tick(1000U);
    h.press(Config::ButtonID::NAV);
    h.tick(
        1000U +
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
    );
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroUi.slotSelection.active.get());
    assert(!h.state.macroUi.slotSelection.placing.get());

    h.press(Config::ButtonID::MACRO_1);
    h.release(Config::ButtonID::MACRO_1);
    h.press(Config::ButtonID::MACRO_4);
    h.release(Config::ButtonID::MACRO_4);
    assert(h.state.macroUi.slotSelection.selectedCount() == 2U);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.macroUi.slotSelection.placing.get());
    assert(h.clipboard.hasMacroSlotSelection());
    assert(h.clipboard.macroAutomationSet->count == 2U);

    for (int position = 1; position <= 7; ++position) {
        h.turn(
            Config::EncoderID::NAV,
            static_cast<float>(position)
        );
    }
    assert(h.state.macroUi.slotSelection.cursorLinear.get() == 10U);
    assert(h.state.macroUi.previewAddPageSlot.get());
    assert(h.state.macroUi.slotSelection.destinationMasks[1] ==
           static_cast<uint8_t>((1U << 2U) | (1U << 5U)));
    assert(h.state.macroUi.slotSelection.overwriteCount == 0U);

    const uint8_t undoBefore = h.state.projectHistory.undoCount();
    h.tick(2000U);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(
        2000U +
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
    );
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.pages.currentEnabledPageMask() == 0x0003U);
    assert(h.state.pages.currentActivePage() == 1U);
    const auto& pasted = h.state.pages.pageData(0U, 1U);
    assert(pasted.isMacroActive(2U));
    assert(pasted.isMacroActive(5U));
    assert(!pasted.isMacroActive(0U));
    assert(pasted.cc[2] == 21U);
    assert(pasted.cc[5] == 74U);
    assert(std::fabs(pasted.values[2] - 0.21f) < 0.0001f);
    assert(std::fabs(pasted.values[5] - 0.74f) < 0.0001f);
    assert(test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 1U, .macro = 2U}
    ).automation.stored());
    assert(h.state.projectHistory.undoCount() ==
           static_cast<uint8_t>(undoBefore + 1U));
    assert(h.state.macroUi.slotSelection.placing.get());
    assert(h.state.macroUi.slotSelection.overwriteCount == 2U);

    assert(h.state.undoProjectHistory());
    assert(h.state.pages.currentEnabledPageMask() == 0x0001U);
    assert(!test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 1U, .macro = 2U}
    ).present());
    assert(h.state.redoProjectHistory());
    assert(h.state.pages.currentEnabledPageMask() == 0x0003U);
    assert(h.state.pages.pageData(0U, 1U).isMacroActive(2U));
    assert(h.state.pages.pageData(0U, 1U).isMacroActive(5U));

    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(h.state.macroUi.slotSelection.active.get());
    assert(!h.state.macroUi.slotSelection.placing.get());
    assert(h.state.macroUi.slotSelection.selectedCount() == 0U);
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroUi.slotSelection.selectedCount() == 1U);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.macroUi.slotSelection.placing.get());
    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(h.state.macroUi.slotSelection.active.get());
    assert(!h.state.macroUi.slotSelection.placing.get());
    assert(h.state.macroUi.slotSelection.selectedCount() == 0U);
    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.state.macroUi.slotSelection.active.get());

    drainNotifications();
    std::cout
        << "[PASS] sparse Macro selection Paste is one exact Undo/Redo\n";
}

void test_macro_page_selection_uses_shared_grammar_and_warns_before_overwrite() {
    MacroPerformanceHarness h;
    auto& source = h.state.pages.pageData(0U, 0U);
    source.setMacroActive(0U, true);
    source.cc[0] = 71U;
    source.values[0] = 0.63f;
    h.state.pages.updateActiveConfigs();
    configureMacroAutomation(h.state, 0U, 0U, 0U, 0.37f);
    (void)configureLocalLfo(h.state, 0U, 0U, 0U);
    h.navigationFocus.set(
        core::state::StructureNavigationFocus::PAGE
    );

    h.tick(1000U);
    h.press(Config::ButtonID::NAV);
    h.tick(
        1000U +
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
    );
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroUi.pageSelection.active.get());
    assert(!h.state.macroUi.slotSelection.active.get());
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(
        h.state.macroUi.pageSelection.selectedMask.get() ==
        0x0001U
    );

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.macroUi.pageSelection.placing.get());
    assert(h.clipboard.hasMacroPageSelection());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.macroUi.pageSelection.cursorIndex.get() == 1U
    );
    assert(
        h.state.macroUi.pageSelection.destinationMask.get() ==
        0x0002U
    );
    assert(
        h.state.macroUi.pageSelection.overwriteMask.get() == 0U
    );

    h.tick(2000U);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(
        2000U +
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
    );
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.pages.currentEnabledPageMask() == 0x0003U);
    assert(h.state.pages.currentActivePage() == 1U);
    const auto& pasted = h.state.pages.pageData(0U, 1U);
    assert(pasted.isMacroActive(0U));
    assert(pasted.cc[0] == 71U);
    assert(std::fabs(pasted.values[0] - 0.63f) < 0.0001f);
    assert(test_support::project_control::readSlot(
        h.state.pages.control,
        {.track = 0U, .page = 1U, .macro = 0U}
    ).automation.stored());
    assert(modulationBindingCountAt(h.state, 0U, 1U, 0U) == 1U);
    assert(
        h.state.macroUi.pageSelection.overwriteMask.get() ==
        0x0002U
    );

    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(h.state.macroUi.pageSelection.active.get());
    assert(!h.state.macroUi.pageSelection.placing.get());
    assert(
        h.state.macroUi.pageSelection.selectedMask.get() == 0U
    );
    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.state.macroUi.pageSelection.active.get());

    assert(h.state.undoProjectHistory());
    assert(h.state.pages.currentEnabledPageMask() == 0x0001U);
    assert(modulationBindingCountAt(h.state, 0U, 1U, 0U) == 0U);
    assert(h.state.redoProjectHistory());
    assert(h.state.pages.currentEnabledPageMask() == 0x0003U);
    assert(modulationBindingCountAt(h.state, 0U, 1U, 0U) == 1U);

    drainNotifications();
    std::cout
        << "[PASS] Macro Page selection shares placement/back grammar\n";
}

void test_macro_track_selection_copies_the_complete_global_track() {
    MacroPerformanceHarness h;
    h.state.sequencer.pattern.setContentLength(8U);
    h.state.sequencer.pattern.setEnabled(0U, true);
    h.state.sequencer.pattern.note[0] = 79U;
    auto& page = h.state.pages.pageData(0U, 0U);
    page.setMacroActive(2U, true);
    page.cc[2] = 22U;
    page.values[2] = 0.82f;
    h.state.pages.updateActiveConfigs();
    (void)configureLocalLfo(h.state, 0U, 0U, 2U);
    h.navigationFocus.set(
        core::state::StructureNavigationFocus::TRACK
    );

    h.tick(3000U);
    h.press(Config::ButtonID::NAV);
    h.tick(
        3000U +
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
    );
    h.release(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.active.get());
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(
        h.state.trackNavigation.selection.selectedMask.get() ==
        0x0001U
    );

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.clipboard.hasSequencerTrackSelection());
    assert(h.state.trackNavigation.selection.placing.get());
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.trackNavigation.selection.cursorIndex.get() == 1U
    );
    assert(
        h.state.trackNavigation.selection.destinationMask.get() ==
        0x0002U
    );

    h.tick(4000U);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(
        4000U +
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
    );
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.currentSharedTrackEnabledMask() == 0x0003U);
    assert(h.state.currentSharedActiveTrack() == 1U);
    assert(h.state.sequencer.pattern.note[0] == 79U);
    assert(h.state.pages.pageData(1U, 0U).isMacroActive(2U));
    assert(h.state.pages.pageData(1U, 0U).cc[2] == 22U);
    assert(
        std::fabs(
            h.state.pages.pageData(1U, 0U).values[2] - 0.82f
        ) < 0.0001f
    );
    assert(modulationBindingCountAt(h.state, 1U, 0U, 2U) == 1U);

    assert(h.state.undoProjectHistory());
    assert(h.state.currentSharedTrackEnabledMask() == 0x0001U);
    assert(!h.state.pages.pageData(1U, 0U).isMacroActive(2U));
    assert(modulationBindingCountAt(h.state, 1U, 0U, 2U) == 0U);
    assert(h.state.redoProjectHistory());
    assert(h.state.currentSharedTrackEnabledMask() == 0x0003U);
    assert(h.state.pages.pageData(1U, 0U).isMacroActive(2U));
    assert(modulationBindingCountAt(h.state, 1U, 0U, 2U) == 1U);

    drainNotifications();
    std::cout
        << "[PASS] Track selection is global from the Macro view\n";
}

}  // namespace

int main() {
    test_nav_turn_switches_enabled_macro_page_directly();
    test_nav_focus_track_turn_switches_context_to_highlighted_macro_track();
    test_macro_track_cursor_can_cross_gaps_and_reach_any_track();
    test_macro_page_hot_surface_exposes_terminal_add_and_creates();
    test_macro_track_hot_surface_crosses_sparse_free_slots_and_creates_exactly();
    test_track_focus_outside_structure_targets_the_active_track();
    test_macro_context_nav_tap_creates_focused_empty_slot();
    test_macro_buttons_create_empty_slots_and_toggle_manual_resume();
    test_macro_context_selector_and_hot_navigation_are_direct();
    test_macro_page_copy_and_long_press_paste();
    test_macro_track_copy_and_long_press_paste_to_add_slot();
    test_early_paste_release_only_cancels_the_hold_for_page_and_track();
    test_paste_only_press_cannot_turn_into_copy_after_navigation();
    test_guarded_page_and_track_actions_cancel_when_target_moves();
    test_page_navigation_applies_live_and_actions_follow_visible_page();
    test_macro_track_copy_preserves_multiple_pages_and_automations();
    test_macro_slot_focus_uses_typed_clipboard_and_guarded_remove();
    test_delete_intermediate_macro_page_compacts_complete_page_state();
    test_left_bottom_shows_temporary_edit_prompt();
    test_left_bottom_macro_chord_cannot_fall_through_to_performance_action();
    test_left_top_cancels_edit_prompt();
    test_left_center_arms_take_and_nav_selects_timing();
    test_macro_slot_selection_pastes_sparse_footprint_atomically();
    test_macro_page_selection_uses_shared_grammar_and_warns_before_overwrite();
    test_macro_track_selection_copies_the_complete_global_track();
    std::cout << "\nAll MacroPerformanceHandler tests passed.\n";
    return 0;
}
