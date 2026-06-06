#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

/**
 * Session state for the device settings view.
 *
 * The view keeps selection/focus state here; durable settings live in the
 * domain states and CoreSettings-backed workflows.
 */
enum class DeviceSettingsFlowPhase : uint8_t {
    CLOSED = 0,
    VIEW = 1,
    VALUE_SELECTOR = 2,
};

struct DeviceSettingsValueSelectorState {
    oc::state::Signal<bool, 4> visible{false};
    oc::state::Signal<int, 4> selectedIndex{0};
    oc::state::Signal<uint8_t, 4> editingRow{0};

    void reset() {
        visible.set(false);
        selectedIndex.set(0);
        editingRow.set(0);
    }
};

struct DeviceSettingsState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<DeviceSettingsFlowPhase, 4> flowPhase{
        DeviceSettingsFlowPhase::CLOSED
    };

    DeviceSettingsValueSelectorState selector;

    void reset() {
        visible.set(false);
        focusedRow.set(0);
        flowPhase.set(DeviceSettingsFlowPhase::CLOSED);
        selector.reset();
    }

    void openView() {
        reset();
        visible.set(true);
        flowPhase.set(DeviceSettingsFlowPhase::VIEW);
    }

    void closeView() {
        reset();
    }

    void openSelector(uint8_t row, int selectedIndex) {
        visible.set(true);
        selector.visible.set(true);
        selector.editingRow.set(row);
        selector.selectedIndex.set(selectedIndex);
        flowPhase.set(DeviceSettingsFlowPhase::VALUE_SELECTOR);
    }

    void closeSelector() {
        selector.reset();
        flowPhase.set(visible.get() ? DeviceSettingsFlowPhase::VIEW
                                    : DeviceSettingsFlowPhase::CLOSED);
    }
};

}  // namespace core::state
