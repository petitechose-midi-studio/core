#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <config/Timing.hpp>
#include <utility>

#include "handler/sequencer/SequencerDirectTrackStructureTransaction.hpp"
#include "handler/sequencer/SequencerPreparedPageStructureMutationPlan.hpp"
#include "handler/sequencer/SequencerPreparedPageStructureTransaction.hpp"
#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"
#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "handler/sequencer/SequencerStructureSelectionOps.hpp"
#include "handler/sequencer/SequencerStructureStepOps.hpp"
#include "handler/sequencer/SequencerStructureStepPasteWorkflow.hpp"
#include "handler/sequencer/SequencerStructureTrackOps.hpp"
#include "handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackTransferAction.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;
namespace contextual = core::state::contextual;

namespace {

constexpr uint32_t TRACK_PASTE_CANCELLED_MS = 700;
constexpr uint32_t TRACK_PASTE_APPLIED_MS = 1200;

constexpr uint8_t TRACK_SELECTION_HOLD_SCOPE_TRACK = 1U << 0U;
constexpr uint8_t TRACK_SELECTION_HOLD_PLACING = 1U << 1U;
constexpr uint8_t TRACK_SELECTION_HOLD_PASTE_BLOCKED = 1U << 2U;
constexpr uint8_t TRACK_SELECTION_HOLD_PREVIEW_ADD = 1U << 3U;

constexpr uint8_t packTrackSelectionHoldFlags(
    core::state::StructureSelectionScope scope,
    bool placing,
    bool pasteBlocked,
    bool previewAddTrack
) noexcept {
    return static_cast<uint8_t>(
        (scope == core::state::StructureSelectionScope::TRACK
             ? TRACK_SELECTION_HOLD_SCOPE_TRACK
             : 0U) |
        (placing ? TRACK_SELECTION_HOLD_PLACING : 0U) |
        (pasteBlocked ? TRACK_SELECTION_HOLD_PASTE_BLOCKED : 0U) |
        (previewAddTrack ? TRACK_SELECTION_HOLD_PREVIEW_ADD : 0U)
    );
}

enum class PreparedStructureSettlement : uint8_t {
    Failed = 0U,
    NoChange,
    Committed,
};

constexpr uint16_t packPreparedStructureSettlement(
    PreparedStructureSettlement outcome,
    uint8_t finalFocus = 0U
) noexcept {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(outcome) << 8U) | finalFocus);
}

constexpr PreparedStructureSettlement preparedStructureSettlementOutcome(
    uint16_t settlement
) noexcept {
    return static_cast<PreparedStructureSettlement>(settlement >> 8U);
}

constexpr uint8_t preparedStructureSettlementFocus(
    uint16_t settlement
) noexcept {
    return static_cast<uint8_t>(settlement & 0xFFU);
}

}  // namespace

FLASHMEM SequencerStructureEditWorkflow::SequencerStructureEditWorkflow(StateRefs state)
    : sequencer_(state.sequencer), tracks_(state.tracks), navigation_focus_(state.navigationFocus),
      track_ui_(state.trackNavigation), project_navigation_(state.projectNavigation),
      project_tracks_(state.projectTracks), project_track_domain_(state.projectTrackDomain),
      structure_clipboard_(state.structureClipboard), shared_tracks_(state.sharedTracks),
      history_(state.history), macro_pages_(state.macroPages),
      track_activations_(state.trackActivations), status_bar_(state.statusBar) {}

FLASHMEM SequencerPreparedTrackStructureResult
SequencerStructureEditWorkflow::createPreviewedTrackStructure() {
    using Status = SequencerPreparedTrackStructureStatus;
    if (track_activations_ == nullptr) {
        return {Status::HistoryUnavailable, {}};
    }
    return executeSequencerCreateTrackStructure({
        tracks_,
        sequencer_,
        navigation_focus_,
        track_ui_,
        structure_clipboard_,
        macro_pages_,
        *track_activations_,
        shared_tracks_,
        history_,
    });
}

FLASHMEM bool SequencerStructureEditWorkflow::canRemoveCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return false;
        return structure_slots::countEnabled(
                   currentTrackEnabledMask(),
                   core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) > 1U;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        return sequencer_.focusedStep.get() <
               core::state::sequencer::activeContentLength(sequencer_);
    }
    return sequencer_.activePageCount() > 1U;
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return buildTrackPastePlan().canCommit();
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        return canPasteFocusedStep();
    }
    return structure_clipboard_.hasSequencerPage();
}

FLASHMEM void SequencerStructureEditWorkflow::beginHoldAction(
    core::state::StructureHoldAction action) {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (action == core::state::StructureHoldAction::PASTE) {
            beginTrackPasteAction(core::time_compat::millis());
            return;
        }
        if (action != core::state::StructureHoldAction::REMOVE ||
            track_ui_.selection.active.get()) {
            return;
        }
        // The physical hold owns an immutable local target. Shared preview
        // state is presentation only: history restore and other global paths
        // may legitimately rewrite it before the long-press callback fires.
        track_hold_intent_ = TrackHoldIntent::CurrentRemove;
        track_hold_target_ = currentActiveTrack();
        track_selection_hold_token_ = {};
        track_ui_.syncPreviewTrack(track_hold_target_);
        track_ui_.hold.begin(action, core::time_compat::millis());
        track_hold_acquisition_id_ = track_ui_.hold.acquisitionId();
        return;
    }
    sequencer_.structureUi.pageHold.begin(action, core::time_compat::millis());
}

