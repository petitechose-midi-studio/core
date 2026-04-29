#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>

#include "../../src/state/CoreSettings.hpp"
#include "../../src/state/CoreSettingsLayout.hpp"

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

private:
    bool initialized_ = false;
    bool dirty_ = false;
    FaultMode fault_mode_ = FaultMode::NONE;
    std::vector<uint8_t> data_;
};

void test_save_all_returns_false_on_short_write() {
    FaultyStorage storage;
    storage.init();
    storage.setFaultMode(FaultyStorage::FaultMode::SHORT_WRITE);

    core::state::MidiSyncState sync;
    constexpr uint16_t sharedTrackEnabledMask = 0x0001;
    constexpr uint8_t sharedTrackActive = 0;

    core::state::CoreSettings settings(storage);
    assert(!settings.saveAll(sync, sharedTrackEnabledMask, sharedTrackActive));
    assert(
        settings.saveAllStatus(sync, sharedTrackEnabledMask, sharedTrackActive) ==
        core::persistence::PersistenceWriteStatus::IO_ERROR
    );

    std::cout << "[PASS] test_save_all_returns_false_on_short_write\n";
}

void test_unavailable_storage_reports_unavailable_statuses() {
    FaultyStorage storage;

    core::state::MidiSyncState sync;
    constexpr uint16_t sharedTrackEnabledMask = 0x0001;
    constexpr uint8_t sharedTrackActive = 0;

    core::state::CoreSettings settings(storage);
    assert(settings.saveAllStatus(sync, sharedTrackEnabledMask, sharedTrackActive) ==
           core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);
    assert(settings.commitStatus() ==
           core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);
    assert(settings.factoryResetStatus() ==
           core::persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE);

    std::cout << "[PASS] test_unavailable_storage_reports_unavailable_statuses\n";
}

void test_commit_failure_propagates() {
    FaultyStorage storage;
    storage.init();
    storage.setFaultMode(FaultyStorage::FaultMode::COMMIT_FAIL);

    core::state::CoreSettings settings(storage);
    assert(!settings.commit());
    assert(settings.commitStatus() == core::persistence::PersistenceWriteStatus::COMMIT_FAILED);

    std::cout << "[PASS] test_commit_failure_propagates\n";
}

void test_factory_reset_failure_propagates() {
    FaultyStorage storage;
    storage.init();
    storage.setFaultMode(FaultyStorage::FaultMode::ERASE_FAIL);

    core::state::CoreSettings settings(storage);
    assert(!settings.factoryReset());
    assert(settings.factoryResetStatus() == core::persistence::PersistenceWriteStatus::ERASE_FAILED);

    std::cout << "[PASS] test_factory_reset_failure_propagates\n";
}

void test_load_shortcuts_returns_false_on_short_read() {
    FaultyStorage storage;
    storage.init();

    namespace StorageLayout = core::state::core_settings::layout;

    uint32_t magic = StorageLayout::MAGIC;
    const uint8_t version = StorageLayout::VERSION;
    storage.write(StorageLayout::ADDR_MAGIC,
                  reinterpret_cast<const uint8_t*>(&magic),
                  sizeof(magic));
    storage.write(StorageLayout::ADDR_VERSION, &version, 1);
    storage.commit();

    storage.setFaultMode(FaultyStorage::FaultMode::SHORT_READ);

    core::state::CoreSettings settings(storage);

    uint8_t macroLeft = 0;
    uint8_t macroRight = 0;
    uint8_t seqLeft = 0;
    uint8_t seqRight = 0;

    assert(!settings.loadDataManagerShortcuts(macroLeft, macroRight, seqLeft, seqRight));
    assert(macroLeft == StorageLayout::DEFAULT_SHORTCUT_MACRO_LEFT);
    assert(macroRight == StorageLayout::DEFAULT_SHORTCUT_MACRO_RIGHT);
    assert(seqLeft == StorageLayout::DEFAULT_SHORTCUT_SEQ_LEFT);
    assert(seqRight == StorageLayout::DEFAULT_SHORTCUT_SEQ_RIGHT);

    std::cout << "[PASS] test_load_shortcuts_returns_false_on_short_read\n";
}

}  // namespace

int main() {
    test_save_all_returns_false_on_short_write();
    test_unavailable_storage_reports_unavailable_statuses();
    test_commit_failure_propagates();
    test_factory_reset_failure_propagates();
    test_load_shortcuts_returns_false_on_short_read();
    std::cout << "\nAll CoreSettings failure tests passed.\n";
    return 0;
}
