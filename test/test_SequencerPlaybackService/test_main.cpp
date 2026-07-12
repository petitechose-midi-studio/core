#include <cassert>
#include <cstdint>
#include <iostream>

#include <oc/api/MidiAPI.hpp>
#include <oc/impl/NullMidi.hpp>

#include "../../src/sequencer/RealtimeMidiQueue.hpp"
#include "../../src/sequencer/SequencerPlaybackService.hpp"
#include "../../src/sequencer/SequencerRuntimeGraphBank.hpp"
#include "../../src/sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "../../src/state/StatusBarState.hpp"
#include "../../src/state/project/ProjectNavigationState.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"

namespace {

using core::state::sequencer::SequencerState;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;

void enableStep(SequencerState& state, uint8_t step) {
    auto mask = state.pattern.enabledMask.get();
    mask.setBit(step, true);
    state.pattern.enabledMask.set(mask);
}

const core::sequencer::SequencerRuntimeSnapshotBank::Snapshot& refreshSnapshot(
    core::sequencer::SequencerRuntimeSnapshotBank& bank,
    core::sequencer::SequencerRuntimeGraphBank& graphBank,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& trackBank
) {
    assert(graphBank.prepare(sequencer, trackBank));
    const uint8_t index = bank.refresh();
    graphBank.publishPrepared([&bank, index]() { bank.commit(index); });
    return bank.activeSnapshot();
}

void test_graph_revision_change_resyncs_playback_service_graph() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer,
        bank,
        projectNavigation,
    };

    sequencer.pattern.length.set(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.velocity[0] = 96;
    sequencer.pattern.gate[0] = 100;
    enableStep(sequencer, 0);

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
    };

    const auto& snapshot = refreshSnapshot(snapshotBank, runtimeGraphBank, sequencer, bank);
    service.update(snapshot, 0, true, 0, 1000, false);
    service.update(snapshot, 12, true, 12000, 1000, false);
    assert(midiQueue.size() == 2);
    midiQueue.clear();
    for (uint8_t track = 0;
         track < core::sequencer::SequencerPlaybackService::TRACK_COUNT;
         ++track) {
        service.stopTrack(track);
        midiQueue.clear();
    }
    service.completeStop();
    midiQueue.clear();

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

    const auto& graphSnapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    service.update(graphSnapshot, 0, true, 0, 1000, false);
    service.update(graphSnapshot, 12, true, 12000, 1000, false);

    assert(midiQueue.size() == 4);
    oc::impl::NullMidi midiTransport;
    oc::api::MidiAPI midi{midiTransport};
    midiQueue.drainDue(midi, 12000, 10000);
    const auto projection = service.takeUiProjectionSnapshot();
    assert(projection.noteOutPulse);
    assert(projection.trackVelocity[0] == 96);

    std::cout << "[PASS] test_graph_revision_change_resyncs_playback_service_graph\n";
}

void test_muted_track_does_not_emit_note_events() {
    SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState bank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank snapshotBank{
        sequencer,
        bank,
        projectNavigation,
    };

    sequencer.pattern.length.set(4);
    sequencer.pattern.stepsPerBeat.set(4);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.velocity[0] = 96;
    sequencer.pattern.gate[0] = 100;
    enableStep(sequencer, 0);
    assert(bank.setTrackMuted(0, true));

    core::sequencer::SequencerPlaybackService service{
        sequencer,
        status,
        midiQueue,
        runtimeGraphBank,
    };

    const auto& snapshot = refreshSnapshot(snapshotBank, runtimeGraphBank, sequencer, bank);
    assert(snapshot.enabledMask == 0x0001);
    assert(snapshot.mutedMask == 0x0001);
    service.update(snapshot, 0, true, 0, 1000, false);

    assert(midiQueue.size() == 0);

    assert(bank.setTrackMuted(0, false));
    const auto& unmutedSnapshot = refreshSnapshot(
        snapshotBank, runtimeGraphBank, sequencer, bank
    );
    service.update(unmutedSnapshot, 0, true, 0, 1000, false);
    assert(midiQueue.size() > 0);

    std::cout << "[PASS] test_muted_track_does_not_emit_note_events\n";
}

}  // namespace

int main() {
    test_graph_revision_change_resyncs_playback_service_graph();
    test_muted_track_does_not_emit_note_events();

    std::cout << "All SequencerPlaybackService tests passed\n";
    return 0;
}
