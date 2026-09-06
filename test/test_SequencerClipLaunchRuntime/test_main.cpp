#include <cassert>
#include <iostream>
#include <vector>

#include <oc/note/clock/ClockConstants.hpp>

#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"
#include "sequencer/SequencerPlaybackService.hpp"
#include "sequencer/SequencerRuntimeGraphBank.hpp"
#include "sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "state/StatusBarState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerClipLaunchQueue.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace seq = core::state::sequencer;

namespace {

seq::SequencerClipDocumentPtr makeDocument(uint8_t note) {
    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    pattern.note[0U] = note;
    pattern.velocity[0U] = 100U;
    pattern.gate[0U] = 100U;
    pattern.setEnabled(0U, true);
    pattern.bumpStepDataRevision();
    seq::SequencerClipDocumentPtr document;
    assert(seq::captureSequencerClipDocument(
        pattern,
        clip,
        seq::SequencerTrackKind::INSTRUMENT,
        nullptr,
        document));
    return document;
}

core::sequencer::ProjectTrackRuntimeSnapshot projectTracks() {
    core::sequencer::ProjectTrackRuntimeSnapshot snapshot{};
    snapshot.revision = 1U;
    snapshot.enabledMask = 0x0001U;
    snapshot.audibleMask = 0x0001U;
    snapshot.midiChannels.fill(0U);
    return snapshot;
}

struct Fixture {
    seq::SequencerState sequencer;
    seq::SequencerTrackBankState tracks;
    seq::SequencerClipGridState clips;
    seq::SequencerClipLaunchQueue launches;
    core::state::project::ProjectNavigationState navigation;
    core::state::StatusBarState status;
    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank graphs;
    core::sequencer::SequencerRuntimeSnapshotBank snapshots{
        sequencer, tracks, navigation,
    };
    core::sequencer::SequencerPlaybackService playback{
        sequencer,
        status,
        midiQueue,
        graphs,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &snapshots,
        &launches,
    };

    Fixture() {
        sequencer.pattern.note[0U] = 60U;
        sequencer.pattern.velocity[0U] = 100U;
        sequencer.pattern.gate[0U] = 100U;
        sequencer.pattern.setEnabled(0U, true);
        sequencer.pattern.bumpStepDataRevision();
        tracks.reset();
        clips.reset(0x0001U);
        assert(clips.installInactiveDocument({0U, 1U}, makeDocument(72U)));
        launches.reset(clips, 0x0001U);
        publishCurrent(0U);
    }

    uint8_t publishCurrent(uint16_t retainMask) {
        seq::SequencerClipRuntimeSources sources{};
        assert(launches.captureRuntimeSources(clips, sources));
        assert(graphs.prepare(sequencer, tracks, sources, retainMask));
        const uint8_t previous = snapshots.activeIndex();
        const uint8_t target = snapshots.refresh(sources);
        assert(snapshots.lastRefreshSucceeded());
        const auto publication = launches.captureRuntimePublication(clips, true);
        graphs.publishPrepared([&]() {
            snapshots.commit(target);
            launches.applyRuntimePublication(publication, previous, target);
        });
        return target;
    }

    void queueAndStage(seq::SequencerClipLaunchQuantization quantization) {
        assert(launches.request({0U, 1U}, clips, true, quantization));
        publishCurrent(0x0001U);
        assert(launches.realtimeView(0U).disposition ==
               seq::SequencerClipLaunchRealtimeView::Disposition::STAGED);
    }

