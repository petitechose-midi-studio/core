#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/NotificationTestUtils.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

namespace allocation_trace {

// Native-only observation fixture for exact request order and payload size.
// The EXTMEM fail-Nth seam remains the authoritative no-allocation barrier
// proof; Teensy allocation spans are locked independently below.
constexpr std::size_t MAX_REQUESTS = 80U;
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

void* operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

namespace seq = core::state::sequencer;
namespace tx = test_support::sequencer_transaction;

constexpr std::size_t ARM_ALLOCATION_HEADER_BYTES = 16U;
constexpr std::size_t ARM_PATTERN_CHANGE_BYTES = 1736U;
constexpr std::size_t ARM_FULL_BANK_CHANGE_BYTES = 26960U;
constexpr std::size_t ARM_STRUCTURE_CHANGE_BYTES = 27192U;
constexpr std::size_t ARM_GRAPH_BYTES = 14792U;
constexpr std::size_t ARM_CC_BYTES = 840U;
constexpr std::size_t ARM_PATTERN_PROVIDER_SPANS = 7U;
constexpr std::size_t ARM_PATTERN_STAGED_SPANS = 9U;
constexpr std::size_t ARM_FULL_BANK_SPANS = 69U;
constexpr std::size_t ARM_STRUCTURE_SPANS = 65U;

static_assert(
    ARM_PATTERN_CHANGE_BYTES + 3U * (ARM_GRAPH_BYTES + ARM_CC_BYTES) +
            ARM_PATTERN_PROVIDER_SPANS * ARM_ALLOCATION_HEADER_BYTES ==
        48744U,
    "LOCK-P: Pattern provider peak changed"
);
static_assert(
    ARM_PATTERN_CHANGE_BYTES + 4U * (ARM_GRAPH_BYTES + ARM_CC_BYTES) +
            ARM_PATTERN_STAGED_SPANS * ARM_ALLOCATION_HEADER_BYTES ==
        64408U,
    "LOCK-P: Pattern staged/provider overlap peak changed"
);
static_assert(
    ARM_FULL_BANK_CHANGE_BYTES + 34U * (ARM_GRAPH_BYTES + ARM_CC_BYTES) +
            ARM_FULL_BANK_SPANS * ARM_ALLOCATION_HEADER_BYTES ==
        559552U,
    "LOCK-P: FullBank provider peak changed"
);
static_assert(
    ARM_STRUCTURE_CHANGE_BYTES + 32U * (ARM_GRAPH_BYTES + ARM_CC_BYTES) +
            ARM_STRUCTURE_SPANS * ARM_ALLOCATION_HEADER_BYTES ==
        528456U,
    "LOCK-P: Structure provider peak changed"
);

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;

    Harness()
        : state(storages.settings) {}
};

enum class PayloadKind : uint8_t {
    None,
    Graph,
    Cc,
    GraphAndCc,
};

constexpr bool hasGraph(PayloadKind kind) {
    return kind == PayloadKind::Graph || kind == PayloadKind::GraphAndCc;
}

constexpr bool hasCc(PayloadKind kind) {
    return kind == PayloadKind::Cc || kind == PayloadKind::GraphAndCc;
}

struct ExpectedAllocationRequests {
    std::array<std::size_t, allocation_trace::MAX_REQUESTS> bytes{};
    std::size_t count = 0U;

    void push(std::size_t requestBytes) {
        assert(count < bytes.size());
        bytes[count++] = requestBytes;
    }
};

void assertAllocationRequests(const ExpectedAllocationRequests& expected) {
    assert(!allocation_trace::overflow);
    assert(allocation_trace::count == expected.count);
    std::size_t actualPayload = 0U;
    std::size_t expectedPayload = 0U;
    std::size_t actualLargest = 0U;
    std::size_t expectedLargest = 0U;
    for (std::size_t i = 0U; i < expected.count; ++i) {
        assert(allocation_trace::requests[i] == expected.bytes[i]);
        actualPayload += allocation_trace::requests[i];
        expectedPayload += expected.bytes[i];
        actualLargest = std::max(actualLargest, allocation_trace::requests[i]);
        expectedLargest = std::max(expectedLargest, expected.bytes[i]);
    }
    assert(actualPayload == expectedPayload);
    assert(actualLargest == expectedLargest);
}

void appendPayloadRequests(
    ExpectedAllocationRequests& expected,
    PayloadKind kind
) {
    if (hasGraph(kind)) {
        expected.push(sizeof(oc::note::sequencer::StepSequencerGraph));
    }
    if (hasCc(kind)) {
        expected.push(sizeof(seq::SequencerCcLaneBank));
    }
}

ExpectedAllocationRequests expectedPatternAllocationRequests(
    PayloadKind kind,
    seq::SequencerHistoryPatternStorage storage
) {
    ExpectedAllocationRequests expected;
    expected.push(sizeof(seq::SequencerHistoryPatternChange));
    if (storage == seq::SequencerHistoryPatternStorage::FullGraph) {
        appendPayloadRequests(expected, kind);  // before
        appendPayloadRequests(expected, kind);  // after reservation
        appendPayloadRequests(expected, kind);  // active-bank synchronization
    }
    return expected;
}

void authorPayload(
    seq::SequencerPatternState& pattern,
    PayloadKind kind,
    uint8_t noteOffset = 0U
) {
    pattern.setContentLength(8U);
    assert(pattern.setStepDataAt(
        0U,
        static_cast<uint8_t>(60U + noteOffset),
        91U,
        seq::SequencerPatternState::DEFAULT_GATE_PERCENT
    ));
    pattern.setEnabled(0U, true);

    if (hasGraph(kind)) {
        assert(seq::ensureGraphRoot(pattern));
        assert(seq::setNodeNoteOffset(
            pattern,
            seq::rootStepNodeId(0U),
            static_cast<int8_t>(5 + noteOffset)
        ));
    }

    if (hasCc(kind)) {
        auto* bank = seq::ensureSequencerCcLaneBank(pattern);
        assert(bank != nullptr);
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = static_cast<uint8_t>(74U + noteOffset);
        assert(seq::createSequencerCcLane(*bank, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(
            *bank,
            0U,
            0U,
            static_cast<uint8_t>(99U - noteOffset)
        ).changed());
        pattern.bumpCcLaneRevision();
    }
}

void settleSetup(Harness& h) {
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    assert(!h.state.hasPendingProjectSessionSave());
}

void initializeActivePayload(Harness& h, PayloadKind kind) {
    authorPayload(h.state.sequencer.pattern, kind);
    assert(seq::initializeTrackBankFromActive(
        h.state.sequencerTracks,
        h.state.sequencer
    ));
    settleSetup(h);
}

void initializeCapturedTracks(
    Harness& h,
    PayloadKind kind,
    uint16_t capturedMask
) {
    authorPayload(h.state.sequencer.pattern, kind);
    assert(seq::initializeTrackBankFromActive(
        h.state.sequencerTracks,
        h.state.sequencer
    ));
    for (uint8_t track = 1U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if ((capturedMask & seq::sequencerHistoryTrackBit(track)) != 0U) {
            authorPayload(h.state.sequencerTracks.track(track), kind, track);
        }
    }
    settleSetup(h);
}

void stageActivePattern(const Harness& h, seq::SequencerState& staged) {
    staged.reset();
    assert(seq::copyPatternState(
        staged.pattern,
        h.state.sequencer.pattern
    ));
}

void stageTrackBank(
    const Harness& h,
    seq::SequencerTrackBankState& stagedBank,
    seq::SequencerState& stagedActive
) {
    stagedBank.reset();
    stagedActive.reset();
    stagedBank.setProjectScaleSettings(
        h.state.sequencerTracks.projectScaleSettings()
    );
    stagedBank.syncSharedTrackState(
        h.state.sequencerTracks.currentEnabledMask(),
        h.state.sequencerTracks.activeTrackIndex()
    );
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        assert(seq::copyPatternState(
            stagedBank.track(track),
            h.state.sequencerTracks.track(track)
        ));
    }
    assert(seq::copyPatternState(
        stagedActive.pattern,
        h.state.sequencer.pattern
    ));
    stagedActive.focusedStep.set(h.state.sequencer.focusedStep.get());
    stagedActive.activeStepProperty.set(
        h.state.sequencer.activeStepProperty.get()
    );
    stagedActive.page.set(h.state.sequencer.page.get());
}

void publishStagedFlatPattern(
    Harness& h,
    const seq::SequencerState& staged
) {
    seq::SequencerPatternSnapshot flat{};
    seq::captureSnapshot(staged.pattern, flat);
    seq::applySnapshotPreservingGraph(h.state.sequencer.pattern, flat);
    h.state.sequencer.pattern.ccLaneRevision.set(
        staged.pattern.ccLaneRevision.get()
    );
}

struct BankOwnerInvariant {
    const void* editorGraph = nullptr;
    const void* editorCc = nullptr;
    std::array<const void*, seq::SequencerTrackBankState::TRACK_COUNT> graphs{};
    std::array<const void*, seq::SequencerTrackBankState::TRACK_COUNT> cc{};
    std::array<uint32_t, seq::SequencerTrackBankState::TRACK_COUNT> graphRevisions{};
    std::array<uint32_t, seq::SequencerTrackBankState::TRACK_COUNT> ccRevisions{};
};

