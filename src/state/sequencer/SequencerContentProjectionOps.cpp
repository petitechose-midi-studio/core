#include "state/sequencer/SequencerContentViewOps.hpp"

#include <algorithm>

#include "state/sequencer/SequencerContentViewInternal.hpp"
#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::state::sequencer {
using namespace content_view_internal;

FLASHMEM SequencerContentStepProjection resolveActiveContentStepProjection(
    const SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    const bool noteOffsetsUseScaleDegrees = pitchContextUsesScaleDegrees(
        authoringPattern(sequencer).pitchEditMode,
        scaleSettings
    );
    SequencerContentStepProjection out{};
    out.localStep = step;
    out.rootContext = isRootContentView(sequencer);

    const auto* graph = graphView(authoringPattern(sequencer));
    if (graph == nullptr) {
        if (!out.rootContext) return out;
    }

    if (out.rootContext) {
        if (step >= activeContentLength(sequencer)) return out;
        const auto nodeId = rootStepNodeId(step);
        const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
        ResolvedStep base = rootBase(sequencer, step);
        ResolvedStep resolved = node
            ? applyNode(
                  base,
                  *node,
                  scaleSettings,
                  noteOffsetsUseScaleDegrees
              )
            : base;
        if (!resolved.valid) return out;

        out.valid = true;
        out.rootStep = step;
        out.nodeId = nodeId;
        out.enabled = resolved.enabled;
        out.parentEnabled = base.enabled;
        out.parentNote = base.note;
        out.note = resolved.note;
        out.parentVelocity = base.velocity;
        out.velocity = resolved.velocity;
        out.parentGate = base.gate;
        out.gate = resolved.gate;
        out.parentNudge = base.nudge;
        out.nudge = resolved.nudge;
        out.parentProbability = base.probability;
        out.probability = resolved.probability;
        out.inheritedChord = resolved.inheritedChord;
        if (node != nullptr) {
            out.noteOffset = node->noteOffset;
            out.velocityOffset = node->velocityOffset;
            out.gateOffset = node->gateOffset;
            out.nudgeOffset = node->nudgeOffset;
            out.probabilityOffset = node->probabilityOffset;
            out.hasMicroSequence = nodeHasMicroSequence(*graph, *node);
            out.hasCycleStates = nodeHasCycleStates(*graph, *node);
        }
        return out;
    }

    const auto* frame = sequencer.contentView.currentFrame();
    if (frame == nullptr || step >= frame->length || graph == nullptr) return out;

    const auto nodeId = stepNodeIdForFrame(sequencer, *frame, step);
    const auto* node = graph->stepNode(nodeId);
    if (node == nullptr) return out;

    const ResolvedStep owner = resolveOwnerStep(
        sequencer,
        scaleSettings,
        noteOffsetsUseScaleDegrees
    );
    const ResolvedStep base = contentBaseForKind(owner, frame->kind, scaleSettings);
    const ResolvedStep resolved = applyNode(
        base,
        *node,
        scaleSettings,
        noteOffsetsUseScaleDegrees
    );
    if (!base.valid || !resolved.valid) return out;

    out.valid = true;
    out.rootContext = false;
    out.rootStep = frame->ownerRootStep;
    out.localStep = step;
    out.nodeId = nodeId;
    out.enabled = resolved.enabled;
    out.parentEnabled = base.enabled;
    out.parentNote = base.note;
    out.note = sequencer.contentView.drumOwnerActive ? base.note : resolved.note;
    out.parentVelocity = base.velocity;
    out.velocity = resolved.velocity;
    out.parentGate = base.gate;
    out.gate = resolved.gate;
    out.parentNudge = base.nudge;
    out.nudge = resolved.nudge;
    out.parentProbability = base.probability;
    out.probability = resolved.probability;
    out.inheritedChord = sequencer.contentView.drumOwnerActive
        ? oc::note::sequencer::StepSequencerInheritedChord{}
        : resolved.inheritedChord;
    out.noteOffset = sequencer.contentView.drumOwnerActive ? 0 : node->noteOffset;
    out.velocityOffset = node->velocityOffset;
    out.gateOffset = node->gateOffset;
    out.nudgeOffset = node->nudgeOffset;
    out.probabilityOffset = node->probabilityOffset;
    out.hasMicroSequence = nodeHasMicroSequence(*graph, *node);
    out.hasCycleStates = nodeHasCycleStates(*graph, *node);
    return out;
}

