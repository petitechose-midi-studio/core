#include <cassert>
#include <iostream>

#include <oc/note/clock/ClockConstants.hpp>

#include "state/sequencer/SequencerClipLaunchQueue.hpp"

namespace seq = core::state::sequencer;

namespace {

seq::SequencerClipDocumentPtr makeDocument() {
    auto document = core::app::makeExtmemUniqueCold<seq::SequencerClipDocument>();
    assert(document);
    return document;
}

void install(
    seq::SequencerClipGridState& clips,
    uint8_t slot,
    uint8_t track = 0U
) {
    auto document = makeDocument();
    assert(clips.installInactiveDocument({track, slot}, std::move(document)));
}

void stage(
    seq::SequencerClipLaunchQueue& queue,
    const seq::SequencerClipGridState& clips,
    uint8_t previousIndex = 0U,
    uint8_t targetIndex = 1U
) {
    const auto publication = queue.captureRuntimePublication(clips, true);
    assert(!publication.empty());
    queue.applyRuntimePublication(publication, previousIndex, targetIndex);
}

void test_quantized_lifecycle_and_replacement_rules() {
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);
    install(clips, 1U);
    install(clips, 2U);

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    assert(queue.activeSlot(0U) == 0U);

    assert(queue.request(
        {0U, 1U}, clips, true, seq::SequencerClipLaunchQuantization::BAR));
    assert(queue.request(
        {0U, 2U}, clips, true, seq::SequencerClipLaunchQuantization::BEAT));
    auto telemetry = queue.telemetry(0U);
    assert(telemetry.status == seq::SequencerClipLaunchStatus::QUEUED);
    assert(telemetry.queuedSlot == 2U);
    assert(telemetry.quantization == seq::SequencerClipLaunchQuantization::BEAT);

    stage(queue, clips);
    const auto firstStaged = queue.realtimeView(0U);
    assert(firstStaged.disposition ==
           seq::SequencerClipLaunchRealtimeView::Disposition::STAGED);
    assert(queue.request({0U, 1U}, clips, true));
    const auto rollback = queue.captureRollbackPublication();
    assert(rollback.trackMask == 0x0001U);
    assert(queue.realtimeView(0U).disposition ==
           seq::SequencerClipLaunchRealtimeView::Disposition::FROZEN);
    queue.applyRollbackPublication(rollback);
    stage(queue, clips);
    const auto staged = queue.realtimeView(0U);
    assert(staged.slot == 1U);

    assert(queue.markAppliedFromRealtime(0U, staged.generation));
    assert(queue.publishRealtimeTelemetry() == 0x0001U);
    telemetry = queue.telemetry(0U);
    assert(telemetry.status == seq::SequencerClipLaunchStatus::APPLIED);
    assert(telemetry.activeSlot == 1U);
    assert(telemetry.queuedSlot == seq::SequencerClipGridState::INVALID_SLOT);
}

void test_stale_target_is_cancelled_before_runtime_publication() {
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);
    install(clips, 1U);

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    assert(queue.request({0U, 1U}, clips, true));
    assert(clips.markInactiveDocumentMutated({0U, 1U}));

    assert(queue.captureRuntimePublication(clips, true).empty());
    assert(queue.telemetry(0U).status ==
           seq::SequencerClipLaunchStatus::CANCELLED);
    assert(queue.activeSlot(0U) == 0U);
}

void test_missing_active_clip_queues_resident_fallback() {
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);
    install(clips, 1U);

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    assert(queue.request({0U, 1U}, clips, false));
    stage(queue, clips);
    const auto staged = queue.realtimeView(0U);
    assert(queue.markAppliedFromRealtime(0U, staged.generation));
    assert(queue.publishRealtimeTelemetry() == 0x0001U);
    assert(clips.removeInactiveDocument({0U, 1U}));

    const auto fallback = queue.captureRuntimePublication(clips, true);
    assert(fallback.queuedMask == 0x0001U);
    assert(fallback.slots[0U] == clips.residentSlot(0U));
    assert(fallback.quantizations[0U] ==
           seq::SequencerClipLaunchQuantization::BAR);
}