FLASHMEM void SequencerStructureEditWorkflow::beginSelectionHoldAction(
    core::state::StructureHoldAction action
) {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (action != core::state::StructureHoldAction::REMOVE ||
            !track_ui_.selection.active.get() ||
            track_ui_.selection.scope.get() !=
                core::state::StructureSelectionScope::TRACK ||
            track_ui_.previewAddSlot.get()) {
            return;
        }
        track_hold_intent_ = TrackHoldIntent::SelectionRemove;
        track_hold_target_ = currentActiveTrack();
        track_selection_hold_token_ = {
            .clipboardRevision = track_ui_.selection.clipboardRevision.get(),
            .selectedMask = track_ui_.selection.selectedMask.get(),
            .enabledMask = currentTrackEnabledMask(),
            .destinationMask = track_ui_.selection.destinationMask.get(),
            .overwriteMask = track_ui_.selection.overwriteMask.get(),
            .cursor = track_ui_.selection.cursorIndex.get(),
            .previewTrack = track_ui_.previewTrackIndex.get(),
            .flags = packTrackSelectionHoldFlags(
                track_ui_.selection.scope.get(),
                track_ui_.selection.placing.get(),
                track_ui_.selection.pasteBlocked.get(),
                track_ui_.previewAddSlot.get()
            ),
        };
        track_ui_.hold.begin(action, core::time_compat::millis());
        track_hold_acquisition_id_ = track_ui_.hold.acquisitionId();
        return;
    }
    sequencer_.structureUi.pageHold.begin(action, core::time_compat::millis());
}

FLASHMEM void SequencerStructureEditWorkflow::clearHoldAction() {
    clearTrackRemoveHoldIntent();
    sequencer_.structureUi.pageHold.clear();
}

FLASHMEM void SequencerStructureEditWorkflow::invalidateTrackRemoveHoldIntent() {
    track_hold_intent_ = TrackHoldIntent::None;
    track_hold_target_ =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    track_hold_acquisition_id_ = 0U;
    track_selection_hold_token_ = {};
}

FLASHMEM void SequencerStructureEditWorkflow::clearTrackRemoveHoldIntent() {
    track_ui_.hold.clear();
    invalidateTrackRemoveHoldIntent();
}

FLASHMEM bool
SequencerStructureEditWorkflow::trackRemoveHoldOwnsSharedState() const {
    return trackRemoveHoldPending() &&
           track_ui_.hold.action.get() ==
               core::state::StructureHoldAction::REMOVE &&
           track_ui_.hold.acquisitionId() == track_hold_acquisition_id_;
}

FLASHMEM void
SequencerStructureEditWorkflow::settleConsumedBottomLeftRelease() {
    if (!trackRemoveHoldPending()) {
        clearHoldAction();
        return;
    }
    if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
    invalidateTrackRemoveHoldIntent();
}

FLASHMEM uint8_t SequencerStructureEditWorkflow::trackPasteTarget() const {
    if (track_ui_.selection.placementActive()) { return track_ui_.selection.cursorIndex.get(); }
    return sequencerStructureTrackTarget(track_ui_, currentActiveTrack());
}

FLASHMEM core::state::ClipboardTransferPlan SequencerStructureEditWorkflow::buildTrackPastePlan()
    const {
    return core::state::buildSequencerTrackClipboardTransferPlan(
        structure_clipboard_, tracks_, project_tracks_, trackPasteTarget(),
        track_activations_ != nullptr ? track_activations_->pendingTrackMask() : 0);
}

FLASHMEM void SequencerStructureEditWorkflow::setTrackPasteFeedback(
    contextual::OperationFeedbackStatus status, contextual::ContextActionReason reason,
    contextual::OperationFeedbackExpiryPolicy expiry, uint32_t nowMs, uint32_t durationMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    contextual::setOperationFeedback(paste.feedback, contextual::ContextActionId::PASTE,
                                     {
                                         .kind = contextual::ContextEntityKind::TRACK,
                                         .track = paste.plan.entries[0].sourceTrack,
                                         .item = paste.plan.sourceMask,
                                     },
                                     {
                                         .kind = contextual::ContextEntityKind::TRACK,
                                         .track = paste.plan.entries[0].targetTrack,
                                         .item = paste.plan.targetMask,
                                     },
                                     status, reason, expiry, nowMs, durationMs);
}

FLASHMEM bool SequencerStructureEditWorkflow::beginTrackPasteAction(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    const auto plan = buildTrackPastePlan();
    if (!plan.canCommit()) return false;

    uint32_t nextGeneration = paste.interactionGeneration + 1U;
    if (nextGeneration == 0) ++nextGeneration;
    paste.guard = {};
    paste.feedback = {};
    paste.plan = plan;
    paste.clipboardKind = structure_clipboard_.kind.get();
    paste.clipboardRevision = structure_clipboard_.revision.get();
    paste.interactionGeneration = nextGeneration;
    paste.operationGeneration = 0;
    paste.activationGeneration = 0;
    paste.detailVisible = false;
    paste.buttonOwned = true;
    paste.commitConsumed = false;
    if (!contextual::beginGuardedActionPress(
            paste.guard, nowMs,
            static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS))) {
        paste.buttonOwned = false;
        return false;
    }
    setTrackPasteFeedback(contextual::OperationFeedbackStatus::PRESSED,
                          core::state::sequencer::contextualReasonForTrackTransfer(plan.reason),
                          contextual::OperationFeedbackExpiryPolicy::MANUAL, nowMs);
    paste.bump();
    return true;
}

