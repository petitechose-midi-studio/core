#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>

#include <array>

#include <iostream>
#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/NotificationTestUtils.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

namespace {

namespace seq = core::state::sequencer;
namespace tx = test_support::sequencer_transaction;

using BeginOutcome = seq::SequencerPreparedPatternEditBeginOutcome;
using SealOutcome = seq::SequencerPreparedPatternEditSealOutcome;
using CommitOutcome = seq::SequencerPreparedPatternEditCommitOutcome;
using AbortOutcome = seq::SequencerPreparedPatternEditAbortOutcome;
using Owner = seq::SequencerPreparedPatternEditOwner;
using Plan = seq::SequencerCoalescedPatternPayloadPlan;

static_assert(sizeof(core::state::SequencerDomainState::CoalescedPatternHistory) ==
                  (sizeof(void*) == 8U ? 64U : 40U),
              "Prepared-family scalar identity must not increase the existing RAM1 bundle");

constexpr uint8_t kStep = 0U;
constexpr uint8_t kInitialNote = 60U;

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;

    Harness() : state(storages.settings) {
        state.sequencer.pattern.setContentLength(8U);
        state.sequencer.pattern.note[kStep] = kInitialNote;
        assert(seq::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer));
        settle();
    }

    void settle() {
        test_support::drainNotifications();
        state.flushProjectMutationCoalescing();
        test_support::drainNotifications();
        state.flushProjectMutationCoalescing();
        state.acknowledgeProjectSessionSave(state.project.metadata.modifiedCounter);
        assert(!state.hasPendingProjectSessionSave());
        assert(!state.hasPendingSequencerPatternHistoryCoalescing());
    }
};

seq::SequencerHistoryDescriptor descriptor(uint8_t step = kStep) {
    return {
        .kind = seq::SequencerHistoryActionKind::StepEdit,
        .stepIndex = step,
    };
}

BeginOutcome begin(Harness& h, Owner owner, uint8_t key, Plan plan = Plan::FlatOnly) {
    return h.state.beginOrContinueSequencerPreparedPatternEdit(owner, key, plan, descriptor());
}

SealOutcome mutateAndSeal(Harness& h, Owner owner, uint8_t key, uint8_t note) {
    const bool changed = h.state.sequencer.setStepNoteAt(kStep, note);
    return h.state.sealSequencerPreparedPatternEdit(owner, key, changed, descriptor());
}

void authorFullPayload(Harness& h) {
    auto& pattern = h.state.sequencer.pattern;
    assert(seq::ensureGraphRoot(pattern));
    assert(seq::setNodeNoteOffset(pattern, seq::rootStepNodeId(kStep), 5));
    auto* lanes = seq::ensureSequencerCcLaneBank(pattern);
    assert(lanes != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74U;
    assert(seq::createSequencerCcLane(*lanes, 0U, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0U, kStep, 99U).changed());
    pattern.bumpCcLaneRevision();
    assert(seq::storeActiveTrack(h.state.sequencerTracks, h.state.sequencer));
    h.settle();
}

void assertEditorRevisionVector(const Harness& h, const tx::StateInvariant& expected) {
    const auto& pattern = h.state.sequencer.pattern;
    assert(pattern.stepDataRevision.get() == expected.editorStepDataRevision);
    assert(pattern.patternVariationRevision.get() == expected.editorPatternVariationRevision);
    assert(pattern.patternScaleRevision.get() == expected.editorPatternScaleRevision);
    assert(pattern.patternTimingRevision.get() == expected.editorPatternTimingRevision);
    assert(pattern.graphRevision.get() == expected.editorGraphRevision);
    assert(pattern.ccLaneRevision.get() == expected.editorCcRevision);
}

void test_all_eight_owners_publish_one_exact_undo() {
    constexpr std::array owners{
        Owner::PatternPitch,    Owner::PropertySelector, Owner::StepContent,
        Owner::StepEditSession, Owner::StepToggle,       Owner::PatternEditor,
        Owner::PageStructure,   Owner::QuickControls,
    };

    for (uint8_t index = 0U; index < owners.size(); ++index) {
        Harness h;
        const auto owner = owners[index];
        const uint8_t key = static_cast<uint8_t>(index + 1U);
        const uint8_t note = static_cast<uint8_t>(70U + index);
        assert(begin(h, owner, key) == BeginOutcome::Started);
        assert(mutateAndSeal(h, owner, key, note) == SealOutcome::Sealed);
        assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
        assert(h.state.sequencerHistory.undoCount() == 1U);
        assert(h.state.sequencer.pattern.note[kStep] == note);
        assert(h.state.sequencerTracks.track(0U).note[kStep] == note);
        assert(h.state.undoSequencerHistory());
        assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
        assert(h.state.redoSequencerHistory());
        assert(h.state.sequencer.pattern.note[kStep] == note);
    }

    std::cout << "[PASS] all eight prepared owners publish one exact Undo\n";
}

void assert_typed_abort_restores_exact_before(Plan plan, bool afterSeal) {
    Harness h;
    if (plan == Plan::FullCurrentPayload) authorFullPayload(h);

    constexpr auto owner = Owner::PageStructure;
    const uint8_t key = static_cast<uint8_t>(
        32U + static_cast<uint8_t>(plan) * 2U + (afterSeal ? 1U : 0U)
    );
    const auto pageDescriptor = seq::SequencerHistoryDescriptor{
        .kind = seq::SequencerHistoryActionKind::PageStructure,
    };
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(h.state, musicalBefore);
    const auto invariantBefore = tx::captureStateInvariant(h.state);
    const uint8_t focusBefore = h.state.sequencer.focusedStep.get();
    const uint8_t pageBefore = h.state.sequencer.page.get();
    const auto propertyBefore = h.state.sequencer.activeStepProperty.get();

    assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
           AbortOutcome::NoPending);
    assert(h.state.beginOrContinueSequencerPreparedPatternEdit(
               owner, key, plan, pageDescriptor) == BeginOutcome::Started);
    assert(h.state.sequencer.setStepNoteAt(kStep, 78U));
    if (afterSeal) {
        assert(h.state.sealSequencerPreparedPatternEdit(
                   owner, key, true, pageDescriptor) == SealOutcome::Sealed);
    }

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.abortSequencerPreparedPatternEdit(Owner::PatternEditor, key) ==
               AbortOutcome::Failed);
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.abortSequencerPreparedPatternEdit(
                   owner, static_cast<uint8_t>(key + 1U)) == AbortOutcome::Failed);
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
               AbortOutcome::Aborted);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
    tx::assertFailureInjectionReset();

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
           AbortOutcome::NoPending);
    tx::assertMusicalSnapshot(h.state, musicalBefore);
    tx::assertStateInvariant(h.state, invariantBefore);
    assert(h.state.sequencer.focusedStep.get() == focusBefore);
    assert(h.state.sequencer.page.get() == pageBefore);
    assert(h.state.sequencer.activeStepProperty.get() == propertyBefore);
}

