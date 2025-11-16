#include "MidiMapper.hpp"

#include "../event/Events.hpp"
#include "../event/IEventBus.hpp"
#include "../event/UnifiedEventTypes.hpp"
#include "../interface/midi/MidiOutput.hpp"

using InputEvent::ButtonPress;
using InputEvent::EncoderChanged;

MidiMapper::MidiMapper(
    MidiOutput& midiOut, IEventBus& eventBus,
    const etl::vector<MidiCCMapping, System::Memory::MAX_MIDI_MAPPINGS>& mappings)
    : midiOut_(midiOut), eventBus_(eventBus), encoderSub_(0), buttonSub_(0) {
    for (const auto& mapping : mappings) {
        MidiConfig config{mapping.channel, mapping.cc};

        if (mapping.inputId >= 300 && mapping.inputId < 500) {
            encoders_[mapping.inputId] = config;
        } else if (mapping.inputId >= 10 && mapping.inputId < 100) {
            buttons_[mapping.inputId] = config;
        }
    }

    encoderSub_ = eventBus_.on(EventCategory::Input, EncoderChanged, [this](const Event& e) {
        onEncoderChangedEvent(static_cast<const EncoderChangedEvent&>(e));
    });

    buttonSub_ = eventBus_.on(EventCategory::Input, ButtonPress, [this](const Event& e) {
        onButtonPressEvent(static_cast<const ButtonPressEvent&>(e));
    });
}

MidiMapper::~MidiMapper() {
    if (encoderSub_ != 0) {
        eventBus_.off(encoderSub_);
    }
    if (buttonSub_ != 0) {
        eventBus_.off(buttonSub_);
    }
}

const MidiMapper::MidiConfig* MidiMapper::findEncoder(EncoderID id) const {
    auto it = encoders_.find(static_cast<uint16_t>(id));
    return (it != encoders_.end()) ? &it->second : nullptr;
}

const MidiMapper::MidiConfig* MidiMapper::findButton(ButtonID id) const {
    auto it = buttons_.find(static_cast<uint16_t>(id));
    return (it != buttons_.end()) ? &it->second : nullptr;
}

void MidiMapper::onEncoderChangedEvent(const EncoderChangedEvent& event) {
    const auto* config = findEncoder(event.encoderId);
    if (!config) {
        return;
    }

    uint8_t value = static_cast<uint8_t>(event.normalizedValue * 127.0f);

    midiOut_.sendControlChange(config->channel, config->control, value);

    MidiCCEvent midiEvent(config->channel, config->control, value, static_cast<uint8_t>(event.encoderId));
    eventBus_.emit(midiEvent);
}

void MidiMapper::onButtonPressEvent(const ButtonPressEvent& event) {
    const auto* config = findButton(event.buttonId);
    if (!config) {
        return;
    }

    uint8_t value = event.pressed ? 127 : 0;

    midiOut_.sendControlChange(config->channel, config->control, value);

    MidiCCEvent midiEvent(config->channel, config->control, value, static_cast<uint8_t>(event.buttonId));
    eventBus_.emit(midiEvent);
}
