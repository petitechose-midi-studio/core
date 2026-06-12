#include "state/sequencer/SequencerContentViewInternal.hpp"

#include <algorithm>

#include <oc/note/sequencer/StepSequencerScale.hpp>

namespace core::state::sequencer::content_view_internal {
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

FLASHMEM ResolvedStep contentBaseForKind(ResolvedStep owner, SequencerContentViewKind kind);

FLASHMEM uint8_t clampMidi7Offset(uint8_t base, int16_t offset) {
    const int value = static_cast<int>(base) + static_cast<int>(offset);
    return static_cast<uint8_t>(std::clamp(value, 0, 127));
}

FLASHMEM uint8_t applyNoteOffset(
    uint8_t base,
    int8_t offset,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (offset == 0) return base;
    scaleSettings.clamp();
    if (scaleSettings.isConstrained()) {
        return oc::note::sequencer::moveByScaleDegrees(base, offset, scaleSettings);
    }
    return clampMidi7Offset(base, offset);
}

FLASHMEM uint16_t clampGateOffset(uint16_t base, int16_t offset) {
    const int value = static_cast<int>(base) + static_cast<int>(offset);
    return static_cast<uint16_t>(
        std::clamp(value, 0, static_cast<int>(SequencerState::MAX_GATE_PERCENT))
    );
}

FLASHMEM int8_t clampNudgeOffset(int8_t base, int8_t offset) {
    const int value = static_cast<int>(base) + static_cast<int>(offset);
    return static_cast<int8_t>(std::clamp(value, -50, 50));
}

FLASHMEM uint8_t clampProbabilityOffset(uint8_t base, int16_t offset) {
    const int value = static_cast<int>(base) + static_cast<int>(offset);
    return static_cast<uint8_t>(std::clamp(value, 0, 100));
}

FLASHMEM bool nodeEnabled(const Node& node) {
    if (!node.has(oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE)) return true;
    return node.has(oc::note::sequencer::STEP_NODE_ENABLED_VALUE);
}

FLASHMEM ResolvedStep applyNode(
    ResolvedStep parent,
    const Node& node,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (!parent.valid) return parent;

    if (node.has(oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE)) {
        parent.enabled = node.has(oc::note::sequencer::STEP_NODE_ENABLED_VALUE);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET)) {
        parent.note = applyNoteOffset(parent.note, node.noteOffset, scaleSettings);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_VELOCITY_OFFSET)) {
        parent.velocity = clampMidi7Offset(parent.velocity, node.velocityOffset);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_GATE_OFFSET)) {
        parent.gate = clampGateOffset(parent.gate, node.gateOffset);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_NUDGE_OFFSET)) {
        parent.nudge = clampNudgeOffset(parent.nudge, node.nudgeOffset);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_PROBABILITY_OFFSET)) {
        parent.probability = clampProbabilityOffset(parent.probability, node.probabilityOffset);
    }
    return parent;
}

FLASHMEM ResolvedStep contentBaseForKind(ResolvedStep owner, SequencerContentViewKind kind) {
    if (owner.valid && kind == SequencerContentViewKind::MICRO_SEQUENCE) {
        owner.gate = SequencerState::DEFAULT_GATE_PERCENT;
    }
    return owner;
}

FLASHMEM ResolvedStep rootBase(const SequencerState& sequencer, uint8_t rootStep) {
    if (rootStep >= SequencerState::MAX_STEPS) return {};
    return {
        .valid = true,
        .enabled = sequencer.pattern.enabledMask.get().test(rootStep),
        .note = sequencer.pattern.note[rootStep],
        .velocity = sequencer.pattern.velocity[rootStep],
        .gate = sequencer.pattern.gate[rootStep],
        .nudge = sequencer.pattern.nudge[rootStep],
        .probability = SequencerState::clampProbability(sequencer.pattern.probability[rootStep]),
    };
}

FLASHMEM const Node* graphNode(const SequencerState& sequencer, SequencerGraphNodeId nodeId) {
    const auto* graph = graphView(sequencer.pattern);
    return graph ? graph->stepNode(nodeId) : nullptr;
}

FLASHMEM bool nodeHasMicroSequence(
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node
) {
    return node.has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE) &&
           graph.sequence(node.childSequenceId) != nullptr;
}

FLASHMEM bool nodeHasCycleStates(
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node
) {
    return node.has(oc::note::sequencer::STEP_NODE_CYCLE_SET) &&
           graph.cycleSet(node.cycleSetId) != nullptr;
}

FLASHMEM bool ownsChildContent(const Node& node) {
    return node.has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE) ||
           node.has(oc::note::sequencer::STEP_NODE_CYCLE_SET);
}