void test_typed_abort_is_exact_before_and_after_seal() {
    constexpr std::array plans{
        Plan::FlatOnly,
        Plan::FullCurrentPayload,
        Plan::FullWithProspectiveGraph,
    };
    for (const auto plan : plans) {
        assert_typed_abort_restores_exact_before(plan, false);
        assert_typed_abort_restores_exact_before(plan, true);
    }

    Harness h;
    assert(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        kStep, seq::StepProperty::NOTE, 10U, Plan::FlatOnly));
    assert(h.state.sequencer.setStepNoteAt(kStep, 66U));
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));
    assert(h.state.abortSequencerPreparedPatternEdit(Owner::PageStructure, 1U) ==
           AbortOutcome::Failed);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.commitSequencerPatternHistoryCoalescingOutcome() ==
           seq::SequencerPatternHistoryCommitOutcome::Committed);

    std::cout << "[PASS] typed abort restores exact Before pre/post seal\n";
}

void test_failed_full_abort_is_write_atomic_and_retryable() {
    constexpr auto owner = Owner::PageStructure;
    constexpr auto plan = Plan::FullCurrentPayload;

    {
        Harness h;
        authorFullPayload(h);
        auto replacementGraph = core::app::makeExtmemUnique<
            oc::note::sequencer::StepSequencerGraph>();
        assert(replacementGraph);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto invariantBefore = tx::captureStateInvariant(h.state);

        constexpr uint8_t key = 51U;
        assert(begin(h, owner, key, plan) == BeginOutcome::Started);
        assert(h.state.sequencer.setStepNoteAt(kStep, 77U));
        auto originalGraph = std::move(h.state.sequencer.pattern.graph);
        h.state.sequencer.pattern.graph = std::move(replacementGraph);
        const auto* replacementOwner = h.state.sequencer.pattern.graph.get();
        const uint32_t liveStepRevision =
            h.state.sequencer.pattern.stepDataRevision.get();

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
                   AbortOutcome::Failed);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        tx::assertFailureInjectionReset();
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencer.pattern.graph.get() == replacementOwner);
        assert(h.state.sequencer.pattern.note[kStep] == 77U);
        assert(h.state.sequencer.pattern.stepDataRevision.get() == liveStepRevision);

        replacementGraph = std::move(h.state.sequencer.pattern.graph);
        h.state.sequencer.pattern.graph = std::move(originalGraph);
        assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
               AbortOutcome::Aborted);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        tx::assertStateInvariant(h.state, invariantBefore);
    }

    {
        Harness h;
        authorFullPayload(h);
        auto replacementCc =
            core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
        assert(replacementCc);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto invariantBefore = tx::captureStateInvariant(h.state);

        constexpr uint8_t key = 52U;
        assert(begin(h, owner, key, plan) == BeginOutcome::Started);
        assert(h.state.sequencer.setStepNoteAt(kStep, 78U));
        assert(seq::setNodeNoteOffset(
            h.state.sequencer.pattern, seq::rootStepNodeId(kStep), 11));
        auto originalCc = std::move(h.state.sequencer.pattern.ccLanes);
        h.state.sequencer.pattern.ccLanes = std::move(replacementCc);
        const auto* replacementOwner = h.state.sequencer.pattern.ccLanes.get();
        const uint32_t liveStepRevision =
            h.state.sequencer.pattern.stepDataRevision.get();
        const uint32_t liveGraphRevision =
            h.state.sequencer.pattern.graphRevision.get();

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
                   AbortOutcome::Failed);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        tx::assertFailureInjectionReset();
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencer.pattern.ccLanes.get() == replacementOwner);
        assert(h.state.sequencer.pattern.note[kStep] == 78U);
        assert(h.state.sequencer.pattern.stepDataRevision.get() == liveStepRevision);
        assert(h.state.sequencer.pattern.graphRevision.get() == liveGraphRevision);
        assert(seq::graphView(h.state.sequencer.pattern)
                   ->stepNodes[seq::rootStepNodeId(kStep)]
                   .noteOffset == 11);

        replacementCc = std::move(h.state.sequencer.pattern.ccLanes);
        h.state.sequencer.pattern.ccLanes = std::move(originalCc);
        assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
               AbortOutcome::Aborted);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        tx::assertStateInvariant(h.state, invariantBefore);
    }

    {
        Harness h;
        authorFullPayload(h);
        h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
        auto replacementCc =
            core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
        assert(replacementCc);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto* graphOwner = h.state.sequencer.pattern.graph.get();
        const auto* ccOwner = h.state.sequencer.pattern.ccLanes.get();

        constexpr uint8_t key = 53U;
        assert(begin(h, owner, key, plan) == BeginOutcome::Started);
        assert(h.state.sequencer.setStepNoteAt(kStep, 79U));
        assert(seq::setNodeNoteOffset(
            h.state.sequencer.pattern, seq::rootStepNodeId(kStep), 12));
        assert(seq::switchActiveTrack(
            h.state.sequencerTracks, h.state.sequencer, 1U));
        auto& frozenTrack = h.state.sequencerTracks.track(0U);
        auto originalCc = std::move(frozenTrack.ccLanes);
        frozenTrack.ccLanes = std::move(replacementCc);
        const auto* replacementOwner = frozenTrack.ccLanes.get();
        const uint32_t liveStepRevision = frozenTrack.stepDataRevision.get();
        const uint32_t liveGraphRevision = frozenTrack.graphRevision.get();

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
                   AbortOutcome::Failed);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        tx::assertFailureInjectionReset();
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(frozenTrack.ccLanes.get() == replacementOwner);
        assert(frozenTrack.note[kStep] == 79U);
        assert(frozenTrack.stepDataRevision.get() == liveStepRevision);
        assert(frozenTrack.graphRevision.get() == liveGraphRevision);
        assert(seq::graphView(frozenTrack)
                   ->stepNodes[seq::rootStepNodeId(kStep)]
                   .noteOffset == 12);

        replacementCc = std::move(frozenTrack.ccLanes);
        frozenTrack.ccLanes = std::move(originalCc);
        assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
               AbortOutcome::Aborted);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(frozenTrack.graph.get() == graphOwner);
        assert(frozenTrack.ccLanes.get() == ccOwner);
        seq::SequencerHistoryPatternSnapshot restored;
        assert(seq::captureHistorySnapshot(
            h.state.sequencerTracks, h.state.sequencer, 0U, restored));
        assert(seq::sameMusicalHistorySnapshot(restored, musicalBefore));
    }

    std::cout << "[PASS] failed Full abort is write-atomic and retryable\n";
}

