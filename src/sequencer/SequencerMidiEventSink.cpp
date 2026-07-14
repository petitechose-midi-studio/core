#include "sequencer/SequencerMidiEventSink.hpp"

#include <config/PlatformCompat.hpp>

namespace core::sequencer {

using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;

FLASHMEM SequencerMidiEventSink::SequencerMidiEventSink(RealtimeMidiQueue& queue,
                                               uint8_t trackIndex,
                                               SequencerMidiEventSinkObserver* observer)
    : queue_(queue)
    , observer_(observer)
    , track_index_(trackIndex) {
    queue_.attachTrackObserver(track_index_, *this);
}

SequencerMidiEventSink::~SequencerMidiEventSink() {
    queue_.detachTrackObserver(track_index_, *this);
}

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

    return true;
}

bool SequencerMidiEventSink::enqueueAllNotesOff_() {
    return queue_.replaceTrackEventsWithNoteOffBatch(
        track_index_,
        current_time_us_,
        active_notes_by_channel_.data(),
        active_notes_by_channel_.size()
    ).ok();
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
    active_notes_by_channel_[channel & 0x0FU].setBit(note & 0x7FU);
}

void SequencerMidiEventSink::markNoteInactive_(uint8_t channel, uint8_t note) {
    active_notes_by_channel_[channel & 0x0FU].setBit(note & 0x7FU, false);
}

void SequencerMidiEventSink::onRealtimeMidiEventDispatched(
    const RealtimeMidiEvent& event
) {
    switch (event.type) {
        case RealtimeMidiEventType::NoteOn:
            markNoteActive_(event.channel, event.note);
            if (observer_ != nullptr) {
                observer_->onNoteOn(track_index_, event.velocity);
            }
            break;
        case RealtimeMidiEventType::NoteOff:
            markNoteInactive_(event.channel, event.note);
            break;
        case RealtimeMidiEventType::ControlChange:
            break;
    }
}

}  // namespace core::sequencer
