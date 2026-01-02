#include "MacroInputHandler.hpp"

#include <Arduino.h>  // For millis()

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
        encoders_.encoder(ENCODERS[i])
            .turn()
            .scope(scope(scopeElement_))
            .then([this, i](float value) { handleValueChange(i, value); });
    }
}

void MacroInputHandler::handleValueChange(uint8_t index, float value) {
    auto& slot = coreState_.macros.slots[index];

    // Update runtime state (triggers UI update via signal subscription)
    slot.value.set(value);
    slot.updateDisplayValue();

    // Sync to page data for persistence
    coreState_.pages.activePageData().values[index] = value;

    // Get CC/channel from active page config
    const auto& config = coreState_.pages.activeConfigs[index];

    // Send MIDI CC
    uint8_t cc_value = static_cast<uint8_t>(value * 127.0f);
    midi_.sendCC(config.channel, config.cc, cc_value);

    // Mark values dirty for delayed persistence
    coreState_.onValueChanged(millis());
}

}  // namespace handler