FLASHMEM SequencerContentStepProjection resolveActiveContentOwnerProjection(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    if (isRootContentView(sequencer)) {
        return resolveActiveContentStepProjection(sequencer, sequencer.focusedStep.get(), scaleSettings);
    }

    SequencerContentStepProjection out{};
    const auto* frame = sequencer.contentView.currentFrame();
    const auto* graph = graphView(authoringPattern(sequencer));
    if (frame == nullptr || graph == nullptr) return out;

    const bool noteOffsetsUseScaleDegrees = pitchContextUsesScaleDegrees(
        authoringPattern(sequencer).pitchEditMode,
        scaleSettings
    );
    const ResolvedStep owner = resolveOwnerStep(
        sequencer,
        scaleSettings,
        noteOffsetsUseScaleDegrees
    );
    if (!owner.valid) return out;

    out.valid = true;
    out.rootContext = false;
    out.rootStep = frame->ownerRootStep;
    out.localStep = frame->ownerLocalStep;
    out.nodeId = frame->ownerNodeId;
    out.enabled = owner.enabled;
    out.parentEnabled = owner.enabled;
    out.parentNote = owner.note;
    out.note = owner.note;
    out.parentVelocity = owner.velocity;
    out.velocity = owner.velocity;
    out.parentGate = owner.gate;
    out.gate = owner.gate;
    out.parentNudge = owner.nudge;
    out.nudge = owner.nudge;
    out.parentProbability = owner.probability;
    out.probability = owner.probability;
    out.inheritedChord = owner.inheritedChord;

    const auto* node = graph->stepNode(frame->ownerNodeId);
    if (node != nullptr) {
        out.noteOffset = node->noteOffset;
        out.velocityOffset = node->velocityOffset;
        out.gateOffset = node->gateOffset;
        out.nudgeOffset = node->nudgeOffset;
        out.probabilityOffset = node->probabilityOffset;
        out.hasMicroSequence = nodeHasMicroSequence(*graph, *node);
        out.hasCycleStates = nodeHasCycleStates(*graph, *node);
    }

    return out;
}

FLASHMEM SequencerContentStepProjection resolveContentFrameOwnerProjection(
    const SequencerState& sequencer,
    uint8_t frameDepth,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    const bool noteOffsetsUseScaleDegrees = pitchContextUsesScaleDegrees(
        authoringPattern(sequencer).pitchEditMode,
        scaleSettings
    );

    SequencerContentStepProjection out{};
    const auto& view = sequencer.contentView;
    const auto* graph = graphView(authoringPattern(sequencer));
    if (frameDepth == 0 ||
        frameDepth > view.stackDepth ||
        frameDepth > view.frames.size() ||
        graph == nullptr) {
        return out;
    }

    const auto& frame = view.frames[frameDepth - 1U];
    const ResolvedStep owner = resolveOwnerStepAtDepth(
        sequencer,
        scaleSettings,
        frameDepth,
        noteOffsetsUseScaleDegrees
    );
    if (!owner.valid) return out;

    out.valid = true;
    out.rootContext = false;
    out.rootStep = frame.ownerRootStep;
    out.localStep = frame.ownerLocalStep;
    out.nodeId = frame.ownerNodeId;
    out.enabled = owner.enabled;
    out.parentEnabled = owner.enabled;
    out.parentNote = owner.note;
    out.note = owner.note;
    out.parentVelocity = owner.velocity;
    out.velocity = owner.velocity;
    out.parentGate = owner.gate;
    out.gate = owner.gate;
    out.parentNudge = owner.nudge;
    out.nudge = owner.nudge;
    out.parentProbability = owner.probability;
    out.probability = owner.probability;
    out.inheritedChord = owner.inheritedChord;

    const auto* node = graph->stepNode(frame.ownerNodeId);
    if (node != nullptr) {
        out.noteOffset = node->noteOffset;
        out.velocityOffset = node->velocityOffset;
        out.gateOffset = node->gateOffset;
        out.nudgeOffset = node->nudgeOffset;
        out.probabilityOffset = node->probabilityOffset;
        out.hasMicroSequence = nodeHasMicroSequence(*graph, *node);
        out.hasCycleStates = nodeHasCycleStates(*graph, *node);
    }

    return out;
}

