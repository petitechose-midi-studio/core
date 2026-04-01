#include "sequencer/SequencerMidiOutput.hpp"

#include <algorithm>

#include "config/TimeCompat.hpp"

namespace core::sequencer {

SequencerMidiOutput::SequencerMidiOutput(oc::api::MidiAPI& midi,
                                         uint8_t trackIndex,
                                         SequencerMidiOutputObserver* observer)
    : midi_(midi)
    , observer_(observer)
    , track_index_(trackIndex) {}

void SequencerMidiOutput::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    const bool observed = observer_ != nullptr;
    const uint32_t startUs = observed ? core::time_compat::micros() : 0;

    midi_.sendNoteOn(channel, note, velocity);
    markNoteActive_(channel, note);

    if (observed) {
        observer_->onNoteOn(track_index_, velocity, core::time_compat::micros() - startUs);
    }
}

void SequencerMidiOutput::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    const bool observed = observer_ != nullptr;
    const uint32_t startUs = observed ? core::time_compat::micros() : 0;

    midi_.sendNoteOff(channel, note, velocity);
    markNoteInactive_(channel, note);

    if (observed) {
        observer_->onNoteOff(core::time_compat::micros() - startUs);
    }
}

void SequencerMidiOutput::sendCC(uint8_t channel, uint8_t cc, uint8_t value) {
    midi_.sendCC(channel, cc, value);
}

void SequencerMidiOutput::allNotesOff() {
    uint32_t panicCount = 0;
    uint32_t panicTotalUs = 0;
    uint32_t panicMaxUs = 0;

    for (auto& slot : active_notes_) {
        if (!slot.active) {
            continue;
        }

        const bool observed = observer_ != nullptr;
        const uint32_t startUs = observed ? core::time_compat::micros() : 0;

        midi_.sendNoteOff(slot.channel, slot.note, 0);
        slot.active = false;

        if (!observed) {
            continue;
        }

        const uint32_t sendUs = core::time_compat::micros() - startUs;
        panicCount += 1;
        panicTotalUs += sendUs;
        panicMaxUs = std::max(panicMaxUs, sendUs);
    }

    if (observer_ != nullptr && panicCount > 0) {
        observer_->onPanicNoteOffs(panicCount, panicTotalUs, panicMaxUs);
    }
}

void SequencerMidiOutput::markNoteActive_(uint8_t channel, uint8_t note) {
    for (auto& slot : active_notes_) {
        if (!slot.active) {
            slot = {channel, note, true};
            return;
        }
    }
}

void SequencerMidiOutput::markNoteInactive_(uint8_t channel, uint8_t note) {
    for (auto& slot : active_notes_) {
        if (slot.active && slot.channel == channel && slot.note == note) {
            slot.active = false;
            return;
        }
    }
}

}  // namespace core::sequencer
