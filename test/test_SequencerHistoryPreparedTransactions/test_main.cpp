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
constexpr std::size_t ARM_FULL_BANK_SPANS = 65U;
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
    ARM_FULL_BANK_CHANGE_BYTES + 32U * (ARM_GRAPH_BYTES + ARM_CC_BYTES) +
            ARM_FULL_BANK_SPANS * ARM_ALLOCATION_HEADER_BYTES ==
        528224U,
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
    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
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

// Native fixture only. Production prepared transactions transfer their
// already-admitted owners directly and must not carry this large automatic
// TrackBank snapshot frame into the firmware image.
void installTrackBankStateForTest(
    seq::SequencerTrackBankState& bank,
    seq::SequencerState& active,
    seq::SequencerTrackBankState& stagedBank,
    seq::SequencerState& stagedActive
) {
    assert(!active.stepContentDraft.active.get());
    seq::SequencerTrackBankSnapshot snapshot;
    seq::captureTrackBankSnapshot(stagedBank, stagedActive, snapshot);

    std::array<core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>,
               seq::SequencerTrackBankState::TRACK_COUNT> graphs{};
    std::array<seq::SequencerCcLaneBankPtr,
               seq::SequencerTrackBankState::TRACK_COUNT> ccLaneBanks{};
    for (uint8_t i = 0; i < seq::SequencerTrackBankState::TRACK_COUNT; ++i) {
        graphs[i] = std::move(stagedBank.track(i).graph);
        ccLaneBanks[i] = std::move(stagedBank.track(i).ccLanes);
    }
    auto editorGraph = std::move(stagedActive.pattern.graph);
    auto editorCcLanes = std::move(stagedActive.pattern.ccLanes);

    seq::applyTrackBankSnapshot(bank, active, snapshot);
    for (uint8_t i = 0; i < seq::SequencerTrackBankState::TRACK_COUNT; ++i) {
        bank.track(i).graph = std::move(graphs[i]);
        bank.track(i).graphRevision.set(snapshot.tracks[i].graphRevision);
        seq::installSequencerCcLaneBank(bank.track(i), std::move(ccLaneBanks[i]));
        bank.track(i).ccLaneRevision.set(stagedBank.track(i).ccLaneRevision.get());
    }
    const uint8_t activeTrack = bank.activeTrackIndex();
    active.pattern.graph = std::move(editorGraph);
    active.pattern.graphRevision.set(snapshot.tracks[activeTrack].graphRevision);
    seq::installSequencerCcLaneBank(active.pattern, std::move(editorCcLanes));
    active.pattern.ccLaneRevision.set(stagedActive.pattern.ccLaneRevision.get());
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
    if (h.state.sharedTrackEnabledMask.get() != expectedMask ||
        h.state.sharedTrackActive.get() != expectedActive) {
        std::cerr << "shared Track projection drift mask="
                  << h.state.sharedTrackEnabledMask.get() << '/'
                  << expectedMask << " active="
                  << static_cast<unsigned>(h.state.sharedTrackActive.get())
                  << '/' << static_cast<unsigned>(expectedActive) << '\n';
    }
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
    assert(h.state.sequencerHistory.canRecordPattern(*prepared.change));

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
        assert(tx::publishAdmittedPattern(h.state, std::move(prepared.change)));
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

    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
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

    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
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

enum class PatternTraversalDirection : uint8_t {
    Undo,
    Redo,
};

constexpr std::size_t ACTIVE_PATTERN_TRAVERSAL_ALLOCATION_ATTEMPTS = 4U;
constexpr std::size_t INACTIVE_PATTERN_TRAVERSAL_ALLOCATION_ATTEMPTS = 2U;

struct PatternTraversalInteractionInvariant {
    uint8_t focusedStep = 0U;
    uint8_t page = 0U;
    seq::StepProperty activeStepProperty = seq::StepProperty::NOTE;
    seq::SequencerContentViewKind contentKind = seq::SequencerContentViewKind::ROOT;
    uint8_t contentParentStep = 0U;
    uint16_t contentOwnerNodeId = 0U;
    uint16_t contentSequenceId = 0U;
    uint16_t contentCycleSetId = 0U;
    uint8_t contentLength = 0U;
    uint8_t contentDepth = 0U;
    uint32_t contentRevision = 0U;
    uint8_t contentRootPageSnapshot = 0U;
    uint8_t contentRootFocusSnapshot = 0U;
    uint8_t contentStackDepth = 0U;
    bool historyFeedbackVisible = false;
    uint32_t historyFeedbackRevision = 0U;
    std::array<char, seq::SequencerHistoryFeedbackState::LINE_SIZE> historyLine1{};
    std::array<char, seq::SequencerHistoryFeedbackState::LINE_SIZE> historyLine2{};
    std::array<char, seq::SequencerHistoryFeedbackState::LINE_SIZE> historyLine3{};
    uint32_t historyFeedbackHideAtMs = 0U;
};

PatternTraversalInteractionInvariant capturePatternTraversalInteractionInvariant(
    const Harness& h
) {
    const auto& content = h.state.sequencer.contentView;
    const auto& feedback = h.state.sequencer.historyFeedback;
    return {
        .focusedStep = h.state.sequencer.focusedStep.get(),
        .page = h.state.sequencer.page.get(),
        .activeStepProperty = h.state.sequencer.activeStepProperty.get(),
        .contentKind = content.kind.get(),
        .contentParentStep = content.parentStep.get(),
        .contentOwnerNodeId = content.ownerNodeId.get(),
        .contentSequenceId = content.sequenceId.get(),
        .contentCycleSetId = content.cycleSetId.get(),
        .contentLength = content.length.get(),
        .contentDepth = content.depth.get(),
        .contentRevision = content.revision.get(),
        .contentRootPageSnapshot = content.rootPageSnapshot,
        .contentRootFocusSnapshot = content.rootFocusSnapshot,
        .contentStackDepth = content.stackDepth,
        .historyFeedbackVisible = feedback.visible.get(),
        .historyFeedbackRevision = feedback.revision.get(),
        .historyLine1 = feedback.line1,
        .historyLine2 = feedback.line2,
        .historyLine3 = feedback.line3,
        .historyFeedbackHideAtMs = feedback.hideAtMs,
    };
}

void assertPatternTraversalInteractionInvariant(
    const Harness& h,
    const PatternTraversalInteractionInvariant& expected
) {
    const auto actual = capturePatternTraversalInteractionInvariant(h);
    assert(actual.focusedStep == expected.focusedStep);
    assert(actual.page == expected.page);
    assert(actual.activeStepProperty == expected.activeStepProperty);
    assert(actual.contentKind == expected.contentKind);
    assert(actual.contentParentStep == expected.contentParentStep);
    assert(actual.contentOwnerNodeId == expected.contentOwnerNodeId);
    assert(actual.contentSequenceId == expected.contentSequenceId);
    assert(actual.contentCycleSetId == expected.contentCycleSetId);
    assert(actual.contentLength == expected.contentLength);
    assert(actual.contentDepth == expected.contentDepth);
    assert(actual.contentRevision == expected.contentRevision);
    assert(actual.contentRootPageSnapshot == expected.contentRootPageSnapshot);
    assert(actual.contentRootFocusSnapshot == expected.contentRootFocusSnapshot);
    assert(actual.contentStackDepth == expected.contentStackDepth);
    assert(actual.historyFeedbackVisible == expected.historyFeedbackVisible);
    assert(actual.historyFeedbackRevision == expected.historyFeedbackRevision);
    assert(actual.historyLine1 == expected.historyLine1);
    assert(actual.historyLine2 == expected.historyLine2);
    assert(actual.historyLine3 == expected.historyLine3);
    assert(actual.historyFeedbackHideAtMs == expected.historyFeedbackHideAtMs);
}

bool applyPatternTraversal(Harness& h, PatternTraversalDirection direction) {
    return direction == PatternTraversalDirection::Undo
        ? h.state.undoSequencerHistory()
        : h.state.redoSequencerHistory();
}

void prepareGraphCcPatternTraversalEntry(Harness& h, bool targetActive) {
    initializeActivePayload(h, PayloadKind::GraphAndCc);
    authorPayload(
        h.state.sequencerTracks.track(1U),
        PayloadKind::GraphAndCc,
        3U
    );
    settleSetup(h);

    PreparedPattern prepared;
    assert(preparePattern(
        h,
        seq::SequencerHistoryPatternStorage::FullGraph,
        prepared
    ));
    seq::SequencerState staged;
    stageActivePattern(h, staged);
    staged.pattern.setEnabled(1U, true);
    assert(seq::setNodeNoteOffset(
        staged.pattern,
        seq::rootStepNodeId(0U),
        11
    ));
    assert(staged.pattern.ccLanes != nullptr);
    assert(seq::setSequencerCcLaneEvent(
        *staged.pattern.ccLanes,
        0U,
        0U,
        42U
    ).changed());
    staged.pattern.bumpCcLaneRevision();
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

    assert(h.state.sequencerHistory.canRecordPattern(*prepared.change));
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
    assert(tx::publishAdmittedPattern(h.state, std::move(prepared.change)));
    settleSetup(h);

    if (!targetActive) {
        assert(h.state.setSharedTrackState(0x0003U, 1U));
        settleSetup(h);
    }

    assert(
        h.state.sequencerHistory.undoCount(
            seq::SequencerHistoryScope::PatternOnly
        ) == 1U
    );
    assert(h.state.sequencerHistory.redoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == 1U);
    assertSharedTrackProjection(h, targetActive ? 0x0001U : 0x0003U, targetActive ? 0U : 1U);
}

void positionPatternTraversalEntry(
    Harness& h,
    PatternTraversalDirection direction
) {
    if (direction == PatternTraversalDirection::Undo) return;
    assert(h.state.undoSequencerHistory());
    settleSetup(h);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(
        h.state.sequencerHistory.redoCount(
            seq::SequencerHistoryScope::PatternOnly
        ) == 1U
    );
    assert(h.state.projectHistory.undoCount() == 0U);
    assert(h.state.projectHistory.redoCount() == 1U);
}

void assertSuccessfulPatternTraversalState(
    const Harness& h,
    const tx::StateInvariant& before,
    PatternTraversalDirection direction
) {
    const auto after = tx::captureStateInvariant(h.state);
    assert(after.retainedBytes == before.retainedBytes);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);

    if (direction == PatternTraversalDirection::Undo) {
        assert(before.sequencerUndoCount == 1U);
        assert(before.sequencerRedoCount == 0U);
        assert(before.projectUndoCount == 1U);
        assert(before.projectRedoCount == 0U);
        assert(before.sequencerUndoIdentity != 0U);
        assert(after.sequencerUndoCount == 0U);
        assert(after.sequencerRedoCount == 1U);
        assert(after.projectUndoCount == 0U);
        assert(after.projectRedoCount == 1U);
        assert(after.sequencerUndoIdentity == 0U);
        assert(after.sequencerRedoIdentity == before.sequencerUndoIdentity);
        return;
    }

    assert(before.sequencerUndoCount == 0U);
    assert(before.sequencerRedoCount == 1U);
    assert(before.projectUndoCount == 0U);
    assert(before.projectRedoCount == 1U);
    assert(before.sequencerRedoIdentity != 0U);
    assert(after.sequencerUndoCount == 1U);
    assert(after.sequencerRedoCount == 0U);
    assert(after.projectUndoCount == 1U);
    assert(after.projectRedoCount == 0U);
    assert(after.sequencerUndoIdentity == before.sequencerRedoIdentity);
    assert(after.sequencerRedoIdentity == 0U);
}

