#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>

#include "../../src/state/CoreSettings.hpp"

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

private:
    bool initialized_ = false;
    bool dirty_ = false;
    std::vector<uint8_t> data_;
};

void test_roundtrip_v2() {
    MemoryStorage storage;
    storage.init();

    core::state::macro::MacroPagesState pages;
    pages.initDefaults();
    pages.activePage = 3;
    pages.pages[3].cc[1] = 74;
    pages.pages[3].channel[1] = 9;
    pages.pages[3].values[1] = 0.42f;

    core::state::MidiSyncState sync;
    sync.mode.set(core::state::MidiSyncMode::SLAVE);
    sync.followTransport.set(false);
    sync.autoFallbackMs.set(750);
    sync.autoLockClockCount.set(12);

    core::state::CoreSettings settings(storage);
    settings.saveAll(pages, sync);

    core::state::macro::MacroPagesState loadedPages;
    core::state::MidiSyncState loadedSync;
    const bool loaded = settings.load(loadedPages, loadedSync);

    assert(loaded);
    assert(loadedPages.activePage == 3);
    assert(loadedPages.pages[3].cc[1] == 74);
    assert(loadedPages.pages[3].channel[1] == 9);
    assert(loadedPages.pages[3].values[1] == 0.42f);

    assert(loadedSync.mode.get() == core::state::MidiSyncMode::SLAVE);
    assert(!loadedSync.followTransport.get());
    assert(loadedSync.autoFallbackMs.get() == 750);
    assert(loadedSync.autoLockClockCount.get() == 12);

    std::cout << "[PASS] test_roundtrip_v2\n";
}

void test_migration_v1_to_v2() {
    MemoryStorage storage;
    storage.init();

    core::state::macro::MacroPagesState pagesV1;
    pagesV1.initDefaults();
    pagesV1.activePage = 2;
    pagesV1.pages[2].cc[0] = 11;
    pagesV1.pages[2].channel[0] = 3;
    pagesV1.pages[2].values[0] = 0.9f;

    uint32_t magic = core::state::StorageLayout::MAGIC;
    const uint8_t version = 1;
    storage.write(core::state::StorageLayout::ADDR_MAGIC, reinterpret_cast<const uint8_t*>(&magic), sizeof(magic));
    storage.write(core::state::StorageLayout::ADDR_VERSION, &version, 1);
    storage.write(core::state::StorageLayout::ADDR_ACTIVE_PAGE, &pagesV1.activePage, 1);
    for (uint8_t i = 0; i < core::state::macro::PAGE_COUNT; ++i) {
        storage.write(core::state::StorageLayout::pageOffset(i),
                      reinterpret_cast<const uint8_t*>(&pagesV1.pages[i]),
                      core::state::StorageLayout::MACRO_PAGE_SIZE);
    }
    storage.commit();

    core::state::CoreSettings settings(storage);
    core::state::macro::MacroPagesState loadedPages;
    core::state::MidiSyncState loadedSync;
    const bool loaded = settings.load(loadedPages, loadedSync);

    assert(loaded);
    assert(loadedPages.activePage == 2);
    assert(loadedPages.pages[2].cc[0] == 11);
    assert(loadedPages.pages[2].channel[0] == 3);
    assert(loadedPages.pages[2].values[0] == 0.9f);

    assert(loadedSync.mode.get() == core::state::MidiSyncMode::AUTO);
    assert(loadedSync.followTransport.get());
    assert(loadedSync.autoFallbackMs.get() == 500);
    assert(loadedSync.autoLockClockCount.get() == 6);

    uint8_t migratedVersion = 0;
    storage.read(core::state::StorageLayout::ADDR_VERSION, &migratedVersion, 1);
    assert(migratedVersion == core::state::StorageLayout::VERSION);

    std::cout << "[PASS] test_migration_v1_to_v2\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "CoreSettings tests\n";
    std::cout << "==============================================\n\n";

    test_roundtrip_v2();
    test_migration_v1_to_v2();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
