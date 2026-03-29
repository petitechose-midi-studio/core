#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include <oc/note/sequencer/StepSequencerState.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::persistence::sequencer_codec {

#pragma pack(push, 1)
struct PatternPayloadV1 {
    uint8_t length = oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
    uint8_t stepsPerBeat = oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
    uint8_t midiChannel = oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED;
    uint8_t reserved0 = 0;
    uint64_t enabledMask = 0;
    std::array<uint8_t, state::sequencer::SequencerState::MAX_STEPS> note{};
    std::array<uint8_t, state::sequencer::SequencerState::MAX_STEPS> velocity{};
    std::array<uint16_t, state::sequencer::SequencerState::MAX_STEPS> gate{};
    std::array<int8_t, state::sequencer::SequencerState::MAX_STEPS> nudge{};
    std::array<uint8_t, state::sequencer::SequencerState::MAX_STEPS> probability{};
};

struct WorkspaceTrackPayloadV2 {
    PatternPayloadV1 pattern{};
    uint8_t page = 0;
    uint8_t focusedStep = 0;
    uint8_t activeStepProperty = static_cast<uint8_t>(state::sequencer::StepProperty::NOTE);
    uint8_t reserved0 = 0;
};

struct WorkspacePayloadV2 {
    uint8_t trackCount = state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t activeTrack = 0;
    uint8_t enabledMask = 0x01;
    uint8_t reserved0 = 0;
    std::array<WorkspaceTrackPayloadV2, state::sequencer::SequencerTrackBankState::TRACK_COUNT>
        tracks{};
};

struct SetPayloadV2 {
    uint8_t trackCount = state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t activeTrack = 0;
    uint8_t enabledMask = 0x01;
    uint8_t reserved0 = 0;
    std::array<PatternPayloadV1, state::sequencer::SequencerTrackBankState::TRACK_COUNT> tracks{};
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable<PatternPayloadV1>::value,
              "PatternPayloadV1 must be trivially copyable");
static_assert(std::is_trivially_copyable<WorkspaceTrackPayloadV2>::value,
              "WorkspaceTrackPayloadV2 must be trivially copyable");
static_assert(std::is_trivially_copyable<WorkspacePayloadV2>::value,
              "WorkspacePayloadV2 must be trivially copyable");
static_assert(std::is_trivially_copyable<SetPayloadV2>::value,
              "SetPayloadV2 must be trivially copyable");

inline constexpr uint16_t PATTERN_PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(PatternPayloadV1));
inline constexpr uint16_t WORKSPACE_PAYLOAD_SIZE =
    static_cast<uint16_t>(sizeof(WorkspacePayloadV2));
inline constexpr uint16_t SET_PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(SetPayloadV2));

}  // namespace core::persistence::sequencer_codec
