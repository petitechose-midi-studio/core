#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerClipState.hpp"
#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

struct SequencerPatternSnapshot : oc::note::sequencer::StepSequencerStepData {
    uint8_t length = SequencerPatternState::DEFAULT_LENGTH;
    uint8_t stepsPerBeat = SequencerPatternState::DEFAULT_STEPS_PER_BEAT;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    uint32_t stepDataRevision = 0;
    uint32_t patternVariationRevision = 0;
    uint32_t patternScaleRevision = 0;
    uint32_t patternTimingRevision = 0;
    uint32_t graphRevision = 0;
    int8_t swingOffsetPercent = 0;
    int8_t patternNudgePercent = 0;
    uint8_t effectiveSwingPercent = 0;
    oc::note::sequencer::StepSequencerVariationRanges variationRanges{};
    SequencerPatternScalePolicy scalePolicy = SequencerPatternScalePolicy::INHERIT_PROJECT;
    oc::note::sequencer::StepSequencerScaleSettings scaleOverride{};
    SequencerPitchEditMode pitchEditMode = SequencerPitchEditMode::FOLLOW_SCALE;
    oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings{};
};

struct SequencerTrackBankSnapshot {
    uint8_t activeTrack = 0;
    uint16_t enabledMask = 0x0001;
    uint32_t projectScaleRevision = 0;
    uint8_t projectSwingPercent = 0;
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings{};
    std::array<SequencerPatternSnapshot, SequencerTrackBankState::TRACK_COUNT> tracks{};
    std::array<SequencerClipState, SequencerTrackBankState::TRACK_COUNT> clips{};
};

}  // namespace core::state::sequencer
