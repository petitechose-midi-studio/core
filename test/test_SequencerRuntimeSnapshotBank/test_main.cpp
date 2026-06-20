#include <cassert>
#include <iostream>

#include "../../src/sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "../../src/state/project/ProjectNavigationState.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
#include "../../src/state/sequencer/SequencerTrackBankState.hpp"

namespace {

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;

bool sameScale(const StepSequencerScaleSettings& lhs, const StepSequencerScaleSettings& rhs) {
    return lhs.root == rhs.root &&
           lhs.type == rhs.type &&
           lhs.mode == rhs.mode;
}

void test_refresh_captures_active_editor_state() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern.length.set(12);
    sequencer.pattern.midiChannel.set(4);
    sequencer.pattern.note[0] = 67;
    sequencer.pattern.bumpStepDataRevision();

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
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern.length.set(8);
    uint8_t index = bank.refresh();
    bank.commit(index);

    sequencer.pattern.length.set(16);
    index = bank.refresh();

    assert(bank.activeSnapshot().tracks[0].length == 8);
    assert(bank.snapshot(index).tracks[0].length == 16);

    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].length == 16);

    std::cout << "[PASS] test_refresh_preserves_active_snapshot_until_commit\n";
}

void test_refresh_keeps_alternating_buffers_current_without_full_copy() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.bumpStepDataRevision();
    uint8_t index = bank.refresh();
    bank.commit(index);

    sequencer.pattern.note[0] = 72;
    sequencer.pattern.bumpStepDataRevision();
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].note[0] == 72);

    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].note[0] == 72);

    std::cout << "[PASS] test_refresh_keeps_alternating_buffers_current_without_full_copy\n";
}

void test_refresh_captures_inactive_bank_track() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    auto& inactiveTrack = trackBank.track(2);
    inactiveTrack.length.set(24);
    inactiveTrack.midiChannel.set(2);
    inactiveTrack.note[0] = 72;
    inactiveTrack.bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 0);
    assert(trackBank.setTrackMuted(2, true));
    const uint8_t index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.enabledMask == 0x0005);
    assert(snapshot.mutedMask == 0x0004);
    assert(snapshot.tracks[2].length == 24);
    assert(snapshot.tracks[2].midiChannel == 2);
    assert(snapshot.tracks[2].note[0] == 72);

    std::cout << "[PASS] test_refresh_captures_inactive_bank_track\n";
}

void test_refresh_switches_active_track_sources() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern.length.set(12);
    sequencer.pattern.midiChannel.set(4);
    sequencer.pattern.note[0] = 67;
    sequencer.pattern.bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 0);
    uint8_t index = bank.refresh();
    bank.commit(index);

    auto& inactiveTrack0 = trackBank.track(0);
    inactiveTrack0.length.set(8);
    inactiveTrack0.midiChannel.set(1);
    inactiveTrack0.note[0] = 60;
    inactiveTrack0.bumpStepDataRevision();

    sequencer.pattern.length.set(32);
    sequencer.pattern.midiChannel.set(9);
    sequencer.pattern.note[0] = 80;
    sequencer.pattern.bumpStepDataRevision();

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

void test_refresh_recreated_active_track_does_not_keep_stale_buffer_payload() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    trackBank.syncSharedTrackState(0x0003, 0);
    auto& staleTrack = trackBank.track(1);
    staleTrack.length.set(16);
    staleTrack.midiChannel.set(1);
    staleTrack.setStepDataAt(0, 99, 111, 80);
    staleTrack.setEnabled(0, true);

    uint8_t index = bank.refresh();
    bank.commit(index);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[1].note[0] == 99);
    assert(bank.activeSnapshot().tracks[1].enabledMask.test(0));

    sequencer.reset();
    sequencer.pattern.midiChannel.set(1);
    trackBank.track(1).reset();
    trackBank.track(1).midiChannel.set(1);
    trackBank.syncSharedTrackState(0x0003, 1);

    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().activeTrack == 1);
    assert(bank.activeSnapshot().tracks[1].note[0] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!bank.activeSnapshot().tracks[1].enabledMask.test(0));
    assert(bank.activeSnapshot().tracks[1].midiChannel == 1);

    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().activeTrack == 1);
    assert(bank.activeSnapshot().tracks[1].note[0] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!bank.activeSnapshot().tracks[1].enabledMask.test(0));

    std::cout << "[PASS] test_refresh_recreated_active_track_does_not_keep_stale_buffer_payload\n";
}

void test_refresh_resolves_project_and_pattern_scale() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    const StepSequencerScaleSettings projectScale{
        .root = 2,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    assert(trackBank.setProjectScaleSettings(projectScale));

    uint8_t index = bank.refresh();
    bank.commit(index);
    assert(sameScale(bank.activeSnapshot().projectScaleSettings, projectScale));
    assert(sameScale(bank.activeSnapshot().tracks[0].effectiveScaleSettings, projectScale));

    const StepSequencerScaleSettings overrideScale{
        .root = 9,
        .type = StepSequencerScaleType::NaturalMinor,
        .mode = StepSequencerScaleConstraintMode::ConstrainDown,
    };
    assert(sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE
    ));
    assert(sequencer.setPatternScaleOverride(overrideScale));
    assert(sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES));

    index = bank.refresh();
    bank.commit(index);
    assert(sameScale(bank.activeSnapshot().tracks[0].effectiveScaleSettings, overrideScale));
    assert(bank.activeSnapshot().tracks[0].pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);

    const StepSequencerScaleSettings nextProjectScale{
        .root = 5,
        .type = StepSequencerScaleType::Dorian,
        .mode = StepSequencerScaleConstraintMode::ConstrainUp,
    };
    assert(trackBank.setProjectScaleSettings(nextProjectScale));
    assert(sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    ));

    index = bank.refresh();
    bank.commit(index);
    assert(sameScale(bank.activeSnapshot().projectScaleSettings, nextProjectScale));
    assert(sameScale(bank.activeSnapshot().tracks[0].effectiveScaleSettings, nextProjectScale));

    std::cout << "[PASS] test_refresh_resolves_project_and_pattern_scale\n";
}

void test_refresh_resolves_project_and_pattern_swing() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    projectNavigation.transportSwingPercent = 20;
    assert(sequencer.setPatternSwingOffsetPercent(12));
    assert(sequencer.setPatternNudgePercent(-8));

    uint8_t index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.projectSwingPercent == 20);
    assert(snapshot.tracks[0].swingOffsetPercent == 12);
    assert(snapshot.tracks[0].effectiveSwingPercent == 32);
    assert(snapshot.tracks[0].patternNudgePercent == -8);

    projectNavigation.transportSwingPercent = 70;
    assert(sequencer.setPatternSwingOffsetPercent(10));
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].effectiveSwingPercent == 75);

    std::cout << "[PASS] test_refresh_resolves_project_and_pattern_swing\n";
}

}  // namespace

int main() {
    test_refresh_captures_active_editor_state();
    test_refresh_preserves_active_snapshot_until_commit();
    test_refresh_keeps_alternating_buffers_current_without_full_copy();
    test_refresh_captures_inactive_bank_track();
    test_refresh_switches_active_track_sources();
    test_refresh_recreated_active_track_does_not_keep_stale_buffer_payload();
    test_refresh_resolves_project_and_pattern_scale();
    test_refresh_resolves_project_and_pattern_swing();
    std::cout << "All SequencerRuntimeSnapshotBank tests passed\n";
    return 0;
}
