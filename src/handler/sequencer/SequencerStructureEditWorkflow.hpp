#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerPreparedTrackStructureTransaction.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/StatusBarState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

enum class StepResetDepth : uint8_t;
enum class SequencerPreparedPageStructureResult : uint8_t;
class SequencerPreparedPageStructureTransaction;

/**
 * Owns sequencer page/track structure edit actions.
 *
 * It applies erase/remove/copy/paste/delete intent using snapshot ops,
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
        core::state::project::ProjectTrackState& projectTracks;
        core::state::project::ProjectTrackDomainServices projectTrackDomain;
        core::state::StructureClipboardState& structureClipboard;
        SharedTrackDomainServices sharedTracks;
        SequencerHistoryDomainServices history;
        core::state::macro::MacroPagesState& macroPages;
        core::state::sequencer::SequencerTrackActivationQueue* trackActivations = nullptr;
        core::state::StatusBarState* statusBar = nullptr;
    };

    explicit SequencerStructureEditWorkflow(StateRefs state);

    SequencerStructureEditWorkflow(const SequencerStructureEditWorkflow&) = delete;
    SequencerStructureEditWorkflow& operator=(const SequencerStructureEditWorkflow&) = delete;

    const SharedTrackDomainServices& sharedTrackServices() const noexcept {
        return shared_tracks_;
    }
    SequencerPreparedTrackStructureResult createPreviewedTrackStructure();

    bool canRemoveCurrentStructure() const;
    bool canPasteCurrentStructure() const;

    void update(uint32_t nowMs);
    void beginHoldAction(core::state::StructureHoldAction action);
    void beginSelectionHoldAction(core::state::StructureHoldAction action);
    void clearHoldAction();
    void settleConsumedBottomLeftRelease();
    core::state::contextual::GuardedActionRelease releaseTrackPasteAction(
        uint32_t nowMs
    );
    bool cancelTrackPasteAction(uint32_t nowMs);
    bool trackPasteNavigationBlocked() const;
    bool trackRemoveNavigationBlocked() const;
    bool trackRemoveHoldPending() const;
    bool currentTrackRemoveHoldPending() const;
    bool selectionTrackRemoveHoldPending() const;
    bool trackPastePlanInspectable() const;
    void toggleTrackPasteDetails();
    void applyLatchedCurrentTrackShortPress();
    void applyLatchedTrackSelectionShortPress();
    void applyLatchedTrackSelectionLongPress();
    void applyCurrentStructureShortPress();
    bool selectionHoldActionAvailable() const;
    void applySelectionBottomLeftTap();
    void applySelectionBottomLeftHold();
    bool canPasteStructureSelection() const;
    void copyStructureSelection();
    void pasteStructureSelection();
    void applyCurrentStructureLongPress();
    void copyCurrentStructure();
    void pasteCurrentStructure();
    void copyStepSelection();
    bool canPasteStepSelection() const;
    void resetStepSelectionShallow();
    void resetStepSelectionDeep();
    void beginStepPastePreview();
    void clearStepPastePreview();
    void pasteStepSelection();

private:
    enum class TrackHoldIntent : uint8_t {
        None = 0U,
        CurrentRemove,
        SelectionRemove,
    };

    struct TrackSelectionHoldToken {
        uint32_t clipboardRevision = 0U;
        uint16_t selectedMask = 0U;
        uint16_t enabledMask = 0U;
        uint16_t destinationMask = 0U;
        uint16_t overwriteMask = 0U;
        uint8_t cursor = 0U;
        uint8_t previewTrack =
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
        uint8_t flags = 0U;
    };

    static_assert(
        sizeof(TrackSelectionHoldToken) == 16U,
        "Track selection hold token must remain compact"
    );

    using HistoryTrackStructureChangePtr =
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr;

    HistoryTrackStructureChangePtr captureTrackHistoryBefore(uint16_t trackMask) const;
    bool recordTrackHistoryAfter(HistoryTrackStructureChangePtr change, uint16_t trackMask);
    void invalidateTrackRemoveHoldIntent();
    void clearTrackRemoveHoldIntent();
    bool trackRemoveHoldOwnsSharedState() const;
    bool currentTrackRemoveHoldStillMatches() const;
    bool currentTrackRemoveIntentMatches(uint8_t targetTrack) const;
    bool selectionTrackRemoveHoldStillMatches() const;
    bool selectionTrackRemoveIntentMatches(
        const TrackSelectionHoldToken& token,
        uint8_t targetTrack
    ) const;
    void settleRejectedSelectionTrackRemoveLongPress();
    void syncPreviewToFocus(core::state::StructureNavigationFocus focus);
    bool canPasteFocusedStep() const;
    void copyFocusedStep();
    void pasteFocusedStep();
    void resetFocusedStep(StepResetDepth depth);
    void resetStepSelection(StepResetDepth depth);
    void clearCurrentPageAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction
    );
    void deleteCurrentPageAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction
    );
    uint16_t pastePageSelectionAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction
    );
    uint16_t pasteCurrentPageAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction
    );
    void pasteStepClipboardAt(uint8_t cursorStep, bool selectionPaste);
    uint16_t pasteStepClipboardAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction
    );
    SequencerPreparedPageStructureResult resetFocusedStepAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction,
        StepResetDepth depth
    );
    SequencerPreparedPageStructureResult resetStepSelectionAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction,
        StepResetDepth depth
    );
    SequencerPreparedPageStructureResult resetPageSelectionAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction
    );
    SequencerPreparedPageStructureResult
    deleteOrResetPageSelectionAfterBoundary(
        SequencerPreparedPageStructureTransaction& transaction
    );
    uint16_t currentTrackEnabledMask() const;
    uint8_t currentActiveTrack() const;
    bool applyTrackState(uint16_t enabledMask, uint8_t activeTrack);
    uint8_t trackPasteTarget() const;
    core::state::ClipboardTransferPlan buildTrackPastePlan() const;
    bool beginTrackPasteAction(uint32_t nowMs);
    void refreshTrackPastePreview(uint32_t nowMs);
    void updateTrackPasteActivation(uint32_t nowMs);
    void refreshStructureSelectionPastePreview();
    void setTrackPasteFeedback(
        core::state::contextual::OperationFeedbackStatus status,
        core::state::contextual::ContextActionReason reason,
        core::state::contextual::OperationFeedbackExpiryPolicy expiry,
        uint32_t nowMs,
        uint32_t durationMs = 0
    );
    bool commitTrackPaste(uint32_t nowMs);

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_ui_;
    core::state::project::ProjectNavigationState& project_navigation_;
    core::state::project::ProjectTrackState& project_tracks_;
    core::state::project::ProjectTrackDomainServices project_track_domain_;
    core::state::StructureClipboardState& structure_clipboard_;
    SharedTrackDomainServices shared_tracks_;
    SequencerHistoryDomainServices history_;
    core::state::macro::MacroPagesState& macro_pages_;
    core::state::sequencer::SequencerTrackActivationQueue* track_activations_ = nullptr;
    core::state::StatusBarState* status_bar_ = nullptr;
    TrackHoldIntent track_hold_intent_ = TrackHoldIntent::None;
    uint8_t track_hold_target_ =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint32_t track_hold_acquisition_id_ = 0U;
    TrackSelectionHoldToken track_selection_hold_token_{};
};

static_assert(
    sizeof(void*) != 4U || sizeof(SequencerStructureEditWorkflow) == 116U,
    "Sequencer Structure edit workflow exceeds its ARM RAM contract"
);

}  // namespace core::handler
