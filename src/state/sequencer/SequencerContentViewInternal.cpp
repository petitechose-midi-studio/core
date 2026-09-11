#include "state/sequencer/SequencerContentViewInternal.hpp"
#include "state/sequencer/SequencerValueMapping.hpp"
#include "state/shared/NormalizedValue.hpp"
#include "state/sequencer/SequencerPitchEditAuthority.hpp"

#include <algorithm>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/DrumPatternState.hpp"

namespace core::state::sequencer::content_view_internal {
namespace note = oc::note::sequencer;

namespace normalized = core::state::normalized;
namespace pitch_edit = core::state::sequencer::pitch_edit;
namespace value_mapping = core::state::sequencer::value_mapping;
FLASHMEM int targetValueFromNormalized(
    StepProperty property,
    float normalized,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (pitch_edit::usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        return pitch_edit::scaleNoteForDegreeIndex(
            normalized::normalizedToIndex(normalized, pitch_edit::countScaleNotes(scaleSettings)),
            scaleSettings
        );
    }

    switch (property) {
        case StepProperty::NOTE:
            return value_mapping::normalizedToMidi7(normalized);
        case StepProperty::VELOCITY:
            return value_mapping::normalizedToMidi7(normalized);
        case StepProperty::GATE:
            return value_mapping::normalizedToGatePercent(normalized);
        case StepProperty::NUDGE:
            return value_mapping::normalizedToNudge(normalized);
        case StepProperty::PROBABILITY:
            return value_mapping::normalizedToProbability(normalized);
    }
    return 0;
}

FLASHMEM float valueToNormalized(
    StepProperty property,
    int value,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (pitch_edit::usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        return normalized::indexToNormalized(
            pitch_edit::scaleDegreeIndexForNote(static_cast<uint8_t>(std::clamp(value, 0, 127)), scaleSettings),
            pitch_edit::countScaleNotes(scaleSettings)
        );
    }

    switch (property) {
        case StepProperty::NOTE:
        case StepProperty::VELOCITY:
            return normalized::indexToNormalized(std::clamp(value, 0, 127), 128);
        case StepProperty::GATE:
            return value_mapping::gatePercentToNormalized(value);
        case StepProperty::NUDGE:
            return value_mapping::nudgeToNormalized(value);
        case StepProperty::PROBABILITY:
            return value_mapping::probabilityToNormalized(value);
    }
    return 0.0f;
}

FLASHMEM ResolvedStep contentBaseForKind(
    ResolvedStep owner,
    SequencerContentViewKind kind,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

FLASHMEM uint8_t applyNoteOffset(
    uint8_t base,
    int8_t offset,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool useScaleDegrees
) {
    if (offset == 0) return base;
    if (useScaleDegrees) {
        scaleSettings.clamp();
        return oc::note::sequencer::moveByScaleDegrees(base, offset, scaleSettings);
    }
    return note::clampMidi7(static_cast<int>(base) + offset);
}

FLASHMEM oc::note::sequencer::StepSequencerInheritedChord activeChordForChildren(
    const ResolvedStep& step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    const oc::note::sequencer::StepSequencerStepValues values{
        .note = step.note,
        .velocity = step.velocity,
        .gate = step.gate,
        .nudge = step.nudge,
    };
    return oc::note::sequencer::resolveStepChord(
        values,
        scaleSettings,
        step.chordState,
        step.inheritedChord,
        step.gate == 0 ? 1U : step.gate
    ).activeForChildren;
}

FLASHMEM bool nodeEnabled(const Node& node) {
    if (!node.has(oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE)) return true;
    return node.has(oc::note::sequencer::STEP_NODE_ENABLED_VALUE);
}

FLASHMEM ResolvedStep applyNode(
    ResolvedStep parent,
    const Node& node,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool noteOffsetsUseScaleDegrees
) {
    if (!parent.valid) return parent;

    if (node.has(oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE)) {
        parent.enabled = node.has(oc::note::sequencer::STEP_NODE_ENABLED_VALUE);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET)) {
        parent.note = applyNoteOffset(
            parent.note,
            node.noteOffset,
            scaleSettings,
            noteOffsetsUseScaleDegrees
        );
    }
    if (node.has(oc::note::sequencer::STEP_NODE_VELOCITY_OFFSET)) {
        parent.velocity = note::clampMidi7(static_cast<int>(parent.velocity) + node.velocityOffset);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_GATE_OFFSET)) {
        parent.gate = note::clampGatePercent(
            static_cast<int>(parent.gate) + node.gateOffset, SequencerState::MAX_GATE_PERCENT
        );
    }
    if (node.has(oc::note::sequencer::STEP_NODE_NUDGE_OFFSET)) {
        parent.nudge = note::clampNudge(static_cast<int>(parent.nudge) + node.nudgeOffset);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_PROBABILITY_OFFSET)) {
        parent.probability = note::clampProbability(static_cast<int>(parent.probability) + node.probabilityOffset);
    }
    if (node.has(oc::note::sequencer::STEP_NODE_CHORD_MODE)) {
        parent.chordState.mode = node.chordMode;
    }
    if (node.has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL)) {
        parent.chordState.local = node.chordSpec;
    }
    parent.chordState.local.clamp();
    return parent;
}

