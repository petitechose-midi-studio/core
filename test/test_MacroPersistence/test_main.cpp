#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "../../src/persistence/MacroPersistence.hpp"
#include "../support/MemoryStorage.hpp"

namespace {
using test_support::MemoryStorage;

#pragma pack(push, 1)
struct SlotFileHeaderRaw {
    uint32_t magic = 0;
    uint8_t formatVersion = 0;
    uint8_t domainVersion = 0;
    uint16_t slotCount = 0;
    uint16_t slotPayloadSize = 0;
    uint16_t reserved0 = 0;
    uint32_t layoutCrc32 = 0;
    uint32_t reserved1 = 0;
    uint32_t reserved2 = 0;
};
#pragma pack(pop)

static_assert(sizeof(SlotFileHeaderRaw) == 24, "Unexpected slot file header size");

uint32_t workspaceSlotPayloadAddress(MemoryStorage& storage, uint16_t slotIndex) {
    SlotFileHeaderRaw header{};
    const size_t readBytes = storage.read(0, reinterpret_cast<uint8_t*>(&header), sizeof(header));
    assert(readBytes == sizeof(header));
    assert(slotIndex < header.slotCount);

    constexpr uint32_t SLOT_HEADER_SIZE = 16;
    const uint32_t slotSize = SLOT_HEADER_SIZE + header.slotPayloadSize;
    return static_cast<uint32_t>(sizeof(header)) +
           static_cast<uint32_t>(slotIndex) * slotSize +
           SLOT_HEADER_SIZE;
}

void configureState(core::state::macro::MacroPagesState& pages,
                    uint8_t activePage,
                    uint8_t cc,
                    float value) {
    pages.initDefaults();
    pages.activePage = activePage;
    pages.pages[activePage].cc[0] = cc;
    pages.pages[activePage].channel[0] = static_cast<uint8_t>(activePage % 16);
    pages.pages[activePage].values[0] = value;
    pages.updateActiveConfigs();
}

void assertStateEquals(const core::state::macro::MacroPagesState& pages,
                       uint8_t expectedPage,
                       uint8_t expectedCc,
                       float expectedValue) {
    assert(pages.activePage == expectedPage);
    assert(pages.pages[expectedPage].cc[0] == expectedCc);
    assert(pages.pages[expectedPage].values[0] == expectedValue);
}

void test_workspace_roundtrip() {
    MemoryStorage workspaceStorage;
    MemoryStorage libraryStorage;
    workspaceStorage.init();
    libraryStorage.init();

    core::persistence::MacroPersistence persistence(workspaceStorage, libraryStorage);
    assert(persistence.init());

    core::state::macro::MacroPagesState source;
    configureState(source, 3, 74, 0.42f);
    assert(persistence.saveWorkspace(source));

    core::state::macro::MacroPagesState loaded;
    loaded.initDefaults();
    assert(persistence.loadWorkspace(loaded));

    assertStateEquals(loaded, 3, 74, 0.42f);
    std::cout << "[PASS] test_workspace_roundtrip\n";
}

void test_workspace_load_latest_after_multiple_saves() {
    MemoryStorage workspaceStorage;
    MemoryStorage libraryStorage;
    workspaceStorage.init();
    libraryStorage.init();

    core::persistence::MacroPersistence persistence(workspaceStorage, libraryStorage);
    assert(persistence.init());

    core::state::macro::MacroPagesState first;
    configureState(first, 1, 21, 0.10f);
    assert(persistence.saveWorkspace(first));

    core::state::macro::MacroPagesState second;
    configureState(second, 5, 91, 0.90f);
    assert(persistence.saveWorkspace(second));

    core::state::macro::MacroPagesState loaded;
    loaded.initDefaults();
    assert(persistence.loadWorkspace(loaded));

    assertStateEquals(loaded, 5, 91, 0.90f);
    std::cout << "[PASS] test_workspace_load_latest_after_multiple_saves\n";
}

void test_workspace_falls_back_when_latest_slot_is_corrupted() {
    MemoryStorage workspaceStorage;
    MemoryStorage libraryStorage;
    workspaceStorage.init();
    libraryStorage.init();

    core::persistence::MacroPersistence persistence(workspaceStorage, libraryStorage);
    assert(persistence.init());

    core::state::macro::MacroPagesState first;
    configureState(first, 1, 21, 0.10f);
    assert(persistence.saveWorkspace(first));

    core::state::macro::MacroPagesState second;
    configureState(second, 5, 91, 0.90f);
    assert(persistence.saveWorkspace(second));

    // Two-slot workspace journal: second save lands in slot 1.
    const uint32_t latestPayloadAddress = workspaceSlotPayloadAddress(workspaceStorage, 1);
    const uint8_t badByte = 0x00;
    const size_t written = workspaceStorage.write(latestPayloadAddress, &badByte, 1);
    assert(written == 1);

    core::state::macro::MacroPagesState loaded;
    loaded.initDefaults();
    assert(persistence.loadWorkspace(loaded));

    // Must fall back to older valid slot (first save).
    assertStateEquals(loaded, 1, 21, 0.10f);

    std::cout << "[PASS] test_workspace_falls_back_when_latest_slot_is_corrupted\n";
}

void test_library_save_load_erase_slot() {
    MemoryStorage workspaceStorage;
    MemoryStorage libraryStorage;
    workspaceStorage.init();
    libraryStorage.init();

    core::persistence::MacroPersistence persistence(workspaceStorage, libraryStorage);
    assert(persistence.init());

    core::state::macro::MacroPagesState snapshot;
    configureState(snapshot, 2, 88, 0.75f);

    assert(persistence.saveLibrarySlot(4, snapshot));

    core::state::macro::MacroPagesState loaded;
    loaded.initDefaults();
    const auto status = persistence.loadLibrarySlot(4, loaded);
    assert(status == core::persistence::SlotLoadStatus::OK);
    assertStateEquals(loaded, 2, 88, 0.75f);

    assert(persistence.eraseLibrarySlot(4));
    const auto erasedStatus = persistence.loadLibrarySlot(4, loaded);
    assert(erasedStatus == core::persistence::SlotLoadStatus::EMPTY);

    std::cout << "[PASS] test_library_save_load_erase_slot\n";
}

void test_library_slot_bounds() {
    MemoryStorage workspaceStorage;
    MemoryStorage libraryStorage;
    workspaceStorage.init();
    libraryStorage.init();

    core::persistence::MacroPersistence persistence(workspaceStorage, libraryStorage);
    assert(persistence.init());

    core::state::macro::MacroPagesState pages;
    pages.initDefaults();

    const uint8_t invalidSlot = static_cast<uint8_t>(core::persistence::MacroPersistence::LIBRARY_SLOT_COUNT);
    assert(!persistence.saveLibrarySlot(invalidSlot, pages));
    const auto status = persistence.loadLibrarySlot(invalidSlot, pages);
    assert(status == core::persistence::SlotLoadStatus::OUT_OF_RANGE);
    assert(!persistence.eraseLibrarySlot(invalidSlot));

    std::cout << "[PASS] test_library_slot_bounds\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "MacroPersistence tests\n";
    std::cout << "==============================================\n\n";

    test_workspace_roundtrip();
    test_workspace_load_latest_after_multiple_saves();
    test_workspace_falls_back_when_latest_slot_is_corrupted();
    test_library_save_load_erase_slot();
    test_library_slot_bounds();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