FLASHMEM void SequencerStructureEditWorkflow::refreshTrackPastePreview(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (paste.feedback.status == contextual::OperationFeedbackStatus::QUEUED ||
        paste.feedback.status == contextual::OperationFeedbackStatus::APPLIED) {
        return;
    }

    // A terminal transient is user feedback, not a new preflight. Preserve it
    // until its own expiry policy clears it; the following update can then
    // restore PREVIEW from the live clipboard/target pair.
    if (!paste.buttonOwned && paste.feedback.active &&
        (paste.feedback.status == contextual::OperationFeedbackStatus::CANCELLED ||
         paste.feedback.status == contextual::OperationFeedbackStatus::BLOCKED ||
         paste.feedback.status == contextual::OperationFeedbackStatus::FAILED ||
         paste.feedback.status == contextual::OperationFeedbackStatus::CONFLICT)) {
        return;
    }

    if (paste.buttonOwned || paste.gestureActive()) {
        const auto live = core::state::buildSequencerTrackClipboardTransferPlan(
            structure_clipboard_, tracks_, project_tracks_, paste.plan.entries[0].targetTrack,
            track_activations_ != nullptr ? track_activations_->pendingTrackMask() : 0);
        if (paste.clipboardKind != structure_clipboard_.kind.get() ||
            paste.clipboardRevision != structure_clipboard_.revision.get() || !live.canCommit() ||
            !core::state::sameSequencerTrackClipboardTransferIdentity(paste.plan, live)) {
            if (contextual::cancelGuardedAction(paste.guard)) {
                paste.detailVisible = false;
                setTrackPasteFeedback(contextual::OperationFeedbackStatus::BLOCKED,
                                      contextual::ContextActionReason::STALE_TARGET,
                                      contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                                      nowMs, TRACK_PASTE_CANCELLED_MS);
                paste.bump();
            }
            return;
        }
        if (!core::state::sameSequencerTrackClipboardTransferPlan(paste.plan, live)) {
            paste.plan = live;
            paste.bump();
        }
        return;
    }

    const bool trackContext =
        navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK;
    const auto live = trackContext ? buildTrackPastePlan() : core::state::ClipboardTransferPlan{};
    if (!trackContext || !live.canCommit()) {
        const bool changed =
            paste.plan.hasEntries() || paste.detailVisible || paste.feedback.active;
        paste.plan = {};
        paste.clipboardKind = core::state::StructureClipboardKind::NONE;
        paste.clipboardRevision = 0;
        paste.detailVisible = false;
        contextual::clearOperationFeedback(paste.feedback);
        if (changed) paste.bump();
        return;
    }

    const bool planChanged =
        !core::state::sameSequencerTrackClipboardTransferPlan(paste.plan, live);
    const bool feedbackChanged =
        paste.feedback.status != contextual::OperationFeedbackStatus::PREVIEW;
    if (!planChanged && !feedbackChanged) return;
    paste.plan = live;
    paste.clipboardKind = structure_clipboard_.kind.get();
    paste.clipboardRevision = structure_clipboard_.revision.get();
    setTrackPasteFeedback(contextual::OperationFeedbackStatus::PREVIEW,
                          core::state::sequencer::contextualReasonForTrackTransfer(live.reason),
                          contextual::OperationFeedbackExpiryPolicy::MANUAL, nowMs);
    paste.bump();
}

FLASHMEM void SequencerStructureEditWorkflow::updateTrackPasteActivation(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (track_activations_ == nullptr || paste.activationGeneration == 0 ||
        paste.feedback.status != contextual::OperationFeedbackStatus::QUEUED ||
        !paste.plan.hasEntries()) {
        return;
    }

    const auto telemetry = track_activations_->telemetry(paste.plan.entries[0].targetTrack);
    if (telemetry.generation != paste.activationGeneration ||
        telemetry.origin != core::state::sequencer::SequencerTrackActivationOrigin::TRACK_PASTE) {
        return;
    }

    if (telemetry.status == core::state::sequencer::SequencerTrackActivationStatus::CANCELLED) {
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::CANCELLED,
                              contextual::ContextActionReason::FAILED,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_CANCELLED_MS);
        paste.bump();
    } else if (telemetry.status ==
               core::state::sequencer::SequencerTrackActivationStatus::APPLIED) {
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::APPLIED,
                              contextual::ContextActionReason::NONE,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_APPLIED_MS);
        paste.bump();
    }
}

FLASHMEM bool SequencerStructureEditWorkflow::commitTrackPaste(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (paste.commitConsumed || !paste.plan.canCommit()) return false;
    if (history_.commitCoalescedPatternEditOutcome() ==
        core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return false;
    }
    paste.commitConsumed = true;

    const auto result = executeSequencerTrackTransfer(
        tracks_, project_tracks_, sequencer_, structure_clipboard_, shared_tracks_, history_,
        paste.plan.entries[0].targetTrack, 0, track_activations_,
        status_bar_ != nullptr && status_bar_->playing.get(), &macro_pages_);
    paste.plan = result.plan;
    paste.activationGeneration = result.activationGeneration;
    paste.operationGeneration =
        result.operationId != 0 ? result.operationId : paste.interactionGeneration;

    if (!result.applied()) {
        auto reason = core::state::sequencer::contextualReasonForTrackTransfer(result.plan.reason);
        if (reason == contextual::ContextActionReason::NONE) {
            switch (result.status) {
                case SequencerTrackTransferStatus::STALE:
                    reason = contextual::ContextActionReason::STALE_TARGET;
                    break;
                case SequencerTrackTransferStatus::NO_CHANGE:
                    reason = contextual::ContextActionReason::NO_ACTION;
                    break;
                case SequencerTrackTransferStatus::HISTORY_UNAVAILABLE:
                    reason = contextual::ContextActionReason::HISTORY_UNAVAILABLE;
                    break;
                case SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE:
                    reason = contextual::ContextActionReason::ALLOCATION_UNAVAILABLE;
                    break;
                default: reason = contextual::ContextActionReason::FAILED; break;
            }
        }
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::BLOCKED, reason,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_CANCELLED_MS);
        paste.bump();
        return false;
    }

    setTrackPasteFeedback(result.activationGeneration != 0
                              ? contextual::OperationFeedbackStatus::QUEUED
                              : contextual::OperationFeedbackStatus::APPLIED,
                          contextual::ContextActionReason::NONE,
                          result.activationGeneration != 0
                              ? contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED
                              : contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                          nowMs, result.activationGeneration != 0 ? 0 : TRACK_PASTE_APPLIED_MS);
    syncPreviewToFocus(core::state::StructureNavigationFocus::TRACK);
    paste.bump();
    return true;
}