FLASHMEM uint8_t normalizeSequenceIndex(uint8_t playIndex, int8_t offset, uint8_t length) {
    if (length == 0) return 0;
    int value = static_cast<int>(playIndex) - static_cast<int>(offset);
    const int len = static_cast<int>(length);
    value %= len;
    if (value < 0) value += len;
    return static_cast<uint8_t>(value);
}

FLASHMEM uint32_t boundaryTick(uint8_t playIndex, uint32_t spanTicks, uint8_t length) {
    if (length == 0) return 0;
    return (static_cast<uint32_t>(playIndex) * spanTicks) / static_cast<uint32_t>(length);
}

FLASHMEM uint32_t effectiveGateSpan(uint32_t spanTicks, uint16_t gatePercent) {
    uint32_t gated = (spanTicks * static_cast<uint32_t>(gatePercent)) / 100U;
    return std::max<uint32_t>(gated, 1U);
}

FLASHMEM uint16_t selectCycleStateNode(
    const oc::note::sequencer::StepSequencerGraph& graph,
    uint16_t cycleSetId,
    uint32_t cycleCursor
) {
    const auto* cycleSet = graph.cycleSet(cycleSetId);
    if (cycleSet == nullptr || cycleSet->length == 0) return kInvalidId;

    const uint8_t stateIndex = normalizeSequenceIndex(
        static_cast<uint8_t>(cycleCursor % cycleSet->length),
        cycleSet->offset,
        cycleSet->length
    );
    return static_cast<uint16_t>(cycleSet->firstStateNode + stateIndex);
}

FLASHMEM bool resolveRepresentativeChildContentStep(
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node,
    ResolvedStep& current,
    uint8_t depth,
    uint32_t localCycleIndex,
    uint8_t microPlayIndex,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (!current.valid || depth >= GraphLimits::MAX_DEPTH) return false;

    bool touchedChild = false;
    uint16_t childSequenceId = node.has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE)
        ? node.childSequenceId
        : kInvalidId;
    uint16_t cycleSetId = node.has(oc::note::sequencer::STEP_NODE_CYCLE_SET)
        ? node.cycleSetId
        : kInvalidId;
    uint32_t cycleCursor = localCycleIndex;
    uint32_t childLocalCycleIndex = localCycleIndex;

    uint8_t cycleDepth = 0;
    while (cycleSetId != kInvalidId) {
        if (static_cast<uint16_t>(depth) + cycleDepth >= GraphLimits::MAX_DEPTH) {
            return touchedChild;
        }

        const auto* cycleSet = graph.cycleSet(cycleSetId);
        if (cycleSet == nullptr || cycleSet->length == 0) break;

        const uint16_t stateNodeId = selectCycleStateNode(graph, cycleSetId, cycleCursor);
        const auto* stateNode = graph.stepNode(stateNodeId);
        if (stateNode == nullptr) break;

        const uint32_t ownerActivationIndex = cycleCursor / cycleSet->length;
        touchedChild = true;
        if (ownsChildContent(*stateNode)) {
            childSequenceId = kInvalidId;
            childLocalCycleIndex = ownerActivationIndex;
        }
        current = applyNode(current, *stateNode, scaleSettings);
        if (stateNode->has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE)) {
            childSequenceId = stateNode->childSequenceId;
        }
        cycleSetId = stateNode->has(oc::note::sequencer::STEP_NODE_CYCLE_SET)
            ? stateNode->cycleSetId
            : kInvalidId;
        cycleCursor = ownerActivationIndex;
        ++cycleDepth;
    }

    const auto* sequence = graph.sequence(childSequenceId);
    if (sequence == nullptr || sequence->length == 0) {
        return touchedChild;
    }
    if (static_cast<uint8_t>(depth + 1U) >= GraphLimits::MAX_DEPTH) {
        return touchedChild;
    }

    const uint8_t sourceIndex = normalizeSequenceIndex(
        static_cast<uint8_t>(microPlayIndex % sequence->length),
        sequence->offset,
        sequence->length
    );
    const auto* childNode = graph.stepNode(
        static_cast<uint16_t>(sequence->firstStepNode + sourceIndex)
    );
    if (childNode == nullptr) return touchedChild;

    touchedChild = true;
    current = applyNode(contentBaseForKind(current, SequencerContentViewKind::MICRO_SEQUENCE),
                        *childNode,
                        scaleSettings);
    resolveRepresentativeChildContentStep(
        graph,
        *childNode,
        current,
        static_cast<uint8_t>(depth + 1U),
        childLocalCycleIndex,
        0,
        scaleSettings
    );
    return touchedChild;
}

FLASHMEM bool ownsSequence(
    const oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphSequenceId sequenceId
) {
    const auto* owner = graph.stepNode(ownerNodeId);
    return owner != nullptr &&
           owner->has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE) &&
           owner->childSequenceId == sequenceId &&
           graph.sequence(sequenceId) != nullptr;
}

