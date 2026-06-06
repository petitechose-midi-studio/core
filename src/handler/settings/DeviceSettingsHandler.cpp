#include "DeviceSettingsHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

FLASHMEM DeviceSettingsHandler::DeviceSettingsHandler(StateRefs state,
                                                      DeviceSettingsDomainServices services,
                                                      oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                      oc::api::EncoderAPI& encoders,
                                                      oc::api::ButtonAPI& buttons,
                                                      oc::type::ScopeID settingsViewScope,
                                                      oc::type::ScopeID selectorOverlayScope)
    : device_settings_(state.deviceSettings)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , settings_view_scope_(settingsViewScope)
    , selector_overlay_scope_(selectorOverlayScope) {
    setupBindings();
}

FLASHMEM void DeviceSettingsHandler::setupBindings() {
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(settings_view_scope_)
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(settings_view_scope_)
        .then([this]() { openValueSelector(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(selector_overlay_scope_)
        .then([this](float delta) { navigateSelector(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(selector_overlay_scope_)
        .then([this]() { applySelectorAndClose(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(selector_overlay_scope_)
        .then([this]() { closeSelectorCancel(); });
}

FLASHMEM void DeviceSettingsHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = static_cast<int>(device_settings_.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);

    device_settings_.focusedRow.set(static_cast<uint8_t>(next));
}

FLASHMEM void DeviceSettingsHandler::openValueSelector() {
    auto& s = device_settings_;
    if (s.flowPhase.get() != core::state::DeviceSettingsFlowPhase::VIEW) {
        return;
    }

    const uint8_t row = s.focusedRow.get();
    const int current = services_.currentChoiceIndex(row);
    s.openSelector(row, current);
    overlays_.show(core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR, true);
}

FLASHMEM void DeviceSettingsHandler::navigateSelector(float delta) {
    if (device_settings_.flowPhase.get() != core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR) {
        return;
    }

    const uint8_t row = device_settings_.selector.editingRow.get();
    const int count = services_.choiceCount(row);
    if (count <= 0) return;

    int next = device_settings_.selector.selectedIndex.get();
    if (!modal::advanceWrappedSelection(delta, device_settings_.selector, count, next)) {
        return;
    }
    device_settings_.selector.selectedIndex.set(next);
}

FLASHMEM void DeviceSettingsHandler::applySelectorAndClose() {
    auto& selector = device_settings_.selector;
    const uint8_t row = selector.editingRow.get();
    const int choice = selector.selectedIndex.get();

    services_.applyChoice(row, choice);

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);
    device_settings_.closeSelector();
}

FLASHMEM void DeviceSettingsHandler::closeSelectorCancel() {
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);
    device_settings_.closeSelector();
}

}  // namespace core::handler