FLASHMEM void SequencerStructureEditWorkflow::update(uint32_t nowMs) {
    refreshStructureSelectionPastePreview();
    auto& paste = sequencer_.structureUi.trackPaste;
    updateTrackPasteActivation(nowMs);

    if (contextual::updateOperationFeedback(paste.feedback, nowMs)) { paste.bump(); }

    if (!paste.buttonOwned) {
        refreshTrackPastePreview(nowMs);
        return;
    }

    refreshTrackPastePreview(nowMs);
    if (paste.guard.phase == contextual::GuardedActionPhase::PRESSED &&
        (nowMs - paste.guard.pressedAtMs) >= Config::Timing::LATCH_THRESHOLD_MS) {
        // Guard progress is anchored to the physical press so COMMITTED occurs
        // at the absolute long-press threshold, not one threshold later.
        if (contextual::armGuardedAction(paste.guard, paste.guard.pressedAtMs)) {
            setTrackPasteFeedback(
                contextual::OperationFeedbackStatus::ARMED,
                core::state::sequencer::contextualReasonForTrackTransfer(paste.plan.reason),
                contextual::OperationFeedbackExpiryPolicy::MANUAL, nowMs);
            paste.bump();
        }
    }
    if (paste.guard.phase == contextual::GuardedActionPhase::ARMED &&
        contextual::updateGuardedAction(paste.guard, nowMs)) {
        if (paste.guard.phase == contextual::GuardedActionPhase::COMMITTED) {
            commitTrackPaste(nowMs);
        } else {
            paste.bump();
        }
    }
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteStructureSelection() const {
    if (track_ui_.selection.placementActive()) { return buildTrackPastePlan().canCommit(); }
    const auto& pageSelection = sequencer_.structureUi.pageSelection;
    if (!pageSelection.placementActive()) return false;
    return buildPageSelectionPastePlan(sequencer_, structure_clipboard_,
                                       pageSelection.cursorIndex.get())
        .canCommit();
}

FLASHMEM void SequencerStructureEditWorkflow::refreshStructureSelectionPastePreview() {
    auto refresh = [](core::state::StructureSelectionState& selection, uint16_t destinationMask,
                      uint16_t overwriteMask, bool blocked, uint32_t clipboardRevision) {
        selection.destinationMask.set(destinationMask);
        selection.overwriteMask.set(overwriteMask);
        selection.pasteBlocked.set(blocked);
        selection.clipboardRevision.set(clipboardRevision);
    };

    auto& trackSelection = track_ui_.selection;
    if (trackSelection.placementActive()) {
        const auto plan = buildTrackPastePlan();
        refresh(trackSelection, plan.targetMask, plan.overwriteMask, !plan.canCommit(),
                structure_clipboard_.revision.get());
    } else {
        refresh(trackSelection, 0U, 0U, false, 0U);
    }

    auto& pageSelection = sequencer_.structureUi.pageSelection;
    if (pageSelection.placementActive()) {
        const auto plan = buildPageSelectionPastePlan(sequencer_, structure_clipboard_,
                                                      pageSelection.cursorIndex.get());
        refresh(pageSelection, plan.destinationMask, plan.overwriteMask, !plan.canCommit(),
                structure_clipboard_.revision.get());
    } else {
        refresh(pageSelection, 0U, 0U, false, 0U);
    }
}

FLASHMEM void SequencerStructureEditWorkflow::copyStructureSelection() {
    if (track_ui_.selection.active.get()) {
        auto& selection = track_ui_.selection;
        if (selection.placing.get()) return;
        auto clipboard = captureTrackSelectionClipboard(tracks_, sequencer_, macro_pages_,
                                                        selection.selectedMask.get());
        if (!clipboard ||
            !structure_clipboard_.storeSequencerTrackSelection(std::move(clipboard))) {
            return;
        }
        selection.placing.set(true);
        selection.clipboardRevision.set(structure_clipboard_.revision.get());
        refreshStructureSelectionPastePreview();
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.active.get() || selection.placing.get()) return;
    core::state::SequencerPageSelectionClipboard clipboard;
    if (!capturePageSelectionClipboard(sequencer_, selection.selectedMask.get(), clipboard) ||
        !structure_clipboard_.storeSequencerPageSelection(
            clipboard, core::state::sequencer::graphView(sequencer_.pattern))) {
        return;
    }
    selection.placing.set(true);
    selection.clipboardRevision.set(structure_clipboard_.revision.get());
    refreshStructureSelectionPastePreview();
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStructureSelection() {
    const auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.placementActive()) return;
    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::PageSelectionPaste;
    SequencerPreparedPageStructureTransaction transaction(sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    const uint16_t settlement = pastePageSelectionAfterBoundary(transaction);
    switch (preparedStructureSettlementOutcome(settlement)) {
        case PreparedStructureSettlement::Committed:
            sequencer_.structureUi.pageHold.clear();
            sequencer_.structureUi.syncPreviewPage(sequencer_.page.get());
            refreshStructureSelectionPastePreview();
            return;
        case PreparedStructureSettlement::NoChange: {
            const uint8_t finalFocus =
                preparedStructureSettlementFocus(settlement);
            sequencer_.page.set(sequencer_.pageForStep(finalFocus));
            sequencer_.focusedStep.set(finalFocus);
            sequencer_.structureUi.pageHold.clear();
            sequencer_.structureUi.syncPreviewPage(sequencer_.page.get());
            refreshStructureSelectionPastePreview();
            return;
        }
        case PreparedStructureSettlement::Failed:
        default:
            return;
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM uint16_t SequencerStructureEditWorkflow::pastePageSelectionAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPageSelectionPasteMutationPlan(
        sequencer_, structure_clipboard_,
        makeSequencerPreparedPageStructureTarget(
            currentActiveTrack(),
            sequencer_.structureUi.pageSelection.cursorIndex.get()),
        plan)) {
        case Preflight::Rejected:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
        case Preflight::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Preflight::Ready:
            break;
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Committed);
        case Result::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Result::Failed:
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }
}

FLASHMEM contextual::GuardedActionRelease SequencerStructureEditWorkflow::releaseTrackPasteAction(
    uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    // Copy remains the unconditional tap action, even when no compatible
    // clipboard exists and therefore no guarded paste press was acquired.
    if (!paste.buttonOwned) return contextual::GuardedActionRelease::TAP;

    update(nowMs);
    const auto phase = paste.guard.phase;
    auto release = contextual::releaseGuardedAction(paste.guard, nowMs);
    if (phase == contextual::GuardedActionPhase::CANCELLED) {
        release = contextual::GuardedActionRelease::CANCELLED;
    }
    paste.buttonOwned = false;

    if (release == contextual::GuardedActionRelease::TAP) {
        paste.detailVisible = false;
        paste.plan = {};
        contextual::clearOperationFeedback(paste.feedback);
    } else if (release == contextual::GuardedActionRelease::CANCELLED) {
        paste.detailVisible = false;
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::CANCELLED,
                              contextual::ContextActionReason::NONE,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_CANCELLED_MS);
    }
    paste.bump();
    return release;
}