FLASHMEM ResolvedStep contentBaseForKind(
    ResolvedStep owner,
    SequencerContentViewKind kind,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (!owner.valid) return owner;

    owner.inheritedChord = activeChordForChildren(owner, scaleSettings);
    owner.chordState = oc::note::sequencer::defaultChildChordState();
    if (owner.valid && kind == SequencerContentViewKind::MICRO_SEQUENCE) {
        owner.gate = SequencerState::DEFAULT_GATE_PERCENT;
    }
    return owner;
}

FLASHMEM ResolvedStep rootBase(const SequencerState& sequencer, uint8_t rootStep) {
    const auto& edit = sequencer.stepEdit;
    const auto& content = sequencer.contentView;
    const auto& drumUi = sequencer.drumSequencer;
    const bool contentOwner = content.drumOwnerActive &&
        content.drumOwnerRootSlot == rootStep;
    const bool editorOwner = edit.drumContext && edit.drumRootSlot == rootStep;
    const uint8_t lane = contentOwner ? content.drumOwnerLane : edit.drumLane;
    const uint8_t step = contentOwner ? content.drumOwnerStep : edit.drumStep;
    if ((contentOwner || editorOwner) &&
        drumUi.drumTrack() != nullptr &&
        drumUi.stepInRange(lane, step)) {
        const auto& drum = *drumUi.drumTrack();
        const auto& lanePattern = drum.pattern.lanes[lane];
        return {
            .valid = true,
            .enabled = drum.pattern.stepEnabled(lane, step),
            .note = drum.kit.lanes[lane].midiNote,
            .velocity = lanePattern.velocity[step],
            .gate = lanePattern.gate[step],
            .nudge = lanePattern.nudge[step],
            .probability = SequencerState::clampProbability(
                lanePattern.probability[step]
            ),
            .chordState = oc::note::sequencer::defaultRootChordState(),
            .inheritedChord = {},
        };
    }
    if (rootStep >= SequencerState::MAX_STEPS) return {};
    return {
        .valid = true,
        .enabled = authoringPattern(sequencer).enabledMask.test(rootStep),
        .note = authoringPattern(sequencer).note[rootStep],
        .velocity = authoringPattern(sequencer).velocity[rootStep],
        .gate = authoringPattern(sequencer).gate[rootStep],
        .nudge = authoringPattern(sequencer).nudge[rootStep],
        .probability = SequencerState::clampProbability(
            authoringPattern(sequencer).probability[rootStep]
        ),
        .chordState = oc::note::sequencer::defaultRootChordState(),
        .inheritedChord = {},
    };
}

