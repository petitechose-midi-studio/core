#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <array>
#include <iostream>
#include <new>

#include "app/ExtmemAllocator.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerProjectScaleOps.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/NotificationTestUtils.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

namespace allocation_trace {

constexpr std::size_t kCapacity = 128U;
bool enabled = false;
std::array<std::size_t, kCapacity> requests{};
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
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }

namespace {

namespace seq = core::state::sequencer;
namespace tx = test_support::sequencer_transaction;
namespace catalog = core::state::sequencer::scale_catalog;

using Owner = seq::SequencerPreparedFullBankEditOwner;
using Outcome = seq::SequencerPreparedFullBankEditOutcome;
using ScaleSettings = oc::note::sequencer::StepSequencerScaleSettings;

constexpr uint8_t kActiveTrack = 0U;
constexpr uint8_t kStep = 0U;
constexpr std::size_t kMaximumAllocationAttempts = 99U;
constexpr std::size_t kArmAllocationHeaderBytes = 16U;
constexpr std::size_t kArmFullBankChangeBytes = 26960U;
constexpr std::size_t kArmTrackBankRootBytes = 31632U;
constexpr std::size_t kArmSequencerRootBytes = 15672U;
constexpr std::size_t kArmGraphBytes = 14792U;
constexpr std::size_t kArmCcBytes = 840U;

static_assert(
    kArmFullBankChangeBytes + kArmTrackBankRootBytes + kArmSequencerRootBytes +
            3U * 16U * (kArmGraphBytes + kArmCcBytes) +
            kMaximumAllocationAttempts * kArmAllocationHeaderBytes ==
        826184U,
    "LOCK-P: prepared FullBank scale caller peak changed"
);

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(seq::SequencerHistoryFullBankChange) == kArmFullBankChangeBytes);
static_assert(sizeof(seq::SequencerTrackBankState) == kArmTrackBankRootBytes);
static_assert(sizeof(seq::SequencerState) == kArmSequencerRootBytes);
static_assert(sizeof(oc::note::sequencer::StepSequencerGraph) == kArmGraphBytes);
static_assert(sizeof(seq::SequencerCcLaneBank) == kArmCcBytes);
#endif

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

bool sameScale(ScaleSettings lhs, ScaleSettings rhs) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

bool sameVariations(
    oc::note::sequencer::StepSequencerVariationRanges lhs,
    oc::note::sequencer::StepSequencerVariationRanges rhs
) {
    lhs.clamp();
    rhs.clamp();
    return lhs.pitchSemitones == rhs.pitchSemitones &&
           lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent &&
           lhs.nudge == rhs.nudge;
}

bool samePatternSnapshot(
    const seq::SequencerPatternSnapshot& lhs,
    const seq::SequencerPatternSnapshot& rhs
) {
    return lhs.length == rhs.length &&
           lhs.playStart == rhs.playStart &&
           lhs.loopStart == rhs.loopStart &&
           lhs.loopEnd == rhs.loopEnd &&
           lhs.stepsPerBeat == rhs.stepsPerBeat &&
           lhs.enabledMask == rhs.enabledMask &&
           lhs.stepDataRevision == rhs.stepDataRevision &&
           lhs.patternVariationRevision == rhs.patternVariationRevision &&
           lhs.patternScaleRevision == rhs.patternScaleRevision &&
           lhs.patternTimingRevision == rhs.patternTimingRevision &&
           lhs.graphRevision == rhs.graphRevision &&
           lhs.swingOffsetPercent == rhs.swingOffsetPercent &&
           lhs.patternNudgePercent == rhs.patternNudgePercent &&
           lhs.effectiveSwingPercent == rhs.effectiveSwingPercent &&
           sameVariations(lhs.variationRanges, rhs.variationRanges) &&
           lhs.scalePolicy == rhs.scalePolicy &&
           sameScale(lhs.scaleOverride, rhs.scaleOverride) &&
           lhs.pitchEditMode == rhs.pitchEditMode &&
           sameScale(lhs.effectiveScaleSettings, rhs.effectiveScaleSettings) &&
           lhs.note == rhs.note &&
           lhs.velocity == rhs.velocity &&
           lhs.gate == rhs.gate &&
           lhs.nudge == rhs.nudge &&
           lhs.probability == rhs.probability;
}

bool sameBankSnapshot(
    const seq::SequencerTrackBankSnapshot& lhs,
    const seq::SequencerTrackBankSnapshot& rhs
) {
    if (lhs.activeTrack != rhs.activeTrack ||
        lhs.enabledMask != rhs.enabledMask ||
        lhs.projectScaleRevision != rhs.projectScaleRevision ||
        !sameScale(lhs.projectScaleSettings, rhs.projectScaleSettings)) {
        return false;
    }
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if (!samePatternSnapshot(lhs.tracks[track], rhs.tracks[track])) return false;
    }
    return true;
}

