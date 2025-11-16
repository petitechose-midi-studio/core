#pragma once

#include "core/interface/midi/MidiInput.hpp"

class IEventBus;

class TeensyUsbMidiIn : public MidiInput {
public:
    explicit TeensyUsbMidiIn(IEventBus& eventBus);
    ~TeensyUsbMidiIn();

    void processPendingMessages() override;

private:
    static void handleSysExStatic(const uint8_t* data, uint16_t length, bool complete);
    static void handleControlChangeStatic(uint8_t channel, uint8_t control, uint8_t value);
    static void handleNoteOnStatic(uint8_t channel, uint8_t note, uint8_t velocity);
    static void handleNoteOffStatic(uint8_t channel, uint8_t note, uint8_t velocity);

    void handleSysEx(const uint8_t* data, uint16_t length, bool complete);
    void handleControlChange(uint8_t channel, uint8_t control, uint8_t value);
    void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);

    IEventBus& eventBus_;
    static TeensyUsbMidiIn* instance_;
};