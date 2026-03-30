#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>
#include "../../src/handler/settings/GlobalSettingsDomainServices.hpp"
// Native tests only build selected source folders; include the implementation
// here so this handler-level service remains testable without widening the
// environment's global src filter.
#include "../../src/handler/settings/GlobalSettingsDomainServices.cpp"
#include "../../src/handler/settings/GlobalSettingsHandler.hpp"
// Same rationale for the handler itself: keep the native src filter narrow
// while still testing the real binding logic end to end.
#include "../../src/handler/settings/GlobalSettingsHandler.cpp"
#include "../../src/state/CoreSettings.hpp"
#include "../../src/state/GlobalSettingsState.hpp"
#include "../../src/state/MidiSyncState.hpp"
#include "../../src/ui/OverlayTypes.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/MemoryStorage.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::MemoryStorage;
using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct GlobalSettingsHarness {
    static constexpr oc::type::ScopeID SETTINGS_SCOPE = 101;
    static constexpr oc::type::ScopeID SELECTOR_SCOPE = 102;

    MemoryStorage storage;
    core::state::MidiSyncState midiSync;
    core::state::CoreSettings settings;
    core::state::GlobalSettingsState globalSettings;
    core::handler::GlobalSettingsDomainServices services;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType> overlayState;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::GlobalSettingsHandler handler;

    GlobalSettingsHarness()
        : settings(storage)
        , services(core::handler::GlobalSettingsDomainServices::StateRefs{midiSync, settings})
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(overlayState, buttons)
        , handler(core::handler::GlobalSettingsHandler::StateRefs{globalSettings},
                  services,
                  overlays,
                  encoders,
                  buttons,
                  SETTINGS_SCOPE,
                  SELECTOR_SCOPE) {
        storage.init();
        overlays.registerCleanup(core::ui::OverlayType::GLOBAL_SETTINGS, SETTINGS_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR, SELECTOR_SCOPE);
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

    void turn(Config::EncoderID id, float delta) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, delta);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, delta));
    }
};

void openSettingsWithLongPress(GlobalSettingsHarness& h) {
    h.tick(0);
    h.press(Config::ButtonID::LEFT_TOP);
    h.tick(1999);
    assert(!h.globalSettings.visible.get());
    h.tick(2000);
    assert(h.globalSettings.visible.get());
    assert(h.globalSettings.flowPhase.get() == core::state::GlobalSettingsFlowPhase::OVERLAY);
    assert(h.overlays.current() == core::ui::OverlayType::GLOBAL_SETTINGS);
}

void test_long_press_opens_settings_and_ignores_open_release() {
    GlobalSettingsHarness h;

    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(!h.globalSettings.visible.get());

    openSettingsWithLongPress(h);

    h.release(Config::ButtonID::LEFT_TOP);
    assert(h.globalSettings.visible.get());
    assert(h.globalSettings.flowPhase.get() == core::state::GlobalSettingsFlowPhase::OVERLAY);
    assert(h.overlays.current() == core::ui::OverlayType::GLOBAL_SETTINGS);

    std::cout << "[PASS] test_long_press_opens_settings_and_ignores_open_release\n";
}

void test_selector_navigation_and_apply_follow_real_bindings() {
    GlobalSettingsHarness h;
    openSettingsWithLongPress(h);
    h.release(Config::ButtonID::LEFT_TOP);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.globalSettings.focusedRow.get() == 1);

    h.tap(Config::ButtonID::NAV);
    assert(h.globalSettings.selector.visible.get());
    assert(h.globalSettings.selector.editingRow.get() == 1);
    assert(h.globalSettings.selector.selectedIndex.get() == 1);
    assert(h.globalSettings.flowPhase.get() == core::state::GlobalSettingsFlowPhase::VALUE_SELECTOR);
    assert(h.overlays.current() == core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.globalSettings.selector.selectedIndex.get() == 0);

    h.tap(Config::ButtonID::NAV);
    assert(!h.midiSync.followTransport.get());
    assert(!h.globalSettings.selector.visible.get());
    assert(h.globalSettings.flowPhase.get() == core::state::GlobalSettingsFlowPhase::OVERLAY);
    assert(h.overlays.current() == core::ui::OverlayType::GLOBAL_SETTINGS);

    std::cout << "[PASS] test_selector_navigation_and_apply_follow_real_bindings\n";
}

void test_selector_cancel_restores_parent_overlay_without_applying() {
    GlobalSettingsHarness h;
    openSettingsWithLongPress(h);
    h.release(Config::ButtonID::LEFT_TOP);

    const auto beforeMode = h.midiSync.mode.get();

    h.tap(Config::ButtonID::NAV);
    assert(h.globalSettings.selector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.globalSettings.selector.selectedIndex.get() !=
           h.services.currentChoiceIndex(h.globalSettings.selector.editingRow.get()));

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.midiSync.mode.get() == beforeMode);
    assert(!h.globalSettings.selector.visible.get());
    assert(h.globalSettings.flowPhase.get() == core::state::GlobalSettingsFlowPhase::OVERLAY);
    assert(h.overlays.current() == core::ui::OverlayType::GLOBAL_SETTINGS);

    std::cout << "[PASS] test_selector_cancel_restores_parent_overlay_without_applying\n";
}

}  // namespace

int main() {
    test_long_press_opens_settings_and_ignores_open_release();
    test_selector_navigation_and_apply_follow_real_bindings();
    test_selector_cancel_restores_parent_overlay_without_applying();
    std::cout << "\nAll GlobalSettingsHandler tests passed.\n";
    return 0;
}
