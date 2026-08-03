#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <utility>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerHistory.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerPatternRegionOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
#include "../../src/state/sequencer/SequencerStructureHistory.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"

namespace {

using core::state::sequencer::SequencerHistoryPatternSnapshot;
using core::state::sequencer::SequencerHistoryScope;
using core::state::sequencer::SequencerHistoryService;
using core::state::sequencer::SequencerHistoryTrackBankSnapshot;
using core::state::sequencer::SequencerHistoryTrackStructureChange;
using core::state::sequencer::SequencerHistoryActionKind;
using core::state::sequencer::SequencerHistoryDescriptor;
using core::state::sequencer::SequencerPatternState;
using core::state::sequencer::SequencerState;
using core::state::sequencer::SequencerTrackBankState;
using core::state::sequencer::StepProperty;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;

void setStep(SequencerPatternState& pattern, uint8_t step, uint8_t note) {
    pattern.setStepDataAt(step, note, 96, SequencerPatternState::DEFAULT_GATE_PERCENT);
    pattern.setEnabled(step, true);
}

uint64_t byteHash(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void assertActiveDraftBlocksDirectHistory(
    SequencerHistoryService& history,
    SequencerTrackBankState& bank,
    SequencerState& active,
    bool redo
) {
    namespace seq = core::state::sequencer;

    assert(seq::beginStepContentDraft(
        active,
        seq::SequencerStepContentDraftKind::CHORD,
        0U,
        seq::rootStepNodeId(0U)
    ));
    const uint32_t draftRevision = active.stepContentDraft.revision.get();
    const uint32_t contentRevision = active.contentView.revision.get();
    const uint8_t undoCount = history.undoCount();
    const uint8_t redoCount = history.redoCount();
    const uintptr_t undoIdentity = history.projectHistoryUndoIdentity();
    const uintptr_t redoIdentity = history.projectHistoryRedoIdentity();
    const uint8_t note = active.pattern.note[0];
    const uint8_t focusedStep = active.focusedStep.get();
    const uint8_t page = active.page.get();
    const StepProperty activeStepProperty = active.activeStepProperty.get();
    const uint16_t enabledMask = bank.currentEnabledMask();
    const uint8_t activeTrack = bank.activeTrackIndex();

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        const auto result = redo ? history.redoWithResult(bank, active)
                                 : history.undoWithResult(bank, active);
        assert(!result.applied);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
#else
    const auto result = redo ? history.redoWithResult(bank, active)
                             : history.undoWithResult(bank, active);
    assert(!result.applied);
#endif

    assert(history.undoCount() == undoCount);
    assert(history.redoCount() == redoCount);
    assert(history.projectHistoryUndoIdentity() == undoIdentity);
    assert(history.projectHistoryRedoIdentity() == redoIdentity);
    assert(active.pattern.note[0] == note);
    assert(active.focusedStep.get() == focusedStep);
    assert(active.page.get() == page);
    assert(active.activeStepProperty.get() == activeStepProperty);
    assert(bank.currentEnabledMask() == enabledMask);
    assert(bank.activeTrackIndex() == activeTrack);
    assert(active.stepContentDraft.failure ==
           seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
    assert(active.stepContentDraft.blockedTransition ==
           seq::SequencerStepContentDraftBlockedTransition::HISTORY);
    assert(active.stepContentDraft.revision.get() == draftRevision + 1U);
    assert(active.contentView.revision.get() == contentRevision + 1U);

    const uint32_t blockedRevision = active.stepContentDraft.revision.get();
    const uint32_t blockedContentRevision = active.contentView.revision.get();
    const auto repeated = redo ? history.redoWithResult(bank, active)
                               : history.undoWithResult(bank, active);
    assert(!repeated.applied);
    assert(active.stepContentDraft.revision.get() == blockedRevision);
    assert(active.contentView.revision.get() == blockedContentRevision);
    assert(history.undoCount() == undoCount);
    assert(history.redoCount() == redoCount);

    seq::abandonStepContentDraft(active);
}

core::state::sequencer::SequencerHistoryTrackStructureChangePtr
makeGraphHeavyStructureHistoryChange() {
    auto change = core::app::makeExtmemUnique<SequencerHistoryTrackStructureChange>();
    assert(change);

    change->before.enabledMask = 0x0001;
    change->after.enabledMask = 0x0003;
    change->before.capturedTrackMask = 0xFFFF;
    change->after.capturedTrackMask = 0xFFFF;
    change->descriptor.kind = SequencerHistoryActionKind::TrackStructure;

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        change->before.tracks[i].graph =
            core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>();
        change->after.tracks[i].graph =
            core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>();
        assert(change->before.tracks[i].graph);
        assert(change->after.tracks[i].graph);
    }

    return change;
}

bool hasMicroSequence(const SequencerPatternState& pattern, uint8_t step) {
    const auto* graph = core::state::sequencer::graphView(pattern);
    if (graph == nullptr) return false;
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    if (nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(STEP_NODE_CHILD_SEQUENCE);
}

bool hasCycleStateSet(const SequencerPatternState& pattern, uint8_t step) {
    const auto* graph = core::state::sequencer::graphView(pattern);
    if (graph == nullptr) return false;
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    if (nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(STEP_NODE_CYCLE_SET);
}

bool hasNestedCycleStateSetWithOffset(
    const SequencerPatternState& pattern,
    uint8_t step,
    uint8_t stateIndex,
    int8_t offset
) {
    const auto* graph = core::state::sequencer::graphView(pattern);
    if (graph == nullptr) return false;
    const auto rootNodeId = core::state::sequencer::rootStepNodeId(step);
    const auto* rootNode = graph->stepNode(rootNodeId);
    if (rootNode == nullptr || !rootNode->has(STEP_NODE_CYCLE_SET)) return false;

    const auto* cycleSet = graph->cycleSet(rootNode->cycleSetId);
    if (cycleSet == nullptr || stateIndex >= cycleSet->length) return false;

    const auto stateNodeId = static_cast<uint16_t>(cycleSet->firstStateNode + stateIndex);
    const auto* stateNode = graph->stepNode(stateNodeId);
    if (stateNode == nullptr || !stateNode->has(STEP_NODE_CYCLE_SET)) return false;

    const auto* nestedCycleSet = graph->cycleSet(stateNode->cycleSetId);
    if (nestedCycleSet == nullptr || nestedCycleSet->length < 2U) return false;

    const auto nestedStateNodeId = static_cast<uint16_t>(nestedCycleSet->firstStateNode + 1U);
    const auto* nestedStateNode = graph->stepNode(nestedStateNodeId);
    return nestedStateNode != nullptr &&
           nestedStateNode->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET) &&
           nestedStateNode->noteOffset == offset;
}

void test_pattern_snapshot_can_capture_inactive_track() {
    SequencerTrackBankState bank;
    SequencerState active;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, active));
    bank.syncSharedTrackState(0x0003, 0);

    setStep(active.pattern, 0, 60);
    active.pattern.velocity[0] = 72;
    assert(core::state::sequencer::storeActiveTrack(bank, active));
    assert(core::state::sequencer::switchActiveTrack(bank, active, 1));

    setStep(active.pattern, 0, 84);
    active.pattern.velocity[0] = 99;
    bank.track(0).velocity[0] = 77;

    SequencerHistoryPatternSnapshot snapshot;
    assert(core::state::sequencer::captureHistorySnapshot(bank, active, 0, snapshot));
    assert(snapshot.flat.note[0] == 60);
    assert(snapshot.flat.velocity[0] == 77);

    std::cout << "[PASS] test_pattern_snapshot_can_capture_inactive_track\n";
}

void test_pattern_history_undo_redo_restores_flat_data_and_focus() {
    SequencerTrackBankState bank;
    SequencerState state;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, state));

    state.pattern.setContentLength(16);
    setStep(state.pattern, 0, 60);
    state.focusedStep.set(0);
    state.page.set(0);

    SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(state, before));

    setStep(state.pattern, 0, 72);
    state.focusedStep.set(9);
    state.page.set(1);

    SequencerHistoryPatternSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(state, after));

    SequencerHistoryService history;
    assert(history.recordPattern(std::move(before), std::move(after)));
    assert(history.undoCount() == 1);

    assertActiveDraftBlocksDirectHistory(history, bank, state, false);
    assert(history.undo(bank, state));
    assert(state.pattern.note[0] == 60);
    assert(state.focusedStep.get() == 0);
    assert(bank.track(bank.activeTrackIndex()).note[0] == 60);

    assertActiveDraftBlocksDirectHistory(history, bank, state, true);
    assert(history.redo(bank, state));
    assert(state.pattern.note[0] == 72);
    assert(state.focusedStep.get() == 9);
    assert(state.page.get() == 1);
    assert(bank.track(bank.activeTrackIndex()).note[0] == 72);

    std::cout << "[PASS] test_pattern_history_undo_redo_restores_flat_data_and_focus\n";
}

