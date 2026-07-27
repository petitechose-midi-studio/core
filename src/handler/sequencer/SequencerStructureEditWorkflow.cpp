#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <config/TimeCompat.hpp>

#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"
#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "handler/sequencer/SequencerStructureSelectionOps.hpp"
#include "handler/sequencer/SequencerStructureStepPasteWorkflow.hpp"
#include "handler/sequencer/SequencerStructureStepOps.hpp"
#include "handler/sequencer/SequencerStructureTrackOps.hpp"
#include "handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/sequencer/SequencerTrackTransferAction.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;
namespace contextual = core::state::contextual;

namespace {

constexpr uint32_t TRACK_PASTE_CANCELLED_MS = 700;
constexpr uint32_t TRACK_PASTE_APPLIED_MS = 1200;

}  // namespace

FLASHMEM SequencerStructureEditWorkflow::SequencerStructureEditWorkflow(StateRefs state)
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , navigation_focus_(state.navigationFocus)
    , track_ui_(state.trackNavigation)
    , project_navigation_(state.projectNavigation)
    , project_tracks_(state.projectTracks)
    , project_track_domain_(state.projectTrackDomain)
    , structure_clipboard_(state.structureClipboard)
    , shared_tracks_(state.sharedTracks)
    , history_(state.history)
    , macro_pages_(state.macroPages)
    , track_activations_(state.trackActivations)
    , status_bar_(state.statusBar) {}

FLASHMEM bool SequencerStructureEditWorkflow::canRemoveCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return false;
        return structure_slots::countEnabled(
            currentTrackEnabledMask(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        ) > 1U;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        return sequencer_.focusedStep.get() <
               core::state::sequencer::activeContentLength(sequencer_);
    }
    if (sequencer_.structureUi.previewAddPageSlot.get()) return false;
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
    core::state::StructureHoldAction action
) {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (action == core::state::StructureHoldAction::PASTE) {
            beginTrackPasteAction(core::time_compat::millis());
            return;
        }
        track_ui_.hold.begin(action, core::time_compat::millis());
        return;
    }
    sequencer_.structureUi.pageHold.begin(action, core::time_compat::millis());
}

FLASHMEM void SequencerStructureEditWorkflow::clearHoldAction() {
    track_ui_.hold.clear();
    sequencer_.structureUi.pageHold.clear();
}

FLASHMEM uint8_t SequencerStructureEditWorkflow::trackPasteTarget() const {
    if (track_ui_.selection.placementActive()) {
        return track_ui_.selection.cursorIndex.get();
    }
    return sequencerStructureTrackTarget(track_ui_, currentActiveTrack());
}

FLASHMEM core::state::ClipboardTransferPlan
SequencerStructureEditWorkflow::buildTrackPastePlan() const {
    return core::state::buildSequencerTrackClipboardTransferPlan(
        structure_clipboard_,
        tracks_,
        project_tracks_,
        trackPasteTarget(),
        track_activations_ != nullptr ? track_activations_->pendingTrackMask() : 0
    );
}

FLASHMEM void SequencerStructureEditWorkflow::setTrackPasteFeedback(
    contextual::OperationFeedbackStatus status,
    contextual::ContextActionReason reason,
    contextual::OperationFeedbackExpiryPolicy expiry,
    uint32_t nowMs,
    uint32_t durationMs
) {
    auto& paste = sequencer_.structureUi.trackPaste;
    contextual::setOperationFeedback(
        paste.feedback,
        contextual::ContextActionId::PASTE,
        {
            .kind = contextual::ContextEntityKind::TRACK,
            .track = paste.plan.entry.sourceTrack,
            .item = paste.plan.sourceMask,
        },
        {
            .kind = contextual::ContextEntityKind::TRACK,
            .track = paste.plan.entry.targetTrack,
            .item = paste.plan.targetMask,
        },
        status,
        reason,
        expiry,
        nowMs,
        durationMs
    );
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
            paste.guard,
            nowMs,
            static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        )) {
        paste.buttonOwned = false;
        return false;
    }
    setTrackPasteFeedback(
        contextual::OperationFeedbackStatus::PRESSED,
        core::state::sequencer::contextualReasonForTrackTransfer(plan.reason),
        contextual::OperationFeedbackExpiryPolicy::MANUAL,
        nowMs
    );
    paste.bump();
    return true;
}

