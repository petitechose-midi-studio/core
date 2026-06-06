#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "handler/settings/DeviceSettingsDomainServices.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/ViewSelectorState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

/**
 * Binds the device settings view and value selector to input.
 *
 * The top-level view selector is owned by ViewSwitcherHandler. This handler
 * only owns row navigation and the modal value picker.
 */
class DeviceSettingsHandler {
public:
    struct StateRefs {
        core::state::DeviceSettingsState& deviceSettings;
    };

    DeviceSettingsHandler(StateRefs state,
                          DeviceSettingsDomainServices services,
                          oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          oc::type::ScopeID settingsViewScope,
                          oc::type::ScopeID selectorOverlayScope);

    ~DeviceSettingsHandler() = default;

    DeviceSettingsHandler(const DeviceSettingsHandler&) = delete;
    DeviceSettingsHandler& operator=(const DeviceSettingsHandler&) = delete;

private:
    void setupBindings();

    void moveFocus(float delta);
    void openValueSelector();

    void navigateSelector(float delta);
    void applySelectorAndClose();
    void closeSelectorCancel();

    core::state::DeviceSettingsState& device_settings_;
    DeviceSettingsDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID settings_view_scope_ = 0;
    oc::type::ScopeID selector_overlay_scope_ = 0;

    static constexpr uint8_t ROW_COUNT = 4;
};

}  // namespace core::handler