void test_flat_pattern_history_restores_region_only_edit() {
    SequencerTrackBankState bank;
    SequencerState state;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, state));

    SequencerHistoryPatternSnapshot before;
    core::state::sequencer::captureFlatHistorySnapshot(state, before);
    assert(core::state::sequencer::setPatternPlaybackRegion(
        state.pattern,
        {8, 1, 2, 6}
    ));
    SequencerHistoryPatternSnapshot after;
    core::state::sequencer::captureFlatHistorySnapshot(state, after);

    SequencerHistoryService history;
    assert(history.recordFlatPattern(std::move(before), std::move(after)));
    assert(history.undo(bank, state));
    auto region = core::state::sequencer::patternPlaybackRegion(state.pattern);
    assert(region.contentLength == 8);
    assert(region.playStart == 0);
    assert(region.loopStart == 0);
    assert(region.loopEnd == 8);

    assert(history.redo(bank, state));
    region = core::state::sequencer::patternPlaybackRegion(state.pattern);
    assert(region.contentLength == 8);
    assert(region.playStart == 1);
    assert(region.loopStart == 2);
    assert(region.loopEnd == 6);

    std::cout << "[PASS] test_flat_pattern_history_restores_region_only_edit\n";
}

void test_pattern_history_restores_graph_payload() {
    SequencerTrackBankState bank;
    SequencerState state;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, state));

    SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(state, before));

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        3
    );
    assert(sequence.ok);
    assert(hasMicroSequence(state.pattern, 0));
    auto chord = oc::note::sequencer::StepSequencerChordSpec::semantic(
        oc::note::sequencer::StepSequencerChordHarmony::Custom,
        8U,
        oc::note::sequencer::StepSequencerChordVoicing::Open,
        1U,
        oc::note::sequencer::StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    constexpr std::array<uint8_t, 8> intervals{
        0U, 3U, 5U, 8U, 12U, 17U, 24U, 31U,
    };
    for (uint8_t voice = 7U; voice > 0U; --voice) {
        chord.setCustomInterval(voice, intervals[voice]);
    }
    assert(core::state::sequencer::setNodeChordSpec(
        state.pattern,
        rootNode,
        chord
    ));

    SequencerHistoryPatternSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(state, after));

    SequencerHistoryService history;
    assert(history.recordPattern(std::move(before), std::move(after)));

    assert(history.undo(bank, state));
    assert(core::state::sequencer::graphView(state.pattern) == nullptr);
    assert(!hasMicroSequence(bank.track(bank.activeTrackIndex()), 0));

    assert(history.redo(bank, state));
    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    assert(hasMicroSequence(state.pattern, 0));
    const auto childSequenceId = graph->stepNodes[rootNode].childSequenceId;
    const auto* child = graph->sequence(childSequenceId);
    assert(child != nullptr);
    assert(child->length == 3);
    assert(oc::note::sequencer::chordSpecsEqual(
        graph->stepNodes[rootNode].chordSpec,
        chord
    ));
    assert(hasMicroSequence(bank.track(bank.activeTrackIndex()), 0));

    std::cout << "[PASS] test_pattern_history_restores_graph_payload\n";
}