void assertSuccessfulPatternTraversalOwners(
    const Harness& h,
    const BankOwnerInvariant& before,
    bool targetActive
) {
    const auto after = captureBankOwners(h);
    assert(after.editorGraph != nullptr);
    assert(after.editorCc != nullptr);
    assert(after.graphs[0U] != nullptr);
    assert(after.cc[0U] != nullptr);

    if (targetActive) {
        assert(after.editorGraph != before.editorGraph);
        assert(after.editorCc != before.editorCc);
        assert(after.graphs[0U] != before.graphs[0U]);
        assert(after.cc[0U] != before.cc[0U]);
        assert(after.editorGraph != after.graphs[0U]);
        assert(after.editorCc != after.cc[0U]);
    } else {
        assert(after.editorGraph == before.editorGraph);
        assert(after.editorCc == before.editorCc);
        assert(after.graphs[0U] != before.graphs[0U]);
        assert(after.cc[0U] != before.cc[0U]);
    }

    for (uint8_t track = 1U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        assert(after.graphs[track] == before.graphs[track]);
        assert(after.cc[track] == before.cc[track]);
        assert(after.graphRevisions[track] == before.graphRevisions[track]);
        assert(after.ccRevisions[track] == before.ccRevisions[track]);
    }
}

void verifyPatternTraversalAllocationFailure(
    bool targetActive,
    PatternTraversalDirection direction,
    std::size_t ordinal
) {
    Harness h;
    prepareGraphCcPatternTraversalEntry(h, targetActive);
    positionPatternTraversalEntry(h, direction);

    const auto before = tx::captureStateInvariant(h.state);
    const auto owners = captureBankOwners(h);
    const auto musical = captureFullBankMusicalProof(h);
    const auto interaction = capturePatternTraversalInteractionInvariant(h);
    assert(
        direction == PatternTraversalDirection::Undo
            ? before.sequencerUndoIdentity != 0U
            : before.sequencerRedoIdentity != 0U
    );

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        assert(!applyPatternTraversal(h, direction));
        tx::assertFailureConsumed(ordinal);
        tx::assertStateInvariant(h.state, before);
        assertBankOwners(h, owners);
        assertPatternTraversalInteractionInvariant(h, interaction);
    }

    assertFullBankMusicalProof(h, musical);
    assertSharedTrackProjection(h, targetActive ? 0x0001U : 0x0003U, targetActive ? 0U : 1U);
    assertNoDeferredPublication(h);
    tx::assertStateInvariant(h.state, before);
    assertBankOwners(h, owners);
    assertPatternTraversalInteractionInvariant(h, interaction);
}

void verifyPatternTraversalAllocationRatchet(
    bool targetActive,
    PatternTraversalDirection direction,
    std::size_t expectedAttempts
) {
    Harness h;
    prepareGraphCcPatternTraversalEntry(h, targetActive);

    FullBankMusicalProof expected;
    if (direction == PatternTraversalDirection::Undo) {
        assert(h.state.undoSequencerHistory());
        settleSetup(h);
        expected = captureFullBankMusicalProof(h);
        assert(h.state.redoSequencerHistory());
        settleSetup(h);
    } else {
        expected = captureFullBankMusicalProof(h);
        positionPatternTraversalEntry(h, direction);
    }

    const auto before = tx::captureStateInvariant(h.state);
    const auto owners = captureBankOwners(h);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        assert(applyPatternTraversal(h, direction));
        tx::assertMaxPlusOneStillArmed(expectedAttempts);
        assertSuccessfulPatternTraversalState(h, before, direction);
        assertSuccessfulPatternTraversalOwners(h, owners, targetActive);
        assertNoDeferredPublication(h);
        tx::assertMaxPlusOneStillArmed(expectedAttempts);
    }

    assertFullBankMusicalProof(h, expected);
    assertSharedTrackProjection(h, targetActive ? 0x0001U : 0x0003U, targetActive ? 0U : 1U);
}

void test_pattern_traversal_allocation_failures_are_atomic() {
    constexpr std::array<PatternTraversalDirection, 2U> directions{{
        PatternTraversalDirection::Undo,
        PatternTraversalDirection::Redo,
    }};

    for (const auto direction : directions) {
        for (std::size_t ordinal = 1U;
             ordinal <= ACTIVE_PATTERN_TRAVERSAL_ALLOCATION_ATTEMPTS;
             ++ordinal) {
            verifyPatternTraversalAllocationFailure(true, direction, ordinal);
        }
        verifyPatternTraversalAllocationRatchet(
            true,
            direction,
            ACTIVE_PATTERN_TRAVERSAL_ALLOCATION_ATTEMPTS
        );

        for (std::size_t ordinal = 1U;
             ordinal <= INACTIVE_PATTERN_TRAVERSAL_ALLOCATION_ATTEMPTS;
             ++ordinal) {
            verifyPatternTraversalAllocationFailure(false, direction, ordinal);
        }
        verifyPatternTraversalAllocationRatchet(
            false,
            direction,
            INACTIVE_PATTERN_TRAVERSAL_ALLOCATION_ATTEMPTS
        );
    }

    std::cout
        << "[PASS] active/inactive Pattern Undo/Redo allocation failures are atomic\n";
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
    assert(!h.state.sequencerHistory.canRecordPattern(*noOp.change));
    assert(!tx::publishAdmittedPattern(h.state, std::move(noOp.change)));
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
    assert(h.state.sequencerHistory.canRecordPattern(*prepared.change));

    publishStagedFlatPattern(h, staged);
    seq::publishPreparedActiveTrackSynchronization(
        h.state.sequencerTracks,
        h.state.sequencer,
        std::move(prepared.synchronization)
    );
    assert(tx::publishAdmittedPattern(h.state, std::move(prepared.change)));

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
    assert(h.state.sequencerHistory.canRecordPattern(*prepared.change));
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
        assert(tx::publishAdmittedPattern(h.state, std::move(prepared.change)));
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
            if (track == 0U) continue;  // active bank slot is noncanonical scratch
            const bool populated =
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
    const uint8_t beforeActive = prepared.change->before.flat.activeTrack;
    const uint8_t afterActive = prepared.change->after.flat.activeTrack;
    assert(!prepared.change->before.bankGraphs[beforeActive]);
    assert(!prepared.change->before.bankCcLanes[beforeActive]);
    assert(!prepared.change->after.bankGraphs[afterActive]);
    assert(!prepared.change->after.bankCcLanes[afterActive]);
    assert(fullBankGraphOwnerCount(prepared.change->before) == ownersPerPayload);
    assert(fullBankGraphOwnerCount(prepared.change->after) == ownersPerPayload);
    assert(fullBankCcOwnerCount(prepared.change->before) == ownersPerPayload);
    assert(fullBankCcOwnerCount(prepared.change->after) == ownersPerPayload);
}

