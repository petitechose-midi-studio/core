#pragma once

#include <array>

#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::handler {

class MacroPerformanceModeWorkflow {
public:
    struct StateRefs {
        core::state::macro::MacroUiState& macroUi;
        core::state::macro::MacroPagesState& pages;
        core::state::TrackNavigationState& trackNavigation;
    };

    MacroPerformanceModeWorkflow(StateRefs state,
                                 MacroPerformanceDomainServices services,
                                 oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                 oc::api::EncoderAPI& encoders);

    MacroPerformanceModeWorkflow(const MacroPerformanceModeWorkflow&) = delete;
    MacroPerformanceModeWorkflow& operator=(const MacroPerformanceModeWorkflow&) = delete;

    bool performanceAvailable() const;
    bool quickControlsSelecting() const;
    bool clutchActive() const;
    bool clutchInactive() const;

    void activateClutch();
    void deactivateClutch();
    void openQuickControls();
    void closeQuickControlsApply();
    void closeQuickControlsCancel();
    void navigateQuickControls(float delta);
    void setFocusedQuickControlValue(float normalized);
    void navigateProperty(float delta);
    void refreshEncoders();

private:
    void configureMacroEncoders();
    void configureValueEncoders();
    void configureDiscreteEncoders(uint8_t discreteSteps);
    void configureQuickControlEncoder();
    void resetQuickControlsState();
    void configureNormalizedEncoder(Config::EncoderID id);
    void configureDiscreteEncoder(Config::EncoderID id, uint8_t discreteSteps);
    int currentCcOffsetMin() const;
    int currentCcOffsetMax() const;
    float offsetToNormalized(int offset) const;
    int normalizedToOffset(float normalized) const;
    void initializeClutchChannelPreview();
    void commitClutchChannelPreview();

    core::state::macro::MacroUiState& macro_ui_;
    core::state::macro::MacroPagesState& pages_;
    core::state::TrackNavigationState& track_ui_;
    MacroPerformanceDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    uint8_t quick_snapshot_page_ = 0;
    std::array<core::state::macro::MacroConfig, Config::MACRO_COUNT> quick_snapshot_configs_{};
};

}  // namespace core::handler