    void play(uint32_t tick, bool playing = true) {
        const auto index = snapshots.activeIndex();
        playback.update(snapshots.activeSnapshot(), tick, playing,
            1000U + tick * 1000U, 1000U, projectTracks(), false,
            snapshots.laneSnapshot(index), false, snapshots.drumSnapshot(index));
        launches.updateTransportPosition(tick, playing);
    }
};

struct RecordedEvents : core::sequencer::RealtimeMidiQueueLifecycleObserver {
    std::vector<core::sequencer::RealtimeMidiEvent> events;
    void onRealtimeMidiEventEnqueued(const core::sequencer::RealtimeMidiEvent& event) override {
        events.push_back(event);
    }
    void onRealtimeMidiEventRemoved(const core::sequencer::RealtimeMidiEvent&,
        core::sequencer::RealtimeMidiQueueLifecycleReason) override {}
    void onRealtimeMidiEventDispatched(const core::sequencer::RealtimeMidiEvent&) override {}
};

void test_beat_launch_starts_content_at_zero_with_physical_deadlines() {
    Fixture fixture;
    RecordedEvents recorded;
    fixture.midiQueue.attachLifecycleObserver(recorded);
    fixture.queueAndStage(seq::SequencerClipLaunchQuantization::BEAT);
    fixture.play(18U);
    assert(!fixture.playback.takeUiProjectionSnapshot().drumPlaying);
    recorded.events.clear();
    fixture.play(24U);
    assert(fixture.playback.copyActiveRuntimeTelemetry().playheadStep == 0);
    assert(fixture.launches.telemetry(0U).activeElapsedTicks == 0U);
    bool heardStart = false;
    for (const auto& event : recorded.events) {
        if (event.type == core::sequencer::RealtimeMidiEventType::NoteOn) {
            assert(event.note == 72U);
            assert(event.deadlineUs == 25000U);
            heardStart = true;
        }
    }
    assert(heardStart);
    fixture.play(30U);
    assert(fixture.playback.copyActiveRuntimeTelemetry().playheadStep == 1);
    assert(fixture.launches.playbackTick(0U, 30U) == 6U);
    assert(fixture.launches.playbackTick(1U, 30U) == 30U);
    fixture.play(30U, false);
    fixture.play(0U);
    assert(fixture.playback.copyActiveRuntimeTelemetry().playheadStep == 0);
    assert(fixture.launches.telemetry(0U).activeElapsedTicks == 0U);
    fixture.play(24U);
    assert(fixture.playback.copyActiveRuntimeTelemetry().playheadStep == 4);
    fixture.midiQueue.detachLifecycleObserver(recorded);
}

void test_drum_launch_uses_local_polymetric_lanes() {
    Fixture fixture;
    seq::DrumTrackState drum;
    drum.reset();
    assert(drum.kit.setLaneCount(2U));
    assert(drum.pattern.setLaneTimingCustom(0U, 7U, 4U));
    assert(drum.pattern.setLaneTimingCustom(1U, 8U, 2U));
    assert(drum.pattern.setStepEnabled(0U, 0U, true));
    assert(drum.pattern.setStepEnabled(1U, 0U, true));
    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    seq::SequencerClipDocumentPtr document;
    assert(seq::captureSequencerClipDocument(pattern, clip,
        seq::SequencerTrackKind::DRUM, &drum, document));
    assert(fixture.clips.removeInactiveDocument({0U, 1U}));
    assert(fixture.clips.installInactiveDocument({0U, 1U}, std::move(document)));
    RecordedEvents recorded;
    fixture.midiQueue.attachLifecycleObserver(recorded);
    fixture.queueAndStage(seq::SequencerClipLaunchQuantization::BEAT);
    fixture.play(18U);
    assert(!fixture.playback.takeUiProjectionSnapshot().drumPlaying);
    recorded.events.clear();
    fixture.play(24U);
    auto ui = fixture.playback.takeUiProjectionSnapshot();
    assert(ui.drumLaneSteps[0] == 0U && ui.drumLaneSteps[1] == 0U);
    unsigned notes = 0;
    for (const auto& event : recorded.events) {
        if (event.type == core::sequencer::RealtimeMidiEventType::NoteOn) {
            assert(event.deadlineUs == 25000U);
            ++notes;
        }
    }
    assert(notes == 2U);
    for (uint32_t tick = 25U; tick <= 66U; ++tick) fixture.play(tick);
    ui = fixture.playback.takeUiProjectionSnapshot();
    assert(ui.drumLaneSteps[0] == 0U); // local 42: seven six-tick steps
    assert(ui.drumLaneSteps[1] == 3U); // same origin, twelve ticks per step
    fixture.midiQueue.detachLifecycleObserver(recorded);
}

void test_bar_launch_waits_for_bar_boundary() {
    Fixture fixture;
    fixture.queueAndStage(seq::SequencerClipLaunchQuantization::BAR);
    const auto routes = projectTracks();
    const uint8_t index = fixture.snapshots.activeIndex();
    const auto& snapshot = fixture.snapshots.activeSnapshot();

    fixture.playback.update(
        snapshot,
        oc::note::clock::PPQN,
        true,
        1000U,
        1000U,
        routes,
        false,
        fixture.snapshots.laneSnapshot(index),
        false,
        fixture.snapshots.drumSnapshot(index));
    assert(fixture.launches.realtimeView(0U).disposition ==
           seq::SequencerClipLaunchRealtimeView::Disposition::STAGED);

    fixture.playback.update(
        snapshot,
        4U * oc::note::clock::PPQN,
        true,
        2000U,
        1000U,
        routes,
        false,
        fixture.snapshots.laneSnapshot(index),
        false,
        fixture.snapshots.drumSnapshot(index));
    assert(fixture.launches.realtimeView(0U).disposition ==
           seq::SequencerClipLaunchRealtimeView::Disposition::NORMAL);
    assert(fixture.launches.activeSlot(0U) == 1U);
    assert(fixture.launches.publishRealtimeTelemetry() == 0x0001U);
}

void test_beat_launch_applies_on_beat_boundary() {
    Fixture fixture;
    fixture.queueAndStage(seq::SequencerClipLaunchQuantization::BEAT);
    const auto routes = projectTracks();
    const uint8_t index = fixture.snapshots.activeIndex();
    fixture.playback.update(
        fixture.snapshots.activeSnapshot(),
        oc::note::clock::PPQN,
        true,
        1000U,
        1000U,
        routes,
        false,
        fixture.snapshots.laneSnapshot(index),
        false,
        fixture.snapshots.drumSnapshot(index));
    assert(fixture.launches.activeSlot(0U) == 1U);
}

}  // namespace

int main() {
    test_drum_launch_uses_local_polymetric_lanes();
    test_beat_launch_starts_content_at_zero_with_physical_deadlines();
    test_bar_launch_waits_for_bar_boundary();
    test_beat_launch_applies_on_beat_boundary();
    std::cout << "All SequencerClipLaunchRuntime tests passed\n";
    return 0;
}
