#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerHistory.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"

namespace {

using core::state::sequencer::SequencerHistoryPatternSnapshot;
using core::state::sequencer::SequencerHistoryScope;
using core::state::sequencer::SequencerHistoryService;
using core::state::sequencer::SequencerHistoryTrackBankSnapshot;
using core::state::sequencer::SequencerPatternState;
using core::state::sequencer::SequencerState;
using core::state::sequencer::SequencerTrackBankState;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;

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

    assert(history.undoCount(SequencerHistoryScope::PatternOnly) ==
           SequencerHistoryService::PATTERN_ENTRY_LIMIT);
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
    test_pattern_history_undo_redo_restores_flat_data_and_focus();
    test_pattern_history_restores_graph_payload();
    test_pattern_noop_ignores_focus_only_change();
    test_redo_clears_after_new_record();
    test_full_bank_history_restores_active_track_and_graphs();
    test_history_limits_prune_by_scope();
    test_clear_resets_stacks();

    std::cout << "All SequencerHistory tests passed\n";
    return 0;
}
