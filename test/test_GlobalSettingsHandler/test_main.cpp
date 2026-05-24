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
#include <config/InputIDs.hpp>
#include "../../src/handler/settings/GlobalSettingsDomainServices.hpp"
#include "../../src/handler/settings/GlobalSettingsHandler.hpp"
#include "../../src/state/CoreSettings.hpp"
#include "../../src/state/GlobalSettingsState.hpp"
#include "../../src/state/MidiSyncState.hpp"
#include "../../src/state/ViewSelectorState.hpp"
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
    core::state::ViewSelectorState viewSelector;
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
        , handler(core::handler::GlobalSettingsHandler::StateRefs{
                      globalSettings,
                      viewSelector,
                  },
                  services,
                  overlays,
                  encoders,
                  buttons,
                  SETTINGS_SCOPE,
                  SELECTOR_SCOPE) {
        storage.init();
        overlayState.registerItem(core::ui::OverlayType::VIEW_SELECTOR, viewSelector.visible);
        overlayState.registerItem(core::ui::OverlayType::GLOBAL_SETTINGS, globalSettings.visible);
        overlayState.registerItem(core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR, globalSettings.selector.visible);
        overlays.registerCleanup(core::ui::OverlayType::VIEW_SELECTOR, SELECTOR_SCOPE);
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

void openSettings(GlobalSettingsHarness& h) {
    h.globalSettings.openOverlay();
    h.overlays.show(core::ui::OverlayType::GLOBAL_SETTINGS, false);
    assert(h.globalSettings.visible.get());
    assert(h.globalSettings.flowPhase.get() == core::state::GlobalSettingsFlowPhase::OVERLAY);
    assert(h.overlays.current() == core::ui::OverlayType::GLOBAL_SETTINGS);
}

void test_settings_back_returns_to_view_selector_on_single_press() {
    GlobalSettingsHarness h;

    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(!h.globalSettings.visible.get());

    openSettings(h);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.globalSettings.visible.get());
    assert(h.viewSelector.visible.get());
    assert(h.viewSelector.selectedIndex.get() == 2);
    assert(h.overlays.current() == core::ui::OverlayType::VIEW_SELECTOR);

    std::cout << "[PASS] test_settings_back_returns_to_view_selector_on_single_press\n";
}

void test_selector_navigation_and_apply_follow_real_bindings() {
    GlobalSettingsHarness h;
    openSettings(h);

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
    openSettings(h);

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
    test_settings_back_returns_to_view_selector_on_single_press();
    test_selector_navigation_and_apply_follow_real_bindings();
    test_selector_cancel_restores_parent_overlay_without_applying();
    std::cout << "\nAll GlobalSettingsHandler tests passed.\n";
    return 0;
}
