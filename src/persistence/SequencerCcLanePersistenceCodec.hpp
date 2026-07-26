#pragma once

#include <cstdint>

#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::persistence::sequencer_codec {

// The explicit layout is stable across compiler padding and host/Teensy
// architectures.
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
