#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <array>

#include <iostream>
#include <new>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/NotificationTestUtils.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

namespace allocation_trace {

// Native-only observation seam for the exact LOCK-P request order. The
// fail-Nth seam below remains the no-allocation/failure-atomicity authority.
constexpr std::size_t MAX_REQUESTS = 8U;
bool enabled = false;
std::array<std::size_t, MAX_REQUESTS> requests{};
std::size_t count = 0U;
bool overflow = false;

void record(std::size_t bytes) {
    if (!enabled) return;
    if (count >= requests.size()) {
        overflow = true;
        return;
    }
    requests[count++] = bytes;
}

class Scope {
public:
    Scope() {
        requests.fill(0U);
        count = 0U;
        overflow = false;
        enabled = true;
    }

    ~Scope() { enabled = false; }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace allocation_trace

void* operator new(std::size_t bytes) {
    allocation_trace::record(bytes);
    if (void* memory = std::malloc(bytes)) return memory;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }

void operator delete(void* memory) noexcept { std::free(memory); }

void operator delete[](void* memory) noexcept { ::operator delete(memory); }

void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }

void operator delete[](void* memory, std::size_t) noexcept { ::operator delete[](memory); }

namespace {

namespace seq = core::state::sequencer;
namespace tx = test_support::sequencer_transaction;

using PayloadPlan = seq::SequencerCoalescedPatternPayloadPlan;

constexpr uint8_t kStep = 0U;
constexpr uint8_t kInitialNote = 60U;
constexpr std::size_t kAllocationHeaderBytes = 16U;
constexpr std::size_t kPatternChangeBytes = 1736U;
constexpr std::size_t kGraphBytes = 14792U;
constexpr std::size_t kCcBankBytes = 840U;

// These are the frozen ARM payload anchors. Native MinGW has different
// pointer/alignment cost for Pattern Change, so request and retained-byte
// observations below deliberately use sizeof(actual native type).
static_assert(kPatternChangeBytes + 3U * (kGraphBytes + kCcBankBytes) +
                      7U * kAllocationHeaderBytes ==
                  48744U,
              "LOCK-P maximum prepared coalesced Pattern peak changed");

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

enum class InitialPayload : uint8_t {
    None = 0U,
    GraphOnly,
    DisabledGraphOnly,
    CcOnly,
    GraphAndCc,
};

bool hasInitialGraph(InitialPayload payload) {
    return payload == InitialPayload::GraphOnly || payload == InitialPayload::GraphAndCc;
}

bool ownsInitialGraph(InitialPayload payload) {
    return hasInitialGraph(payload) || payload == InitialPayload::DisabledGraphOnly;
}

bool hasInitialCc(InitialPayload payload) {
    return payload == InitialPayload::CcOnly || payload == InitialPayload::GraphAndCc;
}

struct ExpectedAllocationRequests {
    std::array<std::size_t, allocation_trace::MAX_REQUESTS> bytes{};
    std::size_t count = 0U;

