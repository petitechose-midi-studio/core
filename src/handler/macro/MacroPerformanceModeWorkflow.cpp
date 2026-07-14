#include "handler/macro/MacroPerformanceModeWorkflow.hpp"

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"

namespace core::handler {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

float normalizedForProperty(const core::handler::MacroPerformanceDomainServices& services,
                            uint8_t index,
                            core::state::macro::MacroPerformanceProperty property) {
    if (property == core::state::macro::MacroPerformanceProperty::CC) {
        return input_utils::indexToNormalized(services.activeConfig(index).cc, 128);
    }

    if (property == core::state::macro::MacroPerformanceProperty::AUTOMATION) {
        if (!services.automationActiveFor(index)) return 0.0f;
        return services.manualOverrideActiveFor(index) ? 0.0f : 1.0f;
    }

    return services.runtimeValue(index);
}

}  // namespace

FLASHMEM MacroPerformanceModeWorkflow::MacroPerformanceModeWorkflow(
    StateRefs state,
    MacroPerformanceDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders)
    : macro_ui_(state.macroUi)
    , pages_(state.pages)
    , track_ui_(state.trackNavigation)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders) {
    refreshEncoders();
}

bool MacroPerformanceModeWorkflow::clutchActive() const {
    return macro_ui_.clutchActive.get() &&
           !macro_ui_.pageSelection.active.get() &&
           !track_ui_.selection.active.get() &&
           !overlays_.hasVisible();
}

FLASHMEM void MacroPerformanceModeWorkflow::activateClutch() {
    if (overlays_.hasVisible()) return;
    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);
    macro_ui_.activeProperty.set(core::state::macro::MacroPerformanceProperty::CC);
    macro_ui_.clutchActive.set(true);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::deactivateClutch() {
    if (!macro_ui_.clutchActive.get()) return;
    macro_ui_.clutchActive.set(false);
    macro_ui_.activeProperty.set(core::state::macro::MacroPerformanceProperty::VALUE);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::cancelClutch() {
    if (!macro_ui_.clutchActive.get()) return;
    macro_ui_.clutchActive.set(false);
    macro_ui_.activeProperty.set(core::state::macro::MacroPerformanceProperty::VALUE);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::navigateProperty(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const auto currentProperty = macro_ui_.activeProperty.get();
    const int current = core::state::macro::performancePropertyIndex(currentProperty);
    const int next = nav::nextWrappedIndex(delta, current, 2);
    const auto nextProperty = core::state::macro::performancePropertyAtIndex(next);

    macro_ui_.activeProperty.set(nextProperty);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::refreshEncoders() {
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::configureMacroEncoders() {
    const auto property = macro_ui_.clutchActive.get()
        ? macro_ui_.activeProperty.get()
        : core::state::macro::MacroPerformanceProperty::VALUE;

    switch (property) {
        case core::state::macro::MacroPerformanceProperty::CC:
            configureDiscreteEncoders(128);
            break;
        case core::state::macro::MacroPerformanceProperty::AUTOMATION:
            configureDiscreteEncoders(2);
            break;
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            configureValueEncoders();
            break;
    }

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders_.setPosition(Config::MACRO_ENCODERS[i], normalizedForProperty(services_, i, property));
    }
}

FLASHMEM void MacroPerformanceModeWorkflow::configureValueEncoders() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureNormalizedEncoder(Config::MACRO_ENCODERS[i]);
        encoders_.setContinuous(Config::MACRO_ENCODERS[i]);
    }
}

FLASHMEM void MacroPerformanceModeWorkflow::configureDiscreteEncoders(uint8_t discreteSteps) {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureDiscreteEncoder(Config::MACRO_ENCODERS[i], discreteSteps);
    }
}

FLASHMEM void MacroPerformanceModeWorkflow::configureNormalizedEncoder(Config::EncoderID id) {
    encoders_.setDiscreteTicksPerStep(id, input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    encoders_.setNormalizedTurns(id, input_utils::DEFAULT_NORMALIZED_TURNS);
}

FLASHMEM void MacroPerformanceModeWorkflow::configureDiscreteEncoder(
    Config::EncoderID id,
    uint8_t discreteSteps
) {
    configureNormalizedEncoder(id);
    encoders_.setDiscreteSteps(id, discreteSteps);
}

}  // namespace core::handler
