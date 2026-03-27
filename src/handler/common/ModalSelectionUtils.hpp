#pragma once

#include "handler/common/NavigationUtils.hpp"

namespace core::handler::modal {

inline bool advanceWrappedSelection(float delta,
                                    bool visible,
                                    int current,
                                    int count,
                                    int& next) {
    if (!visible || !nav::hasTurnDelta(delta) || count <= 0) {
        return false;
    }

    next = nav::nextWrappedIndex(delta, current, count);
    return true;
}

template <typename OverlayManager, typename ResetFn>
inline void hideOverlayAndReset(OverlayManager& overlays, ResetFn&& reset) {
    overlays.hide();
    reset();
}

}  // namespace core::handler::modal
