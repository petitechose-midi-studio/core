#include <cassert>
#include <cstring>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/macro/MacroDomainServices.hpp"
#include "../../src/handler/macro/MacroDomainServices.cpp"
#include "../../src/handler/macro/MacroPerformanceHandler.hpp"
#include "../../src/handler/macro/MacroPerformanceHandler.cpp"
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
    core::handler::MacroDomainServices services;

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
        , services(core::handler::MacroDomainServices::fromCoreState(state))
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(
              core::handler::MacroPerformanceHandler::StateRefs{
                  state.macroUi,
                  state.pages,
              },
              services,
              overlays,
              encoders,
              buttons,
              MACRO_VIEW_SCOPE
          ) {
        overlays.setActiveViewProvider([]() { return MACRO_VIEW_SCOPE; });
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

void test_nav_turn_changes_macro_page_when_clutch_is_inactive() {
    MacroPerformanceHarness h;

    std::strncpy(h.state.pages.activeTrackData().pages[1].name,
                 "Page 2",
                 core::state::macro::PAGE_NAME_SIZE - 1);
    h.state.pages.activeTrackData().pages[1].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';

    h.turn(Config::EncoderID::NAV, 1.0f);

    assert(h.state.pages.activePage == 1);
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Page 2") == 0);
    assert(!h.state.macroUi.clutchActive.get());

    drainNotifications();

    std::cout << "[PASS] test_nav_turn_changes_macro_page_when_clutch_is_inactive\n";
}

void test_nav_hold_and_turn_changes_macro_track_immediately() {
    MacroPerformanceHarness h;

    h.state.pages.tracks[1].activePage = 3;
    h.state.pages.tracks[1].channel = 9;
    h.state.pages.tracks[1].pages[3].cc[0] = 91;
    std::strncpy(h.state.pages.tracks[1].pages[3].name,
                 "Track 2 Page 4",
                 core::state::macro::PAGE_NAME_SIZE - 1);
    h.state.pages.tracks[1].pages[3].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::NAV);

    assert(h.state.pages.activeTrack == 1);
    assert(h.state.pages.activePage == 3);
    assert(h.services.activeConfig(0).channel == 9);
    assert(h.services.activeConfig(0).cc == 91);
    assert(std::strcmp(h.state.statusBar.pageName.get(), "Track 2 Page 4") == 0);

    drainNotifications();

    std::cout << "[PASS] test_nav_hold_and_turn_changes_macro_track_immediately\n";
}

void test_nav_short_release_toggles_active_macro_track_enabled() {
    MacroPerformanceHarness h;

    h.state.pages.trackEnabledMask.set(0x03);
    h.state.pages.activeTrack = 0;

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.pages.trackEnabledMask.get() == 0x02);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.pages.trackEnabledMask.get() == 0x03);

    h.state.pages.trackEnabledMask.set(0x01);
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.pages.trackEnabledMask.get() == 0x01);

    drainNotifications();

    std::cout << "[PASS] test_nav_short_release_toggles_active_macro_track_enabled\n";
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
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        assert(h.state.pages.activeConfigs[i].channel == 15);
    }

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroUi.focusedQuickControl.get() ==
           core::state::macro::MacroQuickControlItem::CC_OFFSET);

    const uint8_t ccBefore = h.state.pages.activeConfigs[0].cc;
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.macroUi.ccOffset.get() >= 0);
    assert(h.state.pages.activeConfigs[0].cc >= ccBefore);

    h.release(Config::ButtonID::LEFT_CENTER);
    h.tick(2);
    assert(h.state.macroUi.quickControlsSelecting.get());

    h.press(Config::ButtonID::LEFT_CENTER);
    h.tick(3);
    h.release(Config::ButtonID::LEFT_CENTER);
    h.tick(4);
    assert(!h.state.macroUi.quickControlsSelecting.get());

    drainNotifications();

    std::cout << "[PASS] test_left_center_opens_macro_quick_controls_and_opt_applies_global_channel_and_cc_offset\n";
}

}  // namespace

int main() {
    test_nav_turn_changes_macro_page_when_clutch_is_inactive();
    test_nav_hold_and_turn_changes_macro_track_immediately();
    test_nav_short_release_toggles_active_macro_track_enabled();
    test_left_bottom_short_press_latches_property_clutch_and_second_tap_releases_it();
    test_left_center_opens_macro_quick_controls_and_opt_applies_global_channel_and_cc_offset();
    std::cout << "\nAll MacroPerformanceHandler tests passed.\n";
    return 0;
}
