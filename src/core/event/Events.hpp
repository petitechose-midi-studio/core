#pragma once

#include <cstdint>
#include <string>

#include "Event.hpp"
#include "UnifiedEventTypes.hpp"
#include "config/InputID.hpp"

class EncoderChangedEvent : public Event {
public:
    EncoderChangedEvent(EncoderID encoderId, float normalizedValue)
        : Event(EventCategory::USER_INPUT, InputEvent::ENCODER_CHANGED),
          encoderId(encoderId),
          normalizedValue(normalizedValue) {}

    EncoderID encoderId;
    float normalizedValue;
};

class ButtonPressEvent : public Event {
public:
    ButtonPressEvent(ButtonID buttonId, bool pressed)
        : Event(EventCategory::USER_INPUT, InputEvent::BUTTON_PRESS),
          buttonId(buttonId),
          pressed(pressed) {}

    ButtonID buttonId;
    bool pressed;
};

class ButtonReleaseEvent : public Event {
public:
    explicit ButtonReleaseEvent(ButtonID buttonId)
        : Event(EventCategory::USER_INPUT, InputEvent::BUTTON_RELEASE), buttonId(buttonId) {}

    ButtonID buttonId;
};

class MidiCCEvent : public Event {
public:
    MidiCCEvent(uint8_t channel, uint8_t controller, uint8_t value, uint8_t source = 0)
        : Event(EventCategory::MIDI, MidiEvent::CC),
          channel(channel),
          controller(controller),
          value(value),
          source(source) {}

    uint8_t channel;
    uint8_t controller;
    uint8_t value;
    uint8_t source;
};

class MidiNoteOnEvent : public Event {
public:
    MidiNoteOnEvent(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t source = 0)
        : Event(EventCategory::MIDI, MidiEvent::NOTE_ON),
          channel(channel),
          note(note),
          velocity(velocity),
          source(source) {}

    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    uint8_t source;
};

class MidiNoteOffEvent : public Event {
public:
    MidiNoteOffEvent(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t source = 0)
        : Event(EventCategory::MIDI, MidiEvent::NOTE_OFF),
          channel(channel),
          note(note),
          velocity(velocity),
          source(source) {}

    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    uint8_t source;
};

class MidiMappingEvent : public Event {
public:
    MidiMappingEvent(uint8_t inputId, uint8_t midiType, uint8_t midiChannel, uint8_t midiNumber,
                     uint8_t midiValue)
        : Event(EventCategory::MIDI, MidiEvent::MAPPING),
          inputId(inputId),
          midiType(midiType),
          midiChannel(midiChannel),
          midiNumber(midiNumber),
          midiValue(midiValue) {}

    uint8_t inputId;
    uint8_t midiType;
    uint8_t midiChannel;
    uint8_t midiNumber;
    uint8_t midiValue;
};

class SysExEvent : public Event {
public:
    SysExEvent(const uint8_t* data, uint16_t length)
        : Event(EventCategory::MIDI, MidiEvent::SYSEX),
          data(data),
          length(length) {}

    const uint8_t* data;  // Pointer to SysEx data (no copy)
    uint16_t length;      // Length of data
};

enum class SystemMode : uint8_t { PERFORMANCE, CONFIGURATION, MIDI_LEARN, BOOTLOADER };

class SystemModeChangedEvent : public Event {
public:
    explicit SystemModeChangedEvent(SystemMode mode)
        : Event(EventCategory::SYSTEM, SystemEvent::MODE_CHANGE), mode(mode) {}

    SystemMode mode;
};

class SystemErrorEvent : public Event {
public:
    SystemErrorEvent(uint16_t errorCode, const std::string& message = "")
        : Event(EventCategory::SYSTEM, SystemEvent::ERROR),
          errorCode(errorCode),
          message(message) {}

    uint16_t errorCode;
    std::string message;
};

class SystemBootCompleteEvent : public Event {
public:
    SystemBootCompleteEvent() : Event(EventCategory::SYSTEM, SystemEvent::BOOT_COMPLETE) {}
};

class IntegrationRegisteredEvent : public Event {
public:
    IntegrationRegisteredEvent(const std::string& name, uint8_t integrationId)
        : Event(EventCategory::SYSTEM, SystemEvent::PLUGIN_REGISTERED),
          name(name),
          integrationId(integrationId) {}

    const std::string name;
    const uint8_t integrationId;
};

class IntegrationActivatedEvent : public Event {
public:
    IntegrationActivatedEvent(const std::string& name, uint8_t integrationId)
        : Event(EventCategory::SYSTEM, SystemEvent::PLUGIN_ACTIVATED),
          name(name),
          integrationId(integrationId) {}

    const std::string name;
    const uint8_t integrationId;
};

class IntegrationDeactivatedEvent : public Event {
public:
    IntegrationDeactivatedEvent(const std::string& name, uint8_t integrationId)
        : Event(EventCategory::SYSTEM, SystemEvent::PLUGIN_DEACTIVATED),
          name(name),
          integrationId(integrationId) {}

    const std::string name;
    const uint8_t integrationId;
};

class IntegrationErrorEvent : public Event {
public:
    IntegrationErrorEvent(const std::string& name, const std::string& error)
        : Event(EventCategory::SYSTEM, SystemEvent::PLUGIN_ERROR), name(name), error(error) {}

    const std::string name;
    const std::string error;
};