FLASHMEM void SequencerStructureEditWorkflow::refreshTrackPastePreview(
    uint32_t nowMs
) {
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
            structure_clipboard_,
            tracks_,
            project_tracks_,
            paste.plan.entry.targetTrack,
            track_activations_ != nullptr ? track_activations_->pendingTrackMask() : 0
        );
        if (paste.clipboardKind != structure_clipboard_.kind.get() ||
            paste.clipboardRevision != structure_clipboard_.revision.get() ||
            !live.canCommit() ||
            !core::state::sameSequencerTrackClipboardTransferIdentity(
                paste.plan,
                live
            )) {
            if (contextual::cancelGuardedAction(paste.guard)) {
                paste.detailVisible = false;
                setTrackPasteFeedback(
                    contextual::OperationFeedbackStatus::BLOCKED,
                    contextual::ContextActionReason::STALE_TARGET,
                    contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                    nowMs,
                    TRACK_PASTE_CANCELLED_MS
                );
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
    const auto live = trackContext
        ? buildTrackPastePlan()
        : core::state::ClipboardTransferPlan{};
    if (!trackContext || !live.canCommit()) {
        const bool changed = paste.plan.hasEntries() || paste.detailVisible ||
            paste.feedback.active;
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
    setTrackPasteFeedback(
        contextual::OperationFeedbackStatus::PREVIEW,
        core::state::sequencer::contextualReasonForTrackTransfer(live.reason),
        contextual::OperationFeedbackExpiryPolicy::MANUAL,
        nowMs
    );
    paste.bump();
}

FLASHMEM void SequencerStructureEditWorkflow::updateTrackPasteActivation(
    uint32_t nowMs
) {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (track_activations_ == nullptr || paste.activationGeneration == 0 ||
        paste.feedback.status != contextual::OperationFeedbackStatus::QUEUED ||
        !paste.plan.hasEntries()) {
        return;
    }

    const auto telemetry = track_activations_->telemetry(
        paste.plan.entry.targetTrack
    );
    if (telemetry.generation != paste.activationGeneration ||
        telemetry.origin !=
            core::state::sequencer::SequencerTrackActivationOrigin::TRACK_PASTE) {
        return;
    }

    if (telemetry.status ==
        core::state::sequencer::SequencerTrackActivationStatus::CANCELLED) {
        setTrackPasteFeedback(
            contextual::OperationFeedbackStatus::CANCELLED,
            contextual::ContextActionReason::FAILED,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            TRACK_PASTE_CANCELLED_MS
        );
        paste.bump();
    } else if (telemetry.status ==
               core::state::sequencer::SequencerTrackActivationStatus::APPLIED) {
        setTrackPasteFeedback(
            contextual::OperationFeedbackStatus::APPLIED,
            contextual::ContextActionReason::NONE,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            TRACK_PASTE_APPLIED_MS
        );
        paste.bump();
    }
}

FLASHMEM bool SequencerStructureEditWorkflow::commitTrackPaste(uint32_t nowMs) {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (paste.commitConsumed || !paste.plan.canCommit()) return false;
    paste.commitConsumed = true;
    history_.commitCoalescedPatternEdit();

    const auto result = executeSequencerTrackTransfer(
        tracks_,
        project_tracks_,
        sequencer_,
        structure_clipboard_,
        shared_tracks_,
        history_,
        paste.plan.entry.targetTrack,
        0,
        track_activations_,
        status_bar_ != nullptr && status_bar_->playing.get(),
        &macro_pages_
    );
    paste.plan = result.plan;
    paste.activationGeneration = result.activationGeneration;
    paste.operationGeneration = result.operationId != 0
        ? result.operationId
        : paste.interactionGeneration;

    if (!result.applied()) {
        auto reason = core::state::sequencer::contextualReasonForTrackTransfer(
            result.plan.reason
        );
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
                default:
                    reason = contextual::ContextActionReason::FAILED;
                    break;
            }
        }
        setTrackPasteFeedback(
            contextual::OperationFeedbackStatus::BLOCKED,
            reason,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            TRACK_PASTE_CANCELLED_MS
        );
        paste.bump();
        return false;
    }

    setTrackPasteFeedback(
        result.activationGeneration != 0
            ? contextual::OperationFeedbackStatus::QUEUED
            : contextual::OperationFeedbackStatus::APPLIED,
        contextual::ContextActionReason::NONE,
        result.activationGeneration != 0
            ? contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED
            : contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        result.activationGeneration != 0 ? 0 : TRACK_PASTE_APPLIED_MS
    );
    syncPreviewToFocus(core::state::StructureNavigationFocus::TRACK);
    paste.bump();
    return true;
}

