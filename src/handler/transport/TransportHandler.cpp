#include "TransportHandler.hpp"

#include <algorithm>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::handler {

using namespace oc::ui::lvgl;

TransportHandler::TransportHandler(core::state::CoreState& coreState,
                                              oc::api::EncoderAPI& encoders,
                                              oc::api::ButtonAPI& buttons,
                                              lv_obj_t* tempoScopeElement,
                                              lv_obj_t* transportScopeElement)
    : core_state_(coreState)
    , encoders_(encoders)
    , buttons_(buttons)
    , tempo_scope_element_(tempoScopeElement)
    , transport_scope_element_(transportScopeElement) {
    setupBindings();
}

void TransportHandler::setupBindings() {
    // NAV encoder: tempo +/- 1 BPM per tick (expects EncoderMode::RELATIVE)
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(tempo_scope_element_))
        .then([this](float delta) { handleTempoChange(delta); });

    // BOTTOM_CENTER button: toggle play (global)
    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .release()
        .then([this]() { handlePlayToggle(); });
}

void TransportHandler::handleTempoChange(float delta) {
    float currentTempo = core_state_.statusBar.tempo.get();
    float newTempo = std::clamp(currentTempo + delta, TEMPO_MIN, TEMPO_MAX);
    core_state_.statusBar.tempo.set(newTempo);
}

void TransportHandler::handlePlayToggle() {
    bool playing = core_state_.statusBar.playing.get();
    core_state_.statusBar.playing.set(!playing);
}

}  // namespace core::handler
