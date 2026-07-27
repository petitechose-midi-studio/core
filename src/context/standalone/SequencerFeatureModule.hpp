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
#include "handler/sequencer/SequencerStepPresetDomainServices.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectTrackEditorState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/PatternPitchSettingsState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/StatusBarState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerPatternRandomizeSession.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
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

namespace core::ui {
class SequencerPatternEditorOverlay;
class SequencerStepEditOverlay;
}

namespace core::ui::project {
class ProjectTrackEditorOverlay;
}

namespace core::context::standalone {
class PatternPitchSettingsOverlayPresenter;
class OverlayPresentationRegistry;
class ProjectTrackEditorPresenter;
class SequencerCcLaneOverlayPresenter;
class SequencerEncoderSyncCoordinator;
class SequencerOverlayPresenter;
class SequencerPatternEditorPresenter;
}  // namespace core::context::standalone

namespace core::handler {
class MidiCcGlobalFrameCoordinator;
class PatternPitchSettingsHandler;
class ProjectTrackEditorHandler;
class SequencerCcLaneHandler;
class SequencerCcLaneWorkflow;
class SequencerMacroPropertyHandler;
class SequencerPatternQuickControlsHandler;
class SequencerPatternEditorHandler;
class SequencerPropertySelectorHandler;
class SequencerStepEditHandler;
class SequencerStepContentHandler;
class SequencerStepHandler;
}  // namespace core::handler

namespace core::state::macro {
struct MacroPagesState;
}

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
        core::state::project::ProjectNavigationState& projectNavigation;
        core::state::project::ProjectTrackEditorState& projectTrackEditor;
        core::state::project::ProjectTrackState& projectTracks;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
        core::state::StructureClipboardState& structureClipboard;
        core::state::PatternPitchSettingsState& patternPitchSettings;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::handler::SequencerHistoryDomainServices history;
        core::state::sequencer::SequencerTrackActivationQueue* trackActivations = nullptr;
        core::state::StatusBarState* statusBar = nullptr;
        core::state::macro::MacroPagesState* macroPages = nullptr;
        const core::handler::MidiCcGlobalFrameCoordinator* midiCcCoordinator = nullptr;
    };

    SequencerFeatureModule(StateRefs stateRefs,
                           core::handler::SharedTrackDomainServices sharedTracks,
                           core::state::project::ProjectTrackDomainServices trackDomain,
                           core::handler::SequencerStepPresetDomainServices stepPresets,
                           oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                           OverlayPresentationRegistry& overlayPresentations,
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

    [[nodiscard]] bool valid() const { return valid_; }
    void update(uint32_t nowMs);
    void resetEncoderSync();
    void syncEncodersNow();
    core::handler::ProjectTrackEditorHandler* trackEditorHandler() const {
        return track_editor_handler_.get();
    }

private:
#if defined(MS_UX_RECORDER)
    core::app::ExtmemUniquePtr<
        core::context::standalone::ux::SequencerPatternEditorUxSurface>
        pattern_editor_ux_surface_;
    core::context::standalone::ux::ProjectTrackEditorUxSurface
        track_editor_ux_surface_;
    core::validation::ux::StructureUxTraceState structure_ux_trace_state_;
    core::context::standalone::ux::SequencerPropertySelectorUxSurface
        property_selector_ux_surface_;
    core::context::standalone::ux::SequencerStepPresetUxSurface
        step_preset_ux_surface_;
    core::context::standalone::ux::SequencerCcLaneUxSurface cc_lane_ux_surface_;
    core::context::standalone::ux::SequencerQuickControlsUxSurface quick_controls_ux_surface_;
    core::context::standalone::ux::SequencerStructureUxSurface structure_ux_surface_;
    core::context::standalone::ux::SequencerStepEditUxSurface step_edit_ux_surface_;
    core::context::standalone::ux::SequencerStepGridUxSurface step_grid_ux_surface_;
#endif

    core::app::ExtmemUniquePtr<core::context::standalone::SequencerEncoderSyncCoordinator>
        encoder_sync_;
    core::app::ExtmemUniquePtr<
        core::state::sequencer::SequencerPatternRandomizeSession>
        pattern_randomize_session_;
    core::app::ExtmemUniquePtr<core::ui::SequencerPatternEditorOverlay>
        pattern_editor_overlay_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip>
        pattern_editor_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::project::ProjectTrackEditorOverlay>
        track_editor_overlay_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip>
        track_editor_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::SequencerStepEditOverlay> step_edit_overlay_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> step_edit_action_strip_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay>
        step_preset_overlay_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip>
        step_preset_action_strip_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay>
        cc_lane_overlay_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip>
        cc_lane_action_strip_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay>
        pattern_pitch_settings_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay>
        pattern_pitch_settings_selector_overlay_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerOverlayPresenter> presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerPatternEditorPresenter>
        pattern_editor_presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::ProjectTrackEditorPresenter>
        track_editor_presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerCcLaneOverlayPresenter>
        cc_lane_presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::PatternPitchSettingsOverlayPresenter>
        pattern_pitch_settings_presenter_;
    core::app::ExtmemUniquePtr<core::handler::SequencerStepHandler> step_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerPatternQuickControlsHandler>
        quick_controls_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerPatternEditorHandler>
        pattern_editor_handler_;
    core::app::ExtmemUniquePtr<core::handler::ProjectTrackEditorHandler>
        track_editor_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerStepEditHandler> step_edit_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerStepContentHandler>
        step_content_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerPropertySelectorHandler>
        property_selector_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerCcLaneWorkflow>
        cc_lane_workflow_;
    core::app::ExtmemUniquePtr<core::handler::SequencerCcLaneHandler>
        cc_lane_handler_;
    core::app::ExtmemUniquePtr<core::handler::PatternPitchSettingsHandler>
        pattern_pitch_settings_handler_;
    core::app::ExtmemUniquePtr<core::handler::SequencerMacroPropertyHandler>
        macro_property_handler_;
    bool valid_ = false;
};

}  // namespace core::context::standalone
