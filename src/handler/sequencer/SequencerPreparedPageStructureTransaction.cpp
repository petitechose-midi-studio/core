#include "handler/sequencer/SequencerPreparedPageStructureTransaction.hpp"

#include <cstdlib>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#if defined(ARDUINO)
// Cold placement is owned by the linker object selector. Preserving ordinary
// function sections prevents this transaction TU becoming one indivisible
// `.flashmem` retention unit.
#undef FLASHMEM
#define FLASHMEM
#endif

namespace core::handler {

namespace {

namespace seq = core::state::sequencer;

constexpr auto kPageStructureOwner =
    seq::SequencerPreparedPatternEditOwner::PageStructure;

constexpr uint8_t pageStructureActionKey(
    SequencerPreparedPageStructureAction action
) {
    return static_cast<uint8_t>(action);
}

constexpr bool validPageStructureAction(
    SequencerPreparedPageStructureAction action
) {
    switch (action) {
        case SequencerPreparedPageStructureAction::PageSelectionPaste:
        case SequencerPreparedPageStructureAction::PageClear:
        case SequencerPreparedPageStructureAction::PageDelete:
        case SequencerPreparedPageStructureAction::PagePaste:
        case SequencerPreparedPageStructureAction::StepPaste:
        case SequencerPreparedPageStructureAction::FocusedStepReset:
        case SequencerPreparedPageStructureAction::StepSelectionReset:
        case SequencerPreparedPageStructureAction::PageSelectionReset:
        case SequencerPreparedPageStructureAction::PageSelectionDeleteOrDeepReset:
            return true;
        case SequencerPreparedPageStructureAction::Invalid:
            return false;
    }
    return false;
}

[[noreturn]] FLASHMEM void failPageStructureTransactionInvariant() {
    // A live Page mutation whose prepared owner cannot be proven closed must
    // never continue in release firmware. The compiler trap is fail-stop
    // without pulling the hosted abort runtime into scarce RAM1.
#if defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    std::abort();
#endif
}

}  // namespace

FLASHMEM SequencerPreparedPageStructureTransaction::
    SequencerPreparedPageStructureTransaction(
        seq::SequencerState& sequencer,
        SequencerHistoryDomainServices history,
        SequencerPreparedPageStructureAction action
    )
    : sequencer_(sequencer),
      history_(history),
      action_(action) {}

FLASHMEM SequencerPreparedPageStructureTransaction::
    ~SequencerPreparedPageStructureTransaction() {
    if (ownsPending()) abortOwned();
}

FLASHMEM bool SequencerPreparedPageStructureTransaction::ownsPending() const {
    return phase_ == Phase::Armed || phase_ == Phase::Sealed;
}

FLASHMEM void SequencerPreparedPageStructureTransaction::abortOwned() {
    if (!ownsPending()) return;
    const auto outcome = history_.abortPreparedPatternEdit(
        kPageStructureOwner,
        pageStructureActionKey(action_)
    );
    if (outcome != seq::SequencerPreparedPatternEditAbortOutcome::Aborted) {
        failPageStructureTransactionInvariant();
    }
    phase_ = Phase::Closed;
}

FLASHMEM void SequencerPreparedPageStructureTransaction::closeProtocolMisuse() {
    if (ownsPending()) abortOwned();
    phase_ = Phase::Closed;
}

FLASHMEM bool SequencerPreparedPageStructureTransaction::openBoundary() {
    if (phase_ != Phase::Initial) {
        closeProtocolMisuse();
        return false;
    }
    phase_ = Phase::Closed;

    if (sequencer_.stepContentDraft.rejectTransitionIfActive(
            seq::SequencerStepContentDraftBlockedTransition::STRUCTURE_EDIT)) {
        return false;
    }

    const auto outcome = history_.commitCoalescedPatternEditOutcome();
    using BoundaryOutcome = seq::SequencerPatternHistoryCommitOutcome;
    switch (outcome) {
        case BoundaryOutcome::Failed:
            sequencer_.historyFeedback.showRejection(
                seq::SequencerHistoryRejectionReason::HistoryUnavailable,
                core::time_compat::millis());
            return false;
        case BoundaryOutcome::NoPending:
        case BoundaryOutcome::NoChange:
        case BoundaryOutcome::Committed:
            break;
        default:
            return false;
    }

    phase_ = Phase::BoundaryOpen;
    return true;
}

FLASHMEM bool SequencerPreparedPageStructureTransaction::begin(
    const SequencerPreparedPageStructureExecution& execution
) {
    if (phase_ != Phase::BoundaryOpen) {
        closeProtocolMisuse();
        return false;
    }
    phase_ = Phase::Closed;
    if (!validPageStructureAction(action_) || execution.action != action_ ||
        execution.revalidate == nullptr ||
        execution.mutate == nullptr ||
        execution.expectedTrack >= seq::SequencerTrackBankState::TRACK_COUNT) {
        return false;
    }

    descriptor_ = seq::SequencerHistoryDescriptor{
        .kind = seq::SequencerHistoryActionKind::PageStructure,
        .trackIndex = execution.expectedTrack,
        .hasValue = execution.beforePageCount != execution.afterPageCount,
        .beforeValue = execution.beforePageCount,
        .afterValue = execution.afterPageCount,
    };
    const auto outcome = history_.beginPreparedPatternEdit(
        kPageStructureOwner,
        pageStructureActionKey(action_),
        execution.payloadPlan,
        descriptor_,
        execution.compactGraphOnSeal
    );
    using BeginOutcome = seq::SequencerPreparedPatternEditBeginOutcome;
    if (outcome == BeginOutcome::Started) {
        phase_ = Phase::Armed;
        return true;
    }
    if (outcome == BeginOutcome::Continued) {
        // A Page action never inherits a pending owner across call stacks.
        phase_ = Phase::Armed;
        abortOwned();
    }
    sequencer_.historyFeedback.showRejection(
        outcome == BeginOutcome::Continued
            ? seq::SequencerHistoryRejectionReason::HistoryUnavailable
            : seq::sequencerHistoryRejectionFor(outcome),
        core::time_compat::millis());
    return false;
}

FLASHMEM SequencerPreparedPageStructureResult
SequencerPreparedPageStructureTransaction::execute(
    const SequencerPreparedPageStructureExecution& execution
) {
    using MutationOutcome = SequencerPreparedPageStructureMutationOutcome;
    if (!begin(execution)) return SequencerPreparedPageStructureResult::Failed;

    if (!execution.revalidate(execution.mutationContext, sequencer_)) {
        return abort();
    }

    if (!history_.preparedPatternEditReady(
            kPageStructureOwner,
            pageStructureActionKey(action_),
            execution.expectedTrack)) {
        return abort();
    }

    const auto mutation = execution.mutate(
        execution.mutationContext,
        sequencer_,
        history_
    );
    bool mutationChanged = false;
    switch (mutation) {
        case MutationOutcome::Failed:
            return abort();
        case MutationOutcome::NoChange:
            break;
        case MutationOutcome::Changed:
            mutationChanged = true;
            break;
        default:
            return abort();
    }

    const auto result = sealAndCommit(mutationChanged);
    if (result == SequencerPreparedPageStructureResult::Failed) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable, core::time_compat::millis());
    }
    if (result == SequencerPreparedPageStructureResult::Committed &&
        execution.finalizeCommitted != nullptr) {
        execution.finalizeCommitted(execution.mutationContext, sequencer_);
    }
    return result;
}