void test_flat_pattern_history_preserves_graph_payload() {
    SequencerTrackBankState bank;
    SequencerState state;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, state));

    state.pattern.setContentLength(8);
    state.focusedStep.set(3);
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        3
    );
    assert(sequence.ok);
    assert(core::state::sequencer::storeActiveTrack(bank, state));

    const auto* editorGraph = core::state::sequencer::graphView(state.pattern);
    const auto* bankGraph = core::state::sequencer::graphView(bank.track(0));
    assert(editorGraph != nullptr);
    assert(bankGraph != nullptr);

    SequencerHistoryPatternSnapshot before;
    core::state::sequencer::captureFlatHistorySnapshot(state, before);
    state.pattern.setEnabled(0, true);
    state.focusedStep.set(0);
    SequencerHistoryPatternSnapshot after;
    core::state::sequencer::captureFlatHistorySnapshot(state, after);

    SequencerHistoryService history;
    assert(history.recordFlatPattern(std::move(before), std::move(after)));

    assert(history.undo(bank, state));
    assert(!state.pattern.isEnabled(0));
    assert(state.focusedStep.get() == 3);
    assert(core::state::sequencer::graphView(state.pattern) == editorGraph);
    assert(core::state::sequencer::graphView(bank.track(0)) == bankGraph);
    assert(hasMicroSequence(state.pattern, 0));
    assert(hasMicroSequence(bank.track(0), 0));

    assert(history.redo(bank, state));
    assert(state.pattern.isEnabled(0));
    assert(state.focusedStep.get() == 0);
    assert(core::state::sequencer::graphView(state.pattern) == editorGraph);
    assert(core::state::sequencer::graphView(bank.track(0)) == bankGraph);
    assert(hasMicroSequence(state.pattern, 0));
    assert(hasMicroSequence(bank.track(0), 0));

    std::cout << "[PASS] test_flat_pattern_history_preserves_graph_payload\n";
}

size_t recordFlatPatternWithOptionalCcLaneAndVerifyPreservation(bool withCcLane) {
    SequencerTrackBankState bank;
    SequencerState state;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, state));
    assert(core::state::sequencer::ensureGraphRoot(state.pattern));
    if (state.pattern.length.get() != 8U) {
        assert(state.pattern.setContentLength(8U));
    }

    if (withCcLane) {
        auto* ccLanes = core::state::sequencer::ensureSequencerCcLaneBank(state.pattern);
        assert(ccLanes != nullptr);
        core::state::sequencer::SequencerCcLaneDraft draft{};
        draft.destination.controller = 74U;
        assert(core::state::sequencer::createSequencerCcLane(
            *ccLanes,
            0U,
            draft
        ).changed());
        assert(core::state::sequencer::setSequencerCcLaneEvent(
            *ccLanes,
            0U,
            3U,
            91U
        ).changed());
        assert(core::state::sequencer::setSequencerCcLaneEvent(
            *ccLanes,
            0U,
            100U,
            37U
        ).changed());
    }
    assert(core::state::sequencer::storeActiveTrack(bank, state));

    const auto* editorGraph = core::state::sequencer::graphView(state.pattern);
    const auto* bankGraph = core::state::sequencer::graphView(bank.track(0U));
    const auto* editorCcLanes =
        core::state::sequencer::sequencerCcLaneView(state.pattern);
    const auto* bankCcLanes =
        core::state::sequencer::sequencerCcLaneView(bank.track(0U));
    assert(editorGraph != nullptr && bankGraph != nullptr);
    assert((editorCcLanes != nullptr) == withCcLane);
    assert((bankCcLanes != nullptr) == withCcLane);
    const uint64_t editorCcHash = withCcLane
        ? byteHash(editorCcLanes, sizeof(*editorCcLanes))
        : 0U;
    const uint64_t bankCcHash = withCcLane
        ? byteHash(bankCcLanes, sizeof(*bankCcLanes))
        : 0U;
    const uint32_t editorCcRevision = state.pattern.ccLaneRevision.get();
    const uint32_t bankCcRevision = bank.track(0U).ccLaneRevision.get();

    // This reproduces the central Step coalescer shape: a complete `before`
    // snapshot followed by a FlatOnly `after` snapshot.
    SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(state, before));
    assert(state.pattern.length.get() == 8U);
    assert(state.pattern.setContentLength(16U));
    state.pattern.setEnabled(0U, true);
    SequencerHistoryPatternSnapshot after;
    core::state::sequencer::captureFlatHistorySnapshot(state, after);

    SequencerHistoryService history;
    assert(history.recordFlatPattern(std::move(before), std::move(after)));
    const size_t retainedBytes = history.retainedBytes();
    assert(retainedBytes > 0U);

    assert(history.undo(bank, state));
    assert(state.pattern.length.get() == 8U);
    assert(!state.pattern.isEnabled(0U));
    assert(core::state::sequencer::graphView(state.pattern) == editorGraph);
    assert(core::state::sequencer::graphView(bank.track(0U)) == bankGraph);
    assert(core::state::sequencer::sequencerCcLaneView(state.pattern) ==
           editorCcLanes);
    assert(core::state::sequencer::sequencerCcLaneView(bank.track(0U)) ==
           bankCcLanes);
    assert(state.pattern.ccLaneRevision.get() == editorCcRevision);
    assert(bank.track(0U).ccLaneRevision.get() == bankCcRevision);
    if (withCcLane) {
        assert(byteHash(editorCcLanes, sizeof(*editorCcLanes)) == editorCcHash);
        assert(byteHash(bankCcLanes, sizeof(*bankCcLanes)) == bankCcHash);
    }

    assert(history.redo(bank, state));
    assert(state.pattern.length.get() == 16U);
    assert(state.pattern.isEnabled(0U));
    assert(core::state::sequencer::graphView(state.pattern) == editorGraph);
    assert(core::state::sequencer::graphView(bank.track(0U)) == bankGraph);
    assert(core::state::sequencer::sequencerCcLaneView(state.pattern) ==
           editorCcLanes);
    assert(core::state::sequencer::sequencerCcLaneView(bank.track(0U)) ==
           bankCcLanes);
    if (withCcLane) {
        assert(editorCcLanes->lanes[0].occupied);
        assert(editorCcLanes->lanes[0].activeMask.test(3U));
        assert(editorCcLanes->lanes[0].values[3U] == 91U);
        assert(editorCcLanes->lanes[0].activeMask.test(100U));
        assert(editorCcLanes->lanes[0].values[100U] == 37U);
        assert(bankCcLanes->lanes[0].occupied);
        assert(bankCcLanes->lanes[0].activeMask.test(3U));
        assert(bankCcLanes->lanes[0].values[3U] == 91U);
        assert(bankCcLanes->lanes[0].activeMask.test(100U));
        assert(bankCcLanes->lanes[0].values[100U] == 37U);
        assert(byteHash(editorCcLanes, sizeof(*editorCcLanes)) == editorCcHash);
        assert(byteHash(bankCcLanes, sizeof(*bankCcLanes)) == bankCcHash);
    }

    return retainedBytes;
}