seq::SequencerPatternState& canonicalTrackPattern(Harness& h, uint8_t track) {
    const uint8_t target =
        seq::SequencerTrackBankState::clampTrackIndex(track);
    return target == h.state.sequencerTracks.activeTrackIndex()
        ? h.state.sequencer.pattern
        : h.state.sequencerTracks.track(target);
}

const seq::SequencerPatternState& canonicalTrackPattern(
    const Harness& h,
    uint8_t track
) {
    const uint8_t target =
        seq::SequencerTrackBankState::clampTrackIndex(track);
    return target == h.state.sequencerTracks.activeTrackIndex()
        ? h.state.sequencer.pattern
        : h.state.sequencerTracks.track(target);
}

void setPatternRevisionVector(
    seq::SequencerPatternState& pattern,
    uint32_t base
) {
    pattern.stepDataRevision.set(base + 1U);
    pattern.patternVariationRevision.set(base + 2U);
    pattern.patternScaleRevision.set(base + 3U);
    pattern.patternTimingRevision.set(base + 4U);
    pattern.graphRevision.set(base + 5U);
    pattern.ccLaneRevision.set(base + 6U);
}

void assertCapturedPatternRevisionVector(
    const seq::SequencerPatternState& pattern,
    const seq::SequencerPatternSnapshot& expected
) {
    assert(pattern.stepDataRevision.get() == expected.stepDataRevision);
    assert(
        pattern.patternVariationRevision.get() ==
        expected.patternVariationRevision
    );
    assert(pattern.patternScaleRevision.get() == expected.patternScaleRevision);
    assert(pattern.patternTimingRevision.get() == expected.patternTimingRevision);
    assert(pattern.graphRevision.get() == expected.graphRevision);
}

void commitFullBankEnabledMaskChange(Harness& h, uint16_t enabledMask) {
    PreparedFullBank prepared;
    assert(prepareFullBank(h, prepared));
    h.state.sequencerTracks.syncSharedTrackState(
        enabledMask,
        h.state.sequencerTracks.activeTrackIndex()
    );
    assert(seq::capturePreparedHistoryFullBankAfterUsingReservedStorage(
        h.state.sequencerTracks,
        h.state.sequencer,
        *prepared.change
    ));
    assert(tx::canPublishAdmittedFullBank(h.state, *prepared.change));
    assert(tx::publishAdmittedFullBank(h.state, std::move(prepared.change)));
    assert(
        h.state.sequencerHistory.undoCount(
            seq::SequencerHistoryScope::FullBank
        ) == 1U
    );
    assert(h.state.sequencerHistory.redoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == 1U);
}

void beginModifiedChordDraft(Harness& h) {
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
}

void assertHistoryBlockedDraft(
    const Harness& h,
    DraftInvariant before
) {
    before.failure = seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED;
    before.blockedTransition =
        seq::SequencerStepContentDraftBlockedTransition::HISTORY;
    ++before.revision;
    assertDraftInvariant(h, before);
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
        {PayloadKind::Graph, 3U},
        {PayloadKind::Cc, 3U},
        {PayloadKind::GraphAndCc, 5U},
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
            1U
        );
    }

    constexpr std::size_t maximumAttempts = 65U;
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
        16U
    );

    std::cout << "[PASS] FullBank preparation allocation matrix\n";
}

void test_full_bank_capture_rejects_active_track_drift() {
    Harness h;
    initializeCapturedTracks(h, PayloadKind::GraphAndCc, 0x0003U);
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    settleSetup(h);

    PreparedFullBank prepared;
    assert(prepareFullBank(h, prepared));
    assert(prepared.change->before.flat.activeTrack == 0U);
    assert(prepared.change->after.flat.activeTrack == 0U);
    auto* const reservedEditorGraph = prepared.change->after.editorGraph.get();
    auto* const reservedEditorCc = prepared.change->after.editorCcLanes.get();
    assert(reservedEditorGraph != nullptr);
    assert(reservedEditorCc != nullptr);
    const bool reservedGraphEnabled = reservedEditorGraph->enabled;
    const uint8_t reservedCcCount = seq::sequencerCcLaneCount(
        *reservedEditorCc
    );

    assert(seq::switchActiveTrack(
        h.state.sequencerTracks,
        h.state.sequencer,
        1U
    ));
    const auto liveAfterSwitch = tx::captureStateInvariant(h.state);
    const auto ownersAfterSwitch = captureBankOwners(h);
    const auto musicalAfterSwitch = captureFullBankMusicalProof(h);

    assert(!seq::capturePreparedHistoryFullBankAfterUsingReservedStorage(
        h.state.sequencerTracks,
        h.state.sequencer,
        *prepared.change
    ));
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    assert(prepared.change->after.flat.activeTrack == 0U);
    assert(prepared.change->after.editorGraph.get() == reservedEditorGraph);
    assert(prepared.change->after.editorCcLanes.get() == reservedEditorCc);
    assert(reservedEditorGraph->enabled == reservedGraphEnabled);
    assert(seq::sequencerCcLaneCount(*reservedEditorCc) == reservedCcCount);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.sequencerHistory.redoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == 0U);
    tx::assertStateInvariant(h.state, liveAfterSwitch);
    assertBankOwners(h, ownersAfterSwitch);
    assertFullBankMusicalProof(h, musicalAfterSwitch);

    std::cout << "[PASS] FullBank capture rejects active Track drift before copy\n";
}

void test_full_bank_traversal_rejects_active_step_draft_before_allocation() {
    {
        Harness h;
        initializeActivePayload(h, PayloadKind::GraphAndCc);
        commitFullBankEnabledMaskChange(h, 0x0003U);
        beginModifiedChordDraft(h);

        const auto before = tx::captureStateInvariant(h.state);
        const auto owners = captureBankOwners(h);
        const auto musical = captureFullBankMusicalProof(h);
        const auto draft = captureDraftInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            allocation_trace::Scope allocationTrace;
            assert(!h.state.undoSequencerHistory());
            assert(!allocation_trace::overflow);
            assert(allocation_trace::count == 0U);
            tx::assertMaxPlusOneStillArmed(0U);
        }
        tx::assertStateInvariant(h.state, before);
        assertBankOwners(h, owners);
        assertFullBankMusicalProof(h, musical);
        assertHistoryBlockedDraft(h, draft);
    }

    {
        Harness h;
        initializeActivePayload(h, PayloadKind::GraphAndCc);
        commitFullBankEnabledMaskChange(h, 0x0003U);
        assert(h.state.undoSequencerHistory());
        assert(
            h.state.sequencerHistory.undoCount(
                seq::SequencerHistoryScope::FullBank
            ) == 0U
        );
        assert(
            h.state.sequencerHistory.redoCount(
                seq::SequencerHistoryScope::FullBank
            ) == 1U
        );
        assertNoDeferredPublication(h);
        beginModifiedChordDraft(h);

        const auto before = tx::captureStateInvariant(h.state);
        const auto owners = captureBankOwners(h);
        const auto musical = captureFullBankMusicalProof(h);
        const auto draft = captureDraftInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            allocation_trace::Scope allocationTrace;
            assert(!h.state.redoSequencerHistory());
            assert(!allocation_trace::overflow);
            assert(allocation_trace::count == 0U);
            tx::assertMaxPlusOneStillArmed(0U);
        }
        tx::assertStateInvariant(h.state, before);
        assertBankOwners(h, owners);
        assertFullBankMusicalProof(h, musical);
        assertHistoryBlockedDraft(h, draft);
    }

    std::cout
        << "[PASS] FullBank Undo/Redo reject an active Step Draft before allocation\n";
}

