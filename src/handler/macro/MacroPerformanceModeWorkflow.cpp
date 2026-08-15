#include "handler/macro/MacroPerformanceModeWorkflow.hpp"

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"

namespace core::handler {

namespace input_utils = core::handler::sequencer::input_utils;

FLASHMEM MacroPerformanceModeWorkflow::MacroPerformanceModeWorkflow(
    StateRefs state,
    MacroPerformanceDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders)
    : macro_ui_(state.macroUi)
    , track_ui_(state.trackNavigation)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders) {
    refreshEncoders();
}

FLASHMEM bool MacroPerformanceModeWorkflow::performanceOverlayActive() const {
    return macro_ui_.performanceOverlayMode.get() !=
               core::state::macro::MacroPerformanceOverlayMode::NONE &&
           !overlays_.hasVisible();
}

FLASHMEM void MacroPerformanceModeWorkflow::openEditPrompt() {
    if (overlays_.hasVisible()) return;
    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);
    macro_ui_.performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::EDIT
    );
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::closePerformanceOverlay() {
    if (macro_ui_.performanceOverlayMode.get() ==
        core::state::macro::MacroPerformanceOverlayMode::NONE) {
        return;
    }
    macro_ui_.performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::NONE
    );
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::navigateTakeTiming(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    (void)services_.navigateAutomationTakeTiming(delta > 0.0f ? 1 : -1);
}

FLASHMEM void MacroPerformanceModeWorkflow::refreshEncoders() {
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::configureMacroEncoders() {
    configureValueEncoders();
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders_.setPosition(
            Config::MACRO_ENCODERS[i],
            services_.absoluteBaseValue(i)
        );
    }
}

FLASHMEM void MacroPerformanceModeWorkflow::configureValueEncoders() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureNormalizedEncoder(Config::MACRO_ENCODERS[i]);
        encoders_.setContinuous(Config::MACRO_ENCODERS[i]);
    }
}

FLASHMEM void MacroPerformanceModeWorkflow::configureNormalizedEncoder(Config::EncoderID id) {
    encoders_.setDiscreteTicksPerStep(id, input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    encoders_.setNormalizedTurns(id, input_utils::DEFAULT_NORMALIZED_TURNS);
}

}  // namespace core::handler