uint64_t hashBytes(const void* data, std::size_t size) {
    if (data == nullptr) return 0U;
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffset;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0U; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kPrime;
    }
    return hash;
}

uint64_t graphHash(const seq::SequencerPatternState& pattern) {
    const auto* graph = seq::graphView(pattern);
    return hashBytes(graph, graph == nullptr ? 0U : sizeof(*graph));
}

uint64_t ccHash(const seq::SequencerPatternState& pattern) {
    const auto* cc = seq::sequencerCcLaneView(pattern);
    return hashBytes(cc, cc == nullptr ? 0U : sizeof(*cc));
}

void authorPayload(
    seq::SequencerPatternState& pattern,
    PayloadKind kind,
    uint8_t salt = 0U
) {
    if (pattern.length.get() != 8U) {
        assert(pattern.setContentLength(8U));
    }
    assert(pattern.setStepDataAt(
        kStep,
        static_cast<uint8_t>(48U + salt),
        static_cast<uint8_t>(80U + salt),
        seq::SequencerPatternState::DEFAULT_GATE_PERCENT
    ));
    pattern.setEnabled(kStep, true);

    if (hasGraph(kind)) {
        assert(seq::ensureGraphRoot(pattern));
        assert(seq::setNodeNoteOffset(
            pattern,
            seq::rootStepNodeId(kStep),
            static_cast<int8_t>(1 + (salt % 11U))
        ));
    }

    if (hasCc(kind)) {
        auto* lanes = seq::ensureSequencerCcLaneBank(pattern);
        assert(lanes != nullptr);
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = static_cast<uint8_t>(40U + salt);
        assert(seq::createSequencerCcLane(*lanes, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(
            *lanes,
            0U,
            kStep,
            static_cast<uint8_t>(64U + salt)
        ).changed());
        pattern.bumpCcLaneRevision();
    }
}

void settle(Harness& h) {
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    assert(!h.state.hasPendingProjectSessionSave());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
}

void initializeTopology(
    Harness& h,
    PayloadKind kind,
    bool populateEveryCanonicalTrack
) {
    authorPayload(h.state.sequencer.pattern, kind, 0U);
    assert(seq::initializeTrackBankFromActive(
        h.state.sequencerTracks,
        h.state.sequencer
    ));
    if (populateEveryCanonicalTrack) {
        h.state.sequencerTracks.syncSharedTrackState(0xFFFFU, kActiveTrack);
        for (uint8_t track = 1U;
             track < seq::SequencerTrackBankState::TRACK_COUNT;
             ++track) {
            authorPayload(h.state.sequencerTracks.track(track), kind, track);
        }
    }
    settle(h);
}

struct RevisionVector {
    uint32_t step = 0U;
    uint32_t variation = 0U;
    uint32_t scale = 0U;
    uint32_t timing = 0U;
    uint32_t graph = 0U;
    uint32_t cc = 0U;
};

RevisionVector revisions(const seq::SequencerPatternState& pattern) {
    return {
        .step = pattern.stepDataRevision.get(),
        .variation = pattern.patternVariationRevision.get(),
        .scale = pattern.patternScaleRevision.get(),
        .timing = pattern.patternTimingRevision.get(),
        .graph = pattern.graphRevision.get(),
        .cc = pattern.ccLaneRevision.get(),
    };
}

void assertRevisionDelta(
    const seq::SequencerPatternState& pattern,
    const RevisionVector& before,
    uint32_t scaleDelta
) {
    const auto after = revisions(pattern);
    assert(after.step == before.step);
    assert(after.variation == before.variation);
    assert(after.scale == before.scale + scaleDelta);
    assert(after.timing == before.timing);
    assert(after.graph == before.graph);
    assert(after.cc == before.cc);
}

struct PatternProof {
    seq::SequencerPatternSnapshot flat{};
    const void* graphOwner = nullptr;
    const void* ccOwner = nullptr;
    uint32_t ccRevision = 0U;
    uint64_t graphContent = 0U;
    uint64_t ccContent = 0U;
};

PatternProof capturePatternProof(const seq::SequencerPatternState& pattern) {
    PatternProof proof;
    seq::captureSnapshot(pattern, proof.flat);
    proof.graphOwner = pattern.graph.get();
    proof.ccOwner = pattern.ccLanes.get();
    proof.ccRevision = pattern.ccLaneRevision.get();
    proof.graphContent = graphHash(pattern);
    proof.ccContent = ccHash(pattern);
    return proof;
}

void assertPatternProof(
    const seq::SequencerPatternState& pattern,
    const PatternProof& expected
) {
    seq::SequencerPatternSnapshot actual;
    seq::captureSnapshot(pattern, actual);
    assert(samePatternSnapshot(actual, expected.flat));
    assert(pattern.graph.get() == expected.graphOwner);
    assert(pattern.ccLanes.get() == expected.ccOwner);
    assert(pattern.ccLaneRevision.get() == expected.ccRevision);
    assert(graphHash(pattern) == expected.graphContent);
    assert(ccHash(pattern) == expected.ccContent);
}

struct ExactLiveProof {
    tx::StateInvariant state{};
    seq::SequencerTrackBankSnapshot bank{};
    PatternProof editor{};
    std::array<PatternProof, seq::SequencerTrackBankState::TRACK_COUNT> tracks{};
    uint8_t page = 0U;
    uint8_t focus = 0U;
    seq::StepProperty property = seq::StepProperty::NOTE;
    uint32_t telemetryRevision = 0U;
};

ExactLiveProof captureExactLiveProof(const Harness& h) {
    ExactLiveProof proof;
    proof.state = tx::captureStateInvariant(h.state);
    seq::captureTrackBankSnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        proof.bank
    );
    proof.editor = capturePatternProof(h.state.sequencer.pattern);
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        proof.tracks[track] = capturePatternProof(
            h.state.sequencerTracks.track(track)
        );
    }
    proof.page = h.state.sequencer.page.get();
    proof.focus = h.state.sequencer.focusedStep.get();
    proof.property = h.state.sequencer.activeStepProperty.get();
    proof.telemetryRevision = h.state.sequencer.variationTelemetryRevision.get();
    return proof;
}