void test_flat_pattern_history_discards_unretained_payloads_and_preserves_live_cc() {
    const size_t withoutCc =
        recordFlatPatternWithOptionalCcLaneAndVerifyPreservation(false);
    const size_t withCc =
        recordFlatPatternWithOptionalCcLaneAndVerifyPreservation(true);

    // retainedBytes() measures actual owners. Equal costs prove that neither
    // the complete graph nor the CC bank captured in `before` leaked into the
    // normalized FlatOnly entry.
    assert(withCc == withoutCc);

    std::cout
        << "[PASS] test_flat_pattern_history_discards_unretained_payloads_and_preserves_live_cc\n";
}

void test_flat_pattern_history_rejects_graph_revision_change() {
    SequencerState state;
    SequencerHistoryPatternSnapshot before;
    core::state::sequencer::captureFlatHistorySnapshot(state, before);

    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        core::state::sequencer::rootStepNodeId(0),
        2
    );
    assert(sequence.ok);

    SequencerHistoryPatternSnapshot after;
    core::state::sequencer::captureFlatHistorySnapshot(state, after);
    SequencerHistoryService history;
    assert(!history.recordFlatPattern(std::move(before), std::move(after)));
    assert(!history.canUndo());

    std::cout << "[PASS] test_flat_pattern_history_rejects_graph_revision_change\n";
}

void test_pattern_noop_ignores_unused_graph_capacity() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        3
    );
    assert(sequence.ok);

    SequencerHistoryPatternSnapshot before;
    SequencerHistoryPatternSnapshot after;
    const bool capturedBefore = core::state::sequencer::captureHistorySnapshot(state, before);
    assert(capturedBefore);
    const bool capturedAfter = core::state::sequencer::captureHistorySnapshot(state, after);
    assert(capturedAfter);
    assert(after.graph != nullptr);
    if (!capturedBefore || !capturedAfter || after.graph == nullptr) {
        std::abort();
    }

    auto& graph = *after.graph;
    assert(graph.stepNodeCount < graph.stepNodes.size());
    graph.stepNodes[graph.stepNodeCount].flags = STEP_NODE_NOTE_OFFSET;
    graph.stepNodes[graph.stepNodeCount].noteOffset = 7;

    assert(graph.sequenceCount < graph.sequences.size());
    graph.sequences[graph.sequenceCount].length = 8;
    graph.sequences[graph.sequenceCount].offset = 2;

    assert(graph.cycleSetCount < graph.cycleSets.size());
    graph.cycleSets[graph.cycleSetCount].length = 4;
    graph.cycleSets[graph.cycleSetCount].offset = 1;

    SequencerHistoryService history;
    assert(!history.recordPattern(std::move(before), std::move(after)));
    assert(!history.canUndo());

    std::cout << "[PASS] test_pattern_noop_ignores_unused_graph_capacity\n";
}

void test_pattern_noop_ignores_focus_only_change() {
    SequencerState state;
    SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(state, before));

    state.focusedStep.set(5);
    state.page.set(0);

    SequencerHistoryPatternSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(state, after));

    SequencerHistoryService history;
    assert(!history.recordPattern(std::move(before), std::move(after)));
    assert(!history.canUndo());

    std::cout << "[PASS] test_pattern_noop_ignores_focus_only_change\n";
}

