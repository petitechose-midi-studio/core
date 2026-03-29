#pragma once

#include "persistence/SequencerPersistencePayloads.hpp"

namespace core::persistence::sequencer_codec {

void fillPatternPayload(const state::sequencer::SequencerState& source, PatternPayloadV1& out);
void applyPatternPayload(const PatternPayloadV1& payload, state::sequencer::SequencerState& target);

void fillWorkspacePayload(const state::sequencer::SequencerTrackBankState& trackBank,
                          const state::sequencer::SequencerState& active,
                          WorkspacePayloadV2& out);
void applyWorkspacePayload(const WorkspacePayloadV2& payload,
                           state::sequencer::SequencerTrackBankState& trackBank,
                           state::sequencer::SequencerState& active);

void fillSetPayload(const state::sequencer::SequencerTrackBankState& trackBank,
                    const state::sequencer::SequencerState& active,
                    SetPayloadV2& out);
void applySetPayload(const SetPayloadV2& payload,
                     state::sequencer::SequencerTrackBankState& trackBank,
                     state::sequencer::SequencerState& active);

}  // namespace core::persistence::sequencer_codec
