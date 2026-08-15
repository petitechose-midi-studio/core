#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceStatus.hpp"
#include "state/MidiNoteDisplayState.hpp"
#include "state/MidiSyncState.hpp"

namespace core::persistence::device_settings {

/**
 * Low-level codec for the compact DeviceSettingsStore layout.
 *
 * It accepts the exact current controller-settings representation only.
 * Decode validates the complete payload before publishing any signal.
 */
bool readMagic(oc::interface::IStorage& backend, uint32_t& magic);
bool readVersion(oc::interface::IStorage& backend, uint8_t& version);

PersistenceWriteStatus saveAll(oc::interface::IStorage& backend,
                               const state::MidiSyncState& midiSync,
                               const state::MidiNoteDisplayState& noteDisplay);
PersistenceWriteStatus stageMidiSyncMode(oc::interface::IStorage& backend,
                                         state::MidiSyncMode mode);
PersistenceWriteStatus stageMidiFollowTransport(oc::interface::IStorage& backend,
                                                bool followTransport);
PersistenceWriteStatus stageMidiAutoFallbackMs(oc::interface::IStorage& backend,
                                               uint16_t fallbackMs);
PersistenceWriteStatus stageMidiAutoLockClockCount(oc::interface::IStorage& backend,
                                                   uint8_t lockCount);
PersistenceWriteStatus stageNoteOctaveConvention(
    oc::interface::IStorage& backend,
    core::midi::NoteOctaveConvention convention
);

bool validMidiSyncMode(state::MidiSyncMode mode);
bool validMidiSyncAutoFallbackMs(uint16_t fallbackMs);
bool validMidiSyncAutoLockClockCount(uint8_t lockClocks);
bool validMidiSyncState(const state::MidiSyncState& midiSync);
bool load(
    oc::interface::IStorage& backend,
    state::MidiSyncState& midiSync,
    state::MidiNoteDisplayState& noteDisplay
);

}  // namespace core::persistence::device_settings
