#pragma once

#include <cstdint>
#include <type_traits>

#include <oc/note/sequencer/StepSequencerChord.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

namespace core::state::sequencer {

#pragma pack(push, 1)
struct SequencerGraphSequenceRecord {
    uint8_t kind = 0;
    uint16_t firstStepNode = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint8_t length = 0;
    int8_t offset = 0;
};

struct SequencerGraphStepNodeRecord {
    uint16_t flags = 0;
    int8_t noteOffset = 0;
    int16_t velocityOffset = 0;
    int16_t gateOffset = 0;
    int8_t nudgeOffset = 0;
    int16_t probabilityOffset = 0;
    uint16_t childSequenceId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint16_t cycleSetId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint8_t localVariationPitchSemitones = 0;
    uint8_t localVariationVelocity = 0;
    uint8_t localVariationGatePercent = 0;
    uint8_t localVariationNudge = 0;
    uint8_t chordMode = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerChordMode::Single
    );
    uint8_t chordVoiceCount = 3;
    uint8_t chordColor = 0;
    uint8_t chordVariant = 0;
    uint8_t chordSpread = 0;
    int8_t chordStrum = 0;
    int8_t chordVelocityCurve = 0;
};

struct SequencerGraphCycleSetRecord {
    uint16_t firstStateNode = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint8_t length = 0;
    int8_t offset = 0;
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<SequencerGraphSequenceRecord>,
              "SequencerGraphSequenceRecord must be trivially copyable");
static_assert(std::is_trivially_copyable_v<SequencerGraphStepNodeRecord>,
              "SequencerGraphStepNodeRecord must be trivially copyable");
static_assert(std::is_trivially_copyable_v<SequencerGraphCycleSetRecord>,
              "SequencerGraphCycleSetRecord must be trivially copyable");
static_assert(sizeof(SequencerGraphSequenceRecord) == 5,
              "Unexpected SequencerGraphSequenceRecord size");
static_assert(sizeof(SequencerGraphStepNodeRecord) == 25,
              "Unexpected SequencerGraphStepNodeRecord size");
static_assert(sizeof(SequencerGraphCycleSetRecord) == 4,
              "Unexpected SequencerGraphCycleSetRecord size");

}  // namespace core::state::sequencer
