#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "handler/project/ProjectLifecycleDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "state/MidiSyncState.hpp"
#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureClipboardState.hpp"

namespace core::handler {

class ProjectHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::project::ProjectNavigationState& navigation;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
        core::state::macro::MacroPagesState& pages;
        core::state::MacroState& macros;
        core::state::MacroEditState& macroEdit;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::macro::MacroHistoryService& macroHistory;
        core::state::StructureClipboardState& clipboard;
        SequencerHistoryDomainServices history;
        ProjectLifecycleDomainServices lifecycle;
    };

    ProjectHandler(StateRefs state,
                   SequencerSettingsDomainServices sequencerSettings,
                   oc::api::EncoderAPI& encoders,
                   oc::api::ButtonAPI& buttons,
                   oc::type::ScopeID projectViewScope,
                   uint32_t (*timeProvider)() = nullptr);

    ProjectHandler(const ProjectHandler&) = delete;
    ProjectHandler& operator=(const ProjectHandler&) = delete;

    void syncFocusedEncoder();
    void update(uint32_t nowMs);

private:
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
    bool setFocusedProjectValue(float normalized);
    bool setFocusedMusicRootValue(float normalized);
    bool setFocusedMusicScaleValue(float normalized);
    bool setFocusedTransportValue(float normalized);
    bool setFocusedStorageValue(float normalized);
    bool setFocusedRoutingValue(float normalized);
    bool setFocusedNameEditorValue(float normalized);
    bool setFocusedModulatorValue(float normalized);
    void enterFocusedModulator();
    void startDestinationPickerAudition();
    void applyDestinationPickerAudition();
    bool cancelDestinationPickerAudition();
    [[nodiscard]] bool destinationPickerAuditionAddress(
        core::state::macro::MacroAutomationSlotAddress& out
    ) const;
    void refreshModulatorPreview(
        bool syncMacroRuntime,
        uint8_t dirtyMacro = core::state::macro::kMacroConfigDirtyAll
    );
    void reconcileModulatorNavigationAfterHistory();
    void beginModulatorBottomLeft();
    void releaseModulatorBottomLeft();
    void beginModulatorBottomRight();
    void releaseModulatorBottomRight();
    void copyFocusedModulator();
    void pasteProjectModulatorSource();
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
    bool loadProjectWithFeedback(const char* projectId);
    bool saveCurrentAndLoadProjectWithFeedback(const char* projectId);
    bool saveAsAndLoadProjectWithFeedback(const char* projectId);
    bool saveAndResetProjectWithFeedback(bool saveAsNew);
    bool commitProjectNameEditor();
    void resetProject();
    void back();
    void consumeUndo();
    void consumeRedo();

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::project::ProjectNavigationState& navigation_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& sequencer_tracks_;
    core::state::StatusBarState& status_bar_;
    core::state::MidiSyncState& midi_sync_;
    core::state::macro::MacroPagesState& pages_;
    core::state::MacroState& macros_;
    core::state::MacroEditState& macro_edit_;
    oc::state::Signal<uint32_t>& config_revision_;
    core::state::macro::MacroHistoryService& macro_history_;
    core::state::StructureClipboardState& clipboard_;
    SequencerHistoryDomainServices history_;
    ProjectLifecycleDomainServices lifecycle_;
    SequencerSettingsDomainServices sequencer_settings_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID project_view_scope_ = 0;
    uint32_t (*time_provider_)() = nullptr;
    bool modulator_bottom_left_was_pressed_ = false;
    bool modulator_bottom_right_was_pressed_ = false;
};

}  // namespace core::handler
