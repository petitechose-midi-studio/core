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
#include "state/sequencer/SequencerHistory.hpp"
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

struct ScaleMutationResult {
    bool changed = false;
    core::state::sequencer::SequencerChordContextProjectionStats projection{};
};
ScaleMutationResult applyScaleTransition(
    core::state::sequencer::SequencerTrackBankState& bank,
    core::state::sequencer::SequencerState& active,
    oc::note::sequencer::StepSequencerScaleSettings target) {
    namespace seq = core::state::sequencer;
    seq::SequencerClipGridState clips;
    ScaleMutationResult result;
    auto change = seq::prepareHistoryProjectScaleChange(bank, active, clips, target, result.projection);
    if (change) result.changed = seq::applyHistoryProjectScaleChange(*change, bank, active, &clips, true);
    return result;
}



namespace seq = core::state::sequencer;
namespace tx = test_support::sequencer_transaction;
namespace catalog = core::state::sequencer::scale_catalog;

using Owner = seq::SequencerProjectScaleEditOwner;
using Outcome = seq::SequencerProjectScaleEditOutcome;
using ScaleSettings = oc::note::sequencer::StepSequencerScaleSettings;

constexpr uint8_t kActiveTrack = 0U;
constexpr uint8_t kStep = 0U;
// A root-only transition keeps degree formulas unchanged, even with 16 Graph/CC pairs.
constexpr std::size_t kMaximumAllocationAttempts = 1U;

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

