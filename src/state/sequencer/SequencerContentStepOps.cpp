#include "state/sequencer/SequencerContentViewOps.hpp"

#include <algorithm>

#include "state/sequencer/SequencerContentViewInternal.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::state::sequencer {
using namespace content_view_internal;

namespace {

FLASHMEM bool resetRootStepPropertyToDefault(
    SequencerState& sequencer,
    uint8_t step,
    StepProperty property
) {
    switch (property) {
        case StepProperty::NOTE:
            return sequencer.setStepNoteAt(step, SequencerState::DEFAULT_NOTE);
        case StepProperty::VELOCITY:
            return sequencer.setStepVelocityAt(step, SequencerState::DEFAULT_VELOCITY);
        case StepProperty::GATE:
            return sequencer.setStepGateAt(step, SequencerState::DEFAULT_GATE_PERCENT);
        case StepProperty::NUDGE:
            return sequencer.setStepNudgeAt(step, 0);
        case StepProperty::PROBABILITY:
            return sequencer.setStepProbabilityAt(step, SequencerState::DEFAULT_PROBABILITY);
    }
    return false;
}

FLASHMEM bool resetChildStepPropertyOffsetToDefault(
    SequencerState& sequencer,
    SequencerGraphNodeId nodeId,
    StepProperty property
) {
    auto& pattern = authoringPattern(sequencer);
    switch (property) {
        case StepProperty::NOTE:
            return setNodeNoteOffset(pattern, nodeId, 0);
        case StepProperty::VELOCITY:
            return setNodeVelocityOffset(pattern, nodeId, 0);
        case StepProperty::GATE:
            return setNodeGateOffset(pattern, nodeId, 0);
        case StepProperty::NUDGE:
            return setNodeNudgeOffset(pattern, nodeId, 0);
        case StepProperty::PROBABILITY:
            return setNodeProbabilityOffset(pattern, nodeId, 0);
    }
    return false;
}

}  // namespace

FLASHMEM bool rotateActiveContentSteps(SequencerState& sequencer, int offsetSteps) {
    if (!isChildContentView(sequencer)) return false;

    bool changed = false;
    auto& pattern = authoringPattern(sequencer);
    if (isMicroSequenceContentView(sequencer)) {
        changed = rotateMicroSequenceSteps(
            pattern,
            sequencer.contentView.sequenceId.get(),
            offsetSteps
        );
    } else if (isCycleStatesContentView(sequencer)) {
        changed = rotateCycleStateSetSteps(
            pattern,
            sequencer.contentView.cycleSetId.get(),
            offsetSteps
        );
    }

    if (!changed) return false;
    refreshContentView(sequencer);
    sequencer.contentView.bump();
    notifyStepContentDraftMutation(sequencer);
    return true;
}

FLASHMEM bool toggleActiveContentStep(SequencerState& sequencer, uint8_t step) {
    if (isRootContentView(sequencer)) {
        sequencer.pattern.toggle(step);
        sequencer.invalidateStepVariationTelemetry(step);
        return true;
    }

    const auto* node = graphNode(sequencer, activeContentStepNodeId(sequencer, step));
    const auto nodeId = activeContentStepNodeId(sequencer, step);
    if (node == nullptr || nodeId == kInvalidId) return false;

    const bool changed = nodeEnabled(*node)
        ? setNodeEnabledOverride(authoringPattern(sequencer), nodeId, false)
        : clearNodeEnabledOverride(authoringPattern(sequencer), nodeId);
    if (changed) {
        sequencer.contentView.bump();
        notifyStepContentDraftMutation(sequencer);
    }
    return changed;
}

FLASHMEM bool activeContentStepEnabled(const SequencerState& sequencer, uint8_t step) {
    if (step >= activeContentLength(sequencer)) return false;
    if (isRootContentView(sequencer)) return sequencer.pattern.isEnabled(step);

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    const auto* node = graphNode(sequencer, nodeId);
    return node != nullptr && nodeId != kInvalidId && nodeEnabled(*node);
}

FLASHMEM bool setActiveContentStepEnabled(SequencerState& sequencer, uint8_t step, bool enabled) {
    if (step >= activeContentLength(sequencer)) return false;

    if (isRootContentView(sequencer)) {
        const bool current = sequencer.pattern.isEnabled(step);
        if (current == enabled) return false;
        sequencer.pattern.setEnabled(step, enabled);
        sequencer.invalidateStepVariationTelemetry(step);
        return true;
    }

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    const auto* node = graphNode(sequencer, nodeId);
    if (node == nullptr || nodeId == kInvalidId) return false;

    const bool current = nodeEnabled(*node);
    if (current == enabled) return false;

    const bool changed = setNodeEnabledOverride(
        authoringPattern(sequencer),
        nodeId,
        enabled
    );
    if (changed) {
        sequencer.contentView.bump();
        notifyStepContentDraftMutation(sequencer);
    }
    return changed;
}

