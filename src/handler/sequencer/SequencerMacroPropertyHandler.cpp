#include "SequencerMacroPropertyHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "SequencerInputUtils.hpp"

namespace core::handler {
namespace input_utils = core::handler::sequencer::input_utils;

namespace {

inline oc::type::IsActiveFn canEditSequencerProperty(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi
) {
    return [&overlays, &sequencer, &trackUi]() {
        return !sequencer.structureUi.pageSelection.active.get() &&
               !trackUi.selection.active.get() &&
               !sequencer.patternQuickControls.selecting.get() &&
               !overlays.hasVisible();
    };
}

}  // namespace

SequencerMacroPropertyHandler::SequencerMacroPropertyHandler(
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::type::ScopeID scopeId,
    NowProvider nowProvider
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , track_ui_(state.trackNavigation)
    , encoders_(encoders)
    , scope_id_(scopeId)
    , now_provider_(nowProvider) {
    setupBindings();
}

FLASHMEM void SequencerMacroPropertyHandler::setupBindings() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope_id_)
            .when(canEditSequencerProperty(overlays_, sequencer_, track_ui_))
            .then([this, i](float value) { handleTurn(i, value); });
    }

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when(canEditSequencerProperty(overlays_, sequencer_, track_ui_))
        .then([this](float value) { handleFocusedTurn(value); });
}

void SequencerMacroPropertyHandler::handleTurn(uint8_t indexInPage, float normalized) {
    uint8_t abs = 0;
    if (!sequencer_.resolveStepInPage(sequencer_.page.get(), indexInPage, abs)) return;
    const auto property = sequencer_.activeStepProperty.get();

    input_utils::applyNormalizedToStep(
        sequencer_,
        abs,
        property,
        normalized
    );
    sequencer_.stepInlineFeedback.show(abs, property, now_provider_ ? now_provider_() : 0);
}

void SequencerMacroPropertyHandler::handleFocusedTurn(float normalized) {
    if (overlays_.hasVisible()) return;

    const uint8_t len = sequencer_.length.get();
    if (len == 0) return;

    const uint8_t focused = sequencer_.focusedStep.get();
    if (focused >= len) return;
    if (focused >= core::state::sequencer::SequencerState::MAX_STEPS) return;
    const auto property = sequencer_.activeStepProperty.get();

    input_utils::applyNormalizedToStep(
        sequencer_,
        focused,
        property,
        normalized
    );
    sequencer_.stepInlineFeedback.show(focused, property, now_provider_ ? now_provider_() : 0);
}

}  // namespace core::handler
