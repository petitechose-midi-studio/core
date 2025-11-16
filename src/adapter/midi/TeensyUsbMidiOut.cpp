#include "TeensyUsbMidiOut.hpp"

#include <Arduino.h>

TeensyUsbMidiOut::TeensyUsbMidiOut(IEventBus& eventBus) : eventBus_(eventBus) {
    for (size_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
        activeNotes_[i].active = false;
    }
}

void TeensyUsbMidiOut::sendControlChange(MidiChannelValue ch, MidiCCValue cc, uint8_t value) {
    usbMIDI.sendControlChange(cc, value, ch + 1);
}

void TeensyUsbMidiOut::sendNoteOn(MidiChannelValue ch, MidiNoteValue note, uint8_t velocity) {
    markNoteActive(ch, note);
    usbMIDI.sendNoteOn(note, velocity, ch + 1);
}

void TeensyUsbMidiOut::sendNoteOff(MidiChannelValue ch, MidiNoteValue note, uint8_t velocity) {
    markNoteInactive(ch, note);
    usbMIDI.sendNoteOff(note, velocity, ch + 1);
}

void TeensyUsbMidiOut::sendProgramChange(MidiChannelValue ch, uint8_t program) {
    usbMIDI.sendProgramChange(program, ch + 1);
}

void TeensyUsbMidiOut::sendPitchBend(MidiChannelValue ch, uint16_t value) {
    usbMIDI.sendPitchBend(value, ch + 1);
}

void TeensyUsbMidiOut::sendChannelPressure(MidiChannelValue ch, uint8_t pressure) {
    usbMIDI.sendAfterTouch(pressure, ch + 1);
}

void TeensyUsbMidiOut::sendSysEx(const uint8_t* data, uint16_t length) {
    usbMIDI.sendSysEx(length, data, true);
}

void TeensyUsbMidiOut::flush() {
    while (usbMIDI.read()) {
    }
}

void TeensyUsbMidiOut::markNoteActive(MidiChannelValue ch, MidiNoteValue note) {
    for (size_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
        if (!activeNotes_[i].active) {
            activeNotes_[i].channel = ch;
            activeNotes_[i].note = note;
            activeNotes_[i].active = true;
            return;
        }
    }

    activeNotes_[0].channel = ch;
    activeNotes_[0].note = note;
    activeNotes_[0].active = true;
}

void TeensyUsbMidiOut::markNoteInactive(MidiChannelValue ch, MidiNoteValue note) {
    for (size_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
        if (activeNotes_[i].active && activeNotes_[i].channel == ch &&
            activeNotes_[i].note == note) {
            activeNotes_[i].active = false;
            return;
        }
    }
}
