#include "MacroInputHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

namespace handler {

using namespace oc::ui::lvgl;

MacroInputHandler::MacroInputHandler(state::MacroState& state,
                                     oc::api::EncoderAPI& encoders,
                                     oc::api::MidiAPI& midi,
                                     lv_obj_t* scopeElement)
    : state_(state)
    , encoders_(encoders)
    , midi_(midi)
    , scopeElement_(scopeElement) {
    setupBindings();
}

void MacroInputHandler::setupBindings() {
    for (uint8_t i = 0; i < state::MACRO_COUNT; ++i) {
        encoders_.encoder(ENCODERS[i])
            .turn()
            .scope(scope(scopeElement_))
            .then([this, i](float value) { handleValueChange(i, value); });
    }
}

void MacroInputHandler::handleValueChange(uint8_t index, float value) {
    auto& slot = state_.slots[index];

    // Update state (triggers UI update via signal subscription)
    slot.value.set(value);
    slot.updateDisplayValue();

    // Send MIDI CC
    uint8_t cc_value = static_cast<uint8_t>(value * 127.0f);
    midi_.sendCC(state::MACRO_CHANNEL, state::MACRO_CC[index], cc_value);
}

}  // namespace handler
