#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "handler/settings/GlobalSettingsDomainServices.hpp"
#include "state/GlobalSettingsState.hpp"
#include "state/ViewSelectorState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

/**
 * Binds the global settings overlay and selector to input.
 *
 * The handler owns modal open/close and navigation. Applying choices is
 * delegated to GlobalSettingsDomainServices.
 */
class GlobalSettingsHandler {
public:
    struct StateRefs {
        core::state::GlobalSettingsState& globalSettings;
        core::state::ViewSelectorState& viewSelector;
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

    void closeSettings();
    void armSettingsBack();
    void backToViewSelector();

    void moveFocus(float delta);
    void openValueSelector();

    void navigateSelector(float delta);
    void applySelectorAndClose();
    void closeSelectorCancel();

    core::state::GlobalSettingsState& global_settings_;
    core::state::ViewSelectorState& view_selector_;
    GlobalSettingsDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID settings_overlay_scope_ = 0;
    oc::type::ScopeID selector_overlay_scope_ = 0;
    bool left_top_pressed_in_settings_ = false;

    static constexpr uint8_t ROW_COUNT = 4;
};

}  // namespace core::handler
