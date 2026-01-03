#include "MacroMidiHandler.hpp"

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
    // Signal MIDI IN activity
    coreState_.statusBar.midiInActive.set(true);

    // Find which macro this CC/channel belongs to
    int8_t index = findMacroForCC(channel, cc);
    if (index < 0) return;

    // Convert CC value to normalized float
    float normalized = static_cast<float>(value) / 127.0f;

    // Update state (triggers UI update, marks dirty for persistence)
    coreState_.setMacroValue(static_cast<uint8_t>(index), normalized);

    // Sync encoder position
    encoders_.setPosition(Config::MACRO_ENCODERS[index], normalized);
}

int8_t MacroMidiHandler::findMacroForCC(uint8_t channel, uint8_t cc) const {
    for (uint8_t i = 0; i < state::MACRO_COUNT; ++i) {
        const auto& config = coreState_.getMacroConfig(i);
        if (config.cc == cc && config.channel == channel) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

}  // namespace handler
