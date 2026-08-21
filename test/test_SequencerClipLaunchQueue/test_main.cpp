#include <cassert>
#include <iostream>

#include "state/sequencer/SequencerClipLaunchQueue.hpp"

namespace seq = core::state::sequencer;

namespace {

seq::SequencerClipDocumentPtr makeDocument() {
    auto document = core::app::makeExtmemUniqueCold<seq::SequencerClipDocument>();
    assert(document);
    return document;
}

void install(seq::SequencerClipGridState& clips, uint8_t slot) {
    auto document = makeDocument();
    assert(clips.installInactiveDocument({0U, slot}, std::move(document)));
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
    const auto staged = queue.realtimeView(0U);
    assert(staged.disposition ==
           seq::SequencerClipLaunchRealtimeView::Disposition::STAGED);
    assert(!queue.request({0U, 1U}, clips, true));

    assert(queue.markAppliedFromRealtime(0U, staged.generation));
    assert(queue.publishRealtimeTelemetry() == 0x0001U);
    telemetry = queue.telemetry(0U);
    assert(telemetry.status == seq::SequencerClipLaunchStatus::APPLIED);
    assert(telemetry.activeSlot == 2U);
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

}  // namespace

int main() {
    test_quantized_lifecycle_and_replacement_rules();
    test_stale_target_is_cancelled_before_runtime_publication();
    test_missing_active_clip_queues_resident_fallback();
    std::cout << "All SequencerClipLaunchQueue tests passed\n";
    return 0;
}
