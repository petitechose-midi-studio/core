#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::persistence::sequencer_codec {

/**
 * Packed on-storage sequencer payloads.
 *
 * These structs are copied byte-for-byte into PersistenceSlotFileStore slots.
 * Field order, packing, and static_asserts are part of the storage contract and
 * must move in lockstep with data-version changes.
 */
inline constexpr uint8_t PERSISTED_PATTERN_STEPS =
    state::sequencer::SequencerPatternState::MAX_STEPS;
inline constexpr uint8_t PERSISTED_TRACK_COUNT =
    state::sequencer::SequencerTrackBankState::TRACK_COUNT;

#pragma pack(push, 1)
struct PatternPayload {
    uint8_t length = state::sequencer::SequencerPatternState::DEFAULT_LENGTH;
    uint8_t stepsPerBeat = state::sequencer::SequencerPatternState::DEFAULT_STEPS_PER_BEAT;
    uint8_t midiChannel = state::sequencer::SequencerPatternState::DEFAULT_MIDI_CHANNEL_0BASED;
    uint8_t pitchEditMode = static_cast<uint8_t>(
        state::sequencer::SequencerPitchEditMode::CHROMATIC
    );
    uint8_t variationPitchSemitones = 0;
    uint8_t variationVelocity = 0;
    uint8_t variationGatePercent = 0;
    uint8_t variationNudge = 0;
    int8_t swingOffsetPercent = 0;
    int8_t patternNudgePercent = 0;
    uint8_t scalePolicy = static_cast<uint8_t>(
        state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );
    uint8_t scaleRoot = 0;
    uint8_t scaleType = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerScaleType::Chromatic
    );
    uint8_t scaleConstraintMode = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerScaleConstraintMode::Free
    );
    uint64_t enabledMaskLow = 0;
    uint64_t enabledMaskHigh = 0;
    std::array<uint8_t, PERSISTED_PATTERN_STEPS> note{};
    std::array<uint8_t, PERSISTED_PATTERN_STEPS> velocity{};
    std::array<uint16_t, PERSISTED_PATTERN_STEPS> gate{};
    std::array<int8_t, PERSISTED_PATTERN_STEPS> nudge{};
    std::array<uint8_t, PERSISTED_PATTERN_STEPS> probability{};
};

struct ProjectSequencerTrackPayload {
    PatternPayload pattern{};
    uint8_t page = 0;
    uint8_t focusedStep = 0;
    uint8_t activeStepProperty = static_cast<uint8_t>(state::sequencer::StepProperty::NOTE);
    uint8_t reserved0 = 0;
};

struct ProjectSequencerPayload {
    uint8_t projectScaleRoot = 0;
    uint8_t projectScaleType = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerScaleType::Chromatic
    );
    uint8_t projectScaleConstraintMode = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerScaleConstraintMode::Free
    );
    uint8_t reserved0 = 0;
    std::array<ProjectSequencerTrackPayload, PERSISTED_TRACK_COUNT> tracks{};
};

struct SetPayload {
    uint8_t trackCount = PERSISTED_TRACK_COUNT;
    uint8_t activeTrack = 0;
    uint16_t enabledMask = 0x0001;
    uint8_t projectScaleRoot = 0;
    uint8_t projectScaleType = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerScaleType::Chromatic
    );
    uint8_t projectScaleConstraintMode = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerScaleConstraintMode::Free
    );
    uint8_t reserved0 = 0;
    std::array<PatternPayload, PERSISTED_TRACK_COUNT> tracks{};
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<PatternPayload>,
              "PatternPayload must be trivially copyable");
static_assert(std::is_trivially_copyable_v<ProjectSequencerTrackPayload>,
              "ProjectSequencerTrackPayload must be trivially copyable");
static_assert(std::is_trivially_copyable_v<ProjectSequencerPayload>,
              "ProjectSequencerPayload must be trivially copyable");
static_assert(std::is_trivially_copyable_v<SetPayload>,
              "SetPayload must be trivially copyable");

inline constexpr uint16_t PATTERN_PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(PatternPayload));
inline constexpr uint16_t PROJECT_SEQUENCER_PAYLOAD_SIZE =
    static_cast<uint16_t>(sizeof(ProjectSequencerPayload));
inline constexpr uint16_t SET_PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(SetPayload));

}  // namespace core::persistence::sequencer_codec
