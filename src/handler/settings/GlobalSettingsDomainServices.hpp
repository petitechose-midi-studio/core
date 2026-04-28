#pragma once

#include <cstdint>

#include "state/CoreSettings.hpp"
#include "state/MidiSyncState.hpp"

namespace core::handler {

/**
 * Applies global settings choices to MidiSyncState and CoreSettings.
 *
 * The service owns row-to-choice mapping and persistence commits; UI handlers
 * only navigate/select rows.
 */
class GlobalSettingsDomainServices {
public:
    struct StateRefs {
        core::state::MidiSyncState& midiSync;
        core::state::CoreSettings& settings;
    };

    explicit GlobalSettingsDomainServices(StateRefs state);

    int currentChoiceIndex(uint8_t row) const;
    int choiceCount(uint8_t row) const;
    void applyChoice(uint8_t row, int choiceIndex) const;

private:
    core::state::MidiSyncState* midi_sync_ = nullptr;
    core::state::CoreSettings* settings_ = nullptr;
};

}  // namespace core::handler