FLASHMEM bool setActiveContentStepFromNormalized(
    SequencerState& sequencer,
    uint8_t step,
    StepProperty property,
    float normalized,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (step >= activeContentLength(sequencer)) return false;

    const int target = targetValueFromNormalized(property, normalized, pitchEditMode, scaleSettings);
    if (isRootContentView(sequencer)) {
        switch (property) {
            case StepProperty::NOTE:
                return sequencer.setStepNoteAt(step, static_cast<uint8_t>(target));
            case StepProperty::VELOCITY:
                return sequencer.setStepVelocityAt(step, static_cast<uint8_t>(target));
            case StepProperty::GATE:
                return sequencer.setStepGateAt(step, static_cast<uint16_t>(target));
            case StepProperty::NUDGE:
                return sequencer.setStepNudgeAt(step, static_cast<int8_t>(target));
            case StepProperty::PROBABILITY:
                return sequencer.setStepProbabilityAt(step, static_cast<uint8_t>(target));
        }
    }

    const auto projection =
        resolveActiveContentStepProjection(sequencer, step, scaleSettings);
    if (!projection.valid) return false;

    return setNodeProperty(
        sequencer,
        projection.nodeId,
        property,
        baseValueForProperty(projection, property),
        target,
        pitchEditMode,
        scaleSettings
    );
}

FLASHMEM bool resetActiveContentStepPropertyToDefault(
    SequencerState& sequencer,
    uint8_t step,
    StepProperty property
) {
    if (step >= activeContentLength(sequencer)) return false;

    bool changed = false;
    if (isRootContentView(sequencer)) {
        changed = resetRootStepPropertyToDefault(sequencer, step, property);
    } else {
        const auto nodeId = activeContentStepNodeId(sequencer, step);
        if (nodeId == kInvalidId) return false;
        changed = resetChildStepPropertyOffsetToDefault(sequencer, nodeId, property);
        if (changed) sequencer.contentView.bump();
    }

    changed = setNodeLocalVariationRange(
        authoringPattern(sequencer),
        activeContentStepNodeId(sequencer, step),
        property,
        0
    ) || changed;
    if (changed) notifyStepContentDraftMutation(sequencer);
    return changed;
}

FLASHMEM float activeContentStepPropertyToNormalized(
    const SequencerState& sequencer,
    uint8_t step,
    StepProperty property,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    const auto projection =
        resolveActiveContentStepProjection(sequencer, step, scaleSettings);
    if (!projection.valid) return 0.0f;
    return valueToNormalized(
        property,
        resolvedValueForProperty(projection, property),
        pitchEditMode,
        scaleSettings
    );
}

FLASHMEM bool resizeActiveMicroSequenceContent(SequencerState& sequencer, uint8_t length) {
    if (!isMicroSequenceContentView(sequencer)) return false;
    const uint8_t clamped = std::clamp<uint8_t>(length, MICRO_LENGTH_MIN, MICRO_LENGTH_MAX);
    const bool changed = resizeMicroSequence(
        authoringPattern(sequencer),
        sequencer.contentView.sequenceId.get(),
        clamped
    );
    refreshContentView(sequencer);
    const uint8_t nextLength = activeContentLength(sequencer);
    if (nextLength == 0) return changed;
    if (sequencer.focusedStep.get() >= nextLength) {
        sequencer.focusedStep.set(static_cast<uint8_t>(nextLength - 1U));
    }
    sequencer.page.set(normalizeActiveContentPage(sequencer, sequencer.page.get()));
    if (changed) {
        sequencer.contentView.bump();
        notifyStepContentDraftMutation(sequencer);
    }
    return changed;
}

FLASHMEM bool resizeActiveCycleStatesContent(SequencerState& sequencer, uint8_t length) {
    if (!isCycleStatesContentView(sequencer)) return false;
    const uint8_t clamped =
        std::clamp<uint8_t>(length, CYCLE_STATE_LENGTH_MIN, CYCLE_STATE_LENGTH_MAX);
    const bool changed = resizeCycleStateSet(
        authoringPattern(sequencer),
        sequencer.contentView.cycleSetId.get(),
        clamped
    );
    refreshContentView(sequencer);
    const uint8_t nextLength = activeContentLength(sequencer);
    if (nextLength == 0) return changed;
    if (sequencer.focusedStep.get() >= nextLength) {
        sequencer.focusedStep.set(static_cast<uint8_t>(nextLength - 1U));
    }
    sequencer.page.set(normalizeActiveContentPage(sequencer, sequencer.page.get()));
    if (changed) {
        sequencer.contentView.bump();
        notifyStepContentDraftMutation(sequencer);
    }
    return changed;
}

}  // namespace core::state::sequencer
