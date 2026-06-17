#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"

#include <cmath>

#include <oc/api/EncoderAPI.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"

namespace core::context::standalone {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

constexpr float ENCODER_POSITION_EPSILON = 0.0005f;

inline bool hasMeaningfulEncoderDelta(float a, float b) {
    return std::fabs(a - b) > ENCODER_POSITION_EPSILON;
}

template <typename EncoderIdT>
inline void applySequencerEncoderConfig(
    oc::api::EncoderAPI& encoders,
    EncoderIdT encoderId,
    const input_utils::StepPropertyEncoderConfig& config
) {
    encoders.setDiscreteTicksPerStep(encoderId, config.discreteTicksPerStep);
    encoders.setNormalizedTurns(encoderId, config.normalizedTurns);
    encoders.setDiscreteSteps(encoderId, config.discreteSteps);
}

}  // namespace

FLASHMEM SequencerEncoderSyncCoordinator::SequencerEncoderSyncCoordinator(
    StateRefs state,
    oc::api::EncoderAPI& encoders
)
    : overlays_(state.overlays)
    , active_view_(state.activeView)
    , sequencer_(state.sequencer)
    , track_bank_(state.trackBank)
    , encoders_(encoders) {}

FLASHMEM void SequencerEncoderSyncCoordinator::bind() {
    watcher_.watchAll(
        [this]() { syncPositions(); },
        active_view_,
        sequencer_.page,
        sequencer_.pattern.length,
        sequencer_.pattern.graphRevision,
        sequencer_.focusedStep,
        sequencer_.activeStepProperty,
        sequencer_.contentView.kind,
        sequencer_.contentView.length,
        sequencer_.contentView.revision,
        sequencer_.pattern.patternScaleRevision,
        sequencer_.stepEdit.visible,
        sequencer_.stepPropertyInlineSelector.selecting,
        sequencer_.stepPropertyInlineSelector.macroLocalVariationEditActive,
        sequencer_.patternQuickControls.selecting,
        sequencer_.patternQuickControls.physicalHoldActive
    );
}

FLASHMEM void SequencerEncoderSyncCoordinator::reset() {
    macro_steps_configured_ = 0;
    macro_ticks_per_step_configured_ = 0;
    macro_turns_configured_ = 0.0f;
    macro_position_valid_.fill(false);
}

FLASHMEM void SequencerEncoderSyncCoordinator::syncNow() {
    syncPositions();
}

FLASHMEM void SequencerEncoderSyncCoordinator::ensureMacroEncoderConfig(
    const input_utils::StepPropertyEncoderConfig& config
) {
    if (macro_steps_configured_ == config.discreteSteps &&
        macro_ticks_per_step_configured_ == config.discreteTicksPerStep &&
        !hasMeaningfulEncoderDelta(macro_turns_configured_, config.normalizedTurns)) {
        return;
    }

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        applySequencerEncoderConfig(encoders_, Config::MACRO_ENCODERS[i], config);
    }

    macro_steps_configured_ = config.discreteSteps;
    macro_ticks_per_step_configured_ = config.discreteTicksPerStep;
    macro_turns_configured_ = config.normalizedTurns;
}

FLASHMEM void SequencerEncoderSyncCoordinator::syncMacroEncoderValues(
    uint8_t page,
    core::state::sequencer::StepProperty property
) {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        float normalized = 0.0f;
        uint8_t abs = 0;

        if (core::state::sequencer::resolveActiveContentStepInPage(sequencer_, page, i, abs)) {
            normalized = core::state::sequencer::activeContentStepPropertyToNormalized(
                sequencer_,
                abs,
                property,
                sequencer_.pattern.pitchEditMode,
                core::state::sequencer::resolveEffectiveScaleSettings(
                    track_bank_.projectScaleSettings(),
                    sequencer_.pattern.scalePolicy,
                    sequencer_.pattern.scaleOverride
                )
            );
        }

        normalized = input_utils::clampNormalized(normalized);

        if (!macro_position_valid_[i] ||
            hasMeaningfulEncoderDelta(macro_position_cache_[i], normalized)) {
            encoders_.setPosition(Config::MACRO_ENCODERS[i], normalized);
            macro_position_cache_[i] = normalized;
            macro_position_valid_[i] = true;
        }
    }
}

FLASHMEM void SequencerEncoderSyncCoordinator::syncMacroLocalVariationValues(
    uint8_t page,
    core::state::sequencer::StepProperty property
) {
    const auto* graph = core::state::sequencer::graphView(sequencer_.pattern);
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        float normalized = 0.0f;
        uint8_t abs = 0;

        if (graph != nullptr &&
            core::state::sequencer::resolveActiveContentStepInPage(sequencer_, page, i, abs)) {
            const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer_, abs);
            const auto* node = graph->stepNode(nodeId);
            if (node != nullptr) {
                normalized = input_utils::variationRangeToNormalized(
                    property,
                    core::state::sequencer::nodeLocalVariationRange(*node, property)
                );
            }
        }

        normalized = input_utils::clampNormalized(normalized);

        if (!macro_position_valid_[i] ||
            hasMeaningfulEncoderDelta(macro_position_cache_[i], normalized)) {
            encoders_.setPosition(Config::MACRO_ENCODERS[i], normalized);
            macro_position_cache_[i] = normalized;
            macro_position_valid_[i] = true;
        }
    }
}

FLASHMEM void SequencerEncoderSyncCoordinator::syncPositions() {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) return;

    if (overlays_.hasVisible()) {
        return;
    }

    const uint8_t page = core::state::sequencer::normalizeActiveContentPage(
        sequencer_,
        sequencer_.page.get()
    );
    const auto property = sequencer_.activeStepProperty.get();

    if (sequencer_.stepPropertyInlineSelector.selecting.get()) {
        if (property == core::state::sequencer::StepProperty::PROBABILITY) {
            return;
        }
        const auto config = input_utils::encoderConfigForVariationRange(property);
        ensureMacroEncoderConfig(config);
        syncMacroLocalVariationValues(page, property);
        return;
    }

    if (sequencer_.patternQuickControls.selecting.get()) {
        return;
    }

    const auto effectiveScale = core::state::sequencer::resolveEffectiveScaleSettings(
        track_bank_.projectScaleSettings(),
        sequencer_.pattern.scalePolicy,
        sequencer_.pattern.scaleOverride
    );
    const auto config = input_utils::encoderConfigForProperty(
        property,
        sequencer_.pattern.pitchEditMode,
        effectiveScale
    );

    ensureMacroEncoderConfig(config);
    syncMacroEncoderValues(page, property);
}

}  // namespace core::context::standalone