FLASHMEM bool ownsCycleSet(
    const oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphCycleSetId cycleSetId
) {
    const auto* owner = graph.stepNode(ownerNodeId);
    return owner != nullptr &&
           owner->has(oc::note::sequencer::STEP_NODE_CYCLE_SET) &&
           owner->cycleSetId == cycleSetId &&
           graph.cycleSet(cycleSetId) != nullptr;
}

FLASHMEM uint8_t ownerRootStepForNewFrame(
    const SequencerState& sequencer,
    SequencerGraphNodeId ownerNodeId
) {
    if (ownerNodeId < SequencerState::MAX_STEPS) {
        return static_cast<uint8_t>(ownerNodeId);
    }
    const auto* frame = sequencer.contentView.currentFrame();
    return frame ? frame->ownerRootStep : 0;
}

FLASHMEM void syncPublicViewFields(SequencerContentViewState& view) {
    if (view.stackDepth == 0) {
        view.kind.set(SequencerContentViewKind::ROOT);
        view.parentStep.set(0);
        view.ownerNodeId.set(kInvalidId);
        view.sequenceId.set(kInvalidId);
        view.cycleSetId.set(kInvalidId);
        view.length.set(0);
        view.depth.set(0);
        return;
    }

    const auto* frame = view.currentFrame();
    if (frame == nullptr) {
        view.reset();
        return;
    }

    view.kind.set(frame->kind);
    view.parentStep.set(frame->ownerRootStep);
    view.ownerNodeId.set(frame->ownerNodeId);
    view.sequenceId.set(frame->sequenceId);
    view.cycleSetId.set(frame->cycleSetId);
    view.length.set(frame->length);
    view.depth.set(view.stackDepth);
}

FLASHMEM bool pushFrame(
    SequencerState& sequencer,
    SequencerContentViewKind kind,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphSequenceId sequenceId,
    SequencerGraphCycleSetId cycleSetId,
    uint8_t length
) {
    auto& view = sequencer.contentView;
    if (view.stackDepth >= GraphLimits::MAX_DEPTH - 1U ||
        view.stackDepth >= SequencerContentViewState::MAX_CHILD_DEPTH) {
        return false;
    }

    const uint8_t index = view.stackDepth;
    const uint8_t ownerRootStep = ownerRootStepForNewFrame(sequencer, ownerNodeId);
    view.frames[index] = SequencerContentViewFrame{
        .kind = kind,
        .ownerRootStep = ownerRootStep,
        .ownerLocalStep = sequencer.focusedStep.get(),
        .pageSnapshot = sequencer.page.get(),
        .focusSnapshot = sequencer.focusedStep.get(),
        .ownerNodeId = ownerNodeId,
        .sequenceId = sequenceId,
        .cycleSetId = cycleSetId,
        .length = length,
    };
    if (index == 0) {
        view.rootPageSnapshot = sequencer.page.get();
        view.rootFocusSnapshot = sequencer.focusedStep.get();
    }
    ++view.stackDepth;
    syncPublicViewFields(view);
    sequencer.page.set(0);
    sequencer.focusedStep.set(0);
    sequencer.structureUi.previewAddPageSlot.set(false);
    sequencer.structureUi.pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    view.bump();
    return true;
}

FLASHMEM bool validateFrame(
    const SequencerState& sequencer,
    SequencerContentViewFrame& frame
) {
    const auto* graph = graphView(sequencer.pattern);
    if (graph == nullptr) return false;

    if (frame.kind == SequencerContentViewKind::MICRO_SEQUENCE) {
        if (!ownsSequence(*graph, frame.ownerNodeId, frame.sequenceId)) return false;
        const Sequence* sequence = graph->sequence(frame.sequenceId);
        if (sequence == nullptr) return false;
        frame.length = sequence->length;
        return true;
    }

    if (frame.kind == SequencerContentViewKind::CYCLE_STATES) {
        if (!ownsCycleSet(*graph, frame.ownerNodeId, frame.cycleSetId)) return false;
        const CycleSet* cycleSet = graph->cycleSet(frame.cycleSetId);
        if (cycleSet == nullptr) return false;
        frame.length = cycleSet->length;
        return true;
    }

    return false;
}

FLASHMEM ResolvedStep resolveOwnerStepAtDepth(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    uint8_t frameDepth
) {
    const auto& view = sequencer.contentView;
    if (view.stackDepth == 0 || view.stackDepth > view.frames.size()) return {};
    if (frameDepth == 0 || frameDepth > view.stackDepth) return {};

    const auto* graph = graphView(sequencer.pattern);
    if (graph == nullptr) return {};

    const auto& first = view.frames[0];
    ResolvedStep current = rootBase(sequencer, first.ownerRootStep);
    const Node* rootNode = graph->stepNode(rootStepNodeId(first.ownerRootStep));
    if (rootNode == nullptr) return {};
    current = applyNode(current, *rootNode, scaleSettings);

    for (uint8_t i = 1; i < frameDepth; ++i) {
        const auto& frame = view.frames[i];
        const auto& containingFrame = view.frames[i - 1U];
        const Node* ownerNode = graph->stepNode(frame.ownerNodeId);
        if (ownerNode == nullptr) return {};
        current = applyNode(contentBaseForKind(current, containingFrame.kind), *ownerNode, scaleSettings);
    }
    return current;
}

