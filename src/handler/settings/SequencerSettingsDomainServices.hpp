#pragma once

#include <cstdint>

#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

class SequencerSettingsDomainServices {
public:
    struct StateRefs {
        core::state::sequencer::SequencerTrackBankState& trackBank;
    };

    explicit SequencerSettingsDomainServices(StateRefs state);

    int currentChoiceIndex(uint8_t row) const;
    int choiceCount(uint8_t row) const;

private:
    core::state::sequencer::SequencerTrackBankState* track_bank_ = nullptr;
};

}  // namespace core::handler
