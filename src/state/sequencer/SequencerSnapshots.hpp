#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

struct SequencerPatternSnapshot {
    uint8_t length = SequencerPatternState::DEFAULT_LENGTH;
    uint8_t stepsPerBeat = SequencerPatternState::DEFAULT_STEPS_PER_BEAT;
    uint8_t midiChannel = SequencerPatternState::DEFAULT_MIDI_CHANNEL_0BASED;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    uint32_t stepDataRevision = 0;
    uint32_t patternVariationRevision = 0;
    uint32_t patternScaleRevision = 0;
    oc::note::sequencer::StepSequencerVariationRanges variationRanges{};
    SequencerPatternScalePolicy scalePolicy = SequencerPatternScalePolicy::INHERIT_PROJECT;
    oc::note::sequencer::StepSequencerScaleSettings scaleOverride{};
    SequencerPitchEditMode pitchEditMode = SequencerPitchEditMode::CHROMATIC;
    oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings{};
    std::array<uint8_t, SequencerPatternState::MAX_STEPS> note{};
    std::array<uint8_t, SequencerPatternState::MAX_STEPS> velocity{};
    std::array<uint16_t, SequencerPatternState::MAX_STEPS> gate{};
    std::array<int8_t, SequencerPatternState::MAX_STEPS> nudge{};
    std::array<uint8_t, SequencerPatternState::MAX_STEPS> probability{};
};

struct SequencerTrackBankSnapshot {
    uint8_t activeTrack = 0;
    uint16_t enabledMask = 0x0001;
    uint32_t projectScaleRevision = 0;
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings{};
    std::array<SequencerPatternSnapshot, SequencerTrackBankState::TRACK_COUNT> tracks{};
};

}  // namespace core::state::sequencer
