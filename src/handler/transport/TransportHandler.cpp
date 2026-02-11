#include "TransportHandler.hpp"

#include <algorithm>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::handler {

using namespace oc::ui::lvgl;

TransportHandler::TransportHandler(core::state::CoreState& coreState,
                                             oc::api::EncoderAPI& encoders,
                                             oc::api::ButtonAPI& buttons,
                                             lv_obj_t* scopeElement)
    : core_state_(coreState)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_element_(scopeElement) {
    setupBindings();
}

void TransportHandler::setupBindings() {
    // NAV encoder: tempo +/- 1 BPM per tick (expects EncoderMode::RELATIVE)
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

void TransportHandler::handleTempoChange(float delta) {
    if (core_state_.viewSelector.visible.get()) return;
    if (core_state_.macroEdit.visible.get()) return;
    if (core_state_.activeView.get() == core::ui::ViewType::SEQUENCER) return;

    float currentTempo = core_state_.statusBar.tempo.get();
    float newTempo = std::clamp(currentTempo + delta, TEMPO_MIN, TEMPO_MAX);
    core_state_.statusBar.tempo.set(newTempo);
}

void TransportHandler::handlePlayToggle() {
    bool playing = core_state_.statusBar.playing.get();
    core_state_.statusBar.playing.set(!playing);
}

}  // namespace core::handler
