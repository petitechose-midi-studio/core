#pragma once

#include <oc/state/Signal.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

class SequencerStructureEditWorkflow {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::TrackNavigationState& trackNavigation;
        core::state::StructureClipboardState& structureClipboard;
        SharedTrackDomainServices sharedTracks;
    };

    explicit SequencerStructureEditWorkflow(StateRefs state);

    SequencerStructureEditWorkflow(const SequencerStructureEditWorkflow&) = delete;
    SequencerStructureEditWorkflow& operator=(const SequencerStructureEditWorkflow&) = delete;

    bool canRemoveCurrentStructure() const;
    bool canPasteCurrentStructure() const;

    void beginHoldAction(core::state::StructureHoldAction action);
    void clearHoldAction();
    void eraseCurrentStructure();
    void removeCurrentStructure();
    void copyCurrentStructure();
    void pasteCurrentStructure();
    void deleteSelection();
    void duplicateSelection();

private:
    bool createPage();
    bool createTrack();
    void syncPreviewToFocus(core::state::StructureNavigationFocus focus);
    void cancelSelectionMode();
    uint16_t currentTrackEnabledMask() const;
    uint8_t currentActiveTrack() const;
    bool applyTrackState(uint16_t enabledMask, uint8_t activeTrack);

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_ui_;
    core::state::StructureClipboardState& structure_clipboard_;
    SharedTrackDomainServices shared_tracks_;
};

}  // namespace core::handler
