#pragma once

#include <cstdint>

#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::persistence::sequencer_codec {

// 5-byte bank header + 4 fixed 156-byte lane records. The explicit byte
// layout is stable across compiler padding and host/Teensy architectures.
inline constexpr uint16_t SEQUENCER_CC_LANE_BANK_RECORD_SIZE = 629;

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