void assertExactLiveProof(const Harness& h, const ExactLiveProof& expected) {
    tx::assertStateInvariant(h.state, expected.state);
    seq::SequencerTrackBankSnapshot actualBank;
    seq::captureTrackBankSnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        actualBank
    );
    assert(sameBankSnapshot(actualBank, expected.bank));
    assertPatternProof(h.state.sequencer.pattern, expected.editor);
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        assertPatternProof(
            h.state.sequencerTracks.track(track),
            expected.tracks[track]
        );
    }
    assert(h.state.sequencer.page.get() == expected.page);
    assert(h.state.sequencer.focusedStep.get() == expected.focus);
    assert(h.state.sequencer.activeStepProperty.get() == expected.property);
    assert(
        h.state.sequencer.variationTelemetryRevision.get() ==
        expected.telemetryRevision
    );
}

struct CanonicalPayloadProof {
    std::array<uint64_t, seq::SequencerTrackBankState::TRACK_COUNT> graph{};
    std::array<uint64_t, seq::SequencerTrackBankState::TRACK_COUNT> cc{};
};

CanonicalPayloadProof captureCanonicalPayloadProof(const Harness& h) {
    CanonicalPayloadProof proof;
    const uint8_t active = h.state.sequencerTracks.activeTrackIndex();
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const auto& pattern = track == active
            ? h.state.sequencer.pattern
            : h.state.sequencerTracks.track(track);
        proof.graph[track] = graphHash(pattern);
        proof.cc[track] = ccHash(pattern);
    }
    return proof;
}

void assertCanonicalPayloadProof(
    const Harness& h,
    const CanonicalPayloadProof& expected
) {
    const auto actual = captureCanonicalPayloadProof(h);
    assert(actual.graph == expected.graph);
    assert(actual.cc == expected.cc);
}

int currentChoice(uint8_t row, ScaleSettings settings) {
    settings.clamp();
    switch (row) {
        case 0: return settings.root;
        case 1: return catalog::scaleTypeIndex(settings.type);
        case 2: return catalog::constraintModeIndex(settings.mode);
        default: return -1;
    }
}

int changedChoice(uint8_t row, ScaleSettings settings) {
    const int current = currentChoice(row, settings);
    switch (row) {
        case 0: return (current + 1) % catalog::ROOT_COUNT;
        case 1: return (current + 1) % catalog::SCALE_TYPE_COUNT;
        case 2: return (current + 1) % catalog::CONSTRAINT_MODE_COUNT;
        default: return -1;
    }
}

seq::SequencerHistoryDescriptor pendingDescriptor() {
    return {
        .kind = seq::SequencerHistoryActionKind::StepEdit,
        .stepIndex = kStep,
    };
}

