#pragma once

#include <Arduino.h>

#include "Event.hpp"
#include "UnifiedEventTypes.hpp"
#include "config/InputID.hpp"

class EncoderChangedEvent : public Event {
public:
    EncoderChangedEvent(EncoderID encoderId, float normalizedValue)
        : Event(EventCategory::Input, InputEvent::EncoderChanged),
          encoderId(encoderId),
          normalizedValue(normalizedValue) {}

    EncoderID encoderId;
    float normalizedValue;
};

class ButtonPressEvent : public Event {
public:
    ButtonPressEvent(ButtonID buttonId, bool pressed)
        : Event(EventCategory::Input, InputEvent::ButtonPress),
          buttonId(buttonId),
          pressed(pressed) {}

    ButtonID buttonId;
    bool pressed;
};

class ButtonReleaseEvent : public Event {
public:
    explicit ButtonReleaseEvent(ButtonID buttonId)
        : Event(EventCategory::Input, InputEvent::ButtonRelease), buttonId(buttonId) {}

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
        : Event(EventCategory::MIDI, MidiEvent::NoteOn),
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
        : Event(EventCategory::MIDI, MidiEvent::NoteOff),
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
        : Event(EventCategory::MIDI, MidiEvent::Mapping),
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
        : Event(EventCategory::MIDI, MidiEvent::SysEx),
          data(data),
          length(length) {}

    const uint8_t* data;  // Pointer to SysEx data (no copy)
    uint16_t length;      // Length of data
};

enum class ViewType : uint8_t;

class SystemViewChangeEvent : public Event {
public:
    explicit SystemViewChangeEvent(ViewType targetView)
        : Event(EventCategory::System, SystemEvent::ViewChange),
          targetView(targetView),
          hasTarget(true) {}

    SystemViewChangeEvent()
        : Event(EventCategory::System, SystemEvent::ViewChange),
          targetView(static_cast<ViewType>(0)),
          hasTarget(false) {}

    ViewType targetView;
    bool hasTarget;
};

enum class SystemMode : uint8_t { Performance, Configuration, MidiLearn, Bootloader };

class SystemModeChangedEvent : public Event {
public:
    explicit SystemModeChangedEvent(SystemMode mode)
        : Event(EventCategory::System, SystemEvent::ModeChange), mode(mode) {}

    SystemMode mode;
};

class SystemErrorEvent : public Event {
public:
    SystemErrorEvent(uint16_t errorCode, const String& message = "")
        : Event(EventCategory::System, SystemEvent::Error),
          errorCode(errorCode),
          message(message) {}

    uint16_t errorCode;
    String message;
};

class SystemBootCompleteEvent : public Event {
public:
    SystemBootCompleteEvent() : Event(EventCategory::System, SystemEvent::BootComplete) {}
};

class IntegrationRegisteredEvent : public Event {
public:
    IntegrationRegisteredEvent(const String& name, uint8_t integrationId)
        : Event(EventCategory::System, SystemEvent::PluginRegistered),
          name(name),
          integrationId(integrationId) {}

    const String name;
    const uint8_t integrationId;
};

class IntegrationActivatedEvent : public Event {
public:
    IntegrationActivatedEvent(const String& name, uint8_t integrationId)
        : Event(EventCategory::System, SystemEvent::PluginActivated),
          name(name),
          integrationId(integrationId) {}

    const String name;
    const uint8_t integrationId;
};

class IntegrationDeactivatedEvent : public Event {
public:
    IntegrationDeactivatedEvent(const String& name, uint8_t integrationId)
        : Event(EventCategory::System, SystemEvent::PluginDeactivated),
          name(name),
          integrationId(integrationId) {}

    const String name;
    const uint8_t integrationId;
};

class IntegrationErrorEvent : public Event {
public:
    IntegrationErrorEvent(const String& name, const String& error)
        : Event(EventCategory::System, SystemEvent::PluginError), name(name), error(error) {}

    const String name;
    const String error;
};
