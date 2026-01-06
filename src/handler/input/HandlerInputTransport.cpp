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
    : coreState_(coreState)
    , encoders_(encoders)
    , buttons_(buttons)
    , scopeElement_(scopeElement) {
    setupBindings();
}

void HandlerInputTransport::setupBindings() {
    // Set NAV encoder to relative mode (gives delta values)
    encoders_.setMode(Config::EncoderID::NAV, oc::hal::EncoderMode::RELATIVE);

    // NAV encoder: tempo +/- 1 BPM per tick (delta from relative mode)
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(scopeElement_))
        .then([this](float delta) { handleTempoChange(delta); });

    // BOTTOM_CENTER button: toggle play
    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .press()
        .scope(scope(scopeElement_))
        .then([this]() { handlePlayToggle(); });
}

void HandlerInputTransport::handleTempoChange(float delta) {
    float currentTempo = coreState_.statusBar.tempo.get();
    float newTempo = std::clamp(currentTempo + delta, TEMPO_MIN, TEMPO_MAX);
    coreState_.statusBar.tempo.set(newTempo);
}

void HandlerInputTransport::handlePlayToggle() {
    bool playing = coreState_.statusBar.playing.get();
    coreState_.statusBar.playing.set(!playing);
}

}  // namespace core::handler
