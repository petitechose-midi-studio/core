#include <cassert>
#include <iostream>

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
};

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
    test_bar_launch_waits_for_bar_boundary();
    test_beat_launch_applies_on_beat_boundary();
    std::cout << "All SequencerClipLaunchRuntime tests passed\n";
    return 0;
}
