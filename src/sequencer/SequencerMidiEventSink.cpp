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
                                         uint32_t tickPeriodUs,
                                         int32_t deadlineOffsetUs) {
    current_tick_ = currentTick;
    current_time_us_ = nowUs;
    tick_period_us_ = tickPeriodUs;
    deadline_offset_us_ = deadlineOffsetUs;
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
    if (!projectedDeadlineForTick_(event.tick, midiEvent.deadlineUs)) {
        return true;
    }
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
    if (!projectedDeadlineForTick_(event.tick, midiEvent.deadlineUs)) {
        return true;
    }
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

bool SequencerMidiEventSink::projectedDeadlineForTick_(
    uint32_t tick,
    uint32_t& out
) const {
    const int64_t tickDelta = static_cast<int64_t>(tick) -
        static_cast<int64_t>(current_tick_);
    const int64_t relativeUs = tickDelta *
        static_cast<int64_t>(tick_period_us_) +
        static_cast<int64_t>(deadline_offset_us_);

    if (deadline_offset_us_ < 0 && relativeUs < 0) {
        if (tick != current_tick_) {
            // At transport launch, future edges whose advanced deadline lies
            // before time zero cannot be recovered causally. Consume them in
            // the scheduler without producing a late catch-up burst. The edge
            // exactly at the current musical tick is clamped below so the
            // first playable step remains immediate and musical.
            return false;
        }
        out = current_time_us_;
        return true;
    }
    // Conversion to uint32_t intentionally follows the same modulo-2^32
    // timeline as micros(); RealtimeMidiQueue compares deadlines wrap-safely.
    out = current_time_us_ + static_cast<uint32_t>(relativeUs);
    return true;
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