    void push(std::size_t bytesToAllocate) {
        assert(count < bytes.size());
        bytes[count++] = bytesToAllocate;
    }
};

ExpectedAllocationRequests expectedBeginAllocationRequests(PayloadPlan plan,
                                                           InitialPayload initialPayload) {
    ExpectedAllocationRequests expected;
    expected.push(sizeof(seq::SequencerHistoryPatternChange));
    if (plan == PayloadPlan::FlatOnly) return expected;

    const bool prospectiveGraph = plan == PayloadPlan::FullWithProspectiveGraph;
    const bool finalGraph = hasInitialGraph(initialPayload) || prospectiveGraph;
    const bool firstGraphOwner =
        hasInitialGraph(initialPayload) || (prospectiveGraph && !ownsInitialGraph(initialPayload));

    if (firstGraphOwner) { expected.push(sizeof(oc::note::sequencer::StepSequencerGraph)); }
    if (hasInitialCc(initialPayload)) { expected.push(sizeof(seq::SequencerCcLaneBank)); }
    if (finalGraph) expected.push(sizeof(oc::note::sequencer::StepSequencerGraph));
    if (hasInitialCc(initialPayload)) { expected.push(sizeof(seq::SequencerCcLaneBank)); }
    if (finalGraph) expected.push(sizeof(oc::note::sequencer::StepSequencerGraph));
    if (hasInitialCc(initialPayload)) { expected.push(sizeof(seq::SequencerCcLaneBank)); }
    return expected;
}

void assertAllocationRequests(const ExpectedAllocationRequests& expected) {
    assert(!allocation_trace::overflow);
    assert(allocation_trace::count == expected.count);
    for (std::size_t index = 0U; index < expected.count; ++index) {
        assert(allocation_trace::requests[index] == expected.bytes[index]);
    }
}

std::size_t expectedRetainedBytes(PayloadPlan plan, InitialPayload initialPayload) {
    std::size_t bytes = sizeof(seq::SequencerHistoryPatternChange) + kAllocationHeaderBytes;
    if (plan == PayloadPlan::FlatOnly) return bytes;

    const std::size_t graphOwners = hasInitialGraph(initialPayload)
                                        ? 2U
                                        : (plan == PayloadPlan::FullWithProspectiveGraph ? 1U : 0U);
    const std::size_t ccOwners = hasInitialCc(initialPayload) ? 2U : 0U;
    bytes +=
        graphOwners * (sizeof(oc::note::sequencer::StepSequencerGraph) + kAllocationHeaderBytes);
    bytes += ccOwners * (sizeof(seq::SequencerCcLaneBank) + kAllocationHeaderBytes);
    return bytes;
}

void assertCommittedOwnerIdentities(const core::state::CoreState& state,
                                    const tx::StateInvariant& before, PayloadPlan plan,
                                    InitialPayload initialPayload) {
    const auto after = tx::captureStateInvariant(state);
    if (plan == PayloadPlan::FlatOnly) {
        assert(after.editorGraphOwner == before.editorGraphOwner);
        assert(after.editorCcOwner == before.editorCcOwner);
        assert(after.bankGraphOwner == before.bankGraphOwner);
        assert(after.bankCcOwner == before.bankCcOwner);
        return;
    }

    const bool finalGraph =
        hasInitialGraph(initialPayload) || plan == PayloadPlan::FullWithProspectiveGraph;
    if (ownsInitialGraph(initialPayload)) {
        assert(after.editorGraphOwner == before.editorGraphOwner);
    } else if (finalGraph) {
        assert(before.editorGraphOwner == nullptr);
        assert(after.editorGraphOwner != nullptr);
    } else {
        assert(after.editorGraphOwner == before.editorGraphOwner);
    }

    if (finalGraph) {
        assert(after.bankGraphOwner != nullptr);
        assert(after.bankGraphOwner != before.bankGraphOwner);
        assert(after.bankGraphOwner != after.editorGraphOwner);
    } else {
        assert(after.bankGraphOwner == before.bankGraphOwner);
    }

    assert(after.editorCcOwner == before.editorCcOwner);
    if (hasInitialCc(initialPayload)) {
        assert(after.bankCcOwner != nullptr);
        assert(after.bankCcOwner != before.bankCcOwner);
        assert(after.bankCcOwner != after.editorCcOwner);
    } else {
        assert(after.bankCcOwner == before.bankCcOwner);
    }
}

seq::SequencerCcLaneBankPtr stageCcLaneEvent(const core::state::CoreState& state, uint8_t value) {
    seq::SequencerCcLaneBankPtr staged;
    assert(
        seq::cloneSequencerCcLaneBank(staged, seq::sequencerCcLaneView(state.sequencer.pattern)));
    assert(staged != nullptr);
    assert(seq::setSequencerCcLaneEvent(*staged, 0U, kStep, value).changed());
    return staged;
}

uint8_t editorCcLaneEventValue(const core::state::CoreState& state) {
    const auto* lanes = seq::sequencerCcLaneView(state.sequencer.pattern);
    assert(lanes != nullptr);
    assert(lanes->lanes[0U].activeMask.test(kStep));
    return lanes->lanes[0U].values[kStep];
}

uint8_t bankCcLaneEventValue(const core::state::CoreState& state) {
    const auto* lanes = seq::sequencerCcLaneView(state.sequencerTracks.track(0U));
    assert(lanes != nullptr);
    assert(lanes->lanes[0U].activeMask.test(kStep));
    return lanes->lanes[0U].values[kStep];
}

void initializePayload(Harness& h, InitialPayload payload) {
    auto& pattern = h.state.sequencer.pattern;
    if (hasInitialGraph(payload)) {
        assert(seq::ensureGraphRoot(pattern));
        assert(seq::setNodeLocalVariationRange(pattern, seq::rootStepNodeId(kStep),
                                               seq::StepProperty::NOTE, 2U));
    } else if (payload == InitialPayload::DisabledGraphOnly) {
        pattern.graph = core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>();
        assert(pattern.graph != nullptr);
        assert(seq::graphView(pattern) == nullptr);
    }
    if (hasInitialCc(payload)) {
        auto* lanes = seq::ensureSequencerCcLaneBank(pattern);
        assert(lanes != nullptr);
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = 74U;
        assert(seq::createSequencerCcLane(*lanes, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(*lanes, 0U, kStep, 91U).changed());
        pattern.bumpCcLaneRevision();
    }

    assert(seq::initializeTrackBankFromActive(h.state.sequencerTracks, h.state.sequencer));
    h.settle();

    assert((pattern.graph != nullptr) == ownsInitialGraph(payload));
    assert((seq::graphView(pattern) != nullptr) == hasInitialGraph(payload));
    assert((seq::sequencerCcLaneView(pattern) != nullptr) == hasInitialCc(payload));
}

void assertOnePublicationAfterQueuedNotifications(Harness& h, const tx::StateInvariant& before) {
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();

    const auto after = tx::captureStateInvariant(h.state);
    assert(after.sequencerUndoCount == before.sequencerUndoCount + 1U);
    assert(after.sequencerRedoCount == 0U);
    assert(after.projectUndoCount == before.projectUndoCount + 1U);
    assert(after.projectRedoCount == 0U);
    assert(after.sequencerUndoIdentity != 0U);
    assert(after.sequencerUndoIdentity != before.sequencerUndoIdentity);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);
}

void assertEditorAndActiveTrackRevisionsMatch(const Harness& h) {
    const auto& editor = h.state.sequencer.pattern;
    const auto& activeTrack = h.state.sequencerTracks.track(0U);
    assert(editor.stepDataRevision.get() == activeTrack.stepDataRevision.get());
    assert(editor.patternVariationRevision.get() == activeTrack.patternVariationRevision.get());
    assert(editor.patternScaleRevision.get() == activeTrack.patternScaleRevision.get());
    assert(editor.patternTimingRevision.get() == activeTrack.patternTimingRevision.get());
    assert(editor.graphRevision.get() == activeTrack.graphRevision.get());
    assert(editor.ccLaneRevision.get() == activeTrack.ccLaneRevision.get());
}

void beginFlat(core::state::CoreState& state, uint32_t nowMs) {
    assert(seq::sequencerHistoryOpenAccepted(state.beginOrContinueSequencerPatternHistoryCoalescing(kStep, seq::StepProperty::NOTE,
                                                                  nowMs, PayloadPlan::FlatOnly)));
}

void mutateFlatAndSeal(core::state::CoreState& state, uint8_t note) {
    assert(state.sequencer.setStepNoteAt(kStep, note));
    assert(state.sealSequencerPatternHistoryCoalescing(true));
}

void test_flat_begin_seal_commit_is_exact_and_undoable() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 72U);

    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == before.sequencerUndoCount);
    assert(h.state.commitSequencerPatternHistoryCoalescing());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[kStep] == 72U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 72U);
    assertOnePublicationAfterQueuedNotifications(h, before);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == kInitialNote);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 72U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 72U);

    std::cout << "[PASS] Flat begin/seal/commit is exact and undoable\n";
}

