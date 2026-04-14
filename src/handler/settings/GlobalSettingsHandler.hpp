#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "handler/settings/GlobalSettingsDomainServices.hpp"
#include "state/GlobalSettingsState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class GlobalSettingsHandler {
public:
    struct StateRefs {
        core::state::GlobalSettingsState& globalSettings;
    };

    GlobalSettingsHandler(StateRefs state,
                          GlobalSettingsDomainServices services,
                          oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          oc::type::ScopeID settingsOverlayScope,
                          oc::type::ScopeID selectorOverlayScope);

    ~GlobalSettingsHandler() = default;

    GlobalSettingsHandler(const GlobalSettingsHandler&) = delete;
    GlobalSettingsHandler& operator=(const GlobalSettingsHandler&) = delete;

private:
    void setupBindings();

    void openSettings();
    void closeSettings();

    void moveFocus(float delta);
    void openValueSelector();

    void navigateSelector(float delta);
    void applySelectorAndClose();
    void closeSelectorCancel();

    core::state::GlobalSettingsState& global_settings_;
    GlobalSettingsDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID settings_overlay_scope_ = 0;
    oc::type::ScopeID selector_overlay_scope_ = 0;

    bool ignore_open_release_ = false;

    static constexpr uint8_t ROW_COUNT = 4;
    static constexpr uint32_t SETTINGS_LONG_PRESS_MS = 2000;
};

}  // namespace core::handler