bool sameClipSnapshot(
    const seq::SequencerClipState& lhs,
    const seq::SequencerClipState& rhs
) {
    return lhs.playStartTick == rhs.playStartTick &&
           lhs.loopStartTick == rhs.loopStartTick &&
           lhs.loopEndTick == rhs.loopEndTick;
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
        if (!samePatternSnapshot(lhs.tracks[track], rhs.tracks[track]) ||
            !sameClipSnapshot(lhs.clips[track], rhs.clips[track])) {
            return false;
        }
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
    if (pattern.length != 8U) {
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
    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
    assert(!h.state.hasPendingProjectSessionSave());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
}

void initializeTopology(
    Harness& h,
    PayloadKind kind,
    bool populateEveryCanonicalTrack
) {
    authorPayload(h.state.sequencer.pattern(), kind, 0U);

    if (populateEveryCanonicalTrack) {
        h.state.sequencerTracks.syncSharedTrackState(0xFFFFU, kActiveTrack);
        for (uint8_t track = 1U;
             track < seq::SequencerTrackBankState::TRACK_COUNT;
             ++track) {
            authorPayload(h.state.sequencerTracks.track(track), kind, track);
        }
    }
    h.state.sequencerClips.synchronizeEnabledTracks(h.state.sequencerTracks.currentEnabledMask());
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
        .step = pattern.stepDataRevision,
        .variation = pattern.patternVariationRevision,
        .scale = pattern.patternScaleRevision,
        .timing = pattern.patternTimingRevision,
        .graph = pattern.graphRevision,
        .cc = pattern.ccLaneRevision,
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
    proof.ccRevision = pattern.ccLaneRevision;
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
    assert(pattern.ccLaneRevision == expected.ccRevision);
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
    proof.editor = capturePatternProof(h.state.sequencer.pattern());
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
    assertPatternProof(h.state.sequencer.pattern(), expected.editor);
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
            ? h.state.sequencer.pattern()
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

void assertCanonicalOwner(const Harness& h) {
    const auto& scratch = h.state.sequencerTracks.track(
        h.state.sequencerTracks.activeTrackIndex()
    );
    assert(&scratch == &h.state.sequencer.pattern());
}

void test_state_operation_rows_revisions_overrides_and_scratch() {
    seq::SequencerTrackBankState bank;
    seq::SequencerState active{bank.track(bank.activeTrackIndex()), bank.clip(bank.activeTrackIndex())};
    authorPayload(active.pattern(), PayloadKind::GraphAndCc, 0U);

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
    const auto editorBefore = revisions(active.pattern());
    const auto* editorGraph = active.pattern().graph.get();
    const auto* editorCc = active.pattern().ccLanes.get();
    const uint32_t projectRevision = bank.projectScaleRevisionSignal().get();
    const auto target = seq::resolveProjectScaleChoice(
        current,
        1U,
        changedChoice(1U, current)
    ).target;

    const auto result = applyScaleTransition(bank, active, target);
    assert(result.changed);
    assert(result.projection.failures == 0U);
    assert(sameScale(bank.projectScaleSettings(), target));
    assert(bank.projectScaleRevisionSignal().get() == projectRevision + 1U);
    assertRevisionDelta(active.pattern(), editorBefore, 1U);
    assert(active.pattern().graph.get() == editorGraph);
    assert(active.pattern().ccLanes.get() == editorCc);

    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const bool override = track == 3U || track == 11U;
        assertRevisionDelta(bank.track(track), before[track],
                            !override ? 1U : 0U);
        assert(bank.track(track).graph.get() == graphs[track]);
        assert(bank.track(track).ccLanes.get() == cc[track]);
    }

    const auto stableEditor = revisions(active.pattern());
    const auto stableProjectRevision = bank.projectScaleRevisionSignal().get();
    const auto noChange = applyScaleTransition(bank, active, target);
    assert(!noChange.changed);
    assertRevisionDelta(active.pattern(), stableEditor, 0U);
    assert(bank.projectScaleRevisionSignal().get() == stableProjectRevision);

    std::cout << "[PASS] state scale operation locks rows, revisions, overrides and scratch\n";
}

void test_project_no_change_bypasses_history_and_allocation() {
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

    std::cout << "[PASS] Project no-change bypasses history and allocation\n";
}

void test_active_draft_rejects_changed_project_choice() {
    constexpr auto owner = Owner::ProjectScale;
    {
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

    std::cout << "[PASS] changed Project choice rejects active draft before allocation\n";
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
    const auto editorBefore = revisions(h.state.sequencer.pattern());
    std::array<RevisionVector, seq::SequencerTrackBankState::TRACK_COUNT> trackBefore{};
    std::array<const void*, seq::SequencerTrackBankState::TRACK_COUNT> graphOwners{};
    std::array<const void*, seq::SequencerTrackBankState::TRACK_COUNT> ccOwners{};
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        trackBefore[track] = revisions(h.state.sequencerTracks.track(track));
        graphOwners[track] = h.state.sequencerTracks.track(track).graph.get();
        ccOwners[track] = h.state.sequencerTracks.track(track).ccLanes.get();
    }
    const auto* editorGraph = h.state.sequencer.pattern().graph.get();
    const auto* editorCc = h.state.sequencer.pattern().ccLanes.get();
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
    assertRevisionDelta(h.state.sequencer.pattern(), editorBefore, 1U);
    assert(h.state.sequencer.pattern().graph.get() == editorGraph);
    assert(h.state.sequencer.pattern().ccLanes.get() == editorCc);
    assert(
        h.state.sequencer.variationTelemetryRevision.get() ==
        telemetryBefore + 1U
    );
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        assertRevisionDelta(
            h.state.sequencerTracks.track(track),
            trackBefore[track],
            1U
        );
        if (track != kActiveTrack) {
            assert(h.state.sequencerTracks.track(track).graph.get() == graphOwners[track]);
            assert(h.state.sequencerTracks.track(track).ccLanes.get() == ccOwners[track]);
        }
    }
    assertCanonicalOwner(h);

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

    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
    assert(h.state.undoSequencerHistory());
    seq::SequencerTrackBankSnapshot undone;
    seq::captureTrackBankSnapshot(h.state.sequencerTracks, h.state.sequencer, undone);
    assert(sameBankSnapshot(undone, beforeSnapshot));
    assertCanonicalPayloadProof(h, beforePayload);
    assertCanonicalOwner(h);

    h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
    assert(h.state.redoSequencerHistory());
    seq::SequencerTrackBankSnapshot redone;
    seq::captureTrackBankSnapshot(h.state.sequencerTracks, h.state.sequencer, redone);
    assert(sameBankSnapshot(redone, afterSnapshot));
    assertCanonicalPayloadProof(h, afterPayload);
    assertCanonicalOwner(h);
}

