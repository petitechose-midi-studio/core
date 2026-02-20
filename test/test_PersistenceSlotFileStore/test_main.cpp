#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>

#include "../../src/persistence/PersistenceSlotFileStore.hpp"

namespace {

class MemoryStorage : public oc::interface::IStorage {
public:
    explicit MemoryStorage(size_t capacity = 4096)
        : data_(capacity, 0xFF) {}

    oc::type::Result<void> init() override {
        initialized_ = true;
        return oc::type::Result<void>::ok();
    }

    bool available() const override { return initialized_; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        const size_t n = (size <= maxSize) ? size : maxSize;
        std::memcpy(buffer, data_.data() + address, n);
        return n;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        const size_t n = (size <= maxSize) ? size : maxSize;
        std::memcpy(data_.data() + address, buffer, n);
        dirty_ = true;
        return n;
    }

    bool commit() override {
        dirty_ = false;
        ++commitCount;
        return true;
    }

    bool erase(uint32_t address, size_t size) override {
        if (address >= data_.size()) return false;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        const size_t n = (size <= maxSize) ? size : maxSize;
        std::memset(data_.data() + address, 0xFF, n);
        dirty_ = true;
        return true;
    }

    size_t capacity() const override { return data_.size(); }
    bool isDirty() const override { return dirty_; }

    int commitCount = 0;

private:
    bool initialized_ = false;
    bool dirty_ = false;
    std::vector<uint8_t> data_;
};

core::persistence::SlotFileStoreConfig makeConfig() {
    core::persistence::SlotFileStoreConfig config{};
    config.fileMagic = 0x50535431;
    config.domainVersion = 1;
    config.slotCount = 4;
    config.slotPayloadSize = 32;
    return config;
}

void test_init_formats_empty_storage() {
    MemoryStorage storage;
    storage.init();

    core::persistence::PersistenceSlotFileStore store(storage, makeConfig());
    const bool initialized = store.init();
    assert(initialized);
    assert(storage.commitCount == 1);

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

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "PersistenceSlotFileStore tests\n";
    std::cout << "==============================================\n\n";

    test_init_formats_empty_storage();
    test_save_load_roundtrip();
    test_crc_mismatch_detected();
    test_load_latest_picks_newest_valid_slot();
    test_load_latest_falls_back_when_newest_is_corrupted();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