void test_redo_clears_after_new_record() {
    SequencerTrackBankState bank;
    SequencerState state;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, state));

    SequencerHistoryService history;
    SequencerHistoryPatternSnapshot firstBefore;
    assert(core::state::sequencer::captureHistorySnapshot(state, firstBefore));
    setStep(state.pattern, 0, 61);
    SequencerHistoryPatternSnapshot firstAfter;
    assert(core::state::sequencer::captureHistorySnapshot(state, firstAfter));
    assert(history.recordPattern(std::move(firstBefore), std::move(firstAfter)));

    assert(history.undo(bank, state));
    assert(history.canRedo());

    SequencerHistoryPatternSnapshot secondBefore;
    assert(core::state::sequencer::captureHistorySnapshot(state, secondBefore));
    setStep(state.pattern, 0, 62);
    SequencerHistoryPatternSnapshot secondAfter;
    assert(core::state::sequencer::captureHistorySnapshot(state, secondAfter));
    assert(history.recordPattern(std::move(secondBefore), std::move(secondAfter)));

    assert(!history.canRedo());
    assert(history.undoCount() == 1);

    std::cout << "[PASS] test_redo_clears_after_new_record\n";
}

void test_pattern_history_undoes_previous_track_without_switching_active_track() {
    SequencerTrackBankState bank;
    SequencerState active;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, active));
    bank.syncSharedTrackState(0x0003, 0);

    setStep(active.pattern, 0, 60);
    SequencerHistoryPatternSnapshot track0Before;
    assert(core::state::sequencer::captureHistorySnapshot(active, track0Before));
    setStep(active.pattern, 0, 61);
    SequencerHistoryPatternSnapshot track0After;
    assert(core::state::sequencer::captureHistorySnapshot(active, track0After));

    SequencerHistoryService history;
    assert(history.recordPattern(
        0,
        std::move(track0Before),
        std::move(track0After),
        SequencerHistoryDescriptor{
            .kind = SequencerHistoryActionKind::StepPropertyEdit,
            .trackIndex = 0,
            .stepIndex = 0,
            .property = StepProperty::NOTE,
            .hasValue = true,
            .beforeValue = 60,
            .afterValue = 61,
        }
    ));

    assert(core::state::sequencer::switchActiveTrack(bank, active, 1));
    setStep(active.pattern, 0, 72);
    SequencerHistoryPatternSnapshot track1Before;
    assert(core::state::sequencer::captureHistorySnapshot(active, track1Before));
    setStep(active.pattern, 0, 73);
    SequencerHistoryPatternSnapshot track1After;
    assert(core::state::sequencer::captureHistorySnapshot(active, track1After));
    assert(history.recordPattern(1, std::move(track1Before), std::move(track1After)));

    assert(history.undo(bank, active));
    assert(bank.activeTrackIndex() == 1);
    assert(active.pattern.note[0] == 72);
    assert(bank.track(1).note[0] == 72);
    assert(bank.track(0).note[0] == 61);

    const auto result = history.undoWithResult(bank, active);
    assert(result.applied);
    assert(result.descriptor.trackIndex == 0);
    assert(result.descriptor.stepIndex == 0);
    assert(result.descriptor.property == StepProperty::NOTE);
    assert(result.descriptor.beforeValue == 60);
    assert(result.descriptor.afterValue == 61);
    assert(bank.activeTrackIndex() == 1);
    assert(active.pattern.note[0] == 72);
    assert(bank.track(1).note[0] == 72);
    assert(bank.track(0).note[0] == 60);

    std::cout << "[PASS] test_pattern_history_undoes_previous_track_without_switching_active_track\n";
}

void test_full_bank_history_restores_active_track_and_graphs() {
    SequencerTrackBankState bank;
    SequencerState active;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, active));
    bank.syncSharedTrackState(0x0003, 0);

    setStep(active.pattern, 0, 60);
    assert(core::state::sequencer::createMicroSequence(
        active.pattern,
        core::state::sequencer::rootStepNodeId(0),
        2
    ).ok);

    setStep(bank.track(1), 0, 72);
    assert(core::state::sequencer::createCycleStateSet(
        bank.track(1),
        core::state::sequencer::rootStepNodeId(0),
        3
    ).ok);

    SequencerHistoryTrackBankSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(bank, active, before));

    assert(core::state::sequencer::switchActiveTrack(bank, active, 1));
    bank.syncSharedTrackState(0x0002, 1);
    setStep(active.pattern, 0, 80);
    assert(core::state::sequencer::createMicroSequence(
        active.pattern,
        core::state::sequencer::rootStepNodeId(2),
        4
    ).ok);

    SequencerHistoryTrackBankSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(bank, active, after));

    SequencerHistoryService history;
    assert(history.recordFullBank(std::move(before), std::move(after)));

    assertActiveDraftBlocksDirectHistory(history, bank, active, false);
    assert(history.undo(bank, active));
    assert(bank.activeTrackIndex() == 0);
    assert(bank.currentEnabledMask() == 0x0003);
    assert(active.pattern.note[0] == 60);
    assert(hasMicroSequence(active.pattern, 0));
    assert(bank.track(1).note[0] == 72);
    assert(hasCycleStateSet(bank.track(1), 0));

    assertActiveDraftBlocksDirectHistory(history, bank, active, true);
    assert(history.redo(bank, active));
    assert(bank.activeTrackIndex() == 1);
    assert(bank.currentEnabledMask() == 0x0002);
    assert(active.pattern.note[0] == 80);
    assert(hasMicroSequence(active.pattern, 2));

    std::cout << "[PASS] test_full_bank_history_restores_active_track_and_graphs\n";
}