void test_project_owner_rows_and_payload_topologies_commit_exactly() {
    struct Case {
        Owner owner;
        uint8_t row;
        PayloadKind kind;
    };
    constexpr std::array cases{
        Case{Owner::ProjectScale, 0U, PayloadKind::None},
        Case{Owner::ProjectScale, 1U, PayloadKind::Graph},
        Case{Owner::ProjectScale, 2U, PayloadKind::Cc},
        Case{Owner::ProjectScale, 0U, PayloadKind::GraphAndCc},
    };
    for (const auto& item : cases) {
        runSuccessfulCase(item.owner, item.row, item.kind);
    }

    std::cout << "[PASS] Project owner rows and all payload topologies commit exactly\n";
}

void test_scale_edits_retain_only_their_context() {
    Harness h;
    initializeTopology(h, PayloadKind::GraphAndCc, true);

    const auto initialScale = h.state.sequencerTracks.projectScaleSettings();
    const int firstChoice = changedChoice(0U, initialScale);
    const auto first = h.state.applyPreparedProjectScaleChoice(
        Owner::ProjectScale, 0U, firstChoice);
    assert(first.outcome == Outcome::Committed);
    const std::size_t firstRetained = h.state.sequencerHistory.retainedBytes();
    assert(firstRetained < 4096U);
    assert(firstRetained <= seq::SequencerHistoryService::RETAINED_BYTE_BUDGET);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::ProjectScale) == 1U);
    const uintptr_t firstIdentity = h.state.sequencerHistory.projectHistoryUndoIdentity();
    assert(firstIdentity != 0U);

    const auto scaleAfterFirst = h.state.sequencerTracks.projectScaleSettings();
    const int secondChoice = changedChoice(0U, scaleAfterFirst);
    const auto second = h.state.applyPreparedProjectScaleChoice(
        Owner::ProjectScale, 0U, secondChoice);
    assert(second.outcome == Outcome::Committed);
    assert(h.state.sequencerHistory.retainedBytes() <=
           seq::SequencerHistoryService::RETAINED_BYTE_BUDGET);
    // Independent scale edits now fit together without retaining Graph/CC banks.
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::ProjectScale) == 2U);
    assert(h.state.sequencerHistory.projectHistoryUndoIdentity() != firstIdentity);
    assert(h.state.undoSequencerHistory());
    assert(sameScale(
        h.state.sequencerTracks.projectScaleSettings(), scaleAfterFirst));

    std::cout << "[PASS] scale edits retain context without copying unrelated payloads\n";
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

    for (std::size_t ordinal = 1U;
         ordinal <= kMaximumAllocationAttempts;
         ++ordinal) {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        const auto result = h.state.applyPreparedProjectScaleChoice(
            Owner::ProjectScale,
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
    tx::assertFailureInjectionReset();

    std::array<RevisionVector, seq::SequencerTrackBankState::TRACK_COUNT> trackBefore{};
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        trackBefore[track] = revisions(h.state.sequencerTracks.track(track));
    }
    const auto editorBefore = revisions(h.state.sequencer.pattern());
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
        assert(allocation_trace::requests[0] == sizeof(seq::SequencerHistoryProjectScaleChange));
        std::cout << "[MEASURE] root-only allocation count=" << allocation_trace::count
                  << " bytes=" << allocation_trace::requests[0] << "\n";
    }
    tx::assertFailureInjectionReset();

    assertRevisionDelta(h.state.sequencer.pattern(), editorBefore, 1U);
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        assertRevisionDelta(
            h.state.sequencerTracks.track(track),
            trackBefore[track],
            1U
        );
    }
    assertCanonicalOwner(h);
    assert(h.state.sequencerHistory.undoCount() == stateBefore.sequencerUndoCount + 1U);
    assert(h.state.projectHistory.undoCount() == stateBefore.projectUndoCount + 1U);
    assertProjectScaleDescriptor(h);

    std::cout << "[PASS] Project scale context is atomic for fail-1 and passes armed at 2\n";
}

