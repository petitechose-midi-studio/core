#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerPreparedPageStructureMutationPlan.hpp"
#include "handler/sequencer/SequencerPreparedPageStructureTransaction.hpp"
#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerStructureSelectionOps.hpp"

namespace core::handler {

FLASHMEM bool
SequencerStructureEditWorkflow::selectionHoldActionAvailable() const {
    if (track_ui_.selection.active.get()) {
        if (track_ui_.selection.placing.get()) return false;
        const uint16_t enabledMask = currentTrackEnabledMask();
        const uint16_t selectedMask = activeTrackSelectionMask(
            track_ui_.selection.selectedMask.get(),
            enabledMask
        );
        const uint8_t count =
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
        const uint8_t selectedCount =
            core::state::shared::countEnabled(selectedMask, count);
        const uint8_t enabledCount =
            core::state::shared::countEnabled(enabledMask, count);
        return selectedCount > 0U && selectedCount < enabledCount;
    }
    if (sequencer_.structureUi.pageSelection.active.get()) {
        if (sequencer_.structureUi.pageSelection.placing.get()) {
            return false;
        }
        const uint8_t pageCount =
            core::state::sequencer::activeContentPageCount(sequencer_);
        const uint16_t selectedMask = activeContentPageSelectionMask(
            sequencer_,
            sequencer_.structureUi.pageSelection.selectedMask.get()
        );
        const uint8_t selectedCount =
            core::state::shared::countEnabled(selectedMask, pageCount);
        return core::state::sequencer::isRootContentView(sequencer_)
            ? selectedCount > 0U && selectedCount < pageCount
            : selectedCount > 0U;
    }
    return sequencer_.structureUi.stepSelection.active.get() &&
           sequencer_.structureUi.stepSelection.anySelected();
}

FLASHMEM void SequencerStructureEditWorkflow::applySelectionBottomLeftTap() {
    if (track_ui_.selection.active.get()) {
        if (track_ui_.selection.placing.get()) return;
        const uint16_t selectedMask = activeTrackSelectionMask(
            track_ui_.selection.selectedMask.get(),
            currentTrackEnabledMask()
        );
        if (selectedMask == 0U) return;

        const uint16_t currentMuted = project_tracks_.authored.mutedMask;
        const bool anySelectedAudible =
            (selectedMask & static_cast<uint16_t>(~currentMuted)) != 0U;
        const uint16_t nextMuted = anySelectedAudible
            ? static_cast<uint16_t>(currentMuted | selectedMask)
            : static_cast<uint16_t>(
                  currentMuted & static_cast<uint16_t>(~selectedMask)
              );
        (void)project_track_domain_.setMutedMask(
            nextMuted,
            track_ui_.selection.cursorIndex.get()
        );
        return;
    }

    if (sequencer_.structureUi.pageSelection.active.get()) {
        if (sequencer_.structureUi.pageSelection.placing.get()) return;
        using Action = SequencerPreparedPageStructureAction;
        constexpr auto action = Action::PageSelectionReset;
        SequencerPreparedPageStructureTransaction transaction(
            sequencer_, history_, action);
        if (!transaction.openBoundary()) return;
        if (resetPageSelectionAfterBoundary(transaction) ==
            SequencerPreparedPageStructureResult::Committed) {
            sequencer_.structureUi.pageHold.clear();
        }
        return;
    }

    resetStepSelectionShallow();
}

FLASHMEM void SequencerStructureEditWorkflow::applySelectionBottomLeftHold() {
    if (track_ui_.selection.active.get()) {
        if (track_ui_.selection.placing.get()) return;
        const uint16_t selectedMask = activeTrackSelectionMask(
            track_ui_.selection.selectedMask.get(),
            currentTrackEnabledMask()
        );
        const auto mutation = deleteSelectedStructureTracks(
            currentTrackEnabledMask(),
            selectedMask,
            currentActiveTrack()
        );
        if (!mutation.changed) return;

        const uint16_t historyMask = static_cast<uint16_t>(
            selectedMask |
            sequencerStructureHistoryTrackBit(currentActiveTrack()) |
            sequencerStructureHistoryTrackBit(mutation.nextActive)
        );
        auto change = captureTrackHistoryBefore(historyMask);
        if (!change) return;
        if (!applyTrackState(mutation.nextMask, mutation.nextActive)) return;

        track_ui_.selection.reset(
            core::state::StructureSelectionScope::TRACK,
            mutation.nextActive
        );
        navigation_focus_.set(core::state::StructureNavigationFocus::TRACK);
        syncPreviewToFocus(core::state::StructureNavigationFocus::TRACK);
        recordTrackHistoryAfter(std::move(change), historyMask);
        return;
    }

    if (sequencer_.structureUi.pageSelection.active.get()) {
        if (sequencer_.structureUi.pageSelection.placing.get()) return;
        using Action = SequencerPreparedPageStructureAction;
        constexpr auto action =
            Action::PageSelectionDeleteOrDeepReset;
        SequencerPreparedPageStructureTransaction transaction(
            sequencer_, history_, action);
        if (!transaction.openBoundary()) return;
        if (deleteOrResetPageSelectionAfterBoundary(transaction) ==
            SequencerPreparedPageStructureResult::Committed) {
            sequencer_.structureUi.pageHold.clear();
            if (!core::state::sequencer::isRootContentView(sequencer_)) {
                return;
            }
            auto& selection = sequencer_.structureUi.pageSelection;
            const uint8_t cursor = sequencer_.visiblePage();
            selection.reset(
                core::state::StructureSelectionScope::PAGE,
                cursor
            );
            navigation_focus_.set(core::state::StructureNavigationFocus::PAGE);
            syncPreviewToFocus(core::state::StructureNavigationFocus::PAGE);
        }
        return;
    }

    resetStepSelectionDeep();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM SequencerPreparedPageStructureResult
SequencerStructureEditWorkflow::resetPageSelectionAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPageSelectionResetMutationPlan(
        sequencer_,
        currentActiveTrack(),
        sequencer_.structureUi.pageSelection.selectedMask.get(),
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

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM SequencerPreparedPageStructureResult
SequencerStructureEditWorkflow::deleteOrResetPageSelectionAfterBoundary(
    SequencerPreparedPageStructureTransaction& transaction
) {
    using Preflight = SequencerPreparedPageStructurePreflightOutcome;
    using Result = SequencerPreparedPageStructureResult;

    SequencerPreparedPageStructureMutationPlan plan;
    switch (buildSequencerPageSelectionDeleteOrDeepResetMutationPlan(
        sequencer_,
        currentActiveTrack(),
        sequencer_.structureUi.pageSelection.selectedMask.get(),
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

}  // namespace core::handler