void test_structure_history_restores_track_mask_active_track_and_graphs() {
    SequencerTrackBankState bank;
    SequencerState active;
    core::state::macro::MacroPagesState pages;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, active));
    bank.syncSharedTrackState(0x0003, 0);

    setStep(active.pattern, 0, 60);
    assert(core::state::sequencer::createMicroSequence(
        active.pattern,
        core::state::sequencer::rootStepNodeId(0),
        2
    ).ok);
    setStep(bank.track(1), 0, 72);
    assert(core::state::sequencer::createCycleStateSet(
        bank.track(1),
        core::state::sequencer::rootStepNodeId(0),
        3
    ).ok);

    auto change = core::app::makeExtmemUnique<SequencerHistoryTrackStructureChange>();
    assert(change);
    const uint16_t historyMask = static_cast<uint16_t>(
        core::state::sequencer::sequencerHistoryTrackBit(0) |
        core::state::sequencer::sequencerHistoryTrackBit(1)
    );
    assert(core::state::sequencer::captureHistoryStructureSnapshot(
        bank,
        active,
        historyMask,
        change->before
    ));

    assert(core::state::sequencer::switchActiveTrack(bank, active, 1));
    bank.syncSharedTrackState(0x0002, 1);
    setStep(active.pattern, 0, 80);
    assert(core::state::sequencer::createMicroSequence(
        active.pattern,
        core::state::sequencer::rootStepNodeId(2),
        4
    ).ok);
    assert(core::state::sequencer::captureHistoryStructureSnapshot(
        bank,
        active,
        historyMask,
        change->after
    ));

    change->descriptor.kind = SequencerHistoryActionKind::TrackStructure;
    SequencerHistoryService history;
    assert(history.recordStructure(std::move(change)));
    assert(history.undoCount(SequencerHistoryScope::Structure) == 1);
    assert(history.undoCount(SequencerHistoryScope::FullBank) == 0);

    assertActiveDraftBlocksDirectHistory(history, bank, active, false);
    core::state::sequencer::SequencerPreparedStructureHistoryReplay undo;
    assert(
        history.prepareStructureHistoryReplay(
            core::state::sequencer::SequencerHistoryDirection::Undo,
            bank,
            active,
            pages,
            undo) ==
        core::state::sequencer::
            SequencerStructureHistoryReplayPrepareOutcome::Prepared);
    assert(history.commitPreparedStructureHistoryReplay(
        bank, active, pages, std::move(undo)).applied);
    assert(bank.activeTrackIndex() == 0);
    assert(bank.currentEnabledMask() == 0x0003);
    assert(active.pattern.note[0] == 60);
    assert(hasMicroSequence(active.pattern, 0));
    assert(bank.track(1).note[0] == 72);
    assert(hasCycleStateSet(bank.track(1), 0));

    assertActiveDraftBlocksDirectHistory(history, bank, active, true);
    core::state::sequencer::SequencerPreparedStructureHistoryReplay redo;
    assert(
        history.prepareStructureHistoryReplay(
            core::state::sequencer::SequencerHistoryDirection::Redo,
            bank,
            active,
            pages,
            redo) ==
        core::state::sequencer::
            SequencerStructureHistoryReplayPrepareOutcome::Prepared);
    assert(history.commitPreparedStructureHistoryReplay(
        bank, active, pages, std::move(redo)).applied);
    assert(bank.activeTrackIndex() == 1);
    assert(bank.currentEnabledMask() == 0x0002);
    assert(active.pattern.note[0] == 80);
    assert(hasMicroSequence(active.pattern, 2));

    std::cout << "[PASS] test_structure_history_restores_track_mask_active_track_and_graphs\n";
}

void test_structure_history_preflight_matches_record_acceptance() {
    SequencerTrackBankState bank;
    SequencerState active;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, active));

    SequencerHistoryService history;
    auto noOp = core::app::makeExtmemUnique<SequencerHistoryTrackStructureChange>();
    assert(noOp);
    const uint16_t trackMask = core::state::sequencer::sequencerHistoryTrackBit(0);
    assert(core::state::sequencer::captureHistoryStructureSnapshot(
        bank,
        active,
        trackMask,
        noOp->before
    ));
    assert(core::state::sequencer::captureHistoryStructureSnapshot(
        bank,
        active,
        trackMask,
        noOp->after
    ));
    assert(!history.canRecordStructure(*noOp));
    assert(!history.recordStructure(std::move(noOp)));

    auto change = core::app::makeExtmemUnique<SequencerHistoryTrackStructureChange>();
    assert(change);
    assert(core::state::sequencer::captureHistoryStructureSnapshot(
        bank,
        active,
        trackMask,
        change->before
    ));
    setStep(active.pattern, 0, 67);
    assert(core::state::sequencer::captureHistoryStructureSnapshot(
        bank,
        active,
        trackMask,
        change->after
    ));
    assert(history.canRecordStructure(*change));
    history.recordPreparedStructure(std::move(change));
    assert(history.undoCount(SequencerHistoryScope::Structure) == 1);

    std::cout << "[PASS] test_structure_history_preflight_matches_record_acceptance\n";
}