void test_typed_abort_rearms_preexisting_generic_mutation() {
    Harness h;
    h.state.sequencer.focusedStep.set(1U);
    const auto before = tx::captureStateInvariant(h.state);
    constexpr auto owner = Owner::PageStructure;
    constexpr uint8_t key = 47U;

    assert(begin(h, owner, key) == BeginOutcome::Started);
    assert(h.state.sequencer.setStepNoteAt(kStep, 79U));
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.abortSequencerPreparedPatternEdit(owner, key) ==
               AbortOutcome::Aborted);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
    tx::assertFailureInjectionReset();

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(h.state.sequencer.focusedStep.get() == 1U);
    assert(h.state.sequencerHistory.undoCount() == before.sequencerUndoCount);
    assert(h.state.projectHistory.undoCount() == before.projectUndoCount);

    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    const auto after = tx::captureStateInvariant(h.state);
    assert(after.sequencerUndoCount == before.sequencerUndoCount);
    assert(after.projectUndoCount == before.projectUndoCount);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);

    std::cout << "[PASS] typed abort rearms the prior generic mutation\n";
}

void test_stable_continuation_seal_and_commit_allocate_zero() {
    Harness h;
    constexpr auto owner = Owner::PatternEditor;
    constexpr uint8_t key = 2U;
    assert(begin(h, owner, key) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, key, 67U) == SealOutcome::Sealed);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(begin(h, owner, key) == BeginOutcome::Continued);
        assert(mutateAndSeal(h, owner, key, 74U) == SealOutcome::Sealed);
        assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
    tx::assertFailureInjectionReset();

    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 74U);

    std::cout << "[PASS] stable continuation, seal and commit allocate zero\n";
}

