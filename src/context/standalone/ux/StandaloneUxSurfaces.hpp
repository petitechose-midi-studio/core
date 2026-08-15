#pragma once

#if defined(MS_UX_RECORDER)

#include <array>
#include <cstdint>

#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "state/contextual/ContextActionSpec.hpp"
#include "state/StructureNavigationState.hpp"
#include "validation/ux/SemanticUxSurface.hpp"

namespace core::state {
struct DeviceSettingsState;
struct MacroEditState;
struct MacroState;
struct MidiSyncState;
struct StatusBarState;
struct StructureClipboardState;
struct TrackNavigationState;
struct ViewSelectorState;
namespace macro {
class MacroHistoryService;
struct MacroPagesState;
struct MacroUiState;
}
namespace project {
class ProjectHistoryCoordinator;
struct ProjectNavigationState;
struct ProjectTrackEditorState;
struct ProjectTrackState;
}
namespace sequencer {
struct SequencerState;
struct SequencerPatternRandomizeSession;
class SequencerTrackActivationQueue;
struct SequencerTrackBankState;
}
}

namespace core::validation::ux {
struct StructureUxTraceState;
}

namespace core::sequencer {
class MidiCcGlobalFrameCoordinator;
}

namespace core::context::standalone::ux {

namespace priority {
constexpr uint8_t DEVICE_SETTINGS = 10;
constexpr uint8_t VIEW_SELECTOR = 15;
constexpr uint8_t TRANSPORT = 20;
constexpr uint8_t SEQUENCER_TRACK_EDIT = 21;
constexpr uint8_t SEQUENCER_PATTERN_EDIT = 22;
constexpr uint8_t SEQUENCER_PRESET_LIBRARY = 23;
constexpr uint8_t SEQUENCER_CC_LANE = 24;
constexpr uint8_t SEQUENCER_STEP_EDIT = 25;
constexpr uint8_t SEQUENCER_DRUM_LANE_EDIT = 26;
constexpr uint8_t SEQUENCER_PROPERTY_SELECTOR = 30;
constexpr uint8_t SEQUENCER_QUICK_CONTROLS = 35;
constexpr uint8_t SEQUENCER_STRUCTURE = 40;
constexpr uint8_t SEQUENCER_STEP_GRID = 50;
constexpr uint8_t PROJECT_NAVIGATION = 51;
constexpr uint8_t MACRO_STRUCTURE = 52;
constexpr uint8_t PROJECT_MODULATORS = 53;
constexpr uint8_t MACRO_PERFORMANCE = 54;
constexpr uint8_t MACRO_EDIT = 55;
constexpr uint8_t MACRO_VALUE = 60;
}  // namespace priority

class ViewSelectorUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    ViewSelectorUxSurface(oc::state::Signal<core::ui::ViewType, 8>& activeView,
                          core::state::ViewSelectorState& viewSelector,
                          core::state::project::ProjectHistoryCoordinator& history);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::ViewSelectorState& view_selector_;
    core::state::project::ProjectHistoryCoordinator& history_;
};

class DeviceSettingsUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    DeviceSettingsUxSurface(core::state::DeviceSettingsState& deviceSettings,
                            core::state::MidiSyncState& midiSync);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    core::state::DeviceSettingsState& device_settings_;
    core::state::MidiSyncState& midi_sync_;
};

class TransportUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    explicit TransportUxSurface(core::state::StatusBarState& statusBar);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    core::state::StatusBarState& status_bar_;
};

class SequencerPropertySelectorUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerPropertySelectorUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::sequencer::SequencerState& sequencer
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::sequencer::SequencerState& sequencer_;
    mutable bool context_selector_seen_ = false;
    mutable bool context_selector_rotated_ = false;
    mutable bool context_selector_held_ = false;
    mutable bool context_selector_release_cached_ = false;
    mutable core::state::StructureNavigationFocus context_selector_target_ =
        core::state::StructureNavigationFocus::PAGE;
};

class SequencerCcLaneUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerCcLaneUxSurface(
        core::state::sequencer::SequencerState& sequencer,
        core::state::sequencer::SequencerTrackBankState& tracks,
        const core::state::project::ProjectNavigationState& projectNavigation,
        const core::state::project::ProjectTrackState& projectTracks,
        const core::sequencer::MidiCcGlobalFrameCoordinator*
            midiCcCoordinator
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    const core::state::project::ProjectNavigationState& project_navigation_;
    const core::state::project::ProjectTrackState& project_tracks_;
    const core::sequencer::MidiCcGlobalFrameCoordinator*
        midi_cc_coordinator_ = nullptr;
    mutable std::array<
        core::state::contextual::ContextActionSpec,
        3>
        gesture_specs_{};
};

class SequencerPresetLibraryUxSurface final
    : public core::validation::ux::SemanticUxSurface {
public:
    explicit SequencerPresetLibraryUxSurface(
        core::state::sequencer::SequencerState& sequencer,
        const core::state::sequencer::SequencerTrackActivationQueue* trackActivations
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    core::state::sequencer::SequencerState& sequencer_;
    const core::state::sequencer::SequencerTrackActivationQueue*
        track_activations_ = nullptr;
};

class SequencerStepGridUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerStepGridUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::sequencer::SequencerState& sequencer,
        core::state::sequencer::SequencerTrackBankState& tracks
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
};

class SequencerStepEditUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerStepEditUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::sequencer::SequencerState& sequencer,
        core::state::sequencer::SequencerTrackBankState& tracks
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    mutable bool step_retarget_seen_ = false;
    mutable bool draft_trace_seen_ = false;
    mutable uint8_t draft_trace_kind_ = 0;
    mutable bool draft_trace_dirty_ = false;
    mutable uint8_t draft_trace_exit_choice_ = 0;
    mutable const char* draft_trace_action_ = nullptr;
};

class DrumLaneEditorUxSurface final
    : public core::validation::ux::SemanticUxSurface {
public:
    DrumLaneEditorUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        core::state::sequencer::SequencerState& sequencer
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::sequencer::SequencerState& sequencer_;
    mutable bool editor_seen_ = false;
    mutable bool observed_dirty_ = false;
    mutable bool observed_text_editing_ = false;
    mutable uint8_t observed_mode_ = 0U;
    mutable uint8_t observed_source_lane_ = 0U;
    mutable uint8_t observed_target_lane_ = 0U;
    mutable uint8_t observed_lane_count_ = 0U;
    mutable uint8_t observed_field_ = 0U;
    mutable uint8_t observed_text_key_ = 0U;
    mutable const char* terminal_effect_ = nullptr;
    mutable core::state::interaction::ControllerIntent terminal_intent_ =
        core::state::interaction::ControllerIntent::NONE;
};

class SequencerQuickControlsUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerQuickControlsUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        core::state::sequencer::SequencerState& sequencer
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::sequencer::SequencerState& sequencer_;
};

class SequencerPatternEditorUxSurface final
    : public core::validation::ux::SemanticUxSurface {
public:
    SequencerPatternEditorUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        core::state::sequencer::SequencerState& sequencer,
        core::state::sequencer::SequencerPatternRandomizeSession& randomize
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerPatternRandomizeSession& randomize_;
};

class ProjectTrackEditorUxSurface final
    : public core::validation::ux::SemanticUxSurface {
public:
    ProjectTrackEditorUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        core::state::project::ProjectTrackEditorState& editor,
        core::state::project::ProjectTrackState& tracks
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::project::ProjectTrackEditorState& editor_;
    core::state::project::ProjectTrackState& tracks_;
    mutable bool editor_state_seen_ = false;
    mutable bool observed_kind_dirty_ = false;
    mutable uint8_t observed_track_ = 0U;
    mutable uint8_t observed_property_ = 0U;
};

class SequencerStructureUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerStructureUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::StructureClipboardState& structureClipboard,
        core::state::sequencer::SequencerState& sequencer,
        core::state::sequencer::SequencerTrackBankState& tracks,
        const core::state::project::ProjectTrackState& projectTracks,
        const core::state::sequencer::SequencerTrackActivationQueue* trackActivations,
        const core::validation::ux::StructureUxTraceState* traceState
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::StructureClipboardState& structure_clipboard_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    const core::state::project::ProjectTrackState& project_tracks_;
    const core::state::sequencer::SequencerTrackActivationQueue* track_activations_ = nullptr;
    const core::validation::ux::StructureUxTraceState* trace_state_ = nullptr;
};

class MacroValueUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    MacroValueUxSurface(oc::state::Signal<core::ui::ViewType, 8>& activeView,
                        core::state::MacroState& macros,
                        core::state::macro::MacroPagesState& pages,
                        core::state::macro::MacroUiState& macroUi,
                        core::state::MacroEditState& macroEdit);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::MacroState& macros_;
    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::MacroEditState& macro_edit_;
};

class MacroPerformanceUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    MacroPerformanceUxSurface(oc::state::Signal<core::ui::ViewType, 8>& activeView,
                              core::state::macro::MacroUiState& macroUi,
                              core::state::MacroEditState& macroEdit);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::MacroEditState& macro_edit_;
};

class MacroStructureUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    MacroStructureUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::StructureClipboardState& structureClipboard,
        core::state::macro::MacroUiState& macroUi,
        core::state::macro::MacroPagesState& pages,
        core::state::MacroEditState& macroEdit,
        const core::validation::ux::StructureUxTraceState* traceState
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::StructureClipboardState& structure_clipboard_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::macro::MacroPagesState& pages_;
    core::state::MacroEditState& macro_edit_;
    const core::validation::ux::StructureUxTraceState* trace_state_ = nullptr;
};

class MacroEditUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    MacroEditUxSurface(oc::state::Signal<core::ui::ViewType, 8>& activeView,
                       core::state::MacroEditState& macroEdit,
                       core::state::macro::MacroPagesState& pages,
                       const core::state::project::ProjectTrackState& projectTracks,
                       core::state::macro::MacroUiState& macroUi,
                       oc::state::Signal<uint32_t>& configRevision,
                       core::state::StructureClipboardState& structureClipboard,
                       const core::sequencer::MidiCcGlobalFrameCoordinator*
                           midiCcCoordinator);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::MacroEditState& macro_edit_;
    core::state::macro::MacroPagesState& pages_;
    const core::state::project::ProjectTrackState& project_tracks_;
    core::state::macro::MacroUiState& macro_ui_;
    oc::state::Signal<uint32_t>& config_revision_;
    core::state::StructureClipboardState& structure_clipboard_;
    const core::sequencer::MidiCcGlobalFrameCoordinator*
        midi_cc_coordinator_ = nullptr;
    core::context::standalone::macro_overlay_presenter::StaticItems static_items_;
    mutable bool contextual_automation_record_seen_ = false;
    mutable bool contextual_recorded_shape_armed_ = false;
};

class ProjectNavigationUxSurface final
    : public core::validation::ux::SemanticUxSurface {
public:
    ProjectNavigationUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        core::state::project::ProjectNavigationState& navigation
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::project::ProjectNavigationState& navigation_;
};

class ProjectModulatorsUxSurface final
    : public core::validation::ux::SemanticUxSurface {
public:
    ProjectModulatorsUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        core::state::project::ProjectNavigationState& navigation,
        core::state::macro::MacroPagesState& pages,
        core::state::macro::MacroUiState& macroUi,
        core::state::StructureClipboardState& clipboard,
        core::state::macro::MacroHistoryService& history
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::project::ProjectNavigationState& navigation_;
    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::StructureClipboardState& clipboard_;
    core::state::macro::MacroHistoryService& history_;
    mutable bool recorded_shape_capture_button_seen_ = false;
};

}  // namespace core::context::standalone::ux

#endif