BankOwnerInvariant captureBankOwners(const Harness& h) {
    BankOwnerInvariant result;
    result.editorGraph = h.state.sequencer.pattern.graph.get();
    result.editorCc = h.state.sequencer.pattern.ccLanes.get();
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const auto& pattern = h.state.sequencerTracks.track(track);
        result.graphs[track] = pattern.graph.get();
        result.cc[track] = pattern.ccLanes.get();
        result.graphRevisions[track] = pattern.graphRevision.get();
        result.ccRevisions[track] = pattern.ccLaneRevision.get();
    }
    return result;
}

void assertBankOwners(
    const Harness& h,
    const BankOwnerInvariant& expected
) {
    const auto actual = captureBankOwners(h);
    assert(actual.editorGraph == expected.editorGraph);
    assert(actual.editorCc == expected.editorCc);
    assert(actual.graphs == expected.graphs);
    assert(actual.cc == expected.cc);
    assert(actual.graphRevisions == expected.graphRevisions);
    assert(actual.ccRevisions == expected.ccRevisions);
}

struct FullBankMusicalProof {
    seq::SequencerHistoryTrackBankSnapshot snapshot;
};

struct DraftInvariant {
    bool active = false;
    seq::SequencerStepContentDraftKind kind =
        seq::SequencerStepContentDraftKind::NONE;
    bool exitPromptVisible = false;
    seq::SequencerStepContentDraftExitChoice exitChoice =
        seq::SequencerStepContentDraftExitChoice::SAVE;
    uint32_t revision = 0U;
    const void* scratch = nullptr;
    uint32_t pristineGraphRevision = 0U;
    uint32_t pristineGraphFingerprint = 0U;
    uint8_t ownerStep = 0U;
    bool modified = false;
    seq::SequencerStepChordDraftState chord{};
    seq::SequencerStepContentDraftFailure failure =
        seq::SequencerStepContentDraftFailure::NONE;
    seq::SequencerStepContentDraftBlockedTransition blockedTransition =
        seq::SequencerStepContentDraftBlockedTransition::NONE;
};

DraftInvariant captureDraftInvariant(const Harness& h) {
    const auto& draft = h.state.sequencer.stepContentDraft;
    DraftInvariant result;
    result.active = draft.active.get();
    result.kind = draft.kind.get();
    result.exitPromptVisible = draft.exitPromptVisible.get();
    result.exitChoice = draft.exitChoice.get();
    result.revision = draft.revision.get();
    result.scratch = draft.scratch.get();
    result.pristineGraphRevision = draft.pristineGraphRevision;
    result.pristineGraphFingerprint = draft.pristineGraphFingerprint;
    result.ownerStep = draft.ownerStep;
    result.modified = draft.modified();
    result.chord = draft.chord;
    result.failure = draft.failure;
    result.blockedTransition = draft.blockedTransition;
    return result;
}

void assertDraftInvariant(const Harness& h, const DraftInvariant& expected) {
    const auto actual = captureDraftInvariant(h);
    assert(actual.active == expected.active);
    assert(actual.kind == expected.kind);
    assert(actual.exitPromptVisible == expected.exitPromptVisible);
    assert(actual.exitChoice == expected.exitChoice);
    assert(actual.revision == expected.revision);
    assert(actual.scratch == expected.scratch);
    assert(actual.pristineGraphRevision == expected.pristineGraphRevision);
    assert(actual.pristineGraphFingerprint == expected.pristineGraphFingerprint);
    assert(actual.ownerStep == expected.ownerStep);
    assert(actual.modified == expected.modified);
    assert(actual.chord.ownerNodeId == expected.chord.ownerNodeId);
    assert(actual.chord.modePresent == expected.chord.modePresent);
    assert(actual.chord.localPresent == expected.chord.localPresent);
    assert(actual.chord.mode == expected.chord.mode);
    assert(oc::note::sequencer::chordSpecsEqual(
        actual.chord.spec,
        expected.chord.spec
    ));
    assert(actual.chord.pristineModePresent == expected.chord.pristineModePresent);
    assert(actual.chord.pristineLocalPresent == expected.chord.pristineLocalPresent);
    assert(actual.chord.pristineMode == expected.chord.pristineMode);
    assert(oc::note::sequencer::chordSpecsEqual(
        actual.chord.pristineSpec,
        expected.chord.pristineSpec
    ));
    assert(actual.failure == expected.failure);
    assert(actual.blockedTransition == expected.blockedTransition);
}

FullBankMusicalProof captureFullBankMusicalProof(const Harness& h) {
    FullBankMusicalProof proof;
    assert(seq::captureHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        proof.snapshot
    ));
    return proof;
}

void assertFullBankMusicalProof(
    const Harness& h,
    const FullBankMusicalProof& expected
) {
    seq::SequencerHistoryTrackBankSnapshot actual;
    assert(seq::captureHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        actual
    ));
    assert(seq::sameMusicalHistorySnapshot(actual, expected.snapshot));
}

void assertEditorAndActiveBankMusicalSnapshot(
    const Harness& h,
    const seq::SequencerHistoryPatternSnapshot& expected
) {
    tx::assertMusicalSnapshot(h.state, expected);
    seq::SequencerState bankState;
    bankState.reset();
    assert(seq::copyPatternState(
        bankState.pattern,
        h.state.sequencerTracks.track(0U)
    ));
    seq::SequencerHistoryPatternSnapshot bank;
    assert(seq::captureHistorySnapshot(bankState, bank));
    assert(seq::sameMusicalHistorySnapshot(bank, expected));
}

void assertExactlyOnePublication(
    const core::state::CoreState& state,
    const tx::StateInvariant& before
) {
    const auto after = tx::captureStateInvariant(state);
    assert(after.sequencerUndoCount == before.sequencerUndoCount + 1U);
    assert(after.sequencerRedoCount == 0U);
    assert(after.projectUndoCount == before.projectUndoCount + 1U);
    assert(after.projectRedoCount == 0U);
    assert(after.sequencerUndoIdentity != 0U);
    assert(after.sequencerUndoIdentity != before.sequencerUndoIdentity);
    assert(after.retainedBytes > before.retainedBytes);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);
}

void assertSharedTrackProjection(
    const Harness& h,
    uint16_t expectedMask,
    uint8_t expectedActive
) {
    assert(h.state.sequencerTracks.currentEnabledMask() == expectedMask);
    assert(h.state.sequencerTracks.activeTrackIndex() == expectedActive);
    assert(h.state.sharedTrackEnabledMask.get() == expectedMask);
    assert(h.state.sharedTrackActive.get() == expectedActive);
    assert(h.state.pages.currentTrackEnabledMask() == expectedMask);
    assert(h.state.pages.currentActiveTrack() == expectedActive);
}

void assertNoDeferredPublication(Harness& h) {
    const auto settled = tx::captureStateInvariant(h.state);
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    tx::assertStateInvariant(h.state, settled);
}

std::size_t expectedPatternRetainedBytes(
    PayloadKind kind,
    seq::SequencerHistoryPatternStorage storage
) {
    constexpr std::size_t allocationHeaderBytes = 16U;
    std::size_t bytes = sizeof(seq::SequencerHistoryPatternChange) +
        allocationHeaderBytes;
    if (storage != seq::SequencerHistoryPatternStorage::FullGraph) {
        return bytes;
    }
    if (hasGraph(kind)) {
        bytes += 2U * (
            sizeof(oc::note::sequencer::StepSequencerGraph) +
            allocationHeaderBytes
        );
    }
    if (hasCc(kind)) {
        bytes += 2U * (
            sizeof(seq::SequencerCcLaneBank) + allocationHeaderBytes
        );
    }
    return bytes;
}

struct PreparedPattern {
    seq::SequencerHistoryPatternChangePtr change;
    seq::SequencerPreparedActiveTrackSynchronization synchronization;
};

bool preparePattern(
    Harness& h,
    seq::SequencerHistoryPatternStorage storage,
    PreparedPattern& out
) {
    seq::SequencerHistoryDescriptor descriptor{};
    descriptor.kind = seq::SequencerHistoryActionKind::StepToggle;
    descriptor.stepIndex = 1U;
    out.change = seq::prepareHistoryPatternChangeBefore(
        h.state.sequencerTracks,
        h.state.sequencer,
        h.state.sequencerTracks.activeTrackIndex(),
        storage,
        descriptor
    );
    if (!out.change) return false;
    if (!seq::reservePreparedHistoryPatternAfter(
            h.state.sequencerTracks,
            h.state.sequencer,
            *out.change
        )) {
        return false;
    }
    return seq::reservePreparedActiveTrackSynchronization(
        h.state.sequencerTracks,
        h.state.sequencer,
        out.change->trackIndex,
        storage,
        out.synchronization
    );
}

