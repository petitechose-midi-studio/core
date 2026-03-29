#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"

#include <cmath>

#include <oc/api/EncoderAPI.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

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

SequencerEncoderSyncCoordinator::SequencerEncoderSyncCoordinator(
    StateRefs state,
    oc::api::EncoderAPI& encoders
)
    : overlays_(state.overlays)
    , active_view_(state.activeView)
    , sequencer_(state.sequencer)
    , encoders_(encoders) {}

FLASHMEM void SequencerEncoderSyncCoordinator::bind() {
    watcher_.watchAll(
        [this]() { syncPositions(); },
        active_view_,
        sequencer_.page,
        sequencer_.length,
        sequencer_.focusedStep,
        sequencer_.activeStepProperty,
        sequencer_.stepEdit.visible,
        sequencer_.stepPropertyInlineSelector.selecting,
        sequencer_.patternQuickControls.selecting,
        sequencer_.rangeSelection.kind,
        sequencer_.rangeSelection.phase,
        sequencer_.rangeSelection.anchorStep,
        sequencer_.rangeSelection.rangeEnd
    );
}

void SequencerEncoderSyncCoordinator::reset() {
    macro_steps_configured_ = 0;
    opt_steps_configured_ = 0;
    macro_ticks_per_step_configured_ = 0;
    opt_ticks_per_step_configured_ = 0;
    macro_turns_configured_ = 0.0f;
    opt_turns_configured_ = 0.0f;
    macro_position_valid_.fill(false);
    opt_position_valid_ = false;
}

void SequencerEncoderSyncCoordinator::syncNow() {
    syncPositions();
}

void SequencerEncoderSyncCoordinator::resetOptCache() {
    opt_steps_configured_ = 0;
    opt_ticks_per_step_configured_ = 0;
    opt_turns_configured_ = 0.0f;
    opt_position_valid_ = false;
}

void SequencerEncoderSyncCoordinator::ensureMacroEncoderConfig(
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

void SequencerEncoderSyncCoordinator::syncMacroEncoderValues(
    uint8_t page,
    core::state::sequencer::StepProperty property
) {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        float normalized = 0.0f;
        uint8_t abs = 0;

        if (sequencer_.resolveStepInPage(page, i, abs)) {
            normalized = input_utils::stepPropertyToNormalized(sequencer_, abs, property);
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

void SequencerEncoderSyncCoordinator::ensureOptEncoderConfig(
    const input_utils::StepPropertyEncoderConfig& config
) {
    if (opt_steps_configured_ == config.discreteSteps &&
        opt_ticks_per_step_configured_ == config.discreteTicksPerStep &&
        !hasMeaningfulEncoderDelta(opt_turns_configured_, config.normalizedTurns)) {
        return;
    }

    applySequencerEncoderConfig(encoders_, Config::EncoderID::OPT, config);
    opt_steps_configured_ = config.discreteSteps;
    opt_ticks_per_step_configured_ = config.discreteTicksPerStep;
    opt_turns_configured_ = config.normalizedTurns;
}

void SequencerEncoderSyncCoordinator::syncOptEncoderValue(
    uint8_t length,
    uint8_t focusedStep,
    core::state::sequencer::StepProperty property
) {
    if (length == 0 ||
        focusedStep >= length ||
        focusedStep >= core::state::sequencer::SequencerState::MAX_STEPS) {
        return;
    }

    const float optPosition =
        input_utils::stepPropertyToNormalized(sequencer_, focusedStep, property);

    if (!opt_position_valid_ || hasMeaningfulEncoderDelta(opt_position_cache_, optPosition)) {
        encoders_.setPosition(Config::EncoderID::OPT, optPosition);
        opt_position_cache_ = optPosition;
        opt_position_valid_ = true;
    }
}

void SequencerEncoderSyncCoordinator::syncPositions() {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) return;

    if (overlays_.hasVisible()) {
        resetOptCache();
        return;
    }

    if (sequencer_.patternQuickControls.selecting.get()) {
        resetOptCache();
        return;
    }

    const uint8_t len = sequencer_.length.get();
    const uint8_t page = sequencer_.normalizePage(sequencer_.page.get());
    const auto property = sequencer_.activeStepProperty.get();
    const auto config = input_utils::encoderConfigForProperty(property);

    ensureMacroEncoderConfig(config);
    syncMacroEncoderValues(page, property);

    if (sequencer_.rangeSelection.active()) {
        if (sequencer_.rangeSelection.selectingSourceRange()) {
            const uint8_t start = sequencer_.rangeSelection.anchorStep.get();
            const uint8_t end = sequencer_.rangeSelection.rangeEnd.get();
            const uint8_t maxStep = static_cast<uint8_t>(len - 1);
            const uint8_t maxSpan = (start < maxStep) ? static_cast<uint8_t>(maxStep - start) : 0;
            const uint8_t currentSpan = (end > start) ? static_cast<uint8_t>(end - start) : 0;

            input_utils::StepPropertyEncoderConfig rangeConfig;
            rangeConfig.discreteSteps = static_cast<uint8_t>(maxSpan + 1);
            rangeConfig.discreteTicksPerStep = input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
            rangeConfig.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;

            ensureOptEncoderConfig(rangeConfig);

            const float normalized = input_utils::indexToNormalized(
                currentSpan,
                static_cast<int>(maxSpan) + 1
            );
            if (!opt_position_valid_ || hasMeaningfulEncoderDelta(opt_position_cache_, normalized)) {
                encoders_.setPosition(Config::EncoderID::OPT, normalized);
                opt_position_cache_ = normalized;
                opt_position_valid_ = true;
            }
        } else {
            resetOptCache();
        }
        return;
    }

    ensureOptEncoderConfig(config);
    syncOptEncoderValue(len, sequencer_.focusedStep.get(), property);
}

}  // namespace core::context::standalone
