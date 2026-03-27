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

void test_roundtrip_v3() {
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
    settings.saveDataManagerMacroShortcutLeft(static_cast<uint8_t>(core::state::DataManagerCommand::MACRO_LOAD_SLOT));
    settings.saveDataManagerMacroShortcutRight(static_cast<uint8_t>(core::state::DataManagerCommand::MACRO_ERASE_SLOT));
    settings.saveDataManagerSeqShortcutLeft(static_cast<uint8_t>(core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT));
    settings.saveDataManagerSeqShortcutRight(static_cast<uint8_t>(core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT));
    settings.commit();

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

    uint8_t macroLeft = 0;
    uint8_t macroRight = 0;
    uint8_t seqLeft = 0;
    uint8_t seqRight = 0;
    settings.loadDataManagerShortcuts(macroLeft, macroRight, seqLeft, seqRight);

    assert(macroLeft == static_cast<uint8_t>(core::state::DataManagerCommand::MACRO_LOAD_SLOT));
    assert(macroRight == static_cast<uint8_t>(core::state::DataManagerCommand::MACRO_ERASE_SLOT));
    assert(seqLeft == static_cast<uint8_t>(core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT));
    assert(seqRight == static_cast<uint8_t>(core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT));

    std::cout << "[PASS] test_roundtrip_v3\n";
}

void test_migration_v1_to_v3() {
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
    storage.write(core::state::StorageLayout::ADDR_MAGIC,
                  reinterpret_cast<const uint8_t*>(&magic),
                  sizeof(magic));
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

    uint8_t macroLeft = 0;
    uint8_t macroRight = 0;
    uint8_t seqLeft = 0;
    uint8_t seqRight = 0;
    settings.loadDataManagerShortcuts(macroLeft, macroRight, seqLeft, seqRight);

    assert(macroLeft == core::state::StorageLayout::DEFAULT_SHORTCUT_MACRO_LEFT);
    assert(macroRight == core::state::StorageLayout::DEFAULT_SHORTCUT_MACRO_RIGHT);
    assert(seqLeft == core::state::StorageLayout::DEFAULT_SHORTCUT_SEQ_LEFT);
    assert(seqRight == core::state::StorageLayout::DEFAULT_SHORTCUT_SEQ_RIGHT);

    std::cout << "[PASS] test_migration_v1_to_v3\n";
}

void test_migration_v2_to_v3() {
    MemoryStorage storage;
    storage.init();

    core::state::macro::MacroPagesState pagesV2;
    pagesV2.initDefaults();
    pagesV2.activePage = 4;
    pagesV2.pages[4].cc[2] = 99;
    pagesV2.pages[4].channel[2] = 5;
    pagesV2.pages[4].values[2] = 0.63f;

    uint32_t magic = core::state::StorageLayout::MAGIC;
    const uint8_t version = 2;
    storage.write(core::state::StorageLayout::ADDR_MAGIC,
                  reinterpret_cast<const uint8_t*>(&magic),
                  sizeof(magic));
    storage.write(core::state::StorageLayout::ADDR_VERSION, &version, 1);
    storage.write(core::state::StorageLayout::ADDR_ACTIVE_PAGE, &pagesV2.activePage, 1);

    const uint8_t mode = static_cast<uint8_t>(core::state::MidiSyncMode::MASTER);
    const uint8_t follow = 0;
    const uint16_t fallbackMs = 1500;
    const uint8_t lockCount = 24;
    storage.write(core::state::StorageLayout::ADDR_SYNC_MODE,
                  reinterpret_cast<const uint8_t*>(&mode),
                  1);
    storage.write(core::state::StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT,
                  reinterpret_cast<const uint8_t*>(&follow),
                  1);
    storage.write(core::state::StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                  reinterpret_cast<const uint8_t*>(&fallbackMs),
                  sizeof(fallbackMs));
    storage.write(core::state::StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS,
                  reinterpret_cast<const uint8_t*>(&lockCount),
                  1);

    for (uint8_t i = 0; i < core::state::macro::PAGE_COUNT; ++i) {
        storage.write(core::state::StorageLayout::pageOffset(i),
                      reinterpret_cast<const uint8_t*>(&pagesV2.pages[i]),
                      core::state::StorageLayout::MACRO_PAGE_SIZE);
    }
    storage.commit();

    core::state::CoreSettings settings(storage);
    core::state::macro::MacroPagesState loadedPages;
    core::state::MidiSyncState loadedSync;
    const bool loaded = settings.load(loadedPages, loadedSync);

    assert(loaded);
    assert(loadedPages.activePage == 4);
    assert(loadedPages.pages[4].cc[2] == 99);
    assert(loadedPages.pages[4].channel[2] == 5);
    assert(loadedPages.pages[4].values[2] == 0.63f);
    assert(loadedSync.mode.get() == core::state::MidiSyncMode::MASTER);
    assert(!loadedSync.followTransport.get());
    assert(loadedSync.autoFallbackMs.get() == 1500);
    assert(loadedSync.autoLockClockCount.get() == 24);

    uint8_t migratedVersion = 0;
    storage.read(core::state::StorageLayout::ADDR_VERSION, &migratedVersion, 1);
    assert(migratedVersion == core::state::StorageLayout::VERSION);

    uint8_t macroLeft = 0;
    uint8_t macroRight = 0;
    uint8_t seqLeft = 0;
    uint8_t seqRight = 0;
    settings.loadDataManagerShortcuts(macroLeft, macroRight, seqLeft, seqRight);

    assert(macroLeft == core::state::StorageLayout::DEFAULT_SHORTCUT_MACRO_LEFT);
    assert(macroRight == core::state::StorageLayout::DEFAULT_SHORTCUT_MACRO_RIGHT);
    assert(seqLeft == core::state::StorageLayout::DEFAULT_SHORTCUT_SEQ_LEFT);
    assert(seqRight == core::state::StorageLayout::DEFAULT_SHORTCUT_SEQ_RIGHT);

    std::cout << "[PASS] test_migration_v2_to_v3\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "CoreSettings tests\n";
    std::cout << "==============================================\n\n";

    test_roundtrip_v3();
    test_migration_v1_to_v3();
    test_migration_v2_to_v3();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
