#pragma once

#include "persistence/SequencerPersistencePayloads.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::persistence::sequencer_codec {

/**
 * Converts sequencer state to/from byte-oriented persistence payloads.
 *
 * Codec functions sanitize length, masks, MIDI values, focused step, page, and
 * step property on read/write so persisted bytes cannot put SequencerState into
 * an invalid runtime shape.
 */
bool fillPatternPayload(const state::sequencer::SequencerPatternState& source,
                        uint8_t* out,
                        uint16_t capacity);
bool applyPatternPayload(const uint8_t* data,
                         uint16_t size,
                         state::sequencer::SequencerPatternState& target);

bool fillProjectSequencerPayload(const state::sequencer::SequencerTrackBankState& trackBank,
                                 const state::sequencer::SequencerState& active,
                                 uint8_t* out,
                                 uint16_t capacity);
bool applyProjectSequencerPayload(const uint8_t* data,
                                  uint16_t size,
                                  state::sequencer::SequencerTrackBankState& trackBank,
                                  state::sequencer::SequencerState& active);

bool fillSetPayload(const state::sequencer::SequencerTrackBankState& trackBank,
                    const state::sequencer::SequencerState& active,
                    uint8_t* out,
                    uint16_t capacity);
bool applySetPayload(const uint8_t* data,
                     uint16_t size,
                     state::sequencer::SequencerTrackBankState& trackBank,
                     state::sequencer::SequencerState& active);

}  // namespace core::persistence::sequencer_codec
