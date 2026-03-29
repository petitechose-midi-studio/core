#include "state/CoreSettingsCodec.hpp"

namespace core::state::core_settings {

using core::persistence::PersistenceWriteStatus;

bool readExact(oc::interface::IStorage& backend, uint32_t address, uint8_t* buffer, size_t size) {
    return backend.read(address, buffer, size) == size;
}

bool writeExact(oc::interface::IStorage& backend, uint32_t address, const uint8_t* buffer, size_t size) {
    return backend.write(address, buffer, size) == size;
}

PersistenceWriteStatus writeExactStatus(oc::interface::IStorage& backend,
                                        uint32_t address,
                                        const uint8_t* buffer,
                                        size_t size) {
    return writeExact(backend, address, buffer, size)
               ? PersistenceWriteStatus::OK
               : PersistenceWriteStatus::IO_ERROR;
}

PersistenceWriteStatus writeDefaultShortcuts(oc::interface::IStorage& backend) {
    const uint8_t macroLeft = layout::DEFAULT_SHORTCUT_MACRO_LEFT;
    const uint8_t macroRight = layout::DEFAULT_SHORTCUT_MACRO_RIGHT;
    const uint8_t seqLeft = layout::DEFAULT_SHORTCUT_SEQ_LEFT;
    const uint8_t seqRight = layout::DEFAULT_SHORTCUT_SEQ_RIGHT;

    const auto macroLeftStatus =
        writeExactStatus(backend, layout::ADDR_SHORTCUT_MACRO_LEFT, &macroLeft, 1);
    if (macroLeftStatus != PersistenceWriteStatus::OK) return macroLeftStatus;

    const auto macroRightStatus =
        writeExactStatus(backend, layout::ADDR_SHORTCUT_MACRO_RIGHT, &macroRight, 1);
    if (macroRightStatus != PersistenceWriteStatus::OK) return macroRightStatus;

    const auto seqLeftStatus =
        writeExactStatus(backend, layout::ADDR_SHORTCUT_SEQ_LEFT, &seqLeft, 1);
    if (seqLeftStatus != PersistenceWriteStatus::OK) return seqLeftStatus;

    return writeExactStatus(backend, layout::ADDR_SHORTCUT_SEQ_RIGHT, &seqRight, 1);
}

PersistenceWriteStatus saveAll(oc::interface::IStorage& backend,
                               const macro::MacroPagesState& pages,
                               const MidiSyncState& midiSync) {
    const uint32_t magic = layout::MAGIC;
    const uint8_t version = layout::VERSION;
    const uint8_t activePage = pages.activePage;
    const uint8_t mode = static_cast<uint8_t>(midiSync.mode.get());
    const uint8_t followTransport = midiSync.followTransport.get() ? 1 : 0;
    const uint16_t fallbackMs = midiSync.autoFallbackMs.get();
    const uint8_t lockClocks = midiSync.autoLockClockCount.get();

    const auto headerStatus =
        writeExactStatus(backend, layout::ADDR_MAGIC, reinterpret_cast<const uint8_t*>(&magic), sizeof(magic));
    if (headerStatus != PersistenceWriteStatus::OK) return headerStatus;

    const auto versionStatus = writeExactStatus(backend, layout::ADDR_VERSION, &version, 1);
    if (versionStatus != PersistenceWriteStatus::OK) return versionStatus;

    const auto activePageStatus = writeExactStatus(backend, layout::ADDR_ACTIVE_PAGE, &activePage, 1);
    if (activePageStatus != PersistenceWriteStatus::OK) return activePageStatus;

    const auto modeStatus =
        writeExactStatus(backend, layout::ADDR_SYNC_MODE, reinterpret_cast<const uint8_t*>(&mode), 1);
    if (modeStatus != PersistenceWriteStatus::OK) return modeStatus;

    const auto followStatus =
        writeExactStatus(backend,
                         layout::ADDR_SYNC_FOLLOW_TRANSPORT,
                         reinterpret_cast<const uint8_t*>(&followTransport),
                         1);
    if (followStatus != PersistenceWriteStatus::OK) return followStatus;

    const auto fallbackStatus =
        writeExactStatus(backend,
                         layout::ADDR_SYNC_AUTO_FALLBACK_MS,
                         reinterpret_cast<const uint8_t*>(&fallbackMs),
                         sizeof(fallbackMs));
    if (fallbackStatus != PersistenceWriteStatus::OK) return fallbackStatus;

    const auto lockStatus =
        writeExactStatus(backend,
                         layout::ADDR_SYNC_AUTO_LOCK_CLOCKS,
                         reinterpret_cast<const uint8_t*>(&lockClocks),
                         1);
    if (lockStatus != PersistenceWriteStatus::OK) return lockStatus;

    const auto shortcutStatus = writeDefaultShortcuts(backend);
    if (shortcutStatus != PersistenceWriteStatus::OK) return shortcutStatus;

    for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
        const auto pageStatus =
            writeExactStatus(backend,
                             layout::pageOffset(i),
                             reinterpret_cast<const uint8_t*>(&pages.pages[i]),
                             layout::MACRO_PAGE_SIZE);
        if (pageStatus != PersistenceWriteStatus::OK) return pageStatus;
    }

