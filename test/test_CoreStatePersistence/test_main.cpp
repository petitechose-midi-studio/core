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

void test_macro_library_save_snapshots_runtime_values_without_manual_flush() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Change runtime macro value and save immediately (without NotificationQueue/state flush).
    state.setMacroValue(0, 0.37f);
    assert(state.saveMacroLibrarySlot(6));

    // Move away from that value so load verification is unambiguous.
    state.setMacroValue(0, 0.02f);
    oc::state::NotificationQueue::instance().flush();
    state.flush();

    const auto status = state.loadMacroLibrarySlot(6);
    assert(status == core::persistence::SlotLoadStatus::OK);

    const float restored = state.getMacroValue(0);
    assert(restored > 0.3699f && restored < 0.3701f);

    drainNotifications();

    std::cout << "[PASS] test_macro_library_save_snapshots_runtime_values_without_manual_flush\n";
}

void test_data_manager_shortcuts_persist_and_sanitize() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroWorkspace,
                                     storage.macroLibrary,
                                     storage.sequencerWorkspace,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);

        state.setDataManagerShortcut(core::state::DataManagerContext::MACRO,
                                     true,
                                     core::state::DataManagerCommand::MACRO_ERASE_SLOT);
        // Cross-context mapping should sanitize to macro default right shortcut.
        state.setDataManagerShortcut(core::state::DataManagerContext::MACRO,
                                     false,
                                     core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT);

        state.setDataManagerShortcut(core::state::DataManagerContext::SEQUENCER,
                                     true,
                                     core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT);
        state.setDataManagerShortcut(core::state::DataManagerContext::SEQUENCER,
                                     false,
                                     core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT);
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroWorkspace,
                                    storage.macroLibrary,
                                    storage.sequencerWorkspace,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);

    assert(restored.dataManager.macroShortcutLeft.get() == core::state::DataManagerCommand::MACRO_ERASE_SLOT);
    assert(restored.dataManager.macroShortcutRight.get() == core::state::DEFAULT_MACRO_SHORTCUT_RIGHT);
    assert(restored.dataManager.seqShortcutLeft.get() == core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT);
    assert(restored.dataManager.seqShortcutRight.get() == core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT);

    drainNotifications();

    std::cout << "[PASS] test_data_manager_shortcuts_persist_and_sanitize\n";
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

void test_sequencer_load_is_quantized_to_next_step_when_playing() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Pattern A (will be saved and reloaded)
    state.sequencer.length.set(8);
    state.sequencer.stepsPerBeat.set(2);
    state.sequencer.midiChannel.set(1);
    state.sequencer.enabledMask.set(0);
    state.sequencer.setStepDataAt(0, 61, 101, 80);
    state.sequencer.toggle(0);
    oc::state::NotificationQueue::instance().flush();
    state.flush();
    assert(state.saveSequencerPatternSlot(1));

    // Pattern B (current live state before queued load)
    state.sequencer.length.set(16);
    state.sequencer.stepsPerBeat.set(4);
    state.sequencer.midiChannel.set(6);
    state.sequencer.enabledMask.set(0);
    state.sequencer.setStepDataAt(0, 40, 55, 30);
    state.sequencer.toggle(0);

    state.statusBar.playing.set(true);
    state.sequencer.playheadStep.set(5);

    const auto queuedStatus = state.loadSequencerPatternSlot(1);
    assert(queuedStatus == core::persistence::SlotLoadStatus::OK);

    // Load is deferred: no immediate replacement while still on same step.
    assert(state.sequencer.length.get() == 16);
    assert(state.sequencer.note[0] == 40);

    state.update();
    assert(state.sequencer.length.get() == 16);
    assert(state.sequencer.note[0] == 40);

    // Next step reached -> queued apply must happen.
    state.sequencer.playheadStep.set(6);
    state.update();
    assert(state.sequencer.length.get() == 8);
    assert(state.sequencer.stepsPerBeat.get() == 2);
    assert(state.sequencer.midiChannel.get() == 1);
    assert(state.sequencer.note[0] == 61);
    assert(state.sequencer.velocity[0] == 101);
    assert(state.sequencer.gate[0] == 80);

    // Same behavior for set library loads.
    assert(state.saveSequencerSetSlot(2));

    state.sequencer.length.set(12);
    state.sequencer.setStepDataAt(0, 77, 77, 77);
    state.sequencer.playheadStep.set(9);

    const auto queuedSetStatus = state.loadSequencerSetSlot(2);
    assert(queuedSetStatus == core::persistence::SlotLoadStatus::OK);
    assert(state.sequencer.length.get() == 12);
    assert(state.sequencer.note[0] == 77);

    state.sequencer.playheadStep.set(10);
    state.update();
    assert(state.sequencer.length.get() == 8);
    assert(state.sequencer.note[0] == 61);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_load_is_quantized_to_next_step_when_playing\n";
}