void test_structure_history_preflight_accepts_with_budget_pruning() {
    SequencerHistoryService history;

    auto first = makeGraphHeavyStructureHistoryChange();
    assert(history.canRecordStructure(*first));
    assert(history.recordStructure(std::move(first)));
    const size_t entryBytes = history.retainedBytes();
    assert(entryBytes > 0);
    assert(entryBytes <= SequencerHistoryService::RETAINED_BYTE_BUDGET);

    auto second = makeGraphHeavyStructureHistoryChange();
    assert(history.canRecordStructure(*second));
    assert(history.recordStructure(std::move(second)));
    assert(history.retainedBytes() == entryBytes * 2U);
    assert(history.retainedBytes() <= SequencerHistoryService::RETAINED_BYTE_BUDGET);

    auto incoming = makeGraphHeavyStructureHistoryChange();
    assert(history.retainedBytes() + entryBytes >
           SequencerHistoryService::RETAINED_BYTE_BUDGET);
    const uint8_t countBefore = history.undoCount(SequencerHistoryScope::Structure);
    assert(history.canRecordStructure(*incoming));
    history.recordPreparedStructure(std::move(incoming));
    assert(history.undoCount(SequencerHistoryScope::Structure) == countBefore);
    assert(history.retainedBytes() == entryBytes * 2U);
    assert(history.retainedBytes() <= SequencerHistoryService::RETAINED_BYTE_BUDGET);

    std::cout << "[PASS] test_structure_history_preflight_accepts_with_budget_pruning\n";
}

void test_track_switch_preserves_nested_graph_payload() {
    SequencerTrackBankState bank;
    SequencerState active;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, active));
    bank.syncSharedTrackState(0x0003, 0);

    setStep(active.pattern, 0, 60);
    const auto rootCycle = core::state::sequencer::createCycleStateSet(
        active.pattern,
        core::state::sequencer::rootStepNodeId(0),
        2
    );
    assert(rootCycle.ok);

    auto* graph = active.pattern.graph.get();
    assert(graph != nullptr);
    const auto* rootCycleSet = graph->cycleSet(rootCycle.id);
    assert(rootCycleSet != nullptr);
    const auto secondStateNode = static_cast<uint16_t>(rootCycleSet->firstStateNode + 1U);

    const auto nestedCycle = core::state::sequencer::createCycleStateSet(
        active.pattern,
        secondStateNode,
        2
    );
    assert(nestedCycle.ok);
    graph = active.pattern.graph.get();
    assert(graph != nullptr);
    const auto* nestedCycleSet = graph->cycleSet(nestedCycle.id);
    assert(nestedCycleSet != nullptr);
    assert(core::state::sequencer::setNodeNoteOffset(
        active.pattern,
        static_cast<uint16_t>(nestedCycleSet->firstStateNode + 1U),
        5
    ));

    active.focusedStep.set(0);
    assert(core::state::sequencer::enterCycleStatesContentView(
        active,
        core::state::sequencer::rootStepNodeId(0),
        rootCycle.id
    ));
    assert(!core::state::sequencer::isRootContentView(active));

    assert(core::state::sequencer::switchActiveTrack(bank, active, 1));
    assert(core::state::sequencer::isRootContentView(active));
    assert(core::state::sequencer::switchActiveTrack(bank, active, 0));
    assert(core::state::sequencer::isRootContentView(active));

    assert(hasCycleStateSet(active.pattern, 0));
    assert(hasNestedCycleStateSetWithOffset(active.pattern, 0, 1, 5));

    std::cout << "[PASS] test_track_switch_preserves_nested_graph_payload\n";
}

void test_track_switch_rotates_graph_and_cc_ownership_without_cloning() {
    SequencerTrackBankState bank;
    SequencerState active;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, active));
    bank.syncSharedTrackState(0x0003, 0);

    assert(core::state::sequencer::ensureGraphRoot(active.pattern));
    auto* track0Lanes =
        core::state::sequencer::ensureSequencerCcLaneBank(active.pattern);
    assert(track0Lanes != nullptr);
    core::state::sequencer::SequencerCcLaneDraft track0Draft{};
    track0Draft.destination.controller = 74U;
    assert(core::state::sequencer::createSequencerCcLane(
        *track0Lanes,
        0U,
        track0Draft
    ).changed());
    setStep(active.pattern, 0, 60);

    assert(core::state::sequencer::ensureGraphRoot(bank.track(1)));
    auto* track1Lanes =
        core::state::sequencer::ensureSequencerCcLaneBank(bank.track(1));
    assert(track1Lanes != nullptr);
    core::state::sequencer::SequencerCcLaneDraft track1Draft{};
    track1Draft.destination.controller = 71U;
    assert(core::state::sequencer::createSequencerCcLane(
        *track1Lanes,
        0U,
        track1Draft
    ).changed());
    setStep(bank.track(1), 0, 72);

    const auto* const track0Graph = active.pattern.graph.get();
    const auto* const track0Cc = active.pattern.ccLanes.get();
    const auto* const track1Graph = bank.track(1).graph.get();
    const auto* const track1Cc = bank.track(1).ccLanes.get();
    assert(track0Graph != nullptr && track0Cc != nullptr);
    assert(track1Graph != nullptr && track1Cc != nullptr);

    for (uint16_t cycle = 0; cycle < 100U; ++cycle) {
        assert(core::state::sequencer::switchActiveTrack(bank, active, 1));
        assert(active.pattern.graph.get() == track1Graph);
        assert(active.pattern.ccLanes.get() == track1Cc);
        assert(active.pattern.note[0] == 72U);

        assert(core::state::sequencer::switchActiveTrack(bank, active, 0));
        assert(active.pattern.graph.get() == track0Graph);
        assert(active.pattern.ccLanes.get() == track0Cc);
        assert(active.pattern.note[0] == 60U);
    }

    std::cout
        << "[PASS] test_track_switch_rotates_graph_and_cc_ownership_without_cloning\n";
}