void test_typed_domain_commit_adapter_distinguishes_failure_from_empty_boundary() {
    Harness h;
    using Outcome = seq::SequencerPatternHistoryCommitOutcome;
    using AbortOutcome = seq::SequencerPreparedPatternEditAbortOutcome;
    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(h.state);

    assert(history.commitCoalescedPatternEditOutcome() == Outcome::NoPending);
    assert(core::handler::SequencerHistoryDomainServices{}.commitCoalescedPatternEditOutcome() ==
           Outcome::Failed);
    assert(core::handler::SequencerHistoryDomainServices{}.abortPreparedPatternEdit(
               seq::SequencerPreparedPatternEditOwner::PageStructure, 21U) ==
           AbortOutcome::Failed);
    assert(history.abortPreparedPatternEdit(
               seq::SequencerPreparedPatternEditOwner::PageStructure, 21U) ==
           AbortOutcome::NoPending);

    const auto beforeAbort = tx::captureStateInvariant(h.state);
    const auto pageDescriptor = seq::SequencerHistoryDescriptor{
        .kind = seq::SequencerHistoryActionKind::PageStructure,
    };
    assert(history.beginPreparedPatternEdit(
               seq::SequencerPreparedPatternEditOwner::PageStructure,
               21U,
               seq::SequencerCoalescedPatternPayloadPlan::FlatOnly,
               pageDescriptor) == seq::SequencerPreparedPatternEditBeginOutcome::Started);
    assert(h.state.sequencer.setStepNoteAt(kStep, 69U));
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        allocation_trace::Scope allocations;
        assert(history.abortPreparedPatternEdit(
                   seq::SequencerPreparedPatternEditOwner::PageStructure, 21U) ==
               AbortOutcome::Aborted);
        assert(allocation_trace::count == 0U);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    tx::assertFailureInjectionReset();
    tx::assertStateInvariant(h.state, beforeAbort);
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);

    // An interrupted begin is a deterministic malformed boundary. Committing
    // it must report Failed, retain the pending owner for recovery, and perform
    // no hidden allocation while doing so.
    beginFlat(h.state, 100U);
#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(history.commitCoalescedPatternEditOutcome() == Outcome::Failed);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    tx::assertFailureInjectionReset();
#else
    assert(history.commitCoalescedPatternEditOutcome() == Outcome::Failed);
#endif
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

    // The exact no-op rollback remains retryable after the failed barrier.
    assert(h.state.sealSequencerPatternHistoryCoalescing(false));
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    beginFlat(h.state, 200U);
    mutateFlatAndSeal(h.state, 74U);
    assert(history.commitCoalescedPatternEditOutcome() == Outcome::Committed);
    assert(history.commitCoalescedPatternEditOutcome() == Outcome::NoPending);
    assert(h.state.sequencer.pattern.note[kStep] == 74U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 74U);
    assert(h.state.sequencerHistory.undoCount() == 1U);

    std::cout
        << "[PASS] typed Domain commit adapter distinguishes Failed/empty/committed boundaries\n";
}

