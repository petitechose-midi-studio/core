#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/state/CoreSettings.hpp"
#include "../../src/state/CoreSettingsLayout.hpp"
#include "../support/MemoryStorage.hpp"

namespace {
using test_support::MemoryStorage;

void test_roundtrip_current_format() {
    MemoryStorage storage;
    storage.init();

    core::state::MidiSyncState sync;
    sync.mode.set(core::state::MidiSyncMode::SLAVE);
    sync.followTransport.set(false);
    sync.autoFallbackMs.set(750);
    sync.autoLockClockCount.set(12);
    uint16_t sharedTrackEnabledMask = 0x0005;
    uint8_t sharedTrackActive = 2;

    core::state::CoreSettings settings(storage);
    assert(settings.saveAll(sync, sharedTrackEnabledMask, sharedTrackActive));

    core::state::MidiSyncState loadedSync;
    uint16_t loadedSharedTrackEnabledMask = 0;
    uint8_t loadedSharedTrackActive = 0;
    const bool loaded = settings.load(
        loadedSync,
        loadedSharedTrackEnabledMask,
        loadedSharedTrackActive
    );

    assert(loaded);
    assert(loadedSync.mode.get() == core::state::MidiSyncMode::SLAVE);
    assert(!loadedSync.followTransport.get());
    assert(loadedSync.autoFallbackMs.get() == 750);
    assert(loadedSync.autoLockClockCount.get() == 12);
    assert(loadedSharedTrackEnabledMask == sharedTrackEnabledMask);
    assert(loadedSharedTrackActive == sharedTrackActive);

    std::cout << "[PASS] test_roundtrip_current_format\n";
}

void test_invalid_version_resets_to_defaults() {
    MemoryStorage storage;
    storage.init();

    namespace StorageLayout = core::state::core_settings::layout;

    const uint32_t magic = StorageLayout::MAGIC;
    const uint8_t version = 99;
    storage.write(StorageLayout::ADDR_MAGIC,
                  reinterpret_cast<const uint8_t*>(&magic),
                  sizeof(magic));
    storage.write(StorageLayout::ADDR_VERSION, &version, 1);
    storage.commit();

    core::state::CoreSettings settings(storage);
    core::state::MidiSyncState loadedSync;
    uint16_t loadedSharedTrackEnabledMask = 0xFFFF;
    uint8_t loadedSharedTrackActive = 0xFF;
    const bool loaded = settings.load(
        loadedSync,
        loadedSharedTrackEnabledMask,
        loadedSharedTrackActive
    );

    assert(loaded);
    assert(loadedSync.mode.get() == core::state::MidiSyncMode::AUTO);
    assert(loadedSync.followTransport.get());
    assert(loadedSync.autoFallbackMs.get() == 500);
    assert(loadedSync.autoLockClockCount.get() == 6);
    assert(loadedSharedTrackEnabledMask == StorageLayout::DEFAULT_SHARED_TRACK_ENABLED_MASK);
    assert(loadedSharedTrackActive == StorageLayout::DEFAULT_SHARED_TRACK_ACTIVE);

    uint8_t persistedVersion = 0;
    storage.read(StorageLayout::ADDR_VERSION, &persistedVersion, 1);
    assert(persistedVersion == StorageLayout::VERSION);

    std::cout << "[PASS] test_invalid_version_resets_to_defaults\n";
}

void test_stale_version_resets_to_current_defaults() {
    MemoryStorage storage;
    storage.init();

    namespace StorageLayout = core::state::core_settings::layout;

    const uint32_t magic = StorageLayout::MAGIC;
    const uint8_t version = 1;
    const uint8_t mode = static_cast<uint8_t>(core::state::MidiSyncMode::SLAVE);
    const uint8_t followTransport = 0;
    const uint16_t fallbackMs = 900;
    const uint8_t lockClocks = 8;
    const uint8_t retiredBytes[4] = {0x11U, 0x22U, 0x33U, 0x44U};

    storage.write(StorageLayout::ADDR_MAGIC,
                  reinterpret_cast<const uint8_t*>(&magic),
                  sizeof(magic));
    storage.write(StorageLayout::ADDR_VERSION, &version, 1);
    storage.write(StorageLayout::ADDR_SYNC_MODE, &mode, 1);
    storage.write(StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT, &followTransport, 1);
    storage.write(StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                  reinterpret_cast<const uint8_t*>(&fallbackMs),
                  sizeof(fallbackMs));
    storage.write(StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS, &lockClocks, 1);
    storage.write(
        StorageLayout::RETIRED_FIXED_SLOT_BYTES_BEGIN,
        retiredBytes,
        sizeof(retiredBytes)
    );
    storage.commit();

    core::state::CoreSettings settings(storage);
    core::state::MidiSyncState loadedSync;
    uint16_t loadedSharedTrackEnabledMask = 0;
    uint8_t loadedSharedTrackActive = 0;
    assert(settings.load(
        loadedSync,
        loadedSharedTrackEnabledMask,
        loadedSharedTrackActive
    ));

    assert(loadedSync.mode.get() == core::state::MidiSyncMode::AUTO);
    assert(loadedSync.followTransport.get());
    assert(loadedSync.autoFallbackMs.get() == 500);
    assert(loadedSync.autoLockClockCount.get() == 6);
    assert(loadedSharedTrackEnabledMask == StorageLayout::DEFAULT_SHARED_TRACK_ENABLED_MASK);
    assert(loadedSharedTrackActive == StorageLayout::DEFAULT_SHARED_TRACK_ACTIVE);

    uint8_t persistedRetiredBytes[4]{};
    assert(storage.read(
        StorageLayout::RETIRED_FIXED_SLOT_BYTES_BEGIN,
        persistedRetiredBytes,
        sizeof(persistedRetiredBytes)
    ) == sizeof(persistedRetiredBytes));
    for (size_t i = 0; i < sizeof(retiredBytes); ++i) {
        assert(persistedRetiredBytes[i] == retiredBytes[i]);
    }

    uint8_t persistedVersion = 0;
    storage.read(StorageLayout::ADDR_VERSION, &persistedVersion, 1);
    assert(persistedVersion == StorageLayout::VERSION);

    std::cout << "[PASS] test_stale_version_resets_to_current_defaults\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "CoreSettings tests\n";
    std::cout << "==============================================\n\n";

    test_roundtrip_current_format();
    test_invalid_version_resets_to_defaults();
    test_stale_version_resets_to_current_defaults();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
