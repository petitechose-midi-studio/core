#include "MacroInputHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

namespace handler {

using namespace oc::ui::lvgl;

MacroInputHandler::MacroInputHandler(state::CoreState& coreState,
                                     oc::api::EncoderAPI& encoders,
                                     oc::api::MidiAPI& midi,
                                     lv_obj_t* scopeElement)
    : coreState_(coreState)
    , encoders_(encoders)
    , midi_(midi)
    , scopeElement_(scopeElement) {
    setupBindings();
}

void MacroInputHandler::setupBindings() {
    for (uint8_t i = 0; i < state::MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope(scopeElement_))
            .then([this, i](float value) { handleValueChange(i, value); });
    }
}

void MacroInputHandler::handleValueChange(uint8_t index, float value) {
    // Update state (triggers UI update, marks dirty for persistence)
    coreState_.setMacroValue(index, value);

    // Send MIDI CC
    const auto& config = coreState_.getMacroConfig(index);
    uint8_t cc_value = static_cast<uint8_t>(value * 127.0f);
    midi_.sendCC(config.channel, config.cc, cc_value);

    // Signal MIDI OUT activity
    coreState_.statusBar.midiOutActive.set(true);
}

}  // namespace handler
