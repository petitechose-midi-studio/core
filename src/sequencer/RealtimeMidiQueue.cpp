#include "sequencer/RealtimeMidiQueue.hpp"

#include <algorithm>
#include <cassert>

#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>

#include "state/macro/MacroConstants.hpp"
#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::sequencer {

static_assert(
    RealtimeMidiQueue::MAX_SEQUENCER_CC_EVENTS_PER_FRAME ==
    core::state::sequencer::SequencerTrackBankState::TRACK_COUNT *
        core::state::sequencer::SequencerCcLaneBank::MAX_LANES
);
static_assert(
    RealtimeMidiQueue::MAX_MACRO_CC_EVENTS_PER_FRAME ==
    core::state::macro::MACRO_COUNT
);
static_assert(RealtimeMidiQueue::MAX_QUEUE_DEPTH == 328U);

bool RealtimeMidiQueue::push(const RealtimeMidiEvent& event) {
    return pushBatch(&event, 1).ok();
}

RealtimeMidiQueueBatchResult RealtimeMidiQueue::pushBatch(
    const RealtimeMidiEvent* events,
    size_t count
) {
    return pushBatchImpl_(events, count, false, 0, false);
}

RealtimeMidiQueueBatchResult RealtimeMidiQueue::replaceTrackEventsWithBatch(
    uint8_t trackIndex,
    const RealtimeMidiEvent* events,
    size_t count
) {
    return pushBatchImpl_(events, count, true, trackIndex, false);
}

RealtimeMidiQueueBatchResult RealtimeMidiQueue::replaceControlChangeEventsWithBatch(
    const RealtimeMidiEvent* events,
    size_t count
) {
    return pushBatchImpl_(events, count, false, 0, true);
}

