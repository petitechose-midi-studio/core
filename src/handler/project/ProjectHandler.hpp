#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "handler/common/ProjectRecordedShapeCaptureWorkflow.hpp"
#include "handler/project/ProjectLifecycleDomainServices.hpp"
#include "handler/macro/MacroEditDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "state/MidiSyncState.hpp"
#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectSettingsHistory.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureClipboardState.hpp"

namespace core::handler {

class ProjectHandler {
public:
    static constexpr uint32_t ROUTING_GESTURE_IDLE_COMMIT_MS = 250U;

    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::project::ProjectNavigationState& navigation;
        core::state::project::ProjectTrackState& projectTracks;
        core::state::project::ProjectTrackDomainServices trackDomain;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        core::state::MacroState& macros;
        core::state::MacroEditState& macroEdit;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::macro::MacroHistoryService& macroHistory;
        core::state::project::ProjectSettingsHistoryService& settingsHistory;
        core::state::StructureClipboardState& clipboard;
        SequencerHistoryDomainServices history;
        ProjectLifecycleDomainServices lifecycle;
    };

    ProjectHandler(StateRefs state,
                   SequencerSettingsDomainServices sequencerSettings,
                   MacroEditDomainServices macroEditServices,
                   oc::api::EncoderAPI& encoders,
                   oc::api::ButtonAPI& buttons,
                   oc::type::ScopeID projectViewScope,
                   uint32_t (*timeProvider)() = nullptr);

    ProjectHandler(const ProjectHandler&) = delete;
    ProjectHandler& operator=(const ProjectHandler&) = delete;

    void syncFocusedEncoder();
    void update(uint32_t nowMs);

private:
    enum class PendingProjectCatalogAction : uint8_t {
        NONE = 0,
        LOAD_PICKER,
        SAVE_CURRENT,
        SAVE_RESET_AS_NEW,
        SAVE_AS_AND_LOAD,
    };

