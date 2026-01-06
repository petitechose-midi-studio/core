#include "HandlerInputTransport.hpp"

#include <algorithm>
#include <oc/hal/IEncoderController.hpp>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::handler {

using namespace oc::ui::lvgl;

HandlerInputTransport::HandlerInputTransport(core::state::CoreState& coreState,
                                             oc::api::EncoderAPI& encoders,
                                             oc::api::ButtonAPI& buttons,
                                             lv_obj_t* scopeElement)
    : core_state_(coreState)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_element_(scopeElement) {
    setupBindings();
}

void HandlerInputTransport::setupBindings() {
    // Set NAV encoder to relative mode (gives delta values)
    encoders_.setMode(Config::EncoderID::NAV, oc::hal::EncoderMode::RELATIVE);

    // NAV encoder: tempo +/- 1 BPM per tick (delta from relative mode)
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(scope_element_))
        .then([this](float delta) { handleTempoChange(delta); });

    // BOTTOM_CENTER button: toggle play
    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .press()
        .scope(scope(scope_element_))
        .then([this]() { handlePlayToggle(); });
}

void HandlerInputTransport::handleTempoChange(float delta) {
    float currentTempo = core_state_.statusBar.tempo.get();
    float newTempo = std::clamp(currentTempo + delta, TEMPO_MIN, TEMPO_MAX);
    core_state_.statusBar.tempo.set(newTempo);
}

void HandlerInputTransport::handlePlayToggle() {
    bool playing = core_state_.statusBar.playing.get();
    core_state_.statusBar.playing.set(!playing);
}

}  // namespace core::handler
