#pragma once

#include "persistence/SequencerPersistencePayloads.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::persistence::sequencer_codec {

/**
 * Converts sequencer state to/from packed persistence payloads.
 *
 * Codec functions sanitize length, masks, MIDI values, focused step, page, and
 * step property on read/write so persisted bytes cannot put SequencerState into
 * an invalid runtime shape.
 */
void fillPatternPayload(const state::sequencer::SequencerPatternState& source, PatternPayload& out);
void applyPatternPayload(const PatternPayload& payload, state::sequencer::SequencerPatternState& target);

void fillWorkspacePayload(const state::sequencer::SequencerTrackBankState& trackBank,
                          const state::sequencer::SequencerState& active,
                          WorkspacePayload& out);
void applyWorkspacePayload(const WorkspacePayload& payload,
                           state::sequencer::SequencerTrackBankState& trackBank,
                           state::sequencer::SequencerState& active);

void fillSetPayload(const state::sequencer::SequencerTrackBankState& trackBank,
                    const state::sequencer::SequencerState& active,
                    SetPayload& out);
void applySetPayload(const SetPayload& payload,
                     state::sequencer::SequencerTrackBankState& trackBank,
                     state::sequencer::SequencerState& active);

}  // namespace core::persistence::sequencer_codec
