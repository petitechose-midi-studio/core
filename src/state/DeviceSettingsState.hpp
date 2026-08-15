#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

/**
 * Session state for the device settings view.
 *
 * The view keeps selection/focus state here; durable settings live in the
 * domain states and DeviceSettingsStore-backed workflows.
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

    void reset();
};

struct DeviceSettingsState {
    static constexpr uint8_t ROW_COUNT = 5;

    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<DeviceSettingsFlowPhase, 4> flowPhase{
        DeviceSettingsFlowPhase::CLOSED
    };

    DeviceSettingsValueSelectorState selector;

    void reset();
    void openView();
    void closeView();
    void openSelector(uint8_t row, int selectedIndex);
    void closeSelector();
};

}  // namespace core::state
