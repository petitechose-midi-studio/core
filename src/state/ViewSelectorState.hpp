#pragma once

#include <oc/state/Signal.hpp>

#include "app/ViewTypes.hpp"

namespace core::state {

/**
 * @brief State for top-level view selector overlay
 */
struct ViewSelectorState {
    oc::state::Signal<int, 4> selectedIndex{0};
    oc::state::Signal<bool, 8> visible{false};

    void reset() {
        selectedIndex.set(0);
        visible.set(false);
    }
};

}  // namespace core::state
