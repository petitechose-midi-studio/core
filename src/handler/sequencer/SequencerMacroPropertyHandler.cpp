#include "SequencerMacroPropertyHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

#include "SequencerInputUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
namespace input_utils = core::handler::sequencer::input_utils;

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

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope(scope_element_))
        .then([this](float value) { handleFocusedTurn(value); });
}

void SequencerMacroPropertyHandler::handleTurn(uint8_t indexInPage, float normalized) {
    const float value = input_utils::clampNormalized(normalized);

    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t pageCount = static_cast<uint8_t>((len + stepsPerPage - 1) / stepsPerPage);
    const uint8_t page = static_cast<uint8_t>(state_.sequencer.page.get() % pageCount);
    const uint8_t abs = static_cast<uint8_t>(page * stepsPerPage + indexInPage);
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    const auto prop = state_.sequencer.activeStepProperty.get();
    if (prop == core::state::sequencer::StepProperty::NOTE) {
        state_.sequencer.note[abs] = input_utils::normalizedToMidi7(value);
        bumpRevision();
    } else if (prop == core::state::sequencer::StepProperty::VELOCITY) {
        state_.sequencer.velocity[abs] = input_utils::normalizedToMidi7(value);
        bumpRevision();
    } else if (prop == core::state::sequencer::StepProperty::GATE) {
        state_.sequencer.gate[abs] = input_utils::normalizedToGatePercent(value);
        bumpRevision();
    }
}

void SequencerMacroPropertyHandler::handleFocusedTurn(float normalized) {
    if (state_.overlays.hasVisible()) return;

    const float value = input_utils::clampNormalized(normalized);
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t focused = state_.sequencer.focusedStep.get();
    if (focused >= len) return;
    if (focused >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    const auto prop = state_.sequencer.activeStepProperty.get();
    if (prop == core::state::sequencer::StepProperty::NOTE) {
        state_.sequencer.note[focused] = input_utils::normalizedToMidi7(value);
    } else if (prop == core::state::sequencer::StepProperty::VELOCITY) {
        state_.sequencer.velocity[focused] = input_utils::normalizedToMidi7(value);
    } else if (prop == core::state::sequencer::StepProperty::GATE) {
        state_.sequencer.gate[focused] = input_utils::normalizedToGatePercent(value);
    }

    bumpRevision();
}

void SequencerMacroPropertyHandler::bumpRevision() {
    state_.sequencer.stepDataRevision.set(state_.sequencer.stepDataRevision.get() + 1);
}

}  // namespace core::handler
