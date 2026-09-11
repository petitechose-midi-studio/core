#pragma once

#include <oc/note/sequencer/StepSequencerState.hpp>

#include "SequencerScaleState.hpp"

namespace core::state::sequencer {

/** Musical values shared by authored documents and detached snapshots.
 * No observers, runtime cursors or payload owners belong in this value type.
 */
struct SequencerPatternData : oc::note::sequencer::StepSequencerStepData {
    uint8_t length = oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
    uint8_t stepsPerBeat = oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    uint32_t stepDataRevision = 0;
    uint32_t patternVariationRevision = 0;
    uint32_t patternScaleRevision = 0;
    uint32_t patternTimingRevision = 0;
    uint32_t graphRevision = 0;
    int8_t swingOffsetPercent = 0;
    int8_t patternNudgePercent = 0;
    oc::note::sequencer::StepSequencerVariationRanges variationRanges{};
    SequencerPatternScalePolicy scalePolicy = SequencerPatternScalePolicy::INHERIT_PROJECT;
    oc::note::sequencer::StepSequencerScaleSettings scaleOverride{};
    SequencerPitchEditMode pitchEditMode = SequencerPitchEditMode::FOLLOW_SCALE;
};

enum class SequencerPatternChange : uint8_t {
    LENGTH, ENABLED, STEPS, GRAPH, CC, VARIATION, SCALE, TIMING,
};

class SequencerPatternObservation;

} // namespace core::state::sequencer
