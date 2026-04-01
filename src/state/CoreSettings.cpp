#include "state/CoreSettings.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "state/CoreSettingsCodec.hpp"

namespace core::state {

using core::persistence::PersistenceWriteStatus;

namespace {

FLASHMEM bool resetToDefaultsAndPersist(CoreSettings& settings,
                                        macro::MacroPagesState& pages,
                                        MidiSyncState& midiSync,
                                        const char* logMessage) {
    pages.initDefaults();
    midiSync.reset();

    const auto status = settings.saveAllStatus(pages, midiSync);
    if (status != PersistenceWriteStatus::OK) {
        OC_LOG_WARN("{}: {}",
                    logMessage,
                    core::persistence::persistenceWriteStatusLabel(status));
    }
    return false;
}

}  // namespace

FLASHMEM CoreSettings::CoreSettings(oc::interface::IStorage& backend)
    : backend_(backend) {}

FLASHMEM bool CoreSettings::load(macro::MacroPagesState& pages, MidiSyncState& midiSync) {
    uint32_t magic = 0;
    if (!readExact_(StorageLayout::ADDR_MAGIC, reinterpret_cast<uint8_t*>(&magic), sizeof(magic))) {
        OC_LOG_WARN("[CoreSettings] Failed to read magic, using defaults");
        return resetToDefaultsAndPersist(
            *this,
            pages,
            midiSync,
            "[CoreSettings] Failed to persist defaults after read error"
        );
    }

    if (magic != StorageLayout::MAGIC) {
        OC_LOG_INFO("[CoreSettings] No valid data, using defaults");
        return resetToDefaultsAndPersist(
            *this,
            pages,
            midiSync,
            "[CoreSettings] Failed to initialize storage with defaults"
        );
    }

    uint8_t version = 0;
    if (!readExact_(StorageLayout::ADDR_VERSION, &version, 1)) {
        OC_LOG_WARN("[CoreSettings] Failed to read version, using defaults");
        return resetToDefaultsAndPersist(
            *this,
            pages,
            midiSync,
            "[CoreSettings] Failed to persist defaults after version read error"
        );
    }

    if (version == StorageLayout::VERSION) {
        if (!loadPages_(pages) || !loadMidiSync_(midiSync)) {
            OC_LOG_WARN("[CoreSettings] Failed to read payload, using defaults");
            return resetToDefaultsAndPersist(
                *this,
                pages,
                midiSync,
                "[CoreSettings] Failed to persist defaults after payload read error"
            );
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

FLASHMEM bool CoreSettings::saveAll(const macro::MacroPagesState& pages, const MidiSyncState& midiSync) {
    return saveAllStatus(pages, midiSync) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveAllStatus(const macro::MacroPagesState& pages,
                                                   const MidiSyncState& midiSync) {
    const auto status = core_settings::saveAll(backend_, pages, midiSync);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved all");
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::saveMidiSyncMode(MidiSyncMode mode) {
    return saveMidiSyncModeStatus(mode) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveMidiSyncModeStatus(MidiSyncMode mode) {
    const uint8_t value = static_cast<uint8_t>(mode);
    return writeExactStatus_(StorageLayout::ADDR_SYNC_MODE,
                             reinterpret_cast<const uint8_t*>(&value),
                             1);
}

FLASHMEM bool CoreSettings::saveMidiFollowTransport(bool followTransport) {
    return saveMidiFollowTransportStatus(followTransport) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveMidiFollowTransportStatus(bool followTransport) {
    const uint8_t value = followTransport ? 1 : 0;
    return writeExactStatus_(StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT,
                             reinterpret_cast<const uint8_t*>(&value),
                             1);
}

FLASHMEM bool CoreSettings::saveMidiAutoFallbackMs(uint16_t fallbackMs) {
    return saveMidiAutoFallbackMsStatus(fallbackMs) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveMidiAutoFallbackMsStatus(uint16_t fallbackMs) {
    return writeExactStatus_(StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                             reinterpret_cast<const uint8_t*>(&fallbackMs),
                             sizeof(fallbackMs));
}

FLASHMEM bool CoreSettings::saveMidiAutoLockClockCount(uint8_t lockCount) {
    return saveMidiAutoLockClockCountStatus(lockCount) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveMidiAutoLockClockCountStatus(uint8_t lockCount) {
    return writeExactStatus_(StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS,
                             reinterpret_cast<const uint8_t*>(&lockCount),
                             1);
}

FLASHMEM bool CoreSettings::saveDataManagerMacroShortcutLeft(uint8_t command) {
    return saveDataManagerMacroShortcutLeftStatus(command) == PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::saveDataManagerMacroShortcutRight(uint8_t command) {
    return saveDataManagerMacroShortcutRightStatus(command) == PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::saveDataManagerSeqShortcutLeft(uint8_t command) {
    return saveDataManagerSeqShortcutLeftStatus(command) == PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::saveDataManagerSeqShortcutRight(uint8_t command) {
    return saveDataManagerSeqShortcutRightStatus(command) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveDataManagerMacroShortcutLeftStatus(uint8_t command) {
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_MACRO_LEFT, command);
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveDataManagerMacroShortcutRightStatus(uint8_t command) {
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_MACRO_RIGHT, command);
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveDataManagerSeqShortcutLeftStatus(uint8_t command) {
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_SEQ_LEFT, command);
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveDataManagerSeqShortcutRightStatus(uint8_t command) {
    return saveDataManagerShortcutStatus_(StorageLayout::ADDR_SHORTCUT_SEQ_RIGHT, command);
}

FLASHMEM bool CoreSettings::loadDataManagerShortcuts(uint8_t& macroLeft,
                                            uint8_t& macroRight,
                                            uint8_t& seqLeft,
                                            uint8_t& seqRight) {
    return core_settings::loadDataManagerShortcuts(backend_, macroLeft, macroRight, seqLeft, seqRight);
}

FLASHMEM bool CoreSettings::saveActivePage(uint8_t pageIndex) {
    return saveActivePageStatus(pageIndex) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveActivePageStatus(uint8_t pageIndex) {
    const auto status = writeExactStatus_(StorageLayout::ADDR_ACTIVE_PAGE, &pageIndex, 1);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved active page: {}", pageIndex);
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::savePage(uint8_t pageIndex, const macro::MacroPageData& page) {
    return savePageStatus(pageIndex, page) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::savePageStatus(uint8_t pageIndex,
                                                    const macro::MacroPageData& page) {
    const auto status = writeExactStatus_(StorageLayout::pageOffset(pageIndex),
                                          reinterpret_cast<const uint8_t*>(&page),
                                          StorageLayout::MACRO_PAGE_SIZE);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved page {}", pageIndex);
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::saveValue(uint8_t pageIndex, uint8_t macroIndex, float value) {
    return saveValueStatus(pageIndex, macroIndex, value) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveValueStatus(uint8_t pageIndex,
                                                     uint8_t macroIndex,
                                                     float value) {
    return writeExactStatus_(StorageLayout::valueOffset(pageIndex, macroIndex),
                             reinterpret_cast<const uint8_t*>(&value),
                             sizeof(float));
}

FLASHMEM bool CoreSettings::saveCC(uint8_t pageIndex, uint8_t macroIndex, uint8_t cc) {
    return saveCCStatus(pageIndex, macroIndex, cc) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveCCStatus(uint8_t pageIndex,
                                                  uint8_t macroIndex,
                                                  uint8_t cc) {
    const auto status = writeExactStatus_(StorageLayout::ccOffset(pageIndex, macroIndex), &cc, 1);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved CC[{}][{}] = {}", pageIndex, macroIndex, cc);
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::saveChannel(uint8_t pageIndex, uint8_t macroIndex, uint8_t channel) {
    return saveChannelStatus(pageIndex, macroIndex, channel) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveChannelStatus(uint8_t pageIndex,
                                                       uint8_t macroIndex,
                                                       uint8_t channel) {
    const auto status =
        writeExactStatus_(StorageLayout::channelOffset(pageIndex, macroIndex), &channel, 1);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[CoreSettings] Saved CH[{}][{}] = {}", pageIndex, macroIndex, channel);
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::commit() {
    return commitStatus() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::commitStatus() {
    return backend_.commit() ? PersistenceWriteStatus::OK : PersistenceWriteStatus::COMMIT_FAILED;
}

FLASHMEM bool CoreSettings::factoryReset() {
    return factoryResetStatus() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::factoryResetStatus() {
    if (!backend_.erase(0, StorageLayout::ADDR_PAGES + macro::PAGE_COUNT * StorageLayout::MACRO_PAGE_SIZE)) {
        return PersistenceWriteStatus::ERASE_FAILED;
    }
    const auto commitWriteStatus = commitStatus();
    if (commitWriteStatus != PersistenceWriteStatus::OK) return commitWriteStatus;
    OC_LOG_INFO("[CoreSettings] Factory reset");
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreSettings::readExact_(uint32_t address, uint8_t* buffer, size_t size) {
    return core_settings::readExact(backend_, address, buffer, size);
}

FLASHMEM bool CoreSettings::writeExact_(uint32_t address, const uint8_t* buffer, size_t size) {
    return core_settings::writeExact(backend_, address, buffer, size);
}

FLASHMEM PersistenceWriteStatus CoreSettings::writeExactStatus_(uint32_t address,
                                                       const uint8_t* buffer,
                                                       size_t size) {
    return core_settings::writeExactStatus(backend_, address, buffer, size);
}

FLASHMEM bool CoreSettings::saveDataManagerShortcut_(uint32_t address, uint8_t command) {
    return writeExact_(address, reinterpret_cast<const uint8_t*>(&command), 1);
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveDataManagerShortcutStatus_(uint32_t address,
                                                                    uint8_t command) {
    return writeExactStatus_(address, reinterpret_cast<const uint8_t*>(&command), 1);
}

FLASHMEM bool CoreSettings::writeDefaultShortcuts_() {
    return writeDefaultShortcutsStatus_() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::writeDefaultShortcutsStatus_() {
    return core_settings::writeDefaultShortcuts(backend_);
}

FLASHMEM bool CoreSettings::loadPages_(macro::MacroPagesState& pages) {
    return core_settings::loadPages(backend_, pages);
}

FLASHMEM bool CoreSettings::loadMidiSync_(MidiSyncState& midiSync) {
    return core_settings::loadMidiSync(backend_, midiSync);
}

}  // namespace core::state
