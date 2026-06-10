#include "state/sequencer/SequencerContentViewOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"

namespace core::state::sequencer {
namespace {

using GraphLimits = oc::note::sequencer::StepSequencerGraphLimits;
using Node = oc::note::sequencer::StepSequencerStepNode;
using Sequence = oc::note::sequencer::StepSequencerSequence;

constexpr uint8_t MICRO_LENGTH_MIN = 2;
constexpr uint8_t MICRO_LENGTH_MAX = GraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;

FLASHMEM int normalizedToInclusiveInt(float normalized, int maxInclusive) {
    if (maxInclusive <= 0) return 0;
    const float value = std::clamp(normalized, 0.0f, 1.0f);
    return std::clamp(
        static_cast<int>(value * static_cast<float>(maxInclusive) + 0.5f),
        0,
        maxInclusive
    );
}

FLASHMEM int normalizedToIndex(float normalized, int itemCount) {
    if (itemCount <= 1) return 0;
    return normalizedToInclusiveInt(normalized, itemCount - 1);
}

FLASHMEM float indexToNormalized(int index, int itemCount) {
    if (itemCount <= 1) return 0.0f;
    return static_cast<float>(std::clamp(index, 0, itemCount - 1)) /
           static_cast<float>(itemCount - 1);
}

FLASHMEM const Sequence* activeMicroSequence(const SequencerState& sequencer) {
    if (!sequencer.contentView.isMicroSequence()) return nullptr;
    const auto* graph = graphView(sequencer.pattern);
    return graph ? graph->sequence(sequencer.contentView.sequenceId.get()) : nullptr;
}

FLASHMEM const Node* activeMicroNode(const SequencerState& sequencer, uint8_t step) {
    const auto* graph = graphView(sequencer.pattern);
    const auto* sequence = activeMicroSequence(sequencer);
    if (graph == nullptr || sequence == nullptr || step >= sequence->length) return nullptr;
    return graph->stepNode(static_cast<uint16_t>(sequence->firstStepNode + step));
}

FLASHMEM SequencerGraphNodeId activeMicroNodeId(const SequencerState& sequencer, uint8_t step) {
    const auto* sequence = activeMicroSequence(sequencer);
    if (sequence == nullptr || step >= sequence->length) return GraphLimits::INVALID_ID;
    return static_cast<uint16_t>(sequence->firstStepNode + step);
}

FLASHMEM uint8_t parentStep(const SequencerState& sequencer) {
    return std::min<uint8_t>(
        sequencer.contentView.parentStep.get(),
        static_cast<uint8_t>(SequencerState::MAX_STEPS - 1U)
    );
}

FLASHMEM int parentNote(const SequencerState& sequencer) {
    return sequencer.pattern.note[parentStep(sequencer)];
}

FLASHMEM int parentVelocity(const SequencerState& sequencer) {
    return sequencer.pattern.velocity[parentStep(sequencer)];
}

FLASHMEM int parentGate(const SequencerState& sequencer) {
    return sequencer.pattern.gate[parentStep(sequencer)];
}

FLASHMEM int parentNudge(const SequencerState& sequencer) {
    return sequencer.pattern.nudge[parentStep(sequencer)];
}

FLASHMEM int parentProbability(const SequencerState& sequencer) {
    return sequencer.pattern.probability[parentStep(sequencer)];
}

FLASHMEM bool nodeEnabled(const Node& node) {
    if (!node.has(oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE)) return true;
    return node.has(oc::note::sequencer::STEP_NODE_ENABLED_VALUE);
}

FLASHMEM int resolvedNodeValue(
    int parentValue,
    int offset,
    int minValue,
    int maxValue
) {
    return std::clamp(parentValue + offset, minValue, maxValue);
}

FLASHMEM int resolvedPropertyValue(
    const SequencerState& sequencer,
    const Node& node,
    StepProperty property
) {
    switch (property) {
        case StepProperty::NOTE:
            return resolvedNodeValue(parentNote(sequencer), node.noteOffset, 0, 127);
        case StepProperty::VELOCITY:
            return resolvedNodeValue(parentVelocity(sequencer), node.velocityOffset, 0, 127);
        case StepProperty::GATE:
            return resolvedNodeValue(
                parentGate(sequencer),
                node.gateOffset,
                0,
                SequencerState::MAX_GATE_PERCENT
            );
        case StepProperty::NUDGE:
            return resolvedNodeValue(parentNudge(sequencer), node.nudgeOffset, -50, 50);
        case StepProperty::PROBABILITY:
            return resolvedNodeValue(parentProbability(sequencer), node.probabilityOffset, 0, 100);
    }
    return 0;
}

FLASHMEM bool usesScaleDegreePitchEdit(
    StepProperty property,
    SequencerPitchEditMode mode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    return property == StepProperty::NOTE &&
           (scaleSettings.isConstrained() || mode == SequencerPitchEditMode::SCALE_DEGREES) &&
           scaleSettings.type != oc::note::sequencer::StepSequencerScaleType::Chromatic;
}

FLASHMEM int countScaleNotes(oc::note::sequencer::StepSequencerScaleSettings scaleSettings) {
    scaleSettings.clamp();
    int count = 0;
    for (int note = 0; note <= 127; ++note) {
        if (oc::note::sequencer::scaleContainsNote(scaleSettings, static_cast<uint8_t>(note))) {
            ++count;
        }
    }
    return std::max(count, 1);
}

FLASHMEM int scaleDegreeIndexForNote(
    uint8_t note,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    const uint8_t resolved =
        oc::note::sequencer::resolveScaleNote(note, scaleSettings).outputNote;
    int index = 0;
    for (int candidate = 0; candidate <= 127; ++candidate) {
        if (!oc::note::sequencer::scaleContainsNote(scaleSettings, static_cast<uint8_t>(candidate))) {
            continue;
        }
        if (candidate >= resolved) return index;
        ++index;
    }
    return std::max(0, index - 1);
}

FLASHMEM uint8_t scaleNoteForDegreeIndex(
    int index,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    const int clampedIndex = std::clamp(index, 0, countScaleNotes(scaleSettings) - 1);
    int current = 0;
    for (int note = 0; note <= 127; ++note) {
        if (!oc::note::sequencer::scaleContainsNote(scaleSettings, static_cast<uint8_t>(note))) {
            continue;
        }
        if (current == clampedIndex) return static_cast<uint8_t>(note);
        ++current;
    }
    return 0;
}

FLASHMEM uint8_t normalizedToMidi7(float normalized) {
    return static_cast<uint8_t>(normalizedToInclusiveInt(normalized, 127));
}

FLASHMEM uint16_t normalizedToGate(float normalized) {
    return static_cast<uint16_t>(
        normalizedToInclusiveInt(normalized, SequencerState::MAX_GATE_PERCENT)
    );
}

FLASHMEM int8_t normalizedToNudge(float normalized) {
    return static_cast<int8_t>(-50 + normalizedToInclusiveInt(normalized, 100));
}

FLASHMEM uint8_t normalizedToProbability(float normalized) {
    return static_cast<uint8_t>(normalizedToInclusiveInt(normalized, 100));
}

FLASHMEM int targetValueFromNormalized(
    StepProperty property,
    float normalized,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        return scaleNoteForDegreeIndex(
            normalizedToIndex(normalized, countScaleNotes(scaleSettings)),
            scaleSettings
        );
    }

