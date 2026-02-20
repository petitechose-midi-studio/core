#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>
#include <oc/state/NotificationQueue.hpp>

#include "../../src/state/CoreState.hpp"

namespace {

class MemoryStorage : public oc::interface::IStorage {
public:
    explicit MemoryStorage(size_t capacity = 128 * 1024)
        : data_(capacity, 0xFF) {}

    oc::type::Result<void> init() override {
        initialized_ = true;
        return oc::type::Result<void>::ok();
    }

    bool available() const override { return initialized_; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t n = std::min(size, data_.size() - static_cast<size_t>(address));
        std::memcpy(buffer, data_.data() + address, n);
        return n;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t n = std::min(size, data_.size() - static_cast<size_t>(address));
        std::memcpy(data_.data() + address, buffer, n);
        return n;
    }

    bool commit() override { return true; }

    bool erase(uint32_t address, size_t size) override {
        if (address >= data_.size()) return false;
        const size_t n = std::min(size, data_.size() - static_cast<size_t>(address));
        std::memset(data_.data() + address, 0xFF, n);
        return true;
    }

    size_t capacity() const override { return data_.size(); }

private:
    bool initialized_ = false;
    std::vector<uint8_t> data_;
};

void test_workspace_survives_legacy_corruption() {
    MemoryStorage storage;
    storage.init();

    {
        core::state::CoreState state(storage);
        state.setMacroValue(0, 0.13f);
        state.setMacroValue(1, 0.87f);
        oc::state::NotificationQueue::instance().flush();
        state.flush();
    }

    // Corrupt legacy CoreSettings area only (workspace slices start at 0x1000).
    storage.erase(0, 0x0200);

    core::state::CoreState restored(storage);
    assert(restored.getMacroValue(0) == 0.13f);
    assert(restored.getMacroValue(1) == 0.87f);

    std::cout << "[PASS] test_workspace_survives_legacy_corruption\n";
}

void test_macro_library_roundtrip_and_erase() {
    MemoryStorage storage;
    storage.init();

    core::state::CoreState state(storage);
    state.switchToPage(2);
    state.setMacroConfig(0, 4, 88);
    state.setMacroValue(0, 0.64f);
    oc::state::NotificationQueue::instance().flush();
    state.flush();

    assert(state.saveMacroLibrarySlot(3));

    state.setMacroConfig(0, 0, 1);
    state.setMacroValue(0, 0.01f);
    oc::state::NotificationQueue::instance().flush();
    state.flush();

    const auto status = state.loadMacroLibrarySlot(3);
    assert(status == core::persistence::SlotLoadStatus::OK);
    assert(state.pages.activePage == 2);
    assert(state.getMacroConfig(0).channel == 4);
    assert(state.getMacroConfig(0).cc == 88);
    assert(state.getMacroValue(0) == 0.64f);

    assert(state.eraseMacroLibrarySlot(3));
    const auto erasedStatus = state.loadMacroLibrarySlot(3);
    assert(erasedStatus == core::persistence::SlotLoadStatus::EMPTY);

    std::cout << "[PASS] test_macro_library_roundtrip_and_erase\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "CoreState persistence tests\n";
    std::cout << "==============================================\n\n";

    test_workspace_survives_legacy_corruption();
    test_macro_library_roundtrip_and_erase();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
