#pragma once

#include <oc/state/Signal.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

/**
 * Owns sequencer page/track structure edit actions.
 *
 * It applies erase/remove/copy/paste/delete/duplicate intent using snapshot ops,
 * track-bank ops, shared track services, and the shared structure clipboard.
 */
class SequencerStructureEditWorkflow {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::TrackNavigationState& trackNavigation;
        core::state::project::ProjectNavigationState& projectNavigation;
        core::state::StructureClipboardState& structureClipboard;
        SharedTrackDomainServices sharedTracks;
        SequencerHistoryDomainServices history;
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
    void copyStepSelection();
    void clearStepSelection();
    void beginStepPastePreview();
    void clearStepPastePreview();
    void pasteStepSelection();
    void deleteSelection();
    void duplicateSelection();

private:
    using HistoryPatternSnapshot =
        core::state::sequencer::SequencerHistoryPatternSnapshot;
    using HistoryTrackStructureChangePtr =
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr;

    bool capturePageHistoryBefore(HistoryPatternSnapshot& before) const;
    void recordPageHistoryAfter(HistoryPatternSnapshot before);
    HistoryTrackStructureChangePtr captureTrackHistoryBefore(uint16_t trackMask) const;
    void recordTrackHistoryAfter(HistoryTrackStructureChangePtr change, uint16_t trackMask);
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
    core::state::project::ProjectNavigationState& project_navigation_;
    core::state::StructureClipboardState& structure_clipboard_;
    SharedTrackDomainServices shared_tracks_;
    SequencerHistoryDomainServices history_;
};

}  // namespace core::handler
