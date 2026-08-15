#include "ui/sequencer/StepContentBadgeProjection.hpp"

#include <algorithm>

#include <oc/note/sequencer/StepSequencerChord.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>
#include <oc/note/clock/ClockConstants.hpp>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"

namespace core::ui::sequencer::grid {
namespace {

using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
using oc::note::sequencer::STEP_NODE_ENABLED_VALUE;
using oc::note::sequencer::StepSequencerGraphLimits;
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
        core::state::sequencer::stepNodeHasMicroSequence(pattern, nodeId);
    badges.cycleStates =
        core::state::sequencer::stepNodeHasCycleStateSet(pattern, nodeId);
    if (badges.microSequence &&
        node->has(STEP_NODE_CHILD_SEQUENCE)) {
        const auto* sequence = graph->sequence(node->childSequenceId);
        if (sequence != nullptr) {
            badges.microLength = std::min<uint8_t>(
                sequence->length,
                StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP
            );
            for (uint8_t index = 0U;
                 index < badges.microLength;
                 ++index) {
                const auto* child = graph->stepNode(
                    static_cast<uint16_t>(sequence->firstStepNode + index)
                );
                const bool enabled = child != nullptr &&
                    (!child->has(STEP_NODE_ENABLED_OVERRIDE) ||
                     child->has(STEP_NODE_ENABLED_VALUE));
                if (enabled) {
                    badges.microActiveMask = static_cast<uint16_t>(
                        badges.microActiveMask |
                        static_cast<uint16_t>(1U << index)
                    );
                }
            }
        }
    }
    const bool localChord =
        node->has(STEP_NODE_CHORD_MODE) &&
        node->chordMode == StepSequencerChordMode::Local &&
        node->has(STEP_NODE_CHORD_LOCAL);
    if (localChord) {
        auto spec = node->chordSpec;
        spec.clamp();
        badges.chord = spec.voices() > 1;
        badges.chordVoiceCount = spec.voices();
        badges.chordSource = StepSequencerChordSource::Local;
    }
    return badges;
}

FLASHMEM void applyMicroSequencePlaybackProjection(
    StepContentBadgeProjection& badges,
    uint16_t gatePercent,
    uint8_t stepsPerBeat,
    uint32_t playheadTickOffset,
    const oc::note::sequencer::StepSequencerExpandedVariationTelemetry& telemetry,
    uint8_t rootStepIndex
) {
    badges.microCursorVisible = false;
    badges.microCursor = 0U;
    if (badges.microLength == 0U) return;

    const uint32_t ticksPerStep = std::max<uint32_t>(
        1U,
        oc::note::clock::PPQN / std::max<uint8_t>(1U, stepsPerBeat)
    );
    const uint32_t microSpan = std::max<uint32_t>(
        1U,
        (ticksPerStep * gatePercent) / 100U
    );
    if (playheadTickOffset < microSpan) {
        badges.microCursorVisible = true;
        badges.microCursor = std::min<uint8_t>(
            static_cast<uint8_t>(badges.microLength - 1U),
            static_cast<uint8_t>(
                (playheadTickOffset * badges.microLength) / microSpan
            )
        );
    }

    if (!telemetry.valid || telemetry.rootStepIndex != rootStepIndex) return;

    uint16_t resolvedMask = 0U;
    for (uint8_t noteIndex = 0U;
         noteIndex < telemetry.count;
         ++noteIndex) {
        const uint8_t microIndex = std::min<uint8_t>(
            static_cast<uint8_t>(badges.microLength - 1U),
            static_cast<uint8_t>(
                (telemetry.localTick[noteIndex] * badges.microLength) /
                microSpan
            )
        );
        resolvedMask = static_cast<uint16_t>(
            resolvedMask | static_cast<uint16_t>(1U << microIndex)
        );
    }
    badges.microActiveMask = resolvedMask;
}

FLASHMEM void applyCycleStatePlaybackProjection(
    StepContentBadgeProjection& badges,
    const core::state::sequencer::SequencerPatternState& pattern,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    uint32_t cycleIndex
) {
    badges.cycleCursorVisible = false;
    badges.cycleLength = 0U;
    badges.cycleCursor = 0U;
    badges.cycleActiveMask = 0U;
    if (!badges.cycleStates) return;

    const auto* graph = core::state::sequencer::graphView(pattern);
    const auto* node = graph != nullptr ? graph->stepNode(nodeId) : nullptr;
    if (graph == nullptr || node == nullptr ||
        !node->has(STEP_NODE_CYCLE_SET)) {
        return;
    }
    const auto* set = graph->cycleSet(node->cycleSetId);
    if (set == nullptr) return;

    badges.cycleLength = std::min<uint8_t>(
        set->length,
        StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET
    );
    if (badges.cycleLength == 0U) return;

    const auto sourceIndexForLogical = [set](uint8_t logicalIndex) {
        int source = static_cast<int>(logicalIndex) -
                     static_cast<int>(set->offset);
        const int length = static_cast<int>(set->length);
        source %= length;
        if (source < 0) source += length;
        return static_cast<uint8_t>(source);
    };
    for (uint8_t logicalIndex = 0U;
         logicalIndex < badges.cycleLength;
         ++logicalIndex) {
        const uint8_t sourceIndex = sourceIndexForLogical(logicalIndex);
        const auto* stateNode = graph->stepNode(
            static_cast<uint16_t>(set->firstStateNode + sourceIndex)
        );
        const bool active = stateNode != nullptr &&
            (!stateNode->has(STEP_NODE_ENABLED_OVERRIDE) ||
             stateNode->has(STEP_NODE_ENABLED_VALUE));
        if (active) {
            badges.cycleActiveMask = static_cast<uint16_t>(
                badges.cycleActiveMask |
                static_cast<uint16_t>(1U << logicalIndex)
            );
        }
    }
    badges.cycleCursorVisible = true;
    badges.cycleCursor = static_cast<uint8_t>(
        cycleIndex % badges.cycleLength
    );
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

    if (telemetry.noteBudgetExceeded &&
        nodeId == core::state::sequencer::rootStepNodeId(
                      telemetry.rootStepIndex
                  )) {
        badges.expansionLimitReached = true;
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

    if (found && telemetry.noteBudgetExceeded) {
        badges.expansionLimitReached = true;
    }
    if (!found || voiceCount <= 1) return false;
    badges.chord = true;
    badges.chordVoiceCount = voiceCount;
    badges.chordSource = source;
    return true;
}

}  // namespace core::ui::sequencer::grid