FLASHMEM bool SequencerStructureEditWorkflow::cancelTrackPasteAction(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    bool changed = false;
    if (paste.detailVisible) {
        paste.detailVisible = false;
        changed = true;
    }
    if (contextual::cancelGuardedAction(paste.guard)) {
        setTrackPasteFeedback(contextual::OperationFeedbackStatus::CANCELLED,
                              contextual::ContextActionReason::NONE,
                              contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION, nowMs,
                              TRACK_PASTE_CANCELLED_MS);
        changed = true;
    }
    if (changed) paste.bump();
    return changed;
}

FLASHMEM bool SequencerStructureEditWorkflow::trackPasteNavigationBlocked() const {
    const auto& paste = sequencer_.structureUi.trackPaste;
    return paste.buttonOwned || paste.gestureActive() || paste.detailVisible;
}

FLASHMEM bool SequencerStructureEditWorkflow::trackRemoveNavigationBlocked() const {
    return trackRemoveHoldPending();
}

FLASHMEM bool SequencerStructureEditWorkflow::trackRemoveHoldPending() const {
    return track_hold_intent_ != TrackHoldIntent::None;
}

FLASHMEM bool SequencerStructureEditWorkflow::currentTrackRemoveHoldPending() const {
    return track_hold_intent_ == TrackHoldIntent::CurrentRemove;
}

FLASHMEM bool
SequencerStructureEditWorkflow::currentTrackRemoveHoldStillMatches() const {
    return currentTrackRemoveHoldPending() &&
           trackRemoveHoldOwnsSharedState() &&
           currentTrackRemoveIntentMatches(track_hold_target_);
}

FLASHMEM bool SequencerStructureEditWorkflow::currentTrackRemoveIntentMatches(
    uint8_t targetTrack
) const {
    return navigation_focus_.get() ==
               core::state::StructureNavigationFocus::TRACK &&
           !track_ui_.selection.active.get() &&
           !track_ui_.previewAddSlot.get() &&
           track_ui_.previewTrackIndex.get() == targetTrack &&
           currentActiveTrack() == targetTrack;
}

FLASHMEM bool SequencerStructureEditWorkflow::selectionTrackRemoveHoldPending() const {
    return track_hold_intent_ == TrackHoldIntent::SelectionRemove;
}

FLASHMEM bool
SequencerStructureEditWorkflow::selectionTrackRemoveHoldStillMatches() const {
    return selectionTrackRemoveHoldPending() &&
           trackRemoveHoldOwnsSharedState() &&
           selectionTrackRemoveIntentMatches(
               track_selection_hold_token_,
               track_hold_target_
           );
}

FLASHMEM bool SequencerStructureEditWorkflow::selectionTrackRemoveIntentMatches(
    const TrackSelectionHoldToken& token,
    uint8_t targetTrack
) const {
    return navigation_focus_.get() ==
               core::state::StructureNavigationFocus::TRACK &&
           track_ui_.selection.active.get() &&
           track_ui_.selection.clipboardRevision.get() ==
               token.clipboardRevision &&
           track_ui_.selection.selectedMask.get() ==
               token.selectedMask &&
           track_ui_.selection.destinationMask.get() ==
               token.destinationMask &&
           track_ui_.selection.overwriteMask.get() ==
               token.overwriteMask &&
           track_ui_.selection.cursorIndex.get() ==
               token.cursor &&
           track_ui_.previewTrackIndex.get() == token.previewTrack &&
           packTrackSelectionHoldFlags(
               track_ui_.selection.scope.get(),
               track_ui_.selection.placing.get(),
               track_ui_.selection.pasteBlocked.get(),
               track_ui_.previewAddSlot.get()
           ) == token.flags &&
           currentTrackEnabledMask() ==
               token.enabledMask &&
           currentActiveTrack() == targetTrack;
}

