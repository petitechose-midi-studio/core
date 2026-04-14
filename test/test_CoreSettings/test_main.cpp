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
    assert(settings.saveDataManagerMacroShortcutLeft(
        static_cast<uint8_t>(core::state::DataManagerCommand::MACRO_LOAD_SLOT)));
    assert(settings.saveDataManagerMacroShortcutRight(
        static_cast<uint8_t>(core::state::DataManagerCommand::MACRO_ERASE_SLOT)));
    assert(settings.saveDataManagerSeqShortcutLeft(
        static_cast<uint8_t>(core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT)));
    assert(settings.saveDataManagerSeqShortcutRight(
        static_cast<uint8_t>(core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT)));
    assert(settings.commit());

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

    uint8_t macroLeft = 0;
    uint8_t macroRight = 0;
    uint8_t seqLeft = 0;
    uint8_t seqRight = 0;
    assert(settings.loadDataManagerShortcuts(macroLeft, macroRight, seqLeft, seqRight));

    assert(macroLeft == static_cast<uint8_t>(core::state::DataManagerCommand::MACRO_LOAD_SLOT));
    assert(macroRight == static_cast<uint8_t>(core::state::DataManagerCommand::MACRO_ERASE_SLOT));
    assert(seqLeft == static_cast<uint8_t>(core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT));
    assert(seqRight == static_cast<uint8_t>(core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT));

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

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "CoreSettings tests\n";
    std::cout << "==============================================\n\n";

    test_roundtrip_current_format();
    test_invalid_version_resets_to_defaults();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
