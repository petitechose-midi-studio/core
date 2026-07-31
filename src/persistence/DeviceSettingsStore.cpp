#include "persistence/DeviceSettingsStore.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "persistence/DeviceSettingsCodec.hpp"
#include "persistence/DeviceSettingsStorageLayout.hpp"

namespace core::persistence {

namespace StorageLayout = device_settings::layout;

FLASHMEM DeviceSettingsStore::DeviceSettingsStore(oc::interface::IStorage& backend)
    : backend_(backend) {}

FLASHMEM bool DeviceSettingsStore::load(state::MidiSyncState& midiSync) {
    uint32_t magic = 0;
    if (!device_settings::readMagic(backend_, magic)) {
        OC_LOG_WARN("[DeviceSettingsStore] Failed to read settings header");
        return false;
    }

    if (magic == UINT32_MAX) {
        state::MidiSyncState defaults;
        const auto status = saveAllStatus(defaults);
        if (status != PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[DeviceSettingsStore] Failed to initialize blank storage: {}",
                        persistenceWriteStatusLabel(status));
            return false;
        }
        midiSync.mode.set(defaults.mode.get());
        midiSync.followTransport.set(defaults.followTransport.get());
        midiSync.autoFallbackMs.set(defaults.autoFallbackMs.get());
        midiSync.autoLockClockCount.set(defaults.autoLockClockCount.get());
        return true;
    }

    if (magic != StorageLayout::MAGIC) {
        OC_LOG_WARN("[DeviceSettingsStore] Invalid settings magic");
        return false;
    }

    uint8_t version = 0;
    if (!device_settings::readVersion(backend_, version)) {
        OC_LOG_WARN("[DeviceSettingsStore] Failed to read settings version");
        return false;
    }

    if (version != StorageLayout::VERSION) {
        OC_LOG_WARN("[DeviceSettingsStore] Unsupported settings version ({} vs {})",
                    version,
                    StorageLayout::VERSION);
        return false;
    }

    if (!loadMidiSync_(midiSync)) {
        OC_LOG_WARN("[DeviceSettingsStore] Invalid current settings payload");
        return false;
    }

    return true;
}

FLASHMEM bool DeviceSettingsStore::saveAll(const state::MidiSyncState& midiSync) {
    return saveAllStatus(midiSync) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus DeviceSettingsStore::saveAllStatus(
    const state::MidiSyncState& midiSync
) {
    const auto status = device_settings::saveAll(backend_, midiSync);
    if (status != PersistenceWriteStatus::OK) return status;
    OC_LOG_DEBUG("[DeviceSettingsStore] Saved all");
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool DeviceSettingsStore::saveMidiSyncMode(state::MidiSyncMode mode) {
    return saveMidiSyncModeStatus(mode) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus DeviceSettingsStore::saveMidiSyncModeStatus(
    state::MidiSyncMode mode
) {
    if (!device_settings::validMidiSyncMode(mode)) {
        return PersistenceWriteStatus::INVALID_CONFIG;
    }
    const auto formatStatus = currentFormatStatus_();
    if (formatStatus != PersistenceWriteStatus::OK) return formatStatus;
    return device_settings::stageMidiSyncMode(backend_, mode);
}

FLASHMEM bool DeviceSettingsStore::saveMidiFollowTransport(bool followTransport) {
    return saveMidiFollowTransportStatus(followTransport) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus DeviceSettingsStore::saveMidiFollowTransportStatus(
    bool followTransport
) {
    const auto formatStatus = currentFormatStatus_();
    if (formatStatus != PersistenceWriteStatus::OK) return formatStatus;
    return device_settings::stageMidiFollowTransport(
        backend_,
        followTransport
    );
}

FLASHMEM bool DeviceSettingsStore::saveMidiAutoFallbackMs(uint16_t fallbackMs) {
    return saveMidiAutoFallbackMsStatus(fallbackMs) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus DeviceSettingsStore::saveMidiAutoFallbackMsStatus(
    uint16_t fallbackMs
) {
    if (!device_settings::validMidiSyncAutoFallbackMs(fallbackMs)) {
        return PersistenceWriteStatus::INVALID_CONFIG;
    }
    const auto formatStatus = currentFormatStatus_();
    if (formatStatus != PersistenceWriteStatus::OK) return formatStatus;
    return device_settings::stageMidiAutoFallbackMs(backend_, fallbackMs);
}

FLASHMEM bool DeviceSettingsStore::saveMidiAutoLockClockCount(uint8_t lockCount) {
    return saveMidiAutoLockClockCountStatus(lockCount) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus DeviceSettingsStore::saveMidiAutoLockClockCountStatus(
    uint8_t lockCount
) {
    if (!device_settings::validMidiSyncAutoLockClockCount(lockCount)) {
        return PersistenceWriteStatus::INVALID_CONFIG;
    }
    const auto formatStatus = currentFormatStatus_();
    if (formatStatus != PersistenceWriteStatus::OK) return formatStatus;
    return device_settings::stageMidiAutoLockClockCount(backend_, lockCount);
}

FLASHMEM bool DeviceSettingsStore::commit() {
    return commitStatus() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus DeviceSettingsStore::commitStatus() {
    if (!backend_.available()) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    return backend_.commit() ? PersistenceWriteStatus::OK : PersistenceWriteStatus::COMMIT_FAILED;
}

FLASHMEM bool DeviceSettingsStore::factoryReset() {
    return factoryResetStatus() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus DeviceSettingsStore::factoryResetStatus() {
    if (!backend_.available()) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    if (!backend_.erase(0, StorageLayout::STORAGE_END)) {
        return PersistenceWriteStatus::ERASE_FAILED;
    }
    const auto commitWriteStatus = commitStatus();
    if (commitWriteStatus != PersistenceWriteStatus::OK) return commitWriteStatus;
    OC_LOG_INFO("[DeviceSettingsStore] Factory reset");
    return PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus DeviceSettingsStore::currentFormatStatus_() {
    if (!backend_.available()) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;

    uint32_t magic = 0;
    uint8_t version = 0;
    if (!device_settings::readMagic(backend_, magic) ||
        !device_settings::readVersion(backend_, version)) {
        return PersistenceWriteStatus::IO_ERROR;
    }
    return magic == StorageLayout::MAGIC && version == StorageLayout::VERSION
        ? PersistenceWriteStatus::OK
        : PersistenceWriteStatus::INVALID_CONFIG;
}

FLASHMEM bool DeviceSettingsStore::loadMidiSync_(state::MidiSyncState& midiSync) {
    return device_settings::loadMidiSync(backend_, midiSync);
}

}  // namespace core::persistence
