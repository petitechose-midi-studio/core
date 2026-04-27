#include "sequencer/SequencerMidiEventSink.hpp"

namespace core::sequencer {

using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;

SequencerMidiEventSink::SequencerMidiEventSink(RealtimeMidiQueue& queue,
                                               uint8_t trackIndex,
                                               SequencerMidiEventSinkObserver* observer)
    : queue_(queue)
    , observer_(observer)
    , track_index_(trackIndex) {}

void SequencerMidiEventSink::setTimeline(uint32_t currentTick,
                                         uint32_t nowUs,
                                         uint32_t tickPeriodUs) {
    current_tick_ = currentTick;
    current_time_us_ = nowUs;
    tick_period_us_ = tickPeriodUs;
}

bool SequencerMidiEventSink::emitSequencerEvent(const SequencerEvent& event) {
    switch (event.type) {
        case SequencerEventType::NoteOn:
            return enqueueNoteOn_(event);
        case SequencerEventType::NoteOff:
            return enqueueNoteOff_(event);
        case SequencerEventType::AllNotesOff:
            return enqueueAllNotesOff_();
    }

    return true;
}

bool SequencerMidiEventSink::enqueueNoteOn_(const SequencerEvent& event) {
    RealtimeMidiEvent midiEvent{};
    midiEvent.deadlineUs = deadlineForTick_(event.tick);
    midiEvent.type = RealtimeMidiEventType::NoteOn;
    midiEvent.channel = event.channel;
    midiEvent.note = event.note;
    midiEvent.velocity = event.velocity;
    midiEvent.trackIndex = track_index_;

    if (!queue_.push(midiEvent)) {
        return false;
    }

    markNoteActive_(event.channel, event.note);
    if (observer_ != nullptr) {
        observer_->onNoteOn(track_index_, event.velocity);
    }
    return true;
}

bool SequencerMidiEventSink::enqueueNoteOff_(const SequencerEvent& event) {
    RealtimeMidiEvent midiEvent{};
    midiEvent.deadlineUs = deadlineForTick_(event.tick);
    midiEvent.type = RealtimeMidiEventType::NoteOff;
    midiEvent.channel = event.channel;
    midiEvent.note = event.note;
    midiEvent.velocity = event.velocity;
    midiEvent.trackIndex = track_index_;

    if (!queue_.push(midiEvent)) {
        return false;
    }

    markNoteInactive_(event.channel, event.note);
    if (observer_ != nullptr) {
        observer_->onNoteOff();
    }
    return true;
}

bool SequencerMidiEventSink::enqueueAllNotesOff_() {
    uint32_t panicCount = 0;
    queue_.cancelPendingNoteOns(track_index_);

    for (auto& slot : active_notes_) {
        if (!slot.active) {
            continue;
        }

        RealtimeMidiEvent midiEvent{};
        midiEvent.deadlineUs = current_time_us_;
        midiEvent.type = RealtimeMidiEventType::NoteOff;
        midiEvent.channel = slot.channel;
        midiEvent.note = slot.note;
        midiEvent.velocity = 0;
        midiEvent.trackIndex = track_index_;

        if (!queue_.push(midiEvent)) {
            return false;
        }
        slot.active = false;
        panicCount += 1;
    }

    if (observer_ != nullptr && panicCount > 0) {
        observer_->onPanicNoteOffs(panicCount);
    }

    return true;
}

uint32_t SequencerMidiEventSink::deadlineForTick_(uint32_t tick) const {
    if (tick_period_us_ == 0 || tick == current_tick_) {
        return current_time_us_;
    }

    if (tick < current_tick_) {
        return current_time_us_ - ((current_tick_ - tick) * tick_period_us_);
    }

    return current_time_us_ + ((tick - current_tick_) * tick_period_us_);
}

void SequencerMidiEventSink::markNoteActive_(uint8_t channel, uint8_t note) {
    for (auto& slot : active_notes_) {
        if (slot.active && slot.channel == channel && slot.note == note) {
            return;
        }
    }

    for (auto& slot : active_notes_) {
        if (!slot.active) {
            slot = {channel, note, true};
            return;
        }
    }
}

void SequencerMidiEventSink::markNoteInactive_(uint8_t channel, uint8_t note) {
    for (auto& slot : active_notes_) {
        if (slot.active && slot.channel == channel && slot.note == note) {
            slot.active = false;
            return;
        }
    }
}

}  // namespace core::sequencer
