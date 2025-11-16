#include "TeensyUsbMidiIn.hpp"

#include <Arduino.h>

#include "core/event/Events.hpp"
#include "core/event/IEventBus.hpp"

TeensyUsbMidiIn* TeensyUsbMidiIn::instance_ = nullptr;

TeensyUsbMidiIn::TeensyUsbMidiIn(IEventBus& eventBus) : eventBus_(eventBus) {
    instance_ = this;

    usbMIDI.setHandleSystemExclusive(handleSysExStatic);
    usbMIDI.setHandleControlChange(handleControlChangeStatic);
    usbMIDI.setHandleNoteOn(handleNoteOnStatic);
    usbMIDI.setHandleNoteOff(handleNoteOffStatic);
}

TeensyUsbMidiIn::~TeensyUsbMidiIn() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void TeensyUsbMidiIn::processPendingMessages() {
    while (usbMIDI.read()) {
    }
}

void TeensyUsbMidiIn::handleSysExStatic(const uint8_t* data, uint16_t length, bool complete) {
    if (instance_) {
        instance_->handleSysEx(data, length, complete);
    }
}

void TeensyUsbMidiIn::handleControlChangeStatic(uint8_t channel, uint8_t control, uint8_t value) {
    if (instance_) {
        instance_->handleControlChange(channel, control, value);
    }
}

void TeensyUsbMidiIn::handleNoteOnStatic(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (instance_) {
        instance_->handleNoteOn(channel, note, velocity);
    }
}

void TeensyUsbMidiIn::handleNoteOffStatic(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (instance_) {
        instance_->handleNoteOff(channel, note, velocity);
    }
}

void TeensyUsbMidiIn::handleSysEx(const uint8_t* data, uint16_t length, bool complete) {
    if (complete) {
        eventBus_.emit(SysExEvent(data, length));
    }
}

void TeensyUsbMidiIn::handleControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    MidiChannelValue ch = static_cast<MidiChannelValue>(channel - 1);
    MidiCCValue cc = static_cast<MidiCCValue>(control);

    eventBus_.emit(MidiCCEvent(ch, cc, value));
}

void TeensyUsbMidiIn::handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    MidiChannelValue ch = static_cast<MidiChannelValue>(channel - 1);
    MidiNoteValue n = static_cast<MidiNoteValue>(note);

    eventBus_.emit(MidiNoteOnEvent(ch, n, velocity));
}

void TeensyUsbMidiIn::handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    MidiChannelValue ch = static_cast<MidiChannelValue>(channel - 1);
    MidiNoteValue n = static_cast<MidiNoteValue>(note);

    eventBus_.emit(MidiNoteOffEvent(ch, n, velocity));
}