#include "state/CoreSettings.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "state/CoreSettingsCodec.hpp"

namespace core::state {

using core::persistence::PersistenceWriteStatus;

namespace {

FLASHMEM bool resetToDefaultsAndPersist(CoreSettings& settings,
                                        MidiSyncState& midiSync,
                                        const char* logMessage) {
    midiSync.reset();

    const auto status = settings.saveAllStatus(midiSync);
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

FLASHMEM bool CoreSettings::load(MidiSyncState& midiSync) {
    uint32_t magic = 0;
    if (!readExact_(StorageLayout::ADDR_MAGIC, reinterpret_cast<uint8_t*>(&magic), sizeof(magic))) {
        OC_LOG_WARN("[CoreSettings] Failed to read magic, using defaults");
        return resetToDefaultsAndPersist(
            *this,
            midiSync,
            "[CoreSettings] Failed to persist defaults after read error"
        );
    }

    if (magic != StorageLayout::MAGIC) {
        OC_LOG_INFO("[CoreSettings] No valid data, using defaults");
        return resetToDefaultsAndPersist(
            *this,
            midiSync,
            "[CoreSettings] Failed to initialize storage with defaults"
        );
    }

    uint8_t version = 0;
    if (!readExact_(StorageLayout::ADDR_VERSION, &version, 1)) {
        OC_LOG_WARN("[CoreSettings] Failed to read version, using defaults");
        return resetToDefaultsAndPersist(
            *this,
            midiSync,
            "[CoreSettings] Failed to persist defaults after version read error"
        );
    }

    if (version != StorageLayout::VERSION) {
        OC_LOG_WARN("[CoreSettings] Version mismatch ({} vs {}), resetting defaults",
                    version,
                    StorageLayout::VERSION);
        return resetToDefaultsAndPersist(
            *this,
            midiSync,
            "[CoreSettings] Failed to persist defaults after version mismatch"
        );
    }

    if (!loadMidiSync_(midiSync)) {
        OC_LOG_WARN("[CoreSettings] Failed to read payload, using defaults");
        return resetToDefaultsAndPersist(
            *this,
            midiSync,
            "[CoreSettings] Failed to persist defaults after payload read error"
        );
    }

    return true;
}

FLASHMEM bool CoreSettings::saveAll(const MidiSyncState& midiSync) {
    return saveAllStatus(midiSync) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus CoreSettings::saveAllStatus(const MidiSyncState& midiSync) {
    const auto status = core_settings::saveAll(backend_, midiSync);
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
    if (!backend_.erase(0, StorageLayout::STORAGE_END)) {
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

FLASHMEM bool CoreSettings::loadMidiSync_(MidiSyncState& midiSync) {
    return core_settings::loadMidiSync(backend_, midiSync);
}

}  // namespace core::state