void test_full_bank_active_scratch_is_excluded_and_cleared_on_apply() {
    Harness h;
    initializeActivePayload(h, PayloadKind::GraphAndCc);
    const uint8_t activeTrack = h.state.sequencerTracks.activeTrackIndex();
    auto& scratch = h.state.sequencerTracks.track(activeTrack);
    scratch.reset();
    authorPayload(scratch, PayloadKind::GraphAndCc, 7U);
    assert(seq::graphView(scratch) != nullptr);
    assert(seq::sequencerCcLaneView(scratch) != nullptr);

    seq::SequencerHistoryTrackBankSnapshot captured;
    assert(seq::captureHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        captured
    ));
    assert(captured.flat.activeTrack == activeTrack);
    assert(captured.editorGraph != nullptr);
    assert(captured.editorCcLanes != nullptr);
    assert(!captured.bankGraphs[activeTrack]);
    assert(!captured.bankCcLanes[activeTrack]);
    const auto root = seq::rootStepNodeId(0U);
    assert(captured.editorGraph->stepNode(root) != nullptr);
    assert(seq::graphView(scratch)->stepNode(root) != nullptr);
    assert(
        captured.editorGraph->stepNode(root)->noteOffset !=
        seq::graphView(scratch)->stepNode(root)->noteOffset
    );
    assert(seq::sameOptionalSequencerCcLaneBank(
        captured.editorCcLanes.get(),
        seq::sequencerCcLaneView(h.state.sequencer.pattern)
    ));
    assert(!seq::sameOptionalSequencerCcLaneBank(
        captured.editorCcLanes.get(),
        seq::sequencerCcLaneView(scratch)
    ));

    assert(seq::setNodeNoteOffset(
        h.state.sequencer.pattern,
        root,
        -4
    ));
    assert(seq::setSequencerCcLaneEvent(
        *h.state.sequencer.pattern.ccLanes,
        0U,
        0U,
        17U
    ).changed());
    h.state.sequencer.pattern.bumpCcLaneRevision();
    assert(h.state.sequencer.pattern.setStepDataAt(
        0U,
        72U,
        101U,
        seq::SequencerPatternState::DEFAULT_GATE_PERCENT
    ));

    assert(seq::applyHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        captured
    ));
    assert(seq::graphView(h.state.sequencerTracks.track(activeTrack)) == nullptr);
    assert(
        seq::sequencerCcLaneView(h.state.sequencerTracks.track(activeTrack)) ==
        nullptr
    );
    assert(seq::graphView(h.state.sequencer.pattern) != nullptr);
    assert(
        seq::graphView(h.state.sequencer.pattern)->stepNode(root)->noteOffset ==
        captured.editorGraph->stepNode(root)->noteOffset
    );
    assert(seq::sameOptionalSequencerCcLaneBank(
        seq::sequencerCcLaneView(h.state.sequencer.pattern),
        captured.editorCcLanes.get()
    ));

    seq::SequencerHistoryTrackBankSnapshot restored;
    assert(seq::captureHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        restored
    ));
    assert(seq::sameMusicalHistorySnapshot(restored, captured));

    std::cout
        << "[PASS] FullBank capture excludes active scratch and apply clears it\n";
}