void preparePendingPatternEdit(Harness& h) {
    constexpr auto owner = seq::SequencerPreparedPatternEditOwner::PatternEditor;
    constexpr uint8_t key = 91U;
    assert(h.state.beginOrContinueSequencerPreparedPatternEdit(
               owner,
               key,
               seq::SequencerCoalescedPatternPayloadPlan::FlatOnly,
               pendingDescriptor()) ==
           seq::SequencerPreparedPatternEditBeginOutcome::Started);
    assert(h.state.sequencer.setStepNoteAt(kStep, 73U));
    assert(h.state.sealSequencerPreparedPatternEdit(
               owner,
               key,
               true,
               pendingDescriptor()) ==
           seq::SequencerPreparedPatternEditSealOutcome::Sealed);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
}

void beginModifiedChordDraft(Harness& h) {
    const auto node = seq::rootStepNodeId(kStep);
    assert(seq::beginStepContentDraft(
        h.state.sequencer,
        seq::SequencerStepContentDraftKind::CHORD,
        kStep,
        node
    ));
    assert(seq::setAuthoringNodeChordMode(
        h.state.sequencer,
        node,
        oc::note::sequencer::StepSequencerChordMode::Local
    ));
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.modified());
}

void assertProjectScaleDescriptor(const Harness& h) {
    const auto* entry = h.state.projectHistory.peekUndo();
    assert(entry != nullptr);
    assert(entry->domain == core::state::project::ProjectHistoryDomain::Sequencer);
    assert(
        entry->actionKind ==
        static_cast<uint8_t>(seq::SequencerHistoryActionKind::ProjectScaleSettings)
    );
    assert(std::strcmp(
               core::state::project::ProjectHistoryCoordinator::actionLabel(*entry),
               "Project Scale") == 0);
}

void assertScratchEmpty(const Harness& h) {
    const auto& scratch = h.state.sequencerTracks.track(
        h.state.sequencerTracks.activeTrackIndex()
    );
    assert(scratch.graph == nullptr);
    assert(scratch.ccLanes == nullptr);
}

void test_state_operation_rows_revisions_overrides_and_scratch() {
    seq::SequencerTrackBankState bank;
    seq::SequencerState active;
    authorPayload(active.pattern, PayloadKind::GraphAndCc, 0U);
    assert(seq::initializeTrackBankFromActive(bank, active));
    bank.syncSharedTrackState(0xFFFFU, kActiveTrack);
    for (uint8_t track = 1U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        authorPayload(bank.track(track), PayloadKind::GraphAndCc, track);
    }
    assert(bank.track(3U).setPatternScalePolicy(
        seq::SequencerPatternScalePolicy::OVERRIDE
    ));
    assert(bank.track(11U).setPatternScalePolicy(
        seq::SequencerPatternScalePolicy::OVERRIDE
    ));

    const auto current = bank.projectScaleSettings();
    for (uint8_t row = 0U; row < 3U; ++row) {
        const auto noOp = seq::resolveProjectScaleChoice(
            current,
            row,
            currentChoice(row, current)
        );
        assert(noOp.valid);
        assert(!noOp.changes);
        assert(sameScale(noOp.target, current));

        const auto changed = seq::resolveProjectScaleChoice(
            current,
            row,
            changedChoice(row, current)
        );
        assert(changed.valid);
        assert(changed.changes);
        assert(!sameScale(changed.target, current));
    }
    assert(!seq::resolveProjectScaleChoice(current, 3U, 0).valid);

    std::array<RevisionVector, seq::SequencerTrackBankState::TRACK_COUNT> before{};
    std::array<const void*, seq::SequencerTrackBankState::TRACK_COUNT> graphs{};
    std::array<const void*, seq::SequencerTrackBankState::TRACK_COUNT> cc{};
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        before[track] = revisions(bank.track(track));
        graphs[track] = bank.track(track).graph.get();
        cc[track] = bank.track(track).ccLanes.get();
    }
    const auto editorBefore = revisions(active.pattern);
    const auto* editorGraph = active.pattern.graph.get();
    const auto* editorCc = active.pattern.ccLanes.get();
    const uint32_t projectRevision = bank.projectScaleRevisionSignal().get();
    const auto target = seq::resolveProjectScaleChoice(
        current,
        1U,
        changedChoice(1U, current)
    ).target;

    const auto result = seq::applyProjectScaleTransition(bank, active, target);
    assert(result.changed);
    assert(result.projection.failures == 0U);
    assert(sameScale(bank.projectScaleSettings(), target));
    assert(bank.projectScaleRevisionSignal().get() == projectRevision + 1U);
    assertRevisionDelta(active.pattern, editorBefore, 1U);
    assert(active.pattern.graph.get() == editorGraph);
    assert(active.pattern.ccLanes.get() == editorCc);

    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const bool activeScratch = track == kActiveTrack;
        const bool override = track == 3U || track == 11U;
        assertRevisionDelta(bank.track(track), before[track],
                            (!activeScratch && !override) ? 1U : 0U);
        if (activeScratch) {
            assert(bank.track(track).graph == nullptr);
            assert(bank.track(track).ccLanes == nullptr);
        } else {
            assert(bank.track(track).graph.get() == graphs[track]);
            assert(bank.track(track).ccLanes.get() == cc[track]);
        }
    }

    const auto stableEditor = revisions(active.pattern);
    const auto stableProjectRevision = bank.projectScaleRevisionSignal().get();
    const auto noChange = seq::applyProjectScaleTransition(bank, active, target);
    assert(!noChange.changed);
    assertRevisionDelta(active.pattern, stableEditor, 0U);
    assert(bank.projectScaleRevisionSignal().get() == stableProjectRevision);

    std::cout << "[PASS] state scale operation locks rows, revisions, overrides and scratch\n";
}