void applyQueued(
    seq::SequencerClipLaunchQueue& queue,
    const seq::SequencerClipGridState& clips,
    uint16_t expectedMask,
    uint32_t tick = 0U
) {
    const auto publication = queue.captureRuntimePublication(clips, true);
    assert(publication.queuedMask == expectedMask);
    queue.applyRuntimePublication(publication, 0U, 1U);
    for (uint8_t track = 0U;
         track < seq::SequencerClipLaunchQueue::TRACK_COUNT;
         ++track) {
        if ((expectedMask & static_cast<uint16_t>(1U << track)) == 0U) continue;
        assert(queue.markAppliedFromRealtime(
            track,
            publication.generations[track],
            tick
        ));
    }
    assert(queue.publishRealtimeTelemetry() == expectedMask);
}

void test_stop_is_immediate_while_idle_and_new_tracks_are_synchronized() {
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    clips.synchronizeEnabledTracks(0x0003U);
    queue.synchronizeEnabledTracks(clips, 0x0003U);
    assert(queue.activeSlot(1U) == clips.residentSlot(1U));

    assert(queue.requestStop(
        1U,
        false,
        seq::SequencerClipLaunchQuantization::BAR
    ));
    const auto telemetry = queue.telemetry(1U);
    assert(telemetry.status == seq::SequencerClipLaunchStatus::APPLIED);
    assert(telemetry.action == seq::SequencerClipLaunchAction::STOP);
    assert(telemetry.origin == seq::SequencerClipLaunchOrigin::DIRECT_STOP);
    assert(telemetry.stopped);
    assert(queue.stopped(1U));
    assert(queue.pendingTrackMask() == 0U);
}

void test_enabled_empty_track_releases_stale_active_clip() {
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    assert(queue.activeSlot(0U) == 0U);
    assert(clips.clearResident({0U, 0U}));

    clips.synchronizeEnabledTracks(0x0001U);
    assert(clips.residentSlot(0U) == seq::SequencerClipGridState::INVALID_SLOT);
    queue.synchronizeEnabledTracks(clips, 0x0001U);
    assert(queue.activeSlot(0U) == seq::SequencerClipGridState::INVALID_SLOT);
    assert(queue.stopped(0U));
}

void test_stopping_one_track_preserves_other_track_progress() {
    constexpr uint32_t kBar = 4U * oc::note::clock::PPQN;
    seq::SequencerClipGridState clips;
    clips.reset(0x0003U);

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0003U);
    queue.updateTransportPosition(kBar / 4U, true);
    assert(queue.telemetry(0U).activePhaseQ8 == 64U);
    assert(queue.telemetry(1U).activePhaseQ8 == 64U);

    assert(queue.requestStop(
        1U,
        true,
        seq::SequencerClipLaunchQuantization::IMMEDIATE
    ));
    applyQueued(queue, clips, 0x0002U, kBar / 4U);

    assert(!queue.stopped(0U));
    assert(queue.activeSlot(0U) == clips.residentSlot(0U));
    assert(queue.telemetry(0U).activePhaseQ8 == 64U);
    assert(queue.stopped(1U));

    queue.updateTransportPosition(kBar / 2U, true);
    assert(queue.telemetry(0U).activePhaseQ8 == 128U);
    assert(queue.telemetry(1U).activePhaseQ8 == 0U);
}

