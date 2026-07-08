#include "state/sequencer/SequencerGraphOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

using namespace graph_ops_internal;

namespace {

FLASHMEM oc::note::sequencer::StepSequencerChordMode sanitizeChordMode(
    oc::note::sequencer::StepSequencerChordMode mode
) {
    if (static_cast<uint8_t>(mode) >
        static_cast<uint8_t>(oc::note::sequencer::StepSequencerChordMode::Local)) {
        return oc::note::sequencer::StepSequencerChordMode::Single;
    }
    return mode;
}

}  // namespace

FLASHMEM bool setNodeEnabledOverride(SequencerPatternState& pattern,
                                     SequencerGraphNodeId nodeId,
                                     bool enabled) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = assignFlag(node.flags, STEP_NODE_ENABLED_OVERRIDE, true);
    changed = assignFlag(node.flags, STEP_NODE_ENABLED_VALUE, enabled) || changed;
    bump(pattern, changed);
    return changed;
}

FLASHMEM bool clearNodeEnabledOverride(SequencerPatternState& pattern,
                                       SequencerGraphNodeId nodeId) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = assignFlag(node.flags, STEP_NODE_ENABLED_OVERRIDE, false);
    changed = assignFlag(node.flags, STEP_NODE_ENABLED_VALUE, false) || changed;
    bump(pattern, changed);
    return changed;
}

FLASHMEM bool setNodeNoteOffset(SequencerPatternState& pattern,
                                SequencerGraphNodeId nodeId,
                                int8_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (offset == 0) {
        changed = assignFlag(node.flags, STEP_NODE_NOTE_OFFSET, false);
        if (node.noteOffset != 0) {
            node.noteOffset = 0;
            changed = true;
        }
    } else {
        if (node.noteOffset != offset) {
            node.noteOffset = offset;
            changed = true;
        }
        changed = assignFlag(node.flags, STEP_NODE_NOTE_OFFSET, true) || changed;
    }

    bump(pattern, changed);
    return changed;
}

FLASHMEM bool setNodeVelocityOffset(SequencerPatternState& pattern,
                                    SequencerGraphNodeId nodeId,
                                    int16_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;
    return setSignedOffset(
        pattern,
        nodeId,
        STEP_NODE_VELOCITY_OFFSET,
        graph->stepNodes[nodeId].velocityOffset,
        offset
    );
}

FLASHMEM bool setNodeGateOffset(SequencerPatternState& pattern,
                                SequencerGraphNodeId nodeId,
                                int16_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;
    return setSignedOffset(
        pattern,
        nodeId,
        STEP_NODE_GATE_OFFSET,
        graph->stepNodes[nodeId].gateOffset,
        offset
    );
}

FLASHMEM bool setNodeNudgeOffset(SequencerPatternState& pattern,
                                 SequencerGraphNodeId nodeId,
                                 int8_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (offset == 0) {
        changed = assignFlag(node.flags, STEP_NODE_NUDGE_OFFSET, false);
        if (node.nudgeOffset != 0) {
            node.nudgeOffset = 0;
            changed = true;
        }
    } else {
        if (node.nudgeOffset != offset) {
            node.nudgeOffset = offset;
            changed = true;
        }
        changed = assignFlag(node.flags, STEP_NODE_NUDGE_OFFSET, true) || changed;
    }

    bump(pattern, changed);
    return changed;
}

FLASHMEM bool setNodeProbabilityOffset(SequencerPatternState& pattern,
                                       SequencerGraphNodeId nodeId,
                                       int16_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;
    return setSignedOffset(
        pattern,
        nodeId,
        STEP_NODE_PROBABILITY_OFFSET,
        graph->stepNodes[nodeId].probabilityOffset,
        offset
    );
}

FLASHMEM bool setNodeChordMode(SequencerPatternState& pattern,
                               SequencerGraphNodeId nodeId,
                               oc::note::sequencer::StepSequencerChordMode mode) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    const auto nextMode = sanitizeChordMode(mode);
    bool changed = false;
    if (node.chordMode != nextMode) {
        node.chordMode = nextMode;
        changed = true;
    }
    changed = assignFlag(node.flags, STEP_NODE_CHORD_MODE, true) || changed;
    bump(pattern, changed);
    return changed;
}