void test_full_bank_apply_restores_revision_contract() {
    Harness h;
    initializeCapturedTracks(h, PayloadKind::GraphAndCc, 0xFFFFU);
    const uint8_t activeTrack = h.state.sequencerTracks.activeTrackIndex();
    constexpr uint32_t capturedProjectRevision = 700U;
    h.state.sequencerTracks.projectScaleRevisionSignal().set(
        capturedProjectRevision
    );
    std::array<uint32_t, seq::SequencerTrackBankState::TRACK_COUNT>
        capturedCcRevisions{};
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        auto& pattern = canonicalTrackPattern(h, track);
        setPatternRevisionVector(
            pattern,
            static_cast<uint32_t>(1000U + 10U * track)
        );
        capturedCcRevisions[track] = pattern.ccLaneRevision.get();
    }

    seq::SequencerHistoryTrackBankSnapshot captured;
    assert(seq::captureHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        captured
    ));
    assert(captured.flat.projectScaleRevision == capturedProjectRevision);
    assert(!captured.bankGraphs[activeTrack]);
    assert(!captured.bankCcLanes[activeTrack]);

    std::array<uint32_t, seq::SequencerTrackBankState::TRACK_COUNT>
        ccRevisionsBeforeApply{};
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        auto& pattern = canonicalTrackPattern(h, track);
        setPatternRevisionVector(
            pattern,
            static_cast<uint32_t>(5000U + 10U * track)
        );
        assert(pattern.ccLanes != nullptr);
        assert(seq::setSequencerCcLaneEvent(
            *pattern.ccLanes,
            0U,
            0U,
            static_cast<uint8_t>(20U + track)
        ).changed());
        pattern.bumpCcLaneRevision();
        ccRevisionsBeforeApply[track] = pattern.ccLaneRevision.get();
        assert(ccRevisionsBeforeApply[track] >= capturedCcRevisions[track]);
    }
    h.state.sequencerTracks.projectScaleRevisionSignal().set(9000U);

    assert(seq::applyHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        captured
    ));
    assert(
        h.state.sequencerTracks.projectScaleRevisionSignal().get() ==
        capturedProjectRevision
    );
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const auto& pattern = canonicalTrackPattern(h, track);
        assertCapturedPatternRevisionVector(pattern, captured.flat.tracks[track]);
        const auto* expectedCc = track == activeTrack
            ? captured.editorCcLanes.get()
            : captured.bankCcLanes[track].get();
        assert(seq::sameOptionalSequencerCcLaneBank(
            seq::sequencerCcLaneView(pattern),
            expectedCc
        ));
        assert(
            pattern.ccLaneRevision.get() >=
            ccRevisionsBeforeApply[track]
        );
    }
    assert(seq::graphView(h.state.sequencerTracks.track(activeTrack)) == nullptr);
    assert(
        seq::sequencerCcLaneView(h.state.sequencerTracks.track(activeTrack)) ==
        nullptr
    );

    seq::SequencerHistoryTrackBankSnapshot restored;
    assert(seq::captureHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        restored
    ));
    assert(seq::sameMusicalHistorySnapshot(restored, captured));

    std::cout
        << "[PASS] FullBank apply restores exact represented revisions and monotone CC\n";
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
        assert(!tx::canPublishAdmittedFullBank(h.state, *noOp.change));
        assert(!tx::publishAdmittedFullBank(h.state, std::move(noOp.change)));
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
        assert(tx::canPublishAdmittedFullBank(h.state, *maximum.change));
        assert(tx::publishAdmittedFullBank(h.state, std::move(maximum.change)));
        assert(
            h.state.sequencerHistory.retainedBytes() <=
            seq::SequencerHistoryService::RETAINED_BYTE_BUDGET
        );
    }

    {
        Harness h;
        initializeActivePayload(h, PayloadKind::None);
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
            assert(tx::canPublishAdmittedFullBank(h.state, *prepared.change));
            assert(tx::publishAdmittedFullBank(h.state, std::move(prepared.change)));
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
    assert(tx::canPublishAdmittedFullBank(h.state, *prepared.change));
    const auto before = tx::captureStateInvariant(h.state);
    const std::size_t expectedRetained = expectedFullBankRetainedBytes(
        *prepared.change
    );

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        installTrackBankStateForTest(
            h.state.sequencerTracks,
            h.state.sequencer,
            stagedBank,
            stagedActive
        );
        assert(tx::publishAdmittedFullBank(h.state, std::move(prepared.change)));
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
    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
    const auto beforeUndo = tx::captureStateInvariant(h.state);
    assert(h.state.undoSequencerHistory());
    assertSharedTrackProjection(h, 0x0001U, 0U);
    assert(
        h.state.project.metadata.modifiedCounter ==
        beforeUndo.modifiedCounter + 1U
    );
    assert(h.state.hasPendingProjectSessionSave());
    assertNoDeferredPublication(h);
    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
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
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(!tx::canPublishAdmittedFullBank(h.state, *prepared.change));
            assert(!tx::publishAdmittedFullBank(h.state, std::move(prepared.change)));
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
            assert(!history.canCommitAdmittedStructure(*prepared.change));
            prepared.change.reset();
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
        assert(!history.canCommitAdmittedStructure(*noOp.change));
        noOp.change.reset();
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
    assert(history.canCommitAdmittedStructure(*prepared.change));
    const auto before = tx::captureStateInvariant(h.state);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        installTrackBankStateForTest(
            h.state.sequencerTracks,
            h.state.sequencer,
            stagedBank,
            stagedActive
        );
        assert(h.state.publishPreparedSequencerTrackState(
            prepared.change->after.enabledMask,
            prepared.change->after.activeTrack
        ));
        history.commitAdmittedStructure(std::move(prepared.change));
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
    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
    const auto beforeUndo = tx::captureStateInvariant(h.state);
    assert(h.state.undoSequencerHistory());
    assertSharedTrackProjection(h, 0x0001U, 0U);
    assert(
        h.state.project.metadata.modifiedCounter ==
        beforeUndo.modifiedCounter + 1U
    );
    assert(h.state.hasPendingProjectSessionSave());
    assertNoDeferredPublication(h);
    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
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

uint64_t replayFingerprint(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t coreReplayFailureFingerprint(const Harness& h) {
    return replayFingerprint(&h.state, sizeof(h.state));
}

uint64_t macroTrackFingerprint(const Harness& h) {
    return replayFingerprint(
        h.state.pages.tracks.data(),
        sizeof(h.state.pages.tracks));
}

uint64_t macroControlFingerprint(const Harness& h) {
    return replayFingerprint(
        &h.state.pages.control.authored,
        sizeof(h.state.pages.control.authored));
}

seq::SequencerTrackActivationExpectedState captureActivationExpected(
    const Harness& h
) {
    seq::SequencerTrackActivationHistoryTransitionPlan plan;
    assert(h.state.sequencerTrackActivations.planHistoryTransition(
        seq::SequencerTrackActivationHistoryRef{
            .trackMask = 0xFFFFU,
            .operationId = 0xA55A1234U,
            .origin = seq::SequencerTrackActivationOrigin::TRACK_PASTE,
        },
        seq::SequencerTrackActivationTarget::BEFORE,
        0xFFFFU,
        false,
        plan));
    return plan.expected;
}

void assertActivationExpected(
    const Harness& h,
    const seq::SequencerTrackActivationExpectedState& expected
) {
    const auto actual = captureActivationExpected(h);
    assert(actual.nextGeneration == expected.nextGeneration);
    assert(actual.nextOperationId == expected.nextOperationId);
    assert(actual.telemetryRevision == expected.telemetryRevision);
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        assert(actual.entries[track].phase == expected.entries[track].phase);
        assert(
            actual.entries[track].requiresLocalLoopBoundary ==
            expected.entries[track].requiresLocalLoopBoundary);
        assert(actual.entries[track].target == expected.entries[track].target);
        assert(actual.entries[track].origin == expected.entries[track].origin);
        assert(
            actual.entries[track].generation ==
            expected.entries[track].generation);
        assert(
            actual.entries[track].operationId ==
            expected.entries[track].operationId);
    }
}

struct CoupledReplayExpectations {
    FullBankMusicalProof before;
    FullBankMusicalProof after;
    uint64_t macroTracksBefore = 0U;
    uint64_t macroTracksAfter = 0U;
    uint64_t macroControlBefore = 0U;
    uint64_t macroControlAfter = 0U;
};

void prepareMaximumCoupledStructureReplay(
    Harness& h,
    CoupledReplayExpectations& expected,
    bool distinctControl = true
) {
    initializeCapturedTracks(h, PayloadKind::GraphAndCc, 0xFFFFU);
    h.state.sequencerTracks.syncSharedTrackState(0x0001U, 0U);
    (void)h.state.refreshSharedTrackStateFromSequencer();
    settleSetup(h);

    expected.before = captureFullBankMusicalProof(h);
    expected.macroTracksBefore = macroTrackFingerprint(h);
    expected.macroControlBefore = macroControlFingerprint(h);

    PreparedStructure prepared;
    assert(prepareStructure(h, 0xFFFFU, prepared));
    assert(seq::captureMacroTrackStructureHistoryBefore(
        h.state.pages,
        seq::sequencerHistoryTrackBit(0U),
        *prepared.change,
        0U));
    prepared.change->activation = {
        .trackMask = seq::sequencerHistoryTrackBit(0U),
        .operationId = 0x10203040U,
        .origin = seq::SequencerTrackActivationOrigin::TRACK_PASTE,
    };
    prepared.change->activationBeforeAudibleMask = 0x0001U;
    prepared.change->activationAfterAudibleMask = 0x0003U;

    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    h.state.sequencer.pattern.setEnabled(2U, true);
    auto& macroTrack = h.state.pages.tracks[0U];
    macroTrack.enabledPageMask = 0x0003U;
    macroTrack.activePage = 1U;
    macroTrack.pages[1U].cc[0U] = 99U;
    if (distinctControl) {
        ++h.state.pages.control.authored.curves.nextCurveId;
        h.state.pages.control.markAuthoredMutation();
    }
    assert(h.state.refreshSharedTrackStateFromSequencer());
    h.state.pages.syncActiveTrackCache();
    h.state.pages.updateActiveConfigs();

    assert(seq::capturePreparedHistoryStructureAfterUsingReservedStorage(
        h.state.sequencerTracks,
        h.state.sequencer,
        *prepared.change));
    assert(seq::captureMacroTrackStructureHistoryAfter(
        h.state.pages, *prepared.change));

    expected.after = captureFullBankMusicalProof(h);
    expected.macroTracksAfter = macroTrackFingerprint(h);
    expected.macroControlAfter = macroControlFingerprint(h);

    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state);
    assert(history.canCommitAdmittedStructure(*prepared.change));
    assert(h.state.publishPreparedSequencerTrackState(
        prepared.change->after.enabledMask,
        prepared.change->after.activeTrack
    ));
    history.commitAdmittedStructure(std::move(prepared.change));
    settleSetup(h);
    assert(
        h.state.sequencerHistory.undoCount(
            seq::SequencerHistoryScope::Structure) == 1U);
    assert(h.state.sequencerHistory.redoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == 1U);
}

ExpectedAllocationRequests expectedMaximumCoupledReplayRequests() {
    ExpectedAllocationRequests expected;
    for (uint8_t owner = 0U;
         owner <= seq::SequencerTrackBankState::TRACK_COUNT;
         ++owner) {
        appendPayloadRequests(expected, PayloadKind::GraphAndCc);
    }
    return expected;
}

void assertAllocationRequestPrefix(
    const ExpectedAllocationRequests& expected,
    std::size_t count
) {
    assert(!allocation_trace::overflow);
    assert(allocation_trace::count == count);
    assert(count <= expected.count);
    for (std::size_t index = 0U; index < count; ++index) {
        assert(allocation_trace::requests[index] == expected.bytes[index]);
    }
}

bool traverseCoupledReplay(
    Harness& h,
    seq::SequencerHistoryDirection direction
) {
    return direction == seq::SequencerHistoryDirection::Undo
        ? h.state.undoSequencerHistory()
        : h.state.redoSequencerHistory();
}

void verifyCoupledReplayAllocationFailures(
    Harness& h,
    seq::SequencerHistoryDirection direction,
    const ExpectedAllocationRequests& expectedRequests
) {
    const auto stateInvariant = tx::captureStateInvariant(h.state);
    const auto bankOwners = captureBankOwners(h);
    const auto activation = captureActivationExpected(h);
    const uint64_t exactCore = coreReplayFailureFingerprint(h);

    for (std::size_t ordinal = 1U; ordinal <= expectedRequests.count; ++ordinal) {
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            allocation_trace::Scope trace;
            assert(!traverseCoupledReplay(h, direction));
            tx::assertFailureConsumed(ordinal);
            assertAllocationRequestPrefix(expectedRequests, ordinal - 1U);
        }
        assert(coreReplayFailureFingerprint(h) == exactCore);
        tx::assertStateInvariant(h.state, stateInvariant);
        assertBankOwners(h, bankOwners);
        assertActivationExpected(h, activation);
    }
}