RealtimeMidiQueueBatchResult RealtimeMidiQueue::replaceTrackEventsWithNoteOffBatch(
    uint8_t trackIndex,
    uint32_t deadlineUs,
    const oc::note::sequencer::StepBitMask128* activeNotesByChannel,
    size_t channelCount
) {
    RealtimeMidiQueueBatchResult result{};
    if (trackIndex >= track_observers_.size() ||
        channelCount > 16U ||
        (channelCount > 0 && activeNotesByChannel == nullptr)) {
        result.status = RealtimeMidiQueueBatchStatus::INVALID_INPUT;
        recordRejectedBatch_(nullptr, 0, false);
        return result;
    }

    size_t noteOffCount = 0;
    for (size_t channel = 0; channel < channelCount; ++channel) {
        for (uint8_t note = 0; note < 128U; ++note) {
            if (activeNotesByChannel[channel].test(note)) ++noteOffCount;
        }
    }
    result.requestedCount = static_cast<uint16_t>(
        std::min<size_t>(noteOffCount, UINT16_MAX)
    );
    if (noteOffCount > MAX_QUEUE_DEPTH) {
        result.status = RealtimeMidiQueueBatchStatus::CAPACITY_EXCEEDED;
        diagnostics_.rejectedBatchCount = realtimeMidiSaturatingAdd(
            diagnostics_.rejectedBatchCount,
            1
        );
        diagnostics_.rejectedEventCount = realtimeMidiSaturatingAdd(
            diagnostics_.rejectedEventCount,
            static_cast<uint32_t>(noteOffCount)
        );
        diagnostics_.criticalNoteOffOverflowCount = realtimeMidiSaturatingAdd(
            diagnostics_.criticalNoteOffOverflowCount,
            1
        );
        return result;
    }

    size_t survivorCount = 0;
    size_t existingNoteOnCount = 0;
    size_t existingControlChangeCount = 0;
    for (size_t i = 0; i < count_; ++i) {
        if (events_[i].trackIndex == trackIndex) continue;
        ++survivorCount;
        if (events_[i].type == RealtimeMidiEventType::NoteOn) {
            ++existingNoteOnCount;
        } else if (events_[i].type == RealtimeMidiEventType::ControlChange) {
            ++existingControlChangeCount;
        }
    }
    const size_t totalRequested = survivorCount + noteOffCount;
    const size_t requiredEvictions = totalRequested > MAX_QUEUE_DEPTH
        ? totalRequested - MAX_QUEUE_DEPTH
        : 0;
    if (requiredEvictions > existingNoteOnCount + existingControlChangeCount) {
        result.status = RealtimeMidiQueueBatchStatus::CAPACITY_EXCEEDED;
        diagnostics_.rejectedBatchCount = realtimeMidiSaturatingAdd(
            diagnostics_.rejectedBatchCount,
            1
        );
        diagnostics_.rejectedEventCount = realtimeMidiSaturatingAdd(
            diagnostics_.rejectedEventCount,
            static_cast<uint32_t>(noteOffCount)
        );
        diagnostics_.criticalNoteOffOverflowCount = realtimeMidiSaturatingAdd(
            diagnostics_.criticalNoteOffOverflowCount,
            1
        );
        return result;
    }

    result.displacedNoteOnCount = static_cast<uint16_t>(
        std::min(requiredEvictions, existingNoteOnCount)
    );
    result.displacedControlChangeCount = static_cast<uint16_t>(
        requiredEvictions - result.displacedNoteOnCount
    );

    size_t index = 0;
    while (index < count_) {
        if (events_[index].trackIndex == trackIndex) {
            remove_(index, RealtimeMidiQueueLifecycleReason::TRACK_CANCELLED);
            ++result.cancelledCount;
            continue;
        }
        ++index;
    }
    uint16_t noteOnsToEvict = result.displacedNoteOnCount;
    for (size_t i = count_; i > 0 && noteOnsToEvict > 0; --i) {
        const size_t candidate = i - 1U;
        if (events_[candidate].type != RealtimeMidiEventType::NoteOn) continue;
        remove_(candidate, RealtimeMidiQueueLifecycleReason::DISPLACED_BY_NOTE_OFF);
        --noteOnsToEvict;
    }
    uint16_t controlsToEvict = result.displacedControlChangeCount;
    for (size_t i = count_; i > 0 && controlsToEvict > 0; --i) {
        const size_t candidate = i - 1U;
        if (events_[candidate].type != RealtimeMidiEventType::ControlChange) continue;
        remove_(candidate, RealtimeMidiQueueLifecycleReason::DISPLACED_BY_NOTE_OFF);
        --controlsToEvict;
    }
    assert(noteOnsToEvict == 0 && controlsToEvict == 0);

    for (uint8_t channel = 0; channel < channelCount; ++channel) {
        for (uint8_t note = 0; note < 128U; ++note) {
            if (!activeNotesByChannel[channel].test(note)) continue;
            insertNoFail_(RealtimeMidiEvent{
                .deadlineUs = deadlineUs,
                .type = RealtimeMidiEventType::NoteOff,
                .channel = channel,
                .note = note,
                .velocity = 0,
                .trackIndex = trackIndex,
            });
        }
    }
    diagnostics_.displacedNoteOnCount = realtimeMidiSaturatingAdd(
        diagnostics_.displacedNoteOnCount,
        result.displacedNoteOnCount
    );
    diagnostics_.displacedControlChangeCount = realtimeMidiSaturatingAdd(
        diagnostics_.displacedControlChangeCount,
        result.displacedControlChangeCount
    );
    updateHighWaterMark_();
    result.status = RealtimeMidiQueueBatchStatus::OK;
    return result;
}

