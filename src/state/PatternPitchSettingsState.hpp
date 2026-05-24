#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

enum class PatternPitchSettingsFlowPhase : uint8_t {
    CLOSED = 0,
    OVERLAY = 1,
    VALUE_SELECTOR = 2,
};

struct PatternPitchSettingsValueSelectorState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> editingRow{0};
    oc::state::Signal<int> selectedIndex{0};

    void reset() {
        visible.set(false);
        editingRow.set(0);
        selectedIndex.set(0);
    }
};

struct PatternPitchSettingsState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<PatternPitchSettingsFlowPhase, 4> flowPhase{
        PatternPitchSettingsFlowPhase::CLOSED
    };
    PatternPitchSettingsValueSelectorState selector;

    void reset() {
        visible.set(false);
        focusedRow.set(0);
        flowPhase.set(PatternPitchSettingsFlowPhase::CLOSED);
        selector.reset();
    }

    void openOverlay() {
        reset();
        visible.set(true);
        flowPhase.set(PatternPitchSettingsFlowPhase::OVERLAY);
    }

    void closeOverlay() {
        reset();
    }

    void openSelector(uint8_t row, int selected) {
        selector.editingRow.set(row);
        selector.selectedIndex.set(selected);
        selector.visible.set(true);
        flowPhase.set(PatternPitchSettingsFlowPhase::VALUE_SELECTOR);
    }

    void closeSelector() {
        selector.reset();
        flowPhase.set(visible.get() ? PatternPitchSettingsFlowPhase::OVERLAY
                                    : PatternPitchSettingsFlowPhase::CLOSED);
    }
};

}  // namespace core::state
