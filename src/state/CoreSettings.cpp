#include "state/CoreSettings.hpp"

#include <oc/log/Log.hpp>

namespace core::state {

using core::persistence::PersistenceWriteStatus;

CoreSettings::CoreSettings(oc::interface::IStorage& backend)
    : backend_(backend) {}

bool CoreSettings::load(macro::MacroPagesState& pages, MidiSyncState& midiSync) {
    uint32_t magic = 0;
    if (!readExact_(StorageLayout::ADDR_MAGIC, reinterpret_cast<uint8_t*>(&magic), sizeof(magic))) {
        OC_LOG_WARN("[CoreSettings] Failed to read magic, using defaults");
        pages.initDefaults();
        midiSync.reset();
        const auto status = saveAllStatus(pages, midiSync);
        if (status != PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[CoreSettings] Failed to persist defaults after read error: {}",
                        core::persistence::persistenceWriteStatusLabel(status));
        }
        return false;
    }

    if (magic != StorageLayout::MAGIC) {
        OC_LOG_INFO("[CoreSettings] No valid data, using defaults");
        pages.initDefaults();
        midiSync.reset();
        const auto status = saveAllStatus(pages, midiSync);
        if (status != PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[CoreSettings] Failed to initialize storage with defaults: {}",
                        core::persistence::persistenceWriteStatusLabel(status));
        }
        return false;
    }

    uint8_t version = 0;
    if (!readExact_(StorageLayout::ADDR_VERSION, &version, 1)) {
        OC_LOG_WARN("[CoreSettings] Failed to read version, using defaults");
        pages.initDefaults();
        midiSync.reset();
        const auto status = saveAllStatus(pages, midiSync);
        if (status != PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[CoreSettings] Failed to persist defaults after version read error: {}",
                        core::persistence::persistenceWriteStatusLabel(status));
        }
        return false;
    }

    if (version == StorageLayout::VERSION) {
        if (!loadPages_(pages) || !loadMidiSync_(midiSync)) {
            OC_LOG_WARN("[CoreSettings] Failed to read payload, using defaults");
            pages.initDefaults();
            midiSync.reset();
            const auto status = saveAllStatus(pages, midiSync);
            if (status != PersistenceWriteStatus::OK) {
                OC_LOG_WARN("[CoreSettings] Failed to persist defaults after payload read error: {}",
                            core::persistence::persistenceWriteStatusLabel(status));
            }
            return false;
        }

        OC_LOG_INFO("[CoreSettings] Loaded page {}", pages.activePage);
        return true;
    }

    if (version == 2) {
        if (!loadPages_(pages) || !loadMidiSync_(midiSync)) {
            OC_LOG_WARN("[CoreSettings] Failed to migrate settings v2, using defaults");
            pages.initDefaults();
            midiSync.reset();
            return false;
        }
        const auto status = saveAllStatus(pages, midiSync);
        if (status != PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[CoreSettings] Failed to migrate settings v2, using defaults");
            pages.initDefaults();
            midiSync.reset();
            return false;
        }
        OC_LOG_INFO("[CoreSettings] Migrated settings v2 -> v{}", StorageLayout::VERSION);
        return true;
    }

    if (version == 1) {
        if (!loadPages_(pages)) {
            OC_LOG_WARN("[CoreSettings] Failed to read v1 pages, using defaults");
            pages.initDefaults();
            midiSync.reset();
            return false;
        }
        midiSync.reset();
        const auto status = saveAllStatus(pages, midiSync);
        if (status != PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[CoreSettings] Failed to migrate settings v1, using defaults");
            pages.initDefaults();
            midiSync.reset();
            return false;
        }
        OC_LOG_INFO("[CoreSettings] Migrated settings v1 -> v{}", StorageLayout::VERSION);
        return true;
    }

    OC_LOG_WARN("[CoreSettings] Version mismatch ({} vs {}), using defaults",
                version,
                StorageLayout::VERSION);
    pages.initDefaults();
    midiSync.reset();
    const auto status = saveAllStatus(pages, midiSync);
    if (status != PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreSettings] Failed to persist defaults after version mismatch: {}",
                    core::persistence::persistenceWriteStatusLabel(status));
    }
    return false;
}