    switch (property) {
        case StepProperty::NOTE:
            return normalizedToMidi7(normalized);
        case StepProperty::VELOCITY:
            return normalizedToMidi7(normalized);
        case StepProperty::GATE:
            return normalizedToGate(normalized);
        case StepProperty::NUDGE:
            return normalizedToNudge(normalized);
        case StepProperty::PROBABILITY:
            return normalizedToProbability(normalized);
    }
    return 0;
}

FLASHMEM float valueToNormalized(
    StepProperty property,
    int value,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        return indexToNormalized(
            scaleDegreeIndexForNote(static_cast<uint8_t>(std::clamp(value, 0, 127)), scaleSettings),
            countScaleNotes(scaleSettings)
        );
    }

    switch (property) {
        case StepProperty::NOTE:
        case StepProperty::VELOCITY:
            return indexToNormalized(std::clamp(value, 0, 127), 128);
        case StepProperty::GATE:
            return indexToNormalized(
                std::clamp(value, 0, static_cast<int>(SequencerState::MAX_GATE_PERCENT)),
                static_cast<int>(SequencerState::MAX_GATE_PERCENT) + 1
            );
        case StepProperty::NUDGE:
            return indexToNormalized(std::clamp(value, -50, 50) + 50, 101);
        case StepProperty::PROBABILITY:
            return indexToNormalized(std::clamp(value, 0, 100), 101);
    }
    return 0.0f;
}