FLASHMEM bool setNodeChordSpec(SequencerPatternState& pattern,
                               SequencerGraphNodeId nodeId,
                               oc::note::sequencer::StepSequencerChordSpec spec) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    spec.clamp();
    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (node.chordMode != oc::note::sequencer::StepSequencerChordMode::Local) {
        node.chordMode = oc::note::sequencer::StepSequencerChordMode::Local;
        changed = true;
    }
    if (node.chordSpec.voiceCount != spec.voiceCount ||
        node.chordSpec.color != spec.color ||
        node.chordSpec.variant != spec.variant ||
        node.chordSpec.spread != spec.spread ||
        node.chordSpec.strum != spec.strum ||
        node.chordSpec.velocityCurve != spec.velocityCurve) {
        node.chordSpec = spec;
        changed = true;
    }
    changed = assignFlag(node.flags, STEP_NODE_CHORD_MODE, true) || changed;
    changed = assignFlag(node.flags, STEP_NODE_CHORD_LOCAL, true) || changed;
    bump(pattern, changed);
    return changed;
}

FLASHMEM bool clearNodeChordState(SequencerPatternState& pattern, SequencerGraphNodeId nodeId) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (node.chordMode != oc::note::sequencer::StepSequencerChordMode::Single) {
        node.chordMode = oc::note::sequencer::StepSequencerChordMode::Single;
        changed = true;
    }
    const oc::note::sequencer::StepSequencerChordSpec defaultSpec{};
    if (node.chordSpec.voiceCount != defaultSpec.voiceCount ||
        node.chordSpec.color != defaultSpec.color ||
        node.chordSpec.variant != defaultSpec.variant ||
        node.chordSpec.spread != defaultSpec.spread ||
        node.chordSpec.strum != defaultSpec.strum ||
        node.chordSpec.velocityCurve != defaultSpec.velocityCurve) {
        node.chordSpec = defaultSpec;
        changed = true;
    }
    changed = assignFlag(node.flags, STEP_NODE_CHORD_MODE, false) || changed;
    changed = assignFlag(node.flags, STEP_NODE_CHORD_LOCAL, false) || changed;
    bump(pattern, changed);
    return changed;
}

FLASHMEM uint8_t nodeLocalVariationRange(
    const oc::note::sequencer::StepSequencerStepNode& node,
    StepProperty property
) {
    auto ranges = node.localVariation;
    ranges.clamp();
    switch (property) {
        case StepProperty::NOTE:
            return ranges.pitchSemitones;
        case StepProperty::VELOCITY:
            return ranges.velocity;
        case StepProperty::GATE:
            return ranges.gatePercent;
        case StepProperty::NUDGE:
            return ranges.nudge;
        case StepProperty::PROBABILITY:
            return 0;
    }
    return 0;
}

FLASHMEM bool setNodeLocalVariationRange(SequencerPatternState& pattern,
                                         SequencerGraphNodeId nodeId,
                                         StepProperty property,
                                         uint8_t range) {
    if (property == StepProperty::PROBABILITY) return false;

    if (graphView(pattern) == nullptr && range == 0) {
        return false;
    }

    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto next = graph->stepNodes[nodeId].localVariation;
    switch (property) {
        case StepProperty::NOTE:
            next.pitchSemitones = range;
            break;
        case StepProperty::VELOCITY:
            next.velocity = range;
            break;
        case StepProperty::GATE:
            next.gatePercent = range;
            break;
        case StepProperty::NUDGE:
            next.nudge = range;
            break;
        case StepProperty::PROBABILITY:
            return false;
    }
    next.clamp();

    auto& current = graph->stepNodes[nodeId].localVariation;
    auto currentClamped = current;
    currentClamped.clamp();
    if (currentClamped.pitchSemitones == next.pitchSemitones &&
        currentClamped.velocity == next.velocity &&
        currentClamped.gatePercent == next.gatePercent &&
        currentClamped.nudge == next.nudge) {
        return false;
    }

    current = next;
    bump(pattern, true);
    return true;
}

}  // namespace core::state::sequencer