FLASHMEM ResolvedStep resolveOwnerStep(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    return resolveOwnerStepAtDepth(
        sequencer,
        scaleSettings,
        sequencer.contentView.stackDepth
    );
}

FLASHMEM SequencerGraphNodeId stepNodeIdForFrame(
    const SequencerState& sequencer,
    const SequencerContentViewFrame& frame,
    uint8_t step
) {
    const auto* graph = graphView(sequencer.pattern);
    if (graph == nullptr || step >= frame.length) return kInvalidId;

    if (frame.kind == SequencerContentViewKind::MICRO_SEQUENCE) {
        const auto* sequence = graph->sequence(frame.sequenceId);
        if (sequence == nullptr || step >= sequence->length) return kInvalidId;
        const uint8_t sourceIndex = normalizeSequenceIndex(step, sequence->offset, sequence->length);
        return static_cast<uint16_t>(sequence->firstStepNode + sourceIndex);
    }

    if (frame.kind == SequencerContentViewKind::CYCLE_STATES) {
        const auto* cycleSet = graph->cycleSet(frame.cycleSetId);
        if (cycleSet == nullptr || step >= cycleSet->length) return kInvalidId;
        const uint8_t sourceIndex = normalizeSequenceIndex(step, cycleSet->offset, cycleSet->length);
        return static_cast<uint16_t>(cycleSet->firstStateNode + sourceIndex);
    }

    return kInvalidId;
}

FLASHMEM int offsetForTargetValue(
    StepProperty property,
    int parentValue,
    int targetValue,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        const int parentDegree =
            scaleDegreeIndexForNote(static_cast<uint8_t>(std::clamp(parentValue, 0, 127)), scaleSettings);
        const int targetDegree =
            scaleDegreeIndexForNote(static_cast<uint8_t>(std::clamp(targetValue, 0, 127)), scaleSettings);
        return targetDegree - parentDegree;
    }
    return targetValue - parentValue;
}

FLASHMEM bool setNodeProperty(
    SequencerState& sequencer,
    SequencerGraphNodeId nodeId,
    StepProperty property,
    int baseValue,
    int targetValue,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (nodeId == kInvalidId) return false;

    bool changed = false;
    switch (property) {
        case StepProperty::NOTE: {
            const int offset = offsetForTargetValue(
                property,
                baseValue,
                targetValue,
                pitchEditMode,
                scaleSettings
            );
            changed = setNodeNoteOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int8_t>(std::clamp(offset, -128, 127))
            );
            break;
        }
        case StepProperty::VELOCITY:
            changed = setNodeVelocityOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int16_t>(targetValue - baseValue)
            );
            break;
        case StepProperty::GATE:
            changed = setNodeGateOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int16_t>(targetValue - baseValue)
            );
            break;
        case StepProperty::NUDGE:
            changed = setNodeNudgeOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int8_t>(std::clamp(targetValue - baseValue, -128, 127))
            );
            break;
        case StepProperty::PROBABILITY:
            changed = setNodeProbabilityOffset(
                sequencer.pattern,
                nodeId,
                static_cast<int16_t>(targetValue - baseValue)
            );
            break;
    }
    if (changed) sequencer.contentView.bump();
    return changed;
}

FLASHMEM int baseValueForProperty(
    const SequencerContentStepProjection& projection,
    StepProperty property
) {
    switch (property) {
        case StepProperty::NOTE:
            return projection.parentNote;
        case StepProperty::VELOCITY:
            return projection.parentVelocity;
        case StepProperty::GATE:
            return projection.parentGate;
        case StepProperty::NUDGE:
            return projection.parentNudge;
        case StepProperty::PROBABILITY:
            return projection.parentProbability;
    }
    return 0;
}

FLASHMEM int resolvedValueForProperty(
    const SequencerContentStepProjection& projection,
    StepProperty property
) {
    switch (property) {
        case StepProperty::NOTE:
            return projection.note;
        case StepProperty::VELOCITY:
            return projection.velocity;
        case StepProperty::GATE:
            return projection.gate;
        case StepProperty::NUDGE:
            return projection.nudge;
        case StepProperty::PROBABILITY:
            return projection.probability;
    }
    return 0;
}

}  // namespace core::state::sequencer::content_view_internal