void assertPatternOwners(
    const PreparedPattern& prepared,
    PayloadKind kind,
    seq::SequencerHistoryPatternStorage storage
) {
    assert(prepared.change != nullptr);
    const bool ownsGraph = storage == seq::SequencerHistoryPatternStorage::FullGraph &&
        hasGraph(kind);
    const bool ownsCc = storage == seq::SequencerHistoryPatternStorage::FullGraph &&
        hasCc(kind);
    assert(static_cast<bool>(prepared.change->before.graph) == ownsGraph);
    assert(static_cast<bool>(prepared.change->before.ccLanes) == ownsCc);
    assert(static_cast<bool>(prepared.change->after.graph) == ownsGraph);
    assert(static_cast<bool>(prepared.change->after.ccLanes) == ownsCc);
    assert(static_cast<bool>(prepared.synchronization.payload.graph) == ownsGraph);
    assert(static_cast<bool>(prepared.synchronization.payload.ccLanes) == ownsCc);
}

void verifyPatternPreparationFailure(
    PayloadKind kind,
    seq::SequencerHistoryPatternStorage storage,
    std::size_t ordinal
) {
    Harness h;
    initializeActivePayload(h, kind);
    auto musicalBaseline = captureFullBankMusicalProof(h);
    const auto invariant = tx::captureStateInvariant(h.state);
    const auto bankOwners = captureBankOwners(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        PreparedPattern rejected;
        assert(!preparePattern(h, storage, rejected));
        tx::assertFailureConsumed(ordinal);
        tx::assertStateInvariant(h.state, invariant);
        assertBankOwners(h, bankOwners);
    }

    assertFullBankMusicalProof(h, musicalBaseline);
}

void verifyPatternPreparationRatchet(
    PayloadKind kind,
    seq::SequencerHistoryPatternStorage storage,
    std::size_t expectedAttempts
) {
    Harness h;
    initializeActivePayload(h, kind);
    auto musicalBaseline = captureFullBankMusicalProof(h);
    const auto invariant = tx::captureStateInvariant(h.state);
    const auto bankOwners = captureBankOwners(h);
    const auto expectedRequests = expectedPatternAllocationRequests(kind, storage);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        allocation_trace::Scope allocationTrace;
        PreparedPattern prepared;
        assert(preparePattern(h, storage, prepared));
        assertPatternOwners(prepared, kind, storage);
        assertAllocationRequests(expectedRequests);
        tx::assertMaxPlusOneStillArmed(expectedAttempts);
        tx::assertStateInvariant(h.state, invariant);
        assertBankOwners(h, bankOwners);
    }

    assertFullBankMusicalProof(h, musicalBaseline);
}

void test_pattern_preparation_allocation_matrix() {
    struct Case {
        PayloadKind kind;
        seq::SequencerHistoryPatternStorage storage;
        std::size_t expectedAttempts;
    };
    constexpr std::array<Case, 6> cases{{
        {PayloadKind::None, seq::SequencerHistoryPatternStorage::FlatOnly, 1U},
        {PayloadKind::GraphAndCc, seq::SequencerHistoryPatternStorage::FlatOnly, 1U},
        {PayloadKind::None, seq::SequencerHistoryPatternStorage::FullGraph, 1U},
        {PayloadKind::Graph, seq::SequencerHistoryPatternStorage::FullGraph, 4U},
        {PayloadKind::Cc, seq::SequencerHistoryPatternStorage::FullGraph, 4U},
        {PayloadKind::GraphAndCc, seq::SequencerHistoryPatternStorage::FullGraph, 7U},
    }};

    for (const auto& item : cases) {
        for (std::size_t ordinal = 1U;
             ordinal <= item.expectedAttempts;
             ++ordinal) {
            verifyPatternPreparationFailure(item.kind, item.storage, ordinal);
        }
        verifyPatternPreparationRatchet(
            item.kind,
            item.storage,
            item.expectedAttempts
        );
    }

    std::cout << "[PASS] Pattern preparation allocation matrix\n";
}

void test_pattern_staged_provider_overlap_contract() {
    Harness h;
    initializeActivePayload(h, PayloadKind::GraphAndCc);
    const auto expectedRequests = expectedPatternAllocationRequests(
        PayloadKind::GraphAndCc,
        seq::SequencerHistoryPatternStorage::FullGraph
    );

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            ARM_PATTERN_STAGED_SPANS + 1U
        );
        allocation_trace::Scope allocationTrace;
        PreparedPattern prepared;
        assert(preparePattern(
            h,
            seq::SequencerHistoryPatternStorage::FullGraph,
            prepared
        ));
        seq::SequencerState staged;
        stageActivePattern(h, staged);

        assert(allocation_trace::count == ARM_PATTERN_STAGED_SPANS);
        for (std::size_t i = 0U; i < expectedRequests.count; ++i) {
            assert(allocation_trace::requests[i] == expectedRequests.bytes[i]);
        }
        assert(
            allocation_trace::requests[ARM_PATTERN_PROVIDER_SPANS] ==
            sizeof(oc::note::sequencer::StepSequencerGraph)
        );
        assert(
            allocation_trace::requests[ARM_PATTERN_PROVIDER_SPANS + 1U] ==
            sizeof(seq::SequencerCcLaneBank)
        );
        tx::assertMaxPlusOneStillArmed(ARM_PATTERN_STAGED_SPANS);
    }
    std::cout << "[PASS] Pattern staged/provider overlap is 9 spans\n";
}

void runPatternCommit(
    PayloadKind kind,
    seq::SequencerHistoryPatternStorage storage
) {
    Harness h;
    initializeActivePayload(h, kind);
    seq::SequencerHistoryPatternSnapshot expectedBefore;
    assert(seq::captureHistorySnapshot(h.state.sequencer, expectedBefore));
    PreparedPattern prepared;
    assert(preparePattern(h, storage, prepared));
    seq::SequencerState staged;
    stageActivePattern(h, staged);
    staged.pattern.setEnabled(1U, true);
    if (storage == seq::SequencerHistoryPatternStorage::FullGraph && hasGraph(kind)) {
        assert(seq::setNodeNoteOffset(
            staged.pattern,
            seq::rootStepNodeId(0U),
            11
        ));
    }
    if (storage == seq::SequencerHistoryPatternStorage::FullGraph && hasCc(kind)) {
        assert(staged.pattern.ccLanes != nullptr);
        assert(seq::setSequencerCcLaneEvent(
            *staged.pattern.ccLanes,
            0U,
            0U,
            42U
        ).changed());
        staged.pattern.bumpCcLaneRevision();
    }
    seq::SequencerHistoryPatternSnapshot expectedAfter;
    assert(seq::captureHistorySnapshot(staged, expectedAfter));
    assert(seq::capturePreparedHistoryPatternAfterUsingReservedStorage(
        h.state.sequencerTracks,
        staged,
        *prepared.change
    ));
    assert(seq::capturePreparedActiveTrackSynchronizationUsingReservedStorage(
        h.state.sequencerTracks,
        staged,
        prepared.synchronization
    ));
    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state
    );
    assert(history.canRecordSynchronizedPattern(*prepared.change));

    const auto before = tx::captureStateInvariant(h.state);
    const auto beforeBankGraph = h.state.sequencerTracks.track(0U).graph.get();
    const auto beforeBankCc = h.state.sequencerTracks.track(0U).ccLanes.get();
    const auto editorGraph = h.state.sequencer.pattern.graph.get();
    const auto editorCc = h.state.sequencer.pattern.ccLanes.get();

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(seq::preparedActiveTrackSynchronizationMatches(
            h.state.sequencerTracks,
            prepared.synchronization
        ));
        seq::installPatternStateToEditor(h.state.sequencer, staged.pattern);
        seq::publishPreparedActiveTrackSynchronization(
            h.state.sequencerTracks,
            h.state.sequencer,
            std::move(prepared.synchronization)
        );
        history.recordPreparedSynchronizedPattern(std::move(prepared.change));
        tx::assertMaxPlusOneStillArmed(0U);
        assertExactlyOnePublication(h.state, before);
        assert(
            h.state.sequencerHistory.retainedBytes() ==
            before.retainedBytes + expectedPatternRetainedBytes(kind, storage)
        );

        const auto committed = tx::captureStateInvariant(h.state);
        h.state.flushProjectMutationCoalescing();
        tx::assertMaxPlusOneStillArmed(0U);
        tx::assertStateInvariant(h.state, committed);
    }

    assertEditorAndActiveBankMusicalSnapshot(h, expectedAfter);
    assert(h.state.sequencer.pattern.isEnabled(1U));
    assert(h.state.sequencerTracks.track(0U).isEnabled(1U));
    if (storage == seq::SequencerHistoryPatternStorage::FullGraph && hasGraph(kind)) {
        assert(h.state.sequencer.pattern.graph.get() != editorGraph);
        assert(h.state.sequencerTracks.track(0U).graph.get() != beforeBankGraph);
    } else {
        assert(h.state.sequencer.pattern.graph.get() == editorGraph);
        assert(h.state.sequencerTracks.track(0U).graph.get() == beforeBankGraph);
    }
    if (storage == seq::SequencerHistoryPatternStorage::FullGraph && hasCc(kind)) {
        assert(h.state.sequencer.pattern.ccLanes.get() != editorCc);
        assert(h.state.sequencerTracks.track(0U).ccLanes.get() != beforeBankCc);
    } else {
        assert(h.state.sequencer.pattern.ccLanes.get() == editorCc);
        assert(h.state.sequencerTracks.track(0U).ccLanes.get() == beforeBankCc);
    }
    assert(
        h.state.sequencerTracks.track(0U).graphRevision.get() ==
        h.state.sequencer.pattern.graphRevision.get()
    );
    assert(
        h.state.sequencerTracks.track(0U).ccLaneRevision.get() ==
        h.state.sequencer.pattern.ccLaneRevision.get()
    );

    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    const auto beforeUndo = tx::captureStateInvariant(h.state);
    assert(h.state.undoSequencerHistory());
    assertEditorAndActiveBankMusicalSnapshot(h, expectedBefore);
    assert(!h.state.sequencer.pattern.isEnabled(1U));
    assert(!h.state.sequencerTracks.track(0U).isEnabled(1U));
    assert(
        h.state.project.metadata.modifiedCounter ==
        beforeUndo.modifiedCounter + 1U
    );
    assert(h.state.hasPendingProjectSessionSave());
    assertNoDeferredPublication(h);

    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    const auto beforeRedo = tx::captureStateInvariant(h.state);
    assert(h.state.redoSequencerHistory());
    assertEditorAndActiveBankMusicalSnapshot(h, expectedAfter);
    assert(h.state.sequencer.pattern.isEnabled(1U));
    assert(h.state.sequencerTracks.track(0U).isEnabled(1U));
    assert(
        h.state.project.metadata.modifiedCounter ==
        beforeRedo.modifiedCounter + 1U
    );
    assert(h.state.hasPendingProjectSessionSave());
    assertNoDeferredPublication(h);
}