void verifyCoupledReplaySuccess(
    Harness& h,
    seq::SequencerHistoryDirection direction,
    const ExpectedAllocationRequests& expectedRequests,
    const CoupledReplayExpectations& expected,
    uint32_t expectedControlRevision
) {
    const auto before = tx::captureStateInvariant(h.state);
    const uint32_t automationRevision =
        h.state.macroUi.automationEditRevision.get();
    const uint32_t runtimeRevision =
        h.state.macroUi.runtimeProjectionRevision.get();
    const uint32_t configRevision = h.state.configRevision.get();
    const uint32_t feedbackRevision =
        h.state.sequencer.historyFeedback.revision.get();

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedRequests.count + 1U);
        allocation_trace::Scope trace;
        assert(traverseCoupledReplay(h, direction));
        assertAllocationRequests(expectedRequests);
        tx::assertMaxPlusOneStillArmed(expectedRequests.count);
    }

    const auto after = tx::captureStateInvariant(h.state);
    assert(after.retainedBytes == before.retainedBytes);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);
    assert(h.state.pages.control.authoredRevision == expectedControlRevision);
    assert(
        h.state.macroUi.automationEditRevision.get() ==
        automationRevision + 1U);
    assert(
        h.state.macroUi.runtimeProjectionRevision.get() ==
        core::state::macro::nextMacroRuntimeProjectionRevision(
            runtimeRevision,
            core::state::macro::kMacroRuntimeProjectionDirtyConfig));
    assert(
        h.state.configRevision.get() ==
        core::state::macro::nextMacroConfigRevision(
            configRevision,
            core::state::macro::kMacroConfigDirtyAll));
    assert(
        h.state.sequencer.historyFeedback.revision.get() ==
        feedbackRevision + 1U);
    const auto activationTelemetry =
        h.state.sequencerTrackActivations.telemetry(0U);
    assert(
        activationTelemetry.status !=
        seq::SequencerTrackActivationStatus::IDLE);
    assert(activationTelemetry.generation != 0U);
    assert(
        activationTelemetry.origin ==
        seq::SequencerTrackActivationOrigin::TRACK_PASTE);

    if (direction == seq::SequencerHistoryDirection::Undo) {
        assert(after.sequencerUndoCount == 0U);
        assert(after.sequencerRedoCount == 1U);
        assert(after.projectUndoCount == 0U);
        assert(after.projectRedoCount == 1U);
        assertFullBankMusicalProof(h, expected.before);
        assert(macroTrackFingerprint(h) == expected.macroTracksBefore);
        assert(macroControlFingerprint(h) == expected.macroControlBefore);
        assertSharedTrackProjection(h, 0x0001U, 0U);
    } else {
        assert(after.sequencerUndoCount == 1U);
        assert(after.sequencerRedoCount == 0U);
        assert(after.projectUndoCount == 1U);
        assert(after.projectRedoCount == 0U);
        assertFullBankMusicalProof(h, expected.after);
        assert(macroTrackFingerprint(h) == expected.macroTracksAfter);
        assert(macroControlFingerprint(h) == expected.macroControlAfter);
        assertSharedTrackProjection(h, 0x0003U, 0U);
    }
    assertNoDeferredPublication(h);
}

void test_coupled_structure_replay_allocation_matrix() {
    Harness h;
    CoupledReplayExpectations expected;
    prepareMaximumCoupledStructureReplay(h, expected);
    const auto expectedRequests = expectedMaximumCoupledReplayRequests();
    assert(expectedRequests.count == 34U);

    verifyCoupledReplayAllocationFailures(
        h, seq::SequencerHistoryDirection::Undo, expectedRequests);
    const uint32_t afterControlRevision =
        h.state.pages.control.authoredRevision;
    verifyCoupledReplaySuccess(
        h,
        seq::SequencerHistoryDirection::Undo,
        expectedRequests,
        expected,
        afterControlRevision + 1U);

    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
    verifyCoupledReplayAllocationFailures(
        h, seq::SequencerHistoryDirection::Redo, expectedRequests);
    verifyCoupledReplaySuccess(
        h,
        seq::SequencerHistoryDirection::Redo,
        expectedRequests,
        expected,
        afterControlRevision + 2U);

    std::cout
        << "[PASS] coupled Structure replay is Macro-first, fail-atomic and no-fail after arm\n";
}

void test_coupled_structure_replay_equal_control_preserves_revision() {
    Harness h;
    CoupledReplayExpectations expected;
    prepareMaximumCoupledStructureReplay(h, expected, false);
    const auto expectedRequests = expectedMaximumCoupledReplayRequests();
    const uint32_t controlRevision =
        h.state.pages.control.authoredRevision;

    verifyCoupledReplaySuccess(
        h,
        seq::SequencerHistoryDirection::Undo,
        expectedRequests,
        expected,
        controlRevision);
    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
    verifyCoupledReplaySuccess(
        h,
        seq::SequencerHistoryDirection::Redo,
        expectedRequests,
        expected,
        controlRevision);

    std::cout
        << "[PASS] coupled Structure replay preserves equal Macro control revision\n";
}

void test_coupled_structure_replay_playing_waits_for_loop() {
    Harness h;
    CoupledReplayExpectations expected;
    prepareMaximumCoupledStructureReplay(h, expected);
    const auto expectedRequests = expectedMaximumCoupledReplayRequests();
    const uint32_t controlRevision =
        h.state.pages.control.authoredRevision;
    h.state.statusBar.playing.set(true);

    verifyCoupledReplaySuccess(
        h,
        seq::SequencerHistoryDirection::Undo,
        expectedRequests,
        expected,
        controlRevision + 1U);
    assert(
        h.state.sequencerTrackActivations.pendingTrackMask() ==
        seq::sequencerHistoryTrackBit(0U));

    std::cout
        << "[PASS] coupled Structure replay while playing waits for local loop\n";
}

void test_coupled_structure_replay_rejects_macro_drift_before_allocation() {
    Harness h;
    CoupledReplayExpectations expected;
    prepareMaximumCoupledStructureReplay(h, expected);
    h.state.pages.tracks[0U].activePage = 2U;
    const uint64_t exactCore = coreReplayFailureFingerprint(h);
    const auto activation = captureActivationExpected(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        allocation_trace::Scope trace;
        assert(!h.state.undoSequencerHistory());
        assertAllocationRequestPrefix(ExpectedAllocationRequests{}, 0U);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    assert(coreReplayFailureFingerprint(h) == exactCore);
    assertActivationExpected(h, activation);

    std::cout << "[PASS] coupled Structure replay rejects Macro drift before allocation\n";
}

void test_coupled_structure_replay_atomic_arm_is_ultimate_gate() {
    Harness h;
    CoupledReplayExpectations expected;
    prepareMaximumCoupledStructureReplay(h, expected);

    {
        seq::SequencerPreparedStructureHistoryReplay prepared;
        assert(
            h.state.sequencerHistory.prepareStructureHistoryReplay(
                seq::SequencerHistoryDirection::Undo,
                h.state.sequencerTracks,
                h.state.sequencer,
                h.state.pages,
                prepared) ==
            seq::SequencerStructureHistoryReplayPrepareOutcome::Prepared);
        seq::SequencerTrackActivationHistoryTransitionPlan historyPlan;
        assert(h.state.sequencerTrackActivations.planHistoryTransition(
            prepared.activation.reference,
            seq::SequencerTrackActivationTarget::BEFORE,
            prepared.activation.targetAudibleMask,
            false,
            historyPlan));

        seq::SequencerTrackActivationPlan interferingPlan;
        assert(h.state.sequencerTrackActivations.planActivation(
            0x0002U,
            0x0002U,
            false,
            interferingPlan,
            seq::SequencerTrackActivationOrigin::TRACK_PASTE));
        seq::SequencerTrackActivationBatch interferingBatch;
        assert(h.state.sequencerTrackActivations.tryArmPlannedActivation(
            interferingPlan, interferingBatch));
        h.state.sequencerTrackActivations.publishPrepared(interferingBatch);

        const uint64_t exactCore = coreReplayFailureFingerprint(h);
        seq::SequencerTrackActivationHistoryTransition rejected;
        assert(!h.state.sequencerTrackActivations.tryArmPlannedHistoryTransition(
            historyPlan, rejected));
        assert(coreReplayFailureFingerprint(h) == exactCore);
        assert(prepared.valid());
    }
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.redoCount() == 0U);

    std::cout << "[PASS] stale atomic arm discards prepared replay without domain mutation\n";
}