FLASHMEM void SequencerStructureEditWorkflow::update(uint32_t nowMs) {
    refreshStructureSelectionPastePreview();
    auto& paste = sequencer_.structureUi.trackPaste;
    updateTrackPasteActivation(nowMs);

    if (contextual::updateOperationFeedback(paste.feedback, nowMs)) {
        paste.bump();
    }

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
                core::state::sequencer::contextualReasonForTrackTransfer(
                    paste.plan.reason
                ),
                contextual::OperationFeedbackExpiryPolicy::MANUAL,
                nowMs
            );
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

FLASHMEM bool
SequencerStructureEditWorkflow::canPasteStructureSelection() const {
    if (track_ui_.selection.placementActive()) {
        return buildTrackPastePlan().canCommit();
    }
    const auto& pageSelection =
        sequencer_.structureUi.pageSelection;
    if (!pageSelection.placementActive()) return false;
    return buildPageSelectionPastePlan(
        sequencer_,
        structure_clipboard_,
        pageSelection.cursorIndex.get()
    ).canCommit();
}

FLASHMEM void
SequencerStructureEditWorkflow::refreshStructureSelectionPastePreview() {
    auto refresh = [](
        core::state::StructureSelectionState& selection,
        uint16_t destinationMask,
        uint16_t overwriteMask,
        bool blocked,
        uint32_t clipboardRevision
    ) {
        selection.destinationMask.set(destinationMask);
        selection.overwriteMask.set(overwriteMask);
        selection.pasteBlocked.set(blocked);
        selection.clipboardRevision.set(clipboardRevision);
    };

    auto& trackSelection = track_ui_.selection;
    if (trackSelection.placementActive()) {
        const auto plan = buildTrackPastePlan();
        refresh(
            trackSelection,
            plan.targetMask,
            plan.overwriteMask,
            !plan.canCommit(),
            structure_clipboard_.revision.get()
        );
    } else {
        refresh(trackSelection, 0U, 0U, false, 0U);
    }

    auto& pageSelection =
        sequencer_.structureUi.pageSelection;
    if (pageSelection.placementActive()) {
        const auto plan = buildPageSelectionPastePlan(
            sequencer_,
            structure_clipboard_,
            pageSelection.cursorIndex.get()
        );
        refresh(
            pageSelection,
            plan.destinationMask,
            plan.overwriteMask,
            !plan.canCommit(),
            structure_clipboard_.revision.get()
        );
    } else {
        refresh(pageSelection, 0U, 0U, false, 0U);
    }
}

FLASHMEM void
SequencerStructureEditWorkflow::copyStructureSelection() {
    if (track_ui_.selection.active.get()) {
        auto& selection = track_ui_.selection;
        if (selection.placing.get()) return;
        auto clipboard = captureTrackSelectionClipboard(
            tracks_,
            sequencer_,
            macro_pages_,
            selection.selectedMask.get()
        );
        if (!clipboard ||
            !structure_clipboard_.storeSequencerTrackSelection(
                std::move(clipboard)
            )) {
            return;
        }
        selection.placing.set(true);
        selection.clipboardRevision.set(
            structure_clipboard_.revision.get()
        );
        refreshStructureSelectionPastePreview();
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.active.get() || selection.placing.get()) return;
    core::state::SequencerPageSelectionClipboard clipboard;
    if (!capturePageSelectionClipboard(
            sequencer_,
            selection.selectedMask.get(),
            clipboard
        ) ||
        !structure_clipboard_.storeSequencerPageSelection(
            clipboard,
            core::state::sequencer::graphView(sequencer_.pattern)
        )) {
        return;
    }
    selection.placing.set(true);
    selection.clipboardRevision.set(
        structure_clipboard_.revision.get()
    );
    refreshStructureSelectionPastePreview();
}

