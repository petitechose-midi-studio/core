#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "../../src/persistence/PersistenceSlotFileStore.hpp"
#include "../support/MemoryStorage.hpp"

namespace {
using test_support::MemoryStorage;

core::persistence::SlotFileStoreConfig makeConfig() {
    core::persistence::SlotFileStoreConfig config{};
    config.fileMagic = 0x50535431;
    config.domainVersion = 1;
    config.slotCount = 4;
    config.slotPayloadSize = 32;
    return config;
}

std::vector<uint8_t> readRegion(MemoryStorage& storage, uint32_t address, size_t size) {
    std::vector<uint8_t> bytes(size);
    assert(storage.read(address, bytes.data(), bytes.size()) == bytes.size());
    return bytes;
}

void test_init_formats_empty_storage() {
    MemoryStorage storage;
    storage.init();

    core::persistence::PersistenceSlotFileStore store(storage, makeConfig());
    const bool initialized = store.init();
    assert(initialized);
    assert(storage.commitCount == 2);

    uint8_t payload[32] = {};
    const auto status = store.loadSlot(0, payload, sizeof(payload));
    assert(status == core::persistence::SlotLoadStatus::EMPTY);

    std::cout << "[PASS] test_init_formats_empty_storage\n";
}

void test_save_load_roundtrip() {
    MemoryStorage storage;
    storage.init();

    core::persistence::PersistenceSlotFileStore store(storage, makeConfig());
    assert(store.init());

    const uint8_t original[] = {1, 2, 3, 4, 5, 9, 42, 99};
    assert(store.saveSlot(2, original, sizeof(original), 7));

    uint8_t loaded[32] = {0};
    core::persistence::SlotMetadata meta{};
    const auto status = store.loadSlot(2, loaded, sizeof(loaded), &meta);

    assert(status == core::persistence::SlotLoadStatus::OK);
    assert(meta.payloadSize == sizeof(original));
    assert(meta.saveCounter == 7);
    assert(std::memcmp(original, loaded, sizeof(original)) == 0);

    std::cout << "[PASS] test_save_load_roundtrip\n";
}

void test_unavailable_storage_reports_unavailable_statuses() {
    MemoryStorage storage;
    core::persistence::PersistenceSlotFileStore store(storage, makeConfig());

    assert(!store.init(false));
    assert(store.formatStatus() == core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);

    const uint8_t payload[] = {1, 2, 3};
    assert(store.saveSlotStatus(0, payload, sizeof(payload), 1) ==
           core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);

    uint8_t loaded[32] = {};
    assert(store.loadSlot(0, loaded, sizeof(loaded)) ==
           core::persistence::SlotLoadStatus::STORAGE_UNAVAILABLE);

    const auto latest = store.loadLatest(loaded, sizeof(loaded));
    assert(latest.status == core::persistence::SlotLoadStatus::STORAGE_UNAVAILABLE);

    assert(store.eraseSlotStatus(0) ==
           core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);

    std::cout << "[PASS] test_unavailable_storage_reports_unavailable_statuses\n";
}

void test_crc_mismatch_detected() {
    MemoryStorage storage;
    storage.init();

    core::persistence::PersistenceSlotFileStore store(storage, makeConfig());
    assert(store.init());

    const uint8_t original[] = {10, 20, 30, 40};
    assert(store.saveSlot(1, original, sizeof(original), 3));

    const uint32_t payloadAddress = store.slotPayloadAddress(1);
    uint8_t corrupted = 0xEE;
    storage.write(payloadAddress, &corrupted, 1);

    uint8_t loaded[32] = {0};
    const auto status = store.loadSlot(1, loaded, sizeof(loaded));
    assert(status == core::persistence::SlotLoadStatus::CRC_MISMATCH);

    std::cout << "[PASS] test_crc_mismatch_detected\n";
}

void test_load_latest_picks_newest_valid_slot() {
    MemoryStorage storage;
    storage.init();

    core::persistence::PersistenceSlotFileStore store(storage, makeConfig());
    assert(store.init());

    const uint8_t oldPayload[] = {0x11, 0x22};
    const uint8_t newPayload[] = {0x99, 0x77, 0x55};

    assert(store.saveSlot(0, oldPayload, sizeof(oldPayload), 2));
    assert(store.saveSlot(3, newPayload, sizeof(newPayload), 9));

    uint8_t loaded[32] = {0};
    const auto latest = store.loadLatest(loaded, sizeof(loaded));
    assert(latest.status == core::persistence::SlotLoadStatus::OK);
    assert(latest.slotIndex == 3);
    assert(latest.metadata.saveCounter == 9);
    assert(latest.metadata.payloadSize == sizeof(newPayload));
    assert(std::memcmp(loaded, newPayload, sizeof(newPayload)) == 0);

    std::cout << "[PASS] test_load_latest_picks_newest_valid_slot\n";
}

void test_load_latest_falls_back_when_newest_is_corrupted() {
    MemoryStorage storage;
    storage.init();

    core::persistence::PersistenceSlotFileStore store(storage, makeConfig());
    assert(store.init());

    const uint8_t stable[] = {1, 2, 3};
    const uint8_t newest[] = {7, 8, 9, 10};

    assert(store.saveSlot(0, stable, sizeof(stable), 4));
    assert(store.saveSlot(1, newest, sizeof(newest), 9));

    const uint32_t newestPayload = store.slotPayloadAddress(1);
    uint8_t bad = 0x00;
    storage.write(newestPayload + 1, &bad, 1);

    uint8_t loaded[32] = {0};
    const auto latest = store.loadLatest(loaded, sizeof(loaded));

    assert(latest.status == core::persistence::SlotLoadStatus::OK);
    assert(latest.slotIndex == 0);
    assert(latest.metadata.saveCounter == 4);
    assert(latest.metadata.payloadSize == sizeof(stable));
    assert(std::memcmp(loaded, stable, sizeof(stable)) == 0);

    std::cout << "[PASS] test_load_latest_falls_back_when_newest_is_corrupted\n";
}

void test_roundtrips_payload_larger_than_uint16() {
    constexpr uint32_t kPayloadSize = 70000;
    auto config = makeConfig();
    config.slotCount = 2;
    config.slotPayloadSize = kPayloadSize;
    MemoryStorage storage(
        core::persistence::PersistenceSlotFileStore::requiredCapacity(
            config.slotCount,
            config.slotPayloadSize
        )
    );
    storage.init();

    core::persistence::PersistenceSlotFileStore store(storage, config);
    assert(store.init());
    assert(store.slotPayloadSize() == kPayloadSize);

    std::vector<uint8_t> source(kPayloadSize);
    for (uint32_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<uint8_t>(i * 17U);
    }
    assert(store.saveSlot(1, source.data(), source.size(), 42));

    std::vector<uint8_t> loaded(kPayloadSize);
    core::persistence::SlotMetadata metadata{};
    assert(store.loadSlot(1, loaded.data(), loaded.size(), &metadata) ==
           core::persistence::SlotLoadStatus::OK);
    assert(metadata.payloadSize == kPayloadSize);
    assert(metadata.saveCounter == 42);
    assert(loaded == source);

    std::cout << "[PASS] test_roundtrips_payload_larger_than_uint16\n";
}

void test_nonzero_base_addresses_are_bank_relative_and_bounded() {
    auto config = makeConfig();
    config.baseAddress = 73;
    const size_t bankCapacity =
        core::persistence::PersistenceSlotFileStore::requiredCapacity(
            config.slotCount,
            config.slotPayloadSize
        );
    MemoryStorage storage(config.baseAddress + bankCapacity + 41);
    storage.init();

    const std::vector<uint8_t> prefix(73, 0xA5);
    const std::vector<uint8_t> suffix(41, 0x5A);
    assert(storage.write(0, prefix.data(), prefix.size()) == prefix.size());
    assert(storage.write(
               static_cast<uint32_t>(config.baseAddress + bankCapacity),
               suffix.data(),
               suffix.size()
           ) == suffix.size());
    assert(storage.commit());

    core::persistence::PersistenceSlotFileStore store(storage, config);
    assert(store.baseAddress() == config.baseAddress);
    assert(store.bankCapacity() == bankCapacity);
    assert(store.slotHeaderAddress(0) ==
           config.baseAddress + core::persistence::PersistenceSlotFileStore::FILE_HEADER_SIZE);
    assert(store.init());

    const uint8_t payload[] = {8, 6, 7, 5, 3, 0, 9};
    assert(store.saveSlot(3, payload, sizeof(payload), 11));
    uint8_t loaded[32] = {};
    assert(store.loadSlot(3, loaded, sizeof(loaded)) ==
           core::persistence::SlotLoadStatus::OK);
    assert(std::memcmp(payload, loaded, sizeof(payload)) == 0);

    assert(readRegion(storage, 0, prefix.size()) == prefix);
    assert(readRegion(
               storage,
               static_cast<uint32_t>(config.baseAddress + bankCapacity),
               suffix.size()
           ) == suffix);

    std::cout << "[PASS] test_nonzero_base_addresses_are_bank_relative_and_bounded\n";
}

void test_layout_probe_is_read_only_and_exposes_only_crc_verified_layout() {
    using ProbeStatus = core::persistence::SlotFileLayoutProbeStatus;

    auto config = makeConfig();
    config.baseAddress = 96;
    const size_t bankCapacity =
        core::persistence::PersistenceSlotFileStore::requiredCapacity(
            config.slotCount,
            config.slotPayloadSize
        );
    MemoryStorage storage(config.baseAddress + bankCapacity);
    storage.init();

    const auto empty = core::persistence::PersistenceSlotFileStore::probeLayout(
        storage,
        config.baseAddress
    );
    assert(empty.status == ProbeStatus::EMPTY);
    assert(empty.config.fileMagic == 0);
    assert(storage.commitCount == 0);

    core::persistence::PersistenceSlotFileStore store(storage, config);
    assert(store.format());
    const int commitsAfterFormat = storage.commitCount;
    const auto valid = core::persistence::PersistenceSlotFileStore::probeLayout(
        storage,
        config.baseAddress
    );
    assert(valid.status == ProbeStatus::VALID);
    assert(valid.config.baseAddress == config.baseAddress);
    assert(valid.config.fileMagic == config.fileMagic);
    assert(valid.config.domainVersion == config.domainVersion);
    assert(valid.config.slotCount == config.slotCount);
    assert(valid.config.slotPayloadSize == config.slotPayloadSize);
    assert(storage.commitCount == commitsAfterFormat);

    auto expected = config;
    ++expected.domainVersion;
    core::persistence::PersistenceSlotFileStore mismatchedStore(storage, expected);
    const auto mismatch = mismatchedStore.probe();
    assert(mismatch.status == ProbeStatus::MISMATCH);
    assert(mismatch.config.domainVersion == config.domainVersion);

    uint8_t corrupt = 0;
    assert(storage.write(config.baseAddress + 15, &corrupt, 1) == 1);
    const auto corruptProbe = core::persistence::PersistenceSlotFileStore::probeLayout(
        storage,
        config.baseAddress
    );
    assert(corruptProbe.status == ProbeStatus::MISMATCH);
    assert(corruptProbe.config.fileMagic == 0);
    assert(corruptProbe.config.slotCount == 0);

    MemoryStorage unavailable;
    assert(core::persistence::PersistenceSlotFileStore::probeLayout(unavailable, 0).status ==
           ProbeStatus::IO);
    MemoryStorage tooSmall(
        core::persistence::PersistenceSlotFileStore::FILE_HEADER_SIZE - 1
    );
    tooSmall.init();
    assert(core::persistence::PersistenceSlotFileStore::probeLayout(tooSmall, 0).status ==
           ProbeStatus::CAPACITY);

    std::cout
        << "[PASS] test_layout_probe_is_read_only_and_exposes_only_crc_verified_layout\n";
}

void test_header_last_publication_preserves_neighbor_bank() {
    using ProbeStatus = core::persistence::SlotFileLayoutProbeStatus;
    using WriteStatus = core::persistence::PersistenceWriteStatus;

    auto firstConfig = makeConfig();
    const size_t bankCapacity =
        core::persistence::PersistenceSlotFileStore::requiredCapacity(
            firstConfig.slotCount,
            firstConfig.slotPayloadSize
        );
    auto secondConfig = makeConfig();
    secondConfig.baseAddress = static_cast<uint32_t>(bankCapacity);
    secondConfig.fileMagic = 0x50535432;
    secondConfig.domainVersion = 2;

    MemoryStorage storage(bankCapacity * 2);
    storage.init();
    core::persistence::PersistenceSlotFileStore first(storage, firstConfig);
    assert(first.format());
    const uint8_t firstPayload[] = {0x11, 0x22, 0x33};
    assert(first.saveSlot(1, firstPayload, sizeof(firstPayload), 4));
    const auto firstBankBefore = readRegion(storage, 0, bankCapacity);

    std::vector<uint8_t> stale(bankCapacity, 0xA5);
    assert(storage.write(
               secondConfig.baseAddress,
               stale.data(),
               stale.size()
           ) == stale.size());
    assert(storage.commit());

    core::persistence::PersistenceSlotFileStore second(storage, secondConfig);
    const int commitsBeforeErase = storage.commitCount;
    assert(second.eraseUnpublishedBankStatus() == WriteStatus::OK);
    assert(storage.commitCount == commitsBeforeErase + 1);
    assert(readRegion(storage, secondConfig.baseAddress, bankCapacity) ==
           std::vector<uint8_t>(bankCapacity, 0xFF));
    assert(core::persistence::PersistenceSlotFileStore::probeLayout(
               storage,
               secondConfig.baseAddress
           ).status == ProbeStatus::EMPTY);

    const uint8_t secondPayload[] = {0x91, 0x82, 0x73, 0x64};
    assert(second.saveSlotStatus(2, secondPayload, sizeof(secondPayload), 9) ==
           WriteStatus::OK);
    // Slot data can be built and verified while the bank remains unpublished.
    assert(core::persistence::PersistenceSlotFileStore::probeLayout(
               storage,
               secondConfig.baseAddress
           ).status == ProbeStatus::EMPTY);
    uint8_t loaded[32] = {};
    assert(second.loadSlot(2, loaded, sizeof(loaded)) ==
           core::persistence::SlotLoadStatus::OK);
    assert(std::memcmp(loaded, secondPayload, sizeof(secondPayload)) == 0);

    const int commitsBeforePublish = storage.commitCount;
    assert(second.publishHeaderStatus() == WriteStatus::OK);
    assert(storage.commitCount == commitsBeforePublish + 1);
    assert(second.probe().status == ProbeStatus::VALID);
    assert(readRegion(storage, 0, bankCapacity) == firstBankBefore);

    std::cout << "[PASS] test_header_last_publication_preserves_neighbor_bank\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "PersistenceSlotFileStore tests\n";
    std::cout << "==============================================\n\n";

    test_init_formats_empty_storage();
    test_save_load_roundtrip();
    test_unavailable_storage_reports_unavailable_statuses();
    test_crc_mismatch_detected();
    test_load_latest_picks_newest_valid_slot();
    test_load_latest_falls_back_when_newest_is_corrupted();
    test_roundtrips_payload_larger_than_uint16();
    test_nonzero_base_addresses_are_bank_relative_and_bounded();
    test_layout_probe_is_read_only_and_exposes_only_crc_verified_layout();
    test_header_last_publication_preserves_neighbor_bank();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
