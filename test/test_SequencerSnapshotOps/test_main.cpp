#include "state/sequencer/SequencerDetachedEditor.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>

#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerClipRegionOps.hpp"
#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"

namespace allocation_trace {

bool enabled = false;
std::size_t count = 0U;

class Scope {
public:
    Scope() {
        count = 0U;
        enabled = true;
    }

    ~Scope() { enabled = false; }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace allocation_trace

void* operator new(std::size_t bytes) {
    if (allocation_trace::enabled) ++allocation_trace::count;
    if (void* memory = std::malloc(bytes)) return memory;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }

void operator delete(void* memory) noexcept { std::free(memory); }

void operator delete[](void* memory) noexcept { ::operator delete(memory); }

void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete[](memory);
}

namespace {

using core::state::sequencer::SequencerState;
using oc::note::sequencer::StepBitMask128;

namespace seq = core::state::sequencer;

uint64_t byteHash(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void setStep(SequencerState& sequencer,
             uint8_t step,
             uint8_t note,
             uint8_t velocity,
             uint16_t gate,
             int8_t nudge,
             uint8_t probability,
             bool enabled) {
    sequencer.pattern().note[step] = note;
    sequencer.pattern().velocity[step] = velocity;
    sequencer.pattern().gate[step] = gate;
    sequencer.pattern().nudge[step] = nudge;
    sequencer.pattern().probability[step] = probability;

    auto mask = sequencer.pattern().enabledMask;
    mask.setBit(step, enabled);
    sequencer.pattern().setEnabledMask(mask);
}

void assertDefaultStep(const SequencerState& sequencer, uint8_t step) {
    assert(sequencer.pattern().note[step] == SequencerState::DEFAULT_NOTE);
    assert(sequencer.pattern().velocity[step] == SequencerState::DEFAULT_VELOCITY);
    assert(sequencer.pattern().gate[step] == SequencerState::DEFAULT_GATE_PERCENT);
    assert(sequencer.pattern().nudge[step] == 0);
    assert(sequencer.pattern().probability[step] == SequencerState::DEFAULT_PROBABILITY);
    assert(!sequencer.pattern().isEnabled(step));
}

void assertStep(const SequencerState& sequencer,
                uint8_t step,
                uint8_t note,
                uint8_t velocity,
                uint16_t gate,
                int8_t nudge,
                uint8_t probability,
                bool enabled) {
    assert(sequencer.pattern().note[step] == note);
    assert(sequencer.pattern().velocity[step] == velocity);
    assert(sequencer.pattern().gate[step] == gate);
    assert(sequencer.pattern().nudge[step] == nudge);
    assert(sequencer.pattern().probability[step] == probability);
    assert(sequencer.pattern().isEnabled(step) == enabled);
}

bool rootStepHasMicroSequence(const SequencerState& sequencer, uint8_t step) {
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern());
    if (graph == nullptr) return false;
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    if (nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE);
}

void createRootMicroSequence(SequencerState& sequencer, uint8_t step, uint8_t length) {
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    const auto result = core::state::sequencer::createMicroSequence(
        sequencer.pattern(),
        nodeId,
        length
    );
    assert(result.ok);
}

seq::SequencerCcLaneBank& createCcLane(SequencerState& sequencer) {
    auto* bank = seq::ensureSequencerCcLaneBank(sequencer.pattern());
    assert(bank != nullptr);
    seq::SequencerCcLaneDraft draft{};
    assert(seq::createSequencerCcLane(*bank, 0U, draft).changed());
    return *bank;
}

void setCcEvent(seq::SequencerCcLaneBank& bank, uint8_t step, uint8_t value) {
    assert(seq::setSequencerCcLaneEvent(bank, 0U, step, value).changed());
}

void assertCcEvent(
    const seq::SequencerCcLaneBank& bank,
    uint8_t step,
    bool active,
    uint8_t value = 0U
) {
    const auto& lane = bank.lanes[0];
    assert(lane.activeMask.test(step) == active);
    assert(lane.values[step] == (active ? value : 0U));
    assert(seq::sequencerCcLaneTransition(lane, step) ==
           seq::SequencerCcLaneTransition::HOLD);
}

void assertBatchRevisions(
    const SequencerState& sequencer,
    uint32_t step,
    uint32_t graph,
    uint32_t cc,
    uint32_t clip,
    uint32_t bank
) {
    assert(sequencer.pattern().stepDataRevision == step);
    assert(sequencer.pattern().graphRevision == graph);
    assert(sequencer.pattern().ccLaneRevision == cc);
    assert(sequencer.clipRevision.get() == clip);
    assert(sequencer.pattern().ccLanes);
    assert(sequencer.pattern().ccLanes->revision == bank);
}

void test_rotate_pattern_moves_payload_and_mask() {
    core::state::sequencer::SequencerDetachedEditor sequencer;
    assert(seq::resizeClipPatternContent(sequencer, 4));
    sequencer.pattern().setEnabledMask(StepBitMask128{});
    setStep(sequencer, 0, 60, 90, 50, 0, 100, true);
    setStep(sequencer, 1, 61, 91, 51, 1, 80, false);

    assert(core::state::sequencer::rotatePatternState(sequencer.pattern(), 1));

    assertStep(sequencer, 1, 60, 90, 50, 0, 100, true);
    assertStep(sequencer, 2, 61, 91, 51, 1, 80, false);
    assert(!sequencer.pattern().isEnabled(0));
    assert(!sequencer.pattern().isEnabled(3));

    std::cout << "[PASS] test_rotate_pattern_moves_payload_and_mask\n";
}

void test_snapshot_apply_clears_graph_payload_but_keeps_revision() {
    core::state::sequencer::SequencerDetachedEditor source;
    const auto sourceNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(source.pattern(), sourceNode, 2).ok);

