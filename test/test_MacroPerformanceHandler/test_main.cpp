#include <cassert>
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
                storage.macroWorkspace,
                storage.macroLibrary,
                storage.sequencerWorkspace,
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
              MACRO_VIEW_SCOPE
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

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
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

void test_nav_selection_mode_deletes_selected_macro_page() {
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

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.pages.currentEnabledPageMask() == 0x0001);
    assert(h.state.pages.currentActivePage() == 0);
    assert(!h.state.macroUi.pageSelection.active.get());

    drainNotifications();

    std::cout << "[PASS] test_nav_selection_mode_deletes_selected_macro_page\n";
}

void test_nav_selection_mode_deletes_selected_macro_track() {
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

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.pages.currentTrackEnabledMask() == 0x0001);
    assert(h.state.pages.currentActiveTrack() == 0);
    assert(!h.state.trackNavigation.selection.active.get());

    drainNotifications();

    std::cout << "[PASS] test_nav_selection_mode_deletes_selected_macro_track\n";
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

    drainNotifications();

    std::cout << "[PASS] test_macro_track_copy_and_long_press_paste_to_add_slot\n";
}

void test_left_bottom_short_press_latches_property_clutch_and_second_tap_releases_it() {
    MacroPerformanceHarness h;

    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::VALUE);
    assert(!h.state.macroUi.clutchActive.get());

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.tick(1);
    assert(h.state.macroUi.clutchActive.get());
    h.release(Config::ButtonID::LEFT_BOTTOM);
    h.tick(2);
    assert(h.state.macroUi.clutchActive.get());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::CC);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::CHANNEL);

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.tick(3);
    h.release(Config::ButtonID::LEFT_BOTTOM);
    h.tick(4);
    assert(!h.state.macroUi.clutchActive.get());
    assert(h.state.macroUi.activeProperty.get() ==
           core::state::macro::MacroPerformanceProperty::VALUE);

    drainNotifications();

    std::cout << "[PASS] test_left_bottom_short_press_latches_property_clutch_and_second_tap_releases_it\n";
}

void test_left_center_opens_macro_quick_controls_and_opt_applies_global_channel_and_cc_offset() {
    MacroPerformanceHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.tick(1);
    assert(h.state.macroUi.quickControlsSelecting.get());
    assert(h.state.macroUi.focusedQuickControl.get() ==
           core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.macroUi.quickControlGlobalChannel.get() == 15);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.focusedQuickControl.get() ==
           core::state::macro::MacroQuickControlItem::CC_OFFSET);

    const uint8_t ccBefore = h.state.pages.activeConfigs[0].cc;
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.macroUi.ccOffset.get() >= 0);
    assert(h.state.pages.activeConfigs[0].cc == ccBefore);

    h.release(Config::ButtonID::LEFT_CENTER);
    h.tick(2);
    assert(h.state.macroUi.quickControlsSelecting.get());

    h.press(Config::ButtonID::LEFT_CENTER);
    h.tick(3);
    h.release(Config::ButtonID::LEFT_CENTER);
    h.tick(4);
    assert(!h.state.macroUi.quickControlsSelecting.get());
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        assert(h.state.pages.activeConfigs[i].channel == 15);
        assert(h.state.pages.activeConfigs[i].cc >= ccBefore);
    }

    drainNotifications();

    std::cout << "[PASS] test_left_center_opens_macro_quick_controls_and_opt_applies_global_channel_and_cc_offset\n";
}

}  // namespace

int main() {
    test_nav_turn_previews_macro_page_until_nav_commit();
    test_nav_focus_track_turn_switches_context_to_highlighted_macro_track();
    test_macro_track_cursor_can_cross_gaps_and_reach_any_track();
    test_macro_page_add_slot_is_terminal_and_does_not_wrap_on_reverse();
    test_nav_selection_mode_deletes_selected_macro_page();
    test_nav_selection_mode_deletes_selected_macro_track();
    test_macro_page_copy_and_long_press_paste();
    test_macro_selection_duplicate_copies_page_into_first_free_slot();
    test_macro_selection_duplicate_copies_track_into_first_free_slot();
    test_macro_track_copy_and_long_press_paste_to_add_slot();
    test_left_bottom_short_press_latches_property_clutch_and_second_tap_releases_it();
    test_left_center_opens_macro_quick_controls_and_opt_applies_global_channel_and_cc_offset();
    std::cout << "\nAll MacroPerformanceHandler tests passed.\n";
    return 0;
}