void test_coupled_structure_replay_draft_precedes_allocation() {
    Harness h;
    CoupledReplayExpectations expected;
    prepareMaximumCoupledStructureReplay(h, expected);
    beginModifiedChordDraft(h);
    const auto draft = captureDraftInvariant(h);
    const auto stateInvariant = tx::captureStateInvariant(h.state);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        allocation_trace::Scope trace;
        assert(!h.state.undoSequencerHistory());
        assertAllocationRequestPrefix(ExpectedAllocationRequests{}, 0U);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    tx::assertStateInvariant(h.state, stateInvariant);
    assertHistoryBlockedDraft(h, draft);

    std::cout << "[PASS] coupled Structure replay rejects Draft before allocation\n";
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

void test_cc_capture_validation_preserves_detached_owners() {
    seq::SequencerState source;
    source.reset();
    authorPayload(source.pattern, PayloadKind::GraphAndCc);

    seq::SequencerHistoryPatternPayloadStorage storage;
    assert(seq::reserveHistoryPatternPayloadStorage(source.pattern, storage));
    assert(seq::captureHistoryPatternPayloadUsingReservedStorage(
        source.pattern,
        storage
    ));
    auto* const graphOwner = storage.graph.get();
    auto* const ccOwner = storage.ccLanes.get();
    assert(graphOwner != nullptr);
    assert(ccOwner != nullptr);
    const auto node = seq::rootStepNodeId(0U);
    const int8_t capturedOffset = graphOwner->stepNodes[node].noteOffset;
    const uint8_t capturedController = ccOwner->lanes[0].destination.controller;

    assert(seq::setNodeNoteOffset(
        source.pattern,
        node,
        static_cast<int8_t>(capturedOffset + 1)
    ));
    source.pattern.ccLanes->formatVersion = 0U;
    assert(!seq::captureHistoryPatternPayloadUsingReservedStorage(
        source.pattern,
        storage
    ));
    assert(storage.graph.get() == graphOwner);
    assert(storage.ccLanes.get() == ccOwner);
    assert(graphOwner->stepNodes[node].noteOffset == capturedOffset);
    assert(ccOwner->lanes[0].destination.controller == capturedController);

    seq::SequencerCcLaneBankPtr detached;
    assert(seq::cloneSequencerCcLaneBank(detached, ccOwner));
    auto* const detachedOwner = detached.get();
    assert(!seq::cloneSequencerCcLaneBank(
        detached,
        source.pattern.ccLanes.get()
    ));
    assert(detached.get() == detachedOwner);
    assert(detached->lanes[0].destination.controller == capturedController);
    assert(!seq::captureSequencerCcLaneBankUsingReservedStorage(
        source.pattern.ccLanes.get(),
        detached
    ));
    assert(detached.get() == detachedOwner);
    assert(detached->lanes[0].destination.controller == capturedController);

    source.pattern.ccLanes->formatVersion = seq::SequencerCcLaneBank::FORMAT_VERSION;
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(!seq::cloneSequencerCcLaneBank(
            detached,
            source.pattern.ccLanes.get()
        ));
        tx::assertFailureConsumed(1U);
        assert(detached.get() == detachedOwner);
    }

    std::cout << "[PASS] CC capture validates before detached/reserved writes\n";
}

void test_structure_after_builder_and_live_revalidation_are_exact() {
    Harness h;
    initializeCapturedTracks(h, PayloadKind::GraphAndCc, 0x0003U);
    auto change = seq::prepareHistoryStructureChangeBefore(
        h.state.sequencerTracks,
        h.state.sequencer,
        0x0003U
    );
    assert(change);

    ExpectedAllocationRequests expected;
    appendPayloadRequests(expected, PayloadKind::GraphAndCc);
    {
        allocation_trace::Scope trace;
        assert(seq::buildHistoryStructureSnapshotAfterFromBefore(
            *change,
            0x0001U,
            0U,
            h.state.sequencer.focusedStep.get(),
            h.state.sequencer.page.get(),
            seq::sequencerHistoryTrackBit(1U)
        ));
        assertAllocationRequests(expected);
    }
    assert(change->after.tracks[0U].graph);
    assert(change->after.tracks[0U].ccLanes);
    assert(!change->after.tracks[1U].graph);
    assert(!change->after.tracks[1U].ccLanes);
    assert(
        change->after.tracks[1U].flat.stepDataRevision ==
        change->before.tracks[1U].flat.stepDataRevision + 1U
    );
    assert(
        change->after.tracks[1U].flat.graphRevision ==
        change->before.tracks[1U].flat.graphRevision + 1U
    );
    assert(
        change->after.tracks[1U].ccLaneRevision ==
        change->before.tracks[1U].ccLaneRevision + 1U
    );

    h.state.sequencerTracks.track(1U).reset();
    assert(seq::liveHistoryStructureSnapshotMatches(
        h.state.sequencerTracks,
        h.state.sequencer,
        change->after
    ));

    auto* const liveGraph = h.state.sequencer.pattern.graph.get();
    assert(liveGraph != nullptr);
    assert(liveGraph->stepNodeCount != 0U);
    const int8_t activeNodeOffset = liveGraph->stepNodes[0U].noteOffset;
    liveGraph->stepNodes[0U].noteOffset =
        static_cast<int8_t>(activeNodeOffset + 1);
    assert(!seq::liveHistoryStructureSnapshotMatches(
        h.state.sequencerTracks,
        h.state.sequencer,
        change->after
    ));
    liveGraph->stepNodes[0U].noteOffset = activeNodeOffset;

    const std::size_t unusedNode = liveGraph->stepNodes.size() - 1U;
    assert(unusedNode >= liveGraph->stepNodeCount);
    const uint16_t unusedFlags = liveGraph->stepNodes[unusedNode].flags;
    liveGraph->stepNodes[unusedNode].flags = static_cast<uint16_t>(
        unusedFlags ^ oc::note::sequencer::STEP_NODE_NOTE_OFFSET
    );
    assert(!seq::liveHistoryStructureSnapshotMatches(
        h.state.sequencerTracks,
        h.state.sequencer,
        change->after
    ));
    liveGraph->stepNodes[unusedNode].flags = unusedFlags;
    assert(seq::liveHistoryStructureSnapshotMatches(
        h.state.sequencerTracks,
        h.state.sequencer,
        change->after
    ));

    auto* const liveCc = h.state.sequencer.pattern.ccLanes.get();
    assert(liveCc != nullptr);
    ++liveCc->revision;
    assert(!seq::liveHistoryStructureSnapshotMatches(
        h.state.sequencerTracks,
        h.state.sequencer,
        change->after
    ));
    --liveCc->revision;
    assert(seq::liveHistoryStructureSnapshotMatches(
        h.state.sequencerTracks,
        h.state.sequencer,
        change->after
    ));

    for (std::size_t ordinal = 1U; ordinal <= 4U; ++ordinal) {
        Harness failed;
        initializeCapturedTracks(failed, PayloadKind::GraphAndCc, 0x0003U);
        auto partial = seq::prepareHistoryStructureChangeBefore(
            failed.state.sequencerTracks,
            failed.state.sequencer,
            0x0003U
        );
        assert(partial);
        auto* const beforeGraph = partial->before.tracks[0U].graph.get();
        auto* const beforeCc = partial->before.tracks[0U].ccLanes.get();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            assert(!seq::buildHistoryStructureSnapshotAfterFromBefore(
                *partial,
                0x0001U,
                0U,
                failed.state.sequencer.focusedStep.get(),
                failed.state.sequencer.page.get()
            ));
            tx::assertFailureConsumed(ordinal);
        }
        assert(partial->before.tracks[0U].graph.get() == beforeGraph);
        assert(partial->before.tracks[0U].ccLanes.get() == beforeCc);
        assert(seq::liveHistoryStructureSnapshotMatches(
            failed.state.sequencerTracks,
            failed.state.sequencer,
            partial->before
        ));
    }

    std::cout << "[PASS] Structure After builder/reset/revalidation are exact\n";
}

void test_trusted_structure_commit_does_not_recheck_admission() {
    Harness h;
    initializeActivePayload(h, PayloadKind::None);
    auto change = seq::prepareHistoryStructureChangeBefore(
        h.state.sequencerTracks,
        h.state.sequencer,
        0x0001U
    );
    assert(change);
    assert(seq::buildHistoryStructureSnapshotAfterFromBefore(
        *change,
        0x0003U,
        0U,
        h.state.sequencer.focusedStep.get(),
        h.state.sequencer.page.get()
    ));
    assert(h.state.sequencerHistory.canRecordStructure(*change));

    // Deliberately break the public precondition after proving admission. The
    // trusted sink must remain a pure ownership transfer with no second policy
    // decision; production callers keep the payload immutable instead.
    change->after.enabledMask = change->before.enabledMask;
    assert(!h.state.sequencerHistory.canRecordStructure(*change));
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.state.sequencerHistory.commitAdmittedStructure(std::move(change));
        tx::assertMaxPlusOneStillArmed(0U);
    }
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::Structure) == 1U);

    std::cout << "[PASS] admitted Structure commit has no defensive recheck\n";
}

void test_trusted_structure_facade_requires_and_uses_admitted_sink() {
    Harness h;
    initializeActivePayload(h, PayloadKind::None);
    auto change = seq::prepareHistoryStructureChangeBefore(
        h.state.sequencerTracks,
        h.state.sequencer,
        0x0001U
    );
    assert(change);
    assert(seq::buildHistoryStructureSnapshotAfterFromBefore(
        *change,
        0x0003U,
        0U,
        h.state.sequencer.focusedStep.get(),
        h.state.sequencer.page.get()
    ));

    const core::handler::SequencerHistoryDomainServices empty;
    assert(!empty.canCommitAdmittedStructure(*change));
    const auto history =
        core::handler::SequencerHistoryDomainServices::fromCoreState(h.state);
    assert(history.canCommitAdmittedStructure(*change));
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        history.commitAdmittedStructure(std::move(change));
        tx::assertMaxPlusOneStillArmed(0U);
    }
    assert(
        h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::Structure) ==
        1U
    );

    std::cout << "[PASS] admitted Structure facade requires the trusted sink\n";
}

