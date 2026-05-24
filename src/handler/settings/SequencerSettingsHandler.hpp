#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "state/SequencerSettingsState.hpp"
#include "state/ViewSelectorState.hpp"

namespace core::handler {

class SequencerSettingsHandler {
public:
    struct StateRefs {
        core::state::SequencerSettingsState& sequencerSettings;
        core::state::ViewSelectorState& viewSelector;
    };

    SequencerSettingsHandler(StateRefs state,
                             SequencerSettingsDomainServices services,
                             oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                             oc::api::EncoderAPI& encoders,
                             oc::api::ButtonAPI& buttons,
                             oc::type::ScopeID settingsOverlayScope,
                             oc::type::ScopeID selectorOverlayScope);

    SequencerSettingsHandler(const SequencerSettingsHandler&) = delete;
    SequencerSettingsHandler& operator=(const SequencerSettingsHandler&) = delete;

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

    core::state::SequencerSettingsState& sequencer_settings_;
    core::state::ViewSelectorState& view_selector_;
    SequencerSettingsDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID settings_overlay_scope_ = 0;
    oc::type::ScopeID selector_overlay_scope_ = 0;
    bool left_top_pressed_in_settings_ = false;

    static constexpr uint8_t ROW_COUNT = 3;
};

}  // namespace core::handler