void test_history_limits_prune_by_scope() {
    SequencerTrackBankState bank;
    SequencerState state;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, state));

    SequencerHistoryService history;
    for (uint8_t i = 0; i < SequencerHistoryService::PATTERN_ENTRY_LIMIT + 1U; ++i) {
        SequencerHistoryPatternSnapshot before;
        assert(core::state::sequencer::captureHistorySnapshot(state, before));
        setStep(state.pattern, 0, static_cast<uint8_t>(50U + i));
        SequencerHistoryPatternSnapshot after;
        assert(core::state::sequencer::captureHistorySnapshot(state, after));
        assert(history.recordPattern(std::move(before), std::move(after)));
    }

    for (uint8_t i = 0; i < SequencerHistoryService::FULL_BANK_ENTRY_LIMIT + 1U; ++i) {
        SequencerHistoryTrackBankSnapshot before;
        assert(core::state::sequencer::captureHistorySnapshot(bank, state, before));
        setStep(bank.track(1), 0, static_cast<uint8_t>(70U + i));
        SequencerHistoryTrackBankSnapshot after;
        assert(core::state::sequencer::captureHistorySnapshot(bank, state, after));
        assert(history.recordFullBank(std::move(before), std::move(after)));
    }

    for (uint8_t i = 0; i < SequencerHistoryService::STRUCTURE_ENTRY_LIMIT + 1U; ++i) {
        auto change = core::app::makeExtmemUnique<SequencerHistoryTrackStructureChange>();
        assert(change);
        const uint16_t historyMask = core::state::sequencer::sequencerHistoryTrackBit(0);
        assert(core::state::sequencer::captureHistoryStructureSnapshot(
            bank,
            state,
            historyMask,
            change->before
        ));
        setStep(state.pattern, 1, static_cast<uint8_t>(90U + i));
        assert(core::state::sequencer::captureHistoryStructureSnapshot(
            bank,
            state,
            historyMask,
            change->after
        ));
        change->descriptor.kind = SequencerHistoryActionKind::TrackStructure;
        assert(history.canRecordStructure(*change));
        assert(history.recordStructure(std::move(change)));
    }

    assert(history.undoCount(SequencerHistoryScope::PatternOnly) ==
           SequencerHistoryService::PATTERN_ENTRY_LIMIT);
    assert(history.undoCount(SequencerHistoryScope::Structure) ==
           SequencerHistoryService::STRUCTURE_ENTRY_LIMIT);
    assert(history.undoCount(SequencerHistoryScope::FullBank) ==
           SequencerHistoryService::FULL_BANK_ENTRY_LIMIT);
    assert(history.undoCount() == SequencerHistoryService::ENTRY_LIMIT);

    std::cout << "[PASS] test_history_limits_prune_by_scope\n";
}

void test_history_prunes_graph_heavy_entries_to_psram_budget() {
    SequencerTrackBankState bank;
    SequencerState state;
    assert(core::state::sequencer::initializeTrackBankFromActive(bank, state));
    bank.syncSharedTrackState(0xFFFF, 0);
    assert(core::state::sequencer::ensureGraphRoot(state.pattern));
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        assert(core::state::sequencer::copyGraph(
            bank.track(i),
            state.pattern
        ));
    }

    SequencerHistoryService history;
    for (uint8_t edit = 0; edit < 3; ++edit) {
        SequencerHistoryTrackBankSnapshot before;
        assert(core::state::sequencer::captureHistorySnapshot(bank, state, before));
        bank.track(1).note[0] = static_cast<uint8_t>(61U + edit);
        SequencerHistoryTrackBankSnapshot after;
        assert(core::state::sequencer::captureHistorySnapshot(bank, state, after));
        assert(history.recordFullBank(std::move(before), std::move(after)));
        assert(history.retainedBytes() <= SequencerHistoryService::RETAINED_BYTE_BUDGET);
    }

    assert(history.undoCount(SequencerHistoryScope::FullBank) <
           SequencerHistoryService::FULL_BANK_ENTRY_LIMIT);
    assert(history.undoCount(SequencerHistoryScope::FullBank) > 0);

    std::cout << "[PASS] test_history_prunes_graph_heavy_entries_to_psram_budget\n";
}

void test_clear_resets_stacks() {
    SequencerState state;
    SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(state, before));
    setStep(state.pattern, 0, 64);
    SequencerHistoryPatternSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(state, after));

    SequencerHistoryService history;
    assert(history.recordPattern(std::move(before), std::move(after)));
    history.clear();

    assert(!history.canUndo());
    assert(!history.canRedo());
    assert(history.undoCount() == 0);
    assert(history.redoCount() == 0);

    std::cout << "[PASS] test_clear_resets_stacks\n";
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);

    test_pattern_snapshot_can_capture_inactive_track();
    test_pattern_history_undo_redo_restores_flat_data_and_focus();
    test_flat_pattern_history_restores_region_only_edit();
    test_pattern_history_restores_graph_payload();
    test_flat_pattern_history_preserves_graph_payload();
    test_flat_pattern_history_discards_unretained_payloads_and_preserves_live_cc();
    test_flat_pattern_history_rejects_graph_revision_change();
    test_pattern_noop_ignores_unused_graph_capacity();
    test_pattern_noop_ignores_focus_only_change();
    test_redo_clears_after_new_record();
    test_pattern_history_undoes_previous_track_without_switching_active_track();
    test_full_bank_history_restores_active_track_and_graphs();
    test_structure_history_restores_track_mask_active_track_and_graphs();
    test_structure_history_preflight_matches_record_acceptance();
    test_structure_history_preflight_accepts_with_budget_pruning();
    test_track_switch_preserves_nested_graph_payload();
    test_track_switch_rotates_graph_and_cc_ownership_without_cloning();
    test_history_limits_prune_by_scope();
    test_history_prunes_graph_heavy_entries_to_psram_budget();
    test_clear_resets_stacks();

    std::cout << "All SequencerHistory tests passed\n";
    return 0;
}
