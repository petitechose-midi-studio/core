#include "DeviceSettingsHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/ValueSelectorInput.hpp"
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

    modal::bindValueSelectorInputs(encoders_, buttons_, selector_overlay_scope_,
        [this](modal::ValueSelectorInput input) {
            input.handle(overlays_, core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR,
                device_settings_.selector,
                device_settings_.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR,
                services_.choiceCount(device_settings_.selector.editingRow.get()),
                [this](uint8_t row, int choice) { return services_.applyChoice(row, choice).success(); },
                [this]() { device_settings_.closeSelector(); });
        });
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

}  // namespace core::handler
