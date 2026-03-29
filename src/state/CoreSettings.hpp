#pragma once

/**
 * @file CoreSettings.hpp
 * @brief Incremental persistence for pages + sync + shortcut mappings
 */

#include <cstddef>
#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "DataManagerCatalog.hpp"
#include "MidiSyncState.hpp"
#include "CoreSettingsLayout.hpp"
#include "macro/MacroPagesState.hpp"

namespace core::state {

namespace StorageLayout = core::state::core_settings::layout;

class CoreSettings {
public:
    static constexpr uint32_t VALUE_SAVE_DELAY_MS = 300;

    explicit CoreSettings(oc::interface::IStorage& backend);

    CoreSettings(const CoreSettings&) = delete;
    CoreSettings& operator=(const CoreSettings&) = delete;

    bool load(macro::MacroPagesState& pages, MidiSyncState& midiSync);
    bool saveAll(const macro::MacroPagesState& pages, const MidiSyncState& midiSync);
    persistence::PersistenceWriteStatus saveAllStatus(const macro::MacroPagesState& pages,
                                                      const MidiSyncState& midiSync);

    bool saveMidiSyncMode(MidiSyncMode mode);
    bool saveMidiFollowTransport(bool followTransport);
    bool saveMidiAutoFallbackMs(uint16_t fallbackMs);
    bool saveMidiAutoLockClockCount(uint8_t lockCount);
    persistence::PersistenceWriteStatus saveMidiSyncModeStatus(MidiSyncMode mode);
    persistence::PersistenceWriteStatus saveMidiFollowTransportStatus(bool followTransport);
    persistence::PersistenceWriteStatus saveMidiAutoFallbackMsStatus(uint16_t fallbackMs);
    persistence::PersistenceWriteStatus saveMidiAutoLockClockCountStatus(uint8_t lockCount);

    bool saveDataManagerMacroShortcutLeft(uint8_t command);
    bool saveDataManagerMacroShortcutRight(uint8_t command);
    bool saveDataManagerSeqShortcutLeft(uint8_t command);
    bool saveDataManagerSeqShortcutRight(uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerMacroShortcutLeftStatus(uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerMacroShortcutRightStatus(uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerSeqShortcutLeftStatus(uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerSeqShortcutRightStatus(uint8_t command);
    bool loadDataManagerShortcuts(uint8_t& macroLeft,
                                  uint8_t& macroRight,
                                  uint8_t& seqLeft,
                                  uint8_t& seqRight);

    bool saveActivePage(uint8_t pageIndex);
    bool savePage(uint8_t pageIndex, const macro::MacroPageData& page);
    bool saveValue(uint8_t pageIndex, uint8_t macroIndex, float value);
    bool saveCC(uint8_t pageIndex, uint8_t macroIndex, uint8_t cc);
    bool saveChannel(uint8_t pageIndex, uint8_t macroIndex, uint8_t channel);
    persistence::PersistenceWriteStatus saveActivePageStatus(uint8_t pageIndex);
    persistence::PersistenceWriteStatus savePageStatus(uint8_t pageIndex,
                                                       const macro::MacroPageData& page);
    persistence::PersistenceWriteStatus saveValueStatus(uint8_t pageIndex,
                                                        uint8_t macroIndex,
                                                        float value);
    persistence::PersistenceWriteStatus saveCCStatus(uint8_t pageIndex,
                                                     uint8_t macroIndex,
                                                     uint8_t cc);
    persistence::PersistenceWriteStatus saveChannelStatus(uint8_t pageIndex,
                                                          uint8_t macroIndex,
                                                          uint8_t channel);
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
    bool saveDataManagerShortcut_(uint32_t address, uint8_t command);
    persistence::PersistenceWriteStatus saveDataManagerShortcutStatus_(uint32_t address,
                                                                       uint8_t command);
    bool writeDefaultShortcuts_();
    persistence::PersistenceWriteStatus writeDefaultShortcutsStatus_();
    bool loadPages_(macro::MacroPagesState& pages);
    bool loadMidiSync_(MidiSyncState& midiSync);

    oc::interface::IStorage& backend_;
};

}  // namespace core::state
