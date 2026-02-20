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

struct CoreStorages {
    MemoryStorage settings;
    MemoryStorage macroWorkspace;
    MemoryStorage macroLibrary;
    MemoryStorage sequencerWorkspace;
    MemoryStorage sequencerPatternLibrary;
    MemoryStorage sequencerSetLibrary;

    void initAll() {
        settings.init();
        macroWorkspace.init();
        macroLibrary.init();
        sequencerWorkspace.init();
        sequencerPatternLibrary.init();
        sequencerSetLibrary.init();
    }
};

void drainNotifications() {
    auto& queue = oc::state::NotificationQueue::instance();
    while (queue.hasPending()) {
        queue.flush();
    }
}

void test_workspace_survives_legacy_corruption() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroWorkspace,
                                     storage.macroLibrary,
                                     storage.sequencerWorkspace,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);
        state.setMacroValue(0, 0.13f);
        state.setMacroValue(1, 0.87f);
        oc::state::NotificationQueue::instance().flush();
        state.flush();
    }

    // Corrupt legacy settings storage only.
    storage.settings.erase(0, storage.settings.capacity());

    core::state::CoreState restored(storage.settings,
                                    storage.macroWorkspace,
                                    storage.macroLibrary,
                                    storage.sequencerWorkspace,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    assert(restored.getMacroValue(0) == 0.13f);
    assert(restored.getMacroValue(1) == 0.87f);

    drainNotifications();

    std::cout << "[PASS] test_workspace_survives_legacy_corruption\n";
}

void test_macro_library_roundtrip_and_erase() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
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

    drainNotifications();

    std::cout << "[PASS] test_macro_library_roundtrip_and_erase\n";
}

void test_sequencer_workspace_and_library_roundtrip() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroWorkspace,
                                     storage.macroLibrary,
                                     storage.sequencerWorkspace,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);

        state.sequencer.length.set(16);
        state.sequencer.stepsPerBeat.set(4);
        state.sequencer.midiChannel.set(3);
        state.sequencer.toggle(0);
        state.sequencer.setStepDataAt(0, 64, 120, 70);

        oc::state::NotificationQueue::instance().flush();
        state.flush();

        assert(state.saveSequencerPatternSlot(4));
        assert(state.saveSequencerSetSlot(2));

        state.sequencer.length.set(8);
        state.sequencer.stepsPerBeat.set(2);
        state.sequencer.midiChannel.set(0);
        state.sequencer.enabledMask.set(0);
        state.sequencer.setStepDataAt(0, 40, 40, 40);
        oc::state::NotificationQueue::instance().flush();
        state.flush();

        const auto patternStatus = state.loadSequencerPatternSlot(4);
        assert(patternStatus == core::persistence::SlotLoadStatus::OK);
        assert(state.sequencer.length.get() == 16);
        assert(state.sequencer.stepsPerBeat.get() == 4);
        assert(state.sequencer.midiChannel.get() == 3);
        assert(state.sequencer.isEnabled(0));
        assert(state.sequencer.note[0] == 64);
        assert(state.sequencer.velocity[0] == 120);
        assert(state.sequencer.gate[0] == 70);

        assert(state.eraseSequencerPatternSlot(4));
        const auto erasedPatternStatus = state.loadSequencerPatternSlot(4);
        assert(erasedPatternStatus == core::persistence::SlotLoadStatus::EMPTY);

        const auto setStatus = state.loadSequencerSetSlot(2);
        assert(setStatus == core::persistence::SlotLoadStatus::OK);
        assert(state.sequencer.length.get() == 16);
        assert(state.sequencer.stepsPerBeat.get() == 4);
        assert(state.sequencer.midiChannel.get() == 3);

        assert(state.eraseSequencerSetSlot(2));
        const auto erasedSetStatus = state.loadSequencerSetSlot(2);
        assert(erasedSetStatus == core::persistence::SlotLoadStatus::EMPTY);
    }

    // Corrupt legacy settings storage only and verify sequencer workspace restores.
    storage.settings.erase(0, storage.settings.capacity());

    core::state::CoreState restored(storage.settings,
                                    storage.macroWorkspace,
                                    storage.macroLibrary,
                                    storage.sequencerWorkspace,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    assert(restored.sequencer.length.get() == 16);
    assert(restored.sequencer.stepsPerBeat.get() == 4);
    assert(restored.sequencer.midiChannel.get() == 3);
    assert(restored.sequencer.isEnabled(0));
    assert(restored.sequencer.note[0] == 64);
    assert(restored.sequencer.velocity[0] == 120);
    assert(restored.sequencer.gate[0] == 70);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_workspace_and_library_roundtrip\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "CoreState persistence tests\n";
    std::cout << "==============================================\n\n";

    test_workspace_survives_legacy_corruption();
    test_macro_library_roundtrip_and_erase();
    test_sequencer_workspace_and_library_roundtrip();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
