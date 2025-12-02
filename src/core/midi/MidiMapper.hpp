#pragma once

#include "Type.hpp"

#include <map>
#include <vector>

#include "config/InputID.hpp"
#include "event/IEventBus.hpp"
#include "struct/MidiCCMapping.hpp"

class IMidiOutput;
class EncoderChangedEvent;
class ButtonPressEvent;

class MidiMapper {
public:
    MidiMapper(IMidiOutput& midiOut, IEventBus& eventBus,
               const std::vector<MidiCCMapping>& mappings);
    ~MidiMapper();
private:
    struct MidiConfig {
        uint8_t channel;
        uint8_t control;
    };

    void onEncoderChangedEvent(const EncoderChangedEvent& event);
    void onButtonPressEvent(const ButtonPressEvent& event);

    const MidiConfig* findEncoder(EncoderID id) const;
    const MidiConfig* findButton(ButtonID id) const;

    IMidiOutput& midi_out_;
    IEventBus& event_bus_;

    std::map<uint16_t, MidiConfig> encoders_;
    std::map<uint16_t, MidiConfig> buttons_;

    SubscriptionId encoder_sub_;
    SubscriptionId button_sub_;
};