void test_scene_plan_keeps_empty_tracks_and_supports_cancel_replace_stop() {
    seq::SequencerClipGridState clips;
    clips.reset(0x0003U);
    install(clips, 1U, 0U);
    assert(clips.setStop({1U, 1U}));
    install(clips, 2U, 0U);

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0003U);
    queue.updateTransportPosition(1U, true);
    assert(queue.requestScene(1U, clips, 0x0003U, true));
    assert(queue.telemetry(0U).queuedRemainingQ8 == 253U);
    auto publication = queue.captureRuntimePublication(clips, true);
    assert(publication.queuedMask == 0x0003U);
    assert(publication.actions[0U] == seq::SequencerClipLaunchAction::CLIP);
    assert(publication.actions[1U] == seq::SequencerClipLaunchAction::STOP);
    assert(queue.sceneTelemetry().queuedScene == 1U);

    // A second press on the same queued Scene is its explicit cancel gesture.
    assert(queue.requestScene(1U, clips, 0x0003U, true));
    assert(queue.pendingTrackMask() == 0U);
    assert(queue.sceneTelemetry().status ==
           seq::SequencerClipLaunchStatus::CANCELLED);

    assert(queue.requestScene(1U, clips, 0x0003U, true));
    assert(queue.requestScene(2U, clips, 0x0003U, true));
    const auto scene = queue.sceneTelemetry();
    assert(scene.status == seq::SequencerClipLaunchStatus::QUEUED);
    assert(scene.queuedScene == 2U);
    assert(scene.replaced);

    // Track 2 / Scene 3 is Empty: replacing the Scene leaves that Track alone.
    publication = queue.captureRuntimePublication(clips, true);
    assert(publication.queuedMask == 0x0001U);
    assert(publication.actions[0U] == seq::SequencerClipLaunchAction::CLIP);
    applyQueued(queue, clips, 0x0001U, 384U);
    assert(queue.sceneTelemetry().activeScene == 2U);
    assert(!queue.stopped(0U));
    assert(!queue.stopped(1U));
}

void test_newest_manual_request_wins_and_direct_stop_has_priority() {
    seq::SequencerClipGridState clips;
    clips.reset(0x0003U);
    install(clips, 1U, 0U);
    install(clips, 1U, 1U);
    install(clips, 2U, 0U);
    install(clips, 2U, 1U);

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0003U);
    queue.updateTransportPosition(1U, true);
    assert(queue.requestScene(1U, clips, 0x0003U, true));
    assert(queue.request({0U, 2U}, clips, true));
    auto track0 = queue.telemetry(0U);
    assert(track0.origin == seq::SequencerClipLaunchOrigin::MANUAL_CLIP);
    assert(track0.queuedSlot == 2U);

    assert(queue.requestStop(
        0U,
        true,
        seq::SequencerClipLaunchQuantization::BAR
    ));
    assert(queue.requestScene(2U, clips, 0x0003U, true));
    track0 = queue.telemetry(0U);
    assert(track0.action == seq::SequencerClipLaunchAction::STOP);
    assert(track0.origin == seq::SequencerClipLaunchOrigin::DIRECT_STOP);
    assert(queue.telemetry(1U).queuedSlot == 2U);
}

void test_clip_and_scene_follow_actions_obey_priority() {
    constexpr uint32_t kBeat = oc::note::clock::PPQN;
    constexpr uint32_t kBar = 4U * kBeat;

    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);
    install(clips, 1U);
    install(clips, 2U);
    install(clips, 3U);
    assert(clips.setClipBehavior({0U, 1U}, {
        .length = 1U,
        .follow = seq::sequencerLauncherFollowTarget(3U),
        .quantization = seq::SequencerLauncherFollowQuantization::BEAT,
    }));
    assert(clips.setSceneBehavior(1U, {
        .length = 1U,
        .follow = seq::sequencerLauncherFollowTarget(2U),
        .quantization = seq::SequencerLauncherFollowQuantization::BAR,
    }));

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    assert(queue.requestScene(1U, clips, 0x0001U, false));
    applyQueued(queue, clips, 0x0001U, 0U);
    assert(queue.sceneTelemetry().activeScene == 1U);
    queue.updateTransportPosition(0U, true);
    assert(queue.sceneTelemetry().activeRemainingQ8 == 255U);

    queue.updateTransportPosition(kBar / 2U, true);
    assert(queue.sceneTelemetry().activeRemainingQ8 == 128U);

    // Both deadlines expire together. Scene follow outranks Clip follow.
    queue.updateTransportPosition(kBar, true);
    const std::array<uint16_t, seq::SequencerClipLaunchQueue::TRACK_COUNT>
        loopTicks = {kBar};
    queue.processFollowActions(clips, 0x0001U, true, &loopTicks);
    auto telemetry = queue.telemetry(0U);
    assert(telemetry.status == seq::SequencerClipLaunchStatus::QUEUED);
    assert(telemetry.origin == seq::SequencerClipLaunchOrigin::SCENE_FOLLOW);
    assert(telemetry.queuedSlot == 2U);
    assert(queue.sceneTelemetry().queuedScene == 2U);
    assert(queue.realtimeView(0U).dueTick == kBar);

    // Any newer manual launch then supersedes the automatic Scene follow.
    assert(queue.request({0U, 3U}, clips, true));
    telemetry = queue.telemetry(0U);
    assert(telemetry.origin == seq::SequencerClipLaunchOrigin::MANUAL_CLIP);
    assert(telemetry.queuedSlot == 3U);
    assert(queue.sceneTelemetry().queuedScene ==
           seq::SequencerClipGridState::INVALID_SLOT);
}