void test_owner_specific_no_change_chronology() {
    {
        Harness h;
        initializeTopology(h, PayloadKind::None, false);
        preparePendingPatternEdit(h);
        const auto scale = h.state.sequencerTracks.projectScaleSettings();
        const uint8_t historyBefore = h.state.sequencerHistory.undoCount();
        const uint32_t draftRevision = h.state.sequencer.stepContentDraft.revision.get();

        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        const auto result = h.state.applyPreparedProjectScaleChoice(
            Owner::ProjectScale,
            0U,
            currentChoice(0U, scale)
        );
        assert(result.outcome == Outcome::NoChange);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencerHistory.undoCount() == historyBefore);
        assert(h.state.sequencer.stepContentDraft.revision.get() == draftRevision);
        assert(sameScale(h.state.sequencerTracks.projectScaleSettings(), scale));
    }
    tx::assertFailureInjectionReset();

    {
        Harness h;
        initializeTopology(h, PayloadKind::None, false);
        preparePendingPatternEdit(h);
        const auto scale = h.state.sequencerTracks.projectScaleSettings();
        const auto result = h.state.applyPreparedProjectScaleChoice(
            Owner::SequencerSettingsScale,
            1U,
            currentChoice(1U, scale)
        );
        assert(result.outcome == Outcome::NoChange);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencerHistory.undoCount() == 1U);
        assert(h.state.sequencerHistory.undoCount(
                   seq::SequencerHistoryScope::PatternOnly) == 1U);
        assert(h.state.projectHistory.undoCount() == 1U);
        assert(h.state.sequencer.pattern.note[kStep] == 73U);
        assert(h.state.sequencerTracks.track(kActiveTrack).note[kStep] == 73U);
        assert(sameScale(h.state.sequencerTracks.projectScaleSettings(), scale));
    }

    {
        Harness h;
        initializeTopology(h, PayloadKind::Graph, false);
        preparePendingPatternEdit(h);
        beginModifiedChordDraft(h);
        const auto scale = h.state.sequencerTracks.projectScaleSettings();
        const uint32_t draftRevision = h.state.sequencer.stepContentDraft.revision.get();
        const auto result = h.state.applyPreparedProjectScaleChoice(
            Owner::SequencerSettingsScale,
            2U,
            currentChoice(2U, scale)
        );
        assert(result.outcome == Outcome::Blocked);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencerHistory.undoCount() == 1U);
        assert(h.state.sequencer.stepContentDraft.active.get());
        assert(h.state.sequencer.stepContentDraft.modified());
        assert(
            h.state.sequencer.stepContentDraft.failure ==
            seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED
        );
        assert(
            h.state.sequencer.stepContentDraft.blockedTransition ==
            seq::SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        assert(h.state.sequencer.stepContentDraft.revision.get() == draftRevision + 1U);
        assert(sameScale(h.state.sequencerTracks.projectScaleSettings(), scale));
    }

    std::cout << "[PASS] Project and Settings no-change chronology remains owner-specific\n";
}