void test_pattern_commits_are_nofail_and_exactly_once() {
    runPatternCommit(
        PayloadKind::None,
        seq::SequencerHistoryPatternStorage::FlatOnly
    );
    runPatternCommit(
        PayloadKind::None,
        seq::SequencerHistoryPatternStorage::FullGraph
    );
    runPatternCommit(
        PayloadKind::Graph,
        seq::SequencerHistoryPatternStorage::FullGraph
    );
    runPatternCommit(
        PayloadKind::Cc,
        seq::SequencerHistoryPatternStorage::FullGraph
    );
    runPatternCommit(
        PayloadKind::GraphAndCc,
        seq::SequencerHistoryPatternStorage::FullGraph
    );
    std::cout << "[PASS] Pattern prepared commits are no-fail and exactly once\n";
}

void test_pattern_noop_admission_preserves_live_state() {
    Harness h;
    initializeActivePayload(h, PayloadKind::GraphAndCc);
    PreparedPattern noOp;
    assert(preparePattern(
        h,
        seq::SequencerHistoryPatternStorage::FullGraph,
        noOp
    ));
    seq::SequencerState staged;
    stageActivePattern(h, staged);
    assert(seq::capturePreparedHistoryPatternAfterUsingReservedStorage(
        h.state.sequencerTracks,
        staged,
        *noOp.change
    ));
    assert(seq::capturePreparedActiveTrackSynchronizationUsingReservedStorage(
        h.state.sequencerTracks,
        staged,
        noOp.synchronization
    ));

    auto musical = captureFullBankMusicalProof(h);
    const auto before = tx::captureStateInvariant(h.state);
    const auto owners = captureBankOwners(h);
    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state
    );
    assert(!history.canRecordSynchronizedPattern(*noOp.change));
    history.recordPreparedSynchronizedPattern(std::move(noOp.change));
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    tx::assertStateInvariant(h.state, before);
    assertBankOwners(h, owners);
    assertFullBankMusicalProof(h, musical);
    std::cout << "[PASS] Pattern no-op admission preserves the complete live bank\n";
}

void test_pattern_identity_and_flat_cc_drift_are_rejected() {
    {
        Harness h;
        initializeActivePayload(h, PayloadKind::None);
        seq::SequencerHistoryDescriptor descriptor{};
        descriptor.kind = seq::SequencerHistoryActionKind::StepToggle;
        descriptor.trackIndex = 1U;
        auto change = seq::prepareHistoryPatternChangeBefore(
            h.state.sequencerTracks,
            h.state.sequencer,
            0U,
            seq::SequencerHistoryPatternStorage::FlatOnly,
            descriptor
        );
        assert(change != nullptr);
        assert(change->trackIndex == 0U);
        assert(change->descriptor.trackIndex == 0U);

        change->descriptor.trackIndex = 1U;
        assert(!seq::reservePreparedHistoryPatternAfter(
            h.state.sequencerTracks,
            h.state.sequencer,
            *change
        ));
    }

    {
        Harness h;
        initializeActivePayload(h, PayloadKind::GraphAndCc);
        PreparedPattern prepared;
        assert(preparePattern(
            h,
            seq::SequencerHistoryPatternStorage::FullGraph,
            prepared
        ));
        h.state.sequencerTracks.syncSharedTrackState(0x0003U, 1U);
        assert(!seq::preparedActiveTrackSynchronizationMatches(
            h.state.sequencerTracks,
            prepared.synchronization
        ));
        assert(!seq::capturePreparedActiveTrackSynchronizationUsingReservedStorage(
            h.state.sequencerTracks,
            h.state.sequencer,
            prepared.synchronization
        ));
    }

    {
        Harness h;
        initializeActivePayload(h, PayloadKind::GraphAndCc);
        PreparedPattern prepared;
        assert(preparePattern(
            h,
            seq::SequencerHistoryPatternStorage::FlatOnly,
            prepared
        ));
        seq::SequencerState staged;
        stageActivePattern(h, staged);
        seq::SequencerHistoryPatternSnapshot liveMusical;
        tx::captureMusicalSnapshot(h.state, liveMusical);
        const auto liveInvariant = tx::captureStateInvariant(h.state);
        const auto liveOwners = captureBankOwners(h);
        const auto* bankCcOwner = h.state.sequencerTracks.track(0U).ccLanes.get();
        const uint32_t bankCcRevision =
            h.state.sequencerTracks.track(0U).ccLaneRevision.get();
        auto* stagedCc = staged.pattern.ccLanes.get();
        assert(stagedCc != nullptr);
        assert(seq::setSequencerCcLaneEvent(*stagedCc, 0U, 0U, 42U).changed());
        staged.pattern.bumpCcLaneRevision();
        staged.pattern.setEnabled(1U, true);
        assert(seq::capturePreparedHistoryPatternAfterUsingReservedStorage(
            h.state.sequencerTracks,
            staged,
            *prepared.change
        ));

        auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
            h.state
        );
        assert(!history.canRecordPattern(*prepared.change));
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
        tx::assertStateInvariant(h.state, liveInvariant);
        assertBankOwners(h, liveOwners);
        tx::assertMusicalSnapshot(h.state, liveMusical);
        assert(h.state.sequencerTracks.track(0U).ccLanes.get() == bankCcOwner);
        assert(
            h.state.sequencerTracks.track(0U).ccLaneRevision.get() ==
            bankCcRevision
        );
    }

    std::cout << "[PASS] Pattern Track identity and FlatOnly CC drift gates\n";
}

void test_partial_pattern_reservation_is_discardable() {
    for (const std::size_t ordinal : {6U, 7U}) {
        Harness h;
        initializeActivePayload(h, PayloadKind::GraphAndCc);
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        PreparedPattern partial;
        assert(!preparePattern(
            h,
            seq::SequencerHistoryPatternStorage::FullGraph,
            partial
        ));
        tx::assertFailureConsumed(ordinal);
        assert(partial.change != nullptr);
        assert(partial.change->before.graph != nullptr);
        assert(partial.change->before.ccLanes != nullptr);
        assert(partial.change->after.graph != nullptr);
        assert(partial.change->after.ccLanes != nullptr);
        assert(!partial.synchronization.reserved);
        assert(!partial.synchronization.captured);
        assert(
            static_cast<bool>(partial.synchronization.payload.graph) ==
            (ordinal == 7U)
        );
        assert(partial.synchronization.payload.ccLanes == nullptr);
    }

    {
        Harness h;
        initializeActivePayload(h, PayloadKind::GraphAndCc);
        seq::SequencerPreparedActiveTrackSynchronization reservedOnly;
        assert(seq::reservePreparedActiveTrackSynchronization(
            h.state.sequencerTracks,
            h.state.sequencer,
            0U,
            seq::SequencerHistoryPatternStorage::FullGraph,
            reservedOnly
        ));
        assert(reservedOnly.reserved);
        assert(!reservedOnly.captured);
        assert(seq::preparedActiveTrackSynchronizationMatches(
            h.state.sequencerTracks,
            reservedOnly
        ));

        seq::SequencerState staged;
        stageActivePattern(h, staged);
        staged.pattern.setEnabled(1U, true);
        seq::SequencerHistoryPatternSnapshot musicalBaseline;
        tx::captureMusicalSnapshot(h.state, musicalBaseline);
        const auto invariant = tx::captureStateInvariant(h.state);
        const auto owners = captureBankOwners(h);
        seq::publishPreparedActiveTrackSynchronization(
            h.state.sequencerTracks,
            staged,
            std::move(reservedOnly)
        );
        tx::assertStateInvariant(h.state, invariant);
        assertBankOwners(h, owners);
        tx::assertMusicalSnapshot(h.state, musicalBaseline);
    }

    std::cout << "[PASS] partial prepared bundles remain safely discardable\n";
}

