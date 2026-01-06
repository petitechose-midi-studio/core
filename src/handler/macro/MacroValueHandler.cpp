#include "MacroValueHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

namespace core::handler {

using namespace oc::ui::lvgl;

MacroValueHandler::MacroValueHandler(core::state::CoreState& coreState,
                                     oc::api::EncoderAPI& encoders,
                                     oc::api::MidiAPI& midi,
                                     lv_obj_t* scopeElement)
    : core_state_(coreState)
    , encoders_(encoders)
    , midi_(midi)
    , scope_element_(scopeElement) {
    setupBindings();
}

void MacroValueHandler::setupBindings() {
    for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope(scope_element_))
            .then([this, i](float value) { handleValueChange(i, value); });
    }
}

void MacroValueHandler::handleValueChange(uint8_t index, float value) {
    // Update state (triggers UI update, marks dirty for persistence)
    core_state_.setMacroValue(index, value);

    // Send MIDI CC
    const auto& config = core_state_.getMacroConfig(index);
    uint8_t cc_value = static_cast<uint8_t>(value * 127.0f);
    midi_.sendCC(config.channel, config.cc, cc_value);

    // Signal CC MIDI OUT activity
    core_state_.statusBar.ccOutActive.set(true);
}

}  // namespace core::handler
