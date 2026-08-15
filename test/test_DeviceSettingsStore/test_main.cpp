#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../../src/midi/MidiUtils.hpp"
#include "../../src/persistence/DeviceSettingsStorageLayout.hpp"
#include "../../src/persistence/DeviceSettingsStore.hpp"
#include "../support/MemoryStorage.hpp"

namespace {

using test_support::MemoryStorage;
namespace StorageLayout = core::persistence::device_settings::layout;

void assertSync(
    const core::state::MidiSyncState& sync,
    core::state::MidiSyncMode mode,
    bool followTransport,
    uint16_t fallbackMs,
    uint8_t lockClocks
) {
    assert(sync.mode.get() == mode);
    assert(sync.followTransport.get() == followTransport);
    assert(sync.autoFallbackMs.get() == fallbackMs);
    assert(sync.autoLockClockCount.get() == lockClocks);
}

void setSentinel(
    core::state::MidiSyncState& sync,
    core::state::MidiNoteDisplayState& noteDisplay
) {
    sync.mode.set(core::state::MidiSyncMode::SLAVE);
    sync.followTransport.set(false);
    sync.autoFallbackMs.set(750);
    sync.autoLockClockCount.set(12);
    assert(noteDisplay.setOctaveConvention(
        core::midi::NoteOctaveConvention::C5
    ));
}

void test_roundtrip_current_format() {
    MemoryStorage storage;
    storage.init();

    core::state::MidiSyncState sync;
    core::state::MidiNoteDisplayState noteDisplay;
    setSentinel(sync, noteDisplay);

    core::persistence::DeviceSettingsStore store(storage);
    assert(store.saveAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);

    core::midi::setActiveNoteOctaveConvention(
        core::midi::NoteOctaveConvention::C3
    );

    core::state::MidiSyncState loadedSync;
    core::state::MidiNoteDisplayState loadedNoteDisplay;
    assert(store.load(loadedSync, loadedNoteDisplay));
    assertSync(
        loadedSync,
        core::state::MidiSyncMode::SLAVE,
        false,
        750,
        12
    );
    assert(loadedNoteDisplay.octaveConvention.get() ==
           core::midi::NoteOctaveConvention::C5);
    char formattedMiddleC[8]{};
    core::midi::formatNoteName(
        formattedMiddleC,
        sizeof(formattedMiddleC),
        60U
    );
    assert(std::strcmp(formattedMiddleC, "C5") == 0);

    std::cout << "[PASS] test_roundtrip_current_format\n";
}

void test_current_format_has_exact_canonical_bytes() {
    static_assert(StorageLayout::STORAGE_END == 11U);
    static_assert(StorageLayout::VERSION == 4U);

    MemoryStorage storage;
    storage.init();

    core::state::MidiSyncState sync;
    core::state::MidiNoteDisplayState noteDisplay;
    setSentinel(sync, noteDisplay);

    core::persistence::DeviceSettingsStore store(storage);
    assert(store.saveAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);

    std::array<uint8_t, StorageLayout::STORAGE_END> encoded{};
    assert(storage.read(0, encoded.data(), encoded.size()) == encoded.size());

    constexpr std::array<uint8_t, StorageLayout::STORAGE_END> expected{
        0x54U, 0x53U, 0x43U, 0x4DU,
        0x04U,
        0x01U,
        0x00U,
        0xEEU, 0x02U,
        0x0CU,
        0x02U,
    };
    assert(encoded == expected);

    std::cout << "[PASS] test_current_format_has_exact_canonical_bytes\n";
}

void test_blank_storage_is_initialized_once() {
    MemoryStorage storage;
    storage.init();

    core::persistence::DeviceSettingsStore store(storage);
    core::state::MidiSyncState loadedSync;
    core::state::MidiNoteDisplayState loadedNoteDisplay;
    setSentinel(loadedSync, loadedNoteDisplay);

    assert(store.load(loadedSync, loadedNoteDisplay));
    assertSync(
        loadedSync,
        core::state::MidiSyncMode::AUTO,
        true,
        500,
        6
    );
    assert(loadedNoteDisplay.octaveConvention.get() ==
           core::midi::NoteOctaveConvention::C4);

    uint32_t persistedMagic = 0;
    uint8_t persistedVersion = 0;
    assert(storage.read(
        StorageLayout::ADDR_MAGIC,
        reinterpret_cast<uint8_t*>(&persistedMagic),
        sizeof(persistedMagic)
    ) == sizeof(persistedMagic));
    assert(storage.read(
        StorageLayout::ADDR_VERSION,
        &persistedVersion,
        sizeof(persistedVersion)
    ) == sizeof(persistedVersion));
    assert(persistedMagic == StorageLayout::MAGIC);
    assert(persistedVersion == StorageLayout::VERSION);

    std::cout << "[PASS] test_blank_storage_is_initialized_once\n";
}

void test_unsupported_version_is_rejected_without_rewrite() {
    MemoryStorage storage;
    storage.init();

    const uint32_t magic = StorageLayout::MAGIC;
    const uint8_t unsupportedVersion =
        static_cast<uint8_t>(StorageLayout::VERSION - 1U);
    assert(storage.write(
        StorageLayout::ADDR_MAGIC,
        reinterpret_cast<const uint8_t*>(&magic),
        sizeof(magic)
    ) == sizeof(magic));
    assert(storage.write(
        StorageLayout::ADDR_VERSION,
        &unsupportedVersion,
        sizeof(unsupportedVersion)
    ) == sizeof(unsupportedVersion));
    assert(storage.commit());

    core::persistence::DeviceSettingsStore store(storage);
    core::state::MidiSyncState loadedSync;
    core::state::MidiNoteDisplayState loadedNoteDisplay;
    setSentinel(loadedSync, loadedNoteDisplay);
    assert(!store.load(loadedSync, loadedNoteDisplay));
    assertSync(
        loadedSync,
        core::state::MidiSyncMode::SLAVE,
        false,
        750,
        12
    );
    assert(loadedNoteDisplay.octaveConvention.get() ==
           core::midi::NoteOctaveConvention::C5);

    uint8_t persistedVersion = 0;
    assert(storage.read(
        StorageLayout::ADDR_VERSION,
        &persistedVersion,
        sizeof(persistedVersion)
    ) == sizeof(persistedVersion));
    assert(persistedVersion == unsupportedVersion);

    std::cout << "[PASS] test_unsupported_version_is_rejected_without_rewrite\n";
}

void test_noncanonical_current_payload_is_rejected_atomically() {
    MemoryStorage storage;
    storage.init();

    core::persistence::DeviceSettingsStore store(storage);
    core::state::MidiSyncState persisted;
    core::state::MidiNoteDisplayState persistedNoteDisplay;
    assert(store.saveAllStatus(persisted, persistedNoteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);

    const uint8_t invalidFollowTransport = 2U;
    assert(storage.write(
        StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT,
        &invalidFollowTransport,
        sizeof(invalidFollowTransport)
    ) == sizeof(invalidFollowTransport));
    assert(storage.commit());

    core::state::MidiSyncState loadedSync;
    core::state::MidiNoteDisplayState loadedNoteDisplay;
    setSentinel(loadedSync, loadedNoteDisplay);
    assert(!store.load(loadedSync, loadedNoteDisplay));
    assertSync(
        loadedSync,
        core::state::MidiSyncMode::SLAVE,
        false,
        750,
        12
    );
    assert(loadedNoteDisplay.octaveConvention.get() ==
           core::midi::NoteOctaveConvention::C5);

    uint8_t persistedFollowTransport = 0;
    assert(storage.read(
        StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT,
        &persistedFollowTransport,
        sizeof(persistedFollowTransport)
    ) == sizeof(persistedFollowTransport));
    assert(persistedFollowTransport == invalidFollowTransport);

    std::cout << "[PASS] test_noncanonical_current_payload_is_rejected_atomically\n";
}

void test_invalid_note_octave_convention_is_rejected_atomically() {
    MemoryStorage storage;
    storage.init();

    core::persistence::DeviceSettingsStore store(storage);
    core::state::MidiSyncState persisted;
    core::state::MidiNoteDisplayState persistedNoteDisplay;
    assert(store.saveAllStatus(persisted, persistedNoteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);

    const uint8_t invalidConvention = 0xFFU;
    assert(storage.write(
        StorageLayout::ADDR_NOTE_OCTAVE_CONVENTION,
        &invalidConvention,
        sizeof(invalidConvention)
    ) == sizeof(invalidConvention));
    assert(storage.commit());

    core::state::MidiSyncState loadedSync;
    core::state::MidiNoteDisplayState loadedNoteDisplay;
    setSentinel(loadedSync, loadedNoteDisplay);
    assert(!store.load(loadedSync, loadedNoteDisplay));
    assertSync(
        loadedSync,
        core::state::MidiSyncMode::SLAVE,
        false,
        750,
        12
    );
    assert(loadedNoteDisplay.octaveConvention.get() ==
           core::midi::NoteOctaveConvention::C5);

    std::cout
        << "[PASS] invalid note octave convention is rejected atomically\n";
}

void test_reconcile_does_not_rewrite_equal_current_settings() {
    MemoryStorage storage;
    storage.init();

    core::state::MidiSyncState sync;
    core::state::MidiNoteDisplayState noteDisplay;
    setSentinel(sync, noteDisplay);
    core::persistence::DeviceSettingsStore store(storage);
    assert(store.saveAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);
    const int initialCommitCount = storage.commitCount;

    assert(store.reconcileAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);
    assert(store.reconcileAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);
    assert(storage.commitCount == initialCommitCount);

    std::cout << "[PASS] reconcile leaves equal current settings untouched\n";
}

void test_reconcile_replaces_unsupported_settings_once() {
    MemoryStorage storage;
    storage.init();

    const uint32_t magic = StorageLayout::MAGIC;
    const uint8_t unsupportedVersion =
        static_cast<uint8_t>(StorageLayout::VERSION - 1U);
    assert(storage.write(
        StorageLayout::ADDR_MAGIC,
        reinterpret_cast<const uint8_t*>(&magic),
        sizeof(magic)
    ) == sizeof(magic));
    assert(storage.write(
        StorageLayout::ADDR_VERSION,
        &unsupportedVersion,
        sizeof(unsupportedVersion)
    ) == sizeof(unsupportedVersion));
    assert(storage.commit());

    core::state::MidiSyncState sync;
    core::state::MidiNoteDisplayState noteDisplay;
    setSentinel(sync, noteDisplay);
    core::persistence::DeviceSettingsStore store(storage);
    const int unsupportedCommitCount = storage.commitCount;
    assert(store.reconcileAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);
    assert(storage.commitCount == unsupportedCommitCount + 1);

    assert(store.reconcileAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);
    assert(storage.commitCount == unsupportedCommitCount + 1);

    core::state::MidiSyncState loaded;
    core::state::MidiNoteDisplayState loadedNoteDisplay;
    assert(store.load(loaded, loadedNoteDisplay));
    assertSync(
        loaded,
        core::state::MidiSyncMode::SLAVE,
        false,
        750,
        12
    );
    assert(loadedNoteDisplay.octaveConvention.get() ==
           core::midi::NoteOctaveConvention::C5);

    std::cout << "[PASS] reconcile replaces unsupported settings once\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "DeviceSettingsStore tests\n";
    std::cout << "==============================================\n\n";

    test_roundtrip_current_format();
    test_current_format_has_exact_canonical_bytes();
    test_blank_storage_is_initialized_once();
    test_unsupported_version_is_rejected_without_rewrite();
    test_noncanonical_current_payload_is_rejected_atomically();
    test_invalid_note_octave_convention_is_rejected_atomically();
    test_reconcile_does_not_rewrite_equal_current_settings();
    test_reconcile_replaces_unsupported_settings_once();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
