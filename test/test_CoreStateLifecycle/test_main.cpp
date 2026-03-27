#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>
#include <oc/time/Time.hpp>

#include "../../src/state/CoreState.hpp"

namespace {

uint32_t g_mock_now_ms = 0;

uint32_t mockTimeMs() {
    return g_mock_now_ms;
}

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

void test_overlay_registration_supports_stacking_and_restore() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());

    state.overlays.show(core::ui::OverlayType::MACRO_EDIT, false);
    assert(state.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
    assert(state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());

    state.overlays.show(core::ui::OverlayType::MACRO_EDIT_SELECTOR, true);
    assert(state.overlays.current() == core::ui::OverlayType::MACRO_EDIT_SELECTOR);
    assert(state.macroEdit.visible.get());
    assert(state.macroEdit.selector.visible.get());

    state.overlays.hide();
    assert(state.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
    assert(state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());

    state.overlays.hide();
    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());

    std::cout << "[PASS] test_overlay_registration_supports_stacking_and_restore\n";
}

void test_factory_reset_clears_transient_state_and_overlays() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.dataManager.resetSession(core::state::DataManagerContext::SEQUENCER);
    state.dataManager.feedback.set("busy");
    state.sequencer.stepInlineFeedback.show(
        3,
        core::state::sequencer::StepProperty::VELOCITY,
        0
    );
    state.sequencer.patternQuickControls.selecting.set(true);
    state.overlays.show(core::ui::OverlayType::DATA_MANAGER, false);
    state.overlays.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);

    const uint32_t beforeRevision = state.configRevision.get();
    state.factoryReset();

    assert(state.activeView.get() == core::ui::ViewType::MACRO);
    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(!state.dataManager.visible.get());
    assert(!state.dataManager.dialog.visible.get());
    assert(!state.sequencer.stepInlineFeedback.visible.get());
    assert(!state.sequencer.patternQuickControls.selecting.get());
    assert(std::strcmp(state.dataManager.feedback.get(), "") == 0);
    assert(std::strcmp(state.statusBar.pageName.get(), state.pages.activePageData().name) == 0);
    assert(state.configRevision.get() == beforeRevision + 1);

    std::cout << "[PASS] test_factory_reset_clears_transient_state_and_overlays\n";
}

void test_core_state_update_expires_inline_feedback() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    g_mock_now_ms = 1000;
    state.sequencer.stepInlineFeedback.show(
        2,
        core::state::sequencer::StepProperty::PROBABILITY,
        g_mock_now_ms
    );
    assert(state.sequencer.stepInlineFeedback.visible.get());

    g_mock_now_ms += core::state::sequencer::SequencerStepInlineFeedbackState::DISPLAY_HOLD_MS - 1;
    state.update();
    assert(state.sequencer.stepInlineFeedback.visible.get());

    g_mock_now_ms += 1;
    state.update();
    assert(!state.sequencer.stepInlineFeedback.visible.get());

    std::cout << "[PASS] test_core_state_update_expires_inline_feedback\n";
}

void test_core_state_update_expires_status_bar_pulses() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    g_mock_now_ms = 2000;
    state.statusBar.pulseNoteIn();
    state.statusBar.pulseSyncInput();
    state.statusBar.pulseBeat();

    assert(state.statusBar.noteInActive.get());
    assert(state.statusBar.syncInputPulse.get());
    assert(state.statusBar.beatPulse.get());

    g_mock_now_ms += 40;
    state.statusBar.pulseNoteIn();

    g_mock_now_ms = 2079;
    state.update();
    assert(state.statusBar.noteInActive.get());
    assert(state.statusBar.syncInputPulse.get());
    assert(state.statusBar.beatPulse.get());

    g_mock_now_ms = 2080;
    state.update();
    assert(state.statusBar.noteInActive.get());
    assert(!state.statusBar.syncInputPulse.get());
    assert(state.statusBar.beatPulse.get());

    g_mock_now_ms = 2119;
    state.update();
    assert(state.statusBar.noteInActive.get());
    assert(!state.statusBar.beatPulse.get());

    g_mock_now_ms = 2120;
    state.update();
    assert(!state.statusBar.noteInActive.get());
    assert(!state.statusBar.beatPulse.get());

    std::cout << "[PASS] test_core_state_update_expires_status_bar_pulses\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_overlay_registration_supports_stacking_and_restore();
    test_factory_reset_clears_transient_state_and_overlays();
    test_core_state_update_expires_inline_feedback();
    test_core_state_update_expires_status_bar_pulses();
    std::cout << "\nAll CoreState lifecycle tests passed.\n";
    return 0;
}
