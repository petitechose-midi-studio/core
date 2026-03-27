#pragma once

/**
 * @file CoreSettings.hpp
 * @brief Incremental persistence for pages + sync + shortcut mappings
 */

#include <cstddef>
#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "DataManagerState.hpp"
#include "MidiSyncState.hpp"
#include "macro/MacroPagesState.hpp"

namespace core::state {

namespace StorageLayout {
    constexpr uint32_t MAGIC = 0x4D435354;
    constexpr uint8_t VERSION = 3;

    constexpr uint32_t ADDR_MAGIC = 0x0000;
    constexpr uint32_t ADDR_VERSION = 0x0004;
    constexpr uint32_t ADDR_ACTIVE_PAGE = 0x0005;
    constexpr uint32_t ADDR_RESERVED = 0x0006;
    constexpr uint32_t ADDR_PAGES = 0x0010;

    constexpr uint32_t ADDR_SYNC_MODE = ADDR_RESERVED;
    constexpr uint32_t ADDR_SYNC_FOLLOW_TRANSPORT = ADDR_RESERVED + 1;
    constexpr uint32_t ADDR_SYNC_AUTO_FALLBACK_MS = ADDR_RESERVED + 2;
    constexpr uint32_t ADDR_SYNC_AUTO_LOCK_CLOCKS = ADDR_RESERVED + 4;

    constexpr uint32_t ADDR_SHORTCUT_MACRO_LEFT = ADDR_RESERVED + 5;
    constexpr uint32_t ADDR_SHORTCUT_MACRO_RIGHT = ADDR_RESERVED + 6;
    constexpr uint32_t ADDR_SHORTCUT_SEQ_LEFT = ADDR_RESERVED + 7;
    constexpr uint32_t ADDR_SHORTCUT_SEQ_RIGHT = ADDR_RESERVED + 8;
    static_assert(ADDR_SHORTCUT_SEQ_RIGHT < ADDR_PAGES);

    constexpr uint8_t DEFAULT_SHORTCUT_MACRO_LEFT =
        static_cast<uint8_t>(DEFAULT_MACRO_SHORTCUT_LEFT);
    constexpr uint8_t DEFAULT_SHORTCUT_MACRO_RIGHT =
        static_cast<uint8_t>(DEFAULT_MACRO_SHORTCUT_RIGHT);
    constexpr uint8_t DEFAULT_SHORTCUT_SEQ_LEFT =
        static_cast<uint8_t>(DEFAULT_SEQ_SHORTCUT_LEFT);
    constexpr uint8_t DEFAULT_SHORTCUT_SEQ_RIGHT =
        static_cast<uint8_t>(DEFAULT_SEQ_SHORTCUT_RIGHT);

    constexpr size_t MACRO_PAGE_SIZE = sizeof(macro::MacroPageData);
    static_assert(MACRO_PAGE_SIZE == 64, "Page size must be 64 bytes");

    static constexpr uint32_t OFF_NAME = static_cast<uint32_t>(offsetof(macro::MacroPageData, name));
    static constexpr uint32_t OFF_CC = static_cast<uint32_t>(offsetof(macro::MacroPageData, cc));
    static constexpr uint32_t OFF_CHANNEL = static_cast<uint32_t>(offsetof(macro::MacroPageData, channel));
    static constexpr uint32_t OFF_VALUES = static_cast<uint32_t>(offsetof(macro::MacroPageData, values));

    static_assert(OFF_NAME == 0, "Unexpected MacroPageData layout");
    static_assert(OFF_CC == 16, "Unexpected MacroPageData layout");
    static_assert(OFF_CHANNEL == 24, "Unexpected MacroPageData layout");
    static_assert(OFF_VALUES == 32, "Unexpected MacroPageData layout");

    inline constexpr uint32_t pageOffset(uint8_t pageIndex) {
        return ADDR_PAGES + pageIndex * MACRO_PAGE_SIZE;
    }

    inline constexpr uint32_t valueOffset(uint8_t pageIndex, uint8_t macroIndex) {
        return pageOffset(pageIndex) + OFF_VALUES + macroIndex * sizeof(float);
    }

    inline constexpr uint32_t ccOffset(uint8_t pageIndex, uint8_t macroIndex) {
        return pageOffset(pageIndex) + OFF_CC + macroIndex;
    }

    inline constexpr uint32_t channelOffset(uint8_t pageIndex, uint8_t macroIndex) {
        return pageOffset(pageIndex) + OFF_CHANNEL + macroIndex;
    }
}

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
