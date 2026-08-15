#pragma once

#include <array>

#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::handler {

/**
 * Owns macro performance modal state transitions.
 *
 * It gates Macro Slot property selection, configures encoder behavior for each
 * mode, and delegates durable macro changes through MacroPerformanceDomainServices.
 */
class MacroPerformanceModeWorkflow {
public:
    struct StateRefs {
        core::state::macro::MacroUiState& macroUi;
        core::state::TrackNavigationState& trackNavigation;
    };

    MacroPerformanceModeWorkflow(StateRefs state,
                                 MacroPerformanceDomainServices services,
                                 oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                 oc::api::EncoderAPI& encoders);

    MacroPerformanceModeWorkflow(const MacroPerformanceModeWorkflow&) = delete;
    MacroPerformanceModeWorkflow& operator=(const MacroPerformanceModeWorkflow&) = delete;

    bool performanceOverlayActive() const;

    void openEditPrompt();
    void closePerformanceOverlay();
    void navigateTakeTiming(float delta);
    void refreshEncoders();

private:
    void configureMacroEncoders();
    void configureValueEncoders();
    void configureNormalizedEncoder(Config::EncoderID id);

    core::state::macro::MacroUiState& macro_ui_;
    core::state::TrackNavigationState& track_ui_;
    MacroPerformanceDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
};

}  // namespace core::handler