bool CoreSettings::saveAll(const macro::MacroPagesState& pages, const MidiSyncState& midiSync) {
    return saveAllStatus(pages, midiSync) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveAllStatus(const macro::MacroPagesState& pages,
                                                   const MidiSyncState& midiSync) {
    const uint32_t magic = StorageLayout::MAGIC;
    const uint8_t version = StorageLayout::VERSION;
    const PersistenceWriteStatus headerStatus =
        writeExactStatus_(StorageLayout::ADDR_MAGIC,
                          reinterpret_cast<const uint8_t*>(&magic),
                          sizeof(magic));
    if (headerStatus != PersistenceWriteStatus::OK) return headerStatus;
    const PersistenceWriteStatus versionStatus =
        writeExactStatus_(StorageLayout::ADDR_VERSION, &version, 1);
    if (versionStatus != PersistenceWriteStatus::OK) return versionStatus;
    const PersistenceWriteStatus pageStatus =
        writeExactStatus_(StorageLayout::ADDR_ACTIVE_PAGE, &pages.activePage, 1);
    if (pageStatus != PersistenceWriteStatus::OK) return pageStatus;
    const uint8_t mode = static_cast<uint8_t>(midiSync.mode.get());
    const uint8_t followTransport = midiSync.followTransport.get() ? 1 : 0;
    const uint16_t fallbackMs = midiSync.autoFallbackMs.get();
    const uint8_t lockClocks = midiSync.autoLockClockCount.get();

    const PersistenceWriteStatus modeStatus =
        writeExactStatus_(StorageLayout::ADDR_SYNC_MODE,
                          reinterpret_cast<const uint8_t*>(&mode),
                          1);
    if (modeStatus != PersistenceWriteStatus::OK) return modeStatus;
    const PersistenceWriteStatus followStatus =
        writeExactStatus_(StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT,
                          reinterpret_cast<const uint8_t*>(&followTransport),
                          1);
    if (followStatus != PersistenceWriteStatus::OK) return followStatus;
    const PersistenceWriteStatus fallbackStatus =
        writeExactStatus_(StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                          reinterpret_cast<const uint8_t*>(&fallbackMs),
                          sizeof(fallbackMs));
    if (fallbackStatus != PersistenceWriteStatus::OK) return fallbackStatus;
    const PersistenceWriteStatus lockStatus =
        writeExactStatus_(StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS,
                          reinterpret_cast<const uint8_t*>(&lockClocks),
                          1);
    if (lockStatus != PersistenceWriteStatus::OK) return lockStatus;

    const auto shortcutStatus = writeDefaultShortcutsStatus_();
    if (shortcutStatus != PersistenceWriteStatus::OK) {
        return shortcutStatus;
    }

    for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
        const PersistenceWriteStatus writeStatus =
            writeExactStatus_(StorageLayout::pageOffset(i),
                              reinterpret_cast<const uint8_t*>(&pages.pages[i]),
                              StorageLayout::MACRO_PAGE_SIZE);
        if (writeStatus != PersistenceWriteStatus::OK) {
            return writeStatus;
        }
    }

    const auto commitWriteStatus = commitStatus();
    if (commitWriteStatus != PersistenceWriteStatus::OK) return commitWriteStatus;

    OC_LOG_DEBUG("[CoreSettings] Saved all");
    return PersistenceWriteStatus::OK;
}