void test_active_draft_rejects_changed_choices_for_both_owners() {
    constexpr std::array owners{Owner::ProjectScale, Owner::SequencerSettingsScale};
    for (const auto owner : owners) {
        Harness h;
        initializeTopology(h, PayloadKind::GraphAndCc, false);
        beginModifiedChordDraft(h);
        const auto scale = h.state.sequencerTracks.projectScaleSettings();
        const auto musicalBefore = captureExactLiveProof(h);
        const uint32_t draftRevision = h.state.sequencer.stepContentDraft.revision.get();

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            const auto result = h.state.applyPreparedProjectScaleChoice(
                owner,
                0U,
                changedChoice(0U, scale)
            );
            assert(result.outcome == Outcome::Blocked);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assertExactLiveProof(h, musicalBefore);
        }
        tx::assertFailureInjectionReset();

        assert(h.state.sequencer.stepContentDraft.active.get());
        assert(h.state.sequencer.stepContentDraft.modified());
        assert(
            h.state.sequencer.stepContentDraft.failure ==
            seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED
        );
        assert(
            h.state.sequencer.stepContentDraft.blockedTransition ==
            seq::SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        assert(h.state.sequencer.stepContentDraft.revision.get() == draftRevision + 1U);
    }

    std::cout << "[PASS] changed scale choices reject active drafts before allocation\n";
}

void runSuccessfulCase(
    Owner owner,
    uint8_t row,
    PayloadKind kind
) {
    Harness h;
    initializeTopology(h, kind, false);
    const auto current = h.state.sequencerTracks.projectScaleSettings();
    const int choice = changedChoice(row, current);
    const auto resolved = seq::resolveProjectScaleChoice(current, row, choice);
    assert(resolved.valid && resolved.changes);

    seq::SequencerTrackBankSnapshot beforeSnapshot;
    seq::captureTrackBankSnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        beforeSnapshot
    );
    const auto beforePayload = captureCanonicalPayloadProof(h);
    const auto beforeState = tx::captureStateInvariant(h.state);
    const auto editorBefore = revisions(h.state.sequencer.pattern);
    std::array<RevisionVector, seq::SequencerTrackBankState::TRACK_COUNT> trackBefore{};
    std::array<const void*, seq::SequencerTrackBankState::TRACK_COUNT> graphOwners{};
    std::array<const void*, seq::SequencerTrackBankState::TRACK_COUNT> ccOwners{};
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        trackBefore[track] = revisions(h.state.sequencerTracks.track(track));
        graphOwners[track] = h.state.sequencerTracks.track(track).graph.get();
        ccOwners[track] = h.state.sequencerTracks.track(track).ccLanes.get();
    }
    const auto* editorGraph = h.state.sequencer.pattern.graph.get();
    const auto* editorCc = h.state.sequencer.pattern.ccLanes.get();
    const uint32_t telemetryBefore =
        h.state.sequencer.variationTelemetryRevision.get();

    const auto result = h.state.applyPreparedProjectScaleChoice(owner, row, choice);
    assert(result.outcome == Outcome::Committed);
    assert(result.projection.failures == 0U);
    assert(sameScale(h.state.sequencerTracks.projectScaleSettings(), resolved.target));
    assert(
        h.state.sequencerTracks.projectScaleRevisionSignal().get() ==
        beforeSnapshot.projectScaleRevision + 1U
    );
    assertRevisionDelta(h.state.sequencer.pattern, editorBefore, 1U);
    assert(h.state.sequencer.pattern.graph.get() == editorGraph);
    assert(h.state.sequencer.pattern.ccLanes.get() == editorCc);
    assert(
        h.state.sequencer.variationTelemetryRevision.get() ==
        telemetryBefore + 1U
    );
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        assertRevisionDelta(
            h.state.sequencerTracks.track(track),
            trackBefore[track],
            track == kActiveTrack ? 0U : 1U
        );
        if (track != kActiveTrack) {
            assert(h.state.sequencerTracks.track(track).graph.get() == graphOwners[track]);
            assert(h.state.sequencerTracks.track(track).ccLanes.get() == ccOwners[track]);
        }
    }
    assertScratchEmpty(h);

    const auto committed = tx::captureStateInvariant(h.state);
    assert(committed.sequencerUndoCount == beforeState.sequencerUndoCount + 1U);
    assert(committed.sequencerRedoCount == 0U);
    assert(committed.projectUndoCount == beforeState.projectUndoCount + 1U);
    assert(committed.projectRedoCount == 0U);
    assert(committed.modifiedCounter == beforeState.modifiedCounter + 1U);
    assert(committed.dirty);
    assert(committed.sessionSavePending);
    assert(committed.retainedBytes > beforeState.retainedBytes);
    assertProjectScaleDescriptor(h);

    seq::SequencerTrackBankSnapshot afterSnapshot;
    seq::captureTrackBankSnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        afterSnapshot
    );
    const auto afterPayload = captureCanonicalPayloadProof(h);

    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    tx::assertStateInvariant(h.state, committed);

    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    assert(h.state.undoSequencerHistory());
    seq::SequencerTrackBankSnapshot undone;
    seq::captureTrackBankSnapshot(h.state.sequencerTracks, h.state.sequencer, undone);
    assert(sameBankSnapshot(undone, beforeSnapshot));
    assertCanonicalPayloadProof(h, beforePayload);
    assertScratchEmpty(h);

    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter
    );
    assert(h.state.redoSequencerHistory());
    seq::SequencerTrackBankSnapshot redone;
    seq::captureTrackBankSnapshot(h.state.sequencerTracks, h.state.sequencer, redone);
    assert(sameBankSnapshot(redone, afterSnapshot));
    assertCanonicalPayloadProof(h, afterPayload);
    assertScratchEmpty(h);
}