FLASHMEM void
SequencerStructureEditWorkflow::settleRejectedSelectionTrackRemoveLongPress() {
    if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
}

FLASHMEM bool SequencerStructureEditWorkflow::trackPastePlanInspectable() const {
    const auto& paste = sequencer_.structureUi.trackPaste;
    return paste.inspectable() && paste.plan.canCommit() && paste.feedback.active;
}

FLASHMEM void SequencerStructureEditWorkflow::toggleTrackPasteDetails() {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (!trackPastePlanInspectable()) return;
    paste.detailVisible = !paste.detailVisible;
    paste.bump();
}

FLASHMEM void
SequencerStructureEditWorkflow::applyLatchedCurrentTrackShortPress() {
    if (!currentTrackRemoveHoldStillMatches()) {
        if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
        invalidateTrackRemoveHoldIntent();
        return;
    }

    const uint8_t targetTrack = track_hold_target_;
    clearTrackRemoveHoldIntent();
    if (history_.commitCoalescedPatternEditOutcome() ==
        core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return;
    }
    if (track_ui_.hold.active() ||
        !currentTrackRemoveIntentMatches(targetTrack)) {
        return;
    }
    (void)toggleSequencerStructureTrackMute(
        project_tracks_,
        project_track_domain_,
        targetTrack
    );
}

FLASHMEM void
SequencerStructureEditWorkflow::applyLatchedTrackSelectionShortPress() {
    if (!selectionTrackRemoveHoldStillMatches()) {
        if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
        invalidateTrackRemoveHoldIntent();
        return;
    }

    const auto token = track_selection_hold_token_;
    const uint8_t targetTrack = track_hold_target_;
    clearTrackRemoveHoldIntent();
    if (history_.commitCoalescedPatternEditOutcome() ==
        core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return;
    }
    if (track_ui_.hold.active() ||
        !selectionTrackRemoveIntentMatches(token, targetTrack)) {
        return;
    }
    applySelectionBottomLeftTap();
}

FLASHMEM void
SequencerStructureEditWorkflow::applyLatchedTrackSelectionLongPress() {
    if (!selectionTrackRemoveHoldStillMatches() ||
        !selectionHoldActionAvailable()) {
        settleRejectedSelectionTrackRemoveLongPress();
        return;
    }

    const auto token = track_selection_hold_token_;
    const uint8_t targetTrack = track_hold_target_;
    if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
    if (history_.commitCoalescedPatternEditOutcome() ==
        core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return;
    }
    if (track_ui_.hold.active() ||
        !selectionTrackRemoveIntentMatches(token, targetTrack)) {
        return;
    }
    applySelectionBottomLeftHold();
}

