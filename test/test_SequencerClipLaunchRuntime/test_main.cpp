#include <cassert>
#include <iostream>
#include <vector>

#include <oc/note/clock/ClockConstants.hpp>
#include <oc/impl/NullMidi.hpp>

#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"
#include "sequencer/MidiCcGlobalFrameCoordinator.hpp"
#include "sequencer/SequencerPlaybackService.hpp"
#include "sequencer/SequencerRuntimeGraphBank.hpp"
#include "sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "state/StatusBarState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerClipLaunchQueue.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerClipRegionOps.hpp"
#include "support/AdvancingMicrosClock.hpp"

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
    seq::SequencerTrackBankState tracks;
    seq::SequencerState sequencer{tracks.track(tracks.activeTrackIndex()), tracks.clip(tracks.activeTrackIndex())};

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
        sequencer.pattern().note[0U] = 60U;
        sequencer.pattern().velocity[0U] = 100U;
        sequencer.pattern().gate[0U] = 100U;
        sequencer.pattern().setEnabled(0U, true);
        sequencer.pattern().bumpStepDataRevision();
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

void test_late_launch_keeps_intended_phase_without_historical_note_burst() {
    Fixture fixture;
    RecordedEvents recorded;
    fixture.midiQueue.attachLifecycleObserver(recorded);
    fixture.queueAndStage(seq::SequencerClipLaunchQuantization::BAR);
    fixture.play(97U);
    assert(fixture.launches.telemetry(0U).activeElapsedTicks == 1U);
    assert(fixture.playback.copyActiveRuntimeTelemetry().playheadStep == 0);
    for (const auto& event : recorded.events) {
        assert(event.type != core::sequencer::RealtimeMidiEventType::NoteOn);
    }
    fixture.play(102U);
    assert(fixture.playback.copyActiveRuntimeTelemetry().playheadStep == 1);
    fixture.midiQueue.detachLifecycleObserver(recorded);
}

void test_follow_chain_does_not_accumulate_foreground_delay() {
    Fixture fixture;
    const seq::SequencerLauncherBehavior behavior{
        .length = 1U,
        .follow = seq::SequencerLauncherFollowChoice::NEXT,
        .quantization = seq::SequencerLauncherFollowQuantization::BAR,
    };
    assert(fixture.clips.setClipBehavior({0U, 0U}, behavior));
    assert(fixture.clips.setClipBehavior({0U, 1U}, behavior));
    fixture.launches.reset(fixture.clips, 1U);
    for (uint32_t transition = 1U; transition <= 32U; ++transition) {
        const uint32_t due = transition * 96U;
        fixture.launches.updateTransportPosition(due - 1U, true);
        fixture.launches.processFollowActions(fixture.clips, 1U, true);
        assert(fixture.launches.realtimeView(0U).dueTick == due);
        fixture.publishCurrent(1U);
        fixture.play(due + 1U); // every foreground publication is late
        assert(fixture.launches.telemetry(0U).activeElapsedTicks == 1U);
        assert(fixture.launches.activeSlot(0U) == transition % 2U);
        fixture.graphs.releaseRetired(fixture.launches.publishRealtimeTelemetry());
    }
}

void test_late_scene_stop_member_keeps_the_shared_follow_origin() {
    Fixture fixture;
    fixture.clips.synchronizeEnabledTracks(3U);
    assert(fixture.clips.setStop({1U, 1U}));
    assert(fixture.clips.setSceneBehavior(1U, {
        .length = 1U,
        .follow = seq::SequencerLauncherFollowChoice::NEXT,
        .quantization = seq::SequencerLauncherFollowQuantization::BAR,
    }));
    fixture.launches.reset(fixture.clips, 3U);
    fixture.launches.updateTransportPosition(1U, true);
    assert(fixture.launches.requestScene(1U, fixture.clips, 3U, true));
    fixture.publishCurrent(3U);
    auto routes = projectTracks();
    routes.enabledMask = routes.audibleMask = 3U;
    const auto index = fixture.snapshots.activeIndex();
    fixture.playback.update(fixture.snapshots.activeSnapshot(), 97U, true,
        98000U, 1000U, routes, false, fixture.snapshots.laneSnapshot(index),
        false, fixture.snapshots.drumSnapshot(index));
    fixture.launches.publishRealtimeTelemetry();
    assert(fixture.launches.activeSlot(0U) == 1U);
    assert(fixture.launches.stopped(1U));
    assert(fixture.launches.sceneTelemetry().activeScene == 1U);
    fixture.launches.updateTransportPosition(191U, true);
    fixture.launches.processFollowActions(fixture.clips, 3U, true);
    // The Stop member applies last, but must not move the scene origin to 97.
    assert(fixture.launches.pendingTrackMask() != 0U);
    assert(fixture.launches.realtimeView(0U).dueTick == 192U);
}

