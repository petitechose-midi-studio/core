#include "sequencer/RealtimeMidiQueue.hpp"

#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>

namespace core::sequencer {

bool RealtimeMidiQueue::push(const RealtimeMidiEvent& event) {
    if (count_ >= events_.size() && !makeRoomFor_(event)) {
        OC_PERF_RECORD("midi.queue.reject", 0, static_cast<uint32_t>(event.type), 0);
        return false;
    }

    size_t insertIndex = count_;
    while (insertIndex > 0 && comesBefore_(event, events_[insertIndex - 1])) {
        events_[insertIndex] = events_[insertIndex - 1];
        --insertIndex;
    }

    events_[insertIndex] = event;
    count_ += 1;
    return true;
}

uint32_t RealtimeMidiQueue::cancelPendingEvents(uint8_t trackIndex) {
    uint32_t removed = 0;
    size_t index = 0;

    while (index < count_) {
        if (events_[index].trackIndex == trackIndex) {
            erase_(index);
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
    count_ = 0;
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
            erase_(0);
        } else {
            if (deltaUs > static_cast<int32_t>(LATE_SEND_THRESHOLD_US)) {
                OC_PERF_RECORD("midi.queue.late-send", 0, static_cast<uint32_t>(deltaUs), 0);
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
    return (type == RealtimeMidiEventType::NoteOff) ? 0U : 1U;
}

bool RealtimeMidiQueue::makeRoomFor_(const RealtimeMidiEvent& event) {
    if (event.type != RealtimeMidiEventType::NoteOff) {
        return false;
    }

    for (size_t i = count_; i > 0; --i) {
        const size_t index = i - 1;
        if (events_[index].type == RealtimeMidiEventType::NoteOn) {
            erase_(index);
            OC_PERF_RECORD("midi.queue.displace-note-on", 0, 1, 0);
            return true;
        }
    }

    return false;
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

void RealtimeMidiQueue::send_(oc::api::MidiAPI& midi, const RealtimeMidiEvent& event) {
    if (event.type == RealtimeMidiEventType::NoteOn) {
        midi.sendNoteOn(event.channel, event.note, event.velocity);
    } else {
        midi.sendNoteOff(event.channel, event.note, event.velocity);
    }

    if (event.trackIndex < track_observers_.size()) {
        auto* observer = track_observers_[event.trackIndex];
        if (observer != nullptr) {
            observer->onRealtimeMidiEventDispatched(event);
        }
    }
}

}  // namespace core::sequencer
