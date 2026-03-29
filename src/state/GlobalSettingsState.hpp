#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

struct GlobalSettingsValueSelectorState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<int> selectedIndex{0};
    oc::state::Signal<uint8_t> editingRow{0};

    void reset() {
        visible.set(false);
        selectedIndex.set(0);
        editingRow.set(0);
    }
};

struct GlobalSettingsState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};

    GlobalSettingsValueSelectorState selector;

    void reset() {
        visible.set(false);
        focusedRow.set(0);
        selector.reset();
    }
};

}  // namespace core::state
