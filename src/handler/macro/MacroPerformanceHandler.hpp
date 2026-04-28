#pragma once

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "handler/macro/MacroPerformanceModeWorkflow.hpp"
#include "handler/macro/MacroStructureDomainServices.hpp"
#include "handler/macro/MacroStructureWorkflow.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

/**
 * Binds macro performance buttons/encoders to macro workflows.
 *
 * This handler owns physical input predicates for clutch, quick controls, and
 * structure editing. State transitions and domain mutations stay in the
 * workflow/service classes it composes.
 */
class MacroPerformanceHandler {
public:
    struct StateRefs {
        core::state::macro::MacroUiState& macroUi;
        core::state::macro::MacroPagesState& pages;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::StructureClipboardState& structureClipboard;
    };

    MacroPerformanceHandler(StateRefs state,
                            MacroPerformanceDomainServices performanceServices,
                            MacroStructureDomainServices structureServices,
                            oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                            oc::api::EncoderAPI& encoders,
                            oc::api::ButtonAPI& buttons,
                            oc::type::ScopeID scopeId);

    ~MacroPerformanceHandler() = default;

    MacroPerformanceHandler(const MacroPerformanceHandler&) = delete;
    MacroPerformanceHandler& operator=(const MacroPerformanceHandler&) = delete;

private:
    void setupBindings();
    bool selectionActive() const;

    MacroStructureWorkflow structure_workflow_;
    MacroPerformanceModeWorkflow performance_workflow_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    bool nav_long_press_used_ = false;
    bool left_center_held_ = false;
    bool left_bottom_held_ = false;
    bool ignore_next_bottom_left_release_ = false;
    bool ignore_next_bottom_right_release_ = false;
};

}  // namespace core::handler
