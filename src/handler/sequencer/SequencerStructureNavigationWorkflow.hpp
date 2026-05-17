#pragma once

#include <vector>

#include <oc/state/Signal.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

/**
 * Owns sequencer page/track navigation and selection state.
 *
 * It previews add slots, switches page/track focus, enters selection mode, and
 * creates previewed structures. Destructive edits live in
 * SequencerStructureEditWorkflow.
 */
class SequencerStructureNavigationWorkflow {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::TrackNavigationState& trackNavigation;
        SharedTrackDomainServices sharedTracks;
    };

    explicit SequencerStructureNavigationWorkflow(StateRefs state);

    SequencerStructureNavigationWorkflow(const SequencerStructureNavigationWorkflow&) = delete;
    SequencerStructureNavigationWorkflow& operator=(const SequencerStructureNavigationWorkflow&) =
        delete;

    bool allowsMainBindings() const;
    bool selectionActive() const;
    bool previewingAddSlot() const;

    void moveByFocus(float delta);
    void cycleNavigationFocus();
    void enterSelectionModeForCurrentFocus();
    void cancelSelectionMode();
    void toggleSelectionAtCursor();
    void navigateSelection(float delta);
    void createPreviewedStructure();

private:
    void bindStateSync();
    void movePage(float delta);
    void moveTrack(float delta);
    void setPagePreview(uint8_t pageIndex, bool addSlot);
    void setTrackPreview(uint8_t trackIndex, bool addSlot);
    uint8_t cursorForSelectionScope(core::state::StructureSelectionScope scope) const;
    void syncPreviewToFocus(core::state::StructureNavigationFocus focus);
    uint16_t currentTrackEnabledMask() const;
    uint8_t currentActiveTrack() const;
    bool applyTrackState(uint16_t enabledMask, uint8_t activeTrack);

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_ui_;
    SharedTrackDomainServices shared_tracks_;
    std::vector<oc::state::Subscription> subscriptions_;
};

}  // namespace core::handler
