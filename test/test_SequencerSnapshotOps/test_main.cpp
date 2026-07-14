#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"

namespace {

using core::state::sequencer::SequencerState;
using oc::note::sequencer::StepBitMask128;

void setStep(SequencerState& sequencer,
             uint8_t step,
             uint8_t note,
             uint8_t velocity,
             uint16_t gate,
             int8_t nudge,
             uint8_t probability,
             bool enabled) {
    sequencer.pattern.note[step] = note;
    sequencer.pattern.velocity[step] = velocity;
    sequencer.pattern.gate[step] = gate;
    sequencer.pattern.nudge[step] = nudge;
    sequencer.pattern.probability[step] = probability;

    auto mask = sequencer.pattern.enabledMask.get();
    mask.setBit(step, enabled);
    sequencer.pattern.enabledMask.set(mask);
}

void assertDefaultStep(const SequencerState& sequencer, uint8_t step) {
    assert(sequencer.pattern.note[step] == SequencerState::DEFAULT_NOTE);
    assert(sequencer.pattern.velocity[step] == SequencerState::DEFAULT_VELOCITY);
    assert(sequencer.pattern.gate[step] == SequencerState::DEFAULT_GATE_PERCENT);
    assert(sequencer.pattern.nudge[step] == 0);
    assert(sequencer.pattern.probability[step] == SequencerState::DEFAULT_PROBABILITY);
    assert(!sequencer.pattern.isEnabled(step));
}

void assertStep(const SequencerState& sequencer,
                uint8_t step,
                uint8_t note,
                uint8_t velocity,
                uint16_t gate,
                int8_t nudge,
                uint8_t probability,
                bool enabled) {
    assert(sequencer.pattern.note[step] == note);
    assert(sequencer.pattern.velocity[step] == velocity);
    assert(sequencer.pattern.gate[step] == gate);
    assert(sequencer.pattern.nudge[step] == nudge);
    assert(sequencer.pattern.probability[step] == probability);
    assert(sequencer.pattern.isEnabled(step) == enabled);
}

bool rootStepHasMicroSequence(const SequencerState& sequencer, uint8_t step) {
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    if (graph == nullptr) return false;
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    if (nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE);
}

void createRootMicroSequence(SequencerState& sequencer, uint8_t step, uint8_t length) {
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    const auto result = core::state::sequencer::createMicroSequence(
        sequencer.pattern,
        nodeId,
        length
    );
    assert(result.ok);
}

void test_clear_step_range_resets_payload_and_mask() {
    SequencerState sequencer;
    sequencer.pattern.length.set(16);
    setStep(sequencer, 2, 62, 90, 70, -3, 55, true);
    setStep(sequencer, 3, 63, 91, 71, 4, 56, true);

    const uint32_t revisionBefore = sequencer.pattern.stepDataRevision.get();
    assert(core::state::sequencer::clearStepRange(sequencer, 2, 3));

    assertDefaultStep(sequencer, 2);
    assertDefaultStep(sequencer, 3);
    assert(sequencer.focusedStep.get() == 2);
    assert(sequencer.page.get() == 0);
    assert(sequencer.pattern.stepDataRevision.get() == revisionBefore + 1);

    std::cout << "[PASS] test_clear_step_range_resets_payload_and_mask\n";
}

void test_clear_step_range_clears_child_content() {
    SequencerState sequencer;
    sequencer.pattern.length.set(16);
    createRootMicroSequence(sequencer, 2, 2);
    assert(rootStepHasMicroSequence(sequencer, 2));

    assert(core::state::sequencer::clearStepRange(sequencer, 2, 2));

    assert(!rootStepHasMicroSequence(sequencer, 2));

    std::cout << "[PASS] test_clear_step_range_clears_child_content\n";
}

void test_insert_page_shifts_payloads_and_clears_inserted_page() {
    SequencerState sequencer;
    sequencer.pattern.length.set(16);
    setStep(sequencer, 8, 70, 110, 90, 5, 60, true);

    assert(core::state::sequencer::insertPage(sequencer, 1));

    assert(sequencer.pattern.length.get() == 24);
    assertDefaultStep(sequencer, 8);
    assertStep(sequencer, 16, 70, 110, 90, 5, 60, true);
    assert(sequencer.focusedStep.get() == 8);
    assert(sequencer.page.get() == 1);

    std::cout << "[PASS] test_insert_page_shifts_payloads_and_clears_inserted_page\n";
}

void test_insert_page_shifts_child_content() {
    SequencerState sequencer;
    sequencer.pattern.length.set(16);
    createRootMicroSequence(sequencer, 8, 2);

    assert(core::state::sequencer::insertPage(sequencer, 1));

    assert(!rootStepHasMicroSequence(sequencer, 8));
    assert(rootStepHasMicroSequence(sequencer, 16));

    std::cout << "[PASS] test_insert_page_shifts_child_content\n";
}

void test_remove_page_shifts_following_payloads() {
    SequencerState sequencer;
    sequencer.pattern.length.set(24);
    setStep(sequencer, 16, 72, 111, 91, -4, 61, true);

    assert(core::state::sequencer::removePage(sequencer, 1));

    assert(sequencer.pattern.length.get() == 16);
    assertStep(sequencer, 8, 72, 111, 91, -4, 61, true);
    assertDefaultStep(sequencer, 16);
    assert(sequencer.focusedStep.get() == 8);
    assert(sequencer.page.get() == 1);

    std::cout << "[PASS] test_remove_page_shifts_following_payloads\n";
}

void test_remove_page_shifts_child_content() {
    SequencerState sequencer;
    sequencer.pattern.length.set(24);
    createRootMicroSequence(sequencer, 16, 2);

    assert(core::state::sequencer::removePage(sequencer, 1));

    assert(rootStepHasMicroSequence(sequencer, 8));
    assert(!rootStepHasMicroSequence(sequencer, 16));

    std::cout << "[PASS] test_remove_page_shifts_child_content\n";
}

void test_rotate_pattern_moves_payload_and_mask() {
    SequencerState sequencer;
    sequencer.pattern.length.set(4);
    sequencer.pattern.enabledMask.set(StepBitMask128{});
    setStep(sequencer, 0, 60, 90, 50, 0, 100, true);
    setStep(sequencer, 1, 61, 91, 51, 1, 80, false);

    assert(core::state::sequencer::rotatePattern(sequencer, 1));

    assertStep(sequencer, 1, 60, 90, 50, 0, 100, true);
    assertStep(sequencer, 2, 61, 91, 51, 1, 80, false);
    assert(!sequencer.pattern.isEnabled(0));
    assert(!sequencer.pattern.isEnabled(3));

    std::cout << "[PASS] test_rotate_pattern_moves_payload_and_mask\n";
}

void test_snapshot_apply_and_merge_clear_graph_payload_but_keep_revision() {
    SequencerState source;
    const auto sourceNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(source.pattern, sourceNode, 2).ok);