FLASHMEM void SequencerStructureEditWorkflow::applyCurrentStructureShortPress() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const uint8_t activeTrack = currentActiveTrack();
        (void)toggleSequencerStructureTrackMute(project_tracks_, project_track_domain_,
                                                activeTrack);
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        resetFocusedStep(StepResetDepth::Shallow);
        return;
    }

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::PageClear;
    SequencerPreparedPageStructureTransaction transaction(sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    clearCurrentPageAfterBoundary(transaction);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM void SequencerStructureEditWorkflow::clearCurrentPageAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPageClearMutationPlan(
        sequencer_, currentActiveTrack(), sequencer_.visiblePage(), plan)) {
        case Preflight::Rejected:
            return;
        case Preflight::NoChange:
            sequencer_.page.set(sequencer_.pageForStep(plan.finalFocus));
            sequencer_.focusedStep.set(plan.finalFocus);
            sequencer_.structureUi.pageHold.clear();
            return;
        case Preflight::Ready:
            break;
        default:
            return;
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            sequencer_.structureUi.pageHold.clear();
            return;
        case Result::NoChange:
            sequencer_.page.set(sequencer_.pageForStep(plan.finalFocus));
            sequencer_.focusedStep.set(plan.finalFocus);
            sequencer_.structureUi.pageHold.clear();
            return;
        case Result::Failed:
        default:
            return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::applyCurrentStructureLongPress() {
    if (trackRemoveHoldPending()) {
        const bool holdStillMatches = currentTrackRemoveHoldStillMatches();
        const uint8_t latchedTarget = track_hold_target_;
        if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();
        if (!holdStillMatches || track_activations_ == nullptr) {
            return;
        }
        const auto result = executeSequencerRemoveCurrentTrackStructure({
            tracks_,
            sequencer_,
            navigation_focus_,
            track_ui_,
            structure_clipboard_,
            macro_pages_,
            *track_activations_,
            shared_tracks_,
            history_,
        }, latchedTarget);
        if (!result.settled()) return;
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        track_ui_.hold.clear();
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        resetFocusedStep(StepResetDepth::Deep);
        return;
    }

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::PageDelete;
    SequencerPreparedPageStructureTransaction transaction(sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    deleteCurrentPageAfterBoundary(transaction);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM void SequencerStructureEditWorkflow::deleteCurrentPageAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPageDeleteMutationPlan(
        sequencer_, currentActiveTrack(), sequencer_.visiblePage(), plan)) {
        case Preflight::Rejected:
        case Preflight::NoChange:
            return;
        case Preflight::Ready:
            break;
        default:
            return;
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            sequencer_.structureUi.pageHold.clear();
            syncSequencerPagePreviewToVisible(sequencer_, false);
            return;
        case Result::NoChange:
        case Result::Failed:
        default:
            return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::copyCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        core::state::sequencer::SequencerPatternSnapshot snapshot;
        core::state::sequencer::captureSnapshot(sequencer_.pattern, snapshot);
        if (!structure_clipboard_.storeSequencerTrack(
                snapshot, core::state::sequencer::graphView(sequencer_.pattern),
                currentActiveTrack(),
                core::state::sequencer::sequencerCcLaneView(sequencer_.pattern))) {
            return;
        }
        return;
    }

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        copyFocusedStep();
        return;
    }

    core::state::SequencerPageClipboard clipboard;
    const uint8_t page = sequencer_.visiblePage();
    if (!capturePageClipboard(sequencer_, page, clipboard)) return;
    if (!structure_clipboard_.storeSequencerPage(
            clipboard, core::state::sequencer::graphView(sequencer_.pattern))) {
        return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::pasteCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        pasteFocusedStep();
        return;
    }

    if (navigation_focus_.get() != core::state::StructureNavigationFocus::PAGE) return;

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::PagePaste;
    SequencerPreparedPageStructureTransaction transaction(sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    const uint16_t settlement = pasteCurrentPageAfterBoundary(transaction);
    switch (preparedStructureSettlementOutcome(settlement)) {
        case PreparedStructureSettlement::Committed:
            sequencer_.structureUi.pageHold.clear();
            syncSequencerPagePreviewToVisible(sequencer_, false);
            return;
        case PreparedStructureSettlement::NoChange: {
            const uint8_t finalFocus =
                preparedStructureSettlementFocus(settlement);
            sequencer_.page.set(sequencer_.pageForStep(finalFocus));
            sequencer_.focusedStep.set(finalFocus);
            sequencer_.structureUi.pageHold.clear();
            syncSequencerPagePreviewToVisible(sequencer_, false);
            return;
        }
        case PreparedStructureSettlement::Failed:
        default:
            return;
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM uint16_t SequencerStructureEditWorkflow::pasteCurrentPageAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPagePasteMutationPlan(
        sequencer_, structure_clipboard_,
        makeSequencerPreparedPageStructureTarget(
            currentActiveTrack(),
            sequencer_.visiblePage()),
        plan)) {
        case Preflight::Rejected:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
        case Preflight::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Preflight::Ready:
            break;
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Committed);
        case Result::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Result::Failed:
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteFocusedStep() const {
    return structure_clipboard_.hasSequencerSteps() &&
           structure_clipboard_.sequencerSteps.rootContext ==
               core::state::sequencer::isRootContentView(sequencer_);
}

FLASHMEM void SequencerStructureEditWorkflow::copyFocusedStep() {
    const uint8_t step = sequencer_.focusedStep.get();

    core::state::SequencerStepsClipboard clipboard;
    if (!captureFocusedStepClipboard(sequencer_, tracks_, step, clipboard)) return;

    if (!structure_clipboard_.storeSequencerSteps(
            clipboard, core::state::sequencer::graphView(sequencer_.pattern))) {
        return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::pasteFocusedStep() {
    pasteStepClipboardAt(sequencer_.focusedStep.get(), false);
}

FLASHMEM void SequencerStructureEditWorkflow::copyStepSelection() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get() || selection.placementActive()) return;

    core::state::SequencerStepsClipboard clipboard;
    if (!captureStepSelectionClipboard(sequencer_, tracks_, selection.selectedMask.get(),
                                       clipboard)) {
        return;
    }
    if (!structure_clipboard_.storeSequencerSteps(
            clipboard, core::state::sequencer::graphView(sequencer_.pattern))) {
        return;
    }
    selection.placing.set(true);
    selection.clipboardRevision.set(structure_clipboard_.revision.get());
    clearStepPastePreview();
}

FLASHMEM bool SequencerStructureEditWorkflow::canPasteStepSelection() const {
    const auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.placementActive() ||
        selection.clipboardRevision.get() != structure_clipboard_.revision.get() ||
        !structure_clipboard_.hasSequencerSteps() ||
        structure_clipboard_.sequencerSteps.rootContext !=
            core::state::sequencer::isRootContentView(sequencer_)) {
        return false;
    }

    const auto plan = buildStructureStepPastePlan(sequencer_, structure_clipboard_.sequencerSteps,
                                                  structureStepPasteMode(project_navigation_),
                                                  selection.cursorStep.get());
    return !plan.blocked && plan.hasEntries();
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelectionShallow() {
    resetStepSelection(StepResetDepth::Shallow);
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelectionDeep() {
    resetStepSelection(StepResetDepth::Deep);
}

FLASHMEM void SequencerStructureEditWorkflow::beginStepPastePreview() {
    beginStructureStepPastePreview(sequencer_, structure_clipboard_, project_navigation_);
}

FLASHMEM void SequencerStructureEditWorkflow::clearStepPastePreview() {
    clearStructureStepPastePreview(sequencer_);
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStepClipboardAt(uint8_t cursorStep,
                                                                   bool selectionPaste) {
    if (!structure_clipboard_.hasSequencerSteps()) return;
    if (selectionPaste) {
        const auto& selection = sequencer_.structureUi.stepSelection;
        if (!selection.placementActive() ||
            selection.cursorStep.get() != cursorStep) {
            return;
        }
    } else if (sequencer_.focusedStep.get() != cursorStep) {
        return;
    }

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::StepPaste;
    SequencerPreparedPageStructureTransaction transaction(
        sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    const uint16_t settlement = pasteStepClipboardAfterBoundary(transaction);
    const auto outcome = preparedStructureSettlementOutcome(settlement);
    if (outcome == PreparedStructureSettlement::Failed) return;

    const uint8_t finalFocus =
        preparedStructureSettlementFocus(settlement);
    if (outcome == PreparedStructureSettlement::NoChange) {
        sequencer_.page.set(
            core::state::sequencer::activeContentPageForStep(finalFocus));
        sequencer_.focusedStep.set(finalFocus);
    }
    if (selectionPaste) {
        auto& selection = sequencer_.structureUi.stepSelection;
        selection.cursorStep.set(finalFocus);
        clearStepPastePreview();
    }
    navigation_focus_.set(core::state::StructureNavigationFocus::STEP);
    sequencer_.structureUi.pageHold.clear();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM uint16_t
SequencerStructureEditWorkflow::pasteStepClipboardAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerStepPasteMutationPlan(
        sequencer_,
        structure_clipboard_,
        makeSequencerPreparedStepPasteTarget(
            currentActiveTrack(),
            structureStepPasteMode(project_navigation_),
            sequencer_.structureUi.stepSelection.placementActive()
                ? sequencer_.structureUi.stepSelection.cursorStep.get()
                : sequencer_.focusedStep.get()),
        plan)) {
        case Preflight::Rejected:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
        case Preflight::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Preflight::Ready:
            break;
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }

    switch (executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan)) {
        case Result::Committed:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Committed, plan.finalFocus);
        case Result::NoChange:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::NoChange, plan.finalFocus);
        case Result::Failed:
        default:
            return packPreparedStructureSettlement(
                PreparedStructureSettlement::Failed);
    }
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStepSelection() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.placementActive() ||
        selection.clipboardRevision.get() != structure_clipboard_.revision.get()) {
        return;
    }
    pasteStepClipboardAt(selection.cursorStep.get(), true);
}

