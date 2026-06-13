#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerHistory.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
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
    core::state::sequencer::initializeTrackBankFromActive(bank, active);
    bank.syncSharedTrackState(0x0003, 0);

    setStep(active.pattern, 0, 60);
    active.pattern.velocity[0] = 72;
    core::state::sequencer::storeActiveTrack(bank, active);
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
    core::state::sequencer::initializeTrackBankFromActive(bank, state);

    state.pattern.length.set(16);
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

    assert(history.undo(bank, state));
    assert(state.pattern.note[0] == 60);
    assert(state.focusedStep.get() == 0);
    assert(bank.track(bank.activeTrackIndex()).note[0] == 60);

    assert(history.redo(bank, state));
    assert(state.pattern.note[0] == 72);
    assert(state.focusedStep.get() == 9);
    assert(state.page.get() == 1);
    assert(bank.track(bank.activeTrackIndex()).note[0] == 72);

    std::cout << "[PASS] test_pattern_history_undo_redo_restores_flat_data_and_focus\n";
}

void test_pattern_history_restores_graph_payload() {
    SequencerTrackBankState bank;
    SequencerState state;
    core::state::sequencer::initializeTrackBankFromActive(bank, state);

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
    assert(hasMicroSequence(bank.track(bank.activeTrackIndex()), 0));

    std::cout << "[PASS] test_pattern_history_restores_graph_payload\n";
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
    assert(core::state::sequencer::captureHistorySnapshot(state, before));
    assert(core::state::sequencer::captureHistorySnapshot(state, after));
    assert(after.graph != nullptr);

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
    core::state::sequencer::initializeTrackBankFromActive(bank, state);

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
    core::state::sequencer::initializeTrackBankFromActive(bank, active);
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
    core::state::sequencer::initializeTrackBankFromActive(bank, active);
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

    assert(history.undo(bank, active));
    assert(bank.activeTrackIndex() == 0);
    assert(bank.currentEnabledMask() == 0x0003);
    assert(active.pattern.note[0] == 60);
    assert(hasMicroSequence(active.pattern, 0));
    assert(bank.track(1).note[0] == 72);
    assert(hasCycleStateSet(bank.track(1), 0));

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
    core::state::sequencer::initializeTrackBankFromActive(bank, active);
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

    assert(history.undo(bank, active));
    assert(bank.activeTrackIndex() == 0);
    assert(bank.currentEnabledMask() == 0x0003);
    assert(active.pattern.note[0] == 60);
    assert(hasMicroSequence(active.pattern, 0));
    assert(bank.track(1).note[0] == 72);
    assert(hasCycleStateSet(bank.track(1), 0));

    assert(history.redo(bank, active));
    assert(bank.activeTrackIndex() == 1);
    assert(bank.currentEnabledMask() == 0x0002);
    assert(active.pattern.note[0] == 80);
    assert(hasMicroSequence(active.pattern, 2));

    std::cout << "[PASS] test_structure_history_restores_track_mask_active_track_and_graphs\n";
}

void test_track_switch_preserves_nested_graph_payload() {
    SequencerTrackBankState bank;
    SequencerState active;
    core::state::sequencer::initializeTrackBankFromActive(bank, active);
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

void test_history_limits_prune_by_scope() {
    SequencerTrackBankState bank;
    SequencerState state;
    core::state::sequencer::initializeTrackBankFromActive(bank, state);

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
    test_pattern_snapshot_can_capture_inactive_track();
    test_pattern_history_undo_redo_restores_flat_data_and_focus();
    test_pattern_history_restores_graph_payload();
    test_pattern_noop_ignores_unused_graph_capacity();
    test_pattern_noop_ignores_focus_only_change();
    test_redo_clears_after_new_record();
    test_pattern_history_undoes_previous_track_without_switching_active_track();
    test_full_bank_history_restores_active_track_and_graphs();
    test_structure_history_restores_track_mask_active_track_and_graphs();
    test_track_switch_preserves_nested_graph_payload();
    test_history_limits_prune_by_scope();
    test_clear_resets_stacks();

    std::cout << "All SequencerHistory tests passed\n";
    return 0;
}
