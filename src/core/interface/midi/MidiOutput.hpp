#pragma once

#include "../../Type.hpp"

class MidiOutput {
protected:
    ~MidiOutput() = default;

public:
    virtual bool supportsEvents() const {
        return false;
    }

    virtual void sendCc(MidiChannelValue ch, MidiCCValue cc, uint8_t value, uint8_t source) {
        sendControlChange(ch, cc, value);
    }

    virtual void sendControlChange(MidiChannelValue ch, MidiCCValue cc, uint8_t value) = 0;

    virtual void sendNoteOn(MidiChannelValue ch, MidiNoteValue note, uint8_t velocity) = 0;

    virtual void sendNoteOff(MidiChannelValue ch, MidiNoteValue note, uint8_t velocity) = 0;

    virtual void sendProgramChange(MidiChannelValue ch, uint8_t program) = 0;

    virtual void sendPitchBend(MidiChannelValue ch, uint16_t value) = 0;

    virtual void sendChannelPressure(MidiChannelValue ch, uint8_t pressure) = 0;

    virtual void sendSysEx(const uint8_t* data, uint16_t length) = 0;
};