bool CoreSettings::saveMidiSyncMode(MidiSyncMode mode) {
    return saveMidiSyncModeStatus(mode) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveMidiSyncModeStatus(MidiSyncMode mode) {
    const uint8_t value = static_cast<uint8_t>(mode);
    return writeExactStatus_(StorageLayout::ADDR_SYNC_MODE,
                             reinterpret_cast<const uint8_t*>(&value),
                             1);
}

bool CoreSettings::saveMidiFollowTransport(bool followTransport) {
    return saveMidiFollowTransportStatus(followTransport) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveMidiFollowTransportStatus(bool followTransport) {
    const uint8_t value = followTransport ? 1 : 0;
    return writeExactStatus_(StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT,
                             reinterpret_cast<const uint8_t*>(&value),
                             1);
}

bool CoreSettings::saveMidiAutoFallbackMs(uint16_t fallbackMs) {
    return saveMidiAutoFallbackMsStatus(fallbackMs) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveMidiAutoFallbackMsStatus(uint16_t fallbackMs) {
    return writeExactStatus_(StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                             reinterpret_cast<const uint8_t*>(&fallbackMs),
                             sizeof(fallbackMs));
}

bool CoreSettings::saveMidiAutoLockClockCount(uint8_t lockCount) {
    return saveMidiAutoLockClockCountStatus(lockCount) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveMidiAutoLockClockCountStatus(uint8_t lockCount) {
    return writeExactStatus_(StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS,
                             reinterpret_cast<const uint8_t*>(&lockCount),
                             1);
}

bool CoreSettings::saveDataManagerMacroShortcutLeft(uint8_t command) {
    return saveDataManagerMacroShortcutLeftStatus(command) == PersistenceWriteStatus::OK;
}

bool CoreSettings::saveDataManagerMacroShortcutRight(uint8_t command) {
    return saveDataManagerMacroShortcutRightStatus(command) == PersistenceWriteStatus::OK;
}

bool CoreSettings::saveDataManagerSeqShortcutLeft(uint8_t command) {
    return saveDataManagerSeqShortcutLeftStatus(command) == PersistenceWriteStatus::OK;
}

bool CoreSettings::saveDataManagerSeqShortcutRight(uint8_t command) {
    return saveDataManagerSeqShortcutRightStatus(command) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveDataManagerMacroShortcutLeftStatus(uint8_t command) {
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_MACRO_LEFT, command);
}

PersistenceWriteStatus CoreSettings::saveDataManagerMacroShortcutRightStatus(uint8_t command) {
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_MACRO_RIGHT, command);
}

PersistenceWriteStatus CoreSettings::saveDataManagerSeqShortcutLeftStatus(uint8_t command) {
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_SEQ_LEFT, command);
}

PersistenceWriteStatus CoreSettings::saveDataManagerSeqShortcutRightStatus(uint8_t command) {
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_SEQ_RIGHT, command);
}

bool CoreSettings::loadDataManagerShortcuts(uint8_t& macroLeft,
                                            uint8_t& macroRight,
                                            uint8_t& seqLeft,
                                            uint8_t& seqRight) {
    macroLeft = StorageLayout::DEFAULT_SHORTCUT_MACRO_LEFT;
    macroRight = StorageLayout::DEFAULT_SHORTCUT_MACRO_RIGHT;
    seqLeft = StorageLayout::DEFAULT_SHORTCUT_SEQ_LEFT;
    seqRight = StorageLayout::DEFAULT_SHORTCUT_SEQ_RIGHT;

    uint8_t version = 0;
    if (!readExact_(StorageLayout::ADDR_VERSION, &version, 1)) {
        return false;
    }
    if (version < StorageLayout::VERSION) {
        return true;
    }

    return readExact_(StorageLayout::ADDR_SHORTCUT_MACRO_LEFT, &macroLeft, 1) &&
           readExact_(StorageLayout::ADDR_SHORTCUT_MACRO_RIGHT, &macroRight, 1) &&
           readExact_(StorageLayout::ADDR_SHORTCUT_SEQ_LEFT, &seqLeft, 1) &&
           readExact_(StorageLayout::ADDR_SHORTCUT_SEQ_RIGHT, &seqRight, 1);
}

bool CoreSettings::saveActivePage(uint8_t pageIndex) {
    return saveActivePageStatus(pageIndex) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveActivePageStatus(uint8_t pageIndex) {
    const auto status = writeExactStatus_(StorageLayout::ADDR_ACTIVE_PAGE, &pageIndex, 1);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved active page: {}", pageIndex);
    return PersistenceWriteStatus::OK;
}

bool CoreSettings::savePage(uint8_t pageIndex, const macro::MacroPageData& page) {
    return savePageStatus(pageIndex, page) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::savePageStatus(uint8_t pageIndex,
                                                    const macro::MacroPageData& page) {
    const auto status = writeExactStatus_(StorageLayout::pageOffset(pageIndex),
                                          reinterpret_cast<const uint8_t*>(&page),
                                          StorageLayout::MACRO_PAGE_SIZE);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved page {}", pageIndex);
    return PersistenceWriteStatus::OK;
}

bool CoreSettings::saveValue(uint8_t pageIndex, uint8_t macroIndex, float value) {
    return saveValueStatus(pageIndex, macroIndex, value) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveValueStatus(uint8_t pageIndex,
                                                     uint8_t macroIndex,
                                                     float value) {
    return writeExactStatus_(StorageLayout::valueOffset(pageIndex, macroIndex),
                             reinterpret_cast<const uint8_t*>(&value),
                             sizeof(float));
}

bool CoreSettings::saveCC(uint8_t pageIndex, uint8_t macroIndex, uint8_t cc) {
    return saveCCStatus(pageIndex, macroIndex, cc) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveCCStatus(uint8_t pageIndex,
                                                  uint8_t macroIndex,
                                                  uint8_t cc) {
    const auto status = writeExactStatus_(StorageLayout::ccOffset(pageIndex, macroIndex), &cc, 1);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved CC[{}][{}] = {}", pageIndex, macroIndex, cc);
    return PersistenceWriteStatus::OK;
}

bool CoreSettings::saveChannel(uint8_t pageIndex, uint8_t macroIndex, uint8_t channel) {
    return saveChannelStatus(pageIndex, macroIndex, channel) == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::saveChannelStatus(uint8_t pageIndex,
                                                       uint8_t macroIndex,
                                                       uint8_t channel) {
    const auto status =
        writeExactStatus_(StorageLayout::channelOffset(pageIndex, macroIndex), &channel, 1);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved CH[{}][{}] = {}", pageIndex, macroIndex, channel);
    return PersistenceWriteStatus::OK;
}

bool CoreSettings::commit() {
    return commitStatus() == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::commitStatus() {
    return backend_.commit() ? PersistenceWriteStatus::OK : PersistenceWriteStatus::COMMIT_FAILED;
}

bool CoreSettings::factoryReset() {
    return factoryResetStatus() == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::factoryResetStatus() {
    if (!backend_.erase(0, StorageLayout::ADDR_PAGES + macro::PAGE_COUNT * StorageLayout::MACRO_PAGE_SIZE)) {
        return PersistenceWriteStatus::ERASE_FAILED;
    }
    const auto commitWriteStatus = commitStatus();
    if (commitWriteStatus != PersistenceWriteStatus::OK) return commitWriteStatus;
    OC_LOG_INFO("[CoreSettings] Factory reset");
    return PersistenceWriteStatus::OK;
}

bool CoreSettings::readExact_(uint32_t address, uint8_t* buffer, size_t size) {
    return backend_.read(address, buffer, size) == size;
}

bool CoreSettings::writeExact_(uint32_t address, const uint8_t* buffer, size_t size) {
    return backend_.write(address, buffer, size) == size;
}

PersistenceWriteStatus CoreSettings::writeExactStatus_(uint32_t address,
                                                       const uint8_t* buffer,
                                                       size_t size) {
    return writeExact_(address, buffer, size) ? PersistenceWriteStatus::OK
                                              : PersistenceWriteStatus::IO_ERROR;
}

bool CoreSettings::saveDataManagerShortcut_(uint32_t address, uint8_t command) {
    return writeExact_(address, reinterpret_cast<const uint8_t*>(&command), 1);
}

PersistenceWriteStatus CoreSettings::saveDataManagerShortcutStatus_(uint32_t address,
                                                                    uint8_t command) {
    return writeExactStatus_(address, reinterpret_cast<const uint8_t*>(&command), 1);
}

bool CoreSettings::writeDefaultShortcuts_() {
    return writeDefaultShortcutsStatus_() == PersistenceWriteStatus::OK;
}

PersistenceWriteStatus CoreSettings::writeDefaultShortcutsStatus_() {
    const uint8_t macroLeft = StorageLayout::DEFAULT_SHORTCUT_MACRO_LEFT;
    const uint8_t macroRight = StorageLayout::DEFAULT_SHORTCUT_MACRO_RIGHT;
    const uint8_t seqLeft = StorageLayout::DEFAULT_SHORTCUT_SEQ_LEFT;
    const uint8_t seqRight = StorageLayout::DEFAULT_SHORTCUT_SEQ_RIGHT;

    const auto macroLeftStatus =
        saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_MACRO_LEFT, macroLeft);
    if (macroLeftStatus != PersistenceWriteStatus::OK) return macroLeftStatus;
    const auto macroRightStatus =
        saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_MACRO_RIGHT, macroRight);
    if (macroRightStatus != PersistenceWriteStatus::OK) return macroRightStatus;
    const auto seqLeftStatus =
        saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_SEQ_LEFT, seqLeft);
    if (seqLeftStatus != PersistenceWriteStatus::OK) return seqLeftStatus;
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_SEQ_RIGHT, seqRight);
}

bool CoreSettings::loadPages_(macro::MacroPagesState& pages) {
    uint8_t activePage = 0;
    if (!readExact_(StorageLayout::ADDR_ACTIVE_PAGE, &activePage, 1)) {
        return false;
    }
    if (activePage >= macro::PAGE_COUNT) {
        activePage = 0;
    }

    for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
        if (!readExact_(StorageLayout::pageOffset(i),
                        reinterpret_cast<uint8_t*>(&pages.pages[i]),
                        StorageLayout::MACRO_PAGE_SIZE)) {
            return false;
        }
    }

    pages.activePage = activePage;
    pages.updateActiveConfigs();
    return true;
}

bool CoreSettings::loadMidiSync_(MidiSyncState& midiSync) {
    uint8_t rawMode = static_cast<uint8_t>(MidiSyncMode::AUTO);
    if (!readExact_(StorageLayout::ADDR_SYNC_MODE, &rawMode, 1)) {
        return false;
    }
    if (rawMode > static_cast<uint8_t>(MidiSyncMode::AUTO)) {
        rawMode = static_cast<uint8_t>(MidiSyncMode::AUTO);
    }

    uint8_t followTransport = 1;
    if (!readExact_(StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT, &followTransport, 1)) {
        return false;
    }

    uint16_t fallbackMs = 500;
    if (!readExact_(StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                    reinterpret_cast<uint8_t*>(&fallbackMs),
                    sizeof(fallbackMs))) {
        return false;
    }
    if (fallbackMs < 100) fallbackMs = 100;
    if (fallbackMs > 5000) fallbackMs = 5000;

    uint8_t lockClocks = 6;
    if (!readExact_(StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS, &lockClocks, 1)) {
        return false;
    }
    if (lockClocks < 1) lockClocks = 1;
    if (lockClocks > 96) lockClocks = 96;

    midiSync.reset();
    midiSync.mode.set(static_cast<MidiSyncMode>(rawMode));
    midiSync.followTransport.set(followTransport != 0);
    midiSync.autoFallbackMs.set(fallbackMs);
    midiSync.autoLockClockCount.set(lockClocks);
    return true;
}

}  // namespace core::state