FLASHMEM const Node* graphNode(const SequencerState& sequencer, SequencerGraphNodeId nodeId) {
    const auto* graph = graphView(authoringPattern(sequencer));
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

    const uint8_t stateIndex = note::normalizeSequenceIndex(
        static_cast<uint8_t>(cycleCursor % cycleSet->length),
        cycleSet->offset,
        cycleSet->length
    );
    return static_cast<uint16_t>(cycleSet->firstStateNode + stateIndex);
}

FLASHMEM void captureRepresentativeNode(
    const Node& node,
    SequencerGraphNodeId nodeId,
    SequencerChildContentSummary* outSummary
) {
    if (outSummary == nullptr) return;

    outSummary->nodeId = nodeId;
    outSummary->localVariation = note::combineVariationRanges(
        outSummary->localVariation, node.localVariation
    );
}

FLASHMEM bool resolveRepresentativeChildContentStep(
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node,
    ResolvedStep& current,
    uint8_t depth,
    uint32_t localCycleIndex,
    uint8_t microPlayIndex,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool noteOffsetsUseScaleDegrees,
    SequencerChildContentSummary* outSummary
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
        captureRepresentativeNode(*stateNode, stateNodeId, outSummary);
        if (ownsChildContent(*stateNode)) {
            childSequenceId = kInvalidId;
            childLocalCycleIndex = ownerActivationIndex;
        }
        current = applyNode(
            current,
            *stateNode,
            scaleSettings,
            noteOffsetsUseScaleDegrees
        );
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

    const uint8_t sourceIndex = note::normalizeSequenceIndex(
        static_cast<uint8_t>(microPlayIndex % sequence->length),
        sequence->offset,
        sequence->length
    );
    const auto* childNode = graph.stepNode(
        static_cast<uint16_t>(sequence->firstStepNode + sourceIndex)
    );
    if (childNode == nullptr) return touchedChild;
    const auto childNodeId = static_cast<uint16_t>(sequence->firstStepNode + sourceIndex);

    touchedChild = true;
    captureRepresentativeNode(*childNode, childNodeId, outSummary);
    current = applyNode(contentBaseForKind(
                            current,
                            SequencerContentViewKind::MICRO_SEQUENCE,
                            scaleSettings
                        ),
                        *childNode,
                        scaleSettings,
                        noteOffsetsUseScaleDegrees);
    resolveRepresentativeChildContentStep(
        graph,
        *childNode,
        current,
        static_cast<uint8_t>(depth + 1U),
        childLocalCycleIndex,
        0,
        scaleSettings,
        noteOffsetsUseScaleDegrees,
        outSummary
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
        .length = length,
        .ownerNodeId = ownerNodeId,
        .sequenceId = sequenceId,
        .cycleSetId = cycleSetId,
    };
    if (index == 0) {
        view.rootPageSnapshot = sequencer.page.get();
        view.rootFocusSnapshot = sequencer.focusedStep.get();
    }
    ++view.stackDepth;
    syncPublicViewFields(view);
    sequencer.page.set(0);
    sequencer.focusedStep.set(0);
    sequencer.structureUi.stepSelection.reset();
    view.bump();
    return true;
}