void test_generic_publication_resynchronizes_switched_track_payloads() {
    Harness h;
    initializeActivePayload(h, PayloadKind::GraphAndCc);
    authorPayload(
        h.state.sequencerTracks.track(1U),
        PayloadKind::GraphAndCc,
        1U
    );
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    assert(seq::switchActiveTrack(
        h.state.sequencerTracks,
        h.state.sequencer,
        1U
    ));

    const auto* editorGraph = seq::graphView(h.state.sequencer.pattern);
    const auto* spareGraph = seq::graphView(h.state.sequencerTracks.track(1U));
    assert(editorGraph != nullptr);
    assert(spareGraph != nullptr);
    assert(
        h.state.sequencer.pattern.graphRevision.get() ==
        h.state.sequencerTracks.track(1U).graphRevision.get()
    );
    assert(
        editorGraph->stepNode(seq::rootStepNodeId(0U))->noteOffset !=
        spareGraph->stepNode(seq::rootStepNodeId(0U))->noteOffset
    );

    PreparedPattern staleFlat;
    assert(!preparePattern(
        h,
        seq::SequencerHistoryPatternStorage::FlatOnly,
        staleFlat
    ));

    h.state.sequencer.pattern.setEnabled(2U, true);
    test_support::drainNotifications();
    assert(h.state.hasPendingProjectMutationCoalescing());
    h.state.flushProjectMutationCoalescing();
    assert(!h.state.hasPendingProjectMutationCoalescing());

    const auto& bankPattern = h.state.sequencerTracks.track(1U);
    const auto* synchronizedGraph = seq::graphView(bankPattern);
    assert(synchronizedGraph != nullptr);
    assert(
        synchronizedGraph->stepNode(seq::rootStepNodeId(0U))->noteOffset ==
        editorGraph->stepNode(seq::rootStepNodeId(0U))->noteOffset
    );
    assert(seq::sameOptionalSequencerCcLaneBank(
        seq::sequencerCcLaneView(bankPattern),
        seq::sequencerCcLaneView(h.state.sequencer.pattern)
    ));
    assert(bankPattern.isEnabled(2U));
    assert(
        bankPattern.graphRevision.get() ==
        h.state.sequencer.pattern.graphRevision.get()
    );
    assert(
        bankPattern.ccLaneRevision.get() ==
        h.state.sequencer.pattern.ccLaneRevision.get()
    );

    std::cout << "[PASS] generic publication fully resynchronizes switched Track payloads\n";
}

void test_flat_sync_accepts_coherent_cold_payload_revision_drift() {
    Harness h;
    initializeActivePayload(h, PayloadKind::GraphAndCc);
    auto& bankPattern = h.state.sequencerTracks.track(0U);
    bankPattern.graphRevision.set(
        h.state.sequencer.pattern.graphRevision.get() + 7U
    );
    bankPattern.ccLaneRevision.set(
        h.state.sequencer.pattern.ccLaneRevision.get() + 9U
    );

    PreparedPattern prepared;
    assert(preparePattern(
        h,
        seq::SequencerHistoryPatternStorage::FlatOnly,
        prepared
    ));
    seq::SequencerState staged;
    stageActivePattern(h, staged);
    staged.pattern.setEnabled(1U, true);
    assert(seq::capturePreparedHistoryPatternAfterUsingReservedStorage(
        h.state.sequencerTracks,
        staged,
        *prepared.change
    ));
    assert(seq::capturePreparedActiveTrackSynchronizationUsingReservedStorage(
        h.state.sequencerTracks,
        staged,
        prepared.synchronization
    ));
    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state
    );
    assert(history.canRecordSynchronizedPattern(*prepared.change));

    publishStagedFlatPattern(h, staged);
    seq::publishPreparedActiveTrackSynchronization(
        h.state.sequencerTracks,
        h.state.sequencer,
        std::move(prepared.synchronization)
    );
    history.recordPreparedSynchronizedPattern(std::move(prepared.change));

    assert(
        bankPattern.graphRevision.get() ==
        h.state.sequencer.pattern.graphRevision.get()
    );
    assert(
        bankPattern.ccLaneRevision.get() ==
        h.state.sequencer.pattern.ccLaneRevision.get()
    );
    assertNoDeferredPublication(h);
    std::cout << "[PASS] Flat sync repairs coherent cold-payload revision drift\n";
}

void test_prepared_publication_is_exact_during_notification_drain() {
    Harness h;
    initializeActivePayload(h, PayloadKind::None);
    PreparedPattern prepared;
    assert(preparePattern(
        h,
        seq::SequencerHistoryPatternStorage::FlatOnly,
        prepared
    ));
    seq::SequencerState staged;
    stageActivePattern(h, staged);
    staged.pattern.setEnabled(1U, true);
    assert(seq::capturePreparedHistoryPatternAfterUsingReservedStorage(
        h.state.sequencerTracks,
        staged,
        *prepared.change
    ));
    assert(seq::capturePreparedActiveTrackSynchronizationUsingReservedStorage(
        h.state.sequencerTracks,
        staged,
        prepared.synchronization
    ));
    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state
    );
    assert(history.canRecordSynchronizedPattern(*prepared.change));
    const auto before = tx::captureStateInvariant(h.state);

    oc::state::Signal<uint8_t> trigger{0U};
    oc::state::Signal<uint8_t> unrelated{0U};
    bool committed = false;
    uint8_t unrelatedNotifications = 0U;
    auto unrelatedSubscription = unrelated.subscribe(
        [&](const uint8_t&) { ++unrelatedNotifications; }
    );
    auto subscription = trigger.subscribe([&](const uint8_t&) {
        assert(seq::preparedActiveTrackSynchronizationMatches(
            h.state.sequencerTracks,
            prepared.synchronization
        ));
        publishStagedFlatPattern(h, staged);
        seq::publishPreparedActiveTrackSynchronization(
            h.state.sequencerTracks,
            h.state.sequencer,
            std::move(prepared.synchronization)
        );
        history.recordPreparedSynchronizedPattern(std::move(prepared.change));
        committed = true;
    });
    assert(subscription.isValid());
    assert(unrelatedSubscription.isValid());

    trigger.set(1U);
    // Queue a watched callback after the committing callback in the same
    // processing wave. Targeted coalescer consumption must cancel it without
    // draining or suppressing unrelated notifications.
    h.state.sequencer.page.set(1U);
    unrelated.set(1U);
    test_support::drainNotifications();
    assert(committed);
    assert(unrelatedNotifications == 1U);
    assert(!h.state.hasPendingProjectMutationCoalescing());
    assertExactlyOnePublication(h.state, before);
    assertNoDeferredPublication(h);

    std::cout << "[PASS] prepared publication remains exact inside notification drain\n";
}

void test_legacy_prepared_pattern_preserves_pending_bank_synchronization() {
    Harness h;
    initializeActivePayload(h, PayloadKind::None);
    seq::SequencerHistoryDescriptor descriptor{};
    descriptor.kind = seq::SequencerHistoryActionKind::CcLaneEventEdit;
    auto change = seq::prepareHistoryPatternChangeBefore(
        h.state.sequencerTracks,
        h.state.sequencer,
        0U,
        seq::SequencerHistoryPatternStorage::FullGraph,
        descriptor
    );
    assert(change != nullptr);
    assert(seq::reservePreparedHistoryPatternAfter(
        h.state.sequencerTracks,
        h.state.sequencer,
        *change
    ));

    h.state.sequencer.pattern.setEnabled(1U, true);
    assert(seq::capturePreparedHistoryPatternAfterUsingReservedStorage(
        h.state.sequencerTracks,
        h.state.sequencer,
        *change
    ));
    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state
    );
    assert(history.canRecordPattern(*change));
    assert(!h.state.sequencerTracks.track(0U).isEnabled(1U));

    history.recordPreparedPattern(std::move(change));
    assert(!h.state.sequencerTracks.track(0U).isEnabled(1U));
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    assert(h.state.sequencerTracks.track(0U).isEnabled(1U));
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.projectHistory.undoCount() == 1U);
    std::cout << "[PASS] legacy prepared Pattern retains delayed bank synchronization\n";
}

struct PreparedFullBank {
    seq::SequencerHistoryFullBankChangePtr change;
};

bool prepareFullBank(Harness& h, PreparedFullBank& out) {
    seq::SequencerHistoryDescriptor descriptor{};
    descriptor.kind = seq::SequencerHistoryActionKind::FullBank;
    out.change = seq::prepareHistoryFullBankChangeBefore(
        h.state.sequencerTracks,
        h.state.sequencer,
        descriptor
    );
    return out.change && seq::reservePreparedHistoryFullBankAfter(
        h.state.sequencerTracks,
        h.state.sequencer,
        *out.change
    );
}

