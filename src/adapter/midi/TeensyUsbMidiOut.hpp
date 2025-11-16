#pragma once
#include <Arduino.h>

#include "config/System.hpp"
#include "core/interface/midi/MidiOutput.hpp"

class IEventBus;

class TeensyUsbMidiOut : public MidiOutput {
public:
    explicit TeensyUsbMidiOut(IEventBus& eventBus);

    void sendControlChange(MidiChannelValue ch, MidiCCValue cc, uint8_t value) override;
    void sendNoteOn(MidiChannelValue ch, MidiNoteValue note, uint8_t velocity) override;
    void sendNoteOff(MidiChannelValue ch, MidiNoteValue note, uint8_t velocity) override;

    void sendProgramChange(MidiChannelValue ch, uint8_t program) override;
    void sendPitchBend(MidiChannelValue ch, uint16_t value) override;
    void sendChannelPressure(MidiChannelValue ch, uint8_t pressure) override;
    void sendSysEx(const uint8_t* data, uint16_t length) override;

    void flush();

private:
    static constexpr size_t MAX_ACTIVE_NOTES = System::Midi::MAX_ACTIVE_NOTES;

    struct ActiveNote {
        MidiChannelValue channel;
        MidiNoteValue note;
        bool active;
    };

    ActiveNote activeNotes_[MAX_ACTIVE_NOTES];
    IEventBus& eventBus_;

    void markNoteActive(MidiChannelValue ch, MidiNoteValue note);

    void markNoteInactive(MidiChannelValue ch, MidiNoteValue note);
};