FLASHMEM bool setMicroNodeProperty(
    SequencerState& sequencer,
    SequencerGraphNodeId nodeId,
    StepProperty property,
    int target
) {
    if (nodeId == GraphLimits::INVALID_ID) return false;

    bool changed = false;
    switch (property) {
        case StepProperty::NOTE:
            changed = setNodeNoteOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int8_t>(std::clamp(target - parentNote(sequencer), -128, 127))
            );
            break;
        case StepProperty::VELOCITY:
            changed = setNodeVelocityOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int16_t>(target - parentVelocity(sequencer))
            );
            break;
        case StepProperty::GATE:
            changed = setNodeGateOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int16_t>(target - parentGate(sequencer))
            );
            break;
        case StepProperty::NUDGE:
            changed = setNodeNudgeOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int8_t>(std::clamp(target - parentNudge(sequencer), -128, 127))
            );
            break;
        case StepProperty::PROBABILITY:
            changed = setNodeProbabilityOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int16_t>(target - parentProbability(sequencer))
            );
            break;
    }
    if (changed) sequencer.contentView.bump();
    return changed;
}

}  // namespace

FLASHMEM bool isRootContentView(const SequencerState& sequencer) {
    return !sequencer.contentView.isMicroSequence();
}

FLASHMEM bool isMicroSequenceContentView(const SequencerState& sequencer) {
    return sequencer.contentView.isMicroSequence();
}

FLASHMEM bool enterMicroSequenceContentView(
    SequencerState& sequencer,
    uint8_t parentStepIndex,
    SequencerGraphSequenceId sequenceId
) {
    const auto* graph = graphView(sequencer.pattern);
    const auto* sequence = graph ? graph->sequence(sequenceId) : nullptr;
    if (sequence == nullptr ||
        sequence->kind != oc::note::sequencer::StepSequencerSequenceKind::MicroSequence) {
        return false;
    }

    auto& view = sequencer.contentView;
    if (!view.isMicroSequence()) {
        view.rootPageSnapshot = sequencer.page.get();
        view.rootFocusSnapshot = sequencer.focusedStep.get();
    }
    view.kind.set(SequencerContentViewKind::MICRO_SEQUENCE);
    view.parentStep.set(parentStepIndex);
    view.sequenceId.set(sequenceId);
    view.length.set(sequence->length);
    sequencer.page.set(0);
    sequencer.focusedStep.set(0);
    sequencer.structureUi.previewAddPageSlot.set(false);
    sequencer.structureUi.pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    view.bump();
    return true;
}

FLASHMEM bool leaveContentView(SequencerState& sequencer) {
    if (!sequencer.contentView.isMicroSequence()) return false;
    const uint8_t rootPage = sequencer.contentView.rootPageSnapshot;
    const uint8_t rootFocus = sequencer.contentView.rootFocusSnapshot;
    sequencer.contentView.reset();
    sequencer.page.set(sequencer.clampPage(rootPage));
    sequencer.focusedStep.set(std::min<uint8_t>(rootFocus, SequencerState::MAX_STEPS - 1U));
    return true;
}

FLASHMEM void refreshContentView(SequencerState& sequencer) {
    const auto* sequence = activeMicroSequence(sequencer);
    if (sequence == nullptr) {
        if (sequencer.contentView.isMicroSequence()) {
            leaveContentView(sequencer);
        }
        return;
    }
    sequencer.contentView.length.set(sequence->length);
}

FLASHMEM uint8_t activeContentLength(const SequencerState& sequencer) {
    if (isMicroSequenceContentView(sequencer)) {
        return sequencer.contentView.length.get();
    }
    return sequencer.pattern.length.get();
}

FLASHMEM uint8_t activeContentPageCount(const SequencerState& sequencer) {
    const uint8_t len = activeContentLength(sequencer);
    if (len == 0) return 0;
    return static_cast<uint8_t>(
        std::min<uint16_t>(
            SequencerState::PAGE_COUNT,
            static_cast<uint16_t>((len + SequencerState::STEPS_PER_PAGE - 1U) /
                                  SequencerState::STEPS_PER_PAGE)
        )
    );
}