FLASHMEM SequencerContentPlaybackProjection resolveActiveContentPlaybackProjection(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    if (!isChildContentView(sequencer) || sequencer.playheadStep.get() < 0) return {};

    const auto& view = sequencer.contentView;
    const auto* frame = view.currentFrame();
    if (frame == nullptr || frame->length == 0 || view.stackDepth == 0) return {};

    const uint8_t rootLength = authoringPattern(sequencer).length.get();
    if (rootLength == 0) return {};

    const uint8_t rootStep = static_cast<uint8_t>(sequencer.playheadStep.get());
    if (rootStep >= rootLength) return {};

    const auto& firstFrame = view.frames[0];
    const uint8_t ownerStep = firstFrame.ownerRootStep;
    if (ownerStep >= rootLength) return {};

    const uint8_t stepsSinceOwner = rootStep >= ownerStep
        ? static_cast<uint8_t>(rootStep - ownerStep)
        : static_cast<uint8_t>(rootStep + rootLength - ownerStep);
    bool active = sequencer.probabilityCycleMask.test(ownerStep);

    const auto rootOwner = resolveContentFrameOwnerProjection(sequencer, 1, scaleSettings);
    if (!rootOwner.valid) return {};
    if (!rootOwner.enabled || rootOwner.gate == 0) {
        active = false;
    }

    const uint32_t ticks = sequencer.playheadStepTicks == 0 ? 1U : sequencer.playheadStepTicks;
    const uint32_t integerTickOffset = std::min<uint32_t>(
        sequencer.playheadStepTickOffset.get(),
        ticks - 1U
    );
    const uint32_t reconstructedTickPositionQ8 =
        static_cast<uint32_t>(sequencer.playheadStepPhaseQ8.get()) * ticks;
    const uint32_t integerTickPositionQ8 = integerTickOffset * 256U;
    const uint32_t subTickQ8 = reconstructedTickPositionQ8 > integerTickPositionQ8
        ? std::min<uint32_t>(
              255U,
              reconstructedTickPositionQ8 - integerTickPositionQ8
          )
        : 0U;
    uint32_t spanTicks = effectiveGateSpan(ticks, rootOwner.gate);
    uint32_t elapsedTicks =
        static_cast<uint32_t>(stepsSinceOwner) * ticks +
        integerTickOffset;
    if (elapsedTicks >= spanTicks) return {};

    uint32_t localCycleIndex = sequencer.probabilityCycleIndex;
    for (uint8_t depth = 1; depth < view.stackDepth; ++depth) {
        const auto& containingFrame = view.frames[depth - 1U];
        const auto& childFrame = view.frames[depth];
        const auto owner = resolveContentFrameOwnerProjection(
            sequencer,
            static_cast<uint8_t>(depth + 1U),
            scaleSettings
        );
        if (!owner.valid) return {};
        if (!owner.enabled || owner.gate == 0) {
            active = false;
        }

        if (containingFrame.kind == SequencerContentViewKind::CYCLE_STATES) {
            if (containingFrame.length == 0) return {};
            const uint8_t selected =
                static_cast<uint8_t>(localCycleIndex % containingFrame.length);
            if (selected != childFrame.ownerLocalStep) return {};

            localCycleIndex /= containingFrame.length;
            spanTicks = effectiveGateSpan(spanTicks, owner.gate);
            if (elapsedTicks >= spanTicks) return {};
            continue;
        }

        if (containingFrame.kind == SequencerContentViewKind::MICRO_SEQUENCE) {
            if (containingFrame.length == 0 || elapsedTicks >= spanTicks) return {};
            uint8_t selected = static_cast<uint8_t>(
                (elapsedTicks * static_cast<uint32_t>(containingFrame.length)) / spanTicks
            );
            if (selected >= containingFrame.length) {
                selected = static_cast<uint8_t>(containingFrame.length - 1U);
            }
            if (selected != childFrame.ownerLocalStep) return {};

            const uint32_t childStart = boundaryTick(selected, spanTicks, containingFrame.length);
            const uint32_t childEnd = boundaryTick(
                static_cast<uint8_t>(selected + 1U),
                spanTicks,
                containingFrame.length
            );
            elapsedTicks -= childStart;
            spanTicks = effectiveGateSpan(
                std::max<uint32_t>(childEnd - childStart, 1U),
                owner.gate
            );
            if (elapsedTicks >= spanTicks) return {};
        }
    }

    uint8_t childPlayhead = 0;
    uint32_t childElapsedTicks = elapsedTicks;
    uint32_t childSpanTicks = std::max<uint32_t>(1U, spanTicks);
    if (frame->kind == SequencerContentViewKind::MICRO_SEQUENCE) {
        if (elapsedTicks >= spanTicks) return {};
        childPlayhead = static_cast<uint8_t>(
            (elapsedTicks * static_cast<uint32_t>(frame->length)) / spanTicks
        );
        if (childPlayhead >= frame->length) {
            childPlayhead = static_cast<uint8_t>(frame->length - 1U);
        }
        const uint32_t childStart = boundaryTick(
            childPlayhead,
            spanTicks,
            frame->length
        );
        const uint32_t childEnd = boundaryTick(
            static_cast<uint8_t>(childPlayhead + 1U),
            spanTicks,
            frame->length
        );
        childElapsedTicks = elapsedTicks - childStart;
        childSpanTicks = std::max<uint32_t>(1U, childEnd - childStart);
    } else {
        childPlayhead = static_cast<uint8_t>(localCycleIndex % frame->length);
    }

    const auto childProjection = resolveActiveContentStepProjection(
        sequencer,
        childPlayhead,
        scaleSettings
    );
    if (!childProjection.valid || !childProjection.enabled) {
        active = false;
    }

    return {
        .visible = true,
        .active = active,
        .step = childPlayhead,
        .progress = static_cast<uint8_t>(std::min<uint32_t>(
            255U,
            static_cast<uint32_t>(
                ((static_cast<uint64_t>(childElapsedTicks) * 256U + subTickQ8) *
                 255U) /
                (static_cast<uint64_t>(childSpanTicks) * 256U)
            )
        )),
    };
}