    return backend.commit() ? PersistenceWriteStatus::OK : PersistenceWriteStatus::COMMIT_FAILED;
}

bool loadPages(oc::interface::IStorage& backend, macro::MacroPagesState& pages) {
    uint8_t activePage = 0;
    if (!readExact(backend, layout::ADDR_ACTIVE_PAGE, &activePage, 1)) {
        return false;
    }
    if (activePage >= macro::PAGE_COUNT) {
        activePage = 0;
    }

    for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
        if (!readExact(backend,
                       layout::pageOffset(i),
                       reinterpret_cast<uint8_t*>(&pages.pages[i]),
                       layout::MACRO_PAGE_SIZE)) {
            return false;
        }
    }

    pages.activePage = activePage;
    pages.updateActiveConfigs();
    return true;
}

bool loadMidiSync(oc::interface::IStorage& backend, MidiSyncState& midiSync) {
    uint8_t rawMode = static_cast<uint8_t>(MidiSyncMode::AUTO);
    if (!readExact(backend, layout::ADDR_SYNC_MODE, &rawMode, 1)) {
        return false;
    }
    if (rawMode > static_cast<uint8_t>(MidiSyncMode::AUTO)) {
        rawMode = static_cast<uint8_t>(MidiSyncMode::AUTO);
    }

    uint8_t followTransport = 1;
    if (!readExact(backend, layout::ADDR_SYNC_FOLLOW_TRANSPORT, &followTransport, 1)) {
        return false;
    }

    uint16_t fallbackMs = 500;
    if (!readExact(backend,
                   layout::ADDR_SYNC_AUTO_FALLBACK_MS,
                   reinterpret_cast<uint8_t*>(&fallbackMs),
                   sizeof(fallbackMs))) {
        return false;
    }
    if (fallbackMs < 100) fallbackMs = 100;
    if (fallbackMs > 5000) fallbackMs = 5000;

    uint8_t lockClocks = 6;
    if (!readExact(backend, layout::ADDR_SYNC_AUTO_LOCK_CLOCKS, &lockClocks, 1)) {
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

bool loadDataManagerShortcuts(oc::interface::IStorage& backend,
                              uint8_t& macroLeft,
                              uint8_t& macroRight,
                              uint8_t& seqLeft,
                              uint8_t& seqRight) {
    macroLeft = layout::DEFAULT_SHORTCUT_MACRO_LEFT;
    macroRight = layout::DEFAULT_SHORTCUT_MACRO_RIGHT;
    seqLeft = layout::DEFAULT_SHORTCUT_SEQ_LEFT;
    seqRight = layout::DEFAULT_SHORTCUT_SEQ_RIGHT;

    uint8_t version = 0;
    if (!readExact(backend, layout::ADDR_VERSION, &version, 1)) {
        return false;
    }
    if (version < layout::VERSION) {
        return true;
    }

    return readExact(backend, layout::ADDR_SHORTCUT_MACRO_LEFT, &macroLeft, 1) &&
           readExact(backend, layout::ADDR_SHORTCUT_MACRO_RIGHT, &macroRight, 1) &&
           readExact(backend, layout::ADDR_SHORTCUT_SEQ_LEFT, &seqLeft, 1) &&
           readExact(backend, layout::ADDR_SHORTCUT_SEQ_RIGHT, &seqRight, 1);
}

}  // namespace core::state::core_settings