void test_macro_replay_validation_and_commit_revision_policy() {
    Harness h;
    auto& pages = h.state.pages;
    const uint16_t trackMask = seq::sequencerHistoryTrackBit(2U);

    auto equalControl = core::app::makeExtmemUnique<
        seq::SequencerHistoryTrackStructureChange
    >();
    assert(equalControl);
    assert(seq::captureMacroTrackStructureHistoryBefore(
        pages,
        trackMask,
        *equalControl,
        2U
    ));
    auto* equalPayload = equalControl->macroStructure.get();
    assert(equalPayload != nullptr);
    assert(equalPayload->affectedTrackIndex == 2U);
    pages.tracks[2U].activePage = 1U;
    assert(seq::captureMacroTrackStructureHistoryAfter(pages, *equalControl));
    assert(!equalPayload->afterControl);

    const uint32_t equalRevision = pages.control.authoredRevision;
    const uint8_t afterPage = pages.tracks[2U].activePage;
    assert(seq::validateMacroTrackStructureHistoryReplay(
        pages,
        *equalPayload,
        false
    ));
    assert(pages.control.authoredRevision == equalRevision);
    assert(pages.tracks[2U].activePage == afterPage);
    seq::commitMacroTrackStructureHistoryReplay(pages, *equalPayload, false);
    assert(pages.control.authoredRevision == equalRevision);
    assert(pages.tracks[2U].activePage == equalPayload->beforeTracks[2U].activePage);
    assert(seq::validateMacroTrackStructureHistoryReplay(
        pages,
        *equalPayload,
        true
    ));
    seq::commitMacroTrackStructureHistoryReplay(pages, *equalPayload, true);
    assert(pages.control.authoredRevision == equalRevision);
    assert(pages.tracks[2U].activePage == afterPage);

    auto distinctControl = core::app::makeExtmemUnique<
        seq::SequencerHistoryTrackStructureChange
    >();
    assert(distinctControl);
    assert(seq::captureMacroTrackStructureHistoryBefore(
        pages,
        trackMask,
        *distinctControl,
        2U
    ));
    auto* distinctPayload = distinctControl->macroStructure.get();
    assert(distinctPayload != nullptr);
    ++pages.control.authored.curves.nextCurveId;
    assert(seq::captureMacroTrackStructureHistoryAfter(pages, *distinctControl));
    assert(distinctPayload->afterControl);
    const uint32_t distinctRevision = pages.control.authoredRevision;
    assert(seq::validateMacroTrackStructureHistoryReplay(
        pages,
        *distinctPayload,
        false
    ));
    seq::commitMacroTrackStructureHistoryReplay(pages, *distinctPayload, false);
    assert(pages.control.authoredRevision == distinctRevision + 1U);
    assert(seq::validateMacroTrackStructureHistoryReplay(
        pages,
        *distinctPayload,
        true
    ));
    seq::commitMacroTrackStructureHistoryReplay(pages, *distinctPayload, true);
    assert(pages.control.authoredRevision == distinctRevision + 2U);

    auto cacheBoundary = core::app::makeExtmemUnique<
        seq::SequencerHistoryTrackStructureChange
    >();
    assert(cacheBoundary);
    const uint16_t activeTrackMask = seq::sequencerHistoryTrackBit(0U);
    assert(seq::captureMacroTrackStructureHistoryBefore(
        pages,
        activeTrackMask,
        *cacheBoundary,
        0U
    ));
    auto* cachePayload = cacheBoundary->macroStructure.get();
    assert(cachePayload != nullptr);
    const uint8_t beforeCc =
        cachePayload->beforeTracks[0U].pages[0U].cc[0U];
    pages.tracks[0U].activePage = 1U;
    pages.tracks[0U].enabledPageMask = 0x0003U;
    pages.tracks[0U].pages[1U].cc[0U] = 99U;
    assert(seq::captureMacroTrackStructureHistoryAfter(pages, *cacheBoundary));
    pages.syncActiveTrackCache();
    pages.updateActiveConfigs();
    assert(pages.currentActivePage() == 1U);
    assert(pages.currentEnabledPageMask() == 0x0003U);
    assert(pages.activeConfigs[0U].cc == 99U);

    assert(seq::validateMacroTrackStructureHistoryReplay(
        pages,
        *cachePayload,
        false
    ));
    seq::commitMacroTrackStructureHistoryReplay(pages, *cachePayload, false);
    assert(pages.tracks[0U].activePage == 0U);
    assert(pages.tracks[0U].enabledPageMask == 0x0001U);
    assert(pages.currentActivePage() == 1U);
    assert(pages.currentEnabledPageMask() == 0x0003U);
    assert(pages.activeConfigs[0U].cc == 99U);

    assert(seq::validateMacroTrackStructureHistoryReplay(
        pages,
        *cachePayload,
        true
    ));
    seq::commitMacroTrackStructureHistoryReplay(pages, *cachePayload, true);
    pages.syncActiveTrackCache();
    pages.updateActiveConfigs();
    assert(pages.currentActivePage() == 1U);
    assert(pages.currentEnabledPageMask() == 0x0003U);
    assert(pages.activeConfigs[0U].cc == 99U);
    assert(seq::validateMacroTrackStructureHistoryReplay(
        pages,
        *cachePayload,
        false
    ));
    seq::commitMacroTrackStructureHistoryReplay(pages, *cachePayload, false);
    pages.syncActiveTrackCache();
    pages.updateActiveConfigs();
    assert(pages.currentActivePage() == 0U);
    assert(pages.currentEnabledPageMask() == 0x0001U);
    assert(pages.activeConfigs[0U].cc == beforeCc);

    auto invalidAffected = core::app::makeExtmemUnique<
        seq::SequencerHistoryTrackStructureChange
    >();
    assert(invalidAffected);
    assert(!seq::captureMacroTrackStructureHistoryBefore(
        pages,
        trackMask,
        *invalidAffected,
        3U
    ));
    assert(!invalidAffected->macroStructure);

    std::cout << "[PASS] Macro replay validates first and advances control revision conditionally\n";
}

}  // namespace

int main() {
    test_pattern_preparation_allocation_matrix();
    test_pattern_staged_provider_overlap_contract();
    test_pattern_commits_are_nofail_and_exactly_once();
    test_pattern_traversal_allocation_failures_are_atomic();
    test_pattern_noop_admission_preserves_live_state();
    test_pattern_identity_and_flat_cc_drift_are_rejected();
    test_partial_pattern_reservation_is_discardable();
    test_generic_publication_resynchronizes_switched_track_payloads();
    test_flat_sync_accepts_coherent_cold_payload_revision_drift();
    test_prepared_publication_is_exact_during_notification_drain();
    test_legacy_prepared_pattern_preserves_pending_bank_synchronization();
    test_full_bank_preparation_allocation_matrix();
    test_full_bank_capture_rejects_active_track_drift();
    test_full_bank_traversal_rejects_active_step_draft_before_allocation();
    test_full_bank_active_scratch_is_excluded_and_cleared_on_apply();
    test_full_bank_apply_restores_revision_contract();
    test_full_bank_noop_budget_and_pruning();
    test_full_bank_commit_is_nofail_and_exactly_once();
    test_structure_preparation_allocation_matrix();
    test_structure_noop_and_commit_are_exact();
    test_structure_frozen_mask_requires_active_track_union();
    test_coupled_structure_replay_allocation_matrix();
    test_coupled_structure_replay_equal_control_preserves_revision();
    test_coupled_structure_replay_playing_waits_for_loop();
    test_coupled_structure_replay_rejects_macro_drift_before_allocation();
    test_coupled_structure_replay_atomic_arm_is_ultimate_gate();
    test_coupled_structure_replay_draft_precedes_allocation();
    test_prepared_bank_admission_rejects_active_step_draft();
    test_presence_growth_is_rejected_by_strict_capture();
    test_cc_capture_validation_preserves_detached_owners();
    test_structure_after_builder_and_live_revalidation_are_exact();
    test_trusted_structure_commit_does_not_recheck_admission();
    test_trusted_structure_facade_requires_and_uses_admitted_sink();
    test_macro_replay_validation_and_commit_revision_policy();
    std::cout << "Sequencer prepared History transaction tests passed\n";
    return 0;
}