struct ChildContentRuntimeCursor {
    uint32_t cycleIndex = 0;
    uint8_t microPlayIndex = 0;
};

FLASHMEM uint8_t microPlayIndexForNode(
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node,
    uint32_t elapsedTicks,
    uint32_t spanTicks
) {
    if (!node.has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE) || spanTicks == 0) {
        return 0;
    }

    const auto* sequence = graph.sequence(node.childSequenceId);
    if (sequence == nullptr || sequence->length == 0) return 0;

    uint8_t playIndex = static_cast<uint8_t>(
        (std::min<uint32_t>(elapsedTicks, spanTicks - 1U) *
         static_cast<uint32_t>(sequence->length)) /
        spanTicks
    );
    if (playIndex >= sequence->length) {
        playIndex = static_cast<uint8_t>(sequence->length - 1U);
    }
    return playIndex;
}

FLASHMEM void focusMicroTimingWindow(
    uint8_t localStep,
    uint8_t length,
    uint32_t& elapsedTicks,
    uint32_t& spanTicks,
    uint16_t gatePercent
) {
    if (length == 0 || spanTicks == 0) return;

    const uint32_t childStart = boundaryTick(localStep, spanTicks, length);
    const uint32_t childEnd = boundaryTick(
        static_cast<uint8_t>(localStep + 1U),
        spanTicks,
        length
    );
    const uint32_t childSpan = std::max<uint32_t>(childEnd - childStart, 1U);
    if (elapsedTicks >= childStart && elapsedTicks < childEnd) {
        elapsedTicks -= childStart;
    } else {
        elapsedTicks = 0;
    }
    spanTicks = effectiveGateSpan(childSpan, gatePercent);
}