FLASHMEM void
SequencerStructureEditWorkflow::pasteStructureSelection() {
    if (track_ui_.selection.placementActive()) {
        const auto result = executeSequencerTrackTransfer(
            tracks_,
            project_tracks_,
            sequencer_,
            structure_clipboard_,
            shared_tracks_,
            history_,
            track_ui_.selection.cursorIndex.get(),
            0,
            track_activations_,
            status_bar_ != nullptr && status_bar_->playing.get(),
            &macro_pages_
        );
        if (!result.applied()) {
            refreshStructureSelectionPastePreview();
            return;
        }
        navigation_focus_.set(
            core::state::StructureNavigationFocus::TRACK
        );
        refreshStructureSelectionPastePreview();
        return;
    }

    auto& selection = sequencer_.structureUi.pageSelection;
    if (!selection.placementActive()) return;
    const auto plan = buildPageSelectionPastePlan(
        sequencer_,
        structure_clipboard_,
        selection.cursorIndex.get()
    );
    if (!plan.canCommit()) {
        refreshStructureSelectionPastePreview();
        return;
    }

    auto historyChange = capturePageHistoryBefore();
    if (!historyChange) return;
    if (!pastePageSelectionClipboard(
            sequencer_,
            structure_clipboard_,
            plan
        )) {
        return;
    }
    sequencer_.pattern.bumpStepDataRevision();
    sequencer_.page.set(plan.firstDestinationPage);
    sequencer_.focusedStep.set(
        sequencer_.pageStartStep(plan.firstDestinationPage)
    );
    sequencer_.structureUi.syncPreviewPage(
        plan.firstDestinationPage
    );
    (void)recordPageHistoryAfter(std::move(historyChange));
    refreshStructureSelectionPastePreview();
}

FLASHMEM contextual::GuardedActionRelease
SequencerStructureEditWorkflow::releaseTrackPasteAction(uint32_t nowMs) {
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
        setTrackPasteFeedback(
            contextual::OperationFeedbackStatus::CANCELLED,
            contextual::ContextActionReason::NONE,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            TRACK_PASTE_CANCELLED_MS
        );
    }
    paste.bump();
    return release;
}

FLASHMEM bool SequencerStructureEditWorkflow::cancelTrackPasteAction(
    uint32_t nowMs
) {
    auto& paste = sequencer_.structureUi.trackPaste;
    bool changed = false;
    if (paste.detailVisible) {
        paste.detailVisible = false;
        changed = true;
    }
    if (contextual::cancelGuardedAction(paste.guard)) {
        setTrackPasteFeedback(
            contextual::OperationFeedbackStatus::CANCELLED,
            contextual::ContextActionReason::NONE,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            TRACK_PASTE_CANCELLED_MS
        );
        changed = true;
    }
    if (changed) paste.bump();
    return changed;
}

FLASHMEM bool SequencerStructureEditWorkflow::trackPasteNavigationBlocked() const {
    const auto& paste = sequencer_.structureUi.trackPaste;
    return paste.buttonOwned || paste.gestureActive() || paste.detailVisible;
}

FLASHMEM bool SequencerStructureEditWorkflow::trackPastePlanInspectable() const {
    const auto& paste = sequencer_.structureUi.trackPaste;
    return paste.inspectable() && paste.plan.canCommit() && paste.feedback.active;
}

FLASHMEM bool SequencerStructureEditWorkflow::trackPasteDetailsVisible() const {
    return sequencer_.structureUi.trackPaste.detailVisible;
}

FLASHMEM void SequencerStructureEditWorkflow::toggleTrackPasteDetails() {
    auto& paste = sequencer_.structureUi.trackPaste;
    if (!trackPastePlanInspectable()) return;
    paste.detailVisible = !paste.detailVisible;
    paste.bump();
}

FLASHMEM void SequencerStructureEditWorkflow::applyBottomLeftTapCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const uint8_t activeTrack = currentActiveTrack();
        (void)toggleSequencerStructureTrackMute(
            project_tracks_,
            project_track_domain_,
            activeTrack
        );
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        resetFocusedStep(StepResetDepth::Shallow);
        return;
    }

    auto historyChange = capturePageHistoryBefore();
    if (!historyChange) return;
    if (clearCurrentSequencerStructurePage(sequencer_)) {
        recordPageHistoryAfter(std::move(historyChange));
    }
}

FLASHMEM void SequencerStructureEditWorkflow::removeCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const auto mutation = structure_slots::removeIndex(
            currentTrackEnabledMask(),
            currentActiveTrack(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        );
        if (!mutation.changed) return;
        const uint16_t historyMask = static_cast<uint16_t>(
            sequencerStructureHistoryTrackBit(currentActiveTrack()) |
            sequencerStructureHistoryTrackBit(mutation.nextActive)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        if (!change) return;
        if (!applyTrackState(mutation.nextMask, mutation.nextActive)) return;
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        resetFocusedStep(StepResetDepth::Deep);
        return;
    }

    auto historyChange = capturePageHistoryBefore();
    if (!historyChange) return;
    if (removeCurrentSequencerStructurePage(sequencer_)) {
        recordPageHistoryAfter(std::move(historyChange));
    }
}

