#pragma once

#include <functional>

#include <oc/state/FixedSubscriptionList.hpp>
#include <oc/state/Signal.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

/**
 * Owns sequencer page/track navigation and contextual selection state.
 *
 * It previews Track add slots, switches page/track focus and enters
 * Track/Page/Step selection. Creation and destructive edits live in the
 * corresponding edit workflows.
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
        /** Borrowed facade; its owner must outlive this workflow. */
        std::reference_wrapper<const SharedTrackDomainServices> sharedTracks;
    };

    explicit SequencerStructureNavigationWorkflow(StateRefs state);

    SequencerStructureNavigationWorkflow(const SequencerStructureNavigationWorkflow&) = delete;
    SequencerStructureNavigationWorkflow& operator=(const SequencerStructureNavigationWorkflow&) =
        delete;

    bool allowsMainBindings() const;
    bool selectionActive() const;
    bool selectedItemsAvailable() const;
    bool stepFocusActive() const;

    void moveByFocus(float delta);
    void setNavigationFocus(core::state::StructureNavigationFocus focus);
    void enterSelectionModeForCurrentFocus();
    /** Handles one local Back tier; returns true when a selection owned it. */
    bool backSelectionMode();
    void toggleSelectionAtCursor();
    void toggleStepSelectionAtVisibleIndex(uint8_t indexInPage);
    void navigateSelection(float delta);

private:
    void bindStateSync();
    void syncTrackPreviewFromActive(uint8_t activeTrack);
    void movePage(float delta);
    void moveTrack(float delta);
    void moveStep(float delta);
    void setPagePreview(uint8_t pageIndex);
    void setTrackPreview(uint8_t trackIndex, bool addSlot);
    uint8_t maxStepCursor() const;
    uint8_t maxStepPage() const;
    bool stepSelectable(uint8_t step) const;
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
    const SharedTrackDomainServices& shared_tracks_;
    oc::state::FixedSubscriptionList<2> subscriptions_;
};

static_assert(
    sizeof(void*) != 4U ||
        sizeof(SequencerStructureNavigationWorkflow) == 48U,
    "Sequencer Structure navigation exceeds its ARM RAM contract"
);

}  // namespace core::handler
