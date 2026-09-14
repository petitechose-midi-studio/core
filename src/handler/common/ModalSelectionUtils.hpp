#pragma once

namespace core::handler::modal {

/**
 * Pop only the caller's current overlay. Domain state transitions stay explicit
 * at the call site; a child or unrelated modal must never be unwound here.
 */
template <typename OverlayManager, typename OverlayEnum>
inline bool hideIfCurrent(OverlayManager& overlays, OverlayEnum overlay) {
    if (overlays.current() != overlay) {
        return false;
    }
    overlays.hide();
    return true;
}

}  // namespace core::handler::modal
