#pragma once

#include "persistence/SequencerPersistencePayloads.hpp"

namespace core::persistence::sequencer_codec {

void fillPatternPayload(const state::sequencer::SequencerState& source, PatternPayload& out);
void applyPatternPayload(const PatternPayload& payload, state::sequencer::SequencerState& target);

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
