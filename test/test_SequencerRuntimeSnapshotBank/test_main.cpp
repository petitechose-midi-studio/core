#include <cassert>
#include <iostream>

#include "../../src/sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "../../src/state/project/ProjectNavigationState.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
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

    sequencer.pattern.setContentLength(12);
    sequencer.pattern.note[0] = 67;
    sequencer.pattern.bumpStepDataRevision();

    const uint8_t index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.activeTrack == 0);
    assert(snapshot.tracks[0].length == 12);
    assert(snapshot.tracks[0].note[0] == 67);

    std::cout << "[PASS] test_refresh_captures_active_editor_state\n";
}

void test_refresh_preserves_active_snapshot_until_commit() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern.setContentLength(8);
    uint8_t index = bank.refresh();
    bank.commit(index);

    sequencer.pattern.setContentLength(16);
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

    sequencer.pattern.setContentLength(8);
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

void test_region_markers_invalidate_both_flat_runtime_buffers() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{
        sequencer,
        trackBank,
        projectNavigation,
    };

    uint8_t index = bank.refresh();
    bank.commit(index);
    index = bank.refresh();
    bank.commit(index);

    const auto before = core::sequencer::captureRuntimeStateSignature(
        sequencer.pattern,
        {},
        {}
    );
    const uint32_t unchangedRevision = sequencer.pattern.patternTimingRevision.get();
    // Reproduce a snapshot restore where the historical revision can be equal
    // even though the persisted region changed.
    sequencer.pattern.playStart = 1;
    sequencer.pattern.loopStart = 2;
    sequencer.pattern.loopEnd = 6;
    assert(sequencer.pattern.patternTimingRevision.get() == unchangedRevision);
    const auto after = core::sequencer::captureRuntimeStateSignature(
        sequencer.pattern,
        {},
        {}
    );
    assert(!before.matches(after));
    assert(after.matches(core::sequencer::captureRuntimeStateSignature(
        sequencer.pattern,
        {},
        {}
    )));

    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].playStart == 1);
    assert(bank.activeSnapshot().tracks[0].loopStart == 2);
    assert(bank.activeSnapshot().tracks[0].loopEnd == 6);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].playStart == 1);
    assert(bank.activeSnapshot().tracks[0].loopStart == 2);
    assert(bank.activeSnapshot().tracks[0].loopEnd == 6);

    std::cout << "[PASS] test_region_markers_invalidate_both_flat_runtime_buffers\n";
}

void test_refresh_skips_unchanged_cc_lane_payloads_per_buffer() {
    using namespace core::state::sequencer;

    SequencerState sequencer;
    SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{
        sequencer,
        trackBank,
        projectNavigation,
    };

    auto* lanes = ensureSequencerCcLaneBank(sequencer.pattern);
    assert(lanes != nullptr);
    SequencerCcLaneDraft draft;
    draft.destination.controller = 74;
    assert(createSequencerCcLane(*lanes, 0, draft).changed());
    sequencer.pattern.bumpCcLaneRevision();

    uint8_t index = bank.refresh();
    bank.commit(index);
    assert(bank.lanePayloadWriteCount() == 1);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.lanePayloadWriteCount() == 2);

    // Both alternating buffers now carry the same immutable lane generation.
    // Stable refreshes must not copy the 16 Track payloads again.
    index = bank.refresh();
    bank.commit(index);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.lanePayloadWriteCount() == 2);

    assert(setSequencerCcLaneEvent(*lanes, 0, 3, 96).changed());
    sequencer.pattern.bumpCcLaneRevision();
    index = bank.refresh();
    bank.commit(index);
    assert(bank.lanePayloadWriteCount() == 3);
    const auto* activeLanes = bank.laneSnapshot(index);
    assert(activeLanes != nullptr);
    const auto* activeTrackLanes = activeLanes->lanesForTrack(0);
    assert(activeTrackLanes != nullptr);
    assert(activeTrackLanes->lanes[0].values[3] == 96);

    index = bank.refresh();
    bank.commit(index);
    assert(bank.lanePayloadWriteCount() == 4);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.lanePayloadWriteCount() == 4);

    sequencer.pattern.ccLanes.reset();
    sequencer.pattern.bumpCcLaneRevision();
    index = bank.refresh();
    bank.commit(index);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.lanePayloadWriteCount() == 6);
    assert(bank.laneSnapshot(index) != nullptr);
    assert(bank.laneSnapshot(index)->lanesForTrack(0) == nullptr);

    index = bank.refresh();
    bank.commit(index);
    assert(bank.lanePayloadWriteCount() == 6);

    std::cout << "[PASS] test_refresh_skips_unchanged_cc_lane_payloads_per_buffer\n";
}

void test_refresh_captures_inactive_bank_track() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    auto& inactiveTrack = trackBank.track(2);
    inactiveTrack.setContentLength(24);
    inactiveTrack.note[0] = 72;
    inactiveTrack.bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 0);
    const uint8_t index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.enabledMask == 0x0005);
    assert(snapshot.tracks[2].length == 24);
    assert(snapshot.tracks[2].note[0] == 72);

    std::cout << "[PASS] test_refresh_captures_inactive_bank_track\n";
}