RealtimeMidiQueueBatchResult RealtimeMidiQueue::pushBatchImpl_(
    const RealtimeMidiEvent* events,
    size_t count,
    bool cancelTrack,
    uint8_t trackIndex,
    bool cancelControlChanges
) {
    RealtimeMidiQueueBatchResult result{};
    result.requestedCount = static_cast<uint16_t>(
        std::min<size_t>(count, UINT16_MAX)
    );

    if (count > MAX_QUEUE_DEPTH) {
        result.status = RealtimeMidiQueueBatchStatus::CAPACITY_EXCEEDED;
        recordRejectedBatch_(events, count, true);
        return result;
    }
    if ((count > 0 && events == nullptr) ||
        (cancelTrack && trackIndex >= track_observers_.size())) {
        result.status = RealtimeMidiQueueBatchStatus::INVALID_INPUT;
        recordRejectedBatch_(events, count, false);
        return result;
    }

    uint16_t batchNoteOffCount = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!validEvent_(events[i]) ||
            (cancelTrack && events[i].trackIndex != trackIndex) ||
            (cancelControlChanges &&
             events[i].type != RealtimeMidiEventType::ControlChange)) {
            result.status = RealtimeMidiQueueBatchStatus::INVALID_INPUT;
            recordRejectedBatch_(events, count, false);
            return result;
        }
        if (events[i].type == RealtimeMidiEventType::NoteOff) {
            ++batchNoteOffCount;
        }
    }

    size_t survivorCount = 0;
    size_t existingNoteOnCount = 0;
    size_t existingControlChangeCount = 0;
    for (size_t i = 0; i < count_; ++i) {
        if ((cancelTrack && events_[i].trackIndex == trackIndex) ||
            (cancelControlChanges &&
             events_[i].type == RealtimeMidiEventType::ControlChange)) {
            continue;
        }
        ++survivorCount;
        if (events_[i].type == RealtimeMidiEventType::NoteOn) {
            ++existingNoteOnCount;
        } else if (events_[i].type == RealtimeMidiEventType::ControlChange) {
            ++existingControlChangeCount;
        }
    }

    const size_t totalRequested = survivorCount + count;
    const size_t requiredEvictions = totalRequested > MAX_QUEUE_DEPTH
        ? totalRequested - MAX_QUEUE_DEPTH
        : 0;
    if (requiredEvictions > batchNoteOffCount ||
        requiredEvictions > existingNoteOnCount + existingControlChangeCount) {
        result.status = RealtimeMidiQueueBatchStatus::CAPACITY_EXCEEDED;
        recordRejectedBatch_(events, count, true);
        OC_PERF_RECORD(
            "midi.queue.reject-batch",
            0,
            static_cast<uint32_t>(count),
            static_cast<uint32_t>(requiredEvictions)
        );
        return result;
    }

    result.displacedNoteOnCount = static_cast<uint16_t>(
        std::min(requiredEvictions, existingNoteOnCount)
    );
    result.displacedControlChangeCount = static_cast<uint16_t>(
        requiredEvictions - result.displacedNoteOnCount
    );

    // Preflight above proves that every mutation below is no-fail. This makes
    // cancellation/eviction plus insertion one observable transaction.
    if (cancelTrack || cancelControlChanges) {
        size_t index = 0;
        while (index < count_) {
            const bool removeTrack =
                cancelTrack && events_[index].trackIndex == trackIndex;
            const bool removeControl = cancelControlChanges &&
                events_[index].type == RealtimeMidiEventType::ControlChange;
            if (removeTrack || removeControl) {
                remove_(
                    index,
                    removeTrack
                        ? RealtimeMidiQueueLifecycleReason::TRACK_CANCELLED
                        : RealtimeMidiQueueLifecycleReason::SOURCE_REPLACED
                );
                ++result.cancelledCount;
                continue;
            }
            ++index;
        }
    }

    uint16_t noteOnsToEvict = result.displacedNoteOnCount;
    for (size_t i = count_; i > 0 && noteOnsToEvict > 0; --i) {
        const size_t index = i - 1U;
        if (events_[index].type != RealtimeMidiEventType::NoteOn) continue;
        remove_(index, RealtimeMidiQueueLifecycleReason::DISPLACED_BY_NOTE_OFF);
        --noteOnsToEvict;
    }
    uint16_t controlsToEvict = result.displacedControlChangeCount;
    for (size_t i = count_; i > 0 && controlsToEvict > 0; --i) {
        const size_t index = i - 1U;
        if (events_[index].type != RealtimeMidiEventType::ControlChange) continue;
        remove_(index, RealtimeMidiQueueLifecycleReason::DISPLACED_BY_NOTE_OFF);
        --controlsToEvict;
    }
    assert(noteOnsToEvict == 0 && controlsToEvict == 0);

    for (size_t i = 0; i < count; ++i) {
        insertNoFail_(events[i]);
    }
    diagnostics_.displacedNoteOnCount = realtimeMidiSaturatingAdd(
        diagnostics_.displacedNoteOnCount,
        result.displacedNoteOnCount
    );
    diagnostics_.displacedControlChangeCount = realtimeMidiSaturatingAdd(
        diagnostics_.displacedControlChangeCount,
        result.displacedControlChangeCount
    );
    updateHighWaterMark_();
    result.status = RealtimeMidiQueueBatchStatus::OK;
    return result;
}

void RealtimeMidiQueue::insertNoFail_(const RealtimeMidiEvent& event) {
    assert(count_ < events_.size());
    size_t insertIndex = count_;
    while (insertIndex > 0 && comesBefore_(event, events_[insertIndex - 1])) {
        events_[insertIndex] = events_[insertIndex - 1];
        --insertIndex;
    }
    events_[insertIndex] = event;
    ++count_;
    if (lifecycle_observer_ != nullptr) {
        lifecycle_observer_->onRealtimeMidiEventEnqueued(event);
    }
}