void test_two_owners_three_rows_and_payload_topologies_commit_exactly() {
    struct Case {
        Owner owner;
        uint8_t row;
        PayloadKind kind;
    };
    constexpr std::array cases{
        Case{Owner::ProjectScale, 0U, PayloadKind::None},
        Case{Owner::ProjectScale, 1U, PayloadKind::Graph},
        Case{Owner::ProjectScale, 2U, PayloadKind::Cc},
        Case{Owner::SequencerSettingsScale, 0U, PayloadKind::GraphAndCc},
        Case{Owner::SequencerSettingsScale, 1U, PayloadKind::None},
        Case{Owner::SequencerSettingsScale, 2U, PayloadKind::GraphAndCc},
    };
    for (const auto& item : cases) {
        runSuccessfulCase(item.owner, item.row, item.kind);
    }

    std::cout << "[PASS] two owners, rows 0/1/2 and all payload topologies commit exactly\n";
}

void test_near_budget_scale_commit_prunes_before_publishing() {
    Harness h;
    initializeTopology(h, PayloadKind::GraphAndCc, true);

    const auto initialScale = h.state.sequencerTracks.projectScaleSettings();
    const int firstChoice = changedChoice(0U, initialScale);
    const auto first = h.state.applyPreparedProjectScaleChoice(
        Owner::ProjectScale, 0U, firstChoice);
    assert(first.outcome == Outcome::Committed);
    const std::size_t firstRetained = h.state.sequencerHistory.retainedBytes();
    assert(firstRetained > seq::SequencerHistoryService::RETAINED_BYTE_BUDGET / 2U);
    assert(firstRetained <= seq::SequencerHistoryService::RETAINED_BYTE_BUDGET);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::FullBank) == 1U);
    const uintptr_t firstIdentity = h.state.sequencerHistory.projectHistoryUndoIdentity();
    assert(firstIdentity != 0U);

    const auto scaleAfterFirst = h.state.sequencerTracks.projectScaleSettings();
    const int secondChoice = changedChoice(0U, scaleAfterFirst);
    const auto second = h.state.applyPreparedProjectScaleChoice(
        Owner::SequencerSettingsScale, 0U, secondChoice);
    assert(second.outcome == Outcome::Committed);
    assert(h.state.sequencerHistory.retainedBytes() <=
           seq::SequencerHistoryService::RETAINED_BYTE_BUDGET);
    // Two maximal entries overlap during preparation, but only the newest can
    // remain under the retained-byte cap. Admission is pre-live; pruning is
    // part of the trusted ownership transfer.
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::FullBank) == 1U);
    assert(h.state.sequencerHistory.projectHistoryUndoIdentity() != firstIdentity);
    assert(h.state.undoSequencerHistory());
    assert(sameScale(
        h.state.sequencerTracks.projectScaleSettings(), scaleAfterFirst));

    std::cout << "[PASS] near-budget typed commit prunes the oldest maximal entry\n";
}

