#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/sequencer/PatternPitchSettingsDomainServices.hpp"
#include "state/PatternPitchSettingsState.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

class PatternPitchSettingsHandler {
public:
    struct StateRefs {
        core::state::PatternPitchSettingsState& settings;
        core::state::sequencer::SequencerState& sequencer;
    };

    PatternPitchSettingsHandler(StateRefs state,
                                PatternPitchSettingsDomainServices services,
                                oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                oc::api::EncoderAPI& encoders,
                                oc::api::ButtonAPI& buttons,
                                oc::type::ScopeID sequencerViewScope,
                                oc::type::ScopeID settingsOverlayScope,
                                oc::type::ScopeID selectorOverlayScope);

    PatternPitchSettingsHandler(const PatternPitchSettingsHandler&) = delete;
    PatternPitchSettingsHandler& operator=(const PatternPitchSettingsHandler&) = delete;

private:
    void setupBindings();
    void openSettings();
    void closeSettings();
    void moveFocus(float delta);
    void openValueSelector();
    void navigateSelector(float delta);
    void applySelectorAndClose();
    void closeSelectorCancel();

    core::state::PatternPitchSettingsState& settings_;
    core::state::sequencer::SequencerState& sequencer_;
    PatternPitchSettingsDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    oc::type::ScopeID settings_overlay_scope_ = 0;
    oc::type::ScopeID selector_overlay_scope_ = 0;

    static constexpr uint8_t ROW_COUNT = 4;
};

}  // namespace core::handler
