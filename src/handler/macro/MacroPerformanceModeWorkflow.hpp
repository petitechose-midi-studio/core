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
    bool clutchActive() const;
    bool clutchInactive() const;

    void activateClutch();
    void deactivateClutch();
    void cancelClutch();
    void navigateProperty(float delta);
    void refreshEncoders();

private:
    void configureMacroEncoders();
    void configureValueEncoders();
    void configureDiscreteEncoders(uint8_t discreteSteps);
    void configureNormalizedEncoder(Config::EncoderID id);
    void configureDiscreteEncoder(Config::EncoderID id, uint8_t discreteSteps);

    core::state::macro::MacroUiState& macro_ui_;
    core::state::macro::MacroPagesState& pages_;
    core::state::TrackNavigationState& track_ui_;
    MacroPerformanceDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
};

}  // namespace core::handler