void test_maximum_topology_fail_nth_is_exact_and_atomic() {
    Harness h;
    initializeTopology(h, PayloadKind::GraphAndCc, true);
    const auto& scratch = h.state.sequencerTracks.track(kActiveTrack);
    assert(scratch.graph != nullptr);
    assert(scratch.ccLanes != nullptr);
    const auto before = captureExactLiveProof(h);
    const auto current = h.state.sequencerTracks.projectScaleSettings();
    const int choice = changedChoice(0U, current);

    constexpr std::array owners{
        Owner::ProjectScale,
        Owner::SequencerSettingsScale,
    };
    for (const Owner owner : owners) {
        for (std::size_t ordinal = 1U;
             ordinal <= kMaximumAllocationAttempts;
             ++ordinal) {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            const auto result = h.state.applyPreparedProjectScaleChoice(
                owner,
                0U,
                choice
            );
            assert(result.outcome == Outcome::ResourceUnavailable);
            assert(result.projection.patternsVisited == 0U);
            tx::assertFailureConsumed(ordinal);
            assertExactLiveProof(h, before);
            assert(h.state.sequencerTracks.track(kActiveTrack).graph != nullptr);
            assert(h.state.sequencerTracks.track(kActiveTrack).ccLanes != nullptr);
        }
    }
    tx::assertFailureInjectionReset();

    std::array<RevisionVector, seq::SequencerTrackBankState::TRACK_COUNT> trackBefore{};
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        trackBefore[track] = revisions(h.state.sequencerTracks.track(track));
    }
    const auto editorBefore = revisions(h.state.sequencer.pattern);
    const auto stateBefore = tx::captureStateInvariant(h.state);
    {
        allocation_trace::Scope trace;
        core::app::testing::ScopedExtmemAllocationFailure failure(
            kMaximumAllocationAttempts + 1U
        );
        const auto result = h.state.applyPreparedProjectScaleChoice(
            Owner::ProjectScale,
            0U,
            choice
        );
        assert(result.outcome == Outcome::Committed);
        tx::assertMaxPlusOneStillArmed(kMaximumAllocationAttempts);

        assert(!allocation_trace::overflow);
        assert(allocation_trace::count == kMaximumAllocationAttempts);
        std::size_t request = 0U;
        assert(allocation_trace::requests[request++] ==
               sizeof(seq::SequencerHistoryFullBankChange));
        for (uint8_t pass = 0U; pass < 2U; ++pass) {
            for (uint8_t owner = 0U;
                 owner < seq::SequencerTrackBankState::TRACK_COUNT;
                 ++owner) {
                assert(allocation_trace::requests[request++] ==
                       sizeof(oc::note::sequencer::StepSequencerGraph));
                assert(allocation_trace::requests[request++] ==
                       sizeof(seq::SequencerCcLaneBank));
            }
        }
        assert(allocation_trace::requests[request++] ==
               sizeof(seq::SequencerTrackBankState));
        assert(allocation_trace::requests[request++] == sizeof(seq::SequencerState));
        for (uint8_t owner = 0U;
             owner < seq::SequencerTrackBankState::TRACK_COUNT;
             ++owner) {
            assert(allocation_trace::requests[request++] ==
                   sizeof(oc::note::sequencer::StepSequencerGraph));
            assert(allocation_trace::requests[request++] ==
                   sizeof(seq::SequencerCcLaneBank));
        }
        assert(request == kMaximumAllocationAttempts);
    }
    tx::assertFailureInjectionReset();

    assertRevisionDelta(h.state.sequencer.pattern, editorBefore, 1U);
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        assertRevisionDelta(
            h.state.sequencerTracks.track(track),
            trackBefore[track],
            track == kActiveTrack ? 0U : 1U
        );
    }
    assertScratchEmpty(h);
    assert(h.state.sequencerHistory.undoCount() == stateBefore.sequencerUndoCount + 1U);
    assert(h.state.projectHistory.undoCount() == stateBefore.projectUndoCount + 1U);
    assertProjectScaleDescriptor(h);

    Harness settings;
    initializeTopology(settings, PayloadKind::GraphAndCc, true);
    const auto settingsBefore = tx::captureStateInvariant(settings.state);
    const int settingsChoice = changedChoice(
        0U, settings.state.sequencerTracks.projectScaleSettings());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            kMaximumAllocationAttempts + 1U);
        const auto result = settings.state.applyPreparedProjectScaleChoice(
            Owner::SequencerSettingsScale, 0U, settingsChoice);
        assert(result.outcome == Outcome::Committed);
        tx::assertMaxPlusOneStillArmed(kMaximumAllocationAttempts);
    }
    tx::assertFailureInjectionReset();
    assertScratchEmpty(settings);
    assert(settings.state.sequencerHistory.undoCount() ==
           settingsBefore.sequencerUndoCount + 1U);
    assert(settings.state.projectHistory.undoCount() ==
           settingsBefore.projectUndoCount + 1U);

    std::cout << "[PASS] both owners are atomic for fail-1..99 and pass armed at 100\n";
}

}  // namespace

int main() {
    test_state_operation_rows_revisions_overrides_and_scratch();
    test_owner_specific_no_change_chronology();
    test_active_draft_rejects_changed_choices_for_both_owners();
    test_two_owners_three_rows_and_payload_topologies_commit_exactly();
    test_near_budget_scale_commit_prunes_before_publishing();
    test_maximum_topology_fail_nth_is_exact_and_atomic();
    std::cout << "All SequencerPreparedFullBankScaleTransaction tests passed.\n";
    return 0;
}