void test_full_payload_continuation_seal_and_commit_allocate_zero() {
    Harness h;
    authorFullPayload(h);
    constexpr auto owner = Owner::StepEditSession;
    constexpr uint8_t key = 9U;
    assert(begin(h, owner, key, Plan::FullCurrentPayload) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, key, 68U) == SealOutcome::Sealed);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(begin(h, owner, key, Plan::FullCurrentPayload) == BeginOutcome::Continued);
        assert(mutateAndSeal(h, owner, key, 75U) == SealOutcome::Sealed);
        assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
    tx::assertFailureInjectionReset();

    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(seq::graphView(h.state.sequencer.pattern) != nullptr);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) != nullptr);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 75U);

    std::cout << "[PASS] Full Graph+CC continuation, seal and commit allocate zero\n";
}

void test_flat_edit_preserves_existing_cold_payload_owners() {
    Harness h;
    authorFullPayload(h);
    constexpr auto owner = Owner::StepEditSession;
    constexpr uint8_t key = 12U;
    const auto* editorGraph = h.state.sequencer.pattern.graph.get();
    const auto* editorCc = h.state.sequencer.pattern.ccLanes.get();
    const auto* bankGraph = h.state.sequencerTracks.track(0U).graph.get();
    const auto* bankCc = h.state.sequencerTracks.track(0U).ccLanes.get();

    assert(begin(h, owner, key, Plan::FlatOnly) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, key, 73U) == SealOutcome::Sealed);
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
    assert(h.state.sequencer.pattern.graph.get() == editorGraph);
    assert(h.state.sequencer.pattern.ccLanes.get() == editorCc);
    assert(h.state.sequencerTracks.track(0U).graph.get() == bankGraph);
    assert(h.state.sequencerTracks.track(0U).ccLanes.get() == bankCc);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.graph.get() == editorGraph);
    assert(h.state.sequencer.pattern.ccLanes.get() == editorCc);
    assert(h.state.sequencerTracks.track(0U).graph.get() == bankGraph);
    assert(h.state.sequencerTracks.track(0U).ccLanes.get() == bankCc);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.graph.get() == editorGraph);
    assert(h.state.sequencer.pattern.ccLanes.get() == editorCc);

    std::cout << "[PASS] Flat edit preserves existing Graph+CC owners\n";
}

void test_virgin_no_op_and_exact_net_return_publish_nothing() {
    Harness h;
    constexpr auto owner = Owner::StepEditSession;
    constexpr uint8_t key = 3U;

    assert(begin(h, owner, key) == BeginOutcome::Started);
    assert(h.state.sealSequencerPreparedPatternEdit(owner, key, false, descriptor()) ==
           SealOutcome::Cleared);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);

    assert(begin(h, owner, key) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, key, 71U) == SealOutcome::Sealed);
    assert(begin(h, owner, key) == BeginOutcome::Continued);
    assert(mutateAndSeal(h, owner, key, kInitialNote) == SealOutcome::Cleared);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::NoPending);

    std::cout << "[PASS] virgin no-op and exact net return publish nothing\n";
}

void test_first_begin_failure_is_atomic() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);
    const auto editorNote = h.state.sequencer.pattern.note[kStep];
    const auto bankNote = h.state.sequencerTracks.track(0U).note[kStep];

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(begin(h, Owner::PatternPitch, 4U) == BeginOutcome::Failed);
        tx::assertFailureConsumed(1U);
    }
    tx::assertFailureInjectionReset();

    tx::assertStateInvariant(h.state, before);
    assert(h.state.sequencer.pattern.note[kStep] == editorNote);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == bankNote);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] first begin failure is atomic\n";
}

void test_failed_transition_keeps_exact_prior_entry_only() {
    Harness h;
    constexpr auto firstOwner = Owner::PatternPitch;
    assert(begin(h, firstOwner, 5U) == BeginOutcome::Started);
    assert(mutateAndSeal(h, firstOwner, 5U, 72U) == SealOutcome::Sealed);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(begin(h, Owner::StepToggle, 6U) == BeginOutcome::Failed);
        tx::assertFailureConsumed(1U);
    }
    tx::assertFailureInjectionReset();

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencer.pattern.note[kStep] == 72U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 72U);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);

    std::cout << "[PASS] failed transition retains only the prior entry\n";
}

