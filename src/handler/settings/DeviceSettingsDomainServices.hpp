#pragma once

#include <cstdint>

#include "persistence/DeviceSettingsStore.hpp"
#include "state/MidiSyncState.hpp"

namespace core::handler {

/**
 * Applies device settings choices to MidiSyncState and DeviceSettingsStore.
 *
 * The service owns row-to-choice mapping and persistence commits; UI handlers
 * only navigate/select rows.
 */
class DeviceSettingsDomainServices {
public:
    struct StateRefs {
        core::state::MidiSyncState& midiSync;
        core::persistence::DeviceSettingsStore& store;
    };

    explicit DeviceSettingsDomainServices(StateRefs state);

    int currentChoiceIndex(uint8_t row) const;
    int choiceCount(uint8_t row) const;
    void applyChoice(uint8_t row, int choiceIndex) const;

private:
    core::state::MidiSyncState* midi_sync_ = nullptr;
    core::persistence::DeviceSettingsStore* store_ = nullptr;
};

}  // namespace core::handler