FLASHMEM void SequencerStructureEditWorkflow::copyCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        core::state::sequencer::SequencerPatternSnapshot snapshot;
        core::state::sequencer::captureSnapshot(sequencer_.pattern, snapshot);
        if (!structure_clipboard_.storeSequencerTrack(
            snapshot,
            core::state::sequencer::graphView(sequencer_.pattern),
            currentActiveTrack(),
            core::state::sequencer::sequencerCcLaneView(sequencer_.pattern)
        )) {
            return;
        }
        return;
    }

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        copyFocusedStep();
        return;
    }

    if (sequencer_.structureUi.previewAddPageSlot.get()) return;
    core::state::SequencerPageClipboard clipboard;
    const uint8_t page = sequencer_.visiblePage();
    if (!capturePageClipboard(sequencer_, page, clipboard)) return;
    if (!structure_clipboard_.storeSequencerPage(
        clipboard,
        core::state::sequencer::graphView(sequencer_.pattern)
    )) {
        return;
    }
}

FLASHMEM void SequencerStructureEditWorkflow::pasteCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (!structure_clipboard_.hasSequencerTrack()) return;
        const uint8_t targetTrack =
            sequencerStructureTrackTarget(track_ui_, currentActiveTrack());
        const auto result = executeSequencerTrackTransfer(
                tracks_,
                project_tracks_,
                sequencer_,
                structure_clipboard_,
                shared_tracks_,
                history_,
                targetTrack,
                0,
                track_activations_,
                status_bar_ != nullptr && status_bar_->playing.get()
            );
        if (!result.applied()) return;
        syncPreviewToFocus(core::state::StructureNavigationFocus::TRACK);
        return;
    }

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::STEP) {
        pasteFocusedStep();
        return;
    }

    if (!structure_clipboard_.hasSequencerPage()) return;
    auto historyChange = capturePageHistoryBefore();
    if (!historyChange) return;
    uint8_t targetPage = sequencer_.visiblePage();
    if (sequencer_.structureUi.previewAddPageSlot.get()) {
        targetPage = sequencer_.clampPage(sequencer_.structureUi.previewPageIndex.get());
        if (!createSequencerStructurePage(sequencer_)) return;
    }

    const auto& clipboard = structure_clipboard_.sequencerPage;
    pastePageClipboard(
        sequencer_,
        clipboard,
        structure_clipboard_.sequencerGraph.get(),
        targetPage
    );
    sequencer_.pattern.bumpStepDataRevision();
    sequencer_.structureUi.syncPreviewPage(targetPage);
    sequencer_.page.set(targetPage);
    sequencer_.structureUi.previewAddPageSlot.set(false);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(targetPage));
    recordPageHistoryAfter(std::move(historyChange));
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
        clipboard,
        core::state::sequencer::graphView(sequencer_.pattern)
    )) {
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
    if (!captureStepSelectionClipboard(
            sequencer_,
            tracks_,
            selection.selectedMask.get(),
            clipboard
        )) {
        return;
    }
    if (!structure_clipboard_.storeSequencerSteps(
        clipboard,
        core::state::sequencer::graphView(sequencer_.pattern)
    )) {
        return;
    }
    selection.placing.set(true);
    selection.clipboardRevision.set(structure_clipboard_.revision.get());
    clearStepPastePreview();
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

FLASHMEM void SequencerStructureEditWorkflow::pasteStepClipboardAt(
    uint8_t cursorStep,
    bool selectionPaste
) {
    if (!structure_clipboard_.hasSequencerSteps()) return;

    const auto mode = structureStepPasteMode(project_navigation_);
    const auto plan = buildStructureStepPastePlan(
        sequencer_,
        structure_clipboard_.sequencerSteps,
        mode,
        cursorStep
    );
    if (plan.blocked || !plan.hasEntries()) {
        clearStepPastePreview();
        return;
    }
    auto historyChange = capturePageHistoryBefore();
    if (!historyChange) return;

    if (!commitStructureStepPastePlan(sequencer_, structure_clipboard_, mode, plan)) {
        clearStepPastePreview();
        return;
    }

    core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.pattern.bumpStepDataRevision();
    if (selectionPaste) {
        auto& selection = sequencer_.structureUi.stepSelection;
        selection.cursorStep.set(plan.firstTarget);
        clearStepPastePreview();
    }
    sequencer_.focusedStep.set(plan.firstTarget);
    sequencer_.page.set(core::state::sequencer::activeContentPageForStep(plan.firstTarget));
    navigation_focus_.set(core::state::StructureNavigationFocus::STEP);
    recordPageHistoryAfter(std::move(historyChange));
}

FLASHMEM void SequencerStructureEditWorkflow::pasteStepSelection() {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.placementActive() ||
        selection.clipboardRevision.get() !=
            structure_clipboard_.revision.get()) {
        return;
    }
    pasteStepClipboardAt(selection.cursorStep.get(), true);
}

