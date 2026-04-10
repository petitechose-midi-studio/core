#include "TransportHandler.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::handler {

TransportHandler::TransportHandler(StateRefs state,
                                   oc::api::EncoderAPI& encoders,
                                   oc::api::ButtonAPI& buttons,
                                   oc::type::ScopeID tempoScope,
                                   TransportHandler::ViewScopes playToggleScopes)
    : status_bar_(state.statusBar)
    , encoders_(encoders)
    , buttons_(buttons)
    , tempo_scope_id_(tempoScope)
    , play_toggle_scopes_(playToggleScopes) {
    setupBindings();
}

FLASHMEM void TransportHandler::setupBindings() {
    for (const auto scope : play_toggle_scopes_) {
        buttons_.button(Config::ButtonID::BOTTOM_CENTER)
            .release()
            .scope(scope)
            .then([this]() { handlePlayToggle(); });
    }
}

void TransportHandler::handleTempoChange(float delta) {
    if (status_bar_.tempoLocked.get()) {
        return;
    }

    float currentTempo = status_bar_.tempo.get();
    float newTempo = std::clamp(currentTempo + delta, TEMPO_MIN, TEMPO_MAX);
    status_bar_.tempo.set(newTempo);
}

void TransportHandler::handlePlayToggle() {
    if (status_bar_.transportLocked.get()) {
        return;
    }

    bool playing = status_bar_.playing.get();
    status_bar_.playing.set(!playing);
}

}  // namespace core::handler