void test_pattern_editor_inactive_owner_commits_old_bank_track() {
    Harness h;
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    h.state.sequencerTracks.track(1U).note[kStep] = 48U;
    constexpr auto owner = Owner::PatternEditor;
    assert(begin(h, owner, static_cast<uint8_t>(1U)) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, 1U, 79U) == SealOutcome::Sealed);

    assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 1U));
    assert(h.state.sequencer.pattern.note[kStep] == 48U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 79U);
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
    assert(h.state.sequencer.pattern.note[kStep] == 48U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 79U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 48U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == kInitialNote);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 79U);

    std::cout << "[PASS] inactive Pattern Editor commits only its old Track\n";
}

void prepareTwoTrackPatternEditorHarness(Harness& h) {
    assert(h.state.setSharedTrackState(0x0003U, 0U));
    h.state.sequencerTracks.track(1U).note[kStep] = 48U;
    h.settle();
}

void prepareTwoTrackFullPayloadPatternEditorHarness(Harness& h) {
    authorFullPayload(h);
    assert(h.state.setSharedTrackState(0x0003U, 0U));
    h.state.sequencerTracks.track(1U).note[kStep] = 48U;
    h.settle();
}

void assertPatternEditorTrackTransitionResult(Harness& h) {
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    assert(h.state.sequencer.pattern.note[kStep] == 48U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 79U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 48U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == kInitialNote);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 48U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 79U);
}

void test_pattern_editor_set_shared_track_state_is_commit_barrier() {
    Harness h;
    prepareTwoTrackPatternEditorHarness(h);
    constexpr auto owner = Owner::PatternEditor;
    assert(begin(h, owner, 1U) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, 1U, 79U) == SealOutcome::Sealed);

    assert(h.state.setSharedTrackState(0x0003U, 1U));
    assertPatternEditorTrackTransitionResult(h);

    std::cout << "[PASS] setSharedTrackState is a Pattern Editor commit barrier\n";
}

void test_pattern_editor_macro_pages_refresh_is_commit_barrier() {
    Harness h;
    prepareTwoTrackPatternEditorHarness(h);
    constexpr auto owner = Owner::PatternEditor;
    assert(begin(h, owner, 1U) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, 1U, 79U) == SealOutcome::Sealed);

    h.state.pages.syncSharedTrackState(0x0003U, 1U);
    assert(h.state.refreshSharedTrackStateFromMacroPages());
    assertPatternEditorTrackTransitionResult(h);

    std::cout << "[PASS] Macro Pages refresh is a Pattern Editor commit barrier\n";
}

void test_pattern_editor_sequencer_refresh_is_commit_barrier() {
    Harness h;
    prepareTwoTrackPatternEditorHarness(h);
    constexpr auto owner = Owner::PatternEditor;
    assert(begin(h, owner, 1U) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, 1U, 79U) == SealOutcome::Sealed);

    assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 1U));
    assert(h.state.refreshSharedTrackStateFromSequencer());
    assertPatternEditorTrackTransitionResult(h);

    std::cout << "[PASS] Sequencer refresh is a Pattern Editor commit barrier\n";
}

void test_pattern_editor_inactive_full_payload_keeps_exact_owner() {
    Harness h;
    prepareTwoTrackFullPayloadPatternEditorHarness(h);
    constexpr auto owner = Owner::PatternEditor;
    const auto* graphOwner = h.state.sequencer.pattern.graph.get();
    const auto* ccOwner = h.state.sequencer.pattern.ccLanes.get();
    assert(graphOwner != nullptr);
    assert(ccOwner != nullptr);

    assert(begin(h, owner, 1U, Plan::FullCurrentPayload) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, 1U, 79U) == SealOutcome::Sealed);
    assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 1U));
    assert(h.state.sequencerTracks.track(0U).graph.get() == graphOwner);
    assert(h.state.sequencerTracks.track(0U).ccLanes.get() == ccOwner);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
    tx::assertFailureInjectionReset();

    assert(h.state.sequencer.pattern.note[kStep] == 48U);
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
    assert(seq::sequencerCcLaneView(h.state.sequencer.pattern) == nullptr);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 48U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == kInitialNote);
    assert(seq::graphView(h.state.sequencerTracks.track(0U)) != nullptr);
    assert(seq::sequencerCcLaneView(h.state.sequencerTracks.track(0U)) != nullptr);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 79U);

    std::cout << "[PASS] inactive Full payload keeps the sealed old owner\n";
}

void test_inactive_commit_preserves_new_track_generic_obligation() {
    Harness h;
    prepareTwoTrackPatternEditorHarness(h);
    constexpr auto owner = Owner::PatternEditor;
    assert(begin(h, owner, 1U) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, 1U, 79U) == SealOutcome::Sealed);
    assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 1U));
    assert(h.state.sequencer.setStepNoteAt(kStep, 55U));
    test_support::drainNotifications();

    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
    assert(h.state.hasPendingProjectMutationCoalescing());
    assert(h.state.sequencerTracks.track(1U).note[kStep] == 48U);
    h.state.flushProjectMutationCoalescing();
    assert(h.state.sequencerTracks.track(1U).note[kStep] == 55U);
    assert(h.state.sequencerHistory.undoCount() == 1U);

    std::cout << "[PASS] inactive commit preserves the new Track obligation\n";
}

