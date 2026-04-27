#include <cassert>
#include <iostream>

#include "../../src/sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
#include "../../src/state/sequencer/SequencerTrackBankState.hpp"

namespace {

void test_refresh_captures_active_editor_state() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank};

    sequencer.length.set(12);
    sequencer.midiChannel.set(4);
    sequencer.note[0] = 67;
    sequencer.bumpStepDataRevision();

    const uint8_t index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.activeTrack == 0);
    assert(snapshot.tracks[0].length == 12);
    assert(snapshot.tracks[0].midiChannel == 4);
    assert(snapshot.tracks[0].note[0] == 67);

    std::cout << "[PASS] test_refresh_captures_active_editor_state\n";
}

void test_refresh_preserves_active_snapshot_until_commit() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank};

    sequencer.length.set(8);
    uint8_t index = bank.refresh();
    bank.commit(index);

    sequencer.length.set(16);
    index = bank.refresh();

    assert(bank.activeSnapshot().tracks[0].length == 8);
    assert(bank.snapshot(index).tracks[0].length == 16);

    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].length == 16);

    std::cout << "[PASS] test_refresh_preserves_active_snapshot_until_commit\n";
}

void test_refresh_captures_inactive_bank_track() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank};

    auto& inactiveTrack = trackBank.track(2);
    inactiveTrack.length.set(24);
    inactiveTrack.midiChannel.set(2);
    inactiveTrack.note[0] = 72;
    inactiveTrack.bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 0);
    const uint8_t index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.enabledMask == 0x0005);
    assert(snapshot.tracks[2].length == 24);
    assert(snapshot.tracks[2].midiChannel == 2);
    assert(snapshot.tracks[2].note[0] == 72);

    std::cout << "[PASS] test_refresh_captures_inactive_bank_track\n";
}

void test_refresh_switches_active_track_sources() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank};

    sequencer.length.set(12);
    sequencer.midiChannel.set(4);
    sequencer.note[0] = 67;
    sequencer.bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 0);
    uint8_t index = bank.refresh();
    bank.commit(index);

    auto& inactiveTrack0 = trackBank.track(0);
    inactiveTrack0.length.set(8);
    inactiveTrack0.midiChannel.set(1);
    inactiveTrack0.note[0] = 60;
    inactiveTrack0.bumpStepDataRevision();

    sequencer.length.set(32);
    sequencer.midiChannel.set(9);
    sequencer.note[0] = 80;
    sequencer.bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 2);
    index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.activeTrack == 2);
    assert(snapshot.enabledMask == 0x0005);
    assert(snapshot.tracks[0].length == 8);
    assert(snapshot.tracks[0].midiChannel == 1);
    assert(snapshot.tracks[0].note[0] == 60);
    assert(snapshot.tracks[2].length == 32);
    assert(snapshot.tracks[2].midiChannel == 9);
    assert(snapshot.tracks[2].note[0] == 80);

    std::cout << "[PASS] test_refresh_switches_active_track_sources\n";
}

}  // namespace

int main() {
    test_refresh_captures_active_editor_state();
    test_refresh_preserves_active_snapshot_until_commit();
    test_refresh_captures_inactive_bank_track();
    test_refresh_switches_active_track_sources();
    std::cout << "All SequencerRuntimeSnapshotBank tests passed\n";
    return 0;
}