FLASHMEM uint8_t normalizeActiveContentPage(const SequencerState& sequencer, uint8_t page) {
    const uint8_t pages = activeContentPageCount(sequencer);
    if (pages == 0) return 0;
    return static_cast<uint8_t>(page % pages);
}

FLASHMEM uint8_t activeContentPageStartStep(const SequencerState& sequencer, uint8_t page) {
    return static_cast<uint8_t>(
        normalizeActiveContentPage(sequencer, page) * SequencerState::STEPS_PER_PAGE
    );
}

FLASHMEM uint8_t activeContentPageForStep(uint8_t step) {
    return static_cast<uint8_t>(step / SequencerState::STEPS_PER_PAGE);
}

FLASHMEM bool resolveActiveContentStepInPage(
    const SequencerState& sequencer,
    uint8_t page,
    uint8_t indexInPage,
    uint8_t& outStep
) {
    if (indexInPage >= SequencerState::STEPS_PER_PAGE) return false;
    const uint8_t pages = activeContentPageCount(sequencer);
    if (pages == 0) return false;

    const uint16_t step =
        static_cast<uint16_t>(normalizeActiveContentPage(sequencer, page)) *
            SequencerState::STEPS_PER_PAGE +
        indexInPage;
    if (step >= activeContentLength(sequencer)) return false;
    outStep = static_cast<uint8_t>(step);
    return true;
}

FLASHMEM bool activeContentStepInPattern(const SequencerState& sequencer, uint8_t step) {
    return step < activeContentLength(sequencer);
}

FLASHMEM bool toggleActiveContentStep(SequencerState& sequencer, uint8_t step) {
    if (isRootContentView(sequencer)) {
        sequencer.pattern.toggle(step);
        sequencer.invalidateStepVariationTelemetry(step);
        return true;
    }

    const auto* node = activeMicroNode(sequencer, step);
    const auto nodeId = activeMicroNodeId(sequencer, step);
    if (node == nullptr || nodeId == GraphLimits::INVALID_ID) return false;

    const bool changed = nodeEnabled(*node)
        ? setNodeEnabledOverride(sequencer.pattern, nodeId, false)
        : clearNodeEnabledOverride(sequencer.pattern, nodeId);
    if (changed) sequencer.contentView.bump();
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
    if (isRootContentView(sequencer)) {
        const int target = targetValueFromNormalized(property, normalized, pitchEditMode, scaleSettings);
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

    const auto nodeId = activeMicroNodeId(sequencer, step);
    const int target = targetValueFromNormalized(property, normalized, pitchEditMode, scaleSettings);
    return setMicroNodeProperty(sequencer, nodeId, property, target);
}

FLASHMEM float activeContentStepPropertyToNormalized(
    const SequencerState& sequencer,
    uint8_t step,
    StepProperty property,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (isRootContentView(sequencer)) {
        if (step >= SequencerState::MAX_STEPS) return 0.0f;
        int value = 0;
        switch (property) {
            case StepProperty::NOTE:
                value = sequencer.pattern.note[step];
                break;
            case StepProperty::VELOCITY:
                value = sequencer.pattern.velocity[step];
                break;
            case StepProperty::GATE:
                value = sequencer.pattern.gate[step];
                break;
            case StepProperty::NUDGE:
                value = sequencer.pattern.nudge[step];
                break;
            case StepProperty::PROBABILITY:
                value = sequencer.pattern.probability[step];
                break;
        }
        return valueToNormalized(property, value, pitchEditMode, scaleSettings);
    }

    const auto* node = activeMicroNode(sequencer, step);
    if (node == nullptr) return 0.0f;
    return valueToNormalized(
        property,
        resolvedPropertyValue(sequencer, *node, property),
        pitchEditMode,
        scaleSettings
    );
}

FLASHMEM bool resizeActiveMicroSequenceContent(SequencerState& sequencer, uint8_t length) {
    if (!isMicroSequenceContentView(sequencer)) return false;
    const uint8_t clamped = std::clamp<uint8_t>(length, MICRO_LENGTH_MIN, MICRO_LENGTH_MAX);
    const bool changed = resizeMicroSequence(
        sequencer.pattern,
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
    if (changed) sequencer.contentView.bump();
    return changed;
}

}  // namespace core::state::sequencer
