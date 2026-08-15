#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>

#include "../../src/persistence/DeviceSettingsStorageLayout.hpp"
#include "../../src/persistence/DeviceSettingsStore.hpp"

namespace {

class FaultyStorage : public oc::interface::IStorage {
public:
    enum class FaultMode {
        NONE,
        SHORT_READ,
        SHORT_WRITE,
        COMMIT_FAIL,
        ERASE_FAIL,
    };

    explicit FaultyStorage(size_t capacity = 4096)
        : data_(capacity, 0xFF) {}

    oc::type::Result<void> init() override {
        initialized_ = true;
        return oc::type::Result<void>::ok();
    }

    bool available() const override { return initialized_; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        size_t n = (size <= maxSize) ? size : maxSize;
        if (fault_mode_ == FaultMode::SHORT_READ && n > 0) {
            n -= 1;
        }
        if (n > 0) {
            std::memcpy(buffer, data_.data() + address, n);
        }
        return n;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        size_t n = (size <= maxSize) ? size : maxSize;
        if (fault_mode_ == FaultMode::SHORT_WRITE && n > 0) {
            n -= 1;
        }
        if (n > 0) {
            std::memcpy(data_.data() + address, buffer, n);
        }
        dirty_ = true;
        return n;
    }

    bool commit() override {
        if (fault_mode_ == FaultMode::COMMIT_FAIL) {
            return false;
        }
        dirty_ = false;
        ++commitCount;
        return true;
    }

    bool erase(uint32_t address, size_t size) override {
        if (fault_mode_ == FaultMode::ERASE_FAIL) {
            return false;
        }
        if (address >= data_.size()) return false;
        const size_t maxSize = data_.size() - static_cast<size_t>(address);
        const size_t n = (size <= maxSize) ? size : maxSize;
        std::memset(data_.data() + address, 0xFF, n);
        dirty_ = true;
        return true;
    }

    size_t capacity() const override { return data_.size(); }
    bool isDirty() const override { return dirty_; }

    void setFaultMode(FaultMode mode) { fault_mode_ = mode; }
    int commitCount = 0;

private:
    bool initialized_ = false;
    bool dirty_ = false;
    FaultMode fault_mode_ = FaultMode::NONE;
    std::vector<uint8_t> data_;
};

void test_save_all_reports_short_write() {
    FaultyStorage storage;
    storage.init();
    storage.setFaultMode(FaultyStorage::FaultMode::SHORT_WRITE);

    core::state::MidiSyncState sync;
    core::state::MidiNoteDisplayState noteDisplay;

    core::persistence::DeviceSettingsStore store(storage);
    assert(
        store.saveAllStatus(sync, noteDisplay) ==
        core::persistence::PersistenceWriteStatus::IO_ERROR
    );

    std::cout << "[PASS] test_save_all_reports_short_write\n";
}

void test_short_read_rejects_without_publishing_partial_state() {
    FaultyStorage storage;
    storage.init();

    core::persistence::DeviceSettingsStore store(storage);
    core::state::MidiSyncState persisted;
    core::state::MidiNoteDisplayState persistedNoteDisplay;
    persisted.mode.set(core::state::MidiSyncMode::SLAVE);
    persisted.followTransport.set(false);
    persisted.autoFallbackMs.set(750U);
    persisted.autoLockClockCount.set(12U);
    assert(store.saveAllStatus(persisted, persistedNoteDisplay) ==
           core::persistence::PersistenceWriteStatus::OK);

    core::state::MidiSyncState loaded;
    core::state::MidiNoteDisplayState loadedNoteDisplay;
    storage.setFaultMode(FaultyStorage::FaultMode::SHORT_READ);
    assert(!store.load(loaded, loadedNoteDisplay));
    assert(loaded.mode.get() == core::state::MidiSyncMode::AUTO);
    assert(loaded.followTransport.get());
    assert(loaded.autoFallbackMs.get() == 500U);
    assert(loaded.autoLockClockCount.get() == 6U);

    std::cout
        << "[PASS] test_short_read_rejects_without_publishing_partial_state\n";
}

void test_unavailable_storage_reports_unavailable_statuses() {
    FaultyStorage storage;

    core::state::MidiSyncState sync;
    core::state::MidiNoteDisplayState noteDisplay;

    core::persistence::DeviceSettingsStore store(storage);
    assert(store.saveAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);
    assert(store.commitStatus() ==
           core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);
    assert(store.factoryResetStatus() ==
           core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);

    std::cout << "[PASS] test_unavailable_storage_reports_unavailable_statuses\n";
}

void test_noncanonical_values_are_rejected_before_io() {
    FaultyStorage storage;
    storage.init();

    core::persistence::DeviceSettingsStore store(storage);
    core::state::MidiSyncState sync;
    core::state::MidiNoteDisplayState noteDisplay;
    sync.autoFallbackMs.set(501U);

    assert(store.saveAllStatus(sync, noteDisplay) ==
           core::persistence::PersistenceWriteStatus::INVALID_CONFIG);
    assert(storage.commitCount == 0);

    std::cout << "[PASS] test_noncanonical_values_are_rejected_before_io\n";
}

void test_commit_failure_propagates() {
    FaultyStorage storage;
    storage.init();
    storage.setFaultMode(FaultyStorage::FaultMode::COMMIT_FAIL);

    core::persistence::DeviceSettingsStore store(storage);
    assert(store.commitStatus() == core::persistence::PersistenceWriteStatus::COMMIT_FAILED);

    std::cout << "[PASS] test_commit_failure_propagates\n";
}

void test_factory_reset_failure_propagates() {
    FaultyStorage storage;
    storage.init();
    storage.setFaultMode(FaultyStorage::FaultMode::ERASE_FAIL);

    core::persistence::DeviceSettingsStore store(storage);
    assert(store.factoryResetStatus() == core::persistence::PersistenceWriteStatus::ERASE_FAILED);

    std::cout << "[PASS] test_factory_reset_failure_propagates\n";
}

}  // namespace

int main() {
    test_save_all_reports_short_write();
    test_short_read_rejects_without_publishing_partial_state();
    test_unavailable_storage_reports_unavailable_statuses();
    test_noncanonical_values_are_rejected_before_io();
    test_commit_failure_propagates();
    test_factory_reset_failure_propagates();
    std::cout << "\nAll DeviceSettingsStore failure tests passed.\n";
    return 0;
}