using ChordSpec = oc::note::sequencer::StepSequencerChordSpec;
using ChordBasis = oc::note::sequencer::StepSequencerChordIntervalBasis;

ChordSpec degreeChord() {
    auto chord = ChordSpec::semantic(oc::note::sequencer::StepSequencerChordHarmony::Custom,
        3U, oc::note::sequencer::StepSequencerChordVoicing::Close, 0, ChordBasis::ScaleDegrees);
    chord.setCustomInterval(1U, 2U);
    chord.setCustomInterval(2U, 4U);
    return chord;
}
int chromaticChoice() {
    return catalog::scaleTypeIndex(oc::note::sequencer::StepSequencerScaleType::Chromatic);
}
void authorProjectChords(Harness& h) {
    initializeTopology(h, PayloadKind::GraphAndCc, true);
    for (uint8_t track = 0U; track < 16U; ++track) {
        auto& pattern = h.state.sequencerTracks.track(track);
        assert(seq::setNodeChordSpec(pattern, seq::rootStepNodeId(0U), degreeChord()));
    }
    settle(h);
}

void test_exact_chords_failures_and_allocation_free_replay() {
    Harness h;
    authorProjectChords(h);
    const auto before = captureExactLiveProof(h);
    const auto beforePayload = captureCanonicalPayloadProof(h);
    for (std::size_t ordinal = 1U; ordinal <= 2U; ++ordinal) {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        assert(h.state.applyPreparedProjectScaleChoice(Owner::ProjectScale, 1U, chromaticChoice()).outcome
               == Outcome::ResourceUnavailable);
        tx::assertFailureConsumed(ordinal);
        assertExactLiveProof(h, before);
    }
    {
        allocation_trace::Scope trace;
        core::app::testing::ScopedExtmemAllocationFailure failure(3U);
        auto result = h.state.applyPreparedProjectScaleChoice(Owner::ProjectScale, 1U, chromaticChoice());
        assert(result.outcome == Outcome::Committed && result.projection.changed == 16U);
        tx::assertMaxPlusOneStillArmed(2U);
        assert(allocation_trace::count == 2U);
        assert(allocation_trace::requests[1] == 16U * sizeof(seq::SequencerProjectScaleChordChange));
        std::cout << "[MEASURE] 16 projected chords allocations=2 bytes="
                  << allocation_trace::requests[0] + allocation_trace::requests[1] << "\n";
    }
    const auto afterPayload = captureCanonicalPayloadProof(h);
    assert(afterPayload.graph != beforePayload.graph);
    assert(afterPayload.cc == beforePayload.cc);
    for (bool redo : {false, true}) {
        assert(seq::beginStepContentDraft(h.state.sequencer,
            seq::SequencerStepContentDraftKind::CHORD, 0U, seq::rootStepNodeId(0U)));
        const auto undoCount = h.state.sequencerHistory.undoCount();
        const auto redoCount = h.state.sequencerHistory.redoCount();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(!(redo ? h.state.redoSequencerHistory() : h.state.undoSequencerHistory()));
            tx::assertMaxPlusOneStillArmed(0U);
        }
        assert(h.state.sequencerHistory.undoCount() == undoCount);
        assert(h.state.sequencerHistory.redoCount() == redoCount);
        seq::abandonStepContentDraft(h.state.sequencer);
        assert(redo ? h.state.redoSequencerHistory() : h.state.undoSequencerHistory());
    }
    for (unsigned pass = 0U; pass < 3U; ++pass) {
        allocation_trace::Scope trace;
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.undoSequencerHistory());
        assertCanonicalPayloadProof(h, beforePayload);
        assert(h.state.redoSequencerHistory());
        assertCanonicalPayloadProof(h, afterPayload);
        assert(allocation_trace::count == 0U);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    assert(h.state.sequencer.pattern().graph.get() == before.editor.graphOwner);
    assert(h.state.sequencer.pattern().ccLanes.get() == before.editor.ccOwner);
    std::cout << "[PASS] exact chords, fail-1..2 and repeated allocation-free Undo/Redo\n";
}