void test_refresh_switches_active_track_sources() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern.setContentLength(12);
    sequencer.pattern.note[0] = 67;
    sequencer.pattern.bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 0);
    uint8_t index = bank.refresh();
    bank.commit(index);

    auto& inactiveTrack0 = trackBank.track(0);
    inactiveTrack0.setContentLength(8);
    inactiveTrack0.note[0] = 60;
    inactiveTrack0.bumpStepDataRevision();

    sequencer.pattern.setContentLength(32);
    sequencer.pattern.note[0] = 80;
    sequencer.pattern.bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 2);
    index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.activeTrack == 2);
    assert(snapshot.enabledMask == 0x0005);
    assert(snapshot.tracks[0].length == 8);
    assert(snapshot.tracks[0].note[0] == 60);
    assert(snapshot.tracks[2].length == 32);
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
    staleTrack.setContentLength(16);
    staleTrack.setStepDataAt(0, 99, 111, 80);
    staleTrack.setEnabled(0, true);

    uint8_t index = bank.refresh();
    bank.commit(index);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[1].note[0] == 99);
    assert(bank.activeSnapshot().tracks[1].enabledMask.test(0));

    sequencer.reset();
    trackBank.track(1).reset();
    trackBank.syncSharedTrackState(0x0003, 1);

    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().activeTrack == 1);
    assert(bank.activeSnapshot().tracks[1].note[0] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!bank.activeSnapshot().tracks[1].enabledMask.test(0));

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
    assert(
        sequencer.pattern.pitchEditMode ==
        core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE
    );

    index = bank.refresh();
    bank.commit(index);
    assert(sameScale(bank.activeSnapshot().tracks[0].effectiveScaleSettings, overrideScale));
    assert(bank.activeSnapshot().tracks[0].pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE);

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

void test_quick_controls_preview_round_trips_through_inactive_runtime_buffers() {
    namespace seq = core::state::sequencer;
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState trackBank;
    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{
        sequencer,
        trackBank,
        projectNavigation,
    };

    sequencer.pattern.setContentLength(8U);
    assert(sequencer.pattern.setStepNoteAt(0U, 60U));
    auto* liveLanes = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(liveLanes != nullptr);
    seq::SequencerCcLaneDraft laneDraft{};
    laneDraft.destination.controller = 74U;
    assert(seq::createSequencerCcLane(*liveLanes, 0U, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*liveLanes, 0U, 0U, 11U).changed());
    sequencer.pattern.bumpCcLaneRevision();

    uint8_t index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].length == 8U);
    assert(bank.activeSnapshot().tracks[0].note[0] == 60U);

    const auto openingPath = seq::capturePreparedSequencerGraphContentPath(sequencer);
    assert(sequencer.quickControlsDraft.begin(
        sequencer.pattern,
        openingPath,
        sequencer.page.get(),
        sequencer.focusedStep.get()));
    auto& draft = seq::authoringPattern(sequencer);
    draft.setContentLength(12U);
    assert(draft.setStepNoteAt(0U, 72U));
    assert(draft.ccLanes != nullptr);
    assert(seq::setSequencerCcLaneEvent(*draft.ccLanes, 0U, 3U, 96U).changed());
    draft.bumpCcLaneRevision();
    sequencer.patternQuickControls.bumpPreview();

    index = bank.refresh();
    assert(bank.activeSnapshot().tracks[0].length == 8U);
    assert(bank.activeSnapshot().tracks[0].note[0] == 60U);
    assert(bank.snapshot(index).tracks[0].length == 12U);
    assert(bank.snapshot(index).tracks[0].note[0] == 72U);
    const auto* preparedLanes = bank.laneSnapshot(index)->lanesForTrack(0U);
    assert(preparedLanes != nullptr);
    assert(preparedLanes->lanes[0].values[3] == 96U);
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].length == 12U);
    assert(bank.activeSnapshot().tracks[0].note[0] == 72U);

    sequencer.quickControlsDraft.reset();
    sequencer.patternQuickControls.bumpPreview();
    index = bank.refresh();
    assert(bank.activeSnapshot().tracks[0].length == 12U);
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].length == 8U);
    assert(bank.activeSnapshot().tracks[0].note[0] == 60U);
    const auto* restoredLanes = bank.laneSnapshot(index)->lanesForTrack(0U);
    assert(restoredLanes != nullptr);
    assert(!restoredLanes->lanes[0].activeMask.test(3U));

    std::cout
        << "[PASS] Quick Controls flat/CC preview round-trips through immutable buffers\n";
}

}  // namespace

int main() {
    test_refresh_captures_active_editor_state();
    test_refresh_preserves_active_snapshot_until_commit();
    test_refresh_keeps_alternating_buffers_current_without_full_copy();
    test_region_markers_invalidate_both_flat_runtime_buffers();
    test_refresh_skips_unchanged_cc_lane_payloads_per_buffer();
    test_refresh_captures_inactive_bank_track();
    test_refresh_switches_active_track_sources();
    test_refresh_recreated_active_track_does_not_keep_stale_buffer_payload();
    test_refresh_resolves_project_and_pattern_scale();
    test_refresh_resolves_project_and_pattern_swing();
    test_quick_controls_preview_round_trips_through_inactive_runtime_buffers();
    std::cout << "All SequencerRuntimeSnapshotBank tests passed\n";
    return 0;
}
