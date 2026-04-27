#include "sequencer/RealtimeMidiQueue.hpp"

#include <algorithm>

#include <oc/time/Time.hpp>

namespace core::sequencer {

bool RealtimeMidiQueue::push(const RealtimeMidiEvent& event) {
    if (count_ >= events_.size() && !makeRoomFor_(event)) {
        counters_.overflow += 1;
        return false;
    }

    size_t insertIndex = count_;
    while (insertIndex > 0 && comesBefore_(event, events_[insertIndex - 1])) {
        events_[insertIndex] = events_[insertIndex - 1];
        --insertIndex;
    }

    events_[insertIndex] = event;
    count_ += 1;
    counters_.pushed += 1;
    counters_.highWater = std::max<uint32_t>(counters_.highWater, static_cast<uint32_t>(count_));
    return true;
}

uint32_t RealtimeMidiQueue::cancelPendingNoteOns(uint8_t trackIndex) {
    uint32_t removed = 0;
    size_t index = 0;

    while (index < count_) {
        if (events_[index].type == RealtimeMidiEventType::NoteOn &&
            events_[index].trackIndex == trackIndex) {
            erase_(index);
            removed += 1;
            continue;
        }

        index += 1;
    }

    counters_.cancelledNoteOns += removed;
    return removed;
}

void RealtimeMidiQueue::clear() {
    count_ = 0;
}

RealtimeMidiQueue::Counters RealtimeMidiQueue::takeCounters() {
    const Counters snapshot = counters_;
    counters_ = {};
    counters_.highWater = static_cast<uint32_t>(count_);
    return snapshot;
}

void RealtimeMidiQueue::drainDue(oc::api::MidiAPI& midi, uint32_t nowUs, uint32_t budgetUs) {
    const uint32_t startUs = nowUs;
    uint32_t currentUs = nowUs;

    while (count_ > 0 && due_(events_[0], currentUs)) {
        const auto event = events_[0];
        const int32_t deltaUs = oc::time::signedDeltaUs(currentUs, event.deadlineUs);

        if (event.type == RealtimeMidiEventType::NoteOn &&
            deltaUs > static_cast<int32_t>(DROP_THRESHOLD_US)) {
            counters_.dropped += 1;
            erase_(0);
        } else {
            if (deltaUs > static_cast<int32_t>(LATE_SEND_THRESHOLD_US)) {
                counters_.lateSent += 1;
            }
            send_(midi, event);
            counters_.sent += 1;
            erase_(0);
        }

        currentUs = oc::time::isMicrosConfigured() ? oc::time::micros32() : nowUs;
        const uint32_t drainUs = currentUs - startUs;
        counters_.maxDrainUs = std::max(counters_.maxDrainUs, drainUs);
        if (drainUs >= budgetUs) {
            break;
        }
    }
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
            counters_.dropped += 1;
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
        return;
    }

    midi.sendNoteOff(event.channel, event.note, event.velocity);
}

}  // namespace core::sequencer