    core::state::sequencer::SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(source.pattern, snapshot);
    assert(snapshot.graphRevision == source.pattern.graphRevision.get());

    SequencerState applied;
    assert(core::state::sequencer::createMicroSequence(applied.pattern, sourceNode, 2).ok);
    core::state::sequencer::applySnapshot(applied.pattern, snapshot);
    assert(applied.pattern.graph.get() == nullptr);
    assert(applied.pattern.graphRevision.get() == snapshot.graphRevision);

    SequencerState merged;
    assert(core::state::sequencer::createCycleStateSet(merged.pattern, sourceNode, 2).ok);
    core::state::sequencer::mergeSnapshotIntoCurrent(merged, snapshot);
    assert(merged.pattern.graph.get() == nullptr);
    assert(merged.pattern.graphRevision.get() == snapshot.graphRevision);

    std::cout << "[PASS] test_snapshot_apply_and_merge_clear_graph_payload_but_keep_revision\n";
}

void test_track_content_snapshot_preserves_destination_midi_channel() {
    SequencerState source;
    source.pattern.midiChannel.set(3);
    setStep(source, 0, 74, 103, 88, -2, 79, true);
    createRootMicroSequence(source, 0, 2);

    core::state::sequencer::SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(source.pattern, snapshot);
    const auto* sourceGraph = core::state::sequencer::graphView(source.pattern);
    assert(sourceGraph != nullptr);

    SequencerState bankTarget;
    bankTarget.pattern.midiChannel.set(10);
    assert(core::state::sequencer::applyTrackContentSnapshotWithGraph(
        bankTarget.pattern,
        snapshot,
        sourceGraph
    ));
    assert(bankTarget.pattern.midiChannel.get() == 10);
    assertStep(bankTarget, 0, 74, 103, 88, -2, 79, true);
    assert(rootStepHasMicroSequence(bankTarget, 0));

    SequencerState editorTarget;
    editorTarget.pattern.midiChannel.set(12);
    assert(core::state::sequencer::applyTrackContentSnapshotToEditorWithGraph(
        editorTarget,
        snapshot,
        sourceGraph
    ));
    assert(editorTarget.pattern.midiChannel.get() == 12);
    assertStep(editorTarget, 0, 74, 103, 88, -2, 79, true);
    assert(rootStepHasMicroSequence(editorTarget, 0));

    SequencerState genericTarget;
    genericTarget.pattern.midiChannel.set(14);
    assert(core::state::sequencer::applySnapshotToEditorWithGraph(
        genericTarget,
        snapshot,
        sourceGraph
    ));
    assert(genericTarget.pattern.midiChannel.get() == 3);

    std::cout
        << "[PASS] test_track_content_snapshot_preserves_destination_midi_channel\n";
}

}  // namespace

int main() {
    test_clear_step_range_resets_payload_and_mask();
    test_clear_step_range_clears_child_content();
    test_insert_page_shifts_payloads_and_clears_inserted_page();
    test_insert_page_shifts_child_content();
    test_remove_page_shifts_following_payloads();
    test_remove_page_shifts_child_content();
    test_rotate_pattern_moves_payload_and_mask();
    test_snapshot_apply_and_merge_clear_graph_payload_but_keep_revision();
    test_track_content_snapshot_preserves_destination_midi_channel();

    std::cout << "All SequencerSnapshotOps tests passed\n";
    return 0;
}