void test_two_bar_scene_follows_after_exactly_two_bars() {
    constexpr uint32_t kBar = 4U * oc::note::clock::PPQN;
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);
    install(clips, 1U);
    install(clips, 2U);
    assert(clips.setSceneBehavior(1U, {
        .length = 2U,
        .follow = seq::sequencerLauncherFollowTarget(2U),
        .quantization = seq::SequencerLauncherFollowQuantization::BAR,
    }));

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    assert(queue.requestScene(1U, clips, 0x0001U, false));
    applyQueued(queue, clips, 0x0001U, 0U);

    queue.updateTransportPosition(kBar, true);
    queue.processFollowActions(clips, 0x0001U, true);
    assert(queue.pendingTrackMask() == 0U);
    assert(queue.sceneTelemetry().activeScene == 1U);
    assert(queue.sceneTelemetry().activeRemainingQ8 >= 127U);

    queue.updateTransportPosition(2U * kBar, true);
    queue.processFollowActions(clips, 0x0001U, true);
    assert(queue.pendingTrackMask() == 0x0001U);
    assert(queue.sceneTelemetry().queuedScene == 2U);
    assert(queue.telemetry(0U).origin ==
           seq::SequencerClipLaunchOrigin::SCENE_FOLLOW);
}

void test_active_phase_and_live_behavior_follow_the_running_clip() {
    constexpr uint32_t kBar = 4U * oc::note::clock::PPQN;
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);
    install(clips, 1U);
    const seq::SequencerLauncherBehavior behavior{
        .length = 1U,
        .follow = seq::sequencerLauncherFollowTarget(1U),
        .quantization = seq::SequencerLauncherFollowQuantization::BEAT,
    };
    assert(clips.setClipBehavior({0U, 0U}, behavior));

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    queue.updateTransportPosition(kBar / 2U, true);
    assert(queue.telemetry(0U).activePhaseQ8 == 128U);
    assert(queue.telemetry(0U).activeRemainingQ8 == 128U);
    assert(queue.telemetry(0U).activeElapsedTicks == kBar / 2U);
    queue.updateTransportPosition(kBar, true);
    assert(queue.telemetry(0U).activePhaseQ8 == 0U);
    assert(queue.telemetry(0U).activeRemainingQ8 == 0U);
    assert(queue.telemetry(0U).activeElapsedTicks == kBar);

    queue.processFollowActions(clips, 0x0001U, true);
    const auto telemetry = queue.telemetry(0U);
    assert(telemetry.status == seq::SequencerClipLaunchStatus::QUEUED);
    assert(telemetry.origin == seq::SequencerClipLaunchOrigin::CLIP_FOLLOW);
    assert(telemetry.queuedSlot == 1U);
}

