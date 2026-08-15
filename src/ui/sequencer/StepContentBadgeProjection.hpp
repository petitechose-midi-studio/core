#pragma once

#include <cstdint>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPatternState.hpp"

namespace oc::note::sequencer {
enum class StepSequencerChordSource : uint8_t;
struct StepSequencerExpandedVariationTelemetry;
}  // namespace oc::note::sequencer

namespace core::ui::sequencer::grid {

struct StepContentBadgeProjection {
    bool microSequence = false;
    bool cycleStates = false;
    bool chord = false;
    bool expansionLimitReached = false;
    bool microCursorVisible = false;
    bool cycleCursorVisible = false;
    uint8_t chordVoiceCount = 1;
    uint8_t microLength = 0U;
    uint8_t microCursor = 0U;
    uint8_t cycleLength = 0U;
    uint8_t cycleCursor = 0U;
    uint16_t microActiveMask = 0U;
    uint16_t cycleActiveMask = 0U;
    oc::note::sequencer::StepSequencerChordSource chordSource =
        oc::note::sequencer::StepSequencerChordSource::Single;
};

StepContentBadgeProjection buildStepContentBadgeProjection(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t absoluteStep
);
StepContentBadgeProjection buildStepContentBadgeProjectionForNode(
    const core::state::sequencer::SequencerPatternState& pattern,
    core::state::sequencer::SequencerGraphNodeId nodeId
);
void applyMicroSequencePlaybackProjection(
    StepContentBadgeProjection& badges,
    uint16_t gatePercent,
    uint8_t stepsPerBeat,
    uint32_t playheadTickOffset,
    const oc::note::sequencer::StepSequencerExpandedVariationTelemetry& telemetry,
    uint8_t rootStepIndex
);
void applyCycleStatePlaybackProjection(
    StepContentBadgeProjection& badges,
    const core::state::sequencer::SequencerPatternState& pattern,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    uint32_t cycleIndex
);
bool mergeExpandedTelemetryChordBadgeForNode(
    StepContentBadgeProjection& badges,
    const oc::note::sequencer::StepSequencerExpandedVariationTelemetry& telemetry,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    int16_t playheadStep,
    uint32_t tickOffset
);

}  // namespace core::ui::sequencer::grid