FLASHMEM SequencerStructureEditWorkflow::HistoryTrackStructureChangePtr
SequencerStructureEditWorkflow::captureTrackHistoryBefore(uint16_t trackMask) const {
    return captureSequencerTrackStructureHistoryBefore(tracks_, sequencer_, trackMask);
}

FLASHMEM bool SequencerStructureEditWorkflow::recordTrackHistoryAfter(
    HistoryTrackStructureChangePtr change, uint16_t trackMask) {
    if (!change) return false;

    if (!captureSequencerTrackStructureHistoryAfter(tracks_, sequencer_, trackMask, *change)) {
        return false;
    }

    return recordSequencerTrackStructureHistoryChange(history_, std::move(change));
}

FLASHMEM void SequencerStructureEditWorkflow::syncPreviewToFocus(
    core::state::StructureNavigationFocus focus) {
    track_ui_.previewAddSlot.set(false);
    track_ui_.syncPreviewTrack(currentActiveTrack());
    syncSequencerPagePreviewToVisible(sequencer_,
                                      focus == core::state::StructureNavigationFocus::PAGE);
}

FLASHMEM void SequencerStructureEditWorkflow::resetFocusedStep(StepResetDepth depth) {
    const uint8_t step = sequencer_.focusedStep.get();
    if (step >= core::state::sequencer::activeContentLength(sequencer_)) return;

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::FocusedStepReset;
    SequencerPreparedPageStructureTransaction transaction(
        sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    if (resetFocusedStepAfterBoundary(transaction, depth) ==
        SequencerPreparedPageStructureResult::Committed) {
        sequencer_.structureUi.pageHold.clear();
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM SequencerPreparedPageStructureResult
SequencerStructureEditWorkflow::resetFocusedStepAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction,
    StepResetDepth depth
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerFocusedStepResetMutationPlan(
        sequencer_,
        makeSequencerPreparedFocusedStepResetTarget(
            currentActiveTrack(), sequencer_.focusedStep.get(), depth),
        plan)) {
        case Preflight::Rejected:
            return Result::Failed;
        case Preflight::NoChange:
            return Result::NoChange;
        case Preflight::Ready:
            break;
        default:
            return Result::Failed;
    }
    return executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan);
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelection(
    StepResetDepth depth
) {
    if (!sequencer_.structureUi.stepSelection.active.get()) return;

    using Action = SequencerPreparedPageStructureAction;
    constexpr auto action = Action::StepSelectionReset;
    SequencerPreparedPageStructureTransaction transaction(
        sequencer_, history_, action);
    if (!transaction.openBoundary()) return;
    if (resetStepSelectionAfterBoundary(transaction, depth) ==
        SequencerPreparedPageStructureResult::Committed) {
        sequencer_.structureUi.pageHold.clear();
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM SequencerPreparedPageStructureResult
SequencerStructureEditWorkflow::resetStepSelectionAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction,
    StepResetDepth depth
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerStepSelectionResetMutationPlan(
        sequencer_,
        sequencer_.structureUi.stepSelection.selectedMask.get(),
        makeSequencerPreparedStepSelectionResetTarget(
            currentActiveTrack(), depth),
        plan)) {
        case Preflight::Rejected:
            return Result::Failed;
        case Preflight::NoChange:
            return Result::NoChange;
        case Preflight::Ready:
            break;
        default:
            return Result::Failed;
    }
    return executeSequencerPreparedPageStructureMutationPlan(
        transaction, plan);
}

FLASHMEM uint16_t SequencerStructureEditWorkflow::currentTrackEnabledMask() const {
    return shared_tracks_.enabledMask();
}

FLASHMEM uint8_t SequencerStructureEditWorkflow::currentActiveTrack() const {
    return shared_tracks_.activeTrack();
}

FLASHMEM bool SequencerStructureEditWorkflow::applyTrackState(uint16_t enabledMask,
                                                              uint8_t activeTrack) {
    return shared_tracks_.setState(enabledMask, activeTrack);
}

}  // namespace core::handler
