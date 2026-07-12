#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/sequencer/SequencerRuntimeGraphBank.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"

namespace {

uint16_t createNestedNode(core::state::sequencer::SequencerPatternState& pattern,
                          uint8_t rootStep,
                          int8_t noteOffset) {
    const auto created = core::state::sequencer::createMicroSequence(
        pattern,
        core::state::sequencer::rootStepNodeId(rootStep),
        2
    );
    assert(created.ok);

    const auto* graph = core::state::sequencer::graphView(pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(created.id);
    assert(sequence != nullptr);
    const uint16_t childNode = static_cast<uint16_t>(sequence->firstStepNode + 1U);
    assert(core::state::sequencer::setNodeNoteOffset(pattern, childNode, noteOffset));
    return childNode;
}

void test_prepare_keeps_the_active_graph_until_publication() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphs;

    const uint16_t childNode = createNestedNode(sequencer.pattern, 0, 3);
    const auto* editorGraph = core::state::sequencer::graphView(sequencer.pattern);
    assert(editorGraph != nullptr);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    assert(runtimeGraphs.graphForTrack(0) == nullptr);
    runtimeGraphs.publishPrepared();
    const auto* firstRuntimeGraph = runtimeGraphs.graphForTrack(0);
    assert(firstRuntimeGraph != nullptr);
    assert(firstRuntimeGraph != editorGraph);
    assert(firstRuntimeGraph->stepNodes[childNode].noteOffset == 3);

    assert(core::state::sequencer::setNodeNoteOffset(sequencer.pattern, childNode, 7));
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 3);
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[childNode].noteOffset == 7);

    core::state::sequencer::clearGraph(sequencer.pattern);
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0) == nullptr);

    std::cout << "[PASS] test_prepare_keeps_the_active_graph_until_publication\n";
}

void test_publication_commits_simultaneous_track_changes_together() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphs;

    const uint16_t activeNode = createNestedNode(sequencer.pattern, 0, 2);
    const uint16_t inactiveNode = createNestedNode(trackBank.track(1), 0, 5);

    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();
    assert(runtimeGraphs.graphForTrack(0)->stepNodes[activeNode].noteOffset == 2);
    assert(runtimeGraphs.graphForTrack(1)->stepNodes[inactiveNode].noteOffset == 5);

    assert(core::state::sequencer::setNodeNoteOffset(sequencer.pattern, activeNode, 4));
    assert(core::state::sequencer::setNodeNoteOffset(trackBank.track(1), inactiveNode, 9));
    assert(runtimeGraphs.prepare(sequencer, trackBank));
    runtimeGraphs.publishPrepared();

    assert(runtimeGraphs.graphForTrack(0)->stepNodes[activeNode].noteOffset == 4);
    assert(runtimeGraphs.graphForTrack(1)->stepNodes[inactiveNode].noteOffset == 9);

    std::cout << "[PASS] test_publication_commits_simultaneous_track_changes_together\n";
}

}  // namespace

int main() {
    test_prepare_keeps_the_active_graph_until_publication();
    test_publication_commits_simultaneous_track_changes_together();
    std::cout << "All SequencerRuntimeGraphBank tests passed\n";
    return 0;
}
