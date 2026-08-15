#pragma once

#include <cstdint>

#include "state/sequencer/DrumPatternState.hpp"

namespace core::persistence::sequencer_codec {

/** Stable, padding-independent record for one authored Drum Track. */
inline constexpr uint16_t DRUM_TRACK_RECORD_SIZE = 11036U;

bool encodeDrumTrackRecord(
    const core::state::sequencer::DrumTrackState& source,
    uint8_t* out,
    uint16_t size
);

bool decodeDrumTrackRecord(
    const uint8_t* data,
    uint16_t size,
    core::state::sequencer::DrumTrackState& out
);

}  // namespace core::persistence::sequencer_codec