    core::state::sequencer::SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(source.pattern(), snapshot);
    assert(snapshot.graphRevision == source.pattern().graphRevision);

    core::state::sequencer::SequencerDetachedEditor applied;
    assert(core::state::sequencer::createMicroSequence(applied.pattern(), sourceNode, 2).ok);
    core::state::sequencer::applySnapshot(applied.pattern(), snapshot);
    assert(applied.pattern().graph.get() == nullptr);
    assert(applied.pattern().graphRevision == snapshot.graphRevision);

    std::cout << "[PASS] test_snapshot_apply_clears_graph_payload_but_keeps_revision\n";
}

void test_track_content_installation_consumes_prepared_graphs() {
    core::state::sequencer::SequencerDetachedEditor source;
    setStep(source, 0, 74, 103, 88, -2, 79, true);
    createRootMicroSequence(source, 0, 2);

    core::state::sequencer::SequencerPatternSnapshot snapshot;
    core::state::sequencer::SequencerClipState clipSnapshot;
    core::state::sequencer::captureSnapshot(source.pattern(), snapshot);
    clipSnapshot = source.clip();
    const auto* sourceGraph = core::state::sequencer::graphView(source.pattern());
    assert(sourceGraph != nullptr);

    core::state::sequencer::SequencerDetachedEditor bankTarget;
    auto bankGraph = core::app::makeExtmemUniqueCopy(*sourceGraph);
    assert(bankGraph);
    const auto* bankOwner = bankGraph.get();
    core::state::sequencer::installTrackContentSnapshotWithOwnedGraph(
        bankTarget.pattern(),
        bankTarget.clip(),
        snapshot,
        clipSnapshot,
        std::move(bankGraph)
    );
    assert(bankTarget.pattern().graph.get() == bankOwner);
    assertStep(bankTarget, 0, 74, 103, 88, -2, 79, true);
    assert(rootStepHasMicroSequence(bankTarget, 0));

    core::state::sequencer::SequencerDetachedEditor editorTarget;
    auto editorGraph = core::app::makeExtmemUniqueCopy(*sourceGraph);
    assert(editorGraph);
    const auto* editorOwner = editorGraph.get();
    core::state::sequencer::installTrackContentSnapshotToEditorWithOwnedGraph(
        editorTarget,
        snapshot,
        clipSnapshot,
        std::move(editorGraph)
    );
    assert(editorTarget.pattern().graph.get() == editorOwner);
    assertStep(editorTarget, 0, 74, 103, 88, -2, 79, true);
    assert(rootStepHasMicroSequence(editorTarget, 0));

    std::cout
        << "[PASS] test_track_content_installation_consumes_prepared_graphs\n";
}

void test_full_128_step_snapshot_and_rotation_contract() {
    core::state::sequencer::SequencerDetachedEditor source;
    const bool resized = seq::resizeClipPatternContent(
        source,
        SequencerState::MAX_STEPS
    );
    assert(resized);
    setStep(source, 127U, 96U, 123U, 175U, 11, 42U, true);

    core::state::sequencer::SequencerPatternSnapshot snapshot{};
    core::state::sequencer::captureSnapshot(source.pattern(), snapshot);
    assert(snapshot.length == SequencerState::MAX_STEPS);
    assert(snapshot.enabledMask.test(127U));
    assert(snapshot.note[127] == 96U);

    core::state::sequencer::SequencerDetachedEditor restored;
    core::state::sequencer::applySnapshot(restored.pattern(), snapshot);
    assert(restored.pattern().length == SequencerState::MAX_STEPS);
    assertStep(restored, 127U, 96U, 123U, 175U, 11, 42U, true);

    const bool rotated = core::state::sequencer::rotatePatternState(restored.pattern(), 1);
    assert(rotated);
    assertStep(restored, 0U, 96U, 123U, 175U, 11, 42U, true);
}

void test_batch_invalid_and_no_change_are_failure_atomic() {
    core::state::sequencer::SequencerDetachedEditor sequencer;
    assert(seq::resizeClipPatternContent(sequencer, 16U));
    const uint8_t length = sequencer.pattern().length;
    const auto mask = sequencer.pattern().enabledMask;
    const uint32_t stepRevision = sequencer.pattern().stepDataRevision;
    const uint32_t graphRevision = sequencer.pattern().graphRevision;
    const uint32_t clipRevision = sequencer.clipRevision.get();

    const auto invalidClear = seq::clearSequencerRootStepSpanUnversioned(
        sequencer,
        length,
        1U
    );
    assert(invalidClear.status ==
           seq::SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT);

    const auto noDelete = seq::deleteSequencerRootPagesUnversioned(sequencer, 0U);
    assert(noDelete.status ==
           seq::SequencerSnapshotBatchMutationStatus::NO_CHANGE);

    const auto existingPage = seq::extendSequencerPageRootUnversioned(sequencer, 0U);
    assert(existingPage.status ==
           seq::SequencerSnapshotBatchMutationStatus::NO_CHANGE);

    const auto invalidZeroResize = seq::resizeSequencerRootContentUnversioned(
        sequencer,
        0U
    );
    assert(invalidZeroResize.status ==
           seq::SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT);
    const auto invalidShrink = seq::resizeSequencerRootContentUnversioned(
        sequencer,
        static_cast<uint8_t>(length - 1U)
    );
    assert(invalidShrink.status ==
           seq::SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT);

    assert(sequencer.pattern().length == length);
    assert(sequencer.pattern().enabledMask == mask);
    assert(sequencer.pattern().stepDataRevision == stepRevision);
    assert(sequencer.pattern().graphRevision == graphRevision);
    assert(sequencer.clipRevision.get() == clipRevision);

    auto& invalidBank = createCcLane(sequencer);
    invalidBank.formatVersion = 0U;
    const auto invalidCcDelete = seq::deleteSequencerRootPagesUnversioned(
        sequencer,
        uint16_t{1} << 1U
    );
    assert(invalidCcDelete.status ==
           seq::SequencerSnapshotBatchMutationStatus::INVALID_CC_LANE_BANK);
    assert(sequencer.pattern().length == length);
    assert(sequencer.pattern().enabledMask == mask);

    std::cout << "[PASS] batch invalid/no-change paths are failure-atomic\n";
}

void test_batch_exact_length_extension_contract() {
    core::state::sequencer::SequencerDetachedEditor sequencer;
    const uint8_t oldLength = sequencer.pattern().length;
    const uint8_t requiredLength = static_cast<uint8_t>(oldLength + 3U);
    const uint32_t stepRevision = sequencer.pattern().stepDataRevision;
    const uint32_t clipRevision = sequencer.clipRevision.get();

    const auto result = seq::resizeSequencerRootContentUnversioned(
        sequencer,
        requiredLength
    );
    assert(result.changed());
    assert(result.previousLength == oldLength);
    assert(result.resultingLength == requiredLength);
    assert(result.domains.stepData);
    assert(result.domains.clip);
    assert(!result.domains.graph);
    assert(!result.domains.ccLanes);
    assert(sequencer.pattern().length == requiredLength);
    assert(seq::clipPlaybackRegion(
        sequencer.pattern(),
        sequencer.clip()
    ).loopEnd == requiredLength);
    for (uint16_t step = oldLength; step < requiredLength; ++step) {
        assertDefaultStep(sequencer, static_cast<uint8_t>(step));
    }
    assert(sequencer.pattern().stepDataRevision == stepRevision);
    assert(sequencer.clipRevision.get() == clipRevision);

    seq::publishSequencerSnapshotBatchRevisions(sequencer, result.domains);
    assert(sequencer.pattern().stepDataRevision == stepRevision + 1U);
    assert(sequencer.clipRevision.get() == clipRevision + 1U);

    const auto noChange = seq::resizeSequencerRootContentUnversioned(
        sequencer,
        requiredLength
    );
    assert(noChange.status ==
           seq::SequencerSnapshotBatchMutationStatus::NO_CHANGE);

    std::cout << "[PASS] exact root extension honors length/no-op/revisions\n";
}

void test_batch_malformed_graph_rejects_before_entry_zero() {
    core::state::sequencer::SequencerDetachedEditor sequencer;
    assert(seq::resizeClipPatternContent(sequencer, 16U));
    setStep(sequencer, 8U, 91U, 108U, 83U, -7, 39U, true);
    createRootMicroSequence(sequencer, 0U, 2U);
    auto& bank = createCcLane(sequencer);
    setCcEvent(bank, 8U, 97U);

    auto* graph = sequencer.pattern().graph.get();
    assert(graph != nullptr);
    graph->stepNodes[0].childSequenceId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;

    const uint64_t graphHash = byteHash(graph, sizeof(*graph));
    const uint64_t ccHash = byteHash(&bank, sizeof(bank));
    const uint64_t noteHash = byteHash(
        sequencer.pattern().note.data(),
        sizeof(sequencer.pattern().note)
    );
    const uint64_t velocityHash = byteHash(
        sequencer.pattern().velocity.data(),
        sizeof(sequencer.pattern().velocity)
    );
    const uint64_t gateHash = byteHash(
        sequencer.pattern().gate.data(),
        sizeof(sequencer.pattern().gate)
    );
    const uint64_t nudgeHash = byteHash(
        sequencer.pattern().nudge.data(),
        sizeof(sequencer.pattern().nudge)
    );
    const uint64_t probabilityHash = byteHash(
        sequencer.pattern().probability.data(),
        sizeof(sequencer.pattern().probability)
    );
    const auto enabledMask = sequencer.pattern().enabledMask;
    const auto region = seq::clipPlaybackRegion(
        sequencer.pattern(),
        sequencer.clip()
    );
    const uint8_t note = sequencer.pattern().note[8];
    const uint8_t velocity = sequencer.pattern().velocity[8];
    const uint16_t gate = sequencer.pattern().gate[8];
    const int8_t nudge = sequencer.pattern().nudge[8];
    const uint8_t probability = sequencer.pattern().probability[8];
    const uint32_t stepRevision = sequencer.pattern().stepDataRevision;
    const uint32_t graphRevision = sequencer.pattern().graphRevision;
    const uint32_t ccRevision = sequencer.pattern().ccLaneRevision;
    const uint32_t clipRevision = sequencer.clipRevision.get();
    const uint32_t bankRevision = bank.revision;

    const auto result = seq::deleteSequencerRootPagesUnversioned(
        sequencer,
        uint16_t{1} << 1U
    );
    assert(result.status ==
           seq::SequencerSnapshotBatchMutationStatus::INVALID_GRAPH);
    assert(byteHash(graph, sizeof(*graph)) == graphHash);
    assert(byteHash(&bank, sizeof(bank)) == ccHash);
    assert(byteHash(sequencer.pattern().note.data(), sizeof(sequencer.pattern().note)) ==
           noteHash);
    assert(byteHash(
               sequencer.pattern().velocity.data(),
               sizeof(sequencer.pattern().velocity)
           ) == velocityHash);
    assert(byteHash(sequencer.pattern().gate.data(), sizeof(sequencer.pattern().gate)) ==
           gateHash);
    assert(byteHash(
               sequencer.pattern().nudge.data(),
               sizeof(sequencer.pattern().nudge)
           ) == nudgeHash);
    assert(byteHash(
               sequencer.pattern().probability.data(),
               sizeof(sequencer.pattern().probability)
           ) == probabilityHash);
    assert(sequencer.pattern().enabledMask == enabledMask);
    assert(seq::clipPlaybackRegion(sequencer.pattern(), sequencer.clip()).contentLength ==
           region.contentLength);
    assert(seq::clipPlaybackRegion(sequencer.pattern(), sequencer.clip()).playStart ==
           region.playStart);
    assert(seq::clipPlaybackRegion(sequencer.pattern(), sequencer.clip()).loopStart ==
           region.loopStart);
    assert(seq::clipPlaybackRegion(sequencer.pattern(), sequencer.clip()).loopEnd ==
           region.loopEnd);
    assert(sequencer.pattern().note[8] == note);
    assert(sequencer.pattern().velocity[8] == velocity);
    assert(sequencer.pattern().gate[8] == gate);
    assert(sequencer.pattern().nudge[8] == nudge);
    assert(sequencer.pattern().probability[8] == probability);
    assertBatchRevisions(
        sequencer,
        stepRevision,
        graphRevision,
        ccRevision,
        clipRevision,
        bankRevision
    );

    std::cout << "[PASS] malformed Graph rejects before entry0 with exact identity\n";
}

void test_batch_page_extension_is_unversioned_and_allocation_free() {
    core::state::sequencer::SequencerDetachedEditor sequencer;
    assert(sequencer.pattern().length == 8U);
    setStep(sequencer, 12U, 93U, 101U, 77U, -6, 44U, false);
    createRootMicroSequence(sequencer, 12U, 2U);
    auto& bank = createCcLane(sequencer);
    setCcEvent(bank, 12U, 88U);

    const uint32_t stepRevision = sequencer.pattern().stepDataRevision;
    const uint32_t graphRevision = sequencer.pattern().graphRevision;
    const uint32_t ccRevision = sequencer.pattern().ccLaneRevision;
    const uint32_t clipRevision = sequencer.clipRevision.get();
    const uint32_t bankRevision = bank.revision;

    seq::SequencerSnapshotBatchMutationResult result{};
    {
        allocation_trace::Scope allocations;
        result = seq::extendSequencerPageRootUnversioned(sequencer, 1U);
        assert(allocation_trace::count == 0U);
    }
    assert(result.changed());
    assert(result.previousLength == 8U);
    assert(result.resultingLength == 16U);
    assert(result.domains.stepData);
    assert(result.domains.graph);
    assert(!result.domains.ccLanes);
    assert(result.domains.clip);
    assert(sequencer.pattern().length == 16U);
    assertDefaultStep(sequencer, 12U);
    assert(!rootStepHasMicroSequence(sequencer, 12U));
    assertCcEvent(bank, 12U, true, 88U);
    assertBatchRevisions(
        sequencer,
        stepRevision,
        graphRevision,
        ccRevision,
        clipRevision,
        bankRevision
    );

    seq::publishSequencerSnapshotBatchRevisions(sequencer, result.domains);
    assertBatchRevisions(
        sequencer,
        stepRevision + 1U,
        graphRevision + 1U,
        ccRevision,
        clipRevision + 1U,
        bankRevision
    );

    std::cout << "[PASS] batch Page extension is unversioned/allocation-free\n";
}

void test_batch_clear_preserves_cc_and_publishes_exact_domains() {
    core::state::sequencer::SequencerDetachedEditor sequencer;
    assert(seq::resizeClipPatternContent(sequencer, 16U));
    setStep(sequencer, 8U, 81U, 99U, 68U, 3, 72U, true);
    createRootMicroSequence(sequencer, 8U, 2U);
    auto& bank = createCcLane(sequencer);
    setCcEvent(bank, 8U, 86U);
    const seq::SequencerCcLaneBank bankBefore = bank;

    const uint32_t stepRevision = sequencer.pattern().stepDataRevision;
    const uint32_t graphRevision = sequencer.pattern().graphRevision;
    const uint32_t ccRevision = sequencer.pattern().ccLaneRevision;
    const uint32_t clipRevision = sequencer.clipRevision.get();
    const uint32_t bankRevision = bank.revision;

    seq::SequencerSnapshotBatchMutationResult result{};
    {
        allocation_trace::Scope allocations;
        result = seq::clearSequencerRootStepSpanUnversioned(sequencer, 8U, 8U);
        assert(allocation_trace::count == 0U);
    }
    assert(result.changed());
    assert(result.domains.stepData);
    assert(result.domains.graph);
    assert(!result.domains.ccLanes);
    assert(!result.domains.clip);
    assertDefaultStep(sequencer, 8U);
    assert(!rootStepHasMicroSequence(sequencer, 8U));
    assert(seq::sameSequencerCcLaneBankMusicalData(bank, bankBefore));
    assert(bank.revision == bankBefore.revision);
    assertBatchRevisions(
        sequencer,
        stepRevision,
        graphRevision,
        ccRevision,
        clipRevision,
        bankRevision
    );

    seq::publishSequencerSnapshotBatchRevisions(sequencer, result.domains);
    assertBatchRevisions(
        sequencer,
        stepRevision + 1U,
        graphRevision + 1U,
        ccRevision,
        clipRevision,
        bankRevision
    );

    const auto noChange = seq::clearSequencerRootStepSpanUnversioned(
        sequencer,
        8U,
        8U
    );
    assert(noChange.status ==
           seq::SequencerSnapshotBatchMutationStatus::NO_CHANGE);

    std::cout << "[PASS] batch clear preserves CC and exact revisions\n";
}

void test_batch_sparse_page_delete_shifts_all_domains_without_compaction() {
    core::state::sequencer::SequencerDetachedEditor sequencer;
    assert(seq::resizeClipPatternContent(sequencer, 40U));
    for (uint8_t page = 0U; page < 5U; ++page) {
        const uint8_t step = static_cast<uint8_t>(page * SequencerState::STEPS_PER_PAGE);
        setStep(
            sequencer,
            step,
            static_cast<uint8_t>(60U + page),
            static_cast<uint8_t>(90U + page),
            static_cast<uint16_t>(70U + page),
            static_cast<int8_t>(page),
            static_cast<uint8_t>(80U + page),
            true
        );
    }
    createRootMicroSequence(sequencer, 8U, 2U);
    createRootMicroSequence(sequencer, 16U, 2U);
    createRootMicroSequence(sequencer, 32U, 2U);
    auto& bank = createCcLane(sequencer);
    setCcEvent(bank, 0U, 70U);
    setCcEvent(bank, 8U, 71U);
    setCcEvent(bank, 16U, 72U);
    setCcEvent(bank, 24U, 73U);
    setCcEvent(bank, 32U, 74U);
    setCcEvent(bank, 100U, 99U);
    assert(seq::setSequencerCcLaneTransition(
        bank,
        0U,
        100U,
        seq::SequencerCcLaneTransition::EASE_IN_OUT
    ).changed());
    assert(seq::setClipPlaybackRegion(
        sequencer,
        {.contentLength = 40U, .playStart = 9U, .loopStart = 17U, .loopEnd = 39U}
    ));

    const auto* graphBefore = seq::graphView(sequencer.pattern());
    assert(graphBefore != nullptr);
    const uint16_t graphNodeCount = graphBefore->stepNodeCount;
    const uint8_t graphSequenceCount = graphBefore->sequenceCount;
    const uint8_t graphCycleSetCount = graphBefore->cycleSetCount;
    const uint32_t stepRevision = sequencer.pattern().stepDataRevision;
    const uint32_t graphRevision = sequencer.pattern().graphRevision;
    const uint32_t ccRevision = sequencer.pattern().ccLaneRevision;
    const uint32_t clipRevision = sequencer.clipRevision.get();
    const uint32_t bankRevision = bank.revision;

    seq::SequencerSnapshotBatchMutationResult result{};
    {
        allocation_trace::Scope allocations;
        result = seq::deleteSequencerRootPagesUnversioned(
            sequencer,
            static_cast<uint16_t>((uint16_t{1} << 1U) | (uint16_t{1} << 3U))
        );
        assert(allocation_trace::count == 0U);
    }
    assert(result.changed());
    assert(result.previousLength == 40U);
    assert(result.resultingLength == 24U);
    assert(result.domains.stepData);
    assert(result.domains.graph);
    assert(result.domains.ccLanes);
    assert(result.domains.clip);

    assertStep(sequencer, 0U, 60U, 90U, 70U, 0, 80U, true);
    assertStep(sequencer, 8U, 62U, 92U, 72U, 2, 82U, true);
    assertStep(sequencer, 16U, 64U, 94U, 74U, 4, 84U, true);
    assertDefaultStep(sequencer, 24U);
    assert(rootStepHasMicroSequence(sequencer, 8U));
    assert(rootStepHasMicroSequence(sequencer, 16U));
    assert(!rootStepHasMicroSequence(sequencer, 24U));
    assertCcEvent(bank, 0U, true, 70U);
    assertCcEvent(bank, 8U, true, 72U);
    assertCcEvent(bank, 16U, true, 74U);
    assertCcEvent(bank, 24U, false);
    assert(bank.lanes[0].activeMask.test(100U));
    assert(bank.lanes[0].values[100U] == 99U);
    assert(seq::sequencerCcLaneTransition(bank.lanes[0], 100U) ==
           seq::SequencerCcLaneTransition::EASE_IN_OUT);
    const auto region = seq::clipPlaybackRegion(
        sequencer.pattern(),
        sequencer.clip()
    );
    assert(region.contentLength == 24U);
    assert(region.playStart == 8U);
    assert(region.loopStart == 9U);
    assert(region.loopEnd == 23U);

    const auto* graphAfter = seq::graphView(sequencer.pattern());
    assert(graphAfter != nullptr);
    assert(graphAfter->stepNodeCount == graphNodeCount);
    assert(graphAfter->sequenceCount == graphSequenceCount);
    assert(graphAfter->cycleSetCount == graphCycleSetCount);
    assertBatchRevisions(
        sequencer,
        stepRevision,
        graphRevision,
        ccRevision,
        clipRevision,
        bankRevision
    );

    seq::publishSequencerSnapshotBatchRevisions(sequencer, result.domains);
    assertBatchRevisions(
        sequencer,
        stepRevision + 1U,
        graphRevision + 1U,
        ccRevision + 1U,
        clipRevision + 1U,
        bankRevision + 1U
    );

    std::cout << "[PASS] sparse Page delete shifts all domains without compaction\n";
}

}  // namespace

int main() {
    test_rotate_pattern_moves_payload_and_mask();
    test_snapshot_apply_clears_graph_payload_but_keeps_revision();
    test_track_content_installation_consumes_prepared_graphs();
    test_full_128_step_snapshot_and_rotation_contract();
    test_batch_invalid_and_no_change_are_failure_atomic();
    test_batch_exact_length_extension_contract();
    test_batch_malformed_graph_rejects_before_entry_zero();
    test_batch_page_extension_is_unversioned_and_allocation_free();
    test_batch_clear_preserves_cc_and_publishes_exact_domains();
    test_batch_sparse_page_delete_shifts_all_domains_without_compaction();

    std::cout << "All SequencerSnapshotOps tests passed\n";
    return 0;
}
