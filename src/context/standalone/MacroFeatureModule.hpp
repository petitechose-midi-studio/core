#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "handler/macro/MacroEditDomainServices.hpp"
#include "handler/macro/MacroAutomationPlaybackService.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "handler/macro/MacroStructureDomainServices.hpp"
#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/StatusBarState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

#if defined(MS_UX_RECORDER)
#include "context/standalone/ux/StandaloneUxSurfaces.hpp"
#include "validation/ux/SemanticUxTraceState.hpp"
#endif

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::context::standalone {

class MacroOverlayPresenter;
class OverlayPresentationRegistry;

}  // namespace core::context::standalone

namespace core::ui {
class ContextActionStrip;
class MacroEditorOverlay;
}

namespace core::handler {
class MacroAutomationHandler;
class MacroEditHandler;
class MacroMidiHandler;
class MacroPerformanceHandler;
class MacroValueHandler;
}  // namespace core::handler

namespace core::context::standalone {

/**
 * Owns macro feature overlays, presenters, MIDI bridge, and input handlers.
 *
 * Domain mutations are delegated to macro domain services/workflows passed in
 * by the assembly; this module is the LVGL/input binding owner for macro mode.
 */
class MacroFeatureModule {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::project::ProjectNavigationState& projectNavigation;
        core::state::MacroState& macros;
        core::state::MacroEditState& macroEdit;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        core::state::StructureClipboardState& structureClipboard;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::StatusBarState& statusBar;
        const oc::state::Signal<uint32_t>* runtimeOwnerRevision = nullptr;
        core::handler::MidiCcGlobalFrameCoordinator* midiCcCoordinator = nullptr;
    };

    MacroFeatureModule(StateRefs stateRefs,
                       core::handler::MacroEditDomainServices editServices,
                       core::handler::MacroPerformanceDomainServices performanceServices,
                       core::handler::MacroStructureDomainServices structureServices,
                       oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                       OverlayPresentationRegistry& overlayPresentations,
                       oc::api::EncoderAPI& encoders,
                       oc::api::ButtonAPI& buttons,
                       lv_obj_t* mainZone,
                       lv_obj_t* macroViewScope
#if defined(MS_UX_RECORDER)
                       ,
                       core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
    );
    ~MacroFeatureModule();

    MacroFeatureModule(const MacroFeatureModule&) = delete;
    MacroFeatureModule& operator=(const MacroFeatureModule&) = delete;

    [[nodiscard]] bool valid() const { return valid_; }
    void onCC(uint8_t channel, uint8_t cc, uint8_t value);
    void onNoteIn();
    void update(uint32_t nowMs);

private:
#if defined(MS_UX_RECORDER)
    core::validation::ux::StructureUxTraceState structure_ux_trace_state_;
    core::context::standalone::ux::MacroEditUxSurface macro_edit_ux_surface_;
    core::context::standalone::ux::MacroStructureUxSurface macro_structure_ux_surface_;
    core::context::standalone::ux::MacroPerformanceUxSurface macro_performance_ux_surface_;
    core::context::standalone::ux::MacroValueUxSurface macro_value_ux_surface_;
#endif

    core::app::ExtmemUniquePtr<core::ui::MacroEditorOverlay> edit_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> automation_overlay_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> edit_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip>
        automation_action_strip_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay> edit_selector_overlay_;
    core::app::ExtmemUniquePtr<core::context::standalone::MacroOverlayPresenter> presenter_;
    core::app::ExtmemUniquePtr<core::handler::MacroMidiCcRuntimeAdapter>
        macro_midi_runtime_;
    std::unique_ptr<core::handler::MacroValueHandler> value_handler_;
    std::unique_ptr<core::handler::MacroMidiHandler> midi_handler_;
    std::unique_ptr<core::handler::MacroAutomationPlaybackService> automation_playback_;
    core::app::ExtmemUniquePtr<core::handler::MacroPerformanceHandler> performance_handler_;
    core::app::ExtmemUniquePtr<core::handler::MacroEditHandler> edit_handler_;
    core::app::ExtmemUniquePtr<core::handler::MacroAutomationHandler> automation_handler_;
    uint32_t last_telemetry_refresh_ms_ = 0;
    bool valid_ = false;
};

}  // namespace core::context::standalone