void test_pattern_editor_rejects_inactive_payload_owner_substitution() {
    Harness h;
    prepareTwoTrackFullPayloadPatternEditorHarness(h);
    constexpr auto owner = Owner::PatternEditor;
    assert(begin(h, owner, 1U, Plan::FullCurrentPayload) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, 1U, 79U) == SealOutcome::Sealed);
    assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 1U));

    auto& oldTrack = h.state.sequencerTracks.track(0U);
    auto sealedOwner = std::move(oldTrack.graph);
    assert(sealedOwner != nullptr);
    oldTrack.graph =
        core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>(*sealedOwner);
    assert(oldTrack.graph != nullptr);
    assert(oldTrack.graph.get() != sealedOwner.get());
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Failed);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);

    oldTrack.graph = std::move(sealedOwner);
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
    assert(h.state.sequencerHistory.undoCount() == 1U);

    std::cout << "[PASS] inactive payload owner substitution is rejected\n";
}

void test_pattern_editor_track_index_aba_requires_exact_payload_owner() {
    Harness h;
    prepareTwoTrackFullPayloadPatternEditorHarness(h);
    constexpr auto owner = Owner::PatternEditor;
    const auto* graphOwner = h.state.sequencer.pattern.graph.get();
    const auto* ccOwner = h.state.sequencer.pattern.ccLanes.get();
    assert(begin(h, owner, 1U, Plan::FullCurrentPayload) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, 1U, 79U) == SealOutcome::Sealed);

    assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 1U));
    assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 0U));
    assert(h.state.sequencer.pattern.graph.get() == graphOwner);
    assert(h.state.sequencer.pattern.ccLanes.get() == ccOwner);
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencer.pattern.note[kStep] == 79U);
    assert(h.state.sequencerTracks.track(1U).note[kStep] == 48U);

    std::cout << "[PASS] Track index ABA retains exact payload identity\n";
}

void test_quick_controls_owner_replacement_keeps_identity_guards_strict() {
    constexpr auto owner = Owner::QuickControls;
    constexpr uint8_t key = 0U;

    {
        Harness h;
        assert(begin(h, owner, key, Plan::FullCurrentPayload) == BeginOutcome::Started);
        assert(seq::ensureGraphRoot(h.state.sequencer.pattern));
        assert(h.state.sequencer.setStepNoteAt(kStep, 79U));
        assert(h.state.sealSequencerPreparedPatternEdit(
                   owner, key, true, descriptor()) == SealOutcome::FailedClosed);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
        assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }

    {
        Harness h;
        prepareTwoTrackFullPayloadPatternEditorHarness(h);
        assert(begin(h, owner, key, Plan::FullCurrentPayload) == BeginOutcome::Started);
        assert(h.state.sequencer.setStepNoteAt(kStep, 79U));
        assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 1U));
        assert(h.state.sealSequencerPreparedPatternEdit(
                   owner, key, true, descriptor()) == SealOutcome::FailedClosed);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencer.pattern.note[kStep] == 48U);
        assert(h.state.sequencerTracks.track(0U).note[kStep] == kInitialNote);
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }

    std::cout << "[PASS] Quick owner replacement keeps topology and Track identity strict\n";
}

void test_post_write_plan_failure_rolls_back_and_unwedges_owner() {
    Harness h;
    constexpr auto owner = Owner::StepEditSession;
    constexpr uint8_t key = 7U;
    const auto before = tx::captureStateInvariant(h.state);
    assert(begin(h, owner, key, Plan::FlatOnly) == BeginOutcome::Started);
    assert(seq::ensureGraphRoot(h.state.sequencer.pattern));
    assert(h.state.sealSequencerPreparedPatternEdit(owner, key, true, descriptor()) ==
           SealOutcome::FailedClosed);
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    tx::assertStateInvariant(h.state, before);

    assert(begin(h, owner, key, Plan::FlatOnly) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, key, 76U) == SealOutcome::Sealed);
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
    assert(h.state.sequencer.pattern.note[kStep] == 76U);

    std::cout << "[PASS] post-write plan failure rolls back and unwedges owner\n";
}

