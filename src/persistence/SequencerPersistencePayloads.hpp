#pragma once

#include <cstdint>

#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::persistence::sequencer_codec {

inline constexpr uint8_t PERSISTED_PATTERN_STEPS =
    state::sequencer::SequencerPatternState::MAX_STEPS;
inline constexpr uint8_t PERSISTED_TRACK_COUNT =
    state::sequencer::SequencerTrackBankState::TRACK_COUNT;

inline constexpr uint16_t PATTERN_HEADER_PAYLOAD_SIZE = 30;
inline constexpr uint16_t PATTERN_PAYLOAD_SIZE =
    PATTERN_HEADER_PAYLOAD_SIZE +
    static_cast<uint16_t>(PERSISTED_PATTERN_STEPS) +       // note
    static_cast<uint16_t>(PERSISTED_PATTERN_STEPS) +       // velocity
    static_cast<uint16_t>(PERSISTED_PATTERN_STEPS * 2U) +  // gate u16
    static_cast<uint16_t>(PERSISTED_PATTERN_STEPS) +       // nudge
    static_cast<uint16_t>(PERSISTED_PATTERN_STEPS);        // probability
inline constexpr uint16_t PROJECT_SEQUENCER_TRACK_PAYLOAD_SIZE =
    PATTERN_PAYLOAD_SIZE + 4U;
inline constexpr uint16_t PROJECT_SEQUENCER_PAYLOAD_SIZE =
    9U + static_cast<uint16_t>(PERSISTED_TRACK_COUNT * PROJECT_SEQUENCER_TRACK_PAYLOAD_SIZE);
inline constexpr uint16_t SET_PAYLOAD_SIZE =
    10U + static_cast<uint16_t>(PERSISTED_TRACK_COUNT * PATTERN_PAYLOAD_SIZE);

static_assert(PATTERN_PAYLOAD_SIZE == 798, "Unexpected pattern payload size");
static_assert(PROJECT_SEQUENCER_TRACK_PAYLOAD_SIZE == 802,
              "Unexpected project sequencer track payload size");
static_assert(PROJECT_SEQUENCER_PAYLOAD_SIZE == 12841,
              "Unexpected project sequencer payload size");
static_assert(SET_PAYLOAD_SIZE == 12778, "Unexpected set payload size");

}  // namespace core::persistence::sequencer_codec