    void setupBindings();
    bool canHandleProjectInput() const;
    bool projectConfirmationActive() const;
    bool physicalHoldActive() const;
    bool regularProjectInputActive() const;
    void enterPhysicalHoldLayer();
    void leavePhysicalHoldLayer();
    void enterProjectNameShift();
    void leaveProjectNameShift();
    void navigate(float delta);
    void switchTab(float delta);
    void enterFocused();
    void setFocusedValue(float normalized);
    bool applyFocusedProjectStep(int steps);
    bool applyFocusedMusicRootStep(int steps);
    bool applyFocusedMusicScaleStep(int steps);
    bool applyFocusedTransportStep(int steps);
    bool applyFocusedStorageStep(int steps);
    bool applyFocusedRoutingStep(int steps);
    bool applyFocusedNameEditorStep(int steps);
    bool recordProjectSettingsChange(
        const core::state::project::ProjectSettingsHistorySnapshot& before,
        core::state::project::ProjectSettingsHistoryActionKind kind,
        uint8_t subject,
        bool coalesce
    );
    void endProjectSettingsGesture();
    bool setFocusedProjectValue(float normalized);
    bool setFocusedMusicRootValue(float normalized);
    bool setFocusedMusicScaleValue(float normalized);
    bool setFocusedTransportValue(float normalized);
    bool setFocusedStorageValue(float normalized);
    bool setFocusedRoutingValue(float normalized);
    [[nodiscard]] bool setRoutingMidiChannel(
        uint8_t track,
        uint8_t channel0Based
    );
    void commitPendingRoutingGesture();
    void cancelPendingRoutingGesture();
    bool setFocusedNameEditorValue(float normalized);
    bool setFocusedModulatorValue(float normalized);
    void enterFocusedModulator();
    void openFocusedModulationDestination();
    void startDestinationPickerAudition();
    void applyDestinationPickerAudition();
    bool cancelDestinationPickerAudition();
    [[nodiscard]] bool modulatorAuditionAddress(
        core::state::macro::MacroAutomationSlotAddress& out
    ) const;
    void refreshModulatorPreview(
        bool syncMacroRuntime,
        uint8_t dirtyMacro = core::state::macro::kMacroConfigDirtyAll
    );
    void beginModulatorBottomLeft();
    void releaseModulatorBottomLeft();
    [[nodiscard]] bool focusedRecordedShapeRecord() const;
    [[nodiscard]] bool beginRecordedShapeCapture();
    void releaseRecordedShapeCapture();
    [[nodiscard]] bool cancelRecordedShapeCapture(const char* feedback = nullptr);
    void syncRecordedShapeCaptureRevision();
    [[nodiscard]] bool createDefaultRecordedShape();
    [[nodiscard]] bool resizeFocusedRecordedShape(uint8_t beats);
    static void markRecordedShapeMutation(void* context);
    static void publishRecordedShapeAudition(
        void* context,
        const core::state::modulation::ProjectRecordedShapeAuditionDescriptor&
            descriptor
    );
    static void clearRecordedShapeAudition(void* context);
    void beginModulatorBottomRight();
    void releaseModulatorBottomRight();
    void copyFocusedModulator();
    void pasteProjectModulatorSource();
    void makeFocusedModulatorIndependent();
    void toggleFocusedModulator();
    void deleteGuardedModulator();
    void publishModulatorMutation(bool markAuthored = true);
    [[nodiscard]] core::state::modulation::ModulationBindingState*
        focusedModulationBinding();
    [[nodiscard]] const core::state::modulation::ModulationBindingState*
        focusedModulationBinding() const;
    [[nodiscard]] core::state::modulation::ModulatorSourceState*
        focusedModulator();
    [[nodiscard]] const core::state::modulation::ModulatorSourceState*
        focusedModulator() const;
    [[nodiscard]] uint16_t focusedModulatorDetailRowCount() const;
    bool activateFocusedProjectAction();
    bool requestProjectLoadPicker();
    bool saveCurrentProjectWithFeedback();
    void beginPendingProjectCatalog(PendingProjectCatalogAction action);
    void pollPendingProjectCatalog();
    bool loadProjectWithFeedback(const char* projectId);
    bool saveCurrentAndLoadProjectWithFeedback(const char* projectId);
    bool saveAsAndLoadProjectWithFeedback(const char* projectId);
    bool saveAndResetProjectWithFeedback(bool saveAsNew);
    bool commitProjectNameEditor();
    void resetProject();
    void back();
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::project::ProjectNavigationState& navigation_;
    core::state::project::ProjectTrackState& project_tracks_;
    core::state::project::ProjectTrackDomainServices track_domain_;
    core::state::StatusBarState& status_bar_;
    core::state::MidiSyncState& midi_sync_;
    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::MacroState& macros_;
    core::state::MacroEditState& macro_edit_;
    oc::state::Signal<uint32_t>& config_revision_;
    core::state::macro::MacroHistoryService& macro_history_;
    core::state::project::ProjectSettingsHistoryService& settings_history_;
    ProjectRecordedShapeCaptureWorkflow recorded_shape_capture_;
    core::state::StructureClipboardState& clipboard_;
    SequencerHistoryDomainServices history_;
    ProjectLifecycleDomainServices lifecycle_;
    SequencerSettingsDomainServices sequencer_settings_;
    MacroEditDomainServices macro_edit_services_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID project_view_scope_ = 0;
    uint32_t (*time_provider_)() = nullptr;
    uint32_t routing_gesture_commit_deadline_ms_ = 0U;
    uint32_t settings_gesture_commit_deadline_ms_ = 0U;
    uint8_t routing_gesture_track_ =
        core::state::project::PROJECT_TRACK_COUNT;
    core::state::project::ProjectNodeId pending_project_catalog_node_ =
        core::state::project::ProjectNodeId::OVERVIEW_ROOT;
    uint8_t pending_project_catalog_row_ = 0U;
    bool modulator_bottom_left_was_pressed_ = false;
    bool modulator_bottom_right_was_pressed_ = false;
    bool recorded_shape_capture_button_active_ = false;
    PendingProjectCatalogAction pending_project_catalog_action_ =
        PendingProjectCatalogAction::NONE;
};

}  // namespace core::handler
