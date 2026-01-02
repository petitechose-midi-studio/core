#include "MacroMidiHandler.hpp"

namespace handler {

MacroMidiHandler::MacroMidiHandler(state::MacroState& state,
                                   oc::api::MidiAPI& midi,
                                   oc::api::EncoderAPI& encoders)
    : state_(state)
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
    // Only respond to our channel
    if (channel != state::MACRO_CHANNEL) return;

    // Find which macro this CC belongs to
    int8_t index = findMacroForCC(cc);
    if (index < 0) return;

    // Convert CC value to normalized float
    float normalized = static_cast<float>(value) / 127.0f;

    // Update state
    auto& slot = state_.slots[index];
    slot.value.set(normalized);
    slot.updateDisplayValue();

    // Sync encoder position
    encoders_.setPosition(ENCODERS[index], normalized);
}

int8_t MacroMidiHandler::findMacroForCC(uint8_t cc) const {
    for (uint8_t i = 0; i < state::MACRO_COUNT; ++i) {
        if (state::MACRO_CC[i] == cc) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

}  // namespace handler
