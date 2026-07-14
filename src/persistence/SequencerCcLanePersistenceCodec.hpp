#pragma once

#include <cstdint>

#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::persistence::sequencer_codec {

// The explicit layouts are stable across compiler padding and host/Teensy
// architectures. V1 had Hold only, V2 packed four shapes on 2 bits, and V3
// packs five shapes on 3 bits.
inline constexpr uint16_t LEGACY_V1_SEQUENCER_CC_LANE_BANK_RECORD_SIZE = 629;
inline constexpr uint16_t LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE = 757;
inline constexpr uint16_t LEGACY_SEQUENCER_CC_LANE_BANK_RECORD_SIZE =
    LEGACY_V1_SEQUENCER_CC_LANE_BANK_RECORD_SIZE;
inline constexpr uint16_t SEQUENCER_CC_LANE_BANK_RECORD_SIZE = 821;

bool encodeSequencerCcLaneBankRecord(
    const core::state::sequencer::SequencerCcLaneBank& source,
    uint8_t* out,
    uint16_t size
);

bool decodeSequencerCcLaneBankRecord(
    const uint8_t* data,
    uint16_t size,
    core::state::sequencer::SequencerCcLaneBank& out
);

}  // namespace core::persistence::sequencer_codec
