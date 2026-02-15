#include "SequencerMacroPropertyHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;

SequencerMacroPropertyHandler::SequencerMacroPropertyHandler(
    core::state::CoreState& state,
    oc::api::EncoderAPI& encoders,
    lv_obj_t* sequencerViewScope
)
    : state_(state)
    , encoders_(encoders)
    , scope_element_(sequencerViewScope) {
    setupBindings();
}

void SequencerMacroPropertyHandler::setupBindings() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope(scope_element_))
            .then([this, i](float value) { handleTurn(i, value); });
    }
}

void SequencerMacroPropertyHandler::handleTurn(uint8_t indexInPage, float normalized) {
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;

    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t abs = static_cast<uint8_t>(state_.sequencer.page.get() * stepsPerPage + indexInPage);
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    const auto prop = state_.sequencer.activeStepProperty.get();
    if (prop == core::state::sequencer::StepProperty::NOTE) {
        int note = static_cast<int>(normalized * 127.0f + 0.5f);
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        state_.sequencer.note[abs] = static_cast<uint8_t>(note);
        bumpRevision();
    } else if (prop == core::state::sequencer::StepProperty::VELOCITY) {
        int vel = static_cast<int>(normalized * 127.0f + 0.5f);
        if (vel < 0) vel = 0;
        if (vel > 127) vel = 127;
        state_.sequencer.velocity[abs] = static_cast<uint8_t>(vel);
        bumpRevision();
    } else if (prop == core::state::sequencer::StepProperty::GATE) {
        int gate = static_cast<int>(normalized * 100.0f + 0.5f);
        if (gate < 0) gate = 0;
        if (gate > 100) gate = 100;
        state_.sequencer.gate[abs] = static_cast<uint16_t>(gate);
        bumpRevision();
    }
}

void SequencerMacroPropertyHandler::bumpRevision() {
    state_.sequencer.stepDataRevision.set(state_.sequencer.stepDataRevision.get() + 1);
}

}  // namespace core::handler