void test_core_state_flush_commits_pending_step_without_allocation_and_global_redoes() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 71U);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.state.flush();
        tx::assertMaxPlusOneStillArmed(0U);
    }
#else
    h.state.flush();
#endif

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[kStep] == 71U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 71U);
    assertOnePublicationAfterQueuedNotifications(h, before);
    assert(h.state.sequencerHistory.retainedBytes() ==
           before.retainedBytes +
               expectedRetainedBytes(PayloadPlan::FlatOnly, InitialPayload::None));

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == kInitialNote);
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 71U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 71U);

    std::cout << "[PASS] CoreState::flush is allocation-free and Global Redo exact\n";
}

void test_same_key_continuation_and_commit_allocate_nothing() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 61U);

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        beginFlat(h.state, 200U);
        mutateFlatAndSeal(h.state, 62U);
        assert(h.state.commitSequencerPatternHistoryCoalescing());
        tx::assertMaxPlusOneStillArmed(0U);
    }
#else
    beginFlat(h.state, 200U);
    mutateFlatAndSeal(h.state, 62U);
    assert(h.state.commitSequencerPatternHistoryCoalescing());
#endif

    assertEditorAndActiveTrackRevisionsMatch(h);
    assertOnePublicationAfterQueuedNotifications(h, before);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 62U);

    std::cout << "[PASS] same-key continuation and commit allocate nothing\n";
}

void test_same_key_no_op_keeps_the_last_real_after_and_refreshes_timeout() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 64U);

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        beginFlat(h.state, 200U);
        assert(h.state.sealSequencerPatternHistoryCoalescing(false));
        tx::assertMaxPlusOneStillArmed(0U);
    }
#else
    beginFlat(h.state, 200U);
    assert(h.state.sealSequencerPatternHistoryCoalescing(false));
#endif

    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[kStep] == 64U);
    assert(!h.state.updateSequencerPatternHistoryCoalescing(699U));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.updateSequencerPatternHistoryCoalescing(700U));
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assertOnePublicationAfterQueuedNotifications(h, before);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);

    std::cout << "[PASS] same-key no-op preserves the real after and refreshes timeout\n";
}

void test_same_key_payload_plan_drift_is_rejected_without_publication() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 63U);
    const auto sealed = tx::captureStateInvariant(h.state);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
            kStep, seq::StepProperty::NOTE, 200U, PayloadPlan::FullCurrentPayload) ==
               seq::SequencerHistoryOpenOutcome::Blocked);
        tx::assertMaxPlusOneStillArmed(0U);
    }
#else
    assert(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        kStep, seq::StepProperty::NOTE, 200U, PayloadPlan::FullCurrentPayload) ==
           seq::SequencerHistoryOpenOutcome::Blocked);
#endif

    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    tx::assertStateInvariant(h.state, sealed);
    assert(h.state.sequencer.pattern.note[kStep] == 63U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == kInitialNote);

    assert(h.state.commitSequencerPatternHistoryCoalescing());
    assertOnePublicationAfterQueuedNotifications(h, before);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 63U);

    std::cout << "[PASS] same-key payload-plan drift rejects without publication\n";
}

void test_return_to_before_cancels_the_session_exactly() {
    Harness h;
    const auto invariantBefore = tx::captureStateInvariant(h.state);
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(h.state, musicalBefore);
    const uint32_t stepDataRevisionBefore = h.state.sequencer.pattern.stepDataRevision.get();
    const uint32_t bankStepDataRevisionBefore =
        h.state.sequencerTracks.track(0U).stepDataRevision.get();

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 65U);
    beginFlat(h.state, 200U);
    mutateFlatAndSeal(h.state, kInitialNote);

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(!h.state.commitSequencerPatternHistoryCoalescing());
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();

    tx::assertStateInvariant(h.state, invariantBefore);
    tx::assertMusicalSnapshot(h.state, musicalBefore);
    assert(h.state.sequencer.pattern.stepDataRevision.get() == stepDataRevisionBefore);
    assert(h.state.sequencerTracks.track(0U).stepDataRevision.get() == bankStepDataRevisionBefore);

    std::cout << "[PASS] returning to before cancels the session exactly\n";
}

