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

FLASHMEM SequencerMacroPropertyHandler::SequencerMacroPropertyHandler(
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::type::ScopeID scopeId,
    NowProvider nowProvider
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , track_bank_(state.trackBank)
    , track_ui_(state.trackNavigation)
    , history_(state.history)
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
}

FLASHMEM void SequencerMacroPropertyHandler::handleTurn(uint8_t indexInPage, float normalized) {
    uint8_t abs = 0;
    if (!sequencer_.resolveStepInPage(sequencer_.page.get(), indexInPage, abs)) return;
    const auto property = sequencer_.activeStepProperty.get();
    const uint32_t now = now_provider_ ? now_provider_() : 0;

    history_.beginCoalescedPatternEdit(abs, property, now);

    input_utils::applyNormalizedToStep(
        sequencer_,
        abs,
        property,
        normalized,
        sequencer_.pattern.pitchEditMode,
        core::state::sequencer::resolveEffectiveScaleSettings(
            track_bank_.projectScaleSettings(),
            sequencer_.pattern.scalePolicy,
            sequencer_.pattern.scaleOverride
        )
    );
    sequencer_.stepInlineFeedback.show(abs, property, now);
}

}  // namespace core::handler
