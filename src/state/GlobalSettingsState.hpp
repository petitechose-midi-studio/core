#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

/**
 * Session state for the global settings overlay.
 *
 * The overlay keeps selection/focus state here; durable settings live in the
 * domain states and CoreSettings-backed workflows.
 */
enum class GlobalSettingsFlowPhase : uint8_t {
    CLOSED = 0,
    OVERLAY = 1,
    VALUE_SELECTOR = 2,
};

struct GlobalSettingsValueSelectorState {
    oc::state::Signal<bool, 8> visible{false};
    oc::state::Signal<int, 4> selectedIndex{0};
    oc::state::Signal<uint8_t, 4> editingRow{0};

    void reset() {
        visible.set(false);
        selectedIndex.set(0);
        editingRow.set(0);
    }
};

struct GlobalSettingsState {
    oc::state::Signal<bool, 8> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<GlobalSettingsFlowPhase, 4> flowPhase{
        GlobalSettingsFlowPhase::CLOSED
    };

    GlobalSettingsValueSelectorState selector;

    void reset() {
        visible.set(false);
        focusedRow.set(0);
        flowPhase.set(GlobalSettingsFlowPhase::CLOSED);
        selector.reset();
    }

    void openOverlay() {
        reset();
        visible.set(true);
        flowPhase.set(GlobalSettingsFlowPhase::OVERLAY);
    }

    void closeOverlay() {
        reset();
    }

    void openSelector(uint8_t row, int selectedIndex) {
        visible.set(true);
        selector.visible.set(true);
        selector.editingRow.set(row);
        selector.selectedIndex.set(selectedIndex);
        flowPhase.set(GlobalSettingsFlowPhase::VALUE_SELECTOR);
    }

    void closeSelector() {
        selector.reset();
        flowPhase.set(visible.get() ? GlobalSettingsFlowPhase::OVERLAY
                                    : GlobalSettingsFlowPhase::CLOSED);
    }
};

}  // namespace core::state
