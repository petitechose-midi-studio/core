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
    uint8_t chordVoiceCount = 1;
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
bool mergeExpandedTelemetryChordBadgeForNode(
    StepContentBadgeProjection& badges,
    const oc::note::sequencer::StepSequencerExpandedVariationTelemetry& telemetry,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    int16_t playheadStep,
    uint32_t tickOffset
);

}  // namespace core::ui::sequencer::grid