FLASHMEM ChildContentRuntimeCursor childContentRuntimeCursorForProjection(
    const SequencerState& sequencer,
    const SequencerContentStepProjection& projection,
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    ChildContentRuntimeCursor cursor{
        .cycleIndex = sequencer.probabilityCycleIndex,
        .microPlayIndex = 0,
    };

    const uint32_t ticks = sequencer.playheadStepTicks == 0 ? 1U : sequencer.playheadStepTicks;
    uint32_t spanTicks = effectiveGateSpan(ticks, projection.gate);
    uint32_t elapsedTicks = 0;

    if (projection.rootContext) {
        if (sequencer.playheadStep.get() >= 0 &&
            projection.rootStep == static_cast<uint8_t>(sequencer.playheadStep.get())) {
            elapsedTicks = std::min<uint32_t>(
                sequencer.playheadStepTickOffset.get(),
                spanTicks - 1U
            );
        }
        cursor.microPlayIndex = microPlayIndexForNode(graph, node, elapsedTicks, spanTicks);
        return cursor;
    }

    const auto& view = sequencer.contentView;
    const auto* frame = view.currentFrame();
    if (frame == nullptr || view.stackDepth == 0) {
        cursor.microPlayIndex = microPlayIndexForNode(graph, node, elapsedTicks, spanTicks);
        return cursor;
    }

    const uint8_t rootLength = authoringPattern(sequencer).length.get();
    const auto& firstFrame = view.frames[0];
    if (rootLength > 0 &&
        firstFrame.ownerRootStep < rootLength &&
        sequencer.playheadStep.get() >= 0) {
        const uint8_t rootStep = static_cast<uint8_t>(sequencer.playheadStep.get());
        const uint8_t stepsSinceOwner = rootStep >= firstFrame.ownerRootStep
            ? static_cast<uint8_t>(rootStep - firstFrame.ownerRootStep)
            : static_cast<uint8_t>(rootStep + rootLength - firstFrame.ownerRootStep);
        const auto rootOwner = resolveContentFrameOwnerProjection(sequencer, 1, scaleSettings);
        spanTicks = effectiveGateSpan(ticks, rootOwner.valid ? rootOwner.gate : projection.gate);
        elapsedTicks =
            static_cast<uint32_t>(stepsSinceOwner) * ticks +
            static_cast<uint32_t>(sequencer.playheadStepTickOffset.get());
    }

    for (uint8_t depth = 1; depth < view.stackDepth; ++depth) {
        const auto& containingFrame = view.frames[depth - 1U];
        const auto& childFrame = view.frames[depth];
        const auto owner = resolveContentFrameOwnerProjection(
            sequencer,
            static_cast<uint8_t>(depth + 1U),
            scaleSettings
        );
        const uint16_t ownerGate = owner.valid ? owner.gate : projection.gate;

        if (containingFrame.kind == SequencerContentViewKind::CYCLE_STATES) {
            if (containingFrame.length == 0) return cursor;
            cursor.cycleIndex /= containingFrame.length;
            spanTicks = effectiveGateSpan(spanTicks, ownerGate);
            continue;
        }

        if (containingFrame.kind == SequencerContentViewKind::MICRO_SEQUENCE) {
            focusMicroTimingWindow(
                childFrame.ownerLocalStep,
                containingFrame.length,
                elapsedTicks,
                spanTicks,
                ownerGate
            );
        }
    }

    if (frame->kind == SequencerContentViewKind::CYCLE_STATES) {
        if (frame->length > 0) {
            cursor.cycleIndex /= frame->length;
            spanTicks = effectiveGateSpan(spanTicks, projection.gate);
        }
    } else if (frame->kind == SequencerContentViewKind::MICRO_SEQUENCE) {
        focusMicroTimingWindow(
            projection.localStep,
            frame->length,
            elapsedTicks,
            spanTicks,
            projection.gate
        );
    }

    cursor.microPlayIndex = microPlayIndexForNode(graph, node, elapsedTicks, spanTicks);
    return cursor;
}