uint32_t RealtimeMidiQueue::cancelPendingEvents(uint8_t trackIndex) {
    uint32_t removed = 0;
    size_t index = 0;

    while (index < count_) {
        if (events_[index].trackIndex == trackIndex) {
            remove_(index, RealtimeMidiQueueLifecycleReason::TRACK_CANCELLED);
            removed += 1;
            continue;
        }

        index += 1;
    }

    if (removed > 0) {
        OC_PERF_RECORD("midi.queue.cancel-track", 0, removed, trackIndex);
    }
    return removed;
}

void RealtimeMidiQueue::clear() {
    while (count_ > 0) {
        remove_(count_ - 1U, RealtimeMidiQueueLifecycleReason::QUEUE_CLEARED);
    }
}

void RealtimeMidiQueue::attachTrackObserver(
    uint8_t trackIndex,
    RealtimeMidiQueueDispatchObserver& observer
) {
    if (trackIndex >= track_observers_.size()) return;
    track_observers_[trackIndex] = &observer;
}

void RealtimeMidiQueue::detachTrackObserver(
    uint8_t trackIndex,
    RealtimeMidiQueueDispatchObserver& observer
) {
    if (trackIndex >= track_observers_.size() ||
        track_observers_[trackIndex] != &observer) {
        return;
    }
    track_observers_[trackIndex] = nullptr;
}

void RealtimeMidiQueue::attachLifecycleObserver(
    RealtimeMidiQueueLifecycleObserver& observer
) {
    lifecycle_observer_ = &observer;
}

void RealtimeMidiQueue::detachLifecycleObserver(
    RealtimeMidiQueueLifecycleObserver& observer
) {
    if (lifecycle_observer_ == &observer) lifecycle_observer_ = nullptr;
}

void RealtimeMidiQueue::drainDue(oc::api::MidiAPI& midi, uint32_t nowUs, uint32_t budgetUs) {
    OC_PERF_SCOPE(perfDrain, "midi.queue.drain");
#if OC_ENABLE_STATS
    const uint32_t queuedBefore = static_cast<uint32_t>(count_);
#endif
    const uint32_t startUs = nowUs;
    uint32_t currentUs = nowUs;

    while (count_ > 0 && due_(events_[0], currentUs)) {
        const auto event = events_[0];
        const int32_t deltaUs = oc::time::signedDeltaUs(currentUs, event.deadlineUs);

        if (event.type == RealtimeMidiEventType::NoteOn &&
            deltaUs > static_cast<int32_t>(DROP_THRESHOLD_US)) {
            OC_PERF_RECORD("midi.queue.drop-late-note-on", 0, static_cast<uint32_t>(deltaUs), 0);
            diagnostics_.droppedLateNoteOnCount = realtimeMidiSaturatingAdd(
                diagnostics_.droppedLateNoteOnCount,
                1
            );
            remove_(0, RealtimeMidiQueueLifecycleReason::DROPPED_LATE);
        } else {
            if (deltaUs > static_cast<int32_t>(LATE_SEND_THRESHOLD_US)) {
                OC_PERF_RECORD("midi.queue.late-send", 0, static_cast<uint32_t>(deltaUs), 0);
                diagnostics_.lateSendCount = realtimeMidiSaturatingAdd(
                    diagnostics_.lateSendCount,
                    1
                );
            }
            send_(midi, event);
            erase_(0);
        }

        currentUs = oc::time::isMicrosConfigured() ? oc::time::micros32() : nowUs;
        const uint32_t drainUs = currentUs - startUs;
        if (drainUs >= budgetUs) {
            break;
        }
    }
    OC_PERF_UNITS(perfDrain, queuedBefore, static_cast<uint32_t>(count_));
}

bool RealtimeMidiQueue::due_(const RealtimeMidiEvent& event, uint32_t nowUs) {
    return oc::time::signedDeltaUs(nowUs, event.deadlineUs) >= 0;
}

