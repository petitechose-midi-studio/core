#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>

#include "../../src/state/CoreState.hpp"
#include "../../src/state/DataManagerWorkflow.hpp"

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

void test_hide_all_invokes_cleanup_for_stacked_overlays() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    std::vector<core::ui::OverlayType> cleaned;
    auto handle = state.overlays.setCleanupCallbackScoped(
        [&cleaned](core::ui::OverlayType type) { cleaned.push_back(type); }
    );

    state.overlays.show(core::ui::OverlayType::MACRO_EDIT, false);
    state.overlays.show(core::ui::OverlayType::MACRO_EDIT_SELECTOR, true);

    assert(state.macroEdit.visible.get());
    assert(state.macroEdit.selector.visible.get());

    state.overlays.hideAll();

    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());
    assert(cleaned.size() == 2);
    assert(cleaned[0] == core::ui::OverlayType::MACRO_EDIT);
    assert(cleaned[1] == core::ui::OverlayType::MACRO_EDIT_SELECTOR);

    std::cout << "[PASS] test_hide_all_invokes_cleanup_for_stacked_overlays\n";
}

void test_data_manager_reports_deferred_sequencer_pattern_load_while_playing() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.sequencer.length.set(8);
    state.sequencer.stepsPerBeat.set(2);
    state.sequencer.midiChannel.set(1);
    state.sequencer.enabledMask.set(0);
    state.sequencer.setStepDataAt(0, 61, 101, 80);
    state.sequencer.toggle(0);
    state.flush();

    assert(core::state::DataManagerWorkflow::execute(
               state,
               core::state::DataManagerCommand::SEQ_SAVE_PATTERN_SLOT,
               1,
               core::state::DataManagerSetLoadMode::REPLACE
           ).success);

    state.sequencer.length.set(16);
    state.sequencer.stepsPerBeat.set(4);
    state.sequencer.midiChannel.set(6);
    state.sequencer.enabledMask.set(0);
    state.sequencer.setStepDataAt(0, 40, 55, 30);
    state.sequencer.toggle(0);

    state.statusBar.playing.set(true);
    state.sequencer.playheadStep.set(5);

    const auto result = core::state::DataManagerWorkflow::execute(
        state,
        core::state::DataManagerCommand::SEQ_LOAD_PATTERN_SLOT,
        1,
        core::state::DataManagerSetLoadMode::REPLACE
    );

    assert(result.handled);
    assert(result.success);
    assert(result.isLoadOperation);
    assert(result.deferredApply);
    assert(result.loadStatus == core::persistence::SlotLoadStatus::OK);

    // Still live state until next step boundary.
    assert(state.sequencer.length.get() == 16);
    assert(state.sequencer.note[0] == 40);

    state.update();
    assert(state.sequencer.length.get() == 16);
    assert(state.sequencer.note[0] == 40);

    state.sequencer.playheadStep.set(6);
    state.update();
    assert(state.sequencer.length.get() == 8);
    assert(state.sequencer.stepsPerBeat.get() == 2);
    assert(state.sequencer.midiChannel.get() == 1);
    assert(state.sequencer.note[0] == 61);

    std::cout << "[PASS] test_data_manager_reports_deferred_sequencer_pattern_load_while_playing\n";
}

}  // namespace

int main() {
    test_hide_all_invokes_cleanup_for_stacked_overlays();
    test_data_manager_reports_deferred_sequencer_pattern_load_while_playing();
    std::cout << "\nAll CoreState authority tests passed.\n";
    return 0;
}
