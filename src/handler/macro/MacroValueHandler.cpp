#include "MacroValueHandler.hpp"

#include <algorithm>

#include <oc/ui/lvgl/Scope.hpp>

#include "midi/MidiUtils.hpp"

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
    const float clamped = std::clamp(value, 0.0f, 1.0f);

    // Update state (triggers UI update, marks dirty for persistence)
    core_state_.setMacroValue(index, clamped);

    // Send MIDI CC
    const auto& config = core_state_.getMacroConfig(index);
    uint8_t cc_value = core::midi::toCC(clamped);
    midi_.sendCC(config.channel, config.cc, cc_value);

    // Signal CC MIDI OUT activity
    core_state_.statusBar.ccOutActive.set(true);
}

}  // namespace core::handler
