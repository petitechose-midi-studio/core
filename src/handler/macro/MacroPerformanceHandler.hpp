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
#include "handler/common/PressHoldTurnReleaseGesture.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroInteractionPolicy.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "app/OverlayTypes.hpp"
#include "config/TimeCompat.hpp"

namespace core::validation::ux {
struct StructureUxTraceState;
}

namespace core::handler {

class MacroEditHandler;
class ProjectTrackEditorHandler;

/**
 * Binds macro performance buttons/encoders to macro workflows.
 *
 * This handler owns physical input predicates for slot property selection and
 * structure editing. State transitions and domain mutations stay in the
 * workflow/service classes it composes.
 */
class MacroPerformanceHandler {
public:
    using TimeProviderFn = uint32_t (*)();

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
                            oc::type::ScopeID scopeId,
                            TimeProviderFn timeProvider = core::time_compat::millis
#if defined(MS_UX_RECORDER)
                            ,
                            core::validation::ux::StructureUxTraceState* uxTraceState = nullptr
#endif
    );

    ~MacroPerformanceHandler() = default;

    MacroPerformanceHandler(const MacroPerformanceHandler&) = delete;
    MacroPerformanceHandler& operator=(const MacroPerformanceHandler&) = delete;

    void update(uint32_t nowMs);
    void attachEditors(MacroEditHandler& macroEditor,
                       ProjectTrackEditorHandler& trackEditor);

private:
    void setupBindings();
    core::state::macro::MacroInteractionContext interactionContext() const;
    bool policyAllows(core::state::macro::MacroInteractionAction action) const;
    void beginContextSelector();
    void moveContextSelector(float delta);
    void releaseContextSelector();
    void beginMacroButtonGesture(uint8_t index);
    void releaseMacroButtonGesture(uint8_t index);

    core::state::macro::MacroUiState& macro_ui_;
    MacroStructureWorkflow structure_workflow_;
    MacroPerformanceDomainServices performance_services_;
    MacroPerformanceModeWorkflow performance_workflow_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    TimeProviderFn time_provider_ = core::time_compat::millis;
    PressHoldTurnReleaseGesture context_selector_gesture_{};
    MacroEditHandler* macro_editor_ = nullptr;
    ProjectTrackEditorHandler* track_editor_ = nullptr;
    bool ignore_next_bottom_left_release_ = false;
    bool ignore_next_bottom_right_release_ = false;
    bool paste_only_press_active_ = false;
    uint16_t owned_macro_button_mask_ = 0U;
    uint16_t edit_chord_macro_mask_ = 0U;
#if defined(MS_UX_RECORDER)
    core::validation::ux::StructureUxTraceState* ux_trace_state_ = nullptr;
#endif
};

}  // namespace core::handler
