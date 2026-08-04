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
#include "../../src/handler/settings/DeviceSettingsDomainServices.hpp"
#include "../../src/handler/settings/DeviceSettingsHandler.hpp"
#include "../../src/state/DeviceSettingsState.hpp"
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

struct DeviceSettingsHarness {
    static constexpr oc::type::ScopeID SETTINGS_SCOPE = 101;
    static constexpr oc::type::ScopeID SELECTOR_SCOPE = 102;

    MemoryStorage storage;
    core::state::MidiSyncState midiSync;
    core::persistence::DeviceSettingsStore settingsStore;
    core::state::DeviceSettingsState deviceSettings;
    core::handler::DeviceSettingsDomainServices services;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType> overlayState;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::DeviceSettingsHandler handler;

    DeviceSettingsHarness()
        : settingsStore(storage)
        , services(core::handler::DeviceSettingsDomainServices::StateRefs{
              midiSync,
              settingsStore,
          })
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(overlayState, buttons)
        , handler(core::handler::DeviceSettingsHandler::StateRefs{
                      deviceSettings,
                  },
                  services,
                  overlays,
                  encoders,
                  buttons,
                  SETTINGS_SCOPE,
                  SELECTOR_SCOPE) {
        storage.init();
        assert(settingsStore.load(midiSync));
        overlayState.registerItem(
            core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR,
            deviceSettings.selector.visible
        );
        overlays.registerCleanup(core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR, SELECTOR_SCOPE);
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

void openSettings(DeviceSettingsHarness& h) {
    h.deviceSettings.openView();
    assert(h.deviceSettings.visible.get());
    assert(h.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VIEW);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
}

void test_settings_left_top_is_owned_by_view_switcher() {
    DeviceSettingsHarness h;

    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(!h.deviceSettings.visible.get());

    openSettings(h);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.deviceSettings.visible.get());
    assert(h.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VIEW);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    std::cout << "[PASS] test_settings_left_top_is_owned_by_view_switcher\n";
}

void test_selector_navigation_and_apply_follow_real_bindings() {
    DeviceSettingsHarness h;
    openSettings(h);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.deviceSettings.focusedRow.get() == 1);

    h.tap(Config::ButtonID::NAV);
    assert(h.deviceSettings.selector.visible.get());
    assert(h.deviceSettings.selector.editingRow.get() == 1);
    assert(h.deviceSettings.selector.selectedIndex.get() == 1);
    assert(h.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR);
    assert(h.overlays.current() == core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.deviceSettings.selector.selectedIndex.get() == 0);

    h.tap(Config::ButtonID::NAV);
    assert(!h.midiSync.followTransport.get());
    assert(!h.deviceSettings.selector.visible.get());
    assert(h.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VIEW);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    std::cout << "[PASS] test_selector_navigation_and_apply_follow_real_bindings\n";
}

void test_selector_cancel_restores_parent_overlay_without_applying() {
    DeviceSettingsHarness h;
    openSettings(h);

    const auto beforeMode = h.midiSync.mode.get();

    h.tap(Config::ButtonID::NAV);
    assert(h.deviceSettings.selector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.deviceSettings.selector.selectedIndex.get() !=
           h.services.currentChoiceIndex(h.deviceSettings.selector.editingRow.get()));

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.midiSync.mode.get() == beforeMode);
    assert(!h.deviceSettings.selector.visible.get());
    assert(h.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VIEW);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    std::cout << "[PASS] test_selector_cancel_restores_parent_overlay_without_applying\n";
}

void test_selector_commit_failure_stays_open_and_retries() {
    DeviceSettingsHarness h;
    openSettings(h);

    h.tap(Config::ButtonID::NAV);
    assert(h.deviceSettings.flowPhase.get() ==
           core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.deviceSettings.selector.selectedIndex.get() == 1);

    h.storage.setFaultMode(MemoryStorage::FaultMode::COMMIT_FAIL);
    h.tap(Config::ButtonID::NAV);
    assert(h.midiSync.mode.get() == core::state::MidiSyncMode::AUTO);
    assert(h.deviceSettings.selector.visible.get());
    assert(h.deviceSettings.flowPhase.get() ==
           core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR);
    assert(h.overlays.current() ==
           core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);

    h.storage.setFaultMode(MemoryStorage::FaultMode::NONE);
    h.tap(Config::ButtonID::NAV);
    assert(h.midiSync.mode.get() == core::state::MidiSyncMode::SLAVE);
    assert(!h.deviceSettings.selector.visible.get());
    assert(h.deviceSettings.flowPhase.get() ==
           core::state::DeviceSettingsFlowPhase::VIEW);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    std::cout << "[PASS] test_selector_commit_failure_stays_open_and_retries\n";
}

}  // namespace

int main() {
    test_settings_left_top_is_owned_by_view_switcher();
    test_selector_navigation_and_apply_follow_real_bindings();
    test_selector_cancel_restores_parent_overlay_without_applying();
    test_selector_commit_failure_stays_open_and_retries();
    std::cout << "\nAll DeviceSettingsHandler tests passed.\n";
    return 0;
}
