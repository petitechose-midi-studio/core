#pragma once

#include <array>
#include <cstdint>

#include "persistence/SequencerPersistencePayloads.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::persistence::sequencer_codec {

inline constexpr uint16_t MAX_ENVELOPE_PAYLOAD_SIZE = 65520;

struct EnvelopeBuffer {
    std::array<uint8_t, MAX_ENVELOPE_PAYLOAD_SIZE> bytes{};
};

struct EnvelopeEncodeResult {
    bool ok = false;
    uint16_t size = 0;
};

EnvelopeEncodeResult fillPatternEnvelope(
    const state::sequencer::SequencerPatternState& source,
    uint8_t* out,
    uint16_t capacity
);

bool applyPatternEnvelope(const uint8_t* data,
                          uint16_t size,
                          state::sequencer::SequencerPatternState& target);

EnvelopeEncodeResult fillProjectSequencerEnvelope(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& active,
    uint8_t* out,
    uint16_t capacity
);

bool applyProjectSequencerEnvelope(const uint8_t* data,
                                   uint16_t size,
                                   state::sequencer::SequencerTrackBankState& trackBank,
                                   state::sequencer::SequencerState& active);

EnvelopeEncodeResult fillSetEnvelope(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& active,
    uint8_t* out,
    uint16_t capacity
);

bool applySetEnvelope(const uint8_t* data,
                      uint16_t size,
                      state::sequencer::SequencerTrackBankState& trackBank,
                      state::sequencer::SequencerState& active);

}  // namespace core::persistence::sequencer_codec