uint8_t queuedClipForFollowChoice(
    seq::SequencerLauncherFollowChoice choice,
    uint8_t current = 0U
) {
    constexpr uint32_t kBar = 4U * oc::note::clock::PPQN;
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);
    install(clips, 1U);
    install(clips, 3U);
    assert(clips.setStop({0U, 2U}));
    assert(clips.setClipBehavior({0U, current}, {
        .length = 1U,
        .follow = choice,
        .quantization = seq::SequencerLauncherFollowQuantization::BAR,
    }));

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    if (current != 0U) {
        assert(queue.request({0U, current}, clips, false));
        applyQueued(queue, clips, 0x0001U, 0U);
    }
    queue.updateTransportPosition(kBar, true);
    queue.processFollowActions(clips, 0x0001U, true);
    return queue.telemetry(0U).queuedSlot;
}

void test_relative_clip_follow_choices_are_sparse_and_deterministic() {
    assert(queuedClipForFollowChoice(
        seq::SequencerLauncherFollowChoice::NEXT) == 1U);
    assert(queuedClipForFollowChoice(
        seq::SequencerLauncherFollowChoice::PREVIOUS) == 3U);
    assert(queuedClipForFollowChoice(
        seq::SequencerLauncherFollowChoice::PREVIOUS, 3U) == 1U);
    assert(queuedClipForFollowChoice(
        seq::SequencerLauncherFollowChoice::FIRST) == 0U);

    const uint8_t randomOther = queuedClipForFollowChoice(
        seq::SequencerLauncherFollowChoice::RANDOM_OTHER);
    assert(randomOther == 1U || randomOther == 3U);
    assert(randomOther == queuedClipForFollowChoice(
        seq::SequencerLauncherFollowChoice::RANDOM_OTHER));

    const uint8_t randomAny = queuedClipForFollowChoice(
        seq::SequencerLauncherFollowChoice::RANDOM_ANY);
    assert(randomAny == 0U || randomAny == 1U || randomAny == 3U);
    assert(randomAny == queuedClipForFollowChoice(
        seq::SequencerLauncherFollowChoice::RANDOM_ANY));
}

void test_scene_next_skips_behavior_only_rows() {
    constexpr uint32_t kBar = 4U * oc::note::clock::PPQN;
    seq::SequencerClipGridState clips;
    clips.reset(0x0001U);
    install(clips, 2U);
    assert(clips.setSceneBehavior(0U, {
        .length = 1U,
        .follow = seq::SequencerLauncherFollowChoice::NEXT,
        .quantization = seq::SequencerLauncherFollowQuantization::BAR,
    }));
    assert(clips.setSceneBehavior(1U, {
        .length = 1U,
        .follow = seq::SequencerLauncherFollowChoice::NONE,
        .quantization = seq::SequencerLauncherFollowQuantization::BAR,
    }));
    assert(clips.sceneUsed(1U));

    seq::SequencerClipLaunchQueue queue;
    queue.reset(clips, 0x0001U);
    assert(queue.requestScene(0U, clips, 0x0001U, false));
    applyQueued(queue, clips, 0x0001U, 0U);
    queue.updateTransportPosition(kBar, true);
    queue.processFollowActions(clips, 0x0001U, true);
    assert(queue.sceneTelemetry().queuedScene == 2U);
}

}  // namespace

int main() {
    test_quantized_lifecycle_and_replacement_rules();
    test_stale_target_is_cancelled_before_runtime_publication();
    test_missing_active_clip_queues_resident_fallback();
    test_stop_is_immediate_while_idle_and_new_tracks_are_synchronized();
    test_enabled_empty_track_releases_stale_active_clip();
    test_stopping_one_track_preserves_other_track_progress();
    test_scene_plan_keeps_empty_tracks_and_supports_cancel_replace_stop();
    test_newest_manual_request_wins_and_direct_stop_has_priority();
    test_clip_and_scene_follow_actions_obey_priority();
    test_two_bar_scene_follows_after_exactly_two_bars();
    test_active_phase_and_live_behavior_follow_the_running_clip();
    test_relative_clip_follow_choices_are_sparse_and_deterministic();
    test_scene_next_skips_behavior_only_rows();
    std::cout << "All SequencerClipLaunchQueue tests passed\n";
    return 0;
}