ExpectedAllocationRequests expectedFullBankAllocationRequests(
    PayloadKind kind,
    uint16_t authoredBankMask
) {
    ExpectedAllocationRequests expected;
    expected.push(sizeof(seq::SequencerHistoryFullBankChange));
    for (uint8_t snapshot = 0U; snapshot < 2U; ++snapshot) {
        appendPayloadRequests(expected, kind);  // editor
        for (uint8_t track = 0U;
             track < seq::SequencerTrackBankState::TRACK_COUNT;
             ++track) {
            const bool populated = track == 0U ||
                (authoredBankMask & seq::sequencerHistoryTrackBit(track)) != 0U;
            if (populated) appendPayloadRequests(expected, kind);
        }
    }
    return expected;
}

std::size_t fullBankGraphOwnerCount(
    const seq::SequencerHistoryTrackBankSnapshot& snapshot
) {
    std::size_t count = snapshot.editorGraph ? 1U : 0U;
    for (const auto& owner : snapshot.bankGraphs) {
        if (owner) ++count;
    }
    return count;
}

std::size_t fullBankCcOwnerCount(
    const seq::SequencerHistoryTrackBankSnapshot& snapshot
) {
    std::size_t count = snapshot.editorCcLanes ? 1U : 0U;
    for (const auto& owner : snapshot.bankCcLanes) {
        if (owner) ++count;
    }
    return count;
}

std::size_t expectedFullBankRetainedBytes(
    const seq::SequencerHistoryFullBankChange& change
) {
    constexpr std::size_t allocationHeaderBytes = 16U;
    const std::size_t graphOwners =
        fullBankGraphOwnerCount(change.before) +
        fullBankGraphOwnerCount(change.after);
    const std::size_t ccOwners =
        fullBankCcOwnerCount(change.before) +
        fullBankCcOwnerCount(change.after);
    return sizeof(seq::SequencerHistoryFullBankChange) + allocationHeaderBytes +
        graphOwners * (
            sizeof(oc::note::sequencer::StepSequencerGraph) +
            allocationHeaderBytes
        ) +
        ccOwners * (sizeof(seq::SequencerCcLaneBank) + allocationHeaderBytes);
}

void assertFullBankOwners(
    const PreparedFullBank& prepared,
    std::size_t ownersPerPayload
) {
    assert(prepared.change != nullptr);
    assert(fullBankGraphOwnerCount(prepared.change->before) == ownersPerPayload);
    assert(fullBankGraphOwnerCount(prepared.change->after) == ownersPerPayload);
    assert(fullBankCcOwnerCount(prepared.change->before) == ownersPerPayload);
    assert(fullBankCcOwnerCount(prepared.change->after) == ownersPerPayload);
}

void verifyFullBankPreparationFailure(
    PayloadKind kind,
    uint16_t authoredBankMask,
    std::size_t ordinal
) {
    Harness h;
    initializeCapturedTracks(h, kind, authoredBankMask);
    auto musical = captureFullBankMusicalProof(h);
    const auto invariant = tx::captureStateInvariant(h.state);
    const auto owners = captureBankOwners(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        PreparedFullBank rejected;
        assert(!prepareFullBank(h, rejected));
        tx::assertFailureConsumed(ordinal);
        tx::assertStateInvariant(h.state, invariant);
        assertBankOwners(h, owners);
    }

    assertFullBankMusicalProof(h, musical);
}

void verifyFullBankPreparationRatchet(
    PayloadKind kind,
    uint16_t authoredBankMask,
    std::size_t expectedAttempts,
    std::size_t ownersPerPayload
) {
    Harness h;
    initializeCapturedTracks(h, kind, authoredBankMask);
    auto musical = captureFullBankMusicalProof(h);
    const auto invariant = tx::captureStateInvariant(h.state);
    const auto owners = captureBankOwners(h);
    const auto expectedRequests = expectedFullBankAllocationRequests(
        kind,
        authoredBankMask
    );

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        allocation_trace::Scope allocationTrace;
        PreparedFullBank prepared;
        assert(prepareFullBank(h, prepared));
        if (kind == PayloadKind::GraphAndCc) {
            assertFullBankOwners(prepared, ownersPerPayload);
        }
        assertAllocationRequests(expectedRequests);
        tx::assertMaxPlusOneStillArmed(expectedAttempts);
        tx::assertStateInvariant(h.state, invariant);
        assertBankOwners(h, owners);
    }

    assertFullBankMusicalProof(h, musical);
}

void test_full_bank_preparation_allocation_matrix() {
    struct Case {
        PayloadKind kind;
        std::size_t expectedAttempts;
    };
    constexpr std::array<Case, 4> activeCases{{
        {PayloadKind::None, 1U},
        {PayloadKind::Graph, 5U},
        {PayloadKind::Cc, 5U},
        {PayloadKind::GraphAndCc, 9U},
    }};

    for (const auto& item : activeCases) {
        for (std::size_t ordinal = 1U;
             ordinal <= item.expectedAttempts;
             ++ordinal) {
            verifyFullBankPreparationFailure(item.kind, 0x0001U, ordinal);
        }
        verifyFullBankPreparationRatchet(
            item.kind,
            0x0001U,
            item.expectedAttempts,
            2U
        );
    }

    constexpr std::size_t maximumAttempts = 69U;
    for (std::size_t ordinal = 1U; ordinal <= maximumAttempts; ++ordinal) {
        verifyFullBankPreparationFailure(
            PayloadKind::GraphAndCc,
            0xFFFFU,
            ordinal
        );
    }
    verifyFullBankPreparationRatchet(
        PayloadKind::GraphAndCc,
        0xFFFFU,
        maximumAttempts,
        17U
    );

    std::cout << "[PASS] FullBank preparation allocation matrix\n";
}

void test_full_bank_noop_budget_and_pruning() {
    {
        Harness h;
        initializeActivePayload(h, PayloadKind::None);
        auto musical = captureFullBankMusicalProof(h);
        const auto before = tx::captureStateInvariant(h.state);
        const auto owners = captureBankOwners(h);
        PreparedFullBank noOp;
        assert(prepareFullBank(h, noOp));
        assert(seq::capturePreparedHistoryFullBankAfterUsingReservedStorage(
            h.state.sequencerTracks,
            h.state.sequencer,
            *noOp.change
        ));
        auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
            h.state
        );
        assert(!history.canRecordFullBank(*noOp.change));
        history.recordPreparedFullBank(std::move(noOp.change));
        test_support::drainNotifications();
        h.state.flushProjectMutationCoalescing();
        tx::assertStateInvariant(h.state, before);
        assertBankOwners(h, owners);
        assertFullBankMusicalProof(h, musical);
    }

    {
        Harness h;
        initializeCapturedTracks(h, PayloadKind::GraphAndCc, 0xFFFFU);
        PreparedFullBank maximum;
        assert(prepareFullBank(h, maximum));
        h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
        assert(seq::capturePreparedHistoryFullBankAfterUsingReservedStorage(
            h.state.sequencerTracks,
            h.state.sequencer,
            *maximum.change
        ));
        auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
            h.state
        );
        assert(history.canRecordFullBank(*maximum.change));
        history.recordPreparedFullBank(std::move(maximum.change));
        assert(
            h.state.sequencerHistory.retainedBytes() <=
            seq::SequencerHistoryService::RETAINED_BYTE_BUDGET
        );
    }

    {
        Harness h;
        initializeActivePayload(h, PayloadKind::None);
        auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
            h.state
        );
        const auto modifiedBefore = h.state.project.metadata.modifiedCounter;
        for (uint8_t index = 0U; index < 5U; ++index) {
            PreparedFullBank prepared;
            assert(prepareFullBank(h, prepared));
            const uint16_t nextMask =
                h.state.sequencerTracks.currentEnabledMask() == 0x0001U
                    ? 0x0003U
                    : 0x0001U;
            h.state.sequencerTracks.syncSharedTrackState(nextMask, 0U);
            assert(seq::capturePreparedHistoryFullBankAfterUsingReservedStorage(
                h.state.sequencerTracks,
                h.state.sequencer,
                *prepared.change
            ));
            assert(history.canRecordFullBank(*prepared.change));
            history.recordPreparedFullBank(std::move(prepared.change));
        }
        assert(
            h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::FullBank) ==
            seq::SequencerHistoryService::FULL_BANK_ENTRY_LIMIT
        );
        assert(
            h.state.projectHistory.undoCount() ==
            seq::SequencerHistoryService::FULL_BANK_ENTRY_LIMIT
        );
        assert(h.state.project.metadata.modifiedCounter == modifiedBefore + 5U);
    }

    std::cout << "[PASS] FullBank no-op, budget, and pruning\n";
}

