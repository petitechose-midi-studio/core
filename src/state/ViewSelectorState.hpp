#pragma once

#include <oc/state/Signal.hpp>

#include "ui/ViewTypes.hpp"

namespace core::state {

/**
 * @brief State for top-level view selector overlay
 */
struct ViewSelectorState {
    oc::state::Signal<int> selectedIndex{0};
    oc::state::Signal<bool> visible{false};

    void reset() {
        selectedIndex.set(0);
        visible.set(false);
    }
};

}  // namespace core::state
