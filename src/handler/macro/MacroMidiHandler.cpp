#include "MacroMidiHandler.hpp"

#include "midi/MidiUtils.hpp"

namespace core::handler {

MacroMidiHandler::MacroMidiHandler(core::state::CoreState& coreState,
                                             oc::api::EncoderAPI& encoders)
    : core_state_(coreState)
    , encoders_(encoders) {
}

void MacroMidiHandler::onCC(uint8_t channel, uint8_t cc, uint8_t value) {
    handleIncomingCC(channel, cc, value);
}

void MacroMidiHandler::onNoteIn() {
    core_state_.statusBar.pulseNoteIn();
}

void MacroMidiHandler::handleIncomingCC(uint8_t channel, uint8_t cc, uint8_t value) {
    // Signal CC MIDI IN activity
    core_state_.statusBar.pulseCcIn();

    // Find which macro this CC/channel belongs to
    int8_t index = findMacroForCC(channel, cc);
    if (index < 0) return;

    // Convert CC value to normalized float
    float normalized = core::midi::fromCC(value);

    // Update state (triggers UI update, marks dirty for persistence)
    core::state::macro::MacroWorkflow::setRuntimeValue(
        core_state_,
        static_cast<uint8_t>(index),
        normalized
    );

    // Sync hardware surface only when the Macro view is active.
    // When another view repurposes macro encoders (e.g., Sequencer), we must
    // not move the encoder positions based on incoming MIDI.
    if (core_state_.activeView.get() == core::ui::ViewType::MACRO) {
        encoders_.setPosition(Config::MACRO_ENCODERS[index], normalized);
    }
}

int8_t MacroMidiHandler::findMacroForCC(uint8_t channel, uint8_t cc) const {
    for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
        const auto& config = core::state::macro::MacroWorkflow::activeConfig(core_state_, i);
        if (config.cc == cc && config.channel == channel) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

}  // namespace core::handler
