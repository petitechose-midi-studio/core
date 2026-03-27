#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"

#include <cmath>

#include <oc/api/EncoderAPI.hpp>

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
    core::state::CoreState& state,
    oc::api::EncoderAPI& encoders
)
    : state_(state)
    , encoders_(encoders) {}

void SequencerEncoderSyncCoordinator::bind() {
    watcher_.watchAll(
        [this]() { syncPositions(); },
        state_.activeView,
        state_.sequencer.page,
        state_.sequencer.length,
        state_.sequencer.focusedStep,
        state_.sequencer.activeStepProperty,
        state_.sequencer.stepEdit.visible,
        state_.sequencer.stepPropertyInlineSelector.selecting,
        state_.sequencer.patternQuickControls.selecting
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

        if (state_.sequencer.resolveStepInPage(page, i, abs)) {
            normalized = input_utils::stepPropertyToNormalized(state_.sequencer, abs, property);
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
        input_utils::stepPropertyToNormalized(state_.sequencer, focusedStep, property);

    if (!opt_position_valid_ || hasMeaningfulEncoderDelta(opt_position_cache_, optPosition)) {
        encoders_.setPosition(Config::EncoderID::OPT, optPosition);
        opt_position_cache_ = optPosition;
        opt_position_valid_ = true;
    }
}

void SequencerEncoderSyncCoordinator::syncPositions() {
    if (state_.activeView.get() != core::ui::ViewType::SEQUENCER) return;

    if (state_.overlays.hasVisible()) {
        resetOptCache();
        return;
    }

    if (state_.sequencer.patternQuickControls.selecting.get()) {
        resetOptCache();
        return;
    }

    const uint8_t len = state_.sequencer.length.get();
    const uint8_t page = state_.sequencer.normalizePage(state_.sequencer.page.get());
    const auto property = state_.sequencer.activeStepProperty.get();
    const auto config = input_utils::encoderConfigForProperty(property);

    ensureMacroEncoderConfig(config);
    syncMacroEncoderValues(page, property);
    ensureOptEncoderConfig(config);
    syncOptEncoderValue(len, state_.sequencer.focusedStep.get(), property);
}

}  // namespace core::context::standalone
