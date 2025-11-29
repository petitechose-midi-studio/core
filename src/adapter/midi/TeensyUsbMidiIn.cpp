#include "TeensyUsbMidiIn.hpp"

#include <Arduino.h>

#include "core/event/Events.hpp"
#include "core/event/IEventBus.hpp"
#include "log/Macros.hpp"

TeensyUsbMidiIn* TeensyUsbMidiIn::instance_ = nullptr;

TeensyUsbMidiIn::TeensyUsbMidiIn(IEventBus& eventBus)
    : eventBus_(eventBus), initialized_(false) {
    instance_ = this;
    // NOTE: Do NOT configure MIDI callbacks here - defer to init()
}

void TeensyUsbMidiIn::init() {
    if (initialized_) return;

    usbMIDI.setHandleSystemExclusive(handleSysExStatic);
    usbMIDI.setHandleControlChange(handleControlChangeStatic);
    usbMIDI.setHandleNoteOn(handleNoteOnStatic);
    usbMIDI.setHandleNoteOff(handleNoteOffStatic);

    initialized_ = true;
    MIDI_LOGLN("[MIDI IN] Handlers registered");
}

TeensyUsbMidiIn::~TeensyUsbMidiIn() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void TeensyUsbMidiIn::processPendingMessages() {
    while (usbMIDI.read()) {
        // Callbacks handle logging per message type
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
    MIDI_LOGF("[MIDI IN] SysEx len=%u complete=%d\n", length, complete);
    if (complete) {
        eventBus_.emit(SysExEvent(data, length));
    }
}

void TeensyUsbMidiIn::handleControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    MIDI_LOGF("[MIDI IN] CC ch=%u cc=%u val=%u\n", channel, control, value);
    MidiChannelValue ch = static_cast<MidiChannelValue>(channel - 1);
    MidiCCValue cc = static_cast<MidiCCValue>(control);

    eventBus_.emit(MidiCCEvent(ch, cc, value));
}

void TeensyUsbMidiIn::handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    MIDI_LOGF("[MIDI IN] NoteOn ch=%u note=%u vel=%u\n", channel, note, velocity);
    MidiChannelValue ch = static_cast<MidiChannelValue>(channel - 1);
    MidiNoteValue n = static_cast<MidiNoteValue>(note);

    eventBus_.emit(MidiNoteOnEvent(ch, n, velocity));
}

void TeensyUsbMidiIn::handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    MIDI_LOGF("[MIDI IN] NoteOff ch=%u note=%u vel=%u\n", channel, note, velocity);
    MidiChannelValue ch = static_cast<MidiChannelValue>(channel - 1);
    MidiNoteValue n = static_cast<MidiNoteValue>(note);

    eventBus_.emit(MidiNoteOffEvent(ch, n, velocity));
}