void test_full_bank_commit_is_nofail_and_exactly_once() {
    Harness h;
    initializeActivePayload(h, PayloadKind::GraphAndCc);
    PreparedFullBank prepared;
    assert(prepareFullBank(h, prepared));
    seq::SequencerTrackBankState stagedBank;
    seq::SequencerState stagedActive;
    stageTrackBank(h, stagedBank, stagedActive);
    stagedBank.syncSharedTrackState(0x0003U, 0U);
    assert(seq::capturePreparedHistoryFullBankAfterUsingReservedStorage(
        stagedBank,
        stagedActive,
        *prepared.change
    ));
    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state
    );
    assert(history.canRecordFullBank(*prepared.change));
    const auto before = tx::captureStateInvariant(h.state);
    const std::size_t expectedRetained = expectedFullBankRetainedBytes(
        *prepared.change
    );

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        seq::installTrackBankState(
            h.state.sequencerTracks,
            h.state.sequencer,
            stagedBank,
            stagedActive
        );
        history.recordPreparedFullBank(std::move(prepared.change));
        tx::assertMaxPlusOneStillArmed(0U);
        assertExactlyOnePublication(h.state, before);
        assert(
            h.state.sequencerHistory.retainedBytes() ==
            before.retainedBytes + expectedRetained
        );

        const auto committed = tx::captureStateInvariant(h.state);
        h.state.flushProjectMutationCoalescing();
        tx::assertMaxPlusOneStillArmed(0U);
        tx::assertStateInvariant(h.state, committed);
    }

    assertSharedTrackProjection(h, 0x0003U, 0U);
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    const auto beforeUndo = tx::captureStateInvariant(h.state);
    assert(h.state.undoSequencerHistory());
    assertSharedTrackProjection(h, 0x0001U, 0U);
    assert(
        h.state.project.metadata.modifiedCounter ==
        beforeUndo.modifiedCounter + 1U
    );
    assert(h.state.hasPendingProjectSessionSave());
    assertNoDeferredPublication(h);
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    const auto beforeRedo = tx::captureStateInvariant(h.state);
    assert(h.state.redoSequencerHistory());
    assertSharedTrackProjection(h, 0x0003U, 0U);
    assert(
        h.state.project.metadata.modifiedCounter ==
        beforeRedo.modifiedCounter + 1U
    );
    assert(h.state.hasPendingProjectSessionSave());
    assertNoDeferredPublication(h);

    std::cout << "[PASS] FullBank prepared commit is no-fail and exactly once\n";
}

struct PreparedStructure {
    seq::SequencerHistoryTrackStructureChangePtr change;
};

bool prepareStructure(
    Harness& h,
    uint16_t trackMask,
    PreparedStructure& out
) {
    seq::SequencerHistoryDescriptor descriptor{};
    descriptor.kind = seq::SequencerHistoryActionKind::TrackStructure;
    out.change = seq::prepareHistoryStructureChangeBefore(
        h.state.sequencerTracks,
        h.state.sequencer,
        trackMask,
        descriptor
    );
    return out.change && seq::reservePreparedHistoryStructureAfter(
        h.state.sequencerTracks,
        h.state.sequencer,
        *out.change
    );
}

ExpectedAllocationRequests expectedStructureAllocationRequests(
    PayloadKind kind,
    uint16_t trackMask
) {
    ExpectedAllocationRequests expected;
    expected.push(sizeof(seq::SequencerHistoryTrackStructureChange));
    for (uint8_t snapshot = 0U; snapshot < 2U; ++snapshot) {
        for (uint8_t track = 0U;
             track < seq::SequencerTrackBankState::TRACK_COUNT;
             ++track) {
            if ((trackMask & seq::sequencerHistoryTrackBit(track)) != 0U) {
                appendPayloadRequests(expected, kind);
            }
        }
    }
    return expected;
}

void test_prepared_bank_admission_rejects_active_step_draft() {
    {
        Harness h;
        initializeActivePayload(h, PayloadKind::Graph);
        PreparedFullBank prepared;
        assert(prepareFullBank(h, prepared));
        seq::SequencerTrackBankState stagedBank;
        seq::SequencerState stagedActive;
        stageTrackBank(h, stagedBank, stagedActive);
        assert(stagedActive.pattern.setStepDataAt(
            0U,
            61U,
            91U,
            seq::SequencerPatternState::DEFAULT_GATE_PERCENT
        ));
        assert(seq::capturePreparedHistoryFullBankAfterUsingReservedStorage(
            stagedBank,
            stagedActive,
            *prepared.change
        ));
        assert(h.state.sequencerHistory.canRecordFullBank(*prepared.change));
        const auto nodeId = seq::rootStepNodeId(0U);
        assert(seq::beginStepContentDraft(
            h.state.sequencer,
            seq::SequencerStepContentDraftKind::CHORD,
            0U,
            nodeId
        ));
        assert(seq::setAuthoringNodeChordMode(
            h.state.sequencer,
            nodeId,
            oc::note::sequencer::StepSequencerChordMode::Local
        ));
        assert(h.state.sequencer.stepContentDraft.modified());
        test_support::drainNotifications();
        const auto before = tx::captureStateInvariant(h.state);
        const auto owners = captureBankOwners(h);
        const auto draft = captureDraftInvariant(h);
        auto musical = captureFullBankMusicalProof(h);
        auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
            h.state
        );
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(!history.canRecordFullBank(*prepared.change));
            history.recordPreparedFullBank(std::move(prepared.change));
            tx::assertMaxPlusOneStillArmed(0U);
            tx::assertStateInvariant(h.state, before);
            assertBankOwners(h, owners);
            assertDraftInvariant(h, draft);
        }
        assertFullBankMusicalProof(h, musical);
        assertNoDeferredPublication(h);
        assertDraftInvariant(h, draft);
        assertSharedTrackProjection(h, 0x0001U, 0U);
    }

    {
        Harness h;
        initializeActivePayload(h, PayloadKind::Graph);
        PreparedStructure prepared;
        assert(prepareStructure(h, 0x0001U, prepared));
        seq::SequencerTrackBankState stagedBank;
        seq::SequencerState stagedActive;
        stageTrackBank(h, stagedBank, stagedActive);
        assert(stagedActive.pattern.setStepDataAt(
            0U,
            61U,
            91U,
            seq::SequencerPatternState::DEFAULT_GATE_PERCENT
        ));
        assert(seq::capturePreparedHistoryStructureAfterUsingReservedStorage(
            stagedBank,
            stagedActive,
            *prepared.change
        ));
        assert(h.state.sequencerHistory.canRecordStructure(*prepared.change));
        const auto nodeId = seq::rootStepNodeId(0U);
        assert(seq::beginStepContentDraft(
            h.state.sequencer,
            seq::SequencerStepContentDraftKind::CHORD,
            0U,
            nodeId
        ));
        assert(seq::setAuthoringNodeChordMode(
            h.state.sequencer,
            nodeId,
            oc::note::sequencer::StepSequencerChordMode::Local
        ));
        assert(h.state.sequencer.stepContentDraft.modified());
        test_support::drainNotifications();
        const auto before = tx::captureStateInvariant(h.state);
        const auto owners = captureBankOwners(h);
        const auto draft = captureDraftInvariant(h);
        auto musical = captureFullBankMusicalProof(h);
        auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
            h.state
        );
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(!history.canRecordStructure(*prepared.change));
            history.recordPreparedStructure(std::move(prepared.change));
            tx::assertMaxPlusOneStillArmed(0U);
            tx::assertStateInvariant(h.state, before);
            assertBankOwners(h, owners);
            assertDraftInvariant(h, draft);
        }
        assertFullBankMusicalProof(h, musical);
        assertNoDeferredPublication(h);
        assertDraftInvariant(h, draft);
        assertSharedTrackProjection(h, 0x0001U, 0U);
    }

    std::cout << "[PASS] prepared bank admission rejects an active Step Draft\n";
}

std::size_t structureGraphOwnerCount(
    const seq::SequencerHistoryTrackStructureSnapshot& snapshot
) {
    std::size_t count = 0U;
    for (const auto& track : snapshot.tracks) {
        if (track.graph) ++count;
    }
    return count;
}

std::size_t structureCcOwnerCount(
    const seq::SequencerHistoryTrackStructureSnapshot& snapshot
) {
    std::size_t count = 0U;
    for (const auto& track : snapshot.tracks) {
        if (track.ccLanes) ++count;
    }
    return count;
}

std::size_t expectedStructureRetainedBytes(
    const seq::SequencerHistoryTrackStructureChange& change
) {
    constexpr std::size_t allocationHeaderBytes = 16U;
    const std::size_t graphOwners =
        structureGraphOwnerCount(change.before) +
        structureGraphOwnerCount(change.after);
    const std::size_t ccOwners =
        structureCcOwnerCount(change.before) +
        structureCcOwnerCount(change.after);
    return sizeof(seq::SequencerHistoryTrackStructureChange) +
        allocationHeaderBytes +
        graphOwners * (
            sizeof(oc::note::sequencer::StepSequencerGraph) +
            allocationHeaderBytes
        ) +
        ccOwners * (sizeof(seq::SequencerCcLaneBank) + allocationHeaderBytes);
}

void assertStructureOwners(
    const PreparedStructure& prepared,
    PayloadKind kind,
    std::size_t capturedTrackCount
) {
    assert(prepared.change != nullptr);
    const std::size_t expectedGraph = hasGraph(kind) ? capturedTrackCount : 0U;
    const std::size_t expectedCc = hasCc(kind) ? capturedTrackCount : 0U;
    assert(structureGraphOwnerCount(prepared.change->before) == expectedGraph);
    assert(structureGraphOwnerCount(prepared.change->after) == expectedGraph);
    assert(structureCcOwnerCount(prepared.change->before) == expectedCc);
    assert(structureCcOwnerCount(prepared.change->after) == expectedCc);
}

