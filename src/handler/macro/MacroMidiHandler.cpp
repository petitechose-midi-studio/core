#include "MacroMidiHandler.hpp"

#include <config/PlatformCompat.hpp>

#include "midi/MidiUtils.hpp"

namespace core::handler {

FLASHMEM MacroMidiHandler::MacroMidiHandler(StateRefs state,
                                   MacroPerformanceDomainServices services,
                                   oc::api::EncoderAPI& encoders)
    : active_view_(state.activeView)
    , services_(services)
    , encoders_(encoders) {
}

void MacroMidiHandler::onCC(uint8_t channel, uint8_t cc, uint8_t value) {
    handleIncomingCC(channel, cc, value);
}

void MacroMidiHandler::onNoteIn() {
    services_.pulseNoteIn();
}

void MacroMidiHandler::handleIncomingCC(uint8_t channel, uint8_t cc, uint8_t value) {
    // Signal CC MIDI IN activity
    services_.pulseCcIn();

    // Find which macro this CC/channel belongs to
    const int8_t index = findMacroForCC(channel, cc);
    if (index < 0) return;
    const uint8_t macroIndex = static_cast<uint8_t>(index);

    // Convert CC value to normalized float
    const float normalized = core::midi::fromCC(value);

    if (!services_.automationRecordingActiveFor(macroIndex) &&
        services_.automationActiveFor(macroIndex)) {
        services_.setAutomationManualOverride(macroIndex, true);
    }

    // Incoming mapped CC is a manual performance input and updates the
    // persistable base value, unlike automation-resolved playback.
    services_.setManualValue(macroIndex, normalized);

    // Sync hardware surface only when the Macro view is active.
    // When another view repurposes macro encoders (e.g., Sequencer), we must
    // not move the encoder positions based on incoming MIDI.
    if (active_view_.get() == core::ui::ViewType::MACRO) {
        encoders_.setPosition(Config::MACRO_ENCODERS[macroIndex], normalized);
    }
}

int8_t MacroMidiHandler::findMacroForCC(uint8_t channel, uint8_t cc) const {
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        if (!services_.isMacroSlotActive(i)) continue;
        const auto& config = services_.activeConfig(i);
        if (config.cc == cc && config.channel == channel) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

}  // namespace core::handler
