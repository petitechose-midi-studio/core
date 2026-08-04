#pragma once

#include "persistence/SequencerPersistencePayloads.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::persistence::sequencer_codec {

/**
 * Converts sequencer state to/from byte-oriented persistence payloads.
 *
 * This is a strict current-format boundary. Encoders reject non-canonical
 * runtime state and decoders reject malformed bytes; neither side repairs,
 * clamps, defaults, or migrates values.
 */
bool fillPatternPayload(const state::sequencer::SequencerPatternState& source,
                        uint8_t* out,
                        uint16_t capacity);
bool applyPatternPayload(const uint8_t* data,
                         uint16_t size,
                         state::sequencer::SequencerPatternState& target);

bool fillProjectSequencerPayload(
    const state::sequencer::SequencerTrackBankSnapshot& snapshot,
    uint8_t focusedStep,
    state::sequencer::StepProperty activeStepProperty,
    uint8_t* out,
    uint16_t capacity
);
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
