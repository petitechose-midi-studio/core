#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "MidiSyncState.hpp"
#include "persistence/PersistenceSlotFileStore.hpp"

namespace core::state::core_settings {

/**
 * Low-level codec for the compact CoreSettings layout.
 *
 * It owns exact byte reads/writes for MIDI sync, shared track state, and Data
 * Manager shortcuts. Callers own timing and save policy.
 */
bool readExact(oc::interface::IStorage& backend, uint32_t address, uint8_t* buffer, size_t size);
bool writeExact(oc::interface::IStorage& backend, uint32_t address, const uint8_t* buffer, size_t size);
persistence::PersistenceWriteStatus writeExactStatus(oc::interface::IStorage& backend,
                                                     uint32_t address,
                                                     const uint8_t* buffer,
                                                     size_t size);

persistence::PersistenceWriteStatus saveAll(oc::interface::IStorage& backend,
                                            const MidiSyncState& midiSync,
                                            uint16_t sharedTrackEnabledMask,
                                            uint8_t sharedTrackActive);

bool loadMidiSync(oc::interface::IStorage& backend, MidiSyncState& midiSync);
bool loadSharedTrackState(oc::interface::IStorage& backend,
                          uint16_t& sharedTrackEnabledMask,
                          uint8_t& sharedTrackActive);
bool loadDataManagerShortcuts(oc::interface::IStorage& backend,
                              uint8_t& macroLeft,
                              uint8_t& macroRight,
                              uint8_t& seqLeft,
                              uint8_t& seqRight);

persistence::PersistenceWriteStatus writeDefaultShortcuts(oc::interface::IStorage& backend);

}  // namespace core::state::core_settings
