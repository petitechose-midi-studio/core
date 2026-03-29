#pragma once

#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "MidiSyncState.hpp"
#include "persistence/PersistenceSlotFileStore.hpp"
#include "state/CoreSettingsLayout.hpp"
#include "macro/MacroPagesState.hpp"

namespace core::state::core_settings {

bool readExact(oc::interface::IStorage& backend, uint32_t address, uint8_t* buffer, size_t size);
bool writeExact(oc::interface::IStorage& backend, uint32_t address, const uint8_t* buffer, size_t size);
persistence::PersistenceWriteStatus writeExactStatus(oc::interface::IStorage& backend,
                                                     uint32_t address,
                                                     const uint8_t* buffer,
                                                     size_t size);

persistence::PersistenceWriteStatus saveAll(oc::interface::IStorage& backend,
                                            const macro::MacroPagesState& pages,
                                            const MidiSyncState& midiSync);

bool loadPages(oc::interface::IStorage& backend, macro::MacroPagesState& pages);
bool loadMidiSync(oc::interface::IStorage& backend, MidiSyncState& midiSync);
bool loadDataManagerShortcuts(oc::interface::IStorage& backend,
                              uint8_t& macroLeft,
                              uint8_t& macroRight,
                              uint8_t& seqLeft,
                              uint8_t& seqRight);

persistence::PersistenceWriteStatus writeDefaultShortcuts(oc::interface::IStorage& backend);

}  // namespace core::state::core_settings