FLASHMEM SequencerPreparedPageStructureResult
SequencerPreparedPageStructureTransaction::abort() {
    if (ownsPending()) abortOwned();
    else phase_ = Phase::Closed;
    sequencer_.historyFeedback.showRejection(seq::SequencerHistoryRejectionReason::Blocked,
                                             core::time_compat::millis());
    return SequencerPreparedPageStructureResult::Failed;
}

FLASHMEM SequencerPreparedPageStructureResult
SequencerPreparedPageStructureTransaction::sealAndCommit(
    bool mutationChanged
) {
    using Result = SequencerPreparedPageStructureResult;
    if (phase_ != Phase::Armed) {
        closeProtocolMisuse();
        return Result::Failed;
    }

    const auto sealOutcome = history_.sealPreparedPatternEdit(
        kPageStructureOwner,
        pageStructureActionKey(action_),
        mutationChanged,
        descriptor_
    );
    using SealOutcome = seq::SequencerPreparedPatternEditSealOutcome;
    switch (sealOutcome) {
        case SealOutcome::Cleared:
            phase_ = Phase::Closed;
            return Result::NoChange;
        case SealOutcome::FailedClosed:
            phase_ = Phase::Closed;
            return Result::Failed;
        case SealOutcome::Failed:
            abortOwned();
            return Result::Failed;
        case SealOutcome::Sealed:
            phase_ = Phase::Sealed;
            break;
        default:
            failPageStructureTransactionInvariant();
    }

    const auto commitOutcome = history_.commitPreparedPatternEdit(
        kPageStructureOwner
    );
    using CommitOutcome = seq::SequencerPreparedPatternEditCommitOutcome;
    switch (commitOutcome) {
        case CommitOutcome::Committed:
            phase_ = Phase::Closed;
            return Result::Committed;
        case CommitOutcome::NoChange:
            phase_ = Phase::Closed;
            return Result::NoChange;
        case CommitOutcome::Failed:
            abortOwned();
            return Result::Failed;
        case CommitOutcome::NoPending:
            failPageStructureTransactionInvariant();
    }
    failPageStructureTransactionInvariant();
}

}  // namespace core::handler
