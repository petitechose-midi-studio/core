#pragma once

/**
 * @file DeviceSettingsStore.hpp
 * @brief Exact-current store for durable device MIDI-sync settings
 */

#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceStatus.hpp"
#include "state/MidiSyncState.hpp"

namespace core::persistence {

class DeviceSettingsStore {
public:
    explicit DeviceSettingsStore(oc::interface::IStorage& backend);

    DeviceSettingsStore(const DeviceSettingsStore&) = delete;
    DeviceSettingsStore& operator=(const DeviceSettingsStore&) = delete;

    bool load(state::MidiSyncState& midiSync);
    bool saveAll(const state::MidiSyncState& midiSync);
    PersistenceWriteStatus saveAllStatus(const state::MidiSyncState& midiSync);

    bool saveMidiSyncMode(state::MidiSyncMode mode);
    bool saveMidiFollowTransport(bool followTransport);
    bool saveMidiAutoFallbackMs(uint16_t fallbackMs);
    bool saveMidiAutoLockClockCount(uint8_t lockCount);
    PersistenceWriteStatus saveMidiSyncModeStatus(state::MidiSyncMode mode);
    PersistenceWriteStatus saveMidiFollowTransportStatus(bool followTransport);
    PersistenceWriteStatus saveMidiAutoFallbackMsStatus(uint16_t fallbackMs);
    PersistenceWriteStatus saveMidiAutoLockClockCountStatus(uint8_t lockCount);

    bool commit();
    bool factoryReset();
    PersistenceWriteStatus commitStatus();
    PersistenceWriteStatus factoryResetStatus();

private:
    PersistenceWriteStatus currentFormatStatus_();
    bool loadMidiSync_(state::MidiSyncState& midiSync);

    oc::interface::IStorage& backend_;
};

}  // namespace core::persistence
