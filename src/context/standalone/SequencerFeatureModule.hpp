#pragma once

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/PatternPitchSettingsState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/strip/ContextActionStrip.hpp"

#if defined(MS_UX_RECORDER)
#include "context/standalone/ux/StandaloneUxSurfaces.hpp"
#include "validation/ux/SemanticUxTraceState.hpp"
#endif

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::context::standalone {
class PatternPitchSettingsOverlayPresenter;
class SequencerEncoderSyncCoordinator;
class SequencerOverlayPresenter;
}  // namespace core::context::standalone

namespace core::handler {
class PatternPitchSettingsHandler;
class SequencerMacroPropertyHandler;
class SequencerPatternQuickControlsHandler;
class SequencerPropertySelectorHandler;
class SequencerStepEditHandler;
class SequencerStepHandler;
}  // namespace core::handler

namespace core::context::standalone {

/**
 * Owns sequencer feature overlay, presenter, encoder sync, and input handlers.
 *
 * The module binds UI/input to SequencerState and shared-track services. It does
 * not schedule playback or send realtime MIDI; that remains in sequencer
 * runtime services.
 */
class SequencerFeatureModule {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        core::state::TrackNavigationState& trackNavigation;
        core::state::StructureClipboardState& structureClipboard;
        core::state::PatternPitchSettingsState& patternPitchSettings;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::handler::SequencerHistoryDomainServices history;
    };

    SequencerFeatureModule(StateRefs stateRefs,
                           core::handler::SharedTrackDomainServices sharedTracks,
                           oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                           oc::api::EncoderAPI& encoders,
                           oc::api::ButtonAPI& buttons,
                           lv_obj_t* overlayRoot,
                           lv_obj_t* sequencerViewScope
#if defined(MS_UX_RECORDER)
                           ,
                           core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
    );
    ~SequencerFeatureModule();

    SequencerFeatureModule(const SequencerFeatureModule&) = delete;
    SequencerFeatureModule& operator=(const SequencerFeatureModule&) = delete;

    void resetEncoderSync();
    void syncEncodersNow();

private:
#if defined(MS_UX_RECORDER)
    core::validation::ux::StructureUxTraceState structure_ux_trace_state_;
    core::context::standalone::ux::SequencerPropertySelectorUxSurface
        property_selector_ux_surface_;
    core::context::standalone::ux::SequencerQuickControlsUxSurface quick_controls_ux_surface_;
    core::context::standalone::ux::SequencerStructureUxSurface structure_ux_surface_;
    core::context::standalone::ux::SequencerStepEditUxSurface step_edit_ux_surface_;
    core::context::standalone::ux::SequencerStepGridUxSurface step_grid_ux_surface_;
#endif

    core::app::ExtmemUniquePtr<core::context::standalone::SequencerEncoderSyncCoordinator>
        encoder_sync_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> step_edit_overlay_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> step_edit_action_strip_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay>
        pattern_pitch_settings_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay>
        pattern_pitch_settings_selector_overlay_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerOverlayPresenter> presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::PatternPitchSettingsOverlayPresenter>
        pattern_pitch_settings_presenter_;
    core::app::ExtmemUniquePtr<core::handler::SequencerStepHandler> step_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerPatternQuickControlsHandler>
        quick_controls_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerStepEditHandler> step_edit_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerPropertySelectorHandler>
        property_selector_handler_;
    core::app::ExtmemUniquePtr<core::handler::PatternPitchSettingsHandler>
        pattern_pitch_settings_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerMacroPropertyHandler>
        macro_property_handler_;
};

}  // namespace core::context::standalone