void verifyStructurePreparationFailure(
    PayloadKind kind,
    uint16_t trackMask,
    std::size_t ordinal
) {
    Harness h;
    initializeCapturedTracks(h, kind, trackMask);
    auto musical = captureFullBankMusicalProof(h);
    const auto invariant = tx::captureStateInvariant(h.state);
    const auto owners = captureBankOwners(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        PreparedStructure rejected;
        assert(!prepareStructure(h, trackMask, rejected));
        tx::assertFailureConsumed(ordinal);
        tx::assertStateInvariant(h.state, invariant);
        assertBankOwners(h, owners);
    }

    assertFullBankMusicalProof(h, musical);
}

void verifyStructurePreparationRatchet(
    PayloadKind kind,
    uint16_t trackMask,
    std::size_t expectedAttempts,
    std::size_t capturedTrackCount
) {
    Harness h;
    initializeCapturedTracks(h, kind, trackMask);
    auto musical = captureFullBankMusicalProof(h);
    const auto invariant = tx::captureStateInvariant(h.state);
    const auto owners = captureBankOwners(h);
    const auto expectedRequests = expectedStructureAllocationRequests(
        kind,
        trackMask
    );

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        allocation_trace::Scope allocationTrace;
        PreparedStructure prepared;
        assert(prepareStructure(h, trackMask, prepared));
        assertStructureOwners(prepared, kind, capturedTrackCount);
        assertAllocationRequests(expectedRequests);
        tx::assertMaxPlusOneStillArmed(expectedAttempts);
        tx::assertStateInvariant(h.state, invariant);
        assertBankOwners(h, owners);
    }

    assertFullBankMusicalProof(h, musical);
}

void test_structure_preparation_allocation_matrix() {
    struct Case {
        PayloadKind kind;
        std::size_t expectedAttempts;
    };
    constexpr std::array<Case, 4> activeCases{{
        {PayloadKind::None, 1U},
        {PayloadKind::Graph, 3U},
        {PayloadKind::Cc, 3U},
        {PayloadKind::GraphAndCc, 5U},
    }};

    for (const auto& item : activeCases) {
        for (std::size_t ordinal = 1U;
             ordinal <= item.expectedAttempts;
             ++ordinal) {
            verifyStructurePreparationFailure(item.kind, 0x0001U, ordinal);
        }
        verifyStructurePreparationRatchet(
            item.kind,
            0x0001U,
            item.expectedAttempts,
            1U
        );
    }

    struct MaskCase {
        uint16_t mask;
        std::size_t capturedTrackCount;
        std::size_t expectedAttempts;
    };
    constexpr std::array<MaskCase, 3> maskCases{{
        {0x0001U, 1U, 5U},
        {0x0003U, 2U, 9U},
        {0xFFFFU, 16U, 65U},
    }};
    for (const auto& item : maskCases) {
        for (std::size_t ordinal = 1U;
             ordinal <= item.expectedAttempts;
             ++ordinal) {
            verifyStructurePreparationFailure(
                PayloadKind::GraphAndCc,
                item.mask,
                ordinal
            );
        }
        verifyStructurePreparationRatchet(
            PayloadKind::GraphAndCc,
            item.mask,
            item.expectedAttempts,
            item.capturedTrackCount
        );
    }

    std::cout << "[PASS] Structure preparation allocation matrix\n";
}

void test_structure_noop_and_commit_are_exact() {
    {
        Harness h;
        initializeActivePayload(h, PayloadKind::None);
        auto musical = captureFullBankMusicalProof(h);
        const auto before = tx::captureStateInvariant(h.state);
        const auto owners = captureBankOwners(h);
        PreparedStructure noOp;
        assert(prepareStructure(h, 0x0001U, noOp));
        assert(seq::capturePreparedHistoryStructureAfterUsingReservedStorage(
            h.state.sequencerTracks,
            h.state.sequencer,
            *noOp.change
        ));
        auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
            h.state
        );
        assert(!history.canRecordStructure(*noOp.change));
        history.recordPreparedStructure(std::move(noOp.change));
        test_support::drainNotifications();
        h.state.flushProjectMutationCoalescing();
        tx::assertStateInvariant(h.state, before);
        assertBankOwners(h, owners);
        assertFullBankMusicalProof(h, musical);
    }

    Harness h;
    initializeCapturedTracks(h, PayloadKind::GraphAndCc, 0x0003U);
    PreparedStructure prepared;
    assert(prepareStructure(h, 0x0003U, prepared));
    seq::SequencerTrackBankState stagedBank;
    seq::SequencerState stagedActive;
    stageTrackBank(h, stagedBank, stagedActive);
    stagedBank.syncSharedTrackState(0x0003U, 0U);
    assert(seq::switchActiveTrack(stagedBank, stagedActive, 1U));
    assert(seq::capturePreparedHistoryStructureAfterUsingReservedStorage(
        stagedBank,
        stagedActive,
        *prepared.change
    ));
    const std::size_t expectedRetained = expectedStructureRetainedBytes(
        *prepared.change
    );
    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state
    );
    assert(history.canRecordStructure(*prepared.change));
    const auto before = tx::captureStateInvariant(h.state);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        seq::installTrackBankState(
            h.state.sequencerTracks,
            h.state.sequencer,
            stagedBank,
            stagedActive
        );
        history.recordPreparedStructure(std::move(prepared.change));
        tx::assertMaxPlusOneStillArmed(0U);
        assertExactlyOnePublication(h.state, before);
        assert(
            h.state.sequencerHistory.retainedBytes() ==
            before.retainedBytes + expectedRetained
        );

        const auto committed = tx::captureStateInvariant(h.state);
        h.state.flushProjectMutationCoalescing();
        tx::assertMaxPlusOneStillArmed(0U);
        tx::assertStateInvariant(h.state, committed);
    }

    assertSharedTrackProjection(h, 0x0003U, 1U);
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    const auto beforeUndo = tx::captureStateInvariant(h.state);
    assert(h.state.undoSequencerHistory());
    assertSharedTrackProjection(h, 0x0001U, 0U);
    assert(
        h.state.project.metadata.modifiedCounter ==
        beforeUndo.modifiedCounter + 1U
    );
    assert(h.state.hasPendingProjectSessionSave());
    assertNoDeferredPublication(h);
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    const auto beforeRedo = tx::captureStateInvariant(h.state);
    assert(h.state.redoSequencerHistory());
    assertSharedTrackProjection(h, 0x0003U, 1U);
    assert(
        h.state.project.metadata.modifiedCounter ==
        beforeRedo.modifiedCounter + 1U
    );
    assert(h.state.hasPendingProjectSessionSave());
    assertNoDeferredPublication(h);

    std::cout << "[PASS] Structure no-op and prepared commit are exact\n";
}

void test_structure_frozen_mask_requires_active_track_union() {
    Harness h;
    initializeCapturedTracks(h, PayloadKind::GraphAndCc, 0x0003U);
    PreparedStructure prepared;
    assert(prepareStructure(h, 0x0001U, prepared));
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    assert(seq::switchActiveTrack(
        h.state.sequencerTracks,
        h.state.sequencer,
        1U
    ));
    assert(!seq::capturePreparedHistoryStructureAfterUsingReservedStorage(
        h.state.sequencerTracks,
        h.state.sequencer,
        *prepared.change
    ));
    assert(prepared.change->before.capturedTrackMask == 0x0001U);
    assert(prepared.change->after.capturedTrackMask == 0x0001U);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == 0U);

    std::cout << "[PASS] Structure frozen mask requires before/after Track union\n";
}

void test_presence_growth_is_rejected_by_strict_capture() {
    seq::SequencerState staged;
    staged.reset();
    seq::SequencerHistoryPatternPayloadStorage storage;
    assert(seq::reserveHistoryPatternPayloadStorage(staged.pattern, storage));
    authorPayload(staged.pattern, PayloadKind::GraphAndCc);
    assert(!seq::captureHistoryPatternPayloadUsingReservedStorage(
        staged.pattern,
        storage
    ));
    std::cout << "[PASS] strict capture rejects post-reservation owner growth\n";
}

}  // namespace

int main() {
    test_pattern_preparation_allocation_matrix();
    test_pattern_staged_provider_overlap_contract();
    test_pattern_commits_are_nofail_and_exactly_once();
    test_pattern_noop_admission_preserves_live_state();
    test_pattern_identity_and_flat_cc_drift_are_rejected();
    test_partial_pattern_reservation_is_discardable();
    test_generic_publication_resynchronizes_switched_track_payloads();
    test_flat_sync_accepts_coherent_cold_payload_revision_drift();
    test_prepared_publication_is_exact_during_notification_drain();
    test_legacy_prepared_pattern_preserves_pending_bank_synchronization();
    test_full_bank_preparation_allocation_matrix();
    test_full_bank_noop_budget_and_pruning();
    test_full_bank_commit_is_nofail_and_exactly_once();
    test_structure_preparation_allocation_matrix();
    test_structure_noop_and_commit_are_exact();
    test_structure_frozen_mask_requires_active_track_union();
    test_prepared_bank_admission_rejects_active_step_draft();
    test_presence_growth_is_rejected_by_strict_capture();
    std::cout << "Sequencer prepared History transaction tests passed\n";
    return 0;
}
