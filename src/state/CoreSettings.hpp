#pragma once

/**
 * @file CoreSettings.hpp
 * @brief Incremental persistence for pages + sync + shortcut mappings
 */

#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "MidiSyncState.hpp"
namespace core::state {

class CoreSettings {
public:
    static constexpr uint32_t VALUE_SAVE_DELAY_MS = 300;

    explicit CoreSettings(oc::interface::IStorage& backend);

    CoreSettings(const CoreSettings&) = delete;
    CoreSettings& operator=(const CoreSettings&) = delete;

    bool load(MidiSyncState& midiSync,
              uint16_t& sharedTrackEnabledMask,
              uint8_t& sharedTrackActive);
    bool saveAll(const MidiSyncState& midiSync,
                 uint16_t sharedTrackEnabledMask,
                 uint8_t sharedTrackActive);
    persistence::PersistenceWriteStatus saveAllStatus(const MidiSyncState& midiSync,
                                                      uint16_t sharedTrackEnabledMask,
                                                      uint8_t sharedTrackActive);

    bool saveMidiSyncMode(MidiSyncMode mode);
    bool saveMidiFollowTransport(bool followTransport);
    bool saveMidiAutoFallbackMs(uint16_t fallbackMs);
    bool saveMidiAutoLockClockCount(uint8_t lockCount);
    persistence::PersistenceWriteStatus saveMidiSyncModeStatus(MidiSyncMode mode);
    persistence::PersistenceWriteStatus saveMidiFollowTransportStatus(bool followTransport);
    persistence::PersistenceWriteStatus saveMidiAutoFallbackMsStatus(uint16_t fallbackMs);
    persistence::PersistenceWriteStatus saveMidiAutoLockClockCountStatus(uint8_t lockCount);
    bool saveSharedTrackState(uint16_t enabledMask, uint8_t activeTrack);
    persistence::PersistenceWriteStatus saveSharedTrackStateStatus(uint16_t enabledMask,
                                                                   uint8_t activeTrack);

    bool saveDataManagerMacroShortcutLeft(uint8_t command);
    bool saveDataManagerMacroShortcutRight(uint8_t command);
    bool saveDataManagerSeqShortcutLeft(uint8_t command);
    bool saveDataManagerSeqShortcutRight(uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerMacroShortcutLeftStatus(uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerMacroShortcutRightStatus(uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerSeqShortcutLeftStatus(uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerSeqShortcutRightStatus(uint8_t command);
    persistence::PersistenceWriteStatus saveDefaultDataManagerShortcutsStatus();
    bool loadDataManagerShortcuts(uint8_t& macroLeft,
                                  uint8_t& macroRight,
                                  uint8_t& seqLeft,
                                  uint8_t& seqRight);

    bool commit();
    bool factoryReset();
    persistence::PersistenceWriteStatus commitStatus();
    persistence::PersistenceWriteStatus factoryResetStatus();

private:
    bool readExact_(uint32_t address, uint8_t* buffer, size_t size);
    bool writeExact_(uint32_t address, const uint8_t* buffer, size_t size);
    persistence::PersistenceWriteStatus writeExactStatus_(uint32_t address,
                                                          const uint8_t* buffer,
                                                          size_t size);
    persistence::PersistenceWriteStatus saveDataManagerShortcutStatus_(uint32_t address,
                                                                       uint8_t command);
    persistence::PersistenceWriteStatus writeDefaultShortcutsStatus_();
    bool loadMidiSync_(MidiSyncState& midiSync);

    oc::interface::IStorage& backend_;
};

}  // namespace core::state