void test_timing_and_variation_net_returns_restore_every_revision() {
    {
        Harness h;
        const auto before = tx::captureStateInvariant(h.state);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);

        beginFlat(h.state, 100U);
        assert(h.state.sequencer.pattern.setPatternSwingOffsetPercent(17));
        assert(h.state.sealSequencerPatternHistoryCoalescing(true));
        beginFlat(h.state, 200U);
        assert(h.state.sequencer.pattern.setPatternSwingOffsetPercent(0));
        assert(h.state.sealSequencerPatternHistoryCoalescing(true));

        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        tx::assertStateInvariant(h.state, before);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
    }

    {
        Harness h;
        const auto before = tx::captureStateInvariant(h.state);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);

        beginFlat(h.state, 100U);
        assert(h.state.sequencer.pattern.setVariationRangeForProperty(seq::StepProperty::NOTE, 7U));
        assert(h.state.sealSequencerPatternHistoryCoalescing(true));
        beginFlat(h.state, 200U);
        assert(h.state.sequencer.pattern.setVariationRangeForProperty(seq::StepProperty::NOTE, 0U));
        assert(h.state.sealSequencerPatternHistoryCoalescing(true));

        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        tx::assertStateInvariant(h.state, before);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
    }

    std::cout << "[PASS] timing and variation net returns restore all revisions\n";
}

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
void test_key_change_commits_the_old_session_before_new_begin_failure() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 66U);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
            1U, seq::StepProperty::NOTE, 200U, PayloadPlan::FlatOnly) ==
               seq::SequencerHistoryOpenOutcome::ResourceUnavailable);
        tx::assertFailureConsumed(1U);
    }

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[kStep] == 66U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 66U);
    assertOnePublicationAfterQueuedNotifications(h, before);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);

    std::cout << "[PASS] key change preserves old commit when new begin fails\n";
}

void test_step_to_cc_transition_keeps_old_commit_when_cc_begin_fails() {
    Harness h;
    initializePayload(h, InitialPayload::CcOnly);
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 66U);
    auto stagedCc = stageCcLaneEvent(h.state, 92U);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.beginOrContinueSequencerCcLaneEventHistoryCoalescing(0U, kStep, 91, 92,
                                                                             stagedCc.get(), 200U) ==
               seq::SequencerHistoryOpenOutcome::ResourceUnavailable);
        tx::assertFailureConsumed(1U);
    }

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[kStep] == 66U);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == 66U);
    assert(editorCcLaneEventValue(h.state) == 91U);
    assert(bankCcLaneEventValue(h.state) == 91U);
    assertCommittedOwnerIdentities(h.state, before, PayloadPlan::FlatOnly, InitialPayload::CcOnly);
    assert(h.state.sequencerHistory.retainedBytes() ==
           before.retainedBytes +
               expectedRetainedBytes(PayloadPlan::FlatOnly, InitialPayload::CcOnly));
    assertOnePublicationAfterQueuedNotifications(h, before);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.note[kStep] == 66U);

    std::cout << "[PASS] Step-to-CC transition commits Step before CC fail-1\n";
}

void test_cc_to_step_transition_keeps_old_commit_when_step_begin_fails() {
    Harness h;
    initializePayload(h, InitialPayload::CcOnly);
    const auto before = tx::captureStateInvariant(h.state);
    auto stagedCc = stageCcLaneEvent(h.state, 92U);
    const void* stagedCcOwner = stagedCc.get();

    assert(seq::sequencerHistoryOpenAccepted(
        h.state.beginOrContinueSequencerCcLaneEventHistoryCoalescing(0U, kStep, 91, 92,
                                                                        stagedCc.get(), 100U)));
    seq::installSequencerCcLaneBank(h.state.sequencer.pattern, std::move(stagedCc));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(editorCcLaneEventValue(h.state) == 92U);

    {
        // Ordinal 1 is the legacy CC commit's active-bank synchronization;
        // ordinal 2 is the new Step transaction's final Change owner.
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        assert(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
            kStep, seq::StepProperty::NOTE, 200U, PayloadPlan::FlatOnly) ==
               seq::SequencerHistoryOpenOutcome::ResourceUnavailable);
        tx::assertFailureConsumed(2U);
    }

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(h.state.sequencerTracks.track(0U).note[kStep] == kInitialNote);
    assert(editorCcLaneEventValue(h.state) == 92U);
    assert(bankCcLaneEventValue(h.state) == 92U);

    const auto after = tx::captureStateInvariant(h.state);
    assert(after.editorCcOwner == stagedCcOwner);
    assert(after.editorCcOwner != before.editorCcOwner);
    assert(after.bankCcOwner != nullptr);
    assert(after.bankCcOwner != before.bankCcOwner);
    assert(after.bankCcOwner != after.editorCcOwner);
    assert(after.editorGraphOwner == before.editorGraphOwner);
    assert(after.bankGraphOwner == before.bankGraphOwner);
    assert(after.sequencerUndoCount == before.sequencerUndoCount + 1U);
    assert(after.sequencerRedoCount == 0U);
    assert(after.projectUndoCount == before.projectUndoCount + 1U);
    assert(after.projectRedoCount == 0U);
    assert(after.sequencerUndoIdentity != 0U);
    assert(after.sequencerUndoIdentity != before.sequencerUndoIdentity);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);
    assert(after.retainedBytes ==
           before.retainedBytes + sizeof(seq::SequencerHistoryPatternChange) +
               kAllocationHeaderBytes +
               2U * (sizeof(seq::SequencerCcLaneBank) + kAllocationHeaderBytes));

    // Empty the queued Signal wave while the Harness is still alive. CC's
    // separate generic coalescer is intentionally outside this Step lot.
    test_support::drainNotifications();
    tx::assertStateInvariant(h.state, after);

    std::cout << "[PASS] CC-to-Step transition commits CC before Step fail-2\n";
}
#endif