FLASHMEM bool resolveRepresentativeChildContentSummary(
    const SequencerState& sequencer,
    const SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    SequencerChildContentSummary& outSummary
) {
    scaleSettings.clamp();
    outSummary = SequencerChildContentSummary{};
    if (!projection.valid ||
        (!projection.hasMicroSequence && !projection.hasCycleStates)) {
        return false;
    }

    const auto* graph = graphView(authoringPattern(sequencer));
    const auto* node = graph ? graph->stepNode(projection.nodeId) : nullptr;
    if (graph == nullptr || node == nullptr) return false;

    const auto runtimeCursor = childContentRuntimeCursorForProjection(
        sequencer,
        projection,
        *graph,
        *node,
        scaleSettings
    );
    ResolvedStep current{
        .valid = true,
        .enabled = projection.enabled,
        .note = projection.note,
        .velocity = projection.velocity,
        .gate = projection.gate,
        .nudge = projection.nudge,
        .probability = projection.probability,
    };
    const bool touchedChild = resolveRepresentativeChildContentStep(
        *graph,
        *node,
        current,
        projection.rootContext ? 0 : activeContentDepth(sequencer),
        runtimeCursor.cycleIndex,
        runtimeCursor.microPlayIndex,
        scaleSettings,
        pitchContextUsesScaleDegrees(
            authoringPattern(sequencer).pitchEditMode,
            scaleSettings
        ),
        &outSummary
    );

    outSummary.enabled = current.enabled;
    outSummary.note = current.note;
    outSummary.velocity = current.velocity;
    outSummary.gate = current.gate;
    outSummary.nudge = current.nudge;
    outSummary.probability = current.probability;
    return touchedChild;
}

FLASHMEM bool resolveRepresentativeChildContentNote(
    const SequencerState& sequencer,
    const SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    uint8_t& outNote
) {
    SequencerChildContentSummary summary{};
    const bool touchedChild = resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        scaleSettings,
        summary
    );
    outNote = summary.note;
    return touchedChild && outNote != projection.note;
}

FLASHMEM bool stepContentProjectionHasAnyChild(
    const SequencerContentStepProjection& projection
) {
    return projection.hasMicroSequence || projection.hasCycleStates;
}

FLASHMEM bool stepContentProjectionHasChild(
    const SequencerContentStepProjection& projection,
    StepContentChildKind childKind
) {
    return childKind == StepContentChildKind::MICRO_SEQUENCE
        ? projection.hasMicroSequence
        : projection.hasCycleStates;
}

FLASHMEM int16_t stepContentProjectionOffsetForProperty(
    const SequencerContentStepProjection& projection,
    StepProperty property
) {
    switch (property) {
        case StepProperty::NOTE:
            return projection.noteOffset;
        case StepProperty::VELOCITY:
            return projection.velocityOffset;
        case StepProperty::GATE:
            return projection.gateOffset;
        case StepProperty::NUDGE:
            return projection.nudgeOffset;
        case StepProperty::PROBABILITY:
            return projection.probabilityOffset;
    }

    return 0;
}

}  // namespace core::state::sequencer
