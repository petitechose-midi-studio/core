#include "MacroMidiHandler.hpp"

#include <Arduino.h>  // For millis()

namespace handler {

MacroMidiHandler::MacroMidiHandler(state::CoreState& coreState,
                                   oc::api::MidiAPI& midi,
                                   oc::api::EncoderAPI& encoders)
    : coreState_(coreState)
    , midi_(midi)
    , encoders_(encoders) {
    setupCallbacks();
}

void MacroMidiHandler::setupCallbacks() {
    midi_.onCC([this](uint8_t channel, uint8_t cc, uint8_t value) {
        handleIncomingCC(channel, cc, value);
    });
}

void MacroMidiHandler::handleIncomingCC(uint8_t channel, uint8_t cc, uint8_t value) {
    // Find which macro this CC/channel belongs to in active page config
    int8_t index = findMacroForCC(channel, cc);
    if (index < 0) return;

    // Convert CC value to normalized float
    float normalized = static_cast<float>(value) / 127.0f;

    // Update state
    auto& slot = coreState_.macros.slots[index];
    slot.value.set(normalized);
    slot.updateDisplayValue();

    // Sync encoder position
    encoders_.setPosition(ENCODERS[index], normalized);

    // Mark values dirty for delayed persistence
    coreState_.onValueChanged(millis());
}

int8_t MacroMidiHandler::findMacroForCC(uint8_t channel, uint8_t cc) const {
    const auto& configs = coreState_.pages.activeConfigs;
    for (uint8_t i = 0; i < state::MACRO_COUNT; ++i) {
        if (configs[i].cc == cc && configs[i].channel == channel) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

}  // namespace handler