void test_timeout_boundary_is_exact_and_commit_allocates_nothing() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 70U);

    assert(!h.state.updateSequencerPatternHistoryCoalescing(599U));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == before.sequencerUndoCount);

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.updateSequencerPatternHistoryCoalescing(600U));
        tx::assertMaxPlusOneStillArmed(0U);
    }
#else
    assert(h.state.updateSequencerPatternHistoryCoalescing(600U));
#endif

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assertOnePublicationAfterQueuedNotifications(h, before);

    std::cout << "[PASS] timeout boundary is exact at 499/500 ms\n";
}

void test_generic_300_ms_coalescer_cannot_publish_before_history_500_ms() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 0U);
    mutateFlatAndSeal(h.state, 69U);

    // Deliver the edit notifications, then force the generic 300 ms
    // coalescer's public flush boundary. The targeted consume performed by
    // seal must leave it with nothing to publish.
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    auto at300 = tx::captureStateInvariant(h.state);
    assert(at300.modifiedCounter == before.modifiedCounter);
    assert(at300.sequencerUndoCount == before.sequencerUndoCount);
    assert(at300.projectUndoCount == before.projectUndoCount);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

    assert(!h.state.updateSequencerPatternHistoryCoalescing(499U));
    auto at499 = tx::captureStateInvariant(h.state);
    assert(at499.modifiedCounter == before.modifiedCounter);
    assert(at499.sequencerUndoCount == before.sequencerUndoCount);
    assert(at499.projectUndoCount == before.projectUndoCount);

    assert(h.state.updateSequencerPatternHistoryCoalescing(500U));
    assertOnePublicationAfterQueuedNotifications(h, before);

    std::cout << "[PASS] generic 300 ms coalescer stays silent until History 500 ms\n";
}

void assertOnlyGenericMutationPublished(const Harness& h, const tx::StateInvariant& before) {
    const auto after = tx::captureStateInvariant(h.state);
    assert(after.sequencerUndoCount == before.sequencerUndoCount);
    assert(after.sequencerRedoCount == before.sequencerRedoCount);
    assert(after.projectUndoCount == before.projectUndoCount);
    assert(after.projectRedoCount == before.projectRedoCount);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);
}

void test_no_op_preserves_a_preexisting_generic_mutation() {
    Harness h;
    h.state.sequencer.focusedStep.set(1U);
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    assert(h.state.sealSequencerPatternHistoryCoalescing(false));
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == before.sequencerUndoCount);

    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    assertOnlyGenericMutationPublished(h, before);
    assert(h.state.sequencer.focusedStep.get() == 1U);

    std::cout << "[PASS] virgin no-op preserves an earlier queued generic mutation\n";
}

void test_net_return_rearms_a_preexisting_generic_mutation() {
    Harness h;
    h.state.sequencer.focusedStep.set(1U);
    const auto before = tx::captureStateInvariant(h.state);

    assert(
        seq::sequencerHistoryOpenAccepted(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        kStep, seq::StepProperty::NOTE, 100U, PayloadPlan::FullWithProspectiveGraph)));
    assert(seq::setNodeLocalVariationRange(h.state.sequencer.pattern, seq::rootStepNodeId(kStep),
                                           seq::StepProperty::NOTE, 5U));
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));
    assert(
        seq::sequencerHistoryOpenAccepted(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        kStep, seq::StepProperty::NOTE, 200U, PayloadPlan::FullWithProspectiveGraph)));
    h.state.sequencer.pattern.graph->reset();
    h.state.sequencer.pattern.bumpGraphRevision();
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == before.sequencerUndoCount);
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);

    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    assertOnlyGenericMutationPublished(h, before);
    assert(h.state.sequencer.pattern.note[kStep] == kInitialNote);
    assert(h.state.sequencer.focusedStep.get() == 1U);

    std::cout << "[PASS] Graph net return re-arms an earlier queued generic mutation\n";
}

void test_no_op_seal_is_immediate_and_byte_identical() {
    {
        Harness h;
        const auto before = tx::captureStateInvariant(h.state);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);

        beginFlat(h.state, 100U);
        assert(h.state.sealSequencerPatternHistoryCoalescing(false));
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(!h.state.commitSequencerPatternHistoryCoalescing());
        tx::assertStateInvariant(h.state, before);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
    }

    {
        Harness h;
        const auto before = tx::captureStateInvariant(h.state);
        assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
        assert(seq::sequencerHistoryOpenAccepted(
            h.state.beginOrContinueSequencerPatternHistoryCoalescing(
            kStep, seq::StepProperty::NOTE, 100U, PayloadPlan::FullWithProspectiveGraph)));
        assert(h.state.sequencer.pattern.graph != nullptr);
        assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
        assert(h.state.sealSequencerPatternHistoryCoalescing(false));
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencer.pattern.graph == nullptr);
        assert(h.state.sequencerTracks.track(0U).graph == nullptr);
        assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
        tx::assertStateInvariant(h.state, before);
    }

    std::cout << "[PASS] no-op seal is immediate and byte-identical\n";
}

