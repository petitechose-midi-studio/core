#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"

#include <algorithm>
#include <cmath>

#include <oc/api/EncoderAPI.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"

namespace core::context::standalone {

namespace input_utils = core::handler::sequencer::input_utils;
namespace interaction_policy = core::handler::sequencer::interaction_policy;

namespace {

constexpr float ENCODER_POSITION_EPSILON = 0.0005f;
constexpr uint8_t MICRO_LENGTH_MIN = 2;
constexpr uint8_t MICRO_LENGTH_MAX =
    oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;
constexpr uint8_t CYCLE_LENGTH_MIN = 1;
constexpr uint8_t CYCLE_LENGTH_MAX =
    oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET;

inline bool hasMeaningfulEncoderDelta(float a, float b) {
    return std::fabs(a - b) > ENCODER_POSITION_EPSILON;
}

struct ChildLengthRange {
    uint8_t min = 1;
    uint8_t max = 1;
};

using QuickItem = core::state::sequencer::PatternQuickControlItem;

FLASHMEM ChildLengthRange activeChildLengthRange(
    const core::state::sequencer::SequencerState& sequencer
) {
    if (core::state::sequencer::isCycleStatesContentView(sequencer)) {
        return {.min = CYCLE_LENGTH_MIN, .max = CYCLE_LENGTH_MAX};
    }
    return {.min = MICRO_LENGTH_MIN, .max = MICRO_LENGTH_MAX};
}

FLASHMEM float childLengthToNormalized(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t length
) {
    const auto range = activeChildLengthRange(sequencer);
    const uint8_t clamped = std::clamp<uint8_t>(length, range.min, range.max);
    return input_utils::indexToNormalized(
        static_cast<int>(clamped - range.min),
        static_cast<int>((range.max - range.min) + 1U)
    );
}

FLASHMEM uint8_t activeChildLengthStepCount(
    const core::state::sequencer::SequencerState& sequencer
) {
    const auto range = activeChildLengthRange(sequencer);
    return static_cast<uint8_t>((range.max - range.min) + 1U);
}

FLASHMEM int currentOffsetMax(const core::state::sequencer::SequencerState& sequencer) {
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer);
    return (len > 0) ? static_cast<int>(len - 1) : 0;
}

FLASHMEM float offsetToNormalized(
    const core::state::sequencer::SequencerState& sequencer,
    int offsetSteps
) {
    const int maxOffset = currentOffsetMax(sequencer);
    if (maxOffset <= 0) return 0.5f;
    const int clamped = std::clamp(offsetSteps, -maxOffset, maxOffset);
    return static_cast<float>(clamped + maxOffset) / static_cast<float>(maxOffset * 2);
}

FLASHMEM input_utils::StepPropertyEncoderConfig offsetEncoderConfig(
    const core::state::sequencer::SequencerState& sequencer
) {
    input_utils::StepPropertyEncoderConfig config;
    config.discreteTicksPerStep = input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
    config.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;
    config.discreteSteps = static_cast<uint8_t>((currentOffsetMax(sequencer) * 2) + 1);
    return config;
}

FLASHMEM QuickItem validQuickItemForContext(
    const core::state::sequencer::SequencerState& sequencer
) {
    const auto item = sequencer.patternQuickControls.focusedItem.get();
    if (core::state::sequencer::isChildContentView(sequencer) &&
        item != QuickItem::LENGTH &&
        item != QuickItem::OFFSET) {
        return QuickItem::LENGTH;
    }
    return item;
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
    , navigation_focus_(state.navigationFocus)
    , track_ui_(state.trackNavigation)
    , sequencer_(state.sequencer)
    , track_bank_(state.trackBank)
    , encoders_(encoders) {}

FLASHMEM bool SequencerEncoderSyncCoordinator::bind() {
    watcher_.bind<&SequencerEncoderSyncCoordinator::syncPositions>(
        *this, 0, "Sequencer.encoderSync"
    );
    return watcher_.watchAll(
        active_view_,
        navigation_focus_,
        sequencer_.page,
        sequencer_.pattern.length,
        sequencer_.pattern.graphRevision,
        sequencer_.focusedStep,
        sequencer_.activeStepProperty,
        sequencer_.stepStatePropertyActive,
        sequencer_.contentView.kind,
        sequencer_.contentView.length,
        sequencer_.contentView.revision,
        sequencer_.pattern.patternScaleRevision,
        track_bank_.projectScaleRevisionSignal(),
        sequencer_.structureUi.pageSelection.active,
        sequencer_.structureUi.stepSelection.active,
        track_ui_.selection.active,
        sequencer_.stepEdit.visible,
        sequencer_.stepPropertyInlineSelector.selecting,
        sequencer_.stepPropertyInlineSelector.macroLocalVariationEditActive,
        sequencer_.patternQuickControls.selecting,
        sequencer_.patternQuickControls.physicalHoldActive,
        sequencer_.patternQuickControls.focusedItem,
        sequencer_.patternQuickControls.offsetSteps,
        sequencer_.pattern.stepsPerBeat,
        sequencer_.pattern.swingOffsetPercent,
        sequencer_.pattern.patternNudgePercent,
        sequencer_.pattern.patternTimingRevision
    );
}

FLASHMEM void SequencerEncoderSyncCoordinator::reset() {
    macro_steps_configured_ = 0;
    macro_ticks_per_step_configured_ = 0;
    macro_turns_configured_ = 0.0f;
    macro_position_valid_.fill(false);
    opt_steps_configured_ = 0;
    opt_ticks_per_step_configured_ = 0;
    opt_turns_configured_ = 0.0f;
    opt_position_valid_ = false;
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

FLASHMEM void SequencerEncoderSyncCoordinator::ensureOptEncoderConfig(
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
    opt_position_valid_ = false;
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

FLASHMEM void SequencerEncoderSyncCoordinator::syncMacroStateValues(uint8_t page) {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        uint8_t abs = 0;
        const float normalized =
            core::state::sequencer::resolveActiveContentStepInPage(
                sequencer_,
                page,
                i,
                abs
            ) && core::state::sequencer::activeContentStepEnabled(sequencer_, abs)
            ? 1.0f
            : 0.0f;
        if (!macro_position_valid_[i] ||
            hasMeaningfulEncoderDelta(macro_position_cache_[i], normalized)) {
            encoders_.setPosition(Config::MACRO_ENCODERS[i], normalized);
            macro_position_cache_[i] = normalized;
            macro_position_valid_[i] = true;
        }
    }
}

FLASHMEM void SequencerEncoderSyncCoordinator::invalidateOptEncoderCache() {
    opt_steps_configured_ = 0;
    opt_ticks_per_step_configured_ = 0;
    opt_turns_configured_ = 0.0f;
    opt_position_valid_ = false;
}

FLASHMEM void SequencerEncoderSyncCoordinator::syncOptPosition(float normalized) {
    normalized = input_utils::clampNormalized(normalized);
    if (!opt_position_valid_ ||
        hasMeaningfulEncoderDelta(opt_position_cache_, normalized)) {
        encoders_.setPosition(Config::EncoderID::OPT, normalized);
        opt_position_cache_ = normalized;
        opt_position_valid_ = true;
    }
}

FLASHMEM void SequencerEncoderSyncCoordinator::syncFocusedStepOptValue(
    core::state::sequencer::StepProperty property
) {
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
    if (len == 0) return;

    const uint8_t step = std::min<uint8_t>(
        sequencer_.focusedStep.get(),
        static_cast<uint8_t>(len - 1U)
    );

    float normalized = core::state::sequencer::activeContentStepPropertyToNormalized(
        sequencer_,
        step,
        property,
        sequencer_.pattern.pitchEditMode,
        core::state::sequencer::resolveEffectiveScaleSettings(
            track_bank_.projectScaleSettings(),
            sequencer_.pattern.scalePolicy,
            sequencer_.pattern.scaleOverride
        )
    );
    normalized = input_utils::clampNormalized(normalized);

    syncOptPosition(normalized);
}

FLASHMEM void SequencerEncoderSyncCoordinator::syncPatternQuickControlOptValue() {
    const auto item = validQuickItemForContext(sequencer_);
    if (core::state::sequencer::isChildContentView(sequencer_)) {
        if (item == QuickItem::OFFSET) {
            ensureOptEncoderConfig(offsetEncoderConfig(sequencer_));
            syncOptPosition(offsetToNormalized(
                sequencer_,
                sequencer_.patternQuickControls.offsetSteps.get()
            ));
            return;
        }

        input_utils::StepPropertyEncoderConfig config;
        config.discreteTicksPerStep = input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
        config.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;
        config.discreteSteps = activeChildLengthStepCount(sequencer_);
        ensureOptEncoderConfig(config);
        syncOptPosition(childLengthToNormalized(
            sequencer_,
            core::state::sequencer::activeContentLength(sequencer_)
        ));
        return;
    }

    if (item == QuickItem::OFFSET) {
        ensureOptEncoderConfig(offsetEncoderConfig(sequencer_));
        syncOptPosition(offsetToNormalized(
            sequencer_,
            sequencer_.patternQuickControls.offsetSteps.get()
        ));
        return;
    }

    ensureOptEncoderConfig(input_utils::encoderConfigForQuickControl(item));
    syncOptPosition(input_utils::quickControlToNormalized(sequencer_, item));
}

FLASHMEM void SequencerEncoderSyncCoordinator::syncPositions() {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) return;

    if (sequencer_.ccLaneUi.visible()) {
        invalidateOptEncoderCache();
        macro_position_valid_.fill(false);
        return;
    }

    if (overlays_.hasVisible()) {
        invalidateOptEncoderCache();
        return;
    }

    const uint8_t page = static_cast<uint8_t>(
        std::min<uint16_t>(
            sequencer_.page.get(),
            static_cast<uint16_t>(
                core::state::sequencer::SequencerState::PAGE_COUNT - 1U
            )
        )
    );
    const auto property = sequencer_.activeStepProperty.get();

    if (sequencer_.stepPropertyInlineSelector.selecting.get()) {
        invalidateOptEncoderCache();
        if (sequencer_.stepStatePropertyActive.get()) {
            input_utils::StepPropertyEncoderConfig stateConfig;
            stateConfig.discreteSteps = 2;
            stateConfig.discreteTicksPerStep =
                input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
            stateConfig.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;
            ensureMacroEncoderConfig(stateConfig);
            syncMacroStateValues(page);
            return;
        }
        if (property == core::state::sequencer::StepProperty::PROBABILITY) {
            return;
        }
        const auto config = input_utils::encoderConfigForVariationRange(property);
        ensureMacroEncoderConfig(config);
        syncMacroLocalVariationValues(page, property);
        return;
    }

    if (sequencer_.patternQuickControls.selecting.get()) {
        invalidateOptEncoderCache();
        return;
    }

    const auto policy = interaction_policy::build(
        sequencer_,
        track_ui_,
        navigation_focus_.get()
    );
    if (sequencer_.stepStatePropertyActive.get()) {
        input_utils::StepPropertyEncoderConfig stateConfig;
        stateConfig.discreteSteps = 2;
        stateConfig.discreteTicksPerStep =
            input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
        stateConfig.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;
        ensureMacroEncoderConfig(stateConfig);
        syncMacroStateValues(page);
        if (policy.optTurn ==
            core::state::sequencer::SequencerInteractionAction::EDIT_STEP_PROPERTY) {
            ensureOptEncoderConfig(stateConfig);
            const uint8_t length =
                core::state::sequencer::activeContentLength(sequencer_);
            const uint8_t step = length == 0
                ? 0
                : std::min<uint8_t>(
                      sequencer_.focusedStep.get(),
                      static_cast<uint8_t>(length - 1U)
                  );
            syncOptPosition(
                length > 0 && core::state::sequencer::activeContentStepEnabled(
                    sequencer_,
                    step
                ) ? 1.0f : 0.0f
            );
        } else {
            invalidateOptEncoderCache();
        }
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

    switch (policy.optTurn) {
        case core::state::sequencer::SequencerInteractionAction::EDIT_PATTERN_DIMENSION:
            syncPatternQuickControlOptValue();
            return;

        case core::state::sequencer::SequencerInteractionAction::EDIT_STEP_PROPERTY:
            ensureOptEncoderConfig(config);
            syncFocusedStepOptValue(property);
            return;

        default:
            invalidateOptEncoderCache();
            return;
    }
}

}  // namespace core::context::standalone
