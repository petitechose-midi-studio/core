#include <cassert>
#include <iostream>

#include "../../src/sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "../../src/state/project/ProjectNavigationState.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerClipRegionOps.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
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
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    assert(core::state::sequencer::resizeClipPatternContent(sequencer, 12));
    sequencer.pattern().note[0] = 67;
    sequencer.pattern().bumpStepDataRevision();

    const uint8_t index = bank.refresh();
    bank.commit(index);

    const auto& snapshot = bank.activeSnapshot();
    assert(snapshot.activeTrack == 0);
    assert(snapshot.tracks[0].length == 12);
    assert(snapshot.tracks[0].note[0] == 67);

    std::cout << "[PASS] test_refresh_captures_active_editor_state\n";
}

void test_refresh_preserves_active_snapshot_until_commit() {
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern().setContentLength(8);
    uint8_t index = bank.refresh();
    bank.commit(index);

    assert(core::state::sequencer::resizeClipPatternContent(sequencer, 16));
    index = bank.refresh();

    assert(bank.activeSnapshot().tracks[0].length == 8);
    assert(bank.snapshot(index).tracks[0].length == 16);

    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].length == 16);

    std::cout << "[PASS] test_refresh_preserves_active_snapshot_until_commit\n";
}

void test_refresh_keeps_alternating_buffers_current_without_full_copy() {
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern().setContentLength(8);
    sequencer.pattern().note[0] = 60;
    sequencer.pattern().bumpStepDataRevision();
    uint8_t index = bank.refresh();
    bank.commit(index);

    sequencer.pattern().note[0] = 72;
    sequencer.pattern().bumpStepDataRevision();
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].note[0] == 72);

    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].note[0] == 72);

    std::cout << "[PASS] test_refresh_keeps_alternating_buffers_current_without_full_copy\n";
}

void test_invalidate_refreshes_both_buffers_after_project_replacement() {
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    sequencer.pattern().note[0] = 60U;
    uint8_t index = bank.refresh();
    bank.commit(index);
    index = bank.refresh();
    bank.commit(index);

    // Project files restore authored revisions, so content may change while
    // presenting the same signature to both alternating runtime buffers.
    sequencer.pattern().note[0] = 72U;
    bank.invalidate();
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].note[0] == 72U);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].note[0] == 72U);

    std::cout << "[PASS] project replacement invalidates both runtime buffers\n";
}

void test_region_markers_invalidate_both_flat_runtime_buffers() {
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

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
        sequencer.pattern(),
        sequencer.clip(),
        {},
        {}
    );
    const uint32_t unchangedRevision = sequencer.pattern().patternTimingRevision.get();
    // Reproduce a snapshot restore where the historical revision can be equal
    // even though the persisted region changed.
    assert(core::state::sequencer::setClipPlaybackRegion(
        sequencer.pattern(),
        sequencer.clip(),
        {8U, 1U, 2U, 6U}
    ));
    assert(sequencer.pattern().patternTimingRevision.get() == unchangedRevision);
    const auto after = core::sequencer::captureRuntimeStateSignature(
        sequencer.pattern(),
        sequencer.clip(),
        {},
        {}
    );
    assert(!before.matches(after));
    assert(after.matches(core::sequencer::captureRuntimeStateSignature(
        sequencer.pattern(),
        sequencer.clip(),
        {},
        {}
    )));

    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().clips[0].playStartTick ==
           sequencer.clip().playStartTick);
    assert(bank.activeSnapshot().clips[0].loopStartTick ==
           sequencer.clip().loopStartTick);
    assert(bank.activeSnapshot().clips[0].loopEndTick ==
           sequencer.clip().loopEndTick);
    index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().clips[0].playStartTick ==
           sequencer.clip().playStartTick);
    assert(bank.activeSnapshot().clips[0].loopStartTick ==
           sequencer.clip().loopStartTick);
    assert(bank.activeSnapshot().clips[0].loopEndTick ==
           sequencer.clip().loopEndTick);

    std::cout << "[PASS] test_region_markers_invalidate_both_flat_runtime_buffers\n";
}