bool RealtimeMidiQueue::comesBefore_(const RealtimeMidiEvent& lhs, const RealtimeMidiEvent& rhs) {
    const int32_t deadlineDelta = oc::time::signedDeltaUs(lhs.deadlineUs, rhs.deadlineUs);
    if (deadlineDelta != 0) {
        return deadlineDelta < 0;
    }
    if (lhs.type != rhs.type) {
        return priority_(lhs.type) < priority_(rhs.type);
    }
    return false;
}

uint8_t RealtimeMidiQueue::priority_(RealtimeMidiEventType type) {
    switch (type) {
        case RealtimeMidiEventType::NoteOff:
            return 0U;
        case RealtimeMidiEventType::ControlChange:
            return 1U;
        case RealtimeMidiEventType::NoteOn:
            return 2U;
        default:
            return 0xFFU;
    }
}

bool RealtimeMidiQueue::validEvent_(const RealtimeMidiEvent& event) {
    switch (event.type) {
        case RealtimeMidiEventType::NoteOn:
        case RealtimeMidiEventType::NoteOff:
        case RealtimeMidiEventType::ControlChange:
            break;
        default:
            return false;
    }
    return event.channel <= 15U &&
           event.note <= 127U &&
           event.velocity <= 127U &&
           event.trackIndex < 16U;
}

void RealtimeMidiQueue::erase_(size_t index) {
    if (index >= count_) {
        return;
    }

    --count_;
    for (size_t i = index; i < count_; ++i) {
        events_[i] = events_[i + 1];
    }
}

void RealtimeMidiQueue::remove_(
    size_t index,
    RealtimeMidiQueueLifecycleReason reason
) {
    if (index >= count_) return;
    const RealtimeMidiEvent removed = events_[index];
    erase_(index);
    if (lifecycle_observer_ != nullptr) {
        lifecycle_observer_->onRealtimeMidiEventRemoved(removed, reason);
    }
}

void RealtimeMidiQueue::send_(oc::api::MidiAPI& midi, const RealtimeMidiEvent& event) {
    switch (event.type) {
        case RealtimeMidiEventType::NoteOn:
            midi.sendNoteOn(event.channel, event.note, event.velocity);
            break;
        case RealtimeMidiEventType::NoteOff:
            midi.sendNoteOff(event.channel, event.note, event.velocity);
            break;
        case RealtimeMidiEventType::ControlChange:
            midi.sendCC(event.channel, event.controller, event.value);
            break;
    }

    if (event.trackIndex < track_observers_.size()) {
        auto* observer = track_observers_[event.trackIndex];
        if (observer != nullptr) {
            observer->onRealtimeMidiEventDispatched(event);
        }
    }
    if (lifecycle_observer_ != nullptr) {
        lifecycle_observer_->onRealtimeMidiEventDispatched(event);
    }
}

void RealtimeMidiQueue::recordRejectedBatch_(
    const RealtimeMidiEvent* events,
    size_t count,
    bool capacityExceeded
) {
    diagnostics_.rejectedBatchCount = realtimeMidiSaturatingAdd(
        diagnostics_.rejectedBatchCount,
        1
    );
    diagnostics_.rejectedEventCount = realtimeMidiSaturatingAdd(
        diagnostics_.rejectedEventCount,
        static_cast<uint32_t>(std::min<size_t>(count, UINT32_MAX))
    );

    bool containsControlChange = false;
    bool containsNoteOff = false;
    if (events != nullptr) {
        const size_t inspectCount = std::min(count, MAX_QUEUE_DEPTH);
        for (size_t i = 0; i < inspectCount; ++i) {
            containsControlChange = containsControlChange ||
                events[i].type == RealtimeMidiEventType::ControlChange;
            containsNoteOff = containsNoteOff ||
                events[i].type == RealtimeMidiEventType::NoteOff;
        }
    }
    if (containsControlChange) {
        diagnostics_.rejectedControlChangeBatchCount = realtimeMidiSaturatingAdd(
            diagnostics_.rejectedControlChangeBatchCount,
            1
        );
    }
    if (capacityExceeded && containsNoteOff) {
        diagnostics_.criticalNoteOffOverflowCount = realtimeMidiSaturatingAdd(
            diagnostics_.criticalNoteOffOverflowCount,
            1
        );
    }
}

void RealtimeMidiQueue::updateHighWaterMark_() {
    diagnostics_.highWaterMark = std::max<uint16_t>(
        diagnostics_.highWaterMark,
        static_cast<uint16_t>(count_)
    );
}

}  // namespace core::sequencer
