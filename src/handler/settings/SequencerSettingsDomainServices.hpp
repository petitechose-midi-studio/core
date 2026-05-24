#pragma once

#include <cstdint>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

class SequencerSettingsDomainServices {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& activeSequencer;
        core::state::sequencer::SequencerTrackBankState& trackBank;
    };

    explicit SequencerSettingsDomainServices(StateRefs state);

    int currentChoiceIndex(uint8_t row) const;
    int choiceCount(uint8_t row) const;
    void applyChoice(uint8_t row, int choiceIndex) const;

private:
    core::state::sequencer::SequencerState* active_sequencer_ = nullptr;
    core::state::sequencer::SequencerTrackBankState* track_bank_ = nullptr;
};

}  // namespace core::handler
