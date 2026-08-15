#pragma once

/**
 * @file DeviceSettingsStore.hpp
 * @brief Exact-current store for durable controller settings
 */

#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceStatus.hpp"
#include "state/MidiNoteDisplayState.hpp"
#include "state/MidiSyncState.hpp"

namespace core::persistence {

class DeviceSettingsStore {
public:
    explicit DeviceSettingsStore(oc::interface::IStorage& backend);

    DeviceSettingsStore(const DeviceSettingsStore&) = delete;
    DeviceSettingsStore& operator=(const DeviceSettingsStore&) = delete;

    bool load(
        state::MidiSyncState& midiSync,
        state::MidiNoteDisplayState& noteDisplay
    );
    PersistenceWriteStatus saveAllStatus(
        const state::MidiSyncState& midiSync,
        const state::MidiNoteDisplayState& noteDisplay
    );
    /**
     * Persist the RAM-authoritative settings only when durable content differs.
     * Unsupported or malformed layouts are replaced; an exact current layout
     * is left untouched so a later product-storage retry cannot amplify writes.
     */
    PersistenceWriteStatus reconcileAllStatus(
        const state::MidiSyncState& midiSync,
        const state::MidiNoteDisplayState& noteDisplay
    );

    PersistenceWriteStatus saveMidiSyncModeStatus(state::MidiSyncMode mode);
    PersistenceWriteStatus saveMidiFollowTransportStatus(bool followTransport);
    PersistenceWriteStatus saveMidiAutoFallbackMsStatus(uint16_t fallbackMs);
    PersistenceWriteStatus saveMidiAutoLockClockCountStatus(uint8_t lockCount);
    PersistenceWriteStatus saveNoteOctaveConventionStatus(
        core::midi::NoteOctaveConvention convention
    );

    PersistenceWriteStatus commitStatus();
    PersistenceWriteStatus factoryResetStatus();

private:
    PersistenceWriteStatus currentFormatStatus_();

    oc::interface::IStorage& backend_;
};

}  // namespace core::persistence
