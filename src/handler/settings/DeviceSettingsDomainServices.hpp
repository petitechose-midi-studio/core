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
    enum class ApplyStatus : uint8_t {
        APPLIED = 0,
        NO_CHANGE,
        INVALID_SELECTION,
        PERSISTENCE_FAILED,
    };

    struct ApplyResult {
        ApplyStatus status = ApplyStatus::INVALID_SELECTION;
        core::persistence::PersistenceWriteStatus persistenceStatus =
            core::persistence::PersistenceWriteStatus::INVALID_CONFIG;

        [[nodiscard]] bool success() const {
            return status == ApplyStatus::APPLIED ||
                   status == ApplyStatus::NO_CHANGE;
        }
        [[nodiscard]] bool changed() const {
            return status == ApplyStatus::APPLIED;
        }
    };

    struct StateRefs {
        core::state::MidiSyncState& midiSync;
        core::persistence::DeviceSettingsStore& store;
    };

    explicit DeviceSettingsDomainServices(StateRefs state);

    int currentChoiceIndex(uint8_t row) const;
    int choiceCount(uint8_t row) const;
    [[nodiscard]] ApplyResult applyMidiSyncMode(
        core::state::MidiSyncMode mode
    ) const;
    [[nodiscard]] ApplyResult applyChoice(uint8_t row, int choiceIndex) const;

private:
    core::state::MidiSyncState* midi_sync_ = nullptr;
    core::persistence::DeviceSettingsStore* store_ = nullptr;
};

}  // namespace core::handler
