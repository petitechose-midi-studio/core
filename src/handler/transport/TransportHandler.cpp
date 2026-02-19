#include "TransportHandler.hpp"

#include <algorithm>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::handler {

using namespace oc::ui::lvgl;

TransportHandler::TransportHandler(core::state::CoreState& coreState,
                                               oc::api::EncoderAPI& encoders,
                                               oc::api::ButtonAPI& buttons,
                                               lv_obj_t* tempoScopeElement,
                                               TransportHandler::ViewScopes playToggleScopes)
    : core_state_(coreState)
    , encoders_(encoders)
    , buttons_(buttons)
    , tempo_scope_element_(tempoScopeElement)
    , play_toggle_scopes_(playToggleScopes) {
    setupBindings();
}

void TransportHandler::setupBindings() {
    // NAV encoder: tempo +/- 1 BPM per tick (expects EncoderMode::RELATIVE)
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(tempo_scope_element_))
        .then([this](float delta) { handleTempoChange(delta); });

    // BOTTOM_CENTER button: toggle play from any active top-level view scope
    lv_obj_t* lastBoundScope = nullptr;
    for (auto* playScope : play_toggle_scopes_) {
        if (!playScope || playScope == lastBoundScope) continue;

        buttons_.button(Config::ButtonID::BOTTOM_CENTER)
            .release()
            .scope(scope(playScope))
            .then([this]() { handlePlayToggle(); });

        lastBoundScope = playScope;
    }
}

void TransportHandler::handleTempoChange(float delta) {
    if (core_state_.statusBar.tempoLocked.get()) {
        return;
    }

    float currentTempo = core_state_.statusBar.tempo.get();
    float newTempo = std::clamp(currentTempo + delta, TEMPO_MIN, TEMPO_MAX);
    core_state_.statusBar.tempo.set(newTempo);
}

void TransportHandler::handlePlayToggle() {
    if (core_state_.statusBar.transportLocked.get()) {
        return;
    }

    bool playing = core_state_.statusBar.playing.get();
    core_state_.statusBar.playing.set(!playing);
}

}  // namespace core::handler