void test_sequencer_set_load_merge_preserves_existing_steps() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Incoming set snapshot: step 0 and step 3 enabled.
    state.sequencer.length.set(8);
    state.sequencer.stepsPerBeat.set(2);
    state.sequencer.midiChannel.set(1);
    state.sequencer.enabledMask.set(0);
    state.sequencer.setStepDataAt(0, 61, 101, 80);
    state.sequencer.toggle(0);
    state.sequencer.setStepDataAt(3, 65, 99, 70);
    state.sequencer.toggle(3);
    assert(state.saveSequencerSetSlot(4));

    // Live pattern before merge: longer length + existing step 1 enabled.
    state.sequencer.length.set(16);
    state.sequencer.stepsPerBeat.set(4);
    state.sequencer.midiChannel.set(6);
    state.sequencer.enabledMask.set(0);
    state.sequencer.setStepDataAt(1, 44, 55, 66);
    state.sequencer.toggle(1);

    const auto status = state.loadSequencerSetSlot(4, true);
    assert(status == core::persistence::SlotLoadStatus::OK);

    // Merge keeps current transport config and length, overlays incoming enabled steps only.
    assert(state.sequencer.length.get() == 16);
    assert(state.sequencer.stepsPerBeat.get() == 4);
    assert(state.sequencer.midiChannel.get() == 6);

    assert(state.sequencer.note[0] == 61);
    assert(state.sequencer.velocity[0] == 101);
    assert(state.sequencer.gate[0] == 80);

    // Existing enabled step remains untouched if incoming did not enable it.
    assert(state.sequencer.note[1] == 44);
    assert(state.sequencer.velocity[1] == 55);
    assert(state.sequencer.gate[1] == 66);

    assert(state.sequencer.note[3] == 65);
    assert(state.sequencer.velocity[3] == 99);
    assert(state.sequencer.gate[3] == 70);

    assert((state.sequencer.enabledMask.get() & (1ULL << 0)) != 0);
    assert((state.sequencer.enabledMask.get() & (1ULL << 1)) != 0);
    assert((state.sequencer.enabledMask.get() & (1ULL << 3)) != 0);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_set_load_merge_preserves_existing_steps\n";
}

void test_sequencer_set_load_merge_is_quantized_when_playing() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Prepare incoming set with only step 2 enabled.
    state.sequencer.length.set(8);
    state.sequencer.enabledMask.set(0);
    state.sequencer.setStepDataAt(2, 72, 110, 45);
    state.sequencer.toggle(2);
    assert(state.saveSequencerSetSlot(6));

    // Live state before queued merge.
    state.sequencer.length.set(16);
    state.sequencer.enabledMask.set(0);
    state.sequencer.setStepDataAt(1, 48, 64, 55);
    state.sequencer.toggle(1);
    state.sequencer.playheadStep.set(7);
    state.statusBar.playing.set(true);

    const auto status = state.loadSequencerSetSlot(6, true);
    assert(status == core::persistence::SlotLoadStatus::OK);

    // Same-step update must stay deferred.
    state.update();
    assert(state.sequencer.note[2] != 72 || (state.sequencer.enabledMask.get() & (1ULL << 2)) == 0);

    // Next step triggers queued merge.
    state.sequencer.playheadStep.set(8);
    state.update();
    assert(state.sequencer.length.get() == 16);
    assert(state.sequencer.note[2] == 72);
    assert(state.sequencer.velocity[2] == 110);
    assert(state.sequencer.gate[2] == 45);
    assert((state.sequencer.enabledMask.get() & (1ULL << 1)) != 0);
    assert((state.sequencer.enabledMask.get() & (1ULL << 2)) != 0);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_set_load_merge_is_quantized_when_playing\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "CoreState persistence tests\n";
    std::cout << "==============================================\n\n";

    test_workspace_survives_legacy_corruption();
    test_macro_library_roundtrip_and_erase();
    test_macro_library_save_snapshots_runtime_values_without_manual_flush();
    test_data_manager_shortcuts_persist_and_sanitize();
    test_sequencer_workspace_and_library_roundtrip();
    test_sequencer_load_is_quantized_to_next_step_when_playing();
    test_sequencer_set_load_merge_preserves_existing_steps();
    test_sequencer_set_load_merge_is_quantized_when_playing();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