void test_scale_admission_and_late_drift_are_atomic() {
    Harness h;
    authorProjectChords(h);
    core::state::project::ProjectHistoryEventSink reject;
    reject.canRetain = [](void*, core::state::project::ProjectHistoryDomain,
                          core::state::project::ProjectHistoryRetainedUsage) { return false; };
    h.state.sequencerHistory.setProjectHistoryEventSink(&reject);
    const auto before = captureExactLiveProof(h);
    assert(h.state.applyPreparedProjectScaleChoice(Owner::ProjectScale, 1U, chromaticChoice()).outcome
           == Outcome::HistoryUnavailable);
    assertExactLiveProof(h, before);
    h.state.sequencerHistory.setProjectHistoryEventSink(nullptr);

    auto target = seq::resolveProjectScaleChoice(h.state.sequencerTracks.projectScaleSettings(),
                                                1U, chromaticChoice()).target;
    seq::SequencerChordContextProjectionStats stats;
    auto change = seq::prepareHistoryProjectScaleChange(h.state.sequencerTracks, h.state.sequencer,
                                                        h.state.sequencerClips, target, stats);
    assert(change && change->chordCount == 16U);
    auto& last = h.state.sequencerTracks.track(15U).graph->stepNodes[0U].chordSpec;
    last.setCustomInterval(1U, 1U);
    const auto drifted = captureExactLiveProof(h);
    assert(!seq::applyHistoryProjectScaleChange(*change, h.state.sequencerTracks,
                                               h.state.sequencer, &h.state.sequencerClips, true));
    assertExactLiveProof(h, drifted);
    last = degreeChord();
    assert(seq::applyHistoryProjectScaleChange(*change, h.state.sequencerTracks,
                                              h.state.sequencer, &h.state.sequencerClips, true));
}

void test_nonresident_clips_and_navigation_replay() {
    Harness h;
    authorProjectChords(h);
    assert(h.state.duplicateSequencerClip({0U, 0U}, {0U, 1U}));
    auto* inactive = h.state.sequencerClips.inactiveDocument({0U, 1U});
    assert(inactive);
    const auto inactiveBefore = hashBytes(inactive->pattern.graph.get(), sizeof(*inactive->pattern.graph));
    const auto residentBefore = graphHash(h.state.sequencer.pattern());
    assert(h.state.clearProjectHistory());
    settle(h);
    auto result = h.state.applyPreparedProjectScaleChoice(Owner::ProjectScale, 1U, chromaticChoice());
    assert(result.outcome == Outcome::Committed && result.projection.changed == 17U);
    const auto inactiveAfter = hashBytes(inactive->pattern.graph.get(), sizeof(*inactive->pattern.graph));
    const auto residentAfter = graphHash(h.state.sequencer.pattern());
    assert(inactiveBefore != inactiveAfter);
    assert(h.state.switchSequencerClipForEditing({0U, 1U}));
    assert(seq::switchActiveTrack(h.state.sequencerTracks, h.state.sequencer, 1U));
    assert(h.state.deleteSequencerClip({0U, 1U}));
    assert(h.state.undoSequencerHistory());
    h.state.sequencer.focusedStep.set(3U);
    const auto* owner = h.state.sequencerTracks.track(0U).graph.get();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(h.state.undoSequencerHistory());
        assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
        assert(h.state.sequencerClips.residentSlot(0U) == 1U);
        assert(h.state.sequencer.focusedStep.get() == 3U);
        assert(graphHash(h.state.sequencerTracks.track(0U)) == inactiveBefore);
        auto* doc = h.state.sequencerClips.inactiveDocument({0U, 0U});
        assert(hashBytes(doc->pattern.graph.get(), sizeof(*doc->pattern.graph)) == residentBefore);
        assert(h.state.redoSequencerHistory());
        assert(graphHash(h.state.sequencerTracks.track(0U)) == inactiveAfter);
        assert(hashBytes(doc->pattern.graph.get(), sizeof(*doc->pattern.graph)) == residentAfter);
        assert(h.state.sequencerTracks.track(0U).graph.get() == owner);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    std::cout << "[PASS] nonresident clips project and replay at their logical address after navigation\n";
}