void test_full_post_write_failure_rolls_back_without_allocation() {
    Harness h;
    authorFullPayload(h);
    constexpr auto owner = Owner::StepEditSession;
    constexpr uint8_t key = 10U;
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(h.state, musicalBefore);
    const auto invariantBefore = tx::captureStateInvariant(h.state);
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    const auto* graphOwner = h.state.sequencer.pattern.graph.get();
    const auto* ccOwner = h.state.sequencer.pattern.ccLanes.get();

    assert(begin(h, owner, key, Plan::FullCurrentPayload) == BeginOutcome::Started);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.sequencer.setStepNoteAt(kStep, 76U));
        assert(seq::switchActiveTrack(
            h.state.sequencerTracks, h.state.sequencer, 1U));
        assert(h.state.sealSequencerPreparedPatternEdit(owner, key, true, descriptor()) ==
               SealOutcome::FailedClosed);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
    tx::assertFailureInjectionReset();

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    const auto& restoredTrack = h.state.sequencerTracks.track(0U);
    assert(restoredTrack.graph.get() == graphOwner);
    assert(restoredTrack.ccLanes.get() == ccOwner);
    assert(restoredTrack.stepDataRevision.get() == invariantBefore.editorStepDataRevision);
    assert(restoredTrack.patternVariationRevision.get() ==
           invariantBefore.editorPatternVariationRevision);
    assert(restoredTrack.patternScaleRevision.get() ==
           invariantBefore.editorPatternScaleRevision);
    assert(restoredTrack.patternTimingRevision.get() ==
           invariantBefore.editorPatternTimingRevision);
    assert(restoredTrack.graphRevision.get() == invariantBefore.editorGraphRevision);
    assert(restoredTrack.ccLaneRevision.get() == invariantBefore.editorCcRevision);
    seq::SequencerHistoryPatternSnapshot restoredMusical;
    assert(seq::captureHistorySnapshot(
        h.state.sequencerTracks, h.state.sequencer, 0U, restoredMusical));
    assert(seq::sameMusicalHistorySnapshot(restoredMusical, musicalBefore));

    assert(seq::switchActiveTrack(
        h.state.sequencerTracks, h.state.sequencer, 0U));
    tx::assertMusicalSnapshot(h.state, musicalBefore);
    assert(h.state.sequencer.pattern.graph.get() == graphOwner);
    assert(h.state.sequencer.pattern.ccLanes.get() == ccOwner);

    assert(begin(h, owner, key, Plan::FullCurrentPayload) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, key, 76U) == SealOutcome::Sealed);
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);

    std::cout << "[PASS] Full post-write failure rolls back allocation-free\n";
}

void test_full_continuation_failure_rolls_back_whole_transaction() {
    Harness h;
    authorFullPayload(h);
    constexpr auto owner = Owner::StepEditSession;
    constexpr uint8_t key = 13U;
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(h.state, musicalBefore);
    const auto invariantBefore = tx::captureStateInvariant(h.state);
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    const auto* graphOwner = h.state.sequencer.pattern.graph.get();
    const auto* ccOwner = h.state.sequencer.pattern.ccLanes.get();

    assert(begin(h, owner, key, Plan::FullCurrentPayload) == BeginOutcome::Started);
    assert(mutateAndSeal(h, owner, key, 68U) == SealOutcome::Sealed);
    assert(begin(h, owner, key, Plan::FullCurrentPayload) == BeginOutcome::Continued);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.sequencer.setStepNoteAt(kStep, 76U));
        assert(seq::switchActiveTrack(
            h.state.sequencerTracks, h.state.sequencer, 1U));
        assert(h.state.sealSequencerPreparedPatternEdit(owner, key, true, descriptor()) ==
               SealOutcome::FailedClosed);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
    tx::assertFailureInjectionReset();

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    const auto& restoredTrack = h.state.sequencerTracks.track(0U);
    assert(restoredTrack.graph.get() == graphOwner);
    assert(restoredTrack.ccLanes.get() == ccOwner);
    assert(restoredTrack.stepDataRevision.get() == invariantBefore.editorStepDataRevision);
    assert(restoredTrack.patternVariationRevision.get() ==
           invariantBefore.editorPatternVariationRevision);
    assert(restoredTrack.patternScaleRevision.get() ==
           invariantBefore.editorPatternScaleRevision);
    assert(restoredTrack.patternTimingRevision.get() ==
           invariantBefore.editorPatternTimingRevision);
    assert(restoredTrack.graphRevision.get() == invariantBefore.editorGraphRevision);
    assert(restoredTrack.ccLaneRevision.get() == invariantBefore.editorCcRevision);
    seq::SequencerHistoryPatternSnapshot restoredMusical;
    assert(seq::captureHistorySnapshot(
        h.state.sequencerTracks, h.state.sequencer, 0U, restoredMusical));
    assert(seq::sameMusicalHistorySnapshot(restoredMusical, musicalBefore));

    assert(seq::switchActiveTrack(
        h.state.sequencerTracks, h.state.sequencer, 0U));
    tx::assertMusicalSnapshot(h.state, musicalBefore);
    assert(h.state.sequencer.pattern.graph.get() == graphOwner);
    assert(h.state.sequencer.pattern.ccLanes.get() == ccOwner);

    std::cout << "[PASS] Full continuation failure rolls back the transaction\n";
}

