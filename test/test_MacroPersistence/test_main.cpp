#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "../../src/persistence/MacroPersistence.hpp"
#include "../support/MemoryStorage.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {
using test_support::MemoryStorage;

void configureState(core::state::macro::MacroPagesState& pages,
                    uint8_t activePage,
                    uint8_t cc,
                    float value) {
    pages.initDefaults();
    pages.syncSharedTrackState(pages.currentTrackEnabledMask(), 0);
    pages.setActivePage(activePage);
    auto& page = pages.pageData(0, activePage);
    page.cc[0] = cc;
    page.values[0] = value;
    pages.updateActiveConfigs();
}

void assertStateEquals(const core::state::macro::MacroPagesState& pages,
                       uint8_t expectedPage,
                       uint8_t expectedCc,
                       float expectedValue) {
    assert(pages.currentActiveTrack() == 0);
    assert(pages.activeTrackData().activePage == expectedPage);
    const auto& page = pages.pageData(0, expectedPage);
    assert(page.cc[0] == expectedCc);
    assert(page.values[0] == expectedValue);
}

void test_library_save_load_erase_slot() {
    MemoryStorage libraryStorage;
    libraryStorage.init();

    core::persistence::MacroPersistence persistence(libraryStorage);
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
    MemoryStorage libraryStorage;
    libraryStorage.init();

    core::persistence::MacroPersistence persistence(libraryStorage);
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

void test_library_load_preserves_project_macro_automation_bank() {
    MemoryStorage libraryStorage;
    libraryStorage.init();

    core::persistence::MacroPersistence persistence(libraryStorage);
    assert(persistence.init());

    core::state::macro::MacroPagesState snapshot;
    configureState(snapshot, 2, 88, 0.75f);
    assert(persistence.saveLibrarySlot(4, snapshot));

    core::state::macro::MacroPagesState loaded;
    loaded.initDefaults();
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = 0,
        .page = 0,
        .macro = 0,
    };
    test_support::project_control::ModulationShape modulation{};
    assert(test_support::project_control::appendModulationPoint(
        modulation,
        0.0f,
        -0.25f
    ));
    assert(test_support::project_control::appendModulationPoint(
        modulation,
        1.0f,
        0.25f
    ));
    assert(test_support::project_control::assignModulation(
        loaded.control,
        address,
        modulation,
        0.75f
    ));

    const auto status = persistence.loadLibrarySlot(4, loaded);
    assert(status == core::persistence::SlotLoadStatus::OK);

    const auto preserved = test_support::project_control::readSlot(
        loaded.control,
        address
    );
    assert(preserved.modulationCount > 0U);
    assert(std::fabs(preserved.primaryModulation.amount - 0.75f) < 0.0001f);

    std::cout << "[PASS] library load preserves Project Control authority\n";
}

void test_library_roundtrip_preserves_sparse_physical_macro_positions() {
    MemoryStorage libraryStorage;
    libraryStorage.init();
    core::persistence::MacroPersistence persistence(libraryStorage);
    assert(persistence.init());

    core::state::macro::MacroPagesState source;
    source.initDefaults();
    auto& page = source.pageData(0U, 0U);
    page.activeMacroMask = 0x29U;  // Physical Macros 1, 4, and 6 only.
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        page.cc[macro] = core::state::macro::defaultMacroCc(0U, macro);
    }
    page.values[0U] = 0.1f;
    page.values[3U] = 0.4f;
    page.values[5U] = 0.6f;
    source.updateActiveConfigs();
    assert(persistence.saveLibrarySlot(7U, source));

    core::state::macro::MacroPagesState restored;
    restored.initDefaults();
    assert(persistence.loadLibrarySlot(7U, restored) ==
           core::persistence::SlotLoadStatus::OK);
    const auto& loaded = restored.pageData(0U, 0U);
    assert(loaded.activeMacroMask == 0x29U);
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        assert(loaded.cc[macro] ==
               core::state::macro::defaultMacroCc(0U, macro));
    }
    assert(std::fabs(loaded.values[0U] - 0.1f) < 0.0001f);
    assert(std::fabs(loaded.values[3U] - 0.4f) < 0.0001f);
    assert(std::fabs(loaded.values[5U] - 0.6f) < 0.0001f);
    std::cout << "[PASS] sparse physical Macro positions survive persistence\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "MacroPersistence tests\n";
    std::cout << "==============================================\n\n";

    test_library_save_load_erase_slot();
    test_library_slot_bounds();
    test_library_load_preserves_project_macro_automation_bank();
    test_library_roundtrip_preserves_sparse_physical_macro_positions();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