void test_dense_chords_and_retained_budget() {
    Harness h;
    authorProjectChords(h);
    for (uint8_t track = 0U; track < 16U; ++track) {
        auto& graph = *h.state.sequencerTracks.track(track).graph;
        graph.stepNodeCount = static_cast<uint16_t>(graph.stepNodes.size());
        for (auto& node : graph.stepNodes) {
            node.flags |= oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
            node.chordSpec = degreeChord();
        }
    }
    settle(h);
    const auto before = captureCanonicalPayloadProof(h);
    {
        allocation_trace::Scope trace;
        auto result = h.state.applyPreparedProjectScaleChoice(Owner::ProjectScale, 1U, chromaticChoice());
        assert(result.outcome == Outcome::Committed && result.projection.changed == 8192U);
        assert(allocation_trace::count == 2U);
        assert(allocation_trace::requests[1] == 8192U * sizeof(seq::SequencerProjectScaleChordChange));
        std::cout << "[MEASURE] 8192 projected chords allocations=2 bytes="
                  << allocation_trace::requests[0] + allocation_trace::requests[1] << "\n";
    }
    assert(h.state.undoSequencerHistory());
    assertCanonicalPayloadProof(h, before);
    assert(h.state.clearProjectHistory());
    // The grid admits at most 16 inactive documents. Fill this real bound;
    // four dense entries exceed the history byte budget despite fitting its scope limit.
    for (uint8_t track = 0U; track < 16U; ++track) {
        seq::SequencerClipDocumentPtr doc;
        const auto& pattern = h.state.sequencerTracks.track(track);
        assert(seq::captureSequencerClipDocument(pattern,
            h.state.sequencerTracks.clip(track),
            seq::SequencerTrackKind::INSTRUMENT, nullptr, doc));
        doc->pattern.ccLanes.reset();
        assert(h.state.sequencerClips.installInactiveDocument({track, 1U}, std::move(doc)));
    }
    settle(h);
    const auto* last = h.state.sequencerClips.inactiveDocument({15U, 1U});
    for (unsigned edit = 0U; edit < 4U; ++edit) {
        const int choice = edit % 2U == 0U ? chromaticChoice() :
            catalog::scaleTypeIndex(oc::note::sequencer::StepSequencerScaleType::HarmonicMinor);
        auto result = h.state.applyPreparedProjectScaleChoice(Owner::ProjectScale, 1U, choice);
        assert(result.outcome == Outcome::Committed && result.projection.changed == 16384U);
        assert(h.state.sequencerHistory.retainedBytes() <= seq::SequencerHistoryService::RETAINED_BYTE_BUDGET);
    }
    const auto lastHash = hashBytes(last->pattern.graph.get(), sizeof(*last->pattern.graph));
    assert(h.state.sequencerHistory.undoCount() == 3U);
    assert(h.state.projectHistory.undoCount() == 3U);
    assert(h.state.sequencerHistory.retainedSpans() == 6U);
    for (unsigned edit = 0U; edit < 3U; ++edit) assert(h.state.undoSequencerHistory());
    assert(!h.state.undoSequencerHistory());
    for (unsigned edit = 0U; edit < 3U; ++edit) assert(h.state.redoSequencerHistory());
    assert(hashBytes(last->pattern.graph.get(), sizeof(*last->pattern.graph)) == lastHash);
    std::cout << "[PASS] 32 dense patterns prune exactly at the retained-byte budget\n";
}

