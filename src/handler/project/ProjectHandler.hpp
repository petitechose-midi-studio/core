#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/project/ProjectLifecycleDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "state/MidiSyncState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/StatusBarState.hpp"

namespace core::handler {

class ProjectHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::project::ProjectNavigationState& navigation;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
        SequencerHistoryDomainServices history;
        ProjectLifecycleDomainServices lifecycle;
    };

    ProjectHandler(StateRefs state,
                   SequencerSettingsDomainServices sequencerSettings,
                   oc::api::EncoderAPI& encoders,
                   oc::api::ButtonAPI& buttons,
                   oc::type::ScopeID projectViewScope);

    ProjectHandler(const ProjectHandler&) = delete;
    ProjectHandler& operator=(const ProjectHandler&) = delete;

    void syncFocusedEncoder();

private:
    void setupBindings();
    bool canHandleProjectInput() const;
    bool projectConfirmationActive() const;
    bool physicalHoldActive() const;
    bool regularProjectInputActive() const;
    void enterPhysicalHoldLayer();
    void leavePhysicalHoldLayer();
    void navigate(float delta);
    void switchTab(float delta);
    void enterFocused();
    void setFocusedValue(float normalized);
    bool applyFocusedProjectStep(int steps);
    bool applyFocusedMusicScaleStep(int steps);
    bool applyFocusedTransportStep(int steps);
    bool applyFocusedStorageStep(int steps);
    bool applyFocusedRoutingStep(int steps);
    bool setFocusedProjectValue(float normalized);
    bool setFocusedMusicScaleValue(float normalized);
    bool setFocusedTransportValue(float normalized);
    bool setFocusedStorageValue(float normalized);
    bool setFocusedRoutingValue(float normalized);
    bool activateFocusedProjectAction();
    bool loadProjectWithFeedback(const char* projectId);
    bool saveCurrentAndLoadProjectWithFeedback(const char* projectId);
    bool saveAsAndLoadProjectWithFeedback(const char* projectId);
    void resetProject();
    void back();
    void consumeUndo();
    void consumeRedo();

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::project::ProjectNavigationState& navigation_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& sequencer_tracks_;
    core::state::StatusBarState& status_bar_;
    core::state::MidiSyncState& midi_sync_;
    SequencerHistoryDomainServices history_;
    ProjectLifecycleDomainServices lifecycle_;
    SequencerSettingsDomainServices sequencer_settings_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID project_view_scope_ = 0;
};

}  // namespace core::handler
