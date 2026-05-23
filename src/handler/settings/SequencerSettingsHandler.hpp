#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
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
                             oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                             oc::api::EncoderAPI& encoders,
                             oc::api::ButtonAPI& buttons,
                             oc::type::ScopeID settingsOverlayScope);

    SequencerSettingsHandler(const SequencerSettingsHandler&) = delete;
    SequencerSettingsHandler& operator=(const SequencerSettingsHandler&) = delete;

private:
    void setupBindings();
    void closeSettings();
    void armSettingsBack();
    void backToViewSelector();
    void moveFocus(float delta);

    core::state::SequencerSettingsState& sequencer_settings_;
    core::state::ViewSelectorState& view_selector_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID settings_overlay_scope_ = 0;
    bool left_top_pressed_in_settings_ = false;

    static constexpr uint8_t ROW_COUNT = 1;
};

}  // namespace core::handler
