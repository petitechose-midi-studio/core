#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"

#include <algorithm>
#include <cmath>

#include <oc/api/EncoderAPI.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
#include "state/sequencer/DrumPatternState.hpp"
#endif
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

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

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
using DrumDimension =
    core::state::sequencer::DrumTrackUxPrototypeDimension;
using DrumProperty = core::state::sequencer::DrumTrackUxPrototypeProperty;

FLASHMEM core::state::sequencer::StepProperty drumStepProperty(
    DrumProperty property
) {
    using StepProperty = core::state::sequencer::StepProperty;
    switch (property) {
        case DrumProperty::PROBABILITY: return StepProperty::PROBABILITY;
        case DrumProperty::GATE: return StepProperty::GATE;
        case DrumProperty::NUDGE: return StepProperty::NUDGE;
        case DrumProperty::STATE:
        case DrumProperty::VELOCITY:
        case DrumProperty::COUNT:
        default: return StepProperty::VELOCITY;
    }
}

FLASHMEM input_utils::StepPropertyEncoderConfig drumPropertyEncoderConfig(
    DrumProperty property
) {
    if (property != DrumProperty::STATE) {
        return input_utils::encoderConfigForProperty(
            drumStepProperty(property)
        );
    }
    input_utils::StepPropertyEncoderConfig config;
    config.discreteSteps = 2U;
    return config;
}

FLASHMEM float drumStepPropertyToNormalized(
    const core::state::sequencer::DrumTrackUxPrototypeState& prototype,
    uint8_t step
) {
    if (!prototype.drumTrack || step >= prototype.MAX_STEPS) return 0.0f;
    const auto& lane =
        prototype.drumTrack->pattern.lanes[prototype.selectedLane];
    switch (prototype.property) {
        case DrumProperty::STATE:
            return prototype.drumTrack->pattern.stepEnabled(
                prototype.selectedLane,
                step
            ) ? 1.0f : 0.0f;
        case DrumProperty::PROBABILITY:
            return input_utils::probabilityToNormalized(
                lane.probability[step]
            );
        case DrumProperty::GATE:
            return input_utils::gatePercentToNormalized(lane.gate[step]);
        case DrumProperty::NUDGE:
            return input_utils::nudgeToNormalized(lane.nudge[step]);
        case DrumProperty::VELOCITY:
        case DrumProperty::COUNT:
        default:
            return input_utils::indexToNormalized(lane.velocity[step], 128);
    }
}

FLASHMEM input_utils::StepPropertyEncoderConfig drumDimensionEncoderConfig(
    DrumDimension dimension
) {
    input_utils::StepPropertyEncoderConfig config;
    switch (dimension) {
        case DrumDimension::MODE:
            config.discreteSteps = 2U;
            return config;
        case DrumDimension::DIVISION:
            config.discreteSteps = static_cast<uint8_t>(
                input_utils::STEPS_PER_BEAT_CHOICES.size()
            );
            return config;
        case DrumDimension::LENGTH:
        case DrumDimension::COUNT:
        default:
            config.discreteSteps =
                core::state::sequencer::DRUM_MAX_STEPS;
            return config;
    }
}

FLASHMEM float drumDimensionToNormalized(
    const core::state::sequencer::DrumTrackUxPrototypeState& prototype
) {
    if (!prototype.drumTrack) return 0.0f;
    const auto& pattern = prototype.drumTrack->pattern;
    switch (prototype.dimension) {
        case DrumDimension::MODE:
            return pattern.lanes[prototype.selectedLane].timing.mode ==
                    core::state::sequencer::DrumLaneTimingMode::CUSTOM
                ? 1.0f
                : 0.0f;
        case DrumDimension::DIVISION:
            return input_utils::indexToNormalized(
                input_utils::findStepsPerBeatChoiceIndex(
                    pattern.effectiveStepsPerBeat(prototype.selectedLane)
                ),
                static_cast<int>(input_utils::STEPS_PER_BEAT_CHOICES.size())
            );
        case DrumDimension::LENGTH:
        case DrumDimension::COUNT:
        default:
            return input_utils::indexToNormalized(
                static_cast<int>(
                    pattern.effectiveLength(prototype.selectedLane) - 1U
                ),
                core::state::sequencer::DRUM_MAX_STEPS
            );
    }
}
#endif

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
    encoders.setMode(encoderId, oc::interface::EncoderMode::NORMALIZED);
    encoders.setBounds(encoderId, 0.0f, 1.0f);
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
    const bool bound = watcher_.watchAll(
        active_view_,
        navigation_focus_,
        overlays_.revisionSignal(),
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
        sequencer_.ccLaneUi.revision,
        sequencer_.structureUi.stepSelection.active,
        sequencer_.stepEdit.visible,
        sequencer_.stepPropertyInlineSelector.selecting,
        sequencer_.stepPropertyInlineSelector.macroLocalVariationEditActive,
        sequencer_.patternQuickControls.selecting,
        sequencer_.patternQuickControls.focusedItem,
        sequencer_.patternQuickControls.offsetSteps,
        sequencer_.pattern.stepsPerBeat,
        sequencer_.pattern.swingOffsetPercent,
        sequencer_.pattern.patternNudgePercent,
        sequencer_.pattern.patternTimingRevision,
        sequencer_.patternQuickControls.previewRevision
    );
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    return watcher_.watch(sequencer_.drumTrackUxPrototype.revision) && bound;
#else
    return bound;
