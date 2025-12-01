#include "MidiMapper.hpp"

#include "../event/Events.hpp"
#include "../event/IEventBus.hpp"
#include "../event/UnifiedEventTypes.hpp"
#include "../interface/midi/MidiOutput.hpp"

using InputEvent::ButtonPress;
using InputEvent::EncoderChanged;

MidiMapper::MidiMapper(
    MidiOutput& midiOut, IEventBus& eventBus,
    const std::vector<MidiCCMapping>& mappings)
    : midi_out_(midiOut), event_bus_(eventBus), encoder_sub_(0), button_sub_(0) {
    for (const auto& mapping : mappings) {
        MidiConfig config{mapping.channel, mapping.cc};

        if (mapping.inputId >= 300 && mapping.inputId < 500) {
            encoders_[mapping.inputId] = config;
        } else if (mapping.inputId >= 10 && mapping.inputId < 100) {
            buttons_[mapping.inputId] = config;
        }
    }

    encoder_sub_ = event_bus_.on(EventCategory::Input, EncoderChanged, [this](const Event& e) {
        onEncoderChangedEvent(static_cast<const EncoderChangedEvent&>(e));
    });

    button_sub_ = event_bus_.on(EventCategory::Input, ButtonPress, [this](const Event& e) {
        onButtonPressEvent(static_cast<const ButtonPressEvent&>(e));
    });
}

MidiMapper::~MidiMapper() {
    if (encoder_sub_ != 0) {
        event_bus_.off(encoder_sub_);
    }
    if (button_sub_ != 0) {
        event_bus_.off(button_sub_);
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

    midi_out_.sendControlChange(config->channel, config->control, value);

    MidiCCEvent midiEvent(config->channel, config->control, value, static_cast<uint8_t>(event.encoderId));
    event_bus_.emit(midiEvent);
}

void MidiMapper::onButtonPressEvent(const ButtonPressEvent& event) {
    const auto* config = findButton(event.buttonId);
    if (!config) {
        return;
    }

    uint8_t value = event.pressed ? 127 : 0;

    midi_out_.sendControlChange(config->channel, config->control, value);

    MidiCCEvent midiEvent(config->channel, config->control, value, static_cast<uint8_t>(event.buttonId));
    event_bus_.emit(midiEvent);
}
