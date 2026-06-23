#include "ui/sequencer/StepContentBadgeProjection.hpp"

#include <oc/note/sequencer/StepSequencerChord.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"

namespace core::ui::sequencer::grid {
namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSource;

}  // namespace

FLASHMEM StepContentBadgeProjection buildStepContentBadgeProjection(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t absoluteStep
) {
    const auto rootNodeId = core::state::sequencer::rootStepNodeId(absoluteStep);
    return buildStepContentBadgeProjectionForNode(pattern, rootNodeId);
}

FLASHMEM StepContentBadgeProjection buildStepContentBadgeProjectionForNode(
    const core::state::sequencer::SequencerPatternState& pattern,
    core::state::sequencer::SequencerGraphNodeId nodeId
) {
    StepContentBadgeProjection badges;
    const auto* graph = core::state::sequencer::graphView(pattern);
    if (graph == nullptr) return badges;

    const auto* node = graph->stepNode(nodeId);
    if (node == nullptr) return badges;

    badges.microSequence =
        node->has(STEP_NODE_CHILD_SEQUENCE) &&
        graph->sequence(node->childSequenceId) != nullptr;
    badges.cycleStates =
        node->has(STEP_NODE_CYCLE_SET) &&
        graph->cycleSet(node->cycleSetId) != nullptr;
    const bool localChord =
        node->has(STEP_NODE_CHORD_MODE) &&
        node->chordMode == StepSequencerChordMode::Local &&
        node->has(STEP_NODE_CHORD_LOCAL);
    if (localChord) {
        auto spec = node->chordSpec;
        spec.clamp();
        badges.chord = spec.voiceCount > 1;
        badges.chordVoiceCount = spec.voiceCount;
        badges.chordSource = StepSequencerChordSource::Local;
    }
    return badges;
}

FLASHMEM bool mergeExpandedTelemetryChordBadgeForNode(
    StepContentBadgeProjection& badges,
    const oc::note::sequencer::StepSequencerExpandedVariationTelemetry& telemetry,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    int16_t playheadStep,
    uint32_t tickOffset
) {
    if (!telemetry.valid ||
        nodeId == oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID ||
        playheadStep < 0 ||
        telemetry.rootStepIndex != static_cast<uint8_t>(playheadStep)) {
        return false;
    }

    bool found = false;
    uint8_t voiceCount = badges.chordVoiceCount;
    StepSequencerChordSource source = badges.chordSource;
    for (uint8_t i = 0; i < telemetry.count; ++i) {
        if (telemetry.nodeId[i] != nodeId) continue;
        const uint32_t start = telemetry.localTick[i];
        const uint32_t span = telemetry.spanTicks[i] == 0 ? 1U : telemetry.spanTicks[i];
        const uint32_t end = start + span;
        if (tickOffset < start || tickOffset >= end) continue;

        if (telemetry.chordVoiceCount[i] > voiceCount) {
            voiceCount = telemetry.chordVoiceCount[i];
            source = telemetry.chordSource[i];
        }
        found = true;
    }

    if (!found || voiceCount <= 1) return false;
    badges.chord = true;
    badges.chordVoiceCount = voiceCount;
    badges.chordSource = source;
    return true;
}

}  // namespace core::ui::sequencer::grid