void test_prospective_graph_partial_no_op_rolls_back_exactly() {
    Harness h;
    constexpr auto owner = Owner::StepEditSession;
    constexpr uint8_t key = 14U;
    const auto before = tx::captureStateInvariant(h.state);

    assert(begin(h, owner, key, Plan::FullWithProspectiveGraph) == BeginOutcome::Started);
    assert(h.state.sequencer.pattern.graph != nullptr);
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        // Model a mutator that initializes the reserved Graph, then discovers
        // that its requested local-variation value is already the default.
        assert(seq::ensureGraphRoot(h.state.sequencer.pattern));
        assert(!seq::setNodeLocalVariationRange(
            h.state.sequencer.pattern, seq::rootStepNodeId(kStep), seq::StepProperty::NOTE, 0U));
        assert(h.state.sealSequencerPreparedPatternEdit(owner, key, false, descriptor()) ==
               SealOutcome::Cleared);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    tx::assertFailureInjectionReset();

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.graph == nullptr);
    tx::assertStateInvariant(h.state, before);

    std::cout << "[PASS] prospective Graph partial no-op rolls back exactly\n";
}

void test_malformed_compaction_rolls_back_preserving_payload_owners() {
    Harness h;
    authorFullPayload(h);
    constexpr auto owner = Owner::StepContent;
    constexpr uint8_t key = 15U;
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(h.state, musicalBefore);
    const auto before = tx::captureStateInvariant(h.state);

    const auto* graphOwner = h.state.sequencer.pattern.graph.get();
    const auto* ccOwner = h.state.sequencer.pattern.ccLanes.get();
    assert(graphOwner != nullptr);
    assert(ccOwner != nullptr);
    assert(h.state.beginOrContinueSequencerPreparedPatternEdit(
               owner, key, Plan::FullCurrentPayload, descriptor(), true) == BeginOutcome::Started);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.sequencer.setStepNoteAt(kStep, 76U));
        auto& graph = *h.state.sequencer.pattern.graph;
        auto& root = graph.stepNodes[seq::rootStepNodeId(kStep)];
        root.flags =
            static_cast<uint16_t>(root.flags | oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE);
        root.childSequenceId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
        h.state.sequencer.pattern.bumpGraphRevision();

        assert(h.state.sealSequencerPreparedPatternEdit(owner, key, true, descriptor()) ==
               SealOutcome::FailedClosed);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    tx::assertFailureInjectionReset();

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.graph.get() == graphOwner);
    assert(h.state.sequencer.pattern.ccLanes.get() == ccOwner);
    tx::assertMusicalSnapshot(h.state, musicalBefore);
    tx::assertStateInvariant(h.state, before);

    std::cout << "[PASS] malformed compaction rollback preserves Graph+CC owners\n";
}

void test_unused_prospective_graph_is_released_before_publication() {
    Harness h;
    constexpr auto owner = Owner::StepContent;
    constexpr uint8_t key = 8U;
    assert(begin(h, owner, key, Plan::FullWithProspectiveGraph) == BeginOutcome::Started);
    assert(h.state.sequencer.pattern.graph != nullptr);
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);

    assert(mutateAndSeal(h, owner, key, 77U) == SealOutcome::Sealed);
    assert(h.state.sequencer.pattern.graph == nullptr);
    assert(h.state.commitSequencerPreparedPatternEdit(owner) == CommitOutcome::Committed);
    assert(h.state.sequencer.pattern.graph == nullptr);
    assert(h.state.sequencerTracks.track(0U).graph == nullptr);

    std::cout << "[PASS] unused prospective Graph is released before publish\n";
}

}  // namespace

int main() {
    test_all_eight_owners_publish_one_exact_undo();
    test_typed_abort_is_exact_before_and_after_seal();
    test_failed_full_abort_is_write_atomic_and_retryable();
    test_typed_abort_rearms_preexisting_generic_mutation();
    test_stable_continuation_seal_and_commit_allocate_zero();
    test_full_payload_continuation_seal_and_commit_allocate_zero();
    test_flat_edit_preserves_existing_cold_payload_owners();
    test_virgin_no_op_and_exact_net_return_publish_nothing();
    test_first_begin_failure_is_atomic();
    test_failed_transition_keeps_exact_prior_entry_only();
    test_pattern_editor_inactive_owner_commits_old_bank_track();
    test_pattern_editor_set_shared_track_state_is_commit_barrier();
    test_pattern_editor_macro_pages_refresh_is_commit_barrier();
    test_pattern_editor_sequencer_refresh_is_commit_barrier();
    test_pattern_editor_inactive_full_payload_keeps_exact_owner();
    test_inactive_commit_preserves_new_track_generic_obligation();
    test_pattern_editor_rejects_inactive_payload_owner_substitution();
    test_pattern_editor_track_index_aba_requires_exact_payload_owner();
    test_quick_controls_owner_replacement_keeps_identity_guards_strict();
    test_post_write_plan_failure_rolls_back_and_unwedges_owner();
    test_full_post_write_failure_rolls_back_without_allocation();
    test_full_continuation_failure_rolls_back_whole_transaction();
    test_prospective_graph_partial_no_op_rolls_back_exactly();
    test_malformed_compaction_rolls_back_preserving_payload_owners();
    test_unused_prospective_graph_is_released_before_publication();

    std::cout << "\nAll prepared Pattern family tests passed.\n";
    return 0;
}