void test_nested_lossy_projection_and_empty_disabled_overrides() {
    Harness h;
    authorProjectChords(h);
    auto source = h.state.sequencerTracks.projectScaleSettings();
    source.type = oc::note::sequencer::StepSequencerScaleType::Chromatic;
    assert(h.state.sequencerTracks.setProjectScaleSettings(source));
    auto& pattern = h.state.sequencer.pattern();
    const auto micro = seq::createMicroSequence(pattern, 0U, 2U);
    assert(micro.ok);
    const auto child = pattern.graph->sequences[micro.id].firstStepNode;
    const auto cycle = seq::createCycleStateSet(pattern, child, 2U);
    assert(cycle.ok);
    const auto leaf = pattern.graph->cycleSets[cycle.id].firstStateNode;
    auto chromatic = degreeChord();
    chromatic = ChordSpec::semantic(oc::note::sequencer::StepSequencerChordHarmony::Custom,
        3U, oc::note::sequencer::StepSequencerChordVoicing::Close, 0, ChordBasis::ChromaticSemitones);
    chromatic.setCustomInterval(1U, 1U);
    chromatic.setCustomInterval(2U, 6U);
    assert(seq::setNodeChordSpec(pattern, child, chromatic));
    assert(seq::setNodeChordSpec(pattern, leaf, chromatic));
    assert(seq::setNodeNoteOffset(pattern, child, 2));
    assert(h.state.sequencerTracks.track(2U).setPatternScalePolicy(seq::SequencerPatternScalePolicy::OVERRIDE));
    h.state.sequencerTracks.track(3U).graph->enabled = false;
    assert(h.state.sequencerClips.clearResident({15U, 0U}));
    settle(h);
    const auto before = captureCanonicalPayloadProof(h);
    const auto choice = catalog::scaleTypeIndex(oc::note::sequencer::StepSequencerScaleType::HarmonicMinor);
    const auto result = h.state.applyPreparedProjectScaleChoice(Owner::ProjectScale, 1U, choice);
    assert(result.outcome == Outcome::Committed && result.projection.changed >= 2U);
    assert(result.projection.hasAdaptations());
    const auto after = captureCanonicalPayloadProof(h);
    assert(after.graph[2U] == before.graph[2U] && after.graph[3U] == before.graph[3U]);
    assert(h.state.undoSequencerHistory());
    assertCanonicalPayloadProof(h, before);
    assert(h.state.redoSequencerHistory());
    assertCanonicalPayloadProof(h, after);
    assert(h.state.sequencerClips.residentSlot(15U) == seq::SequencerClipGridState::INVALID_SLOT);
}

}  // namespace

int main() {
    test_dense_chords_and_retained_budget();
    test_nested_lossy_projection_and_empty_disabled_overrides();
    test_exact_chords_failures_and_allocation_free_replay();
    test_scale_admission_and_late_drift_are_atomic();
    test_nonresident_clips_and_navigation_replay();
    test_state_operation_rows_revisions_overrides_and_scratch();
    test_project_no_change_bypasses_history_and_allocation();
    test_active_draft_rejects_changed_project_choice();
    test_project_owner_rows_and_payload_topologies_commit_exactly();
    test_scale_edits_retain_only_their_context();
    test_maximum_topology_fail_nth_is_exact_and_atomic();
    std::cout << "All SequencerProjectScaleHistory tests passed.\n";
    return 0;
}
