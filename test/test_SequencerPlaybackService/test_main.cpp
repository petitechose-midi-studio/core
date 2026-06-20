#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/sequencer/RealtimeMidiQueue.hpp"
#include "../../src/sequencer/SequencerPlaybackService.hpp"
#include "../../src/state/StatusBarState.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"

namespace {

using core::state::sequencer::SequencerState;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;

void enableStep(SequencerState& state, uint8_t step) {
    auto mask = state.pattern.enabledMask.get();
    mask.setBit(step, true);
    state.pattern.enabledMask.set(mask);
}

core::state::sequencer::SequencerTrackBankSnapshot captureSnapshot(
    const core::state::sequencer::SequencerTrackBankState& bank,
    const SequencerState& sequencer
) {
    core::state::sequencer::SequencerTrackBankSnapshot snapshot;
    core::state::sequencer::captureTrackBankSnapshot(bank, sequencer, snapshot);
    return snapshot;
}

void test_graph_revision_change_resyncs_playback_service_graph() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;

    sequencer.pattern.length.set(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.velocity[0] = 96;
    sequencer.pattern.gate[0] = 100;
    enableStep(sequencer, 0);

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        bank,
        status,
        midiQueue,
    };

    auto snapshot = captureSnapshot(bank, sequencer);
    service.update(snapshot, 0, true, 0, 0, 1000, false, false);
    service.update(snapshot, 12, true, 0, 12000, 1000, false, false);
    auto counters = midiQueue.takeCounters();
    assert(counters.pushed == 2);
    midiQueue.clear();
    service.stop();
    midiQueue.clear();
    midiQueue.takeCounters();

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        sequencer.pattern,
        rootNode,
        2
    );
    assert(sequence.ok);
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* child = graph->sequence(sequence.id);
    assert(child != nullptr);
    assert(core::state::sequencer::setNodeNoteOffset(
        sequencer.pattern,
        static_cast<uint16_t>(child->firstStepNode + 1U),
        7
    ));
    assert(graph->stepNodes[child->firstStepNode + 1U].has(STEP_NODE_NOTE_OFFSET));

    snapshot = captureSnapshot(bank, sequencer);
    service.update(snapshot, 0, true, 1, 0, 1000, false, false);
    service.update(snapshot, 12, true, 1, 12000, 1000, false, false);
    counters = midiQueue.takeCounters();

    assert(counters.pushed == 4);
    const auto projection = service.takeUiProjectionSnapshot();
    assert(projection.noteOutPulse);
    assert(projection.trackVelocity[0] == 96);

    std::cout << "[PASS] test_graph_revision_change_resyncs_playback_service_graph\n";
}

void test_muted_track_does_not_emit_note_events() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;

    sequencer.pattern.length.set(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.velocity[0] = 96;
    sequencer.pattern.gate[0] = 100;
    enableStep(sequencer, 0);
    assert(bank.setTrackMuted(0, true));

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        bank,
        status,
        midiQueue,
    };

    auto snapshot = captureSnapshot(bank, sequencer);
    assert(snapshot.enabledMask == 0x0001);
    assert(snapshot.mutedMask == 0x0001);
    service.update(snapshot, 0, true, 0, 0, 1000, false, false);

    auto counters = midiQueue.takeCounters();
    assert(counters.pushed == 0);

    assert(bank.setTrackMuted(0, false));
    snapshot = captureSnapshot(bank, sequencer);
    service.update(snapshot, 0, true, 1, 0, 1000, false, false);
    counters = midiQueue.takeCounters();
    assert(counters.pushed > 0);

    std::cout << "[PASS] test_muted_track_does_not_emit_note_events\n";
}

}  // namespace

int main() {
    test_graph_revision_change_resyncs_playback_service_graph();
    test_muted_track_does_not_emit_note_events();

    std::cout << "All SequencerPlaybackService tests passed\n";
    return 0;
}