FLASHMEM SequencerStructureEditWorkflow::HistoryPatternChangePtr
SequencerStructureEditWorkflow::capturePageHistoryBefore() const {
    return captureSequencerPageStructureHistoryBefore(sequencer_);
}

FLASHMEM bool SequencerStructureEditWorkflow::recordPageHistoryAfter(
    HistoryPatternChangePtr change
) {
    return recordSequencerPageStructureHistoryChange(
        history_,
        sequencer_,
        std::move(change),
        currentActiveTrack()
    );
}

FLASHMEM SequencerStructureEditWorkflow::HistoryTrackStructureChangePtr
SequencerStructureEditWorkflow::captureTrackHistoryBefore(uint16_t trackMask) const {
    return captureSequencerTrackStructureHistoryBefore(tracks_, sequencer_, trackMask);
}

FLASHMEM bool SequencerStructureEditWorkflow::recordTrackHistoryAfter(
    HistoryTrackStructureChangePtr change,
    uint16_t trackMask
) {
    if (!change) return false;

    if (!captureSequencerTrackStructureHistoryAfter(
            tracks_,
            sequencer_,
            trackMask,
            *change
        )) {
        return false;
    }

    return recordSequencerTrackStructureHistoryChange(history_, std::move(change));
}

FLASHMEM void SequencerStructureEditWorkflow::syncPreviewToFocus(
    core::state::StructureNavigationFocus focus
) {
    track_ui_.previewAddSlot.set(false);
    track_ui_.syncPreviewTrack(currentActiveTrack());
    syncSequencerPagePreviewToVisible(
        sequencer_,
        focus == core::state::StructureNavigationFocus::PAGE
    );
}

FLASHMEM void SequencerStructureEditWorkflow::resetFocusedStep(StepResetDepth depth) {
    const uint8_t step = sequencer_.focusedStep.get();
    if (step >= core::state::sequencer::activeContentLength(sequencer_)) return;

    auto historyChange = capturePageHistoryBefore();
    if (!historyChange) return;

    if (!resetActiveContentStep(sequencer_, step, depth)) return;
    const bool compacted = depth == StepResetDepth::Deep &&
        core::state::sequencer::compactSequencerGraph(sequencer_);
    if (!compacted) core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.pattern.bumpStepDataRevision();
    sequencer_.focusedStep.set(step);
    sequencer_.page.set(core::state::sequencer::activeContentPageForStep(step));
    recordPageHistoryAfter(std::move(historyChange));
}

FLASHMEM void SequencerStructureEditWorkflow::resetStepSelection(StepResetDepth depth) {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get()) return;

    const auto selectedMask = selection.selectedMask.get();

    auto historyChange = capturePageHistoryBefore();
    if (!historyChange) return;

    if (!resetSelectedActiveContentSteps(sequencer_, selectedMask, depth)) return;
    const bool compacted = depth == StepResetDepth::Deep &&
        core::state::sequencer::compactSequencerGraph(sequencer_);
    if (!compacted) core::state::sequencer::refreshContentView(sequencer_);
    sequencer_.pattern.bumpStepDataRevision();
    recordPageHistoryAfter(std::move(historyChange));
}

FLASHMEM uint16_t SequencerStructureEditWorkflow::currentTrackEnabledMask() const {
    return shared_tracks_.enabledMask();
}

FLASHMEM uint8_t SequencerStructureEditWorkflow::currentActiveTrack() const {
    return shared_tracks_.activeTrack();
}

FLASHMEM bool SequencerStructureEditWorkflow::applyTrackState(
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    return shared_tracks_.setState(enabledMask, activeTrack);
}

}  // namespace core::handler