FLASHMEM bool validateFrame(
    const SequencerState& sequencer,
    SequencerContentViewFrame& frame
) {
    const auto* graph = graphView(authoringPattern(sequencer));
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
    uint8_t frameDepth,
    bool noteOffsetsUseScaleDegrees
) {
    const auto& view = sequencer.contentView;
    if (view.stackDepth == 0 || view.stackDepth > view.frames.size()) return {};
    if (frameDepth == 0 || frameDepth > view.stackDepth) return {};

    const auto* graph = graphView(authoringPattern(sequencer));
    if (graph == nullptr) return {};

    const auto& first = view.frames[0];
    ResolvedStep current = rootBase(sequencer, first.ownerRootStep);
    const Node* rootNode = graph->stepNode(rootStepNodeId(first.ownerRootStep));
    if (rootNode == nullptr) return {};
    current = applyNode(
        current,
        *rootNode,
        scaleSettings,
        noteOffsetsUseScaleDegrees
    );
    if (view.drumOwnerActive) {
        current.note = rootBase(sequencer, first.ownerRootStep).note;
        current.chordState = oc::note::sequencer::defaultRootChordState();
        current.inheritedChord = {};
    }

    for (uint8_t i = 1; i < frameDepth; ++i) {
        const auto& frame = view.frames[i];
        const auto& containingFrame = view.frames[i - 1U];
        const Node* ownerNode = graph->stepNode(frame.ownerNodeId);
        if (ownerNode == nullptr) return {};
        current = applyNode(
            contentBaseForKind(current, containingFrame.kind, scaleSettings),
            *ownerNode,
            scaleSettings,
            noteOffsetsUseScaleDegrees
        );
        if (view.drumOwnerActive) {
            current.note = rootBase(sequencer, first.ownerRootStep).note;
            current.chordState = oc::note::sequencer::defaultRootChordState();
            current.inheritedChord = {};
        }
    }
    return current;
}

FLASHMEM ResolvedStep resolveOwnerStep(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool noteOffsetsUseScaleDegrees
) {
    return resolveOwnerStepAtDepth(
        sequencer,
        scaleSettings,
        sequencer.contentView.stackDepth,
        noteOffsetsUseScaleDegrees
    );
}

FLASHMEM SequencerGraphNodeId stepNodeIdForFrame(
    const SequencerState& sequencer,
    const SequencerContentViewFrame& frame,
    uint8_t step
) {
    const auto* graph = graphView(authoringPattern(sequencer));
    if (graph == nullptr || step >= frame.length) return kInvalidId;

    if (frame.kind == SequencerContentViewKind::MICRO_SEQUENCE) {
        const auto* sequence = graph->sequence(frame.sequenceId);
        if (sequence == nullptr || step >= sequence->length) return kInvalidId;
        const uint8_t sourceIndex = note::normalizeSequenceIndex(step, sequence->offset, sequence->length);
        return static_cast<uint16_t>(sequence->firstStepNode + sourceIndex);
    }

    if (frame.kind == SequencerContentViewKind::CYCLE_STATES) {
        const auto* cycleSet = graph->cycleSet(frame.cycleSetId);
        if (cycleSet == nullptr || step >= cycleSet->length) return kInvalidId;
        const uint8_t sourceIndex = note::normalizeSequenceIndex(step, cycleSet->offset, cycleSet->length);
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
    if (pitch_edit::usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        const int parentDegree =
            pitch_edit::scaleDegreeIndexForNote(static_cast<uint8_t>(std::clamp(parentValue, 0, 127)), scaleSettings);
        const int targetDegree =
            pitch_edit::scaleDegreeIndexForNote(static_cast<uint8_t>(std::clamp(targetValue, 0, 127)), scaleSettings);
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
        authoringPattern(sequencer),
                nodeId,
                static_cast<int8_t>(std::clamp(offset, -128, 127))
            );
            break;
        }
        case StepProperty::VELOCITY:
            changed = setNodeVelocityOffset(
        authoringPattern(sequencer),
                nodeId,
                static_cast<int16_t>(targetValue - baseValue)
            );
            break;
        case StepProperty::GATE:
            changed = setNodeGateOffset(
        authoringPattern(sequencer),
                nodeId,
                static_cast<int16_t>(targetValue - baseValue)
            );
            break;
        case StepProperty::NUDGE:
            changed = setNodeNudgeOffset(
        authoringPattern(sequencer),
                nodeId,
                static_cast<int8_t>(std::clamp(targetValue - baseValue, -128, 127))
            );
            break;
        case StepProperty::PROBABILITY:
            changed = setNodeProbabilityOffset(
        authoringPattern(sequencer),
                nodeId,
                static_cast<int16_t>(targetValue - baseValue)
            );
            break;
    }
    if (changed) {
        sequencer.contentView.bump();
        notifyStepContentDraftMutation(sequencer);
    }
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
