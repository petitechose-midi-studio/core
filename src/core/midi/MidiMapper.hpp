#pragma once

#include <etl/flat_map.h>
#include <etl/vector.h>

#include "../Type.hpp"
#include "../event/IEventBus.hpp"
#include "../struct/MidiCCMapping.hpp"
#include "config/InputID.hpp"
#include "config/System.hpp"

class MidiOutput;
class EncoderChangedEvent;
class ButtonPressEvent;

class MidiMapper {
public:
    MidiMapper(MidiOutput& midiOut, IEventBus& eventBus,
               const etl::vector<MidiCCMapping, System::Memory::MAX_MIDI_MAPPINGS>& mappings);
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

    MidiOutput& midiOut_;
    IEventBus& eventBus_;

    etl::flat_map<uint16_t, MidiConfig, System::Memory::MAX_MIDI_MAPPINGS> encoders_;
    etl::flat_map<uint16_t, MidiConfig, System::Memory::MAX_MIDI_MAPPINGS> buttons_;

    SubscriptionId encoderSub_;
    SubscriptionId buttonSub_;
};
