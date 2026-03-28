#include "SequencerMacroPropertyHandler.hpp"

#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "SequencerInputUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
namespace input_utils = core::handler::sequencer::input_utils;

namespace {

inline oc::type::IsActiveFn canEditSequencerProperty(core::state::CoreState& state) {
    return [&state]() {
        return !state.sequencer.patternQuickControls.selecting.get() &&
               !state.sequencer.rangeSelection.active() &&
               !state.overlays.hasVisible();
    };
}

}  // namespace

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

FLASHMEM void SequencerMacroPropertyHandler::setupBindings() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope(scope_element_))
            .when(canEditSequencerProperty(state_))
            .then([this, i](float value) { handleTurn(i, value); });
    }

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope(scope_element_))
        .when(canEditSequencerProperty(state_))
        .then([this](float value) { handleFocusedTurn(value); });
}

void SequencerMacroPropertyHandler::handleTurn(uint8_t indexInPage, float normalized) {
    uint8_t abs = 0;
    if (!state_.sequencer.resolveStepInPage(state_.sequencer.page.get(), indexInPage, abs)) return;
    const auto property = state_.sequencer.activeStepProperty.get();

    input_utils::applyNormalizedToStep(
        state_.sequencer,
        abs,
        property,
        normalized
    );
    state_.sequencer.stepInlineFeedback.show(abs, property, oc::time::millis());
}

void SequencerMacroPropertyHandler::handleFocusedTurn(float normalized) {
    if (state_.overlays.hasVisible()) return;

    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t focused = state_.sequencer.focusedStep.get();
    if (focused >= len) return;
    if (focused >= core::state::sequencer::SequencerState::MAX_STEPS) return;
    const auto property = state_.sequencer.activeStepProperty.get();

    input_utils::applyNormalizedToStep(
        state_.sequencer,
        focused,
        property,
        normalized
    );
    state_.sequencer.stepInlineFeedback.show(focused, property, oc::time::millis());
}

}  // namespace core::handler
