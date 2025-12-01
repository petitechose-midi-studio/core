#pragma once

#include "core/interface/midi/MidiInput.hpp"

class IEventBus;

/**
 * USB MIDI input handler with lazy initialization.
 *
 * IMPORTANT: Call init() after Arduino setup() to register MIDI callbacks.
 */
class TeensyUsbMidiIn : public MidiInput {
public:
    explicit TeensyUsbMidiIn(IEventBus& eventBus);
    ~TeensyUsbMidiIn();

    /**
     * Initialize MIDI callbacks. Must be called after Arduino setup().
     * Safe to call multiple times.
     */
    void init();

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

    IEventBus& event_bus_;
    bool initialized_ = false;
    static TeensyUsbMidiIn* instance_;
};