void test_prospective_graph_net_return_releases_the_live_owner_exactly() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(h.state, musicalBefore);
    assert(before.editorGraphOwner == nullptr);
    assert(before.bankGraphOwner == nullptr);

    assert(
        seq::sequencerHistoryOpenAccepted(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        kStep, seq::StepProperty::NOTE, 100U, PayloadPlan::FullWithProspectiveGraph)));
    assert(h.state.sequencer.pattern.graph != nullptr);
    assert(seq::setNodeLocalVariationRange(h.state.sequencer.pattern, seq::rootStepNodeId(kStep),
                                           seq::StepProperty::NOTE, 6U));
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));

    assert(
        seq::sequencerHistoryOpenAccepted(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        kStep, seq::StepProperty::NOTE, 200U, PayloadPlan::FullWithProspectiveGraph)));
    assert(h.state.sequencer.pattern.graph != nullptr);
    h.state.sequencer.pattern.graph->reset();
    h.state.sequencer.pattern.bumpGraphRevision();
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(!h.state.commitSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.graph == nullptr);
    assert(h.state.sequencerTracks.track(0U).graph == nullptr);
    tx::assertStateInvariant(h.state, before);
    tx::assertMusicalSnapshot(h.state, musicalBefore);

    std::cout << "[PASS] prospective Graph 0-x-0 releases its live owner exactly\n";
}

void test_prospective_graph_commits_without_post_begin_allocation() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        // Graphless/no-CC prospective order is Change, prospective live Graph,
        // reserved after Graph, and reserved active-bank synchronization Graph.
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        assert(seq::sequencerHistoryOpenAccepted(
            h.state.beginOrContinueSequencerPatternHistoryCoalescing(
            kStep, seq::StepProperty::NOTE, 100U, PayloadPlan::FullWithProspectiveGraph)));
        assert(h.state.sequencer.pattern.graph != nullptr);
        assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
        assert(seq::setNodeLocalVariationRange(
            h.state.sequencer.pattern, seq::rootStepNodeId(kStep), seq::StepProperty::NOTE, 5U));
        assert(h.state.sealSequencerPatternHistoryCoalescing(true));
        assert(h.state.commitSequencerPatternHistoryCoalescing());
        tx::assertMaxPlusOneStillArmed(4U);
    }
#else
    assert(
        seq::sequencerHistoryOpenAccepted(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        kStep, seq::StepProperty::NOTE, 100U, PayloadPlan::FullWithProspectiveGraph)));
    assert(seq::setNodeLocalVariationRange(h.state.sequencer.pattern, seq::rootStepNodeId(kStep),
                                           seq::StepProperty::NOTE, 5U));
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));
    assert(h.state.commitSequencerPatternHistoryCoalescing());
#endif

    const auto* editorGraph = seq::graphView(h.state.sequencer.pattern);
    const auto* bankGraph = seq::graphView(h.state.sequencerTracks.track(0U));
    assert(editorGraph != nullptr && bankGraph != nullptr);
    assert(seq::nodeLocalVariationRange(*editorGraph->stepNode(seq::rootStepNodeId(kStep)),
                                        seq::StepProperty::NOTE) == 5U);
    assert(seq::nodeLocalVariationRange(*bankGraph->stepNode(seq::rootStepNodeId(kStep)),
                                        seq::StepProperty::NOTE) == 5U);
    assertOnePublicationAfterQueuedNotifications(h, before);

    assert(h.state.undoSequencerHistory());
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
    assert(seq::graphView(h.state.sequencerTracks.track(0U)) == nullptr);
    assert(h.state.redoSequencerHistory());
    assert(seq::graphView(h.state.sequencer.pattern) != nullptr);
    assert(seq::graphView(h.state.sequencerTracks.track(0U)) != nullptr);

    std::cout << "[PASS] prospective Graph has no post-begin allocation\n";
}

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
void mutateMatrixCandidateAndSeal(core::state::CoreState& state, PayloadPlan plan) {
    if (plan == PayloadPlan::FullWithProspectiveGraph) {
        assert(state.sequencer.pattern.graph != nullptr);
        assert(seq::setNodeLocalVariationRange(state.sequencer.pattern, seq::rootStepNodeId(kStep),
                                               seq::StepProperty::NOTE, 7U));
    } else {
        assert(state.sequencer.setStepNoteAt(kStep, 74U));
    }
    assert(state.sealSequencerPatternHistoryCoalescing(true));
}