void test_clip_cc_and_notes_share_intro_loop_and_output_delay() {
    test_support::AdvancingMicrosClock clock;
    clock.install();
    for (const int16_t delay : {-1, 0, 2}) {
        Fixture fixture;
        seq::SequencerPatternState pattern;
        seq::SequencerClipState clip;
        assert(seq::setClipPlaybackRegion(pattern, clip, {8U, 1U, 2U, 7U}));
        pattern.note[1U] = 73U;
        pattern.velocity[1U] = 100U;
        pattern.gate[1U] = 400U;
        pattern.setEnabled(1U, true);
        pattern.bumpStepDataRevision();
        auto* lanes = seq::ensureSequencerCcLaneBank(pattern);
        assert(lanes);
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = 74U;
        assert(seq::createSequencerCcLane(*lanes, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(*lanes, 0U, 1U, 96U).changed());
        pattern.setCcLaneRevision(lanes->revision);
        seq::SequencerClipDocumentPtr document;
        assert(seq::captureSequencerClipDocument(pattern, clip,
            seq::SequencerTrackKind::INSTRUMENT, nullptr, document));
        assert(fixture.clips.removeInactiveDocument({0U, 1U}));
        assert(fixture.clips.installInactiveDocument({0U, 1U}, std::move(document)));
        fixture.queueAndStage(seq::SequencerClipLaunchQuantization::BEAT);
        core::sequencer::SequencerCcLaneRuntime cc;
        core::sequencer::SequencerCcLaneRuntime predictiveCc;
        core::sequencer::MidiCcGlobalFrameCoordinator coordinator{fixture.midiQueue};
        core::sequencer::SequencerPlaybackService playback{
            fixture.sequencer, fixture.status, fixture.midiQueue, fixture.graphs,
            nullptr, &cc, &coordinator, &predictiveCc, &fixture.snapshots, &fixture.launches};
        struct Midi : oc::impl::NullMidi {
            unsigned notes = 0U;
            unsigned noteOffs = 0U;
            bool ccReceived = false;
            MidiOutputAcceptance sendCC(uint8_t, uint8_t controller, uint8_t value) override {
                if (controller == 74U && value == 96U) ccReceived = true;
                return MidiOutputAcceptance::ACCEPTED;
            }
            MidiOutputAcceptance sendNoteOn(uint8_t, uint8_t note, uint8_t) override {
                assert(note == 73U);
                assert(ccReceived);
                ++notes;
                return MidiOutputAcceptance::ACCEPTED;
            }
            MidiOutputAcceptance sendNoteOff(uint8_t, uint8_t note, uint8_t) override {
                if (note == 73U) ++noteOffs;
                return MidiOutputAcceptance::ACCEPTED;
            }
        } midiTransport;
        oc::api::MidiAPI midi{midiTransport};
        auto routes = projectTracks();
        routes.delayMs[0] = delay;
        for (uint32_t tick = 24U; tick <= 66U; ++tick) {
            const auto index = fixture.snapshots.activeIndex();
            const uint32_t now = 1000U + tick * 1000U;
            clock.freezeAt(now);
            playback.update(fixture.snapshots.activeSnapshot(), tick, true,
                now, 1000U, routes, false, fixture.snapshots.laneSnapshot(index),
                true, fixture.snapshots.drumSnapshot(index));
            fixture.midiQueue.drainDue(midi, now, UINT32_MAX);
        }
        assert(midiTransport.ccReceived && midiTransport.notes == 1U);
        assert(midiTransport.noteOffs == 1U);
        assert(playback.copyActiveRuntimeTelemetry().playheadStep == 3);
    }
    oc::time::setMicrosProvider(nullptr);
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
    test_late_scene_stop_member_keeps_the_shared_follow_origin();
    test_clip_cc_and_notes_share_intro_loop_and_output_delay();
    test_follow_chain_does_not_accumulate_foreground_delay();
    test_late_launch_keeps_intended_phase_without_historical_note_burst();
    test_drum_launch_uses_local_polymetric_lanes();
    test_beat_launch_starts_content_at_zero_with_physical_deadlines();
    test_bar_launch_waits_for_bar_boundary();
    test_beat_launch_applies_on_beat_boundary();
    std::cout << "All SequencerClipLaunchRuntime tests passed\n";
    return 0;
}