void test_refresh_skips_unchanged_cc_lane_payloads_per_buffer() {
    using namespace core::state::sequencer;

    SequencerTrackBankState trackBank;
    SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{
        sequencer,
        trackBank,
        projectNavigation,
    };

    auto* lanes = ensureSequencerCcLaneBank(sequencer.pattern());
    assert(lanes != nullptr);
    SequencerCcLaneDraft draft;
    draft.destination.controller = 74;
    assert(createSequencerCcLane(*lanes, 0, draft).changed());
    sequencer.pattern().bumpCcLaneRevision();

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
    sequencer.pattern().bumpCcLaneRevision();
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

    sequencer.pattern().ccLanes.reset();
    sequencer.pattern().bumpCcLaneRevision();
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
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    auto& inactiveTrack = trackBank.track(2);
    assert(core::state::sequencer::resizeClipPatternContent(
        inactiveTrack,
        trackBank.clip(2),
        24
    ));
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
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    assert(core::state::sequencer::resizeClipPatternContent(sequencer, 12));
    sequencer.pattern().note[0] = 67;
    sequencer.pattern().bumpStepDataRevision();

    trackBank.syncSharedTrackState(0x0005, 0);
    uint8_t index = bank.refresh();
    bank.commit(index);

    auto& inactiveTrack0 = trackBank.track(0);
    inactiveTrack0.setContentLength(8);
    core::state::sequencer::resetClipToPattern(trackBank.clip(0), inactiveTrack0);
    inactiveTrack0.note[0] = 60;
    inactiveTrack0.bumpStepDataRevision();

    sequencer.selectPattern(trackBank.track(2U), trackBank.clip(2U));
    assert(core::state::sequencer::resizeClipPatternContent(sequencer, 32));
    sequencer.pattern().note[0] = 80;
    sequencer.pattern().bumpStepDataRevision();

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
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{sequencer, trackBank, projectNavigation};

    trackBank.syncSharedTrackState(0x0003, 0);
    auto& staleTrack = trackBank.track(1);
    assert(core::state::sequencer::resizeClipPatternContent(
        staleTrack,
        trackBank.clip(1),
        16
    ));
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
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

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
        sequencer.pattern().pitchEditMode ==
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
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::state::sequencer::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

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
    seq::SequencerTrackBankState trackBank;
    seq::SequencerState sequencer{trackBank.track(trackBank.activeTrackIndex()), trackBank.clip(trackBank.activeTrackIndex())};

    core::state::project::ProjectNavigationState projectNavigation;
    core::sequencer::SequencerRuntimeSnapshotBank bank{
        sequencer,
        trackBank,
        projectNavigation,
    };

    sequencer.pattern().setContentLength(8U);
    assert(sequencer.pattern().setStepNoteAt(0U, 60U));
    auto* liveLanes = seq::ensureSequencerCcLaneBank(sequencer.pattern());
    assert(liveLanes != nullptr);
    seq::SequencerCcLaneDraft laneDraft{};
    laneDraft.destination.controller = 74U;
    assert(seq::createSequencerCcLane(*liveLanes, 0U, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*liveLanes, 0U, 0U, 11U).changed());
    sequencer.pattern().bumpCcLaneRevision();

    uint8_t index = bank.refresh();
    bank.commit(index);
    assert(bank.activeSnapshot().tracks[0].length == 8U);
    assert(bank.activeSnapshot().tracks[0].note[0] == 60U);

    const auto openingPath = seq::capturePreparedSequencerGraphContentPath(sequencer);
    assert(sequencer.quickControlsDraft.begin(
        sequencer.pattern(),
        sequencer.clip(),
        openingPath,
        sequencer.page.get(),
        sequencer.focusedStep.get()));
    auto& draft = seq::authoringPattern(sequencer);
    auto& draftClip = seq::authoringClip(sequencer);
    assert(seq::resizeClipPatternContent(draft, draftClip, 12U));
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


void test_resident_and_document_publications_converge() {
    namespace seq = core::state::sequencer;
    seq::SequencerTrackBankState tracks;
    seq::SequencerState editor{tracks.track(tracks.activeTrackIndex()), tracks.clip(tracks.activeTrackIndex())};

    core::state::project::ProjectNavigationState navigation;
    core::sequencer::SequencerRuntimeSnapshotBank resident{editor, tracks, navigation};
    core::sequencer::SequencerRuntimeSnapshotBank documents{editor, tracks, navigation};
    std::array<seq::SequencerClipDocument, 16> clips{};
    seq::SequencerClipRuntimeSources sources{};
    tracks.syncSharedTrackState(0xFFFF, 0);
    for (uint8_t track = 0; track < clips.size(); ++track) {
        auto& pattern = tracks.track(track);
        auto& clip = tracks.clip(track);
        assert(seq::resizeClipPatternContent(pattern, clip, 32));
        pattern.note[0] = 40 + track;
        pattern.note.back() = 80 + track;
        pattern.velocity[track] = 90;
        pattern.gate[track] = 125;
        pattern.nudge[track] = -12;
        pattern.probability[track] = 73;
        pattern.bumpStepDataRevision();
        pattern.swingOffsetPercent.set(track % 2 ? -50 : 50);
        pattern.scalePolicy = track % 2 ? seq::SequencerPatternScalePolicy::OVERRIDE
                                       : seq::SequencerPatternScalePolicy::INHERIT_PROJECT;
        pattern.scaleOverride = {9, StepSequencerScaleType::NaturalMinor,
            StepSequencerScaleConstraintMode::ConstrainDown};
        pattern.pitchEditMode = track % 3 ? seq::SequencerPitchEditMode::FOLLOW_SCALE
                                         : seq::SequencerPitchEditMode::CHROMATIC;
        seq::captureSnapshot(pattern, clips[track].pattern);
        seq::captureSnapshot(clip, clips[track].clip);
        // Document effective values are stale derived data, not runtime authority.
        clips[track].pattern.effectiveSwingPercent = 255;
        clips[track].pattern.effectiveScaleSettings.root = 255;
        sources[track] = {{track, 1}, 1, &clips[track], true};
    }
    for (uint8_t round = 0; round < 12; ++round) {
        navigation.transportSwingPercent = round % 2 ? 70 : 10;
        tracks.setProjectScaleSettings({round, StepSequencerScaleType::Major,
            StepSequencerScaleConstraintMode::ConstrainNearest});
        // Project scale mutation bumps inherited resident revisions. Give the
        // equivalent documents those revisions before comparing full signatures.
        for (uint8_t track = 0; track < clips.size(); ++track) {
            clips[track].pattern.patternScaleRevision =
                tracks.track(track).patternScaleRevision.get();
        }
        // Update both alternating buffers, then exercise both cache hits.
        for (unsigned replay = 0; replay < 4; ++replay) {
            const auto oldActive = documents.activeIndex();
            const auto oldNote = documents.activeSnapshot().tracks[0].note[0];
            const auto a = resident.refresh();
            const auto b = documents.refresh(sources);
            assert(resident.lastRefreshSucceeded() && documents.lastRefreshSucceeded());
            assert(documents.activeIndex() == oldActive);
            assert(documents.activeSnapshot().tracks[0].note[0] == oldNote);
            for (uint8_t track = 0; track < clips.size(); ++track) {
                const auto& x = resident.snapshot(a).tracks[track];
                const auto& y = documents.snapshot(b).tracks[track];
                assert(core::sequencer::captureRuntimeStateSignature(x, resident.snapshot(a).clips[track])
                    .matches(core::sequencer::captureRuntimeStateSignature(y, documents.snapshot(b).clips[track])));
                assert(x.note == y.note && x.velocity == y.velocity && x.gate == y.gate);
                assert(x.nudge == y.nudge && x.probability == y.probability);
                assert(x.scalePolicy == y.scalePolicy && sameScale(x.scaleOverride, y.scaleOverride));
                assert(x.swingOffsetPercent == y.swingOffsetPercent && x.pitchEditMode == y.pitchEditMode);
            }
            resident.commit(a);
            documents.commit(b);
        }
    }
    // Equal authored revisions still refresh when the Clip generation changes.
    clips[0].pattern.note[0] = 99;
    auto next = documents.refresh(sources);
    documents.commit(next);
    assert(documents.activeSnapshot().tracks[0].note[0] == 40);
    ++sources[0].generation;
    for (unsigned replay = 0; replay < 2; ++replay) {
        next = documents.refresh(sources);
        documents.commit(next);
        assert(documents.activeSnapshot().tracks[0].note[0] == 99);
    }
    // Returning to the resident Clip must replace both cached document payloads.
    sources[0] = {{0, 0}, sources[0].generation + 1, nullptr, true};
    for (unsigned replay = 0; replay < 2; ++replay) {
        next = documents.refresh(sources);
        documents.commit(next);
        assert(documents.activeSnapshot().tracks[0].note[0] == 40);
    }
    std::cout << "[PASS] resident/document parity: 16 tracks, project scale/swing, cache and generations\n";
}

}  // namespace

int main() {
    test_resident_and_document_publications_converge();
    test_refresh_captures_active_editor_state();
    test_refresh_preserves_active_snapshot_until_commit();
    test_refresh_keeps_alternating_buffers_current_without_full_copy();
    test_invalidate_refreshes_both_buffers_after_project_replacement();
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