void assertBeginFailNthAndMaxPlusOne(PayloadPlan plan, InitialPayload initialPayload,
                                     std::size_t allocationCount) {
    const auto expectedRequests = expectedBeginAllocationRequests(plan, initialPayload);
    assert(expectedRequests.count == allocationCount);

    for (std::size_t ordinal = 1U; ordinal <= allocationCount; ++ordinal) {
        Harness h;
        initializePayload(h, initialPayload);
        const auto invariantBefore = tx::captureStateInvariant(h.state);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            assert(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
                kStep, seq::StepProperty::NOTE, 100U, plan) ==
                   seq::SequencerHistoryOpenOutcome::ResourceUnavailable);
            tx::assertFailureConsumed(ordinal);
        }

        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        tx::assertStateInvariant(h.state, invariantBefore);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
    }

    Harness h;
    initializePayload(h, initialPayload);
    const auto before = tx::captureStateInvariant(h.state);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(allocationCount + 1U);
        {
            allocation_trace::Scope trace;
            assert(seq::sequencerHistoryOpenAccepted(
                h.state.beginOrContinueSequencerPatternHistoryCoalescing(
                kStep, seq::StepProperty::NOTE, 100U, plan)));
            assertAllocationRequests(expectedRequests);
        }
        mutateMatrixCandidateAndSeal(h.state, plan);
        assert(h.state.commitSequencerPatternHistoryCoalescing());
        tx::assertMaxPlusOneStillArmed(allocationCount);
    }
    assertCommittedOwnerIdentities(h.state, before, plan, initialPayload);
    assert(h.state.sequencerHistory.retainedBytes() ==
           before.retainedBytes + expectedRetainedBytes(plan, initialPayload));
    assertOnePublicationAfterQueuedNotifications(h, before);
}

void test_begin_fail_nth_and_max_plus_one_payload_matrix() {
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FlatOnly, InitialPayload::None, 1U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FlatOnly, InitialPayload::GraphAndCc, 1U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FullCurrentPayload, InitialPayload::None, 1U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FullCurrentPayload, InitialPayload::GraphOnly, 4U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FullCurrentPayload, InitialPayload::CcOnly, 4U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FullCurrentPayload, InitialPayload::GraphAndCc,
                                    7U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FullWithProspectiveGraph, InitialPayload::None,
                                    4U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FullWithProspectiveGraph, InitialPayload::CcOnly,
                                    7U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FullWithProspectiveGraph,
                                    InitialPayload::GraphOnly, 4U);
    assertBeginFailNthAndMaxPlusOne(PayloadPlan::FullWithProspectiveGraph,
                                    InitialPayload::DisabledGraphOnly, 3U);

    std::cout << "[PASS] exact LOCK-P order/owners/bytes and fail-Nth matrix hold\n";
}

void test_sealed_commit_ignores_fail_one() {
    Harness h;
    const auto before = tx::captureStateInvariant(h.state);

    beginFlat(h.state, 100U);
    mutateFlatAndSeal(h.state, 73U);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.commitSequencerPatternHistoryCoalescing());
        tx::assertMaxPlusOneStillArmed(0U);
    }

    assertOnePublicationAfterQueuedNotifications(h, before);
    assert(h.state.sequencer.pattern.note[kStep] == 73U);

    std::cout << "[PASS] sealed commit performs zero allocation under fail-1\n";
}
#endif

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    test_flat_begin_seal_commit_is_exact_and_undoable();
    test_typed_domain_commit_adapter_distinguishes_failure_from_empty_boundary();
    test_core_state_flush_commits_pending_step_without_allocation_and_global_redoes();
    test_same_key_continuation_and_commit_allocate_nothing();
    test_same_key_no_op_keeps_the_last_real_after_and_refreshes_timeout();
    test_same_key_payload_plan_drift_is_rejected_without_publication();
    test_timeout_boundary_is_exact_and_commit_allocates_nothing();
    test_generic_300_ms_coalescer_cannot_publish_before_history_500_ms();
    test_no_op_preserves_a_preexisting_generic_mutation();
    test_net_return_rearms_a_preexisting_generic_mutation();
    test_no_op_seal_is_immediate_and_byte_identical();
    test_prospective_graph_net_return_releases_the_live_owner_exactly();
    test_prospective_graph_commits_without_post_begin_allocation();
    test_timing_and_variation_net_returns_restore_every_revision();
#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    test_key_change_commits_the_old_session_before_new_begin_failure();
    test_step_to_cc_transition_keeps_old_commit_when_cc_begin_fails();
    test_cc_to_step_transition_keeps_old_commit_when_step_begin_fails();
    test_begin_fail_nth_and_max_plus_one_payload_matrix();
    test_sealed_commit_ignores_fail_one();
#endif
    test_return_to_before_cancels_the_session_exactly();

    std::cout << "All Sequencer coalesced Pattern transaction tests passed.\n";
    return 0;
}