#endif
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
    const auto& pattern = core::state::sequencer::authoringPattern(sequencer_);
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        float normalized = 0.0f;
        uint8_t abs = 0;

        if (core::state::sequencer::resolveActiveContentStepInPage(sequencer_, page, i, abs)) {
            normalized = core::state::sequencer::activeContentStepPropertyToNormalized(
                sequencer_,
                abs,
                property,
                pattern.pitchEditMode,
                core::state::sequencer::resolveEffectiveScaleSettings(
                    track_bank_.projectScaleSettings(),
                    pattern.scalePolicy,
                    pattern.scaleOverride
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
    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(sequencer_)
    );
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
    const auto& pattern = core::state::sequencer::authoringPattern(sequencer_);
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
        pattern.pitchEditMode,
        core::state::sequencer::resolveEffectiveScaleSettings(
            track_bank_.projectScaleSettings(),
            pattern.scalePolicy,
            pattern.scaleOverride
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

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
FLASHMEM void SequencerEncoderSyncCoordinator::syncDrumTrackUxPrototypeValues() {
    const auto& prototype = sequencer_.drumTrackUxPrototype;

    ensureMacroEncoderConfig(drumPropertyEncoderConfig(prototype.property));

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const uint8_t step = prototype.visibleStep(i);
        const float normalized = drumStepPropertyToNormalized(prototype, step);
        if (!macro_position_valid_[i] ||
            hasMeaningfulEncoderDelta(macro_position_cache_[i], normalized)) {
            encoders_.setPosition(Config::MACRO_ENCODERS[i], normalized);
            macro_position_cache_[i] = normalized;
            macro_position_valid_[i] = true;
        }
    }

    if (prototype.selector ==
        core::state::sequencer::DrumTrackUxPrototypeSelector::PROPERTY) {
        invalidateOptEncoderCache();
        return;
    }
    if (navigation_focus_.get() ==
            core::state::StructureNavigationFocus::STEP &&
        !prototype.selectorVisible()) {
        ensureOptEncoderConfig(drumPropertyEncoderConfig(prototype.property));
        syncOptPosition(
            drumStepPropertyToNormalized(prototype, prototype.focusedStep)
        );
        return;
    }
    ensureOptEncoderConfig(drumDimensionEncoderConfig(prototype.dimension));
    syncOptPosition(drumDimensionToNormalized(prototype));
}
#endif

FLASHMEM void SequencerEncoderSyncCoordinator::syncPositions() {
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) return;

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    if (sequencer_.drumTrackUxPrototype.active()) {
        if (sequencer_.drumTrackUxPrototype.gridVisible()) {
            syncDrumTrackUxPrototypeValues();
        } else {
            invalidateOptEncoderCache();
            macro_position_valid_.fill(false);
        }
        return;
    }
#endif

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
        } else if (policy.optTurn ==
                   core::state::sequencer::SequencerInteractionAction::
                       EDIT_PATTERN_DIMENSION) {
            // State remains the selected Step property while the user moves
            // between contexts. Pattern focus nevertheless owns OPT and must
            // replace the two-state encoder contract with its own dimension.
            syncPatternQuickControlOptValue();
